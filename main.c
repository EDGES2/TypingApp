#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h> // Потрібно для system()
#include <string.h>
#include <stdbool.h>
#include <ctype.h> // For iscntrl, isspace
#include <SDL2/SDL_filesystem.h> // For SDL_GetBasePath()
#include <errno.h> // Замінено sys/errno.h на errno.h для кращої портативності (особливо для MSVC)
#include <time.h> // For timestamp in stats

// --- Application Constants ---
#define WINDOW_W     800
#define WINDOW_H     200
#define FONT_SIZE    14
#define MAX_TEXT_LEN (5 * 1024 * 1024)

#ifndef TEXT_FILE_PATH_BASENAME
#define TEXT_FILE_PATH_BASENAME "text.txt"
#endif
#ifndef STATS_FILE_BASENAME
#define STATS_FILE_BASENAME "stats.txt"
#endif

// Ці визначення будуть замінені значеннями з CMake, якщо вони там вказані.
// Якщо компілюється без CMake або там не визначено, використовуються ці значення за замовчуванням.
#ifndef PROJECT_NAME_STR // Використовується для SDL_GetPrefPath та шляхів за замовчуванням
#define PROJECT_NAME_STR "TypingApp"
#endif
#ifndef COMPANY_NAME_STR // Використовується для SDL_GetPrefPath
#define COMPANY_NAME_STR "com.typingapp" // Оновлено для узгодження з CMakeLists.txt та логом збірки
#endif


#define TEXT_AREA_X 10
#define TEXT_AREA_PADDING_Y 10
#define TEXT_AREA_W (WINDOW_W - (2 * TEXT_AREA_X))
#define DISPLAY_LINES 3
#define CURSOR_TARGET_VIEWPORT_LINE 1
#define TAB_SIZE_IN_SPACES 4

// Встановіть в 1, щоб увімкнути логування у файл.
// Файл логів буде створено у теці налаштувань користувача.
#define ENABLE_GAME_LOGS 0

enum { COL_BG, COL_TEXT, COL_CORRECT, COL_INCORRECT, COL_CURSOR, N_COLORS };

typedef struct {
    SDL_Window *win;
    SDL_Renderer *ren;
    TTF_Font *font;
    SDL_Color palette[N_COLORS];
    int line_h;
    SDL_Texture *glyph_tex_cache[N_COLORS][128];
    int glyph_adv_cache[128];
    int glyph_w_cache[N_COLORS][128];
    int glyph_h_cache[N_COLORS][128];
    int space_advance_width;
    int tab_width_pixels;
} AppContext;

typedef struct {
    const char* start_ptr;
    size_t num_bytes;
    int pixel_width;
    bool is_word;
    bool is_newline;
    bool is_tab;
} TextBlockInfo;

static int first_visible_abs_line_num_static = 0;
static bool typing_started_main = false;
static FILE *log_file = NULL;
static unsigned long long total_keystrokes_for_accuracy = 0;
static unsigned long long total_errors_committed_for_accuracy = 0;
static bool predictive_scroll_triggered_for_this_input_idx = false;
static int y_offset_due_to_prediction_for_current_idx = 0;

static bool is_paused = false;
static Uint32 time_at_pause_ms = 0;
static bool l_modifier_held = false;
static bool r_modifier_held = false;

// --- MODIFICATION: Шляхи до файлів даних користувача ---
static char actual_text_file_path[1024];
static char actual_stats_file_path[1024];
static char default_text_file_in_bundle[1024]; // Шлях до text.txt за замовчуванням
// --- END MODIFICATION ---


Sint32 decode_utf8(const char **s_ptr, const char *s_end_const_char) {
    if (!s_ptr || !*s_ptr || *s_ptr >= s_end_const_char) return 0;
    const unsigned char *s = (const unsigned char *)*s_ptr;
    const unsigned char *s_end = (const unsigned char *)s_end_const_char;
    unsigned char c1 = *s;
    Sint32 codepoint;
    int len = 0;

    if (c1 < 0x80) { codepoint = c1; len = 1; }
    else if ((c1 & 0xE0) == 0xC0) {
        if (s + 1 >= s_end || (s[1] & 0xC0) != 0x80) return -1;
        codepoint = ((Sint32)(c1 & 0x1F) << 6) | (Sint32)(s[1] & 0x3F); len = 2;
    } else if ((c1 & 0xF0) == 0xE0) {
        if (s + 2 >= s_end || (s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80) return -1;
        codepoint = ((Sint32)(c1 & 0x0F) << 12) | ((Sint32)(s[1] & 0x3F) << 6) | (Sint32)(s[2] & 0x3F); len = 3;
    } else if ((c1 & 0xF8) == 0xF0) {
        if (s + 3 >= s_end || (s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 || (s[3] & 0xC0) != 0x80) return -1;
        codepoint = ((Sint32)(c1 & 0x07) << 18) | ((Sint32)(s[1] & 0x3F) << 12) | ((Sint32)(s[2] & 0x3F) << 6) | (Sint32)(s[3] & 0x3F); len = 4;
    } else { return -1; }
    (*s_ptr) += len;
    return codepoint;
}

size_t CountUTF8Chars(const char* text, size_t text_byte_len) {
    size_t char_count = 0;
    const char* p = text;
    const char* end = text + text_byte_len;
    while (p < end) {
        const char* char_start = p;
        Sint32 cp = decode_utf8(&p, end);
        if (p > char_start) {
             if (cp > 0) { char_count++; }
             else if (cp == 0 && p < end) { char_count++; }
             else if (cp == -1) { char_count++; }
             else if (cp == 0 && p == end) { break; }
        } else {
            if (p < end) p++; else break;
        }
    }
    return char_count;
}

int get_codepoint_advance_and_metrics_func(AppContext *appCtx, Uint32 codepoint, int fallback_adv, int *out_char_w, int *out_char_h) {
    int adv = 0; int char_w = 0; int char_h = 0;
    if (!appCtx || !appCtx->font) { if (out_char_w) *out_char_w = fallback_adv; if (out_char_h) *out_char_h = FONT_SIZE; return fallback_adv; }
    char_h = TTF_FontHeight(appCtx->font);
    if (codepoint < 128 && codepoint >= 32) {
        adv = appCtx->glyph_adv_cache[codepoint];
        if (appCtx->glyph_w_cache[COL_TEXT][codepoint] > 0) char_w = appCtx->glyph_w_cache[COL_TEXT][codepoint]; else char_w = adv;
        if (appCtx->glyph_h_cache[COL_TEXT][codepoint] > 0) char_h = appCtx->glyph_h_cache[COL_TEXT][codepoint]; else char_h = TTF_FontHeight(appCtx->font);
        if (adv == 0 && fallback_adv > 0) adv = fallback_adv;
    } else {
        if (TTF_GlyphMetrics32(appCtx->font, codepoint, NULL, NULL, NULL, NULL, &adv) != 0) {
            adv = fallback_adv;
        }
        char_w = adv;
    }
    if (adv <= 0 && codepoint != '\n' && codepoint != '\t') adv = fallback_adv;
    if (out_char_w) *out_char_w = (char_w > 0) ? char_w : adv;
    if (out_char_h) *out_char_h = (char_h > 0) ? char_h : TTF_FontHeight(appCtx->font);
    return adv;
}

TextBlockInfo get_next_text_block_func(AppContext *appCtx, const char **text_parser_ptr_ref, const char *text_end, int current_pen_x_for_tab_calc) {
    TextBlockInfo block = {0};
    if (!text_parser_ptr_ref || !*text_parser_ptr_ref || *text_parser_ptr_ref >= text_end || !appCtx || !appCtx->font) { return block; }

    block.start_ptr = *text_parser_ptr_ref;
    const char *p_initial_for_block = *text_parser_ptr_ref;
    const char *temp_scanner = *text_parser_ptr_ref;

    Sint32 first_cp_in_block = decode_utf8(&temp_scanner, text_end);

    if (first_cp_in_block <= 0) {
        if (*text_parser_ptr_ref == p_initial_for_block && *text_parser_ptr_ref < text_end) {
            (*text_parser_ptr_ref)++;
        } else if (temp_scanner > *text_parser_ptr_ref) {
            *text_parser_ptr_ref = temp_scanner;
        }
        block.num_bytes = (size_t)(*text_parser_ptr_ref - block.start_ptr);
        return block;
    }

    if (first_cp_in_block == '\n') {
        block.is_newline = true;
        decode_utf8(text_parser_ptr_ref, text_end);
        block.pixel_width = 0;
    } else if (first_cp_in_block == '\t') {
        block.is_tab = true;
        decode_utf8(text_parser_ptr_ref, text_end);
        if (appCtx->tab_width_pixels > 0) {
            int offset_in_line = current_pen_x_for_tab_calc - TEXT_AREA_X;
            block.pixel_width = appCtx->tab_width_pixels - (offset_in_line % appCtx->tab_width_pixels);
            if (block.pixel_width == 0 && offset_in_line >=0) block.pixel_width = appCtx->tab_width_pixels;
            if (block.pixel_width <=0) block.pixel_width = appCtx->tab_width_pixels;
        } else {
            block.pixel_width = appCtx->space_advance_width * TAB_SIZE_IN_SPACES;
        }
    } else {
        bool first_char_was_space = (first_cp_in_block == ' ');
        block.is_word = !first_char_was_space;

        while(*text_parser_ptr_ref < text_end) {
            const char* peek_ptr = *text_parser_ptr_ref;
            Sint32 cp = decode_utf8(&peek_ptr, text_end);

            if (cp <= 0 || cp == '\n' || cp == '\t') {
                break;
            }
            bool current_char_is_space = (cp == ' ');
            if (current_char_is_space != first_char_was_space) {
                break;
            }
            *text_parser_ptr_ref = peek_ptr;
            block.pixel_width += get_codepoint_advance_and_metrics_func(appCtx, (Uint32)cp, appCtx->space_advance_width, NULL, NULL);
        }
    }
    block.num_bytes = (size_t)(*text_parser_ptr_ref - block.start_ptr);
    return block;
}

bool InitializeApp(AppContext *appCtx, const char* title) {
    if (!appCtx) return false; memset(appCtx, 0, sizeof(AppContext));
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError()); return false; }
    if (TTF_Init() != 0) { fprintf(stderr, "TTF_Init error: %s\n", TTF_GetError()); SDL_Quit(); return false; }
    TTF_SetFontHinting(NULL, TTF_HINTING_LIGHT);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");
    appCtx->win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_W, WINDOW_H, SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!appCtx->win) { fprintf(stderr, "SDL_CreateWindow error: %s\n", SDL_GetError()); TTF_Quit(); SDL_Quit(); return false; }
    appCtx->ren = SDL_CreateRenderer(appCtx->win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!appCtx->ren) { fprintf(stderr, "SDL_CreateRenderer error: %s\n", SDL_GetError()); SDL_DestroyWindow(appCtx->win); TTF_Quit(); SDL_Quit(); return false; }
    int physW_val, physH_val; SDL_GetRendererOutputSize(appCtx->ren, &physW_val, &physH_val);
    float scale_x = (float)physW_val / WINDOW_W; float scale_y = (float)physH_val / WINDOW_H; SDL_RenderSetScale(appCtx->ren, scale_x, scale_y);
    const char* font_paths[] = {
        // Пріоритет для шрифтів, що розповсюджуються з програмою (мають бути в робочому каталозі)
        "Arial Unicode.ttf", // Спробуйте цей першим, якщо ви його додаєте до проєкту
        "arial.ttf",         // Стандартний Arial як резервний варіант поруч з exe

        // Системні шляхи для Windows (додано)
#ifdef _WIN32
        "C:/Windows/Fonts/arialuni.ttf", // Arial Unicode MS (якщо встановлено)
        "C:/Windows/Fonts/arial.ttf",    // Стандартний Arial
        "C:/Windows/Fonts/verdana.ttf",  // Verdana як альтернатива
        "C:/Windows/Fonts/tahoma.ttf",   // Tahoma як альтернатива
        "C:/Windows/Fonts/times.ttf",    // Times New Roman як базова альтернатива
        "C:/Windows/Fonts/consola.ttf",  // Consolas (моноширинний, може бути корисним)
#endif
        // Системні шляхи для macOS (залишено з оригіналу)
        "/System/Library/Fonts/Arial Unicode.ttf", // [ייט: המשתמש הציג את התוכן הזה]
        "/System/Library/Fonts/Arial.ttf",         // [ייט: המשתמש הציג את התוכן הזה]
        "/Library/Fonts/Arial Unicode.ttf",          // [ייט: המשתמש הציג את התוכן הזה]

        // Системні шляхи для Linux (залишено з оригіналу)
        "/usr/share/fonts/truetype/msttcorefonts/Arial.ttf",                 // [ייט: המשתמש הציג את התוכן הזה]
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",                  // [ייט: המשתמש הציג את התוכן הזה]
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", // [ייט: המשתמש הציג את התוכן הזה]
        "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",                    // Ubuntu
        "/usr/share/fonts/TTF/DejaVuSans.ttf",                              // Інший поширений шлях Linux
        NULL
    };
    for (int i = 0; font_paths[i] != NULL; ++i) {
        #if SDL_TTF_VERSION_ATLEAST(2,20,0)
            appCtx->font = TTF_OpenFontDPI(font_paths[i], FONT_SIZE, (int)(72 * scale_x), (int)(72 * scale_y));
        #else
            appCtx->font = TTF_OpenFont(font_paths[i], FONT_SIZE);
        #endif
        if (appCtx->font) { break; }
    }
    if (!appCtx->font) { fprintf(stderr, "Failed to load font: %s\n", TTF_GetError()); SDL_DestroyRenderer(appCtx->ren); SDL_DestroyWindow(appCtx->win); TTF_Quit(); SDL_Quit(); return false;}
    appCtx->palette[COL_BG] = (SDL_Color){50,52,55,255}; appCtx->palette[COL_TEXT] = (SDL_Color){100,102,105,255};
    appCtx->palette[COL_CORRECT] = (SDL_Color){201,200,190,255}; appCtx->palette[COL_INCORRECT] = (SDL_Color){200,0,0,255};
    appCtx->palette[COL_CURSOR] = (SDL_Color){255,200,0,255};
    appCtx->line_h = TTF_FontLineSkip(appCtx->font); if (appCtx->line_h <= 0) appCtx->line_h = TTF_FontHeight(appCtx->font); if (appCtx->line_h <= 0) appCtx->line_h = FONT_SIZE + 4;
    for (int c = 32; c < 127; c++) {
        int adv_val; if (TTF_GlyphMetrics(appCtx->font, (Uint16)c, NULL, NULL, NULL, NULL, &adv_val) != 0) { adv_val = FONT_SIZE / 2; }
        appCtx->glyph_adv_cache[c] = (adv_val > 0) ? adv_val : FONT_SIZE/2;
        for (int col_idx = COL_TEXT; col_idx <= COL_INCORRECT; col_idx++) {
            SDL_Surface *surf = TTF_RenderGlyph_Blended(appCtx->font, (Uint16)c, appCtx->palette[col_idx]); if (!surf) continue;
            appCtx->glyph_w_cache[col_idx][c] = surf->w; appCtx->glyph_h_cache[col_idx][c] = surf->h;
            appCtx->glyph_tex_cache[col_idx][c] = SDL_CreateTextureFromSurface(appCtx->ren, surf);
            if (!appCtx->glyph_tex_cache[col_idx][c]) { fprintf(stderr, "Warning: Failed to create texture for glyph %c (ASCII %d) color %d\n", c, c, col_idx); }
            SDL_FreeSurface(surf); surf = NULL;
        }
    }
    appCtx->space_advance_width = appCtx->glyph_adv_cache[' ']; if (appCtx->space_advance_width <= 0) appCtx->space_advance_width = FONT_SIZE / 3;
    appCtx->tab_width_pixels = (appCtx->space_advance_width > 0) ? (TAB_SIZE_IN_SPACES * appCtx->space_advance_width) : (TAB_SIZE_IN_SPACES * (FONT_SIZE / 3));
    return true;
}

