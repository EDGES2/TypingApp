#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h> // For iscntrl, isspace
#include <SDL2/SDL_filesystem.h> // For SDL_GetBasePath()
#include <sys/errno.h>

// --- Application Constants ---
#define WINDOW_W     800
#define WINDOW_H     200
#define FONT_SIZE    14
#define MAX_TEXT_LEN (5 * 1024 * 1024)
#ifndef TEXT_FILE_PATH_BASENAME // Use a basename for flexibility
#define TEXT_FILE_PATH_BASENAME "text.txt"
#endif
#ifndef PROJECT_NAME_STR // Define if not set by CMake (for fallback path)
#define PROJECT_NAME_STR "TypingApp"
#endif


#define TEXT_AREA_X 10
#define TEXT_AREA_PADDING_Y 10
#define TEXT_AREA_W (WINDOW_W - (2 * TEXT_AREA_X))
#define DISPLAY_LINES 3
#define CURSOR_TARGET_VIEWPORT_LINE 1
#define TAB_SIZE_IN_SPACES 4

// Logging switch: 1 to enable regular game event logs, 0 to disable them.
// Critical errors to stderr and app start/finish to log_file (if open) are always active.
#define ENABLE_GAME_LOGS 0 // User requested to disable detailed logs by default

// Color palette indices
enum { COL_BG, COL_TEXT, COL_CORRECT, COL_INCORRECT, COL_CURSOR, N_COLORS };

// --- Data Structures ---
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

// --- Global Static Variables ---
static int first_visible_abs_line_num_static = 0;
static bool typing_started_main = false;
static FILE *log_file = NULL;
// For accuracy calculation (these were in the user's provided code that had the redefinition error)
static unsigned long long total_keystrokes_for_accuracy = 0;
static unsigned long long total_errors_committed_for_accuracy = 0;
// Predictive scroll variables (also from user's provided code)
static bool predictive_scroll_triggered_for_this_input_idx = false;
static int y_offset_due_to_prediction_for_current_idx = 0;


// --- Helper Function Definitions (All functions except main are defined before main) ---
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
             if (cp > 0) {
                char_count++;
            } else if (cp == 0 && p < end) {
                 char_count++;
            } else if (cp == -1) {
                 char_count++;
            } else if (cp == 0 && p == end) {
                break;
            }
        } else {
            if (p < end) p++; else break;
        }
    }
    return char_count;
}


int get_codepoint_advance_and_metrics_func(AppContext *appCtx, Uint32 codepoint, int fallback_adv,
                                     int *out_char_w, int *out_char_h) {
    int adv = 0;
    int char_w = 0;
    int char_h = 0;

    if (!appCtx || !appCtx->font) {
        if (out_char_w) *out_char_w = fallback_adv;
        if (out_char_h) *out_char_h = FONT_SIZE;
        return fallback_adv;
    }
    char_h = TTF_FontHeight(appCtx->font);

    if (codepoint < 128 && codepoint >= 32) {
        adv = appCtx->glyph_adv_cache[codepoint];
        if (appCtx->glyph_w_cache[COL_TEXT][codepoint] > 0) char_w = appCtx->glyph_w_cache[COL_TEXT][codepoint];
        else char_w = adv;
        if (appCtx->glyph_h_cache[COL_TEXT][codepoint] > 0) char_h = appCtx->glyph_h_cache[COL_TEXT][codepoint];
        else char_h = TTF_FontHeight(appCtx->font);
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

TextBlockInfo get_next_text_block_func(AppContext *appCtx, const char **text_parser_ptr_ref, const char *text_end,
                                       int current_pen_x_for_tab_calc) {
    TextBlockInfo block = {0};
    if (!text_parser_ptr_ref || !*text_parser_ptr_ref || *text_parser_ptr_ref >= text_end || !appCtx || !appCtx->font) {
        return block;
    }

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
            block.pixel_width = appCtx->space_advance_width;
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
    if (!appCtx) return false;
    memset(appCtx, 0, sizeof(AppContext));

    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError()); return false; }
    if (TTF_Init() != 0) { fprintf(stderr, "TTF_Init error: %s\n", TTF_GetError()); SDL_Quit(); return false; }
    TTF_SetFontHinting(NULL, TTF_HINTING_LIGHT);

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");
    appCtx->win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_W, WINDOW_H, SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!appCtx->win) { fprintf(stderr, "SDL_CreateWindow error: %s\n", SDL_GetError()); TTF_Quit(); SDL_Quit(); return false; }

    appCtx->ren = SDL_CreateRenderer(appCtx->win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!appCtx->ren) { fprintf(stderr, "SDL_CreateRenderer error: %s\n", SDL_GetError()); SDL_DestroyWindow(appCtx->win); TTF_Quit(); SDL_Quit(); return false; }

    int physW_val, physH_val;
    SDL_GetRendererOutputSize(appCtx->ren, &physW_val, &physH_val);
    float scale_x = (float)physW_val / WINDOW_W; float scale_y = (float)physH_val / WINDOW_H;
    SDL_RenderSetScale(appCtx->ren, scale_x, scale_y);

    const char* font_paths[] = { "Arial.ttf", "/System/Library/Fonts/Arial.ttf", "/Library/Fonts/Arial Unicode.ttf", "/usr/share/fonts/truetype/msttcorefonts/Arial.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", NULL };
    for (int i = 0; font_paths[i] != NULL; ++i) {
        #if SDL_TTF_VERSION_ATLEAST(2,20,0)
            appCtx->font = TTF_OpenFontDPI(font_paths[i], FONT_SIZE, (int)(72 * scale_x), (int)(72 * scale_y));
        #else
            appCtx->font = TTF_OpenFont(font_paths[i], FONT_SIZE);
        #endif
        if (appCtx->font) {
#if ENABLE_GAME_LOGS
            if (log_file) {
                fprintf(stderr, "DEBUG: Attempting to log 'Loaded font'. Current ferror: %d\n", ferror(log_file));
                int ret = fprintf(log_file, "Loaded font: %s (line_h from TTF_FontLineSkip: %d, from TTF_FontHeight: %d)\n",
                                   font_paths[i], TTF_FontLineSkip(appCtx->font), TTF_FontHeight(appCtx->font));
                if (ret < 0) {
                    fprintf(stderr, "DEBUG: Error writing 'Loaded font' log to log_file. fprintf returned %d. errno: %s. ferror: %d\n", ret, strerror(errno), ferror(log_file));
                }
                int flush_ret = fflush(log_file);
                if (flush_ret == EOF) {
                    fprintf(stderr, "DEBUG: Error flushing 'Loaded font' log. fflush returned EOF. errno: %s. ferror: %d\n", strerror(errno), ferror(log_file));
                }
                fprintf(stderr, "DEBUG: After 'Loaded font' log attempt. ferror: %d\n", ferror(log_file));
            }
#endif
            break;
        }
    }
    if (!appCtx->font) { fprintf(stderr, "Failed to load font: %s\n", TTF_GetError()); SDL_DestroyRenderer(appCtx->ren); SDL_DestroyWindow(appCtx->win); TTF_Quit(); SDL_Quit(); return false;}

    appCtx->palette[COL_BG]        = (SDL_Color){50, 52, 55, 255};
    appCtx->palette[COL_TEXT]      = (SDL_Color){100,102,105,255};
    appCtx->palette[COL_CORRECT]   = (SDL_Color){201,200,190,255};
    appCtx->palette[COL_INCORRECT] = (SDL_Color){200,  0,  0,255};
    appCtx->palette[COL_CURSOR]    = (SDL_Color){255,200,  0,255};

    appCtx->line_h = TTF_FontLineSkip(appCtx->font);
    if (appCtx->line_h <= 0) appCtx->line_h = TTF_FontHeight(appCtx->font);
    if (appCtx->line_h <= 0) appCtx->line_h = FONT_SIZE + 4;

    for (int c = 32; c < 127; c++) {
        int adv_val;
        if (TTF_GlyphMetrics(appCtx->font, (Uint16)c, NULL, NULL, NULL, NULL, &adv_val) != 0) {
            adv_val = FONT_SIZE / 2;
        }
        appCtx->glyph_adv_cache[c] = (adv_val > 0) ? adv_val : FONT_SIZE/2;

        for (int col_idx = COL_TEXT; col_idx <= COL_INCORRECT; col_idx++) {
            SDL_Surface *surf = TTF_RenderGlyph_Blended(appCtx->font, (Uint16)c, appCtx->palette[col_idx]);
            if (!surf) {
                continue;
            }
            appCtx->glyph_w_cache[col_idx][c] = surf->w;
            appCtx->glyph_h_cache[col_idx][c] = surf->h;
            appCtx->glyph_tex_cache[col_idx][c] = SDL_CreateTextureFromSurface(appCtx->ren, surf);
            if (!appCtx->glyph_tex_cache[col_idx][c]) {
                 fprintf(stderr, "Warning: Failed to create texture for glyph %c (ASCII %d) color %d\n", c, c, col_idx);
            }
            SDL_FreeSurface(surf);
            surf = NULL;
        }
    }
    appCtx->space_advance_width = appCtx->glyph_adv_cache[' '];
    if (appCtx->space_advance_width <= 0) appCtx->space_advance_width = FONT_SIZE / 3;
    appCtx->tab_width_pixels = (appCtx->space_advance_width > 0) ? (TAB_SIZE_IN_SPACES * appCtx->space_advance_width) : (TAB_SIZE_IN_SPACES * (FONT_SIZE / 3));

    #if ENABLE_GAME_LOGS
    if (log_file) {
        fprintf(log_file, "SECOND TEST LOG LINE.\n");
        if (fflush(log_file) == EOF) {
            fprintf(stderr, "Error flushing SECOND TEST LOG LINE.\n");
        }
        fprintf(stderr, "Attempted SECOND TEST LOG. ferror: %d\n", ferror(log_file));
    }
    #endif
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
        return empty_str;
    }

    size_t temp_buffer_capacity = raw_text_len * 2 + 10;
    char *temp_buffer = (char*)malloc(temp_buffer_capacity);
    if (!temp_buffer) {
        perror("Failed to allocate temporary buffer in PreprocessText");
        *out_final_text_len = 0;
        return NULL;
    }

    size_t temp_w_idx = 0;
    const char* p_read = raw_text_buffer;
    const char* p_read_end = raw_text_buffer + raw_text_len;

    // Pass 1: Character replacements and initial newline/CR handling
    while (p_read < p_read_end) {
    // 1. Розширення буфера за потреби
    if (temp_w_idx + 4 >= temp_buffer_capacity) { // Ensure space for up to 4 bytes (max UTF-8 char or replacement)
        temp_buffer_capacity = temp_buffer_capacity * 2 + 4;
        char *new_temp_buffer = (char *)realloc(temp_buffer, temp_buffer_capacity);
        if (!new_temp_buffer) {
            perror("Failed to reallocate temporary buffer in PreprocessText (Pass 1)");
            free(temp_buffer);
            *out_final_text_len = 0;
            return NULL;
        }
        temp_buffer = new_temp_buffer;
    }

    // 2. Нормалізація CR / CR+LF → LF
    if (*p_read == '\r') {
        p_read++;
        if (p_read < p_read_end && *p_read == '\n') p_read++;
        temp_buffer[temp_w_idx++] = '\n';
        continue;
    }

    // 3. Спеціальні заміни Unicode-символів
    //   • "--" → EM DASH (U+2014)
    //   • '—' (EM DASH U+2014) → '–' (EN DASH U+2013)  <-- ADDED RULE DESCRIPTION
    //   • "…" → "..."
    //   • «розумні» лапки → апостроф '
    if (*p_read == '-' && (p_read + 1 < p_read_end) && *(p_read + 1) == '-') {
        // EM DASH (U+2014) в UTF-8
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
        if (orig_len == 0 && p_read < p_read_end) p_read++; // Advance if stuck on invalid byte
        continue;
    }

    // --- START OF ADDED LOGIC ---
    // Заміна EM DASH (U+2014) на EN DASH (U+2013)
    if (cp == 0x2014) { // Якщо декодований символ - це EM DASH (U+2014)
        // Перевіряємо, чи достатньо місця для EN DASH (3 байти UTF-8)
        if (temp_w_idx + 3 <= temp_buffer_capacity) {
            temp_buffer[temp_w_idx++] = (char)0xE2;
            temp_buffer[temp_w_idx++] = (char)0x80;
            temp_buffer[temp_w_idx++] = (char)0x93; // EN DASH (U+2013)
            continue; // Переходимо до наступного символу вхідного тексту
        } else {
            // Недостатньо місця в буфері. Це не повинно статися через логіку розширення вище.
            // Для безпеки, зупиняємо обробку, як в інших подібних випадках.
            break;
        }
    }
    // --- END OF ADDED LOGIC ---

    if (cp == 0x2026) { // HORIZONTAL ELLIPSIS (U+2026)
        if (temp_w_idx + 3 <= temp_buffer_capacity) { // "..." is 3 bytes
            temp_buffer[temp_w_idx++] = '.';
            temp_buffer[temp_w_idx++] = '.';
            temp_buffer[temp_w_idx++] = '.';
            continue;
        }
        break; // Not enough space
    }
    if (cp == 0x2018 || cp == 0x2019 || cp == 0x201C || cp == 0x201D) { // Smart quotes (U+2018, U+2019, U+201C, U+201D)
        if (temp_w_idx + 1 <= temp_buffer_capacity) { // "'" is 1 byte
            temp_buffer[temp_w_idx++] = '\'';
            continue;
        }
        break; // Not enough space
    }

    // 4. Всі інші символи — копіюємо «як є»
    //    (Якщо це був EM DASH, ми б вже виконали 'continue' вище)
    if (temp_w_idx + orig_len <= temp_buffer_capacity) {
        memcpy(temp_buffer + temp_w_idx, char_start, orig_len);
        temp_w_idx += orig_len;
    } else {
        // Not enough space for the original character; should be rare due to initial buffer check.
        break;
    }
}