void CleanupApp(AppContext *appCtx) {
    if (!appCtx) return;
    for (int c = 32; c < 127; c++) {
        for (int col = COL_TEXT; col <= COL_INCORRECT; col++) {
            if (appCtx->glyph_tex_cache[col][c]) {
                SDL_DestroyTexture(appCtx->glyph_tex_cache[col][c]);
                appCtx->glyph_tex_cache[col][c] = NULL;
            }
        }
    }
    if (appCtx->font) { TTF_CloseFont(appCtx->font); appCtx->font = NULL; }
    if (appCtx->ren) { SDL_DestroyRenderer(appCtx->ren); appCtx->ren = NULL; }
    if (appCtx->win) { SDL_DestroyWindow(appCtx->win); appCtx->win = NULL; }
    TTF_Quit();
    SDL_Quit();
}

char* PreprocessText(const char* raw_text_buffer, size_t raw_text_len, size_t* out_final_text_len) {
    if (raw_text_buffer == NULL || out_final_text_len == NULL) {
        if (out_final_text_len) *out_final_text_len = 0;
        return NULL;
    }
    if (raw_text_len == 0) {
        *out_final_text_len = 0;
        char* empty_str = (char*)malloc(1);
        if (empty_str) empty_str[0] = '\0';
        else { if(log_file) fprintf(log_file, "Error: malloc failed for empty_str in PreprocessText\n"); }
        return empty_str;
    }

    size_t temp_buffer_capacity = raw_text_len * 2 + 10;
    char *temp_buffer = (char*)malloc(temp_buffer_capacity);
    if (!temp_buffer) {
        if(log_file) fprintf(log_file, "Error: Failed to allocate temporary buffer in PreprocessText: %s\n", strerror(errno));
        perror("Failed to allocate temporary buffer in PreprocessText");
        *out_final_text_len = 0;
        return NULL;
    }

    size_t temp_w_idx = 0;
    const char* p_read = raw_text_buffer;
    const char* p_read_end = raw_text_buffer + raw_text_len;

    while (p_read < p_read_end) {
        if (temp_w_idx + 4 >= temp_buffer_capacity) {
            temp_buffer_capacity = temp_buffer_capacity * 2 + 4;
            char *new_temp_buffer = (char *)realloc(temp_buffer, temp_buffer_capacity);
            if (!new_temp_buffer) {
                if(log_file) fprintf(log_file, "Error: Failed to reallocate temporary buffer (Pass 1): %s\n", strerror(errno));
                perror("Failed to reallocate temporary buffer in PreprocessText (Pass 1)");
                free(temp_buffer);
                *out_final_text_len = 0;
                return NULL;
            }
            temp_buffer = new_temp_buffer;
        }

        if (*p_read == '\r') {
            p_read++;
            if (p_read < p_read_end && *p_read == '\n') {
                p_read++;
            }
            temp_buffer[temp_w_idx++] = '\n';
            continue;
        }

        if (*p_read == '-' && (p_read + 1 < p_read_end) && *(p_read + 1) == '-') {
            temp_buffer[temp_w_idx++] = (char)0xE2;
            temp_buffer[temp_w_idx++] = (char)0x80;
            temp_buffer[temp_w_idx++] = (char)0x94;
            p_read += 2;
            continue;
        }

        const char *char_start = p_read;
        Sint32 cp = decode_utf8(&p_read, p_read_end);
        size_t orig_len = (size_t)(p_read - char_start);

        if (cp <= 0) {
            if (orig_len == 0 && p_read < p_read_end) {
                p_read++;
            }
            continue;
        }

        if (cp == 0x2014) { if (temp_w_idx + 3 <= temp_buffer_capacity) { temp_buffer[temp_w_idx++] = (char)0xE2; temp_buffer[temp_w_idx++] = (char)0x80; temp_buffer[temp_w_idx++] = (char)0x93; continue; } else { break; } }
        if (cp == 0x2026) { if (temp_w_idx + 3 <= temp_buffer_capacity) { temp_buffer[temp_w_idx++] = '.'; temp_buffer[temp_w_idx++] = '.'; temp_buffer[temp_w_idx++] = '.'; continue; } break; }
        if (cp == 0x2018 || cp == 0x2019 || cp == 0x201C || cp == 0x201D) { if (temp_w_idx + 1 <= temp_buffer_capacity) { temp_buffer[temp_w_idx++] = '\''; continue; } break; }

        if (temp_w_idx + orig_len <= temp_buffer_capacity) {
            memcpy(temp_buffer + temp_w_idx, char_start, orig_len);
            temp_w_idx += orig_len;
        } else {
            if(log_file) fprintf(log_file, "Error: Buffer overflow in PreprocessText Pass 1, character copy.\n");
            break;
        }
    }
    temp_buffer[temp_w_idx] = '\0';

    char *processed_text = (char*)malloc(temp_w_idx + 1);
    if (!processed_text) {
        if(log_file) fprintf(log_file, "Error: Failed to allocate processed_text in PreprocessText (Pass 2): %s\n", strerror(errno));
        perror("Failed to allocate processed_text in PreprocessText (Pass 2)");
        free(temp_buffer);
        *out_final_text_len = 0;
        return NULL;
    }

    size_t final_pt_idx = 0;
    const char* p2_read = temp_buffer;
    const char* p2_read_end = temp_buffer + temp_w_idx;

    int consecutive_newlines = 0;
    bool last_char_output_was_space = true;
    bool content_has_started = false;

    const char* p2_scan_lead = p2_read;
    while(p2_scan_lead < p2_read_end) {
        const char* peek_ptr_ws = p2_scan_lead;
        Sint32 cp_peek_ws = decode_utf8(&peek_ptr_ws, p2_read_end);
        if (cp_peek_ws == ' ' || cp_peek_ws == '\n' || cp_peek_ws == '\t') {
            p2_scan_lead = peek_ptr_ws;
        } else {
            break;
        }
    }
    p2_read = p2_scan_lead;

    while(p2_read < p2_read_end) {
        const char* char_start_pass2_original = p2_read;
        Sint32 cp2 = decode_utf8(&p2_read, p2_read_end);
        size_t char_len_pass2 = (size_t)(p2_read - char_start_pass2_original);

        if(cp2 <= 0) {
            if (char_len_pass2 == 0 && p2_read < p2_read_end) p2_read++;
            continue;
        }

        if (cp2 == '\n') {
            consecutive_newlines++;
        } else {
            if (consecutive_newlines > 0) {
                if (content_has_started) {
                    if (final_pt_idx > 0 && processed_text[final_pt_idx - 1] == ' ') {
                        final_pt_idx--;
                    }
                    if (consecutive_newlines >= 2) {
                         if (final_pt_idx == 0 || (final_pt_idx > 0 && processed_text[final_pt_idx - 1] != '\n')) {
                            if (final_pt_idx < temp_w_idx) processed_text[final_pt_idx++] = '\n'; else break;
                         }
                    } else {
                        if (final_pt_idx > 0 && processed_text[final_pt_idx - 1] != ' ' && processed_text[final_pt_idx - 1] != '\n') {
                            if (final_pt_idx < temp_w_idx) processed_text[final_pt_idx++] = ' '; else break;
                        } else if (final_pt_idx == 0 && content_has_started ) {
                            if (final_pt_idx < temp_w_idx) processed_text[final_pt_idx++] = ' '; else break;
                        }
                    }
                }
                last_char_output_was_space = true;
            }
            consecutive_newlines = 0;

            if (cp2 == ' ' || cp2 == '\t') {
                if (content_has_started && !last_char_output_was_space) {
                    if (final_pt_idx < temp_w_idx) processed_text[final_pt_idx++] = ' '; else break;
                }
                last_char_output_was_space = true;
            } else {
                if (final_pt_idx + char_len_pass2 <= temp_w_idx) {
                    memcpy(processed_text + final_pt_idx, char_start_pass2_original, char_len_pass2);
                    final_pt_idx += char_len_pass2;
                } else break;
                last_char_output_was_space = false;
                content_has_started = true;
            }
        }
    }

    if (consecutive_newlines > 0 && content_has_started) {
        if (final_pt_idx > 0 && processed_text[final_pt_idx - 1] == ' ') final_pt_idx--;
        if (consecutive_newlines >= 2) {
             if (final_pt_idx == 0 || (final_pt_idx > 0 && processed_text[final_pt_idx - 1] != '\n')) {
                if (final_pt_idx < temp_w_idx) processed_text[final_pt_idx++] = '\n';
             }
        }
    }

    while (final_pt_idx > 0 && (processed_text[final_pt_idx-1] == ' ' || processed_text[final_pt_idx-1] == '\n')) {
        final_pt_idx--;
    }
    processed_text[final_pt_idx] = '\0';
    *out_final_text_len = final_pt_idx;
    free(temp_buffer);

    char* final_text = (char*)realloc(processed_text, final_pt_idx + 1);
    if (!final_text && final_pt_idx > 0) {
        if(log_file) fprintf(log_file, "Warning: realloc failed for final_text, returning original buffer. Length: %zu\n", final_pt_idx);
        return processed_text;
    }
    if (!final_text && final_pt_idx == 0 && processed_text != NULL) {
        free(processed_text);
        final_text = (char*)malloc(1);
        if(final_text) final_text[0] = '\0';
        else { if(log_file) fprintf(log_file, "Error: malloc for empty final_text failed after realloc failure.\n"); }
        return final_text;
    }
    return final_text ? final_text : processed_text;
}


void HandleAppEvents(SDL_Event *event, size_t *current_input_byte_idx,
                     char *input_buffer, size_t final_text_len,
                     const char* text_to_type,
                     bool *typing_started, Uint32 *start_time, bool *quit_flag) {
    if (!event || !current_input_byte_idx || !input_buffer || !typing_started || !start_time || !quit_flag || !text_to_type) return;

    while (SDL_PollEvent(event)) {
        if (event->type == SDL_QUIT) {
            *quit_flag = true;
            return;
        }

        if (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_ESCAPE) {
            *quit_flag = true;
            return;
        }

        if (event->type == SDL_KEYDOWN) {
            if (!event->key.repeat) {
                bool prev_l_modifier_held = l_modifier_held;
                bool prev_r_modifier_held = r_modifier_held;

                #if defined(_WIN32) || defined(__linux__)
                    if (event->key.keysym.sym == SDLK_LALT) l_modifier_held = true;
                    else if (event->key.keysym.sym == SDLK_RALT) r_modifier_held = true;
                #elif defined(__APPLE__)
                    if (event->key.keysym.sym == SDLK_LGUI) l_modifier_held = true;
                    else if (event->key.keysym.sym == SDLK_RGUI) r_modifier_held = true;
                #else
                    if (event->key.keysym.mod & KMOD_LALT) l_modifier_held = true;
                    if (event->key.keysym.mod & KMOD_RALT) r_modifier_held = true;
                #endif

                if (l_modifier_held && r_modifier_held && !(prev_l_modifier_held && prev_r_modifier_held)) {
                    is_paused = !is_paused;
                    if (is_paused) {
                        if (*typing_started) { time_at_pause_ms = SDL_GetTicks(); }
                        if (log_file) fprintf(log_file, "INFO: Game paused.\n");
                    } else {
                        if (*typing_started) { *start_time += (SDL_GetTicks() - time_at_pause_ms); }
                         if (log_file) fprintf(log_file, "INFO: Game resumed.\n");
                    }
                }
            }
        } else if (event->type == SDL_KEYUP) {
             if (!event->key.repeat) {
                #if defined(_WIN32) || defined(__linux__)
                    if (event->key.keysym.sym == SDLK_LALT) l_modifier_held = false;
                    else if (event->key.keysym.sym == SDLK_RALT) r_modifier_held = false;
                #elif defined(__APPLE__)
                    if (event->key.keysym.sym == SDLK_LGUI) l_modifier_held = false;
                    else if (event->key.keysym.sym == SDLK_RGUI) r_modifier_held = false;
                #else
                    if (!(event->key.keysym.mod & KMOD_LALT) && l_modifier_held && event->key.keysym.scancode == SDL_GetScancodeFromKey(SDLK_LALT)) l_modifier_held = false;
                    if (!(event->key.keysym.mod & KMOD_RALT) && r_modifier_held && event->key.keysym.scancode == SDL_GetScancodeFromKey(SDLK_RALT)) r_modifier_held = false;
                #endif
            }
        }

        if (is_paused && event->type == SDL_KEYDOWN && !event->key.repeat) {
            char command[1100] = {0};
            const char* file_to_open = NULL;

            if (event->key.keysym.sym == SDLK_s) {
                file_to_open = actual_stats_file_path;
                if (log_file) fprintf(log_file, "INFO: 's' pressed (paused state) to open stats file: %s\n", file_to_open);
            } else if (event->key.keysym.sym == SDLK_t) {
                file_to_open = actual_text_file_path;
                if (log_file) fprintf(log_file, "INFO: 't' pressed (paused state) to open text file: %s\n", file_to_open);
            }

            if (file_to_open && file_to_open[0] != '\0') {
                #ifdef _WIN32
                    snprintf(command, sizeof(command), "explorer \"%s\"", file_to_open);
                #elif __APPLE__
                    snprintf(command, sizeof(command), "open \"%s\"", file_to_open);
                #elif __linux__
                    snprintf(command, sizeof(command), "xdg-open \"%s\"", file_to_open);
                #else
                    if (log_file) fprintf(log_file, "INFO: No system command defined for this OS to open files.\n");
                #endif

                if (command[0] != '\0') {
                    if (log_file) fprintf(log_file, "Attempting to execute system command: %s\n", command);
                    int ret = system(command);
                    if (ret != 0 && log_file) {
                        fprintf(log_file, "WARN: System command '%s' returned %d. Error: %s\n", command, ret, strerror(errno));
                    }
                }
            } else {
                if (log_file && (event->key.keysym.sym == SDLK_s || event->key.keysym.sym == SDLK_t)) {
                     fprintf(log_file, "WARN: File path is not set for the requested action (s or t); cannot open.\n");
                }
            }
            if (log_file) fflush(log_file);

            if (event->key.keysym.sym == SDLK_s || event->key.keysym.sym == SDLK_t) {
                 continue;
            }
        }

        if (is_paused) {
            continue;
        }

        if (event->type == SDL_KEYDOWN) {
            if (event->key.keysym.sym == SDLK_BACKSPACE && *current_input_byte_idx > 0) {
                const char *buffer_start = input_buffer;
                const char *current_pos = input_buffer + *current_input_byte_idx;
                const char *prev_char_start_ptr = buffer_start;
                const char *temp_iter = buffer_start;

                while(temp_iter < current_pos) {
                    prev_char_start_ptr = temp_iter;
                    Sint32 cp_decoded = decode_utf8(&temp_iter, current_pos);
                    if (temp_iter <= prev_char_start_ptr || cp_decoded <=0) {
                        prev_char_start_ptr = current_pos - 1;
                        if (prev_char_start_ptr < buffer_start) prev_char_start_ptr = buffer_start;
                        break;
                    }
                }
                *current_input_byte_idx = (size_t)(prev_char_start_ptr - buffer_start);
                input_buffer[*current_input_byte_idx] = '\0';
                 if (log_file) fprintf(log_file, "Backspace. New input index: %zu. Input: '%s'\n", *current_input_byte_idx, input_buffer);
            }
        }
        if (event->type == SDL_TEXTINPUT) {
            if (!(*typing_started) && final_text_len > 0) {
                *start_time = SDL_GetTicks();
                *typing_started = true;
                total_keystrokes_for_accuracy = 0;
                total_errors_committed_for_accuracy = 0;
                 if (log_file) fprintf(log_file, "Typing started.\n");
            }

            size_t input_event_len_bytes = strlen(event->text.text);
            const char* p_event_char_iter = event->text.text;
            const char* event_text_end = event->text.text + input_event_len_bytes;
            size_t current_target_byte_offset_for_event = *current_input_byte_idx;

            while(p_event_char_iter < event_text_end) {
                const char* p_event_char_start_loop = p_event_char_iter;
                Sint32 cp_event = decode_utf8(&p_event_char_iter, event_text_end);
                size_t event_char_len = (size_t)(p_event_char_iter - p_event_char_start_loop);

                if (cp_event <=0 || event_char_len == 0) {
                    if (p_event_char_iter < event_text_end && event_char_len == 0) p_event_char_iter++;
                    continue;
                }

                total_keystrokes_for_accuracy++;

                if (current_target_byte_offset_for_event < final_text_len) {
                    const char* p_target_char_at_offset = text_to_type + current_target_byte_offset_for_event;
                    const char* p_target_char_next_ptr_for_len = p_target_char_at_offset;
                    Sint32 cp_target = decode_utf8(&p_target_char_next_ptr_for_len, text_to_type + final_text_len);
                    size_t target_char_len = (size_t)(p_target_char_next_ptr_for_len - p_target_char_at_offset);

                    if (cp_target <=0 || cp_event != cp_target) {
                        total_errors_committed_for_accuracy++;
                         if (log_file && cp_target > 0) fprintf(log_file, "Error: Typed U+%04X (event), Expected U+%04X (target)\n", cp_event, cp_target);
                         else if (log_file) fprintf(log_file, "Error: Typed U+%04X (event), Expected invalid/end of target text.\n", cp_event);
                    }
                    if(cp_target > 0 && target_char_len > 0) {
                        current_target_byte_offset_for_event += target_char_len;
                    } else {
                        current_target_byte_offset_for_event++;
                    }
                } else {
                    total_errors_committed_for_accuracy++;
                    current_target_byte_offset_for_event++;
                    if (log_file) fprintf(log_file, "Error: Typed U+%04X past end of target text.\n", cp_event);
                }
            }

            if (*current_input_byte_idx + input_event_len_bytes < final_text_len + 90 ) {
                bool can_add_input = true;
                if(input_event_len_bytes == 1 && event->text.text[0] == ' ' && *current_input_byte_idx > 0){
                    const char *end_of_current_input = input_buffer + (*current_input_byte_idx);
                    const char *last_char_ptr_in_buf = input_buffer;
                    const char *iter_ptr_buf = input_buffer;
                     while(iter_ptr_buf < end_of_current_input){
                        last_char_ptr_in_buf = iter_ptr_buf;
                        Sint32 cp_buf_temp = decode_utf8(&iter_ptr_buf, end_of_current_input);
                        if(iter_ptr_buf <= last_char_ptr_in_buf || cp_buf_temp <= 0) {
                            last_char_ptr_in_buf = end_of_current_input -1;
                            if (last_char_ptr_in_buf < input_buffer) last_char_ptr_in_buf = input_buffer;
                            break;
                        }
                     }
                     const char *temp_last_char_ptr = last_char_ptr_in_buf;
                     Sint32 last_cp_in_buf = decode_utf8(&temp_last_char_ptr, end_of_current_input);
                     if (last_cp_in_buf == ' ') {
                        can_add_input = false;
                     }
                }

                if(can_add_input){
                    memcpy(input_buffer + *current_input_byte_idx, event->text.text, input_event_len_bytes);
                    (*current_input_byte_idx) += input_event_len_bytes;
                    input_buffer[*current_input_byte_idx] = '\0';
                }
            } else {
                 if (log_file) fprintf(log_file, "WARN: Input buffer near full or event text too long. Input from event '%s' ignored.\n", event->text.text);
            }
        }
    }
}

void RenderAppTimer(AppContext *appCtx, Uint32 elapsed_ms_param, int *out_timer_h, int *out_timer_w) {
    if (!appCtx || !appCtx->font || !appCtx->ren || !out_timer_h || !out_timer_w) return;

    Uint32 elapsed_s;
    char timer_buf[40];

    if (is_paused) {
        if (typing_started_main) {
            elapsed_s = elapsed_ms_param / 1000;
            int m = (int)(elapsed_s / 60);
            int s = (int)(elapsed_s % 60);
            snprintf(timer_buf, sizeof(timer_buf), "%02d:%02d (Paused)", m, s);
        } else {
            snprintf(timer_buf, sizeof(timer_buf), "00:00 (Paused)");
        }
    } else {
        elapsed_s = elapsed_ms_param / 1000;
        int m = (int)(elapsed_s / 60);
        int s = (int)(elapsed_s % 60);
        snprintf(timer_buf, sizeof(timer_buf), "%02d:%02d", m, s);
    }

    int current_timer_h_val = 0;
    int current_timer_w_val = 0;
    SDL_Surface *timer_surf = TTF_RenderText_Blended(appCtx->font, timer_buf, appCtx->palette[COL_CURSOR]);

    if (timer_surf) {
        SDL_Texture *timer_tex = SDL_CreateTextureFromSurface(appCtx->ren, timer_surf);
        if (timer_tex) {
            current_timer_w_val = timer_surf->w;
            current_timer_h_val = timer_surf->h;
            SDL_Rect rtimer = { TEXT_AREA_X, TEXT_AREA_PADDING_Y, current_timer_w_val, current_timer_h_val };
            SDL_RenderCopy(appCtx->ren, timer_tex, NULL, &rtimer);
            SDL_DestroyTexture(timer_tex);
        } else {
            if(log_file) fprintf(log_file, "Error: Failed to create timer texture from surface: %s\n", SDL_GetError());
            TTF_SizeText(appCtx->font, timer_buf, &current_timer_w_val, &current_timer_h_val);
            if(current_timer_h_val <=0) current_timer_h_val = appCtx->line_h;
        }
        SDL_FreeSurface(timer_surf);
    } else {
        if(log_file) fprintf(log_file, "Error: Failed to render timer text surface: %s\n", TTF_GetError());
        TTF_SizeText(appCtx->font, timer_buf, &current_timer_w_val, &current_timer_h_val);
        if(current_timer_h_val <=0) current_timer_h_val = appCtx->line_h;
    }

    if (current_timer_w_val <= 0) current_timer_w_val = 50;
    *out_timer_h = current_timer_h_val;
    *out_timer_w = current_timer_w_val;
}

void RenderLiveStats(AppContext *appCtx, Uint32 current_ticks_param, Uint32 start_time_ticks_param,
                     const char *text_to_type , const char *input_buffer, size_t current_input_byte_idx,
                     int timer_x_pos, int timer_width, int timer_y_pos, int timer_height) {
    (void)text_to_type;

    if (!appCtx || !appCtx->font || !appCtx->ren || !typing_started_main ) return;

    float elapsed_seconds;
    if (is_paused) {
        elapsed_seconds = (float)(time_at_pause_ms - start_time_ticks_param) / 1000.0f;
    } else {
        elapsed_seconds = (float)(current_ticks_param - start_time_ticks_param) / 1000.0f;
    }

    if (elapsed_seconds < 0.05f && total_keystrokes_for_accuracy > 0) elapsed_seconds = 0.05f;
    else if (elapsed_seconds < 0.001f) elapsed_seconds = 0.001f;

    float elapsed_minutes = elapsed_seconds / 60.0f;
    float live_accuracy = 100.0f;
    if (total_keystrokes_for_accuracy > 0) {
        live_accuracy = ((float)(total_keystrokes_for_accuracy - total_errors_committed_for_accuracy) / (float)total_keystrokes_for_accuracy) * 100.0f;
        if (live_accuracy < 0.0f) live_accuracy = 0.0f;
        if (live_accuracy > 100.0f) live_accuracy = 100.0f;
    }

    size_t live_correct_keystrokes = (total_keystrokes_for_accuracy >= total_errors_committed_for_accuracy) ?
                                     (total_keystrokes_for_accuracy - total_errors_committed_for_accuracy) : 0;
    float live_net_words_for_wpm = (float)live_correct_keystrokes / 5.0f;
    float live_wpm = (elapsed_minutes > 0.0001f) ? (live_net_words_for_wpm / elapsed_minutes) : 0.0f;
    if (live_wpm < 0.0f) live_wpm = 0.0f;

    int live_typed_words_count = 0;
    if (current_input_byte_idx > 0) {
        bool in_word_flag = false;
        const char* p_word_scan_iter = input_buffer;
        const char* p_word_scan_end_iter = input_buffer + current_input_byte_idx;
        while(p_word_scan_iter < p_word_scan_end_iter) {
            const char* temp_p_word_start = p_word_scan_iter;
            Sint32 cp_word = decode_utf8(&p_word_scan_iter, p_word_scan_end_iter);
            if (cp_word <= 0) { if (p_word_scan_iter <= temp_p_word_start && p_word_scan_iter < p_word_scan_end_iter) p_word_scan_iter++; else break; continue; }

            bool is_current_char_separator = (cp_word == ' ' || cp_word == '\n' || cp_word == '\t');

            if (!is_current_char_separator) {
                if (!in_word_flag) {
                    live_typed_words_count++;
                    in_word_flag = true;
                }
            } else {
                in_word_flag = false;
            }
        }
    }

    char wpm_buf[32], acc_buf[32], words_buf[32];
    snprintf(wpm_buf, sizeof(wpm_buf), "WPM: %.0f", live_wpm);
    snprintf(acc_buf, sizeof(acc_buf), "Acc: %.0f%%", live_accuracy);
    snprintf(words_buf, sizeof(words_buf), "Words: %d", live_typed_words_count);

    SDL_Color stat_color = appCtx->palette[COL_TEXT];
    int current_x_render_pos = timer_x_pos + timer_width + 20;
    int stats_y_render_pos = timer_y_pos + (timer_height - appCtx->line_h) / 2;
    if (stats_y_render_pos < TEXT_AREA_PADDING_Y) stats_y_render_pos = TEXT_AREA_PADDING_Y;

    SDL_Surface *surf;
    SDL_Texture *tex;
    SDL_Rect dst;

    surf = TTF_RenderText_Blended(appCtx->font, wpm_buf, stat_color);
    if (surf) {
        tex = SDL_CreateTextureFromSurface(appCtx->ren, surf);
        if (tex) {
            dst = (SDL_Rect){current_x_render_pos, stats_y_render_pos, surf->w, surf->h};
            SDL_RenderCopy(appCtx->ren, tex, NULL, &dst);
            current_x_render_pos += surf->w + 15;
            SDL_DestroyTexture(tex);
        } else if (log_file) fprintf(log_file, "Error creating WPM texture: %s\n", SDL_GetError());
        SDL_FreeSurface(surf);
    } else if (log_file) fprintf(log_file, "Error rendering WPM surface: %s\n", TTF_GetError());

    surf = TTF_RenderText_Blended(appCtx->font, acc_buf, stat_color);
    if (surf) {
        tex = SDL_CreateTextureFromSurface(appCtx->ren, surf);
        if (tex) {
            dst = (SDL_Rect){current_x_render_pos, stats_y_render_pos, surf->w, surf->h};
            SDL_RenderCopy(appCtx->ren, tex, NULL, &dst);
            current_x_render_pos += surf->w + 15;
            SDL_DestroyTexture(tex);
        } else if (log_file) fprintf(log_file, "Error creating Accuracy texture: %s\n", SDL_GetError());
        SDL_FreeSurface(surf);
    } else if (log_file) fprintf(log_file, "Error rendering Accuracy surface: %s\n", TTF_GetError());

    surf = TTF_RenderText_Blended(appCtx->font, words_buf, stat_color);
    if (surf) {
        tex = SDL_CreateTextureFromSurface(appCtx->ren, surf);
        if (tex) {
            dst = (SDL_Rect){current_x_render_pos, stats_y_render_pos, surf->w, surf->h};
            SDL_RenderCopy(appCtx->ren, tex, NULL, &dst);
            SDL_DestroyTexture(tex);
        } else if (log_file) fprintf(log_file, "Error creating Words texture: %s\n", SDL_GetError());
        SDL_FreeSurface(surf);
    } else if (log_file) fprintf(log_file, "Error rendering Words surface: %s\n", TTF_GetError());
}


void CalculateAndPrintAppStats(Uint32 start_time_ms_param, size_t current_input_byte_idx,
                               const char *text_to_type, size_t final_text_len,
                               const char *input_buffer) {
    (void)text_to_type; (void)final_text_len; (void)input_buffer;

    if (!typing_started_main) {
        printf("No typing done. No stats to display.\n");
        if (log_file) fprintf(log_file, "CalculateAndPrintAppStats: No typing done.\n");
        return;
    }

    Uint32 end_time_ms_val = is_paused ? time_at_pause_ms : SDL_GetTicks();
    float time_taken_seconds = (float)(end_time_ms_val - start_time_ms_param) / 1000.0f;

    if (time_taken_seconds <= 0.001f && total_keystrokes_for_accuracy == 0) {
        printf("Time taken is too short or no characters typed for meaningful stats.\n");
        if (log_file) fprintf(log_file, "CalculateAndPrintAppStats: Time too short or no keystrokes.\n");
        return;
    }
    if (time_taken_seconds <= 0.001f) time_taken_seconds = 0.001f;

    size_t final_correct_keystrokes = (total_keystrokes_for_accuracy >= total_errors_committed_for_accuracy) ?
                                     (total_keystrokes_for_accuracy - total_errors_committed_for_accuracy) : 0;
    float net_words = (float)final_correct_keystrokes / 5.0f;
    float wpm = (time_taken_seconds > 0.0001f) ? (net_words / (time_taken_seconds / 60.0f)) : 0.0f;
    if (wpm < 0.0f) wpm = 0.0f;

    float accuracy = 0.0f;
    if (total_keystrokes_for_accuracy > 0) {
        accuracy = ((float)final_correct_keystrokes / (float)total_keystrokes_for_accuracy) * 100.0f;
    }
    if (accuracy < 0.0f) accuracy = 0.0f;
    if (accuracy > 100.0f && total_keystrokes_for_accuracy > 0) accuracy = 100.0f;

    printf("\n--- Typing Stats (Final) ---\n");
    printf("Time Taken: %.2f seconds\n", time_taken_seconds);
    printf("WPM (Net): %.2f\n", wpm);
    printf("Correct Keystrokes: %zu\n", final_correct_keystrokes);
    printf("Total Keystrokes (Accuracy Basis): %llu\n", total_keystrokes_for_accuracy);
    printf("Committed Errors: %llu\n", total_errors_committed_for_accuracy);
    printf("Accuracy (Keystroke-based): %.2f%%\n", accuracy);
    printf("--------------------\n");

    if (actual_stats_file_path[0] != '\0') {
        FILE *stats_file_handle = fopen(actual_stats_file_path, "a");
        if (stats_file_handle) {
            time_t now = time(NULL);
            char time_str[26];
            if (strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now)) == 0) {
                strcpy(time_str, "TimestampError");
            }

            fprintf(stats_file_handle, "%s | WPM: %.2f | Accuracy: %.2f%% | Time: %.1fs | Correct Ks: %zu | Total Ks: %llu | Errors: %llu\n",
                    time_str, wpm, accuracy, time_taken_seconds,
                    final_correct_keystrokes, total_keystrokes_for_accuracy, total_errors_committed_for_accuracy);
            fclose(stats_file_handle);
            if (log_file) fprintf(log_file, "Stats successfully appended to '%s'\n", actual_stats_file_path);
        } else {
            if(log_file) fprintf(log_file, "ERROR: Failed to open/append to stats file '%s': %s\n", actual_stats_file_path, strerror(errno));
            perror("Failed to open stats.txt for appending");
            fprintf(stderr, "Could not open or create stats file at: %s\n", actual_stats_file_path);
        }
    } else {
        if (log_file) fprintf(log_file, "Warning: Stats file path (actual_stats_file_path) is empty. Cannot save stats to file.\n");
    }
}


void CalculateCursorLayout(AppContext *appCtx, const char *text_to_type, size_t final_text_len,
                           size_t current_input_byte_idx, int *out_cursor_abs_y, int *out_cursor_exact_x) {
    if (!appCtx || !text_to_type || !out_cursor_abs_y || !out_cursor_exact_x || !appCtx->font || appCtx->line_h <= 0) {
        if(out_cursor_abs_y) *out_cursor_abs_y = 0;
        if(out_cursor_exact_x) *out_cursor_exact_x = TEXT_AREA_X;
        return;
    }

    int calculated_cursor_y_abs = 0;
    int calculated_cursor_x_on_line = TEXT_AREA_X;
    int current_pen_x_on_line = TEXT_AREA_X;
    int current_abs_line_num = 0;

    const char *p_iter = text_to_type;
    const char *p_end = text_to_type + final_text_len;
    size_t processed_bytes_total = 0;
    bool cursor_position_found_this_pass = false;

    if (current_input_byte_idx == 0) {
        *out_cursor_abs_y = 0;
        *out_cursor_exact_x = TEXT_AREA_X;
        return;
    }

    while(p_iter < p_end && !cursor_position_found_this_pass) {
        size_t bytes_at_block_start = processed_bytes_total;
        int pen_x_at_block_start_on_current_line = current_pen_x_on_line;
        int abs_line_num_at_block_start = current_abs_line_num;

        const char* p_iter_before_get_next_block = p_iter;
        TextBlockInfo current_block = get_next_text_block_func(appCtx, &p_iter, p_end, pen_x_at_block_start_on_current_line);

        if (current_block.num_bytes == 0) {
            if(p_iter < p_end && p_iter == p_iter_before_get_next_block) {p_iter++;}
            if (p_iter > p_iter_before_get_next_block) {
                processed_bytes_total = (size_t)(p_iter - text_to_type);
            } else if (p_iter_before_get_next_block < p_end) {
                 processed_bytes_total = (size_t)(p_iter - text_to_type);
            }
            if (p_iter >= p_end) break;
            continue;
        }

        int y_for_chars_in_this_block_abs = abs_line_num_at_block_start * appCtx->line_h;
        int x_for_chars_in_this_block_start = pen_x_at_block_start_on_current_line;

        if (current_block.is_newline) {
            current_abs_line_num = abs_line_num_at_block_start + 1;
            current_pen_x_on_line = TEXT_AREA_X;
            y_for_chars_in_this_block_abs = (abs_line_num_at_block_start) * appCtx->line_h;
            x_for_chars_in_this_block_start = current_pen_x_on_line;
        } else {
            bool must_wrap_this_block = false;
            if (pen_x_at_block_start_on_current_line != TEXT_AREA_X &&
                (current_block.is_word || current_block.is_tab)) {
                if (pen_x_at_block_start_on_current_line + current_block.pixel_width > TEXT_AREA_X + TEXT_AREA_W) {
                    must_wrap_this_block = true;
                }
                else if (current_block.is_word) {
                    const char *p_peek_next = p_iter;
                    if (p_peek_next < p_end) {
                        const char *temp_peek_ptr = p_peek_next;
                        int pen_x_after_current_block = pen_x_at_block_start_on_current_line + current_block.pixel_width;
                        TextBlockInfo next_block_peek = get_next_text_block_func(appCtx, &temp_peek_ptr, p_end, pen_x_after_current_block);

                        if (next_block_peek.num_bytes > 0 && !next_block_peek.is_word && !next_block_peek.is_newline && !next_block_peek.is_tab) {
                            const char* space_char_ptr = next_block_peek.start_ptr;
                            Sint32 cp_space = decode_utf8(&space_char_ptr, next_block_peek.start_ptr + next_block_peek.num_bytes);
                            if (cp_space == ' ') {
                                int space_width = get_codepoint_advance_and_metrics_func(appCtx, (Uint32)cp_space, appCtx->space_advance_width, NULL, NULL);
                                if (space_width > 0 && (pen_x_after_current_block + space_width > TEXT_AREA_X + TEXT_AREA_W)) {
                                    must_wrap_this_block = true;
                                }
                            }
                        }
                    }
                }
            }

            if (must_wrap_this_block) {
                current_abs_line_num = abs_line_num_at_block_start + 1;
                y_for_chars_in_this_block_abs = current_abs_line_num * appCtx->line_h;
                x_for_chars_in_this_block_start = TEXT_AREA_X;
            }
            current_pen_x_on_line = x_for_chars_in_this_block_start + current_block.pixel_width;
        }

        if (!cursor_position_found_this_pass &&
            current_input_byte_idx >= bytes_at_block_start &&
            current_input_byte_idx < bytes_at_block_start + current_block.num_bytes) {

            calculated_cursor_y_abs = y_for_chars_in_this_block_abs;
            calculated_cursor_x_on_line = x_for_chars_in_this_block_start;

            const char* p_char_iter_in_block = current_block.start_ptr;
            const char* target_cursor_ptr_in_text = text_to_type + current_input_byte_idx;

            while (p_char_iter_in_block < target_cursor_ptr_in_text &&
                   p_char_iter_in_block < current_block.start_ptr + current_block.num_bytes) {
                const char* temp_char_start_in_block_loop = p_char_iter_in_block;
                Sint32 cp_in_block = decode_utf8(&p_char_iter_in_block, p_end);

                if (cp_in_block <= 0 ) {
                    if (p_char_iter_in_block <= temp_char_start_in_block_loop) p_char_iter_in_block = temp_char_start_in_block_loop + 1;
                    break;
                }
                if (p_char_iter_in_block > target_cursor_ptr_in_text && target_cursor_ptr_in_text > temp_char_start_in_block_loop) {
                    p_char_iter_in_block = temp_char_start_in_block_loop;
                    break;
                }

                int adv_char_in_block = 0;
                if (current_block.is_tab) {
                    int offset_in_line_inner = calculated_cursor_x_on_line - TEXT_AREA_X;
                    adv_char_in_block = appCtx->tab_width_pixels - (offset_in_line_inner % appCtx->tab_width_pixels);
                    if (adv_char_in_block == 0 && offset_in_line_inner >=0) adv_char_in_block = appCtx->tab_width_pixels;
                    if (adv_char_in_block <=0) adv_char_in_block = appCtx->tab_width_pixels;
                } else {
                    adv_char_in_block = get_codepoint_advance_and_metrics_func(appCtx, (Uint32)cp_in_block, appCtx->space_advance_width, NULL, NULL);
                }

                if (calculated_cursor_x_on_line + adv_char_in_block > TEXT_AREA_X + TEXT_AREA_W && calculated_cursor_x_on_line != TEXT_AREA_X ) {
                    calculated_cursor_y_abs += appCtx->line_h;
                    calculated_cursor_x_on_line = TEXT_AREA_X;
                }
                calculated_cursor_x_on_line += adv_char_in_block;
            }
            cursor_position_found_this_pass = true;
        }
        processed_bytes_total = (size_t)(p_iter - text_to_type);

        if (!cursor_position_found_this_pass && processed_bytes_total == current_input_byte_idx) {
            calculated_cursor_x_on_line = current_pen_x_on_line;
            calculated_cursor_y_abs = current_abs_line_num * appCtx->line_h;
            cursor_position_found_this_pass = true;
        }
    }

    if (!cursor_position_found_this_pass && current_input_byte_idx == final_text_len) {
        calculated_cursor_x_on_line = current_pen_x_on_line;
        calculated_cursor_y_abs = current_abs_line_num * appCtx->line_h;
    }

    *out_cursor_abs_y = calculated_cursor_y_abs;
    *out_cursor_exact_x = calculated_cursor_x_on_line;
}


void UpdateVisibleLine(int y_coord_for_update_abs, int line_h_val, int *first_visible_abs_line_num_ptr) {
    if (line_h_val > 0 && first_visible_abs_line_num_ptr) {
        int target_cursor_abs_line_idx = y_coord_for_update_abs / line_h_val;
        *first_visible_abs_line_num_ptr = target_cursor_abs_line_idx - CURSOR_TARGET_VIEWPORT_LINE;
        if (*first_visible_abs_line_num_ptr < 0) {
            *first_visible_abs_line_num_ptr = 0;
        }
    }
}


void RenderTextContent(AppContext *appCtx, const char *text_to_type, size_t final_text_len,
                       const char *input_buffer, size_t current_input_byte_idx,
                       int first_visible_abs_line_num_val, int text_viewport_top_y,
                       int *out_final_cursor_draw_x, int *out_final_cursor_draw_y_baseline) {

    if (!appCtx || !text_to_type || !input_buffer || !out_final_cursor_draw_x || !out_final_cursor_draw_y_baseline || !appCtx->font || appCtx->line_h <=0) {
        if (out_final_cursor_draw_x) *out_final_cursor_draw_x = -100;
        if (out_final_cursor_draw_y_baseline) *out_final_cursor_draw_y_baseline = -100;
        return;
    }

    int render_pen_x = TEXT_AREA_X;
    int render_current_abs_line_num = 0;
    const char *p_render_iter = text_to_type;
    const char *p_text_end_for_render = text_to_type + final_text_len;

    *out_final_cursor_draw_x = -100;
    *out_final_cursor_draw_y_baseline = -100;

    if (current_input_byte_idx == 0) {
        int relative_line_idx_for_cursor_at_start = 0 - first_visible_abs_line_num_val;
        if (relative_line_idx_for_cursor_at_start >=0 && relative_line_idx_for_cursor_at_start < DISPLAY_LINES) {
            *out_final_cursor_draw_x = TEXT_AREA_X;
            *out_final_cursor_draw_y_baseline = text_viewport_top_y + relative_line_idx_for_cursor_at_start * appCtx->line_h;
        }
    }

    while(p_render_iter < p_text_end_for_render) {
        int current_viewport_line_idx = render_current_abs_line_num - first_visible_abs_line_num_val;
        if (current_viewport_line_idx >= DISPLAY_LINES) break;

        size_t block_start_byte_offset_in_doc = (size_t)(p_render_iter - text_to_type);
        TextBlockInfo block = get_next_text_block_func(appCtx, &p_render_iter, p_text_end_for_render, render_pen_x);

        if (block.num_bytes == 0 && p_render_iter >= p_text_end_for_render) break;
        if (block.num_bytes == 0 || !block.start_ptr) {
            if(p_render_iter < p_text_end_for_render) p_render_iter++; else break;
            continue;
        }

        int line_on_screen_y_baseline = text_viewport_top_y + current_viewport_line_idx * appCtx->line_h;

        if (block_start_byte_offset_in_doc == current_input_byte_idx &&
            current_viewport_line_idx >=0 && current_viewport_line_idx < DISPLAY_LINES ) {
            *out_final_cursor_draw_x = render_pen_x;
            *out_final_cursor_draw_y_baseline = line_on_screen_y_baseline;
        }

        if (block.is_newline) {
            render_current_abs_line_num++;
            render_pen_x = TEXT_AREA_X;
        } else {
            int x_block_starts_on_this_line = render_pen_x;
            int y_baseline_for_block_content = line_on_screen_y_baseline;
            bool wrap_this_block = false;

            if (render_pen_x != TEXT_AREA_X && block.is_word) {
                if (render_pen_x + block.pixel_width > TEXT_AREA_X + TEXT_AREA_W) {
                    wrap_this_block = true;
                } else {
                    const char *p_peek_next_char_after_block = p_render_iter;
                    if (p_peek_next_char_after_block < p_text_end_for_render) {
                        const char *temp_peek_iter = p_peek_next_char_after_block;
                        int pen_x_after_this_block = render_pen_x + block.pixel_width;
                        TextBlockInfo next_block_after_this = get_next_text_block_func(appCtx, &temp_peek_iter, p_text_end_for_render, pen_x_after_this_block);

                        if (next_block_after_this.num_bytes > 0 && !next_block_after_this.is_word &&
                            !next_block_after_this.is_newline && !next_block_after_this.is_tab) {
                            const char* space_block_char_ptr = next_block_after_this.start_ptr;
                            Sint32 cp_space_in_next_block = decode_utf8(&space_block_char_ptr, next_block_after_this.start_ptr + next_block_after_this.num_bytes);
                            if (cp_space_in_next_block == ' ') {
                                int space_char_width = get_codepoint_advance_and_metrics_func(appCtx, (Uint32)cp_space_in_next_block, appCtx->space_advance_width,NULL,NULL);
                                if (space_char_width > 0 && (pen_x_after_this_block + space_char_width > TEXT_AREA_X + TEXT_AREA_W)) {
                                    wrap_this_block = true;
                                }
                            }
                        }
                    }
                }
            }

            if (wrap_this_block) {
                render_current_abs_line_num++;
                current_viewport_line_idx = render_current_abs_line_num - first_visible_abs_line_num_val;
                if (current_viewport_line_idx >= DISPLAY_LINES) break;

                y_baseline_for_block_content = text_viewport_top_y + current_viewport_line_idx * appCtx->line_h;
                render_pen_x = TEXT_AREA_X;
                x_block_starts_on_this_line = render_pen_x;

                if (block_start_byte_offset_in_doc == current_input_byte_idx &&
                    current_viewport_line_idx >=0 && current_viewport_line_idx < DISPLAY_LINES) {
                    *out_final_cursor_draw_x = render_pen_x;
                    *out_final_cursor_draw_y_baseline = y_baseline_for_block_content;
                }
            }

            if (current_viewport_line_idx >= 0 && current_viewport_line_idx < DISPLAY_LINES) {
                if (!block.is_tab) {
                    const char *p_char_in_block = block.start_ptr;
                    const char *p_char_end_in_block = block.start_ptr + block.num_bytes;
                    size_t char_offset_within_block = 0;
                    int char_render_px = x_block_starts_on_this_line;
                    int char_render_py_baseline = y_baseline_for_block_content;
                    int char_current_abs_line_num_for_render = render_current_abs_line_num;

                    while(p_char_in_block < p_char_end_in_block) {
                        int char_current_viewport_line_for_render = char_current_abs_line_num_for_render - first_visible_abs_line_num_val;
                        if (char_current_viewport_line_for_render >= DISPLAY_LINES) goto end_char_loop_render_local;

                        const char* glyph_start_ptr_in_block = p_char_in_block;
                        Sint32 cp_to_render = decode_utf8(&p_char_in_block, p_char_end_in_block);
                        size_t glyph_byte_len = (size_t)(p_char_in_block - glyph_start_ptr_in_block);

                        if (cp_to_render <= 0 || glyph_byte_len == 0) {
                            if (p_char_in_block <= glyph_start_ptr_in_block && p_char_in_block < p_char_end_in_block) p_char_in_block++; else break;
                            continue;
                        }

                        size_t char_absolute_byte_pos_in_doc = block_start_byte_offset_in_doc + char_offset_within_block;
                        if (char_absolute_byte_pos_in_doc == current_input_byte_idx &&
                            char_current_viewport_line_for_render >= 0 && char_current_viewport_line_for_render < DISPLAY_LINES) {
                            *out_final_cursor_draw_x = char_render_px;
                            *out_final_cursor_draw_y_baseline = char_render_py_baseline;
                        }

                        int glyph_w_metric=0, glyph_h_metric=0;
                        int advance = get_codepoint_advance_and_metrics_func(appCtx, (Uint32)cp_to_render, appCtx->space_advance_width, &glyph_w_metric, &glyph_h_metric);

                        if (char_render_px + advance > TEXT_AREA_X + TEXT_AREA_W && char_render_px != TEXT_AREA_X) {
                            char_current_abs_line_num_for_render++;
                            char_current_viewport_line_for_render = char_current_abs_line_num_for_render - first_visible_abs_line_num_val;
                            if (char_current_viewport_line_for_render >= DISPLAY_LINES) goto end_char_loop_render_local;

                            char_render_py_baseline = text_viewport_top_y + char_current_viewport_line_for_render * appCtx->line_h;
                            char_render_px = TEXT_AREA_X;

                            if (char_absolute_byte_pos_in_doc == current_input_byte_idx &&
                                char_current_viewport_line_for_render >=0 && char_current_viewport_line_for_render < DISPLAY_LINES) {
                                *out_final_cursor_draw_x = char_render_px;
                                *out_final_cursor_draw_y_baseline = char_render_py_baseline;
                            }
                        }

                        SDL_Color render_color;
                        bool char_is_typed = char_absolute_byte_pos_in_doc < current_input_byte_idx;
                        bool char_is_correct = false;
                        if(char_is_typed){
                            if(char_absolute_byte_pos_in_doc + glyph_byte_len <= current_input_byte_idx) {
                                // Перевірка з memcmp залишається тут
                                char_is_correct = (memcmp(glyph_start_ptr_in_block, input_buffer + char_absolute_byte_pos_in_doc, glyph_byte_len) == 0);
                            } else {
                                // Якщо current_input_byte_idx ще не досяг кінця поточного символу оригіналу,
                                // але char_is_typed = true (тобто початок символу вже пройдено),
                                // це може бути складна ситуація для багатобайтових символів.
                                // Зазвичай, якщо char_absolute_byte_pos_in_doc < current_input_byte_idx,
                                // то char_absolute_byte_pos_in_doc + glyph_byte_len також має бути <= current_input_byte_idx
                                // для повного порівняння. Якщо це не так, символ ще не повністю введено.
                                // Поточна логіка може вважати такий символ помилковим, якщо char_is_correct не встановлено в true.
                                // Можливо, тут потрібне більш точне визначення стану "частково введеного" багатобайтового символу.
                                // Однак, для простоти, якщо перевірка вище не пройдена, char_is_correct залишиться false.
                            }
                            render_color = char_is_correct ? appCtx->palette[COL_CORRECT] : appCtx->palette[COL_INCORRECT];
                        } else {
                            render_color = appCtx->palette[COL_TEXT];
                        }

                        if(cp_to_render >= 32){
                            SDL_Texture* tex_to_render =NULL;
                            bool use_otf_render = false;

                            if(cp_to_render < 128){
                                int cache_color_idx = char_is_typed ? (char_is_correct ? COL_CORRECT : COL_INCORRECT) : COL_TEXT;
                                tex_to_render = appCtx->glyph_tex_cache[cache_color_idx][(int)cp_to_render];
                                if(tex_to_render){
                                    glyph_w_metric = appCtx->glyph_w_cache[cache_color_idx][(int)cp_to_render];
                                    glyph_h_metric = appCtx->glyph_h_cache[cache_color_idx][(int)cp_to_render];
                                }
                            }

                            if(!tex_to_render && appCtx->font){
                                SDL_Surface* surf_otf = TTF_RenderGlyph32_Blended(appCtx->font, (Uint32)cp_to_render, render_color);
                                if(surf_otf){
                                    tex_to_render = SDL_CreateTextureFromSurface(appCtx->ren, surf_otf);
                                    if(tex_to_render){
                                        glyph_w_metric = surf_otf->w;
                                        glyph_h_metric = surf_otf->h;
                                    } else if(log_file) fprintf(log_file, "RenderTextContent: OTF Tex Error for U+%04X: %s\n", cp_to_render, SDL_GetError());
                                    SDL_FreeSurface(surf_otf);
                                    use_otf_render = true;
                                } else if(log_file) fprintf(log_file, "RenderTextContent: OTF Surf Error for U+%04X: %s\n", cp_to_render, TTF_GetError());
                            }

                            if(tex_to_render){
                                if(glyph_w_metric == 0 && advance > 0) glyph_w_metric = advance;
                                if(glyph_h_metric == 0) glyph_h_metric = appCtx->line_h;

                                int y_offset_for_glyph = (appCtx->line_h > glyph_h_metric) ? (appCtx->line_h - glyph_h_metric) / 2 : 0;
                                SDL_Rect dst_rect = {char_render_px, char_render_py_baseline + y_offset_for_glyph, glyph_w_metric, glyph_h_metric};
                                SDL_RenderCopy(appCtx->ren, tex_to_render, NULL, &dst_rect);
                                if(use_otf_render) SDL_DestroyTexture(tex_to_render);
                            }
                        }
                        char_render_px += advance;
                        char_offset_within_block += glyph_byte_len;
                    }
                    end_char_loop_render_local:;
                    render_pen_x = char_render_px;
                    render_current_abs_line_num = char_current_abs_line_num_for_render;
                } else {
                    render_pen_x += block.pixel_width;
                }
            } else {
                render_pen_x += block.pixel_width;
            }
        }

        if (block_start_byte_offset_in_doc + block.num_bytes == current_input_byte_idx) {
            int final_block_viewport_line = render_current_abs_line_num - first_visible_abs_line_num_val;
            if (final_block_viewport_line >=0 && final_block_viewport_line < DISPLAY_LINES) {
                *out_final_cursor_draw_x = render_pen_x;
                *out_final_cursor_draw_y_baseline = text_viewport_top_y + final_block_viewport_line * appCtx->line_h;
            }
        }
    }

    if (current_input_byte_idx == final_text_len) {
        int final_text_end_viewport_line = render_current_abs_line_num - first_visible_abs_line_num_val;
        if (final_text_end_viewport_line >=0 && final_text_end_viewport_line < DISPLAY_LINES) {
            *out_final_cursor_draw_x = render_pen_x;
            *out_final_cursor_draw_y_baseline = text_viewport_top_y + final_text_end_viewport_line * appCtx->line_h;
        }
    }
}


void RenderAppCursor(AppContext *appCtx, bool show_cursor_param, int final_cursor_x_on_screen,
                     int final_cursor_y_baseline_on_screen, int cursor_abs_y_logical ,
                     int first_visible_abs_line_num , int text_viewport_top_y) {
    (void)cursor_abs_y_logical; (void)first_visible_abs_line_num;

    bool actually_show_cursor = is_paused ? true : show_cursor_param;
    if (!appCtx || !appCtx->ren || !actually_show_cursor) return;

    if (final_cursor_x_on_screen >= TEXT_AREA_X &&
        final_cursor_x_on_screen <= TEXT_AREA_X + TEXT_AREA_W + 2 &&
        final_cursor_y_baseline_on_screen >= text_viewport_top_y &&
        final_cursor_y_baseline_on_screen < text_viewport_top_y + (DISPLAY_LINES * appCtx->line_h) ) {

        SDL_Rect cursor_rect = { final_cursor_x_on_screen, final_cursor_y_baseline_on_screen, 2, appCtx->line_h };
        SDL_SetRenderDrawColor(appCtx->ren, appCtx->palette[COL_CURSOR].r, appCtx->palette[COL_CURSOR].g, appCtx->palette[COL_CURSOR].b, appCtx->palette[COL_CURSOR].a);
        SDL_RenderFillRect(appCtx->ren, &cursor_rect);
    }
}