// 5. Завершуємо нуль-термінатором
temp_buffer[temp_w_idx] = '\0';

    // Pass 2: Process newlines, collapse multiple spaces, trim leading/trailing whitespace
    char *processed_text = (char*)malloc(temp_w_idx + 1);
    if (!processed_text) {
        perror("Failed to allocate processed_text in PreprocessText");
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

    // Skip leading whitespace from temp_buffer
    while(p2_read < p2_read_end) {
        const char* peek_ptr = p2_read;
        Sint32 cp_peek = decode_utf8(&peek_ptr, p2_read_end);
        if (cp_peek == ' ' || cp_peek == '\n') {
            p2_read = peek_ptr;
        } else {
            break;
        }
    }

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
        } else { // Non-newline character
            if (consecutive_newlines > 0) {
                if (content_has_started || final_pt_idx > 0) {  // Only add separator if not truly the beginning of output
                    if (final_pt_idx > 0 && processed_text[final_pt_idx - 1] == ' ') {
                         final_pt_idx--;
                    }
                    if (consecutive_newlines >= 2) { // Paragraph break
                        if (final_pt_idx == 0 || (final_pt_idx > 0 && processed_text[final_pt_idx - 1] != '\n')) {
                             if (final_pt_idx < temp_w_idx) processed_text[final_pt_idx++] = '\n'; else break;
                        }
                    } else { // Single newline becomes a space
                        if (final_pt_idx > 0 && processed_text[final_pt_idx - 1] != ' ' && processed_text[final_pt_idx - 1] != '\n') {
                             if (final_pt_idx < temp_w_idx) processed_text[final_pt_idx++] = ' '; else break;
                        } else if (final_pt_idx == 0 && content_has_started ) { // If first content was preceded by single \n
                             if (final_pt_idx < temp_w_idx) processed_text[final_pt_idx++] = ' '; else break;
                        }
                    }
                }
                last_char_output_was_space = true;
            }
            consecutive_newlines = 0;

            if (cp2 == ' ') {
                if (content_has_started && !last_char_output_was_space) {
                    if (final_pt_idx < temp_w_idx) processed_text[final_pt_idx++] = ' '; else break;
                }
                last_char_output_was_space = true;
            } else {
                // If the last action was writing a newline (from consecutive_newlines >=2),
                // and now we have content, last_char_output_was_space should be true to prevent double space if content starts with space.
                // But if last was newline, and current is content, we don't want a leading space.
                if (last_char_output_was_space && final_pt_idx > 0 && processed_text[final_pt_idx-1] == '\n' ) {
                    // Correct: if previous output was newline, this new char is start of new line, no leading space needed.
                } else if (last_char_output_was_space && final_pt_idx > 0 && content_has_started) {
                     // This case might be redundant with the content_has_started && !last_char_output_was_space for spaces.
                }


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
        // A single trailing newline from input is effectively ignored/trimmed by the next step.
    }

    while (final_pt_idx > 0 && (processed_text[final_pt_idx-1] == ' ' || processed_text[final_pt_idx-1] == '\n')) {
        final_pt_idx--;
    }
    processed_text[final_pt_idx] = '\0';

    *out_final_text_len = final_pt_idx;
    free(temp_buffer);

    char* final_text = (char*)realloc(processed_text, final_pt_idx + 1);
    if (!final_text && final_pt_idx == 0 && processed_text != NULL) {
        free(processed_text);
        final_text = (char*)malloc(1);
        if(final_text) final_text[0] = '\0';
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
        if (event->type == SDL_KEYDOWN) {
            if (event->key.keysym.sym == SDLK_ESCAPE) {
                *quit_flag = true;
                return;
            }
            if (event->key.keysym.sym == SDLK_BACKSPACE && *current_input_byte_idx > 0) {
                const char *start_of_buffer_bk = input_buffer;
                const char *current_end_of_input_bk = input_buffer + *current_input_byte_idx;
                const char *ptr_to_prev_char_bk = start_of_buffer_bk;
                const char *last_full_char_start_bk = start_of_buffer_bk;

                while(ptr_to_prev_char_bk < current_end_of_input_bk) {
                    const char *char_being_decoded_start_bk = ptr_to_prev_char_bk;
                    Sint32 cp_bk = decode_utf8(&ptr_to_prev_char_bk, current_end_of_input_bk);
                    if (ptr_to_prev_char_bk > current_end_of_input_bk || cp_bk <= 0) {
                         ptr_to_prev_char_bk = char_being_decoded_start_bk; break;
                    }
                    last_full_char_start_bk = char_being_decoded_start_bk;
                    if (ptr_to_prev_char_bk == current_end_of_input_bk) break;
                }
                *current_input_byte_idx = (size_t)(last_full_char_start_bk - start_of_buffer_bk);
                input_buffer[*current_input_byte_idx] = '\0';
            }
        }
        if (event->type == SDL_TEXTINPUT) {
            if (!(*typing_started) && final_text_len > 0) {
                *start_time = SDL_GetTicks();
                *typing_started = true;
                total_keystrokes_for_accuracy = 0;
                total_errors_committed_for_accuracy = 0;
            }

            size_t input_event_len_bytes = strlen(event->text.text);

            const char* p_event_char_iter = event->text.text;
            const char* event_text_end = event->text.text + input_event_len_bytes;
            size_t current_target_byte_offset_for_event = *current_input_byte_idx;

            while(p_event_char_iter < event_text_end) {
                const char* p_event_char_start_loop = p_event_char_iter;
                Sint32 cp_event = decode_utf8(&p_event_char_iter, event_text_end);
                if (cp_event <=0) { if (p_event_char_iter < event_text_end) p_event_char_iter++; else break; continue;}

                total_keystrokes_for_accuracy++;

                if (current_target_byte_offset_for_event < final_text_len) {
                    const char* p_target_char_at_offset = text_to_type + current_target_byte_offset_for_event;
                    const char* p_target_char_next_ptr_for_len = p_target_char_at_offset;
                    Sint32 cp_target = decode_utf8(&p_target_char_next_ptr_for_len, text_to_type + final_text_len);

                    if (cp_target <=0 || cp_event != cp_target) {
                        total_errors_committed_for_accuracy++;
                    }

                    if(cp_target > 0) {
                        current_target_byte_offset_for_event = (size_t)(p_target_char_next_ptr_for_len - text_to_type);
                    } else {
                         current_target_byte_offset_for_event++;
                    }
                } else {
                    total_errors_committed_for_accuracy++;
                    current_target_byte_offset_for_event++;
                }
                if (p_event_char_iter == p_event_char_start_loop && p_event_char_iter < event_text_end) p_event_char_iter++;
            }


            if (*current_input_byte_idx + input_event_len_bytes <= final_text_len + 90 ) {
                bool can_add_input = true;
                if(input_event_len_bytes == 1 && event->text.text[0] == ' ' && *current_input_byte_idx > 0){
                    if(input_buffer[(*current_input_byte_idx)-1] == ' ') can_add_input = false;
                }
                if(can_add_input){
                   strncat(input_buffer, event->text.text, input_event_len_bytes);
                   (*current_input_byte_idx) += input_event_len_bytes;
                }
            }
        }
    }
}