int main(int argc, char **argv) {
    (void)argc; (void)argv;

    #if ENABLE_GAME_LOGS
        char log_file_path_buffer[1024];
        char* pref_path_for_logs_tmp = SDL_GetPrefPath(COMPANY_NAME_STR, PROJECT_NAME_STR);
        if (pref_path_for_logs_tmp) {
            snprintf(log_file_path_buffer, sizeof(log_file_path_buffer), "%slogs.txt", pref_path_for_logs_tmp);
            SDL_free(pref_path_for_logs_tmp);
        } else {
            strcpy(log_file_path_buffer, "logs.txt");
        }
        log_file = fopen(log_file_path_buffer, "w");
        if (log_file == NULL) {
            perror("CRITICAL_STDERR: Failed to open logs.txt for writing in main");
            fprintf(stderr, "Log file path attempted: %s\n", log_file_path_buffer);
        } else {
            fputs("Application started.\n", log_file);
            fprintf(log_file, "Log file initialized at: %s\n", log_file_path_buffer);
            fflush(log_file);
        }
    #else
        log_file = NULL;
    #endif

    AppContext appCtx = {0};
    char *raw_text_content_main = NULL;
    size_t raw_text_len_main = 0;

    actual_text_file_path[0] = '\0';
    actual_stats_file_path[0] = '\0';
    default_text_file_in_bundle[0] = '\0';

    char* pref_path_str_main = SDL_GetPrefPath(COMPANY_NAME_STR, PROJECT_NAME_STR); // Line 1422 (new context)
    if (pref_path_str_main) {
        snprintf(actual_text_file_path, sizeof(actual_text_file_path), "%s%s", pref_path_str_main, TEXT_FILE_PATH_BASENAME);
        snprintf(actual_stats_file_path, sizeof(actual_stats_file_path), "%s%s", pref_path_str_main, STATS_FILE_BASENAME);
        if (log_file) {
            fprintf(log_file, "User data directory: %s\n", pref_path_str_main);
            fprintf(log_file, "User text file path set to: %s\n", actual_text_file_path);
            fprintf(log_file, "User stats file path set to: %s\n", actual_stats_file_path);
        }
        SDL_free(pref_path_str_main);
    } else {
        if (log_file) fprintf(log_file, "Warning: SDL_GetPrefPath() failed: %s. Falling back to paths relative to executable for user data.\n", SDL_GetError());
        char* base_path_fallback_main = SDL_GetBasePath();
        if (base_path_fallback_main) {
            snprintf(actual_text_file_path, sizeof(actual_text_file_path), "%s%s", base_path_fallback_main, TEXT_FILE_PATH_BASENAME);
            snprintf(actual_stats_file_path, sizeof(actual_stats_file_path), "%s%s", base_path_fallback_main, STATS_FILE_BASENAME);
            SDL_free(base_path_fallback_main);
        } else {
            strncpy(actual_text_file_path, TEXT_FILE_PATH_BASENAME, sizeof(actual_text_file_path) - 1); actual_text_file_path[sizeof(actual_text_file_path) - 1] = '\0';
            strncpy(actual_stats_file_path, STATS_FILE_BASENAME, sizeof(actual_stats_file_path) - 1); actual_stats_file_path[sizeof(actual_stats_file_path) - 1] = '\0';
             if (log_file) fprintf(log_file, "Warning: SDL_GetBasePath() also failed. Using Current Working Directory for data files.\n");
        }
        if (log_file) { fprintf(log_file, "Fallback user text file path: %s\nFallback user stats file path: %s\n", actual_text_file_path, actual_stats_file_path); }
    }

    char* bundle_resources_path_base_main = SDL_GetBasePath();
    if (bundle_resources_path_base_main) {
        #if defined(__APPLE__)
            snprintf(default_text_file_in_bundle, sizeof(default_text_file_in_bundle), "%s../Resources/%s", bundle_resources_path_base_main, TEXT_FILE_PATH_BASENAME);
        #elif defined(_WIN32)
            // Для Windows, SDL_GetBasePath() зазвичай повертає <app_dir>/bin/, якщо програма встановлена за допомогою нашого CMake скрипта.
            // text.txt встановлюється в <app_dir>/.
            // Отже, шлях до text.txt відносно виконуваного файлу буде "../text.txt".
            snprintf(default_text_file_in_bundle, sizeof(default_text_file_in_bundle), "%s../%s", bundle_resources_path_base_main, TEXT_FILE_PATH_BASENAME);
            FILE *test_f = fopen(default_text_file_in_bundle, "rb");
            if (test_f) {
                fclose(test_f);
                if (log_file) fprintf(log_file, "INFO: Знайдено стандартний text.txt у встановленому місці: %s\n", default_text_file_in_bundle);
            } else {
                // Резервний варіант для випадків, коли виконуваний файл знаходиться в тій самій директорії, що й text.txt (наприклад, локальна збірка, а не встановлена версія)
                snprintf(default_text_file_in_bundle, sizeof(default_text_file_in_bundle), "%s%s", bundle_resources_path_base_main, TEXT_FILE_PATH_BASENAME);
                if (log_file) fprintf(log_file, "INFO: Стандартний text.txt не знайдено за шляхом '%s../%s'. Спроба '%s%s' (наприклад, локальна збірка).\n", bundle_resources_path_base_main, TEXT_FILE_PATH_BASENAME, bundle_resources_path_base_main, TEXT_FILE_PATH_BASENAME);
            }
        #elif defined(__linux__)
            // Для Linux, text.txt копіюється поруч із виконуваним файлом у директорії збірки.
            // При встановленні він може бути розміщений деінде, але типова команда `make install` може помістити його в share/
            // Наразі припускаємо, що для "стандартного комплектного" випадку він знаходиться відносно виконуваного файлу.
            snprintf(default_text_file_in_bundle, sizeof(default_text_file_in_bundle), "%s%s", bundle_resources_path_base_main, TEXT_FILE_PATH_BASENAME);
        #else
            // Загальний резервний варіант
            snprintf(default_text_file_in_bundle, sizeof(default_text_file_in_bundle), "%s%s", bundle_resources_path_base_main, TEXT_FILE_PATH_BASENAME);
        #endif
        SDL_free(bundle_resources_path_base_main);
        bundle_resources_path_base_main = NULL;
    } else {
        // Резервний варіант, якщо SDL_GetBasePath() не спрацював
        strncpy(default_text_file_in_bundle, TEXT_FILE_PATH_BASENAME, sizeof(default_text_file_in_bundle)-1);
        default_text_file_in_bundle[sizeof(default_text_file_in_bundle)-1] = '\0';
        if (log_file) fprintf(log_file, "Попередження: SDL_GetBasePath() не спрацював для визначення шляху до стандартного text.txt. Використовується поточна робоча директорія як резервний варіант для стандартного тексту: %s\n", default_text_file_in_bundle);
    }
    if (log_file) { fprintf(log_file, "Path to default text.txt (source for initial copy if user's own is missing): %s\n", default_text_file_in_bundle); fflush(log_file); }

    FILE *text_file_handle_main = fopen(actual_text_file_path, "rb");
    if (!text_file_handle_main) {
        if (log_file) fprintf(log_file, "User-specific text.txt not found at '%s'. Attempting to copy from default location '%s'. Error (for user file): %s\n", actual_text_file_path, default_text_file_in_bundle, strerror(errno));
        FILE *default_file_handle = fopen(default_text_file_in_bundle, "rb");
        if (default_file_handle) {
            fseek(default_file_handle, 0, SEEK_END);
            long default_file_size_long = ftell(default_file_handle);
            if (default_file_size_long > 0 && default_file_size_long < MAX_TEXT_LEN) {
                size_t default_file_size = (size_t)default_file_size_long;
                char* temp_copy_buffer = (char*)malloc(default_file_size + 1);
                if (temp_copy_buffer) {
                    fseek(default_file_handle, 0, SEEK_SET);
                    if (fread(temp_copy_buffer, 1, default_file_size, default_file_handle) == default_file_size) {
                        temp_copy_buffer[default_file_size] = '\0';
                        FILE* user_file_write_handle = fopen(actual_text_file_path, "wb");
                        if (user_file_write_handle) {
                            if (fwrite(temp_copy_buffer, 1, default_file_size, user_file_write_handle) == default_file_size) {
                                if (log_file) fprintf(log_file, "Successfully copied default text content to user's path '%s'\n", actual_text_file_path);
                            } else {
                                if (log_file) fprintf(log_file, "Error writing copied text to user's path '%s': %s\n", actual_text_file_path, strerror(errno));
                            }
                            fclose(user_file_write_handle);
                            text_file_handle_main = fopen(actual_text_file_path, "rb");
                        } else {
                            if (log_file) fprintf(log_file, "Error: Could not open user text file '%s' for writing the copy: %s\n", actual_text_file_path, strerror(errno));
                        }
                    } else {
                        if (log_file) fprintf(log_file, "Error reading content from default text file '%s': %s\n", default_text_file_in_bundle, strerror(errno));
                    }
                    free(temp_copy_buffer);
                } else {
                    if (log_file) fprintf(log_file, "Error: malloc failed for default text copy buffer.\n");
                }
            } else {
                if (log_file) {
                    if (default_file_size_long <= 0) fprintf(log_file, "Default text file '%s' is empty or error determining size (Size: %ld).\n", default_text_file_in_bundle, default_file_size_long);
                    else fprintf(log_file, "Default text file '%s' is too large (Size: %ld, Max allowed: %d).\n", default_text_file_in_bundle, default_file_size_long, MAX_TEXT_LEN);
                }
            }
            fclose(default_file_handle);
        } else {
            if (log_file) fprintf(log_file, "Error: Default text file '%s' also not found or not readable: %s.\n", default_text_file_in_bundle, strerror(errno));
        }
    } else {
        if (log_file) fprintf(log_file, "Successfully opened existing user text file: %s\n", actual_text_file_path);
    }
    if (log_file) fflush(log_file);

    if (!text_file_handle_main) {
        perror("Critical: Failed to open or create any text file for reading");
        fprintf(stderr, "User-specific path tried: %s\nDefault path tried: %s\n", actual_text_file_path, default_text_file_in_bundle);
        if (log_file) { fprintf(log_file, "CRITICAL: Failed to open any text file. Using empty text as fallback.\n"); }
        raw_text_content_main = strdup("");
        if (!raw_text_content_main) {
            if (log_file) {fprintf(log_file, "CRITICAL: strdup for empty text fallback failed.\n"); fclose(log_file);}
            return 1;
        }
        raw_text_len_main = 0;
    } else {
        fseek(text_file_handle_main, 0, SEEK_END);
        long file_size_long_main = ftell(text_file_handle_main);
        if (file_size_long_main < 0 || (size_t)file_size_long_main >= MAX_TEXT_LEN -1 ) {
            fprintf(stderr, "Error reading file size or file is too large.\n");
            if (log_file) fprintf(log_file, "Error: File '%s' size error (%ld bytes) or too large (Max: %d).\n", actual_text_file_path, file_size_long_main, MAX_TEXT_LEN);
            fclose(text_file_handle_main); if(log_file) { fflush(log_file); fclose(log_file); } return 1;
        }
        raw_text_len_main = (size_t)file_size_long_main;
        fseek(text_file_handle_main, 0, SEEK_SET);
        raw_text_content_main = (char*)malloc(raw_text_len_main + 1);
        if (!raw_text_content_main) {
            perror("malloc for text content");
            if(log_file) fprintf(log_file, "Error: malloc failed for text content from '%s': %s\n", actual_text_file_path, strerror(errno));
            fclose(text_file_handle_main); if(log_file) { fflush(log_file); fclose(log_file); } return 1;
        }
        size_t bytes_read_main = fread(raw_text_content_main, 1, raw_text_len_main, text_file_handle_main);
        fclose(text_file_handle_main); text_file_handle_main = NULL;
        if (bytes_read_main != raw_text_len_main) {
            if (log_file) fprintf(log_file, "WARN: fread mismatch from '%s'. Expected %zu bytes, got %zu. Error: %s\n", actual_text_file_path, raw_text_len_main, bytes_read_main, strerror(errno));
            raw_text_len_main = bytes_read_main;
        }
        raw_text_content_main[raw_text_len_main] = '\0';
    }

    size_t final_text_len_val = 0;
    char *text_to_type = PreprocessText(raw_text_content_main, raw_text_len_main, &final_text_len_val);
    free(raw_text_content_main); raw_text_content_main = NULL;

    if (!text_to_type) {
        perror("Failed to preprocess text");
        if(log_file) { fprintf(log_file, "CRITICAL: Failed to preprocess text. Text content might be null or malformed from file reading.\n"); fclose(log_file); }
        return 1;
    }
    if (log_file && final_text_len_val == 0) {
        fprintf(log_file, "Warning: Text content after preprocessing is empty.\n");
    }

    if (!InitializeApp(&appCtx, PROJECT_NAME_STR)) { // Line 1562 (new context)
        free(text_to_type);
        if(log_file) { fprintf(log_file, "CRITICAL: Failed to initialize application context (SDL, TTF, Font, etc.).\n"); fclose(log_file); }
        return 1;
    }

    char *input_buffer = (char*)calloc(final_text_len_val + 100, 1);
    if (!input_buffer && (final_text_len_val + 100 > 0) ) {
        perror("Failed to allocate input buffer");
        if(log_file) { fprintf(log_file, "CRITICAL: Failed to allocate input buffer.\n"); fclose(log_file); }
        CleanupApp(&appCtx); free(text_to_type); return 1;
    }

    size_t current_input_byte_idx_main = 0;
    Uint32 start_time_val = 0;
    bool show_cursor_flag = true;
    Uint32 last_blink_time = SDL_GetTicks();
    bool quit_game_flag = false;

    SDL_StartTextInput();

    while (!quit_game_flag) {
        SDL_Event event_main_loop;
        size_t old_input_idx_main = current_input_byte_idx_main;

        HandleAppEvents(&event_main_loop, &current_input_byte_idx_main, input_buffer,
                        final_text_len_val, text_to_type,
                        &typing_started_main, &start_time_val, &quit_game_flag);

        if (quit_game_flag) break;

        if (current_input_byte_idx_main != old_input_idx_main) {
            predictive_scroll_triggered_for_this_input_idx = false;
            y_offset_due_to_prediction_for_current_idx = 0;
        }

        if (!is_paused && SDL_GetTicks() - last_blink_time > 500) {
            show_cursor_flag = !show_cursor_flag;
            last_blink_time = SDL_GetTicks();
        } else if (is_paused) {
            show_cursor_flag = true;
        }

        SDL_SetRenderDrawColor(appCtx.ren, appCtx.palette[COL_BG].r, appCtx.palette[COL_BG].g, appCtx.palette[COL_BG].b, appCtx.palette[COL_BG].a);
        SDL_RenderClear(appCtx.ren);

        int timer_h_val = 0; int timer_w_val = 0;
        Uint32 elapsed_ms_for_timer_render;
        if (typing_started_main) {
            if (is_paused) {
                elapsed_ms_for_timer_render = time_at_pause_ms - start_time_val;
            } else {
                elapsed_ms_for_timer_render = SDL_GetTicks() - start_time_val;
            }
        } else {
            elapsed_ms_for_timer_render = 0;
        }
        RenderAppTimer(&appCtx, elapsed_ms_for_timer_render, &timer_h_val, &timer_w_val);

        RenderLiveStats(&appCtx, SDL_GetTicks(), start_time_val, text_to_type, input_buffer,
                        current_input_byte_idx_main, TEXT_AREA_X, timer_w_val,
                        TEXT_AREA_PADDING_Y, timer_h_val);

        int text_viewport_top_y_val = TEXT_AREA_PADDING_Y + timer_h_val + TEXT_AREA_PADDING_Y;

        int logical_cursor_abs_y_pos = 0;
        int logical_cursor_x_on_line = 0;
        CalculateCursorLayout(&appCtx, text_to_type, final_text_len_val, current_input_byte_idx_main,
                              &logical_cursor_abs_y_pos, &logical_cursor_x_on_line);

        int y_coord_for_scroll_update;
        if (is_paused) {
            y_coord_for_scroll_update = logical_cursor_abs_y_pos;
        } else {
            if (predictive_scroll_triggered_for_this_input_idx) {
                y_coord_for_scroll_update = logical_cursor_abs_y_pos + y_offset_due_to_prediction_for_current_idx;
            } else {
                y_coord_for_scroll_update = logical_cursor_abs_y_pos;
                if (appCtx.line_h > 0) {
                    int current_abs_line_of_cursor = logical_cursor_abs_y_pos / appCtx.line_h;
                    int target_abs_line_for_viewport_focus = first_visible_abs_line_num_static + CURSOR_TARGET_VIEWPORT_LINE;

                    if (current_abs_line_of_cursor == target_abs_line_for_viewport_focus && current_input_byte_idx_main < final_text_len_val) {
                        size_t next_char_idx_in_doc = 0;
                        const char* p_next_char_scanner_main = text_to_type + current_input_byte_idx_main;
                        const char* temp_scan_ptr_main = p_next_char_scanner_main;
                        Sint32 cp_next_char_main = decode_utf8(&temp_scan_ptr_main, text_to_type + final_text_len_val);

                        if (cp_next_char_main > 0 && temp_scan_ptr_main > p_next_char_scanner_main) {
                            next_char_idx_in_doc = (size_t)(temp_scan_ptr_main - text_to_type);
                        } else {
                            next_char_idx_in_doc = current_input_byte_idx_main + 1;
                            if(next_char_idx_in_doc > final_text_len_val) next_char_idx_in_doc = final_text_len_val;
                        }

                        if (next_char_idx_in_doc <= final_text_len_val && next_char_idx_in_doc > current_input_byte_idx_main) {
                            int y_of_next_char_logical, x_of_next_char_logical;
                            CalculateCursorLayout(&appCtx, text_to_type, final_text_len_val, next_char_idx_in_doc,
                                                  &y_of_next_char_logical, &x_of_next_char_logical);
                            if (y_of_next_char_logical > logical_cursor_abs_y_pos) {
                                int potential_new_first_visible_abs_line = (y_of_next_char_logical / appCtx.line_h) - CURSOR_TARGET_VIEWPORT_LINE;
                                if (potential_new_first_visible_abs_line < 0) potential_new_first_visible_abs_line = 0;
                                if (potential_new_first_visible_abs_line > first_visible_abs_line_num_static) {
                                    y_coord_for_scroll_update = y_of_next_char_logical;
                                    y_offset_due_to_prediction_for_current_idx = y_of_next_char_logical - logical_cursor_abs_y_pos;
                                    predictive_scroll_triggered_for_this_input_idx = true;
                                }
                            }
                        }
                    }
                }
            }
        }

        if (!is_paused) {
            UpdateVisibleLine(y_coord_for_scroll_update, appCtx.line_h, &first_visible_abs_line_num_static);
        }

        int final_cursor_x_on_screen_calculated = -100;
        int final_cursor_y_baseline_on_screen_calculated = -100;
        RenderTextContent(&appCtx, text_to_type, final_text_len_val, input_buffer,
                          current_input_byte_idx_main, first_visible_abs_line_num_static,
                          text_viewport_top_y_val, &final_cursor_x_on_screen_calculated, &final_cursor_y_baseline_on_screen_calculated);

        RenderAppCursor(&appCtx, show_cursor_flag, final_cursor_x_on_screen_calculated, final_cursor_y_baseline_on_screen_calculated,
                        logical_cursor_abs_y_pos, first_visible_abs_line_num_static, text_viewport_top_y_val);

        SDL_RenderPresent(appCtx.ren);
        SDL_Delay(16);
    }

    SDL_StopTextInput();

    if (typing_started_main) {
        CalculateAndPrintAppStats(start_time_val, current_input_byte_idx_main,
                                  text_to_type, final_text_len_val, input_buffer);

        if (current_input_byte_idx_main > 0 && current_input_byte_idx_main <= final_text_len_val) {
            if (actual_text_file_path[0] != '\0') {
                FILE *output_file_handle = fopen(actual_text_file_path, "w");
                if (output_file_handle) {
                    const char *remaining_text_ptr = text_to_type + current_input_byte_idx_main;
                    size_t remaining_len = final_text_len_val - current_input_byte_idx_main;
                    if (remaining_len > 0) {
                        if (fwrite(remaining_text_ptr, 1, remaining_len, output_file_handle) != remaining_len) {
                            if (log_file) fprintf(log_file, "ERROR: Failed to write all remaining text to '%s'. Error: %s\n", actual_text_file_path, strerror(errno));
                            perror("Error writing remaining text to file");
                        } else {
                             if (log_file) fprintf(log_file, "Successfully wrote remaining %zu bytes of text to '%s'.\n", remaining_len, actual_text_file_path);
                        }
                    } else {
                        if (log_file) fprintf(log_file, "All text processed. User text file '%s' is now empty.\n", actual_text_file_path);
                    }
                    fclose(output_file_handle);
                } else {
                    if (log_file) fprintf(log_file, "ERROR: Could not open user text file '%s' for writing to save remaining text. Error: %s\n", actual_text_file_path, strerror(errno));
                    perror("Error opening text file for writing remaining text");
                }
            } else {
                if (log_file) fprintf(log_file, "ERROR: User text file path (actual_text_file_path) is empty. Cannot update text file with remaining text.\n");
            }
        } else {
            char temp_log_path_buffer_main[1024];
            snprintf(temp_log_path_buffer_main, sizeof(temp_log_path_buffer_main), "%s", actual_text_file_path[0] ? actual_text_file_path : TEXT_FILE_PATH_BASENAME);
            if (current_input_byte_idx_main == 0) {
                 if (log_file) fprintf(log_file, "No text processed (0 bytes typed according to current_input_byte_idx_main). Text file '%s' not modified.\n", temp_log_path_buffer_main);
            } else {
                 if (log_file) fprintf(log_file, "WARN: current_input_byte_idx_main (%zu) > final_text_len_val (%zu). Text file '%s' not modified.\n", current_input_byte_idx_main, final_text_len_val, temp_log_path_buffer_main);
            }
        }
    } else {
        printf("No typing started. Stats not saved. Text file not modified.\n");
        if (log_file) {
            char temp_log_path_main[1024];
            snprintf(temp_log_path_main, sizeof(temp_log_path_main), "%s", actual_text_file_path[0] ? actual_text_file_path : TEXT_FILE_PATH_BASENAME);
            fprintf(log_file, "No typing started. Text file '%s' not modified.\n", temp_log_path_main);
        }
    }

    CleanupApp(&appCtx);
    if (text_to_type) free(text_to_type); text_to_type = NULL;
    if (input_buffer) free(input_buffer); input_buffer = NULL;

    if (log_file) {
        fputs("Application finished normally.\n", log_file);
        fflush(log_file);
        fclose(log_file);
        log_file = NULL;
    }
    return 0;
}