void RenderAppTimer(AppContext *appCtx, Uint32 elapsed_ms, int *out_timer_h, int *out_timer_w) {
    if (!appCtx || !appCtx->font || !appCtx->ren || !out_timer_h || !out_timer_w) return;
    Uint32 elapsed_s = elapsed_ms / 1000;
    int m = (int)(elapsed_s / 60);
    int s = (int)(elapsed_s % 60);
    char timer_buf[16];
    snprintf(timer_buf, sizeof(timer_buf), "%02d:%02d", m, s);

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
             timer_tex = NULL;
        } else {
            current_timer_h_val = appCtx->line_h;
            TTF_SizeText(appCtx->font, timer_buf, &current_timer_w_val, NULL);
        }
        SDL_FreeSurface(timer_surf);
        timer_surf = NULL;
    } else {
        current_timer_h_val = appCtx->line_h;
        TTF_SizeText(appCtx->font, timer_buf, &current_timer_w_val, NULL);
    }

    if (current_timer_h_val <= 0 && appCtx->line_h > 0) current_timer_h_val = appCtx->line_h;
    if (current_timer_w_val <= 0) current_timer_w_val = 50;

    *out_timer_h = current_timer_h_val;
    *out_timer_w = current_timer_w_val;
}

void RenderLiveStats(AppContext *appCtx, Uint32 current_ticks, Uint32 start_time_ticks,
                     const char *text_to_type,
                     const char *input_buffer, size_t current_input_byte_idx,
                     int timer_x_pos, int timer_width, int timer_y_pos, int timer_height) {

    if (!appCtx || !appCtx->font || !appCtx->ren || !typing_started_main ) {
        return;
    }

    float elapsed_seconds = (float)(current_ticks - start_time_ticks) / 1000.0f;
    if (elapsed_seconds < 0.05f && total_keystrokes_for_accuracy > 0) elapsed_seconds = 0.05f;
    else if (elapsed_seconds < 0.001f) elapsed_seconds = 0.001f;

    float elapsed_minutes = elapsed_seconds / 60.0f;

    float live_accuracy = 100.0f;
    if (total_keystrokes_for_accuracy > 0) {
        live_accuracy = ((float)(total_keystrokes_for_accuracy - total_errors_committed_for_accuracy) / (float)total_keystrokes_for_accuracy) * 100.0f;
        if (live_accuracy < 0) live_accuracy = 0;
    }

    size_t live_correct_keystrokes = (total_keystrokes_for_accuracy > total_errors_committed_for_accuracy) ? (total_keystrokes_for_accuracy - total_errors_committed_for_accuracy) : 0;
    float live_net_words_for_wpm = (float)live_correct_keystrokes / 5.0f;
    float live_wpm = (elapsed_minutes > 0.0001f) ? (live_net_words_for_wpm / elapsed_minutes) : 0.0f;

    int live_typed_words_count = 0;
    if (current_input_byte_idx > 0) {
        bool in_word_flag = false;
        const char* p_word_scan_iter = input_buffer;
        const char* p_word_scan_end_iter = input_buffer + current_input_byte_idx;
        while(p_word_scan_iter < p_word_scan_end_iter) {
            const char* temp_p_word = p_word_scan_iter;
            Sint32 cp_word = decode_utf8(&p_word_scan_iter, p_word_scan_end_iter);
            if (cp_word <= 0) {
                if (p_word_scan_iter < p_word_scan_end_iter) p_word_scan_iter++; else break;
                continue;
            }

            bool is_current_char_space = false;
            if (cp_word < 128) {
                is_current_char_space = isspace((unsigned char)cp_word);
            } else {
                is_current_char_space = false;
            }

            if (!is_current_char_space) {
                if (!in_word_flag) {
                    live_typed_words_count++;
                    in_word_flag = true;
                }
            } else {
                in_word_flag = false;
            }
             if (p_word_scan_iter == temp_p_word && p_word_scan_iter < p_word_scan_end_iter) p_word_scan_iter++;
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

    SDL_Surface *surf = TTF_RenderText_Blended(appCtx->font, wpm_buf, stat_color);
    if (surf) {
        SDL_Texture *tex = SDL_CreateTextureFromSurface(appCtx->ren, surf);
        if (tex) {
            SDL_Rect dst = {current_x_render_pos, stats_y_render_pos, surf->w, surf->h};
            SDL_RenderCopy(appCtx->ren, tex, NULL, &dst);
            current_x_render_pos += surf->w + 15;
            SDL_DestroyTexture(tex);
        }
        SDL_FreeSurface(surf);
    }

    surf = TTF_RenderText_Blended(appCtx->font, acc_buf, stat_color);
    if (surf) {
        SDL_Texture *tex = SDL_CreateTextureFromSurface(appCtx->ren, surf);
        if (tex) {
            SDL_Rect dst = {current_x_render_pos, stats_y_render_pos, surf->w, surf->h};
            SDL_RenderCopy(appCtx->ren, tex, NULL, &dst);
            current_x_render_pos += surf->w + 15;
            SDL_DestroyTexture(tex);
        }
        SDL_FreeSurface(surf);
    }

    surf = TTF_RenderText_Blended(appCtx->font, words_buf, stat_color);
    if (surf) {
        SDL_Texture *tex = SDL_CreateTextureFromSurface(appCtx->ren, surf);
        if (tex) {
            SDL_Rect dst = {current_x_render_pos, stats_y_render_pos, surf->w, surf->h};
            SDL_RenderCopy(appCtx->ren, tex, NULL, &dst);
            SDL_DestroyTexture(tex);
        }
        SDL_FreeSurface(surf);
    }
}


void CalculateAndPrintAppStats(Uint32 start_time_ms, size_t current_input_byte_idx,
                               const char *text_to_type, size_t final_text_len,
                               const char *input_buffer) {
    if (!typing_started_main) {
        printf("No typing done. No stats to display.\n");
        return;
    }
     if (total_keystrokes_for_accuracy == 0 && current_input_byte_idx == 0) {
        printf("No characters typed. No stats to display.\n");
        return;
    }

    Uint32 end_time_ms = SDL_GetTicks();
    float time_taken_seconds = (float)(end_time_ms - start_time_ms) / 1000.0f;

    if (time_taken_seconds <= 0.001f && total_keystrokes_for_accuracy == 0) {
        printf("Time taken is too short or no characters typed for meaningful stats.\n");
        return;
    }
     if (time_taken_seconds <= 0.001f) time_taken_seconds = 0.001f;

    size_t final_correct_keystrokes = (total_keystrokes_for_accuracy > total_errors_committed_for_accuracy) ?
                                      (total_keystrokes_for_accuracy - total_errors_committed_for_accuracy) : 0;

    float net_words = (float)final_correct_keystrokes / 5.0f;
    float wpm = (time_taken_seconds > 0.0001f) ? (net_words / time_taken_seconds) * 60.0f : 0.0f;

    float accuracy = 0.0f;
    if (total_keystrokes_for_accuracy > 0) {
         accuracy = ((float)final_correct_keystrokes / (float)total_keystrokes_for_accuracy) * 100.0f;
    }
     if (accuracy < 0.0f) accuracy = 0.0f;
     if (accuracy > 100.0f && total_keystrokes_for_accuracy > 0) accuracy = 100.0f;

    printf("\n--- Typing Stats (Final) ---\n");
    printf("Time Taken: %.2f seconds\n", time_taken_seconds);
    printf("WPM (Net): %.2f\n", wpm);
    printf("Correct Keystrokes: %zu\n", final_correct_keystrokes); // Changed %llu to %zu
    printf("Total Keystrokes: %llu\n", total_keystrokes_for_accuracy);
    printf("Committed Errors: %llu\n", total_errors_committed_for_accuracy);
    printf("Accuracy (based on all keystrokes): %.2f%%\n", accuracy);
    printf("--------------------\n");
}

void CalculateCursorLayout(AppContext *appCtx, const char *text_to_type, size_t final_text_len, size_t current_input_byte_idx,
                           int *out_cursor_abs_y, int *out_cursor_exact_x) {
    if (!appCtx || !text_to_type || !out_cursor_abs_y || !out_cursor_exact_x) return;

    int calculated_cursor_y = 0;
    int calculated_cursor_x = TEXT_AREA_X; // Початкова позиція X для розрахунку курсора
    int current_pen_x = TEXT_AREA_X;       // Поточна позиція X пера при обході тексту
    int current_line_abs_y = 0;          // Поточна абсолютна Y позиція (0 для першого рядка)

    const char *p_iter = text_to_type;
    const char *p_end = text_to_type + final_text_len;
    size_t processed_bytes_total = 0; // Кількість оброблених байт тексту
    bool cursor_position_found = false;

    if (current_input_byte_idx == 0) {
        *out_cursor_abs_y = 0;
        *out_cursor_exact_x = TEXT_AREA_X;
        #if ENABLE_GAME_LOGS
        // Log для нульового індексу залишається, якщо потрібен
        if (log_file) fprintf(log_file, "[CalculateCursorLayout] OUTPUT (idx 0): final_abs_y: %d, final_exact_x: %d\n", *out_cursor_abs_y, *out_cursor_exact_x);
        #endif
        return;
    }

    while(p_iter < p_end && !cursor_position_found) {
        size_t bytes_at_block_start = processed_bytes_total;
        int pen_x_at_block_start = current_pen_x;
        int line_y_at_block_start = current_line_abs_y;

        // Зберігаємо p_iter перед викликом get_next_text_block_func, щоб знати, де починався наступний блок для "заглядання вперед"
        const char* p_iter_before_current_block = p_iter;
        TextBlockInfo current_block = get_next_text_block_func(appCtx, &p_iter, p_end, pen_x_at_block_start);

        if (current_block.num_bytes == 0) {
            if(p_iter < p_end && p_iter == p_iter_before_current_block) { // Якщо get_next_text_block_func не змістив p_iter
                p_iter++; // Примусове зміщення, щоб уникнути зациклення
            }
            if (p_iter > p_iter_before_current_block) {
                 processed_bytes_total += (size_t)(p_iter - p_iter_before_current_block);
            } else if (p_iter_before_current_block < p_end) { // Якщо p_iter не змінився і не кінець
                 processed_bytes_total++; // Припускаємо, що хоча б один байт "пропущено"
            }
            continue;
        }

        int y_for_chars_in_this_block = line_y_at_block_start;
        int x_for_chars_in_this_block = pen_x_at_block_start;

        if (current_block.is_newline) {
            current_line_abs_y = line_y_at_block_start + appCtx->line_h;
            current_pen_x = TEXT_AREA_X;
        } else {
            // --- MODIFICATION START: Більш узгоджена логіка перенесення блоку ---
            bool must_wrap_this_block_for_calc = false;
            if (pen_x_at_block_start != TEXT_AREA_X) { // Переносимо, тільки якщо це не перший елемент на рядку
                if (current_block.is_word || current_block.is_tab) { // Для слів та табів
                    // Умова 1: Сам блок не вміщується
                    if (pen_x_at_block_start + current_block.pixel_width > TEXT_AREA_X + TEXT_AREA_W) {
                        must_wrap_this_block_for_calc = true;
                    } else {
                        // Умова 2 (для слів): Блок вміщується, але перевіримо, чи не призведе наступний пробіл до переповнення
                        // Це копіює логіку з RenderTextContent
                        if (current_block.is_word) { // Ця додаткова перевірка лише для слів
                            const char *p_peek_iter_calc = p_iter; // p_iter вже вказує на початок НАСТУПНОГО блоку
                            if (p_peek_iter_calc < p_end) {
                                const char *temp_peek_ptr_for_get_next_calc = p_peek_iter_calc; // Копія для get_next_text_block_func
                                int pen_x_after_current_block_calc = pen_x_at_block_start + current_block.pixel_width;

                                // Важливо: get_next_text_block_func змінює вказівник, тому передаємо копію
                                TextBlockInfo next_block_peek_calc = get_next_text_block_func(appCtx, &temp_peek_ptr_for_get_next_calc, p_end, pen_x_after_current_block_calc);

                                if (next_block_peek_calc.num_bytes > 0 && !next_block_peek_calc.is_word && !next_block_peek_calc.is_newline && !next_block_peek_calc.is_tab) {
                                    // Наступний блок - це послідовність пробілів
                                    Sint32 cp_space_peek_calc = 0;
                                    int first_space_char_width_calc = 0;
                                    const char* temp_s_ptr_peek_calc = next_block_peek_calc.start_ptr;
                                    const char* temp_s_ptr_peek_end_calc = next_block_peek_calc.start_ptr + next_block_peek_calc.num_bytes;

                                    if (temp_s_ptr_peek_calc < temp_s_ptr_peek_end_calc) {
                                       cp_space_peek_calc = decode_utf8(&temp_s_ptr_peek_calc, temp_s_ptr_peek_end_calc);
                                    }

                                    if (cp_space_peek_calc == ' ') { // Якщо перший символ наступного блоку - пробіл
                                        first_space_char_width_calc = get_codepoint_advance_and_metrics_func(appCtx, (Uint32)cp_space_peek_calc, appCtx->space_advance_width, NULL, NULL);
                                        // Якщо поточний блок + цей перший пробіл наступного блоку не вміщуються
                                        if (first_space_char_width_calc > 0 && (pen_x_after_current_block_calc + first_space_char_width_calc > TEXT_AREA_X + TEXT_AREA_W)) {
                                            must_wrap_this_block_for_calc = true; // Тоді поточний блок треба перенести
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if (must_wrap_this_block_for_calc) {
                y_for_chars_in_this_block = line_y_at_block_start + appCtx->line_h;
                x_for_chars_in_this_block = TEXT_AREA_X;
            }
            // --- MODIFICATION END ---

            current_pen_x = x_for_chars_in_this_block + current_block.pixel_width;
            current_line_abs_y = y_for_chars_in_this_block;
        }

        // Логіка пошуку позиції курсора всередині блоку або в кінці блоку
        if (!cursor_position_found &&
            current_input_byte_idx >= bytes_at_block_start && // Курсор знаходиться в цьому блоці або відразу після нього
            current_input_byte_idx < bytes_at_block_start + current_block.num_bytes) { // Курсор всередині поточного блоку

            calculated_cursor_y = y_for_chars_in_this_block; // Початкова Y для символів цього блоку
            calculated_cursor_x = x_for_chars_in_this_block; // Початкова X для символів цього блоку

            const char* p_char_iter_in_block = current_block.start_ptr;
            // target_cursor_ptr_global вказує на байт, *після* якого має бути курсор
            const char* target_cursor_ptr_global = text_to_type + current_input_byte_idx;

            while (p_char_iter_in_block < target_cursor_ptr_global && p_char_iter_in_block < current_block.start_ptr + current_block.num_bytes) {
                const char* temp_char_start_in_loop = p_char_iter_in_block;
                Sint32 cp_in_block = decode_utf8(&p_char_iter_in_block, p_end); // p_end тут може бути current_block.start_ptr + current_block.num_bytes

                if (cp_in_block <= 0 ) { // Або помилка, або кінець тексту в блоці
                     if (p_char_iter_in_block <= temp_char_start_in_loop) p_char_iter_in_block = temp_char_start_in_loop + 1; // Уникнути зациклення
                     break; // Вихід з внутрішнього циклу
                }
                // Якщо decode_utf8 просунув p_char_iter_in_block за межі target_cursor_ptr_global,
                // це означає, що target_cursor_ptr_global був всередині багатобайтового символу. Курсор має бути перед ним.
                if (p_char_iter_in_block > target_cursor_ptr_global && target_cursor_ptr_global > temp_char_start_in_loop) {
                     p_char_iter_in_block = temp_char_start_in_loop; // Повертаємо вказівник на початок цього символу
                    break; // Курсор перед цим символом
                }


                int adv_char_in_block = 0;
                if (cp_in_block == '\t') { // Спеціальна обробка для табуляції всередині блоку (малоймовірно, якщо get_next_text_block_func їх відокремлює)
                    int offset_in_line_inner = calculated_cursor_x - TEXT_AREA_X;
                    adv_char_in_block = appCtx->tab_width_pixels - (offset_in_line_inner % appCtx->tab_width_pixels);
                    if (adv_char_in_block == 0 && offset_in_line_inner >=0) adv_char_in_block = appCtx->tab_width_pixels;
                    if (adv_char_in_block <=0) adv_char_in_block = appCtx->tab_width_pixels;
                } else {
                    adv_char_in_block = get_codepoint_advance_and_metrics_func(appCtx, (Uint32)cp_in_block, appCtx->space_advance_width, NULL, NULL);
                }

                // Внутрішнє перенесення для дуже довгих слів, що не вмістились навіть після блочного переносу
                if (calculated_cursor_x + adv_char_in_block > TEXT_AREA_X + TEXT_AREA_W && calculated_cursor_x != TEXT_AREA_X ) {
                    calculated_cursor_y += appCtx->line_h;
                    calculated_cursor_x = TEXT_AREA_X;
                }
                calculated_cursor_x += adv_char_in_block;
            }
            cursor_position_found = true;
        }

        processed_bytes_total += current_block.num_bytes;

        // Якщо курсор точно в кінці поточного обробленого блоку
        if (!cursor_position_found && processed_bytes_total == current_input_byte_idx) {
            calculated_cursor_x = current_pen_x; // Позиція пера після цього блоку
            calculated_cursor_y = current_line_abs_y;  // Y позиція цього блоку
            cursor_position_found = true;
        }
    }

    // Якщо курсор в самому кінці тексту (після всіх символів)
    if (!cursor_position_found && current_input_byte_idx == final_text_len) {
        calculated_cursor_x = current_pen_x;
        calculated_cursor_y = current_line_abs_y;
    }

    *out_cursor_abs_y = calculated_cursor_y;
    *out_cursor_exact_x = calculated_cursor_x;

    #if ENABLE_GAME_LOGS
    if (log_file && (current_input_byte_idx > 0)) {
        fprintf(log_file, "[CalculateCursorLayout] OUTPUT for byte_idx %zu: final_abs_y: %d (line ~%d), final_exact_x: %d\n",
                current_input_byte_idx, *out_cursor_abs_y, (appCtx->line_h > 0 ? (*out_cursor_abs_y / appCtx->line_h) : -1) , *out_cursor_exact_x);
    }
    #endif
}


void UpdateVisibleLine(int y_coord_for_update, int line_h_val, int *first_visible_line_num_ptr) {
    if (line_h_val > 0 && first_visible_line_num_ptr) {
        int line_idx_for_update = y_coord_for_update / line_h_val;
        int old_first_visible = *first_visible_line_num_ptr;

        *first_visible_line_num_ptr = line_idx_for_update - CURSOR_TARGET_VIEWPORT_LINE;
        if (*first_visible_line_num_ptr < 0) {
            *first_visible_line_num_ptr = 0;
        }
        #if ENABLE_GAME_LOGS
        if (log_file && old_first_visible != *first_visible_line_num_ptr) {
            fprintf(log_file, "[UpdateVisibleLine] y_coord_for_update: %d (line_idx: %d), TARGET_VIEWPORT_LINE: %d => new_first_visible_line: %d (old: %d)\n",
                   y_coord_for_update, line_idx_for_update, CURSOR_TARGET_VIEWPORT_LINE, *first_visible_line_num_ptr, old_first_visible);
        }
        #endif
    }
}

void RenderTextContent(AppContext *appCtx, const char *text_to_type, size_t final_text_len,
                       const char *input_buffer, size_t current_input_byte_idx,
                       int first_visible_abs_line_num_val, int text_viewport_top_y,
                       int *out_final_cursor_draw_x, int *out_final_cursor_draw_y_baseline) {
    if (!appCtx || !text_to_type || !input_buffer || !out_final_cursor_draw_x || !out_final_cursor_draw_y_baseline || !appCtx->font) return;

    int render_pen_x = TEXT_AREA_X;
    int render_current_abs_line_num = 0;

    const char *p_render_iter = text_to_type;
    const char *p_text_end_for_render = text_to_type + final_text_len;

    *out_final_cursor_draw_x = -100;
    *out_final_cursor_draw_y_baseline = -100;

    if (current_input_byte_idx == 0) {
        int relative_line_idx_for_cursor_at_0 = 0 - first_visible_abs_line_num_val;
        if (relative_line_idx_for_cursor_at_0 >=0 && relative_line_idx_for_cursor_at_0 < DISPLAY_LINES) {
            *out_final_cursor_draw_x = TEXT_AREA_X;
            *out_final_cursor_draw_y_baseline = text_viewport_top_y + relative_line_idx_for_cursor_at_0 * appCtx->line_h;
        }
    }


    while(p_render_iter < p_text_end_for_render) {
        int current_viewport_line_idx = render_current_abs_line_num - first_visible_abs_line_num_val;

        if (current_viewport_line_idx >= DISPLAY_LINES) {
            break;
        }

        size_t current_block_start_byte_pos_render = (size_t)(p_render_iter - text_to_type);
        TextBlockInfo render_block = get_next_text_block_func(appCtx, &p_render_iter, p_text_end_for_render, render_pen_x);

        if (render_block.num_bytes == 0 && p_render_iter >= p_text_end_for_render) break;
        if (render_block.num_bytes == 0 || !render_block.start_ptr) {
            if(p_render_iter < p_text_end_for_render) p_render_iter++; else break;
            continue;
        }

        int current_line_on_screen_y = text_viewport_top_y + current_viewport_line_idx * appCtx->line_h;

        if (current_block_start_byte_pos_render == current_input_byte_idx &&
            current_viewport_line_idx >=0 && current_viewport_line_idx < DISPLAY_LINES ) {
            *out_final_cursor_draw_x = render_pen_x;
            *out_final_cursor_draw_y_baseline = current_line_on_screen_y;
        }

        if (render_block.is_newline) {
            render_current_abs_line_num++;
            render_pen_x = TEXT_AREA_X;
        } else {
            int x_at_block_render_start = render_pen_x;
            int y_on_screen_for_block_content = current_line_on_screen_y;

            bool must_wrap_block = false;

            if (render_pen_x != TEXT_AREA_X) {
                if (render_block.is_word) {
                    if (render_pen_x + render_block.pixel_width > TEXT_AREA_X + TEXT_AREA_W) {
                        must_wrap_block = true;
                    } else {
                        const char *p_peek_iter = p_render_iter;
                        if (p_peek_iter < p_text_end_for_render) {
                            const char *temp_peek_ptr_for_get_next = p_peek_iter;
                            int pen_x_for_next_block_peek = render_pen_x + render_block.pixel_width;
                            TextBlockInfo next_block = get_next_text_block_func(appCtx, &temp_peek_ptr_for_get_next, p_text_end_for_render, pen_x_for_next_block_peek);

                            if (next_block.num_bytes > 0 && !next_block.is_word && !next_block.is_newline && !next_block.is_tab) {
                                Sint32 cp_space_peek = 0;
                                int first_space_char_width = 0;
                                const char* temp_s_ptr_peek = next_block.start_ptr;
                                const char* temp_s_ptr_peek_end = next_block.start_ptr + next_block.num_bytes;

                                if (temp_s_ptr_peek < temp_s_ptr_peek_end) {
                                   cp_space_peek = decode_utf8(&temp_s_ptr_peek, temp_s_ptr_peek_end);
                                }

                                if (cp_space_peek == ' ') {
                                    first_space_char_width = get_codepoint_advance_and_metrics_func(appCtx, (Uint32)cp_space_peek, appCtx->space_advance_width, NULL, NULL);
                                    if (first_space_char_width > 0 && (pen_x_for_next_block_peek + first_space_char_width > TEXT_AREA_X + TEXT_AREA_W)) {
                                        must_wrap_block = true;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if (must_wrap_block) {
                render_current_abs_line_num++;
                current_viewport_line_idx = render_current_abs_line_num - first_visible_abs_line_num_val;
                if (current_viewport_line_idx >= DISPLAY_LINES) break;
                y_on_screen_for_block_content = text_viewport_top_y + current_viewport_line_idx * appCtx->line_h;
                render_pen_x = TEXT_AREA_X;

                x_at_block_render_start = render_pen_x;

                 if (current_block_start_byte_pos_render == current_input_byte_idx &&
                     current_viewport_line_idx >=0 && current_viewport_line_idx < DISPLAY_LINES) {
                    *out_final_cursor_draw_x = render_pen_x;
                    *out_final_cursor_draw_y_baseline = y_on_screen_for_block_content;
                }
            }
             // Actual rendering of characters if the line is visible
            if (current_viewport_line_idx >= 0 && current_viewport_line_idx < DISPLAY_LINES) {
                if (!render_block.is_tab) {
                    const char *p_char_in_block_iter = render_block.start_ptr;
                    const char *p_char_in_block_iter_end = render_block.start_ptr + render_block.num_bytes;
                    size_t char_byte_offset_in_block_render = 0;

                    int current_char_pen_x = x_at_block_render_start;
                    int current_char_pen_y_baseline = y_on_screen_for_block_content;
                    int current_char_abs_line_num_for_render = render_current_abs_line_num;

                    while(p_char_in_block_iter < p_char_in_block_iter_end) {
                         int current_char_render_viewport_line_idx_local = current_char_abs_line_num_for_render - first_visible_abs_line_num_val;
                         if (current_char_render_viewport_line_idx_local >= DISPLAY_LINES) goto end_char_loop_render_final_v7;

                        const char* current_glyph_render_start_ptr = p_char_in_block_iter;
                        Sint32 codepoint_to_render = decode_utf8(&p_char_in_block_iter, p_char_in_block_iter_end);
                        if (codepoint_to_render <= 0) {
                            if (p_char_in_block_iter <= current_glyph_render_start_ptr && p_char_in_block_iter < p_char_in_block_iter_end) p_char_in_block_iter++; else break;
                            continue;
                        }

                        size_t char_abs_byte_pos_render = current_block_start_byte_pos_render + char_byte_offset_in_block_render;

                        if (char_abs_byte_pos_render == current_input_byte_idx &&
                            current_char_render_viewport_line_idx_local >= 0 &&
                            current_char_render_viewport_line_idx_local < DISPLAY_LINES) {
                            *out_final_cursor_draw_x = current_char_pen_x;
                            *out_final_cursor_draw_y_baseline = current_char_pen_y_baseline;
                        }

                        int glyph_w_render_val = 0, glyph_h_render_val = 0;
                        int adv_render = get_codepoint_advance_and_metrics_func(appCtx, (Uint32)codepoint_to_render, appCtx->space_advance_width,
                                                                    &glyph_w_render_val, &glyph_h_render_val);

                        if (current_char_pen_x + adv_render > TEXT_AREA_X + TEXT_AREA_W && current_char_pen_x != TEXT_AREA_X) {
                            current_char_abs_line_num_for_render++;
                            current_char_render_viewport_line_idx_local = current_char_abs_line_num_for_render - first_visible_abs_line_num_val;
                            if (current_char_render_viewport_line_idx_local >= DISPLAY_LINES) goto end_char_loop_render_final_v7;

                            current_char_pen_y_baseline = text_viewport_top_y + current_char_render_viewport_line_idx_local * appCtx->line_h;
                            current_char_pen_x = TEXT_AREA_X;
                            if (char_abs_byte_pos_render == current_input_byte_idx &&
                                current_char_render_viewport_line_idx_local >=0 &&
                                current_char_render_viewport_line_idx_local < DISPLAY_LINES) {
                                *out_final_cursor_draw_x = current_char_pen_x;
                                *out_final_cursor_draw_y_baseline = current_char_pen_y_baseline;
                            }
                        }

                        SDL_Color char_render_color;
                        bool is_char_typed = char_abs_byte_pos_render < current_input_byte_idx;
                        bool is_char_correct_val = false;
                        if (is_char_typed) {
                            size_t len_of_current_glyph_bytes = (size_t)(p_char_in_block_iter - current_glyph_render_start_ptr);
                            if (char_abs_byte_pos_render + len_of_current_glyph_bytes <= final_text_len &&
                                char_abs_byte_pos_render + len_of_current_glyph_bytes <= current_input_byte_idx &&
                                char_abs_byte_pos_render + len_of_current_glyph_bytes <= strlen(input_buffer) ) {
                                 is_char_correct_val = (memcmp(current_glyph_render_start_ptr, input_buffer + char_abs_byte_pos_render, len_of_current_glyph_bytes) == 0);
                            } else {
                                is_char_correct_val = false;
                            }
                            char_render_color = is_char_correct_val ? appCtx->palette[COL_CORRECT] : appCtx->palette[COL_INCORRECT];
                        } else { char_render_color = appCtx->palette[COL_TEXT]; }

                        if (codepoint_to_render >= 32) {
                            SDL_Texture* tex_to_render_final = NULL; bool rendered_on_the_fly = false;
                            if (codepoint_to_render < 128 ) {
                                int col_idx_for_cache_lookup;
                                if (is_char_typed) { col_idx_for_cache_lookup = is_char_correct_val ? COL_CORRECT : COL_INCORRECT; }
                                else { col_idx_for_cache_lookup = COL_TEXT; }
                                tex_to_render_final = appCtx->glyph_tex_cache[col_idx_for_cache_lookup][(int)codepoint_to_render];
                                if (tex_to_render_final) {
                                    glyph_w_render_val = appCtx->glyph_w_cache[col_idx_for_cache_lookup][(int)codepoint_to_render];
                                    glyph_h_render_val = appCtx->glyph_h_cache[col_idx_for_cache_lookup][(int)codepoint_to_render];
                                }
                            }
                            if (!tex_to_render_final && appCtx->font) {
                                SDL_Surface* surf_otf = TTF_RenderGlyph32_Blended(appCtx->font, (Uint32)codepoint_to_render, char_render_color);
                                if (surf_otf) {
                                    tex_to_render_final = SDL_CreateTextureFromSurface(appCtx->ren, surf_otf);
                                    if(tex_to_render_final) {glyph_w_render_val = surf_otf->w; glyph_h_render_val = surf_otf->h;}
                                    else { glyph_w_render_val = adv_render; glyph_h_render_val = appCtx->line_h;}
                                    SDL_FreeSurface(surf_otf); surf_otf = NULL;
                                    rendered_on_the_fly = true;
                                } else {
                                     glyph_w_render_val = adv_render; glyph_h_render_val = appCtx->line_h;
                                }
                            }
                            if (tex_to_render_final) {
                                int y_offset = (appCtx->line_h > glyph_h_render_val) ? (appCtx->line_h - glyph_h_render_val) / 2 : 0;
                                SDL_Rect dst_rect = {current_char_pen_x, current_char_pen_y_baseline + y_offset, glyph_w_render_val, glyph_h_render_val};
                                SDL_RenderCopy(appCtx->ren, tex_to_render_final, NULL, &dst_rect);
                                if (rendered_on_the_fly) { SDL_DestroyTexture(tex_to_render_final); tex_to_render_final = NULL; }
                            }
                        }
                        current_char_pen_x += adv_render;
                        char_byte_offset_in_block_render += (size_t)(p_char_in_block_iter - current_glyph_render_start_ptr);
                    }
                    end_char_loop_render_final_v7:;
                    render_pen_x = current_char_pen_x;
                    render_current_abs_line_num = current_char_abs_line_num_for_render;
                } else {
                    render_pen_x += render_block.pixel_width;
                }
            } else { // Block is not visible, but advance pen logically
                 render_pen_x += render_block.pixel_width;
            }
        }

        // If cursor is at the end of this block (after all its chars) and it's visible
        if (current_block_start_byte_pos_render + render_block.num_bytes == current_input_byte_idx) {
            int final_block_viewport_line = render_current_abs_line_num - first_visible_abs_line_num_val;
            if (final_block_viewport_line >=0 && final_block_viewport_line < DISPLAY_LINES) {
                *out_final_cursor_draw_x = render_pen_x; // Cursor is where this block ended
                *out_final_cursor_draw_y_baseline = text_viewport_top_y + final_block_viewport_line * appCtx->line_h;
            }
        }
    }

    // If cursor is at the very end of the text
    if (current_input_byte_idx == final_text_len) {
        int final_text_end_viewport_line = render_current_abs_line_num - first_visible_abs_line_num_val;
        if (final_text_end_viewport_line >=0 && final_text_end_viewport_line < DISPLAY_LINES) {
            *out_final_cursor_draw_x = render_pen_x; // Cursor is at the final pen position
            *out_final_cursor_draw_y_baseline = text_viewport_top_y + final_text_end_viewport_line * appCtx->line_h;
        }
    }
}


void RenderAppCursor(AppContext *appCtx, bool show_cursor,
                     int final_cursor_x_on_screen, int final_cursor_y_baseline_on_screen,
                     int cursor_abs_y_logical, int first_visible_abs_line_num, int text_viewport_top_y) {
    if (!appCtx || !appCtx->ren || !show_cursor) {
        return;
    }

    if (final_cursor_x_on_screen >= TEXT_AREA_X &&
        final_cursor_x_on_screen <= TEXT_AREA_X + TEXT_AREA_W + 2 &&
        final_cursor_y_baseline_on_screen >= text_viewport_top_y &&
        final_cursor_y_baseline_on_screen < text_viewport_top_y + (DISPLAY_LINES * appCtx->line_h) ) {

        SDL_Rect cur_rect = { final_cursor_x_on_screen, final_cursor_y_baseline_on_screen, 2, appCtx->line_h };
        SDL_SetRenderDrawColor(appCtx->ren, appCtx->palette[COL_CURSOR].r, appCtx->palette[COL_CURSOR].g, appCtx->palette[COL_CURSOR].b, appCtx->palette[COL_CURSOR].a);
        SDL_RenderFillRect(appCtx->ren, &cur_rect);
    }
}

// --- Main Function ---
int main(int argc, char **argv) {
    // --- Початок діагностичного блоку ---
    fprintf(stderr, "DEBUG: main() CALLED. Trying to open logs.txt.\n"); // ПОВИННО З'ЯВИТИСЯ НА STDERR
    fflush(stderr); // Примусово скинути буфер stderr

    log_file = fopen("logs.txt", "w");
    if (log_file == NULL) {
        perror("CRITICAL_STDERR: Failed to open logs.txt for writing"); // perror пише на stderr
        fprintf(stderr, "CRITICAL_STDERR: fopen for logs.txt failed. log_file is NULL.\n");
        fflush(stderr);
        // Не виходимо з програми тут, щоб побачити, чи працює решта логування на stderr
    } else {
        fprintf(stderr, "DEBUG: logs.txt opened successfully. log_file pointer: %p\n", (void*)log_file);
        fflush(stderr);

        int ret_fputs = fputs("Application started. Logging to logs.txt.\n", log_file);
        if (ret_fputs == EOF) {
            fprintf(stderr, "DEBUG: fputs to log_file for first line FAILED. ferror: %d\n", ferror(log_file));
            perror("DEBUG_STDERR_PERROR: fputs (first line)");
            fflush(stderr);
        }

        int ret_fflush = fflush(log_file);
        if (ret_fflush == EOF) {
            fprintf(stderr, "DEBUG: fflush for first line FAILED. ferror: %d\n", ferror(log_file));
            perror("DEBUG_STDERR_PERROR: fflush (first line)");
            fflush(stderr);
        }

        // Тест другого запису
        fprintf(stderr, "DEBUG: Attempting SECOND write to log_file.\n");
        fflush(stderr);
        ret_fputs = fputs("SECOND LINE TEST.\n", log_file);
        if (ret_fputs == EOF) {
            fprintf(stderr, "DEBUG: fputs for SECOND LINE TEST FAILED. ferror: %d\n", ferror(log_file));
            perror("DEBUG_STDERR_PERROR: fputs (second line)");
            fflush(stderr);
        } else {
            fprintf(stderr, "DEBUG: fputs for SECOND LINE TEST succeeded (returned non-EOF).\n");
            fflush(stderr);
        }
        ret_fflush = fflush(log_file);
        if (ret_fflush == EOF) {
            fprintf(stderr, "DEBUG: fflush for SECOND LINE TEST FAILED. ferror: %d\n", ferror(log_file));
            perror("DEBUG_STDERR_PERROR: fflush (second line)");
            fflush(stderr);
        } else {
             fprintf(stderr, "DEBUG: fflush for SECOND LINE TEST succeeded.\n");
             fflush(stderr);
        }
    }

    AppContext appCtx = {0};
    char *raw_text_content_main = NULL;
    size_t raw_text_len_main = 0;

    char *base_path = SDL_GetBasePath();
    char full_path_to_text_file[1024];
    const char* resource_file_name = TEXT_FILE_PATH_BASENAME;

    if (base_path) {
        #ifdef __APPLE__
            snprintf(full_path_to_text_file, sizeof(full_path_to_text_file), "%s../Resources/%s", base_path, resource_file_name);
        #else
            // 1. Try next to executable
            snprintf(full_path_to_text_file, sizeof(full_path_to_text_file), "%s%s", base_path, resource_file_name);
            FILE* test_file = fopen(full_path_to_text_file, "rb");
            if (test_file) {
                fclose(test_file);
            } else {
                // 2. Try ../share/PROJECT_NAME_STR/resource_file_name
                snprintf(full_path_to_text_file, sizeof(full_path_to_text_file), "%s../share/%s/%s", base_path, PROJECT_NAME_STR, resource_file_name);
                test_file = fopen(full_path_to_text_file, "rb");
                 if (test_file) {
                    fclose(test_file);
                } else {
                    // Fallback to current directory if not found in expected install location
                    fprintf(stderr, "Warning: Could not find '%s' relative to executable or in share dir. Trying current dir.\n", resource_file_name);
                    strncpy(full_path_to_text_file, resource_file_name, sizeof(full_path_to_text_file) - 1);
                    full_path_to_text_file[sizeof(full_path_to_text_file) - 1] = '\0';
                }
            }
        #endif
        SDL_free(base_path);
    } else {
        fprintf(stderr, "Warning: SDL_GetBasePath() failed. Trying relative path for '%s'\n", resource_file_name);
        strncpy(full_path_to_text_file, resource_file_name, sizeof(full_path_to_text_file) - 1);
        full_path_to_text_file[sizeof(full_path_to_text_file) - 1] = '\0';
    }

    #if ENABLE_GAME_LOGS
    if (log_file) fprintf(log_file, "Attempting to open text file at: %s\n", full_path_to_text_file);
    #endif

    FILE *text_file_handle_main = fopen(full_path_to_text_file, "rb");

    if (!text_file_handle_main) {
        perror("Failed to open text file");
        fprintf(stderr, "Attempted to open: %s\n", full_path_to_text_file);
        if (log_file) {
            fprintf(log_file, "ERROR: Failed to open text file: %s\n", full_path_to_text_file);
            fclose(log_file);
            log_file = NULL;
        }
        return 1;
    }

    fseek(text_file_handle_main, 0, SEEK_END);
    long raw_file_size_main_long = ftell(text_file_handle_main);
    if (raw_file_size_main_long < 0 || (size_t)raw_file_size_main_long >= MAX_TEXT_LEN -1 ) {
        fprintf(stderr, "Error getting file size or file too large (max %d).\n", MAX_TEXT_LEN -1);
        if (log_file) {
             fprintf(log_file, "ERROR: Error getting file size (size: %ld) or file too large.\n", raw_file_size_main_long);
             fclose(log_file); log_file = NULL;
        }
        fclose(text_file_handle_main); return 1;
    }
    raw_text_len_main = (size_t)raw_file_size_main_long;
    fseek(text_file_handle_main, 0, SEEK_SET);

    raw_text_content_main = (char*)malloc(raw_text_len_main + 1);
    if (!raw_text_content_main) {
        perror("Failed to allocate memory for raw_text_content_main");
        if (log_file) {
            fprintf(log_file, "ERROR: Failed to allocate memory for raw_text_content_main.\n");
            fclose(log_file); log_file = NULL;
        }
        fclose(text_file_handle_main); return 1;
    }

    size_t bytes_actually_read = fread(raw_text_content_main, 1, raw_text_len_main, text_file_handle_main);
    fclose(text_file_handle_main);
    text_file_handle_main = NULL;

    if (bytes_actually_read != raw_text_len_main) {
        fprintf(stderr, "Warning: Mismatch in file size and bytes read. Read %zu, expected %zu. Using %zu.\n", bytes_actually_read, raw_text_len_main, bytes_actually_read);
        #if ENABLE_GAME_LOGS
        if (log_file) {
            fprintf(log_file, "WARN: Mismatch in file size and bytes read. Read %zu, expected %zu. Using %zu.\n", bytes_actually_read, raw_text_len_main, bytes_actually_read);
        }
        #endif
        raw_text_len_main = bytes_actually_read;
    }
    raw_text_content_main[bytes_actually_read] = '\0';

    size_t final_text_len_val = 0;
    char *text_to_type = PreprocessText(raw_text_content_main, bytes_actually_read, &final_text_len_val);
    free(raw_text_content_main);
    raw_text_content_main = NULL;
    if (!text_to_type) {
        perror("Failed to preprocess text");
        if (log_file) {
            fprintf(log_file, "ERROR: Failed to preprocess text.\n");
            fclose(log_file); log_file = NULL;
        }
        return 1;
    }

    if (!InitializeApp(&appCtx, "TypingApp")) { // Changed title
        free(text_to_type);
        if (log_file) {
            fprintf(log_file, "ERROR: Failed to initialize app. appCtx.line_h = %d\n", appCtx.line_h);
            fclose(log_file); log_file = NULL;
        }
        return 1;
    }
    #if ENABLE_GAME_LOGS
    if (log_file) fprintf(log_file, "InitializeApp successful. appCtx.line_h = %d, space_adv: %d, tab_pixels: %d\n", appCtx.line_h, appCtx.space_advance_width, appCtx.tab_width_pixels);
    #endif


    char *input_buffer = (char*)calloc(final_text_len_val + 100, 1);
    if (!input_buffer && (final_text_len_val + 100 > 0) ) {
        perror("Failed to allocate input_buffer");
        CleanupApp(&appCtx); free(text_to_type);
        if (log_file) {
            fprintf(log_file, "ERROR: Failed to allocate input_buffer.\n");
            fclose(log_file); log_file = NULL;
        }
        return 1;
    }
    size_t current_input_byte_idx_main = 0;
    static size_t prev_input_idx_for_log = (size_t)-1;
    static int prev_first_visible_for_log_main = -1;


    Uint32 start_time_main = 0;
    bool show_cursor_flag = true; Uint32 last_blink_time = SDL_GetTicks();
    bool quit_game_flag = false;

    SDL_StartTextInput();

    while (!quit_game_flag) {
        SDL_Event event_main_loop;
        size_t old_input_idx = current_input_byte_idx_main;

        HandleAppEvents(&event_main_loop, &current_input_byte_idx_main, input_buffer, final_text_len_val, text_to_type, &typing_started_main, &start_time_main, &quit_game_flag);

        if (quit_game_flag) break;

        if (current_input_byte_idx_main != old_input_idx) {
            predictive_scroll_triggered_for_this_input_idx = false;
            y_offset_due_to_prediction_for_current_idx = 0;
        }

        if (SDL_GetTicks() - last_blink_time > 500) { show_cursor_flag = !show_cursor_flag; last_blink_time = SDL_GetTicks(); }

        SDL_SetRenderDrawColor(appCtx.ren, appCtx.palette[COL_BG].r, appCtx.palette[COL_BG].g, appCtx.palette[COL_BG].b, appCtx.palette[COL_BG].a);
        SDL_RenderClear(appCtx.ren);

        int timer_h_val = 0;
        int timer_w_val = 0;
        RenderAppTimer(&appCtx, typing_started_main ? (SDL_GetTicks() - start_time_main) : 0, &timer_h_val, &timer_w_val);

        RenderLiveStats(&appCtx, SDL_GetTicks(), start_time_main,
                        text_to_type,
                        input_buffer, current_input_byte_idx_main,
                        TEXT_AREA_X, timer_w_val, TEXT_AREA_PADDING_Y, timer_h_val);


        int text_viewport_top_y_val = TEXT_AREA_PADDING_Y + timer_h_val + TEXT_AREA_PADDING_Y;

        int logical_cursor_abs_y = 0;
        int logical_cursor_x = 0;
        CalculateCursorLayout(&appCtx, text_to_type, final_text_len_val, current_input_byte_idx_main, &logical_cursor_abs_y, &logical_cursor_x);

        int y_for_scroll_update;

        if (predictive_scroll_triggered_for_this_input_idx) {
            y_for_scroll_update = logical_cursor_abs_y + y_offset_due_to_prediction_for_current_idx;
        } else {
            y_for_scroll_update = logical_cursor_abs_y;

            if (appCtx.line_h > 0) {
                int current_cursor_abs_line_idx = logical_cursor_abs_y / appCtx.line_h;
                int target_abs_line_for_cursor_in_viewport = first_visible_abs_line_num_static + CURSOR_TARGET_VIEWPORT_LINE;

                if (current_cursor_abs_line_idx == target_abs_line_for_cursor_in_viewport &&
                    current_input_byte_idx_main < final_text_len_val) {

                    size_t next_logical_pos_idx = 0;
                    const char* p_next_char_temp = text_to_type + current_input_byte_idx_main;
                    const char* p_next_char_temp_end = text_to_type + final_text_len_val;
                    Sint32 cp_next_temp = decode_utf8(&p_next_char_temp, p_next_char_temp_end);
                    if (cp_next_temp > 0) {
                        next_logical_pos_idx = (size_t)(p_next_char_temp - text_to_type);
                    } else {
                        next_logical_pos_idx = current_input_byte_idx_main + 1;
                        if(next_logical_pos_idx > final_text_len_val) next_logical_pos_idx = final_text_len_val;
                    }

                    if (next_logical_pos_idx <= final_text_len_val && next_logical_pos_idx > current_input_byte_idx_main) {
                        int y_of_next_logical_pos, x_of_next_logical_pos;
                        CalculateCursorLayout(&appCtx, text_to_type, final_text_len_val, next_logical_pos_idx, &y_of_next_logical_pos, &x_of_next_logical_pos);

                        if (y_of_next_logical_pos > logical_cursor_abs_y) {
                            int potential_new_first_visible = (y_of_next_logical_pos / appCtx.line_h) - CURSOR_TARGET_VIEWPORT_LINE;
                            if (potential_new_first_visible < 0) potential_new_first_visible = 0;

                            if (potential_new_first_visible > first_visible_abs_line_num_static) {
                                y_for_scroll_update = y_of_next_logical_pos;
                                y_offset_due_to_prediction_for_current_idx = y_of_next_logical_pos - logical_cursor_abs_y;
                                predictive_scroll_triggered_for_this_input_idx = true;
                                #if ENABLE_GAME_LOGS
                                if (log_file) {
                                    fprintf(log_file, "[MainLoop] Predictive scroll ACTIVATED: current_idx=%zu (y=%d), next_idx_check=%zu (y_next=%d). Using y_next=%d for scroll. Offset=%d. PotentialNewFirstVis=%d, CurrentFirstVis=%d\n",
                                            current_input_byte_idx_main, logical_cursor_abs_y, next_logical_pos_idx, y_of_next_logical_pos, y_for_scroll_update, y_offset_due_to_prediction_for_current_idx, potential_new_first_visible, first_visible_abs_line_num_static);
                                }
                                #endif
                            }
                        }
                    }
                }
            }
        }

        int current_first_visible_for_log = first_visible_abs_line_num_static;
        UpdateVisibleLine(y_for_scroll_update, appCtx.line_h, &first_visible_abs_line_num_static);

        #if ENABLE_GAME_LOGS
        if (log_file && (current_input_byte_idx_main != prev_input_idx_for_log || first_visible_abs_line_num_static != prev_first_visible_for_log_main)) {
            fprintf(log_file, "[MainLoop] input_idx: %zu, logical_cursor_abs_y: %d (line ~%d from CalcLayout), y_for_update: %d (line ~%d), logical_cursor_x: %d | PrevFirstVisMain: %d, CurFirstVisForLog: %d, NewFirstVis: %d (TARGET_LINE: %d)\n",
                current_input_byte_idx_main,
                logical_cursor_abs_y, (appCtx.line_h > 0 ? logical_cursor_abs_y / appCtx.line_h : -1),
                y_for_scroll_update, (appCtx.line_h > 0 ? y_for_scroll_update / appCtx.line_h : -1),
                logical_cursor_x,
                prev_first_visible_for_log_main,
                current_first_visible_for_log,
                first_visible_abs_line_num_static,
                CURSOR_TARGET_VIEWPORT_LINE);
        }
        #endif
        prev_input_idx_for_log = current_input_byte_idx_main;
        prev_first_visible_for_log_main = first_visible_abs_line_num_static;

        int final_cursor_x_on_screen_calc = -100;
        int final_cursor_y_baseline_on_screen_calc = -100;

        RenderTextContent(&appCtx, text_to_type, final_text_len_val, input_buffer, current_input_byte_idx_main,
                          first_visible_abs_line_num_static,
                          text_viewport_top_y_val,
                          &final_cursor_x_on_screen_calc, &final_cursor_y_baseline_on_screen_calc);

        RenderAppCursor(&appCtx, show_cursor_flag, final_cursor_x_on_screen_calc, final_cursor_y_baseline_on_screen_calc,
                        logical_cursor_abs_y,
                        first_visible_abs_line_num_static,
                        text_viewport_top_y_val);

        SDL_RenderPresent(appCtx.ren);
        #if ENABLE_GAME_LOGS
        if (log_file) fflush(log_file);
        #endif
        SDL_Delay(16);
    }

    SDL_StopTextInput();
    if (typing_started_main) {
        CalculateAndPrintAppStats(start_time_main, current_input_byte_idx_main, text_to_type, final_text_len_val, input_buffer);

        // --- BEGIN MODIFICATION: Update text.txt to remove typed portion ---
        // Check if any progress was made in the source text.
        // current_input_byte_idx_main represents the byte offset in text_to_type the user reached.
        if (current_input_byte_idx_main > 0) {
            size_t num_bytes_to_remove_from_source = current_input_byte_idx_main;

            // Ensure we don't try to remove more bytes than exist in the source text.
            // This handles cases where input might have technically gone "past the end"
            // (e.g., errors typed after the last character of the source text).
            if (num_bytes_to_remove_from_source > final_text_len_val) {
                num_bytes_to_remove_from_source = final_text_len_val;
            }

            // Proceed only if the path to the text file is valid.
            if (full_path_to_text_file[0] != '\0') {
                FILE *output_text_file_handle = fopen(full_path_to_text_file, "w"); // Open in write mode ("w") to overwrite.
                if (output_text_file_handle) {
                    // Pointer to the beginning of the text that should remain.
                    const char *remaining_text_content_ptr = text_to_type + num_bytes_to_remove_from_source;
                    // Length of the text that should remain.
                    size_t remaining_content_len = final_text_len_val - num_bytes_to_remove_from_source;

                    if (remaining_content_len > 0) {
                        // Write the remaining text to the file.
                        if (fwrite(remaining_text_content_ptr, 1, remaining_content_len, output_text_file_handle) == remaining_content_len) {
                            if (log_file) fprintf(log_file, "Successfully updated '%s', wrote %zu bytes of remaining text.\n", full_path_to_text_file, remaining_content_len);
                        } else {
                            // Error during writing.
                            if (log_file) fprintf(log_file, "Error: Failed to write all remaining bytes to '%s'.\n", full_path_to_text_file);
                            perror("Error writing remaining text to text file"); // Also print to stderr for visibility.
                        }
                    } else {
                        // All text was processed (num_bytes_to_remove_from_source == final_text_len_val),
                        // so remaining_content_len is 0. The file is effectively truncated by opening in "w" mode.
                        if (log_file) fprintf(log_file, "Successfully updated '%s'. All text was processed; file is now empty.\n", full_path_to_text_file);
                    }
                    fclose(output_text_file_handle);
                    output_text_file_handle = NULL;
                } else {
                    // Error opening the file for writing.
                    if (log_file) fprintf(log_file, "Error: Could not open '%s' for writing to update content.\n", full_path_to_text_file);
                    perror("Error opening text file for writing"); // To stderr.
                }
            } else {
                 // This case should ideally not happen if file reading was successful.
                 if (log_file) fprintf(log_file, "Error: Text file path is empty, cannot update '%s'.\n", TEXT_FILE_PATH_BASENAME);
            }
        } else { // typing_started_main is true, but current_input_byte_idx_main is 0 (no characters of source text processed).
            if (log_file) {
                fprintf(log_file, "Typing session started but no characters were advanced in source text. Text file '%s' not modified.\n", full_path_to_text_file);
            }
        }
        // --- END MODIFICATION ---

    } else {
        // No typing was started at all.
        printf("No typing started. No stats to display. Text file not modified.\n");
        if (log_file) {
            // Use full_path_to_text_file if available, otherwise TEXT_FILE_PATH_BASENAME for logging.
            const char* log_path = (full_path_to_text_file[0] != '\0') ? full_path_to_text_file : TEXT_FILE_PATH_BASENAME;
            fprintf(log_file, "No typing started. Text file '%s' not modified.\n", log_path);
        }
    }

    CleanupApp(&appCtx);
    if (text_to_type) free(text_to_type); text_to_type = NULL;
    return 0;
}