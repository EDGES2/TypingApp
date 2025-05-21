#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h> // For iscntrl, isspace

// --- Application Constants ---
#define WINDOW_W     800
#define WINDOW_H     200
#define FONT_SIZE    14
#define MAX_TEXT_LEN 50000 // Increased from 10000
#ifndef TEXT_FILE_PATH
#define TEXT_FILE_PATH "text.txt"
#endif

#define TEXT_AREA_X 10
#define TEXT_AREA_PADDING_Y 10
#define TEXT_AREA_W (WINDOW_W - (2 * TEXT_AREA_X))
#define DISPLAY_LINES 3
#define CURSOR_TARGET_VIEWPORT_LINE 1
#define TAB_SIZE_IN_SPACES 4

#define ENABLE_DETAILED_LOGGING 0 // Set to 1 to enable verbose logging, 0 to disable

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
static bool predictive_scroll_triggered_for_this_input_idx = false;
static int y_offset_due_to_prediction_for_current_idx = 0;

// For accuracy calculation based on all keystrokes
static unsigned long long total_keystrokes_for_accuracy = 0;
static unsigned long long total_errors_committed_for_accuracy = 0;


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
    TextBlockInfo block = {NULL};
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
            if (log_file) fprintf(log_file, "Loaded font: %s (line_h from TTF_FontLineSkip: %d, from TTF_FontHeight: %d)\n",
                                   font_paths[i], TTF_FontLineSkip(appCtx->font), TTF_FontHeight(appCtx->font));
            else printf("Loaded font: %s\n", font_paths[i]);
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

    if (log_file) fflush(log_file);
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

    size_t temp_buffer_size = raw_text_len + (raw_text_len / 2) + 2;
    char *temp_buffer = (char*)malloc(temp_buffer_size);
    if (!temp_buffer) {
        perror("Failed to allocate temporary buffer in PreprocessText");
        *out_final_text_len = 0;
        return NULL;
    }

    size_t temp_idx = 0;
    const char* p_read = raw_text_buffer;
    const char* p_read_end = raw_text_buffer + raw_text_len;
    int consecutive_newlines = 0;
    bool last_char_was_actual_content = false; // Tracks if the last char *before* a potential newline sequence was content

    // Pass 1: Normalize newlines, replace special characters, handle \r
    while(p_read < p_read_end) {
        const char* char_start_original = p_read;
        Sint32 cp = decode_utf8(&p_read, p_read_end);
        size_t char_len_original = (size_t)(p_read - char_start_original);

        if (cp <= 0) { // Invalid UTF-8 or end of string
            if (char_len_original == 0 && p_read < p_read_end) p_read++; // Ensure progress on null or invalid char
            continue;
        }

        bool replaced_quotes = false;
        // char quote_replacement_char = '\''; // Defined outside for clarity if needed, but direct write is fine

        if (cp == 0x2018 || cp == 0x2019 || cp == 0x201C || cp == 0x201D) { // Smart quotes (single/double)
            replaced_quotes = true;
        }
        // Em-dash (0x2014) is handled specifically in the writing section below

        if (cp == '\n') {
            consecutive_newlines++;
            last_char_was_actual_content = false; // Newline itself is not "actual content" for space insertion logic
        } else if (cp == '\r') {
            // \r is effectively ignored, does not contribute to content or newlines directly here
            last_char_was_actual_content = false;
            continue; // Skip \r
        } else { // Not \n or \r
            // Handle insertion of space or newline based on `consecutive_newlines` from previous iterations
            if (consecutive_newlines > 0) {
                if (temp_idx > 0 && temp_buffer[temp_idx-1] != '\n') {
                    // Previous char in buffer exists and is not a newline: insert space or newline
                    if (temp_idx < temp_buffer_size -1) temp_buffer[temp_idx++] = (consecutive_newlines >= 2) ? '\n' : ' ';
                    else break; // Buffer full
                } else if (consecutive_newlines >= 2) {
                    // Previous char was newline or buffer start, and we have >= 2 newlines: insert single newline
                     if (temp_idx < temp_buffer_size -1) temp_buffer[temp_idx++] = '\n';
                     else break; // Buffer full
                } else {
                    // Previous char was newline or buffer start, and we have 1 newline.
                    // Convert to space if `last_char_was_actual_content` (before this newline sequence) was true.
                    if (temp_idx > 0 && temp_buffer[temp_idx-1] != ' ' && temp_buffer[temp_idx-1] != '\n') {
                        // This specific condition from original was a bit complex.
                        // Simplified: if it's a single newline after something that wasn't a newline, it becomes a space.
                        // The above (temp_idx > 0 && temp_buffer[temp_idx-1] != '\n') handles this by adding a space.
                        // If temp_idx == 0 (start of buffer) and last_char_was_actual_content is true, add a space.
                         if (temp_idx < temp_buffer_size -1) temp_buffer[temp_idx++] = ' '; // Add space
                    } else if (temp_idx == 0 && last_char_was_actual_content) { // At start of buffer, after "conceptual" previous content
                         if (temp_idx < temp_buffer_size -1) temp_buffer[temp_idx++] = ' '; // Add space
                    }
                    // If none of these, the single newline (if not following content) is effectively dropped,
                    // which is fine as leading/trailing newlines are trimmed later.
                }
            }
            consecutive_newlines = 0;

            // Now, write the current character (cp) or its replacement to temp_buffer
            if (cp == 0x2014) { // Em-dash (U+2014)
                // Replace with En-dash (U+2013), UTF-8: E2 80 93
                if (temp_idx + 3 <= temp_buffer_size) { // Check space for 3 bytes (+ null terminator later)
                    temp_buffer[temp_idx++] = (char)0xE2;
                    temp_buffer[temp_idx++] = (char)0x80;
                    temp_buffer[temp_idx++] = (char)0x93;
                } else {
                    break; // Buffer full
                }
                last_char_was_actual_content = true; // En-dash is content
            } else if (replaced_quotes) { // Smart quotes replaced by ASCII single quote
                if (temp_idx + 1 <= temp_buffer_size) { // Check space for 1 byte (+ null terminator later)
                    temp_buffer[temp_idx++] = '\'';
                } else {
                    break; // Buffer full
                }
                last_char_was_actual_content = true; // ASCII quote is content
            } else { // Original character (not em-dash, not a replaced quote)
                if (temp_idx + char_len_original <= temp_buffer_size) { // Check space for original char bytes (+ null)
                    memcpy(temp_buffer + temp_idx, char_start_original, char_len_original);
                    temp_idx += char_len_original;
                } else {
                    break; // Buffer full
                }
                last_char_was_actual_content = (cp != ' '); // Update based on the original character
            }
        }
    }

    // Handle any trailing newlines collected at the end of the loop
    if (consecutive_newlines > 0) {
        if (temp_idx > 0 && temp_buffer[temp_idx-1] != '\n' && temp_buffer[temp_idx-1] != ' ') {
             // If buffer has content and last char is not newline or space, add appropriate trailing char
             if (temp_idx < temp_buffer_size -1) temp_buffer[temp_idx++] = (consecutive_newlines >= 2) ? '\n' : ' ';
        } else if (consecutive_newlines >= 2) { // If buffer ends with newline or is empty, and we had >=2 newlines
            if (temp_idx < temp_buffer_size -1) temp_buffer[temp_idx++] = '\n';
        }
        // Single trailing newline after space/newline or at start is effectively ignored / trimmed later
    }
    temp_buffer[temp_idx] = '\0'; // Null-terminate the content in temp_buffer

    // Pass 2: Refine spacing, trim, and copy to final processed_text buffer
    // (This part of the function remains unchanged from your original code)
    char *processed_text = (char*)malloc(temp_idx + 1); // Allocate for the content in temp_buffer
    if (!processed_text) {
        perror("Failed to allocate processed_text in PreprocessText");
        free(temp_buffer);
        *out_final_text_len = 0;
        return NULL;
    }

    size_t pt_idx = 0; // Index for processed_text
    bool last_char_was_a_space_in_pass2 = true; // Start as if preceded by a space to trim leading spaces

    const char* p_pass2_read = temp_buffer;
    const char* p_pass2_end = temp_buffer + temp_idx;

    // Trim leading spaces/newlines from temp_buffer before copying to processed_text
    while(p_pass2_read < p_pass2_end && (*p_pass2_read == ' ' || *p_pass2_read == '\n' || *p_pass2_read == '\r')) {
        p_pass2_read++;
    }

    while(p_pass2_read < p_pass2_end) {
        const char* char_start_pass2_original = p_pass2_read; // Keep original pointer to copy from
        Sint32 cp_pass2 = decode_utf8(&p_pass2_read, p_pass2_end); // Advances p_pass2_read
        size_t current_char_len_pass2 = (size_t)(p_pass2_read - char_start_pass2_original);


        if(cp_pass2 <= 0) { // Should not happen if Pass 1 is correct, but as safeguard
            if (current_char_len_pass2 == 0 && p_pass2_read < p_pass2_end) p_pass2_read++;
            continue;
        }

        if (cp_pass2 == ' ') {
            if (!last_char_was_a_space_in_pass2) { // Only add space if previous wasn't one
                processed_text[pt_idx++] = ' ';
            }
            last_char_was_a_space_in_pass2 = true;
        } else if (cp_pass2 == '\n') {
            // Remove trailing spaces before a newline
            while(pt_idx > 0 && processed_text[pt_idx-1] == ' ') {
                pt_idx--;
            }
            // Add newline only if it's not a duplicate
            if (pt_idx == 0 || processed_text[pt_idx-1] != '\n') {
                 processed_text[pt_idx++] = '\n';
            }
            last_char_was_a_space_in_pass2 = true; // Treat newline like a space for subsequent char logic
        } else { // Actual content character (including our new en-dash)
            // Copy the character (UTF-8 aware)
             memcpy(processed_text + pt_idx, char_start_pass2_original, current_char_len_pass2);
             pt_idx += current_char_len_pass2;
            last_char_was_a_space_in_pass2 = false;
        }
    }

    // Trim trailing spaces and newlines from processed_text
    while (pt_idx > 0 && (processed_text[pt_idx-1] == ' ' || processed_text[pt_idx-1] == '\n')) {
        pt_idx--;
    }
    processed_text[pt_idx] = '\0'; // Null-terminate the final string

    *out_final_text_len = pt_idx;
    free(temp_buffer); // Free the intermediate buffer

    // Realloc to exact size
    char* final_text = (char*)realloc(processed_text, pt_idx + 1);
    if (!final_text && pt_idx > 0) { // Realloc failed but original buffer is still valid
        return processed_text;
    } else if (!final_text && pt_idx == 0) { // Realloc failed and string is empty
        free(processed_text); // Free the empty allocated string
        char* empty_str = (char*)malloc(1); // Return a new minimal empty string
        if (empty_str) empty_str[0] = '\0';
        return empty_str;
    }
    return final_text ? final_text : processed_text; // Return realloc'd or original
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
                    const char* p_target_char_at_offset_original = p_target_char_at_offset; // Save original pointer
                    Sint32 cp_target = decode_utf8(&p_target_char_at_offset, text_to_type + final_text_len);

                    if (cp_target <=0 || cp_event != cp_target) {
                        total_errors_committed_for_accuracy++;
                    }

                    if(cp_target > 0) { // If target was valid, advance by its length
                        current_target_byte_offset_for_event = (size_t)(p_target_char_at_offset - text_to_type);
                    } else { // If target was invalid, advance by at least 1 to avoid stall
                         current_target_byte_offset_for_event = (size_t)((p_target_char_at_offset_original + 1) - text_to_type);
                         if (current_target_byte_offset_for_event > final_text_len) current_target_byte_offset_for_event = final_text_len;
                    }
                } else { // Typed past the end of the text_to_type
                    total_errors_committed_for_accuracy++;
                    // current_target_byte_offset_for_event can exceed final_text_len here, which is fine for error counting
                    // but input_buffer should not grow indefinitely past text_to_type.
                    // The check for adding to input_buffer handles this.
                }
                // Ensure p_event_char_iter progresses even if decode_utf8 returns 0 length (e.g. null char in event.text.text)
                if (p_event_char_iter == p_event_char_start_loop && p_event_char_iter < event_text_end) p_event_char_iter++;
            }


            // Allow typing slightly beyond final_text_len for cases where user types extra chars at the end
            // but cap it to avoid excessive buffer growth. Max 90 extra bytes.
            if (*current_input_byte_idx + input_event_len_bytes <= final_text_len + 90 ) {
                bool can_add_input = true;
                // Prevent double spaces in input buffer
                if(input_event_len_bytes == 1 && event->text.text[0] == ' ' && *current_input_byte_idx > 0){
                    if(input_buffer[(*current_input_byte_idx)-1] == ' ') can_add_input = false;
                }
                if(can_add_input){
                   // Only add to input_buffer up to its capacity (final_text_len + 99 for null)
                   size_t space_left_in_buffer = (final_text_len + 99) - (*current_input_byte_idx);
                   size_t bytes_to_add = (input_event_len_bytes < space_left_in_buffer) ? input_event_len_bytes : space_left_in_buffer;

                   if (bytes_to_add > 0) {
                       strncat(input_buffer, event->text.text, bytes_to_add);
                       (*current_input_byte_idx) += bytes_to_add;
                       // input_buffer is already null-terminated by calloc and strncat maintains it.
                   }
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
    if (current_timer_w_val <= 0) current_timer_w_val = 50; // Default width if calculation fails

    *out_timer_h = current_timer_h_val;
    *out_timer_w = current_timer_w_val;
}

void RenderLiveStats(AppContext *appCtx, Uint32 current_ticks, Uint32 start_time_ticks,
                     const char *text_to_type,
                     const char *input_buffer, size_t current_input_byte_idx,
                     int timer_x_pos, int timer_width, int timer_y_pos, int timer_height) {

    if (!appCtx || !appCtx->font || !appCtx->ren || !typing_started_main ) { // Only show if typing started
        return;
    }

    float elapsed_seconds = (float)(current_ticks - start_time_ticks) / 1000.0f;
    if (elapsed_seconds < 0.05f && total_keystrokes_for_accuracy > 0) elapsed_seconds = 0.05f; // Avoid division by zero or tiny numbers if typing just started
    else if (elapsed_seconds < 0.001f) elapsed_seconds = 0.001f; // Minimum threshold for calculation

    float elapsed_minutes = elapsed_seconds / 60.0f;

    float live_accuracy = 100.0f;
    if (total_keystrokes_for_accuracy > 0) {
        live_accuracy = ((float)(total_keystrokes_for_accuracy - total_errors_committed_for_accuracy) / (float)total_keystrokes_for_accuracy) * 100.0f;
        if (live_accuracy < 0) live_accuracy = 0;
        if (live_accuracy > 100) live_accuracy = 100; // Cap at 100%
    }

    // Calculate WPM based on "net words" (correct keystrokes / 5)
    size_t live_correct_keystrokes = (total_keystrokes_for_accuracy > total_errors_committed_for_accuracy) ? (total_keystrokes_for_accuracy - total_errors_committed_for_accuracy) : 0;
    float live_net_words_for_wpm = (float)live_correct_keystrokes / 5.0f; // Standard WPM calculation convention
    float live_wpm = (elapsed_minutes > 0.0001f) ? (live_net_words_for_wpm / elapsed_minutes) : 0.0f;


    // Count typed words based on spaces in the input_buffer (simple word count)
    int live_typed_words_count = 0;
    if (current_input_byte_idx > 0) {
        bool in_word_flag = false;
        const char* p_word_scan_iter = input_buffer;
        const char* p_word_scan_end_iter = input_buffer + current_input_byte_idx;
        while(p_word_scan_iter < p_word_scan_end_iter) {
            const char* temp_p_word_start = p_word_scan_iter; // Save start pointer for current char
            Sint32 cp_word = decode_utf8(&p_word_scan_iter, p_word_scan_end_iter); // Advances p_word_scan_iter

            if (cp_word <= 0) { // Invalid char or end
                if (p_word_scan_iter == temp_p_word_start && p_word_scan_iter < p_word_scan_end_iter) p_word_scan_iter++; // Ensure progress
                else if (p_word_scan_iter <= temp_p_word_start) break; // Should not happen if decode_utf8 works
                continue;
            }

            bool is_current_char_space = false;
            if (cp_word < 128) { // ASCII
                is_current_char_space = isspace((unsigned char)cp_word);
            } else { // Non-ASCII UTF-8 characters are generally not spaces
                is_current_char_space = false;
            }

            if (!is_current_char_space) { // Character is not a space
                if (!in_word_flag) { // Start of a new word
                    live_typed_words_count++;
                    in_word_flag = true;
                }
            } else { // Character is a space
                in_word_flag = false; // End of a word
            }
            // Ensure p_word_scan_iter advanced if decode_utf8 didn't (e.g. null char)
            if (p_word_scan_iter == temp_p_word_start && p_word_scan_iter < p_word_scan_end_iter) p_word_scan_iter++;
        }
    }


    char wpm_buf[32], acc_buf[32], words_buf[32];
    snprintf(wpm_buf, sizeof(wpm_buf), "WPM: %.0f", live_wpm > 0 ? live_wpm : 0); // Display 0 if WPM is negative/small
    snprintf(acc_buf, sizeof(acc_buf), "Acc: %.0f%%", live_accuracy);
    snprintf(words_buf, sizeof(words_buf), "Words: %d", live_typed_words_count);

    SDL_Color stat_color = appCtx->palette[COL_TEXT];

    int current_x_render_pos = timer_x_pos + timer_width + 20; // Start rendering stats after the timer
    int stats_y_render_pos = timer_y_pos + (timer_height - appCtx->line_h) / 2; // Align vertically with timer
    if (stats_y_render_pos < TEXT_AREA_PADDING_Y) stats_y_render_pos = TEXT_AREA_PADDING_Y; // Ensure it's within bounds

    SDL_Surface *surf = TTF_RenderText_Blended(appCtx->font, wpm_buf, stat_color);
    if (surf) {
        SDL_Texture *tex = SDL_CreateTextureFromSurface(appCtx->ren, surf);
        if (tex) {
            SDL_Rect dst = {current_x_render_pos, stats_y_render_pos, surf->w, surf->h};
            SDL_RenderCopy(appCtx->ren, tex, NULL, &dst);
            current_x_render_pos += surf->w + 15; // Add padding for next stat
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
     if (time_taken_seconds <= 0.001f) time_taken_seconds = 0.001f; // Avoid division by zero

    size_t final_correct_keystrokes = (total_keystrokes_for_accuracy > total_errors_committed_for_accuracy) ?
                                      (total_keystrokes_for_accuracy - total_errors_committed_for_accuracy) : 0;

    float net_words = (float)final_correct_keystrokes / 5.0f; // Standard definition: 5 chars = 1 word
    float wpm = (time_taken_seconds > 0.0001f) ? (net_words / time_taken_seconds) * 60.0f : 0.0f;

    float accuracy = 0.0f;
    if (total_keystrokes_for_accuracy > 0) {
         accuracy = ((float)final_correct_keystrokes / (float)total_keystrokes_for_accuracy) * 100.0f;
    }
     if (accuracy < 0.0f) accuracy = 0.0f;
     if (accuracy > 100.0f && total_keystrokes_for_accuracy > 0) accuracy = 100.0f; // Cap at 100%

    printf("\n--- Typing Stats (Final) ---\n");
    printf("Time Taken: %.2f seconds\n", time_taken_seconds);
    printf("WPM (Net): %.2f\n", wpm);
    printf("Correct Keystrokes: %zu\n", final_correct_keystrokes);
    printf("Total Keystrokes: %llu\n", total_keystrokes_for_accuracy);
    printf("Committed Errors: %llu\n", total_errors_committed_for_accuracy);
    printf("Accuracy (based on all keystrokes): %.2f%%\n", accuracy);
    printf("--------------------\n");
}

void CalculateCursorLayout(AppContext *appCtx, const char *text_to_type, size_t final_text_len, size_t current_input_byte_idx,
                           int *out_cursor_abs_y, int *out_cursor_exact_x) {
    if (!appCtx || !text_to_type || !out_cursor_abs_y || !out_cursor_exact_x) return;

    int calculated_cursor_y = 0;        // Absolute Y position from the top of the entire text content
    int calculated_cursor_x = TEXT_AREA_X; // X position relative to the window
    int current_pen_x = TEXT_AREA_X;    // Current X position for laying out text
    int current_line_abs_y = 0;         // Absolute Y of the current line being processed

    const char *p_iter = text_to_type;
    const char *p_end = text_to_type + final_text_len;
    size_t processed_bytes_total = 0; // Total bytes processed so far from text_to_type
    bool cursor_position_found = false;

    // Handle case for cursor at the very beginning
    if (current_input_byte_idx == 0) {
        *out_cursor_abs_y = 0;
        *out_cursor_exact_x = TEXT_AREA_X;
        #if ENABLE_DETAILED_LOGGING
        if (log_file) fprintf(log_file, "[CalculateCursorLayout] OUTPUT (idx 0): final_abs_y: %d, final_exact_x: %d\n", *out_cursor_abs_y, *out_cursor_exact_x);
        #endif
        return;
    }

    int loop_iteration_count = 0; // For debugging logs

    while(p_iter < p_end && !cursor_position_found) {
        loop_iteration_count++;
        size_t bytes_at_block_start = processed_bytes_total; // Byte offset at the start of this block
        int pen_x_at_block_start = current_pen_x;       // Pen X at the start of this block
        int line_y_at_block_start = current_line_abs_y; // Line Y at the start of this block

        // Get the next logical block of text (word, spaces, newline, tab)
        TextBlockInfo current_block = get_next_text_block_func(appCtx, &p_iter, p_end, pen_x_at_block_start);

        #if ENABLE_DETAILED_LOGGING
        if (log_file) {
           char block_preview[21];
           size_t preview_len = current_block.num_bytes < 20 ? current_block.num_bytes : 20;
           if (current_block.start_ptr) {
                strncpy(block_preview, current_block.start_ptr, preview_len);
                block_preview[preview_len] = '\0';
                for(int k=0; k < preview_len; ++k) if(iscntrl((unsigned char)block_preview[k]) && block_preview[k] != '\n' && block_preview[k] != '\t') block_preview[k] = '?';
           } else {
                strcpy(block_preview, "NULL_PTR");
           }
           size_t block_start_offset = current_block.start_ptr ? (current_block.start_ptr - text_to_type) : 0;
           fprintf(log_file, "  [CalcLayout Loop %3d] input_idx: %3zu, block_offset: %4zu, processed_total: %4zu, block_len: %2zu, text: '%s', nl:%d, tab:%d, word:%d, y_start_block: %3d, pen_x_start_block: %3d, block_w: %3d\n",
                  loop_iteration_count, current_input_byte_idx, block_start_offset, bytes_at_block_start, current_block.num_bytes, block_preview,
                  current_block.is_newline, current_block.is_tab, current_block.is_word, line_y_at_block_start, pen_x_at_block_start, current_block.pixel_width);
        }
        #endif

        if (current_block.num_bytes == 0) { // Should not happen if p_iter < p_end, but safeguard
            if(p_iter < p_end) p_iter++; else break;
            continue;
        }

        // Determine the Y and X where characters *within this block* will start rendering
        int y_for_chars_in_this_block = line_y_at_block_start;
        int x_for_chars_in_this_block = pen_x_at_block_start;

        if (current_block.is_newline) {
            current_line_abs_y = line_y_at_block_start + appCtx->line_h; // Move to next line
            current_pen_x = TEXT_AREA_X; // Reset X to margin
        } else { // Not a newline (word, spaces, or tab)
            // Check for word wrap for this block
            if (pen_x_at_block_start + current_block.pixel_width > TEXT_AREA_X + TEXT_AREA_W && // Block exceeds width
                pen_x_at_block_start != TEXT_AREA_X && // Not already at the start of a line
                (current_block.is_word || current_block.is_tab) ) { // Only wrap words or tabs (spaces can hang)
                y_for_chars_in_this_block = line_y_at_block_start + appCtx->line_h; // Move block to next line
                x_for_chars_in_this_block = TEXT_AREA_X; // Reset X for this block
            }
            current_pen_x = x_for_chars_in_this_block + current_block.pixel_width; // Update pen X after this block
            current_line_abs_y = y_for_chars_in_this_block; // Update current line Y
        }

        #if ENABLE_DETAILED_LOGGING
        // Log if a wrap occurred for a word/tab block
        if (log_file && y_for_chars_in_this_block != line_y_at_block_start && !current_block.is_newline && current_block.start_ptr ) {
             char block_preview_ch[11]; strncpy(block_preview_ch, current_block.start_ptr, 10); block_preview_ch[10] = '\0';
             for(int k=0; k<10 && block_preview_ch[k]!='\0'; ++k) if(iscntrl((unsigned char)block_preview_ch[k]) && block_preview_ch[k] != '\n' && block_preview_ch[k] != '\t') block_preview_ch[k] = '?';
             fprintf(log_file, "  [CalcLayout Iter %d] Word/Tab wrap: y_start_block %d, y_for_chars %d, new_current_line_abs_y %d. BY BLOCK: '%s'\n",
                    loop_iteration_count, line_y_at_block_start, y_for_chars_in_this_block, current_line_abs_y, block_preview_ch);
        } else if (log_file && current_block.is_newline && current_block.start_ptr) { // Log newline processing
             fprintf(log_file, "  [CalcLayout Iter %d] Newline block: y_start_block %d, new_current_line_abs_y %d.\n",
                    loop_iteration_count, line_y_at_block_start, current_line_abs_y);
        }
        #endif

        // Check if the target input_idx falls *within* the current block
        if (!cursor_position_found && current_block.start_ptr &&
            current_input_byte_idx >= bytes_at_block_start &&
            current_input_byte_idx < bytes_at_block_start + current_block.num_bytes) {

            calculated_cursor_y = y_for_chars_in_this_block; // Start with the block's Y
            calculated_cursor_x = x_for_chars_in_this_block; // Start with the block's X

            const char* p_char_iter_in_block = current_block.start_ptr;
            // Pointer to the exact byte offset of the cursor within the text_to_type string
            const char* target_cursor_ptr_global = text_to_type + current_input_byte_idx;

            // Iterate through characters *within this block* up to the cursor position
            while (p_char_iter_in_block < target_cursor_ptr_global) {
                const char* temp_char_start = p_char_iter_in_block; // Save current position
                Sint32 cp_in_block = decode_utf8(&p_char_iter_in_block, p_end); // Advances p_char_iter_in_block

                if (cp_in_block <= 0) { // Invalid char or end of block
                    if (p_char_iter_in_block <= temp_char_start) p_char_iter_in_block = temp_char_start +1; // Ensure progress
                    break;
                }
                // If decoding went past our target, means target was mid-UTF8 char (error), revert and break
                if (p_char_iter_in_block > target_cursor_ptr_global && target_cursor_ptr_global > temp_char_start) {
                     p_char_iter_in_block = temp_char_start; // Should not happen if input_idx is at char boundaries
                    break;
                }

                int adv_char_in_block = 0;
                if (cp_in_block == '\t') { // Tab character
                    int offset_in_line = calculated_cursor_x - TEXT_AREA_X;
                    adv_char_in_block = appCtx->tab_width_pixels - (offset_in_line % appCtx->tab_width_pixels);
                    if (adv_char_in_block == 0 && offset_in_line >=0) adv_char_in_block = appCtx->tab_width_pixels;
                    if (adv_char_in_block <=0) adv_char_in_block = appCtx->tab_width_pixels;
                } else { // Regular character
                    adv_char_in_block = get_codepoint_advance_and_metrics_func(appCtx, (Uint32)cp_in_block, appCtx->space_advance_width, NULL, NULL);
                }

                // Check for line wrap *within the characters of this block*
                if (calculated_cursor_x + adv_char_in_block > TEXT_AREA_X + TEXT_AREA_W && calculated_cursor_x != TEXT_AREA_X ) {
                    calculated_cursor_y += appCtx->line_h;
                    calculated_cursor_x = TEXT_AREA_X;
                }
                calculated_cursor_x += adv_char_in_block; // Add character width to cursor X
            }
            cursor_position_found = true;
        }

        processed_bytes_total += current_block.num_bytes; // Add processed block bytes

        // If cursor is exactly at the end of this block
        if (!cursor_position_found && processed_bytes_total == current_input_byte_idx) {
            calculated_cursor_x = current_pen_x; // Cursor X is where this block ended
            calculated_cursor_y = current_line_abs_y; // Cursor Y is the line this block ended on
            cursor_position_found = true;
        }
    }

    // If cursor is at the very end of the entire text
    if (!cursor_position_found && current_input_byte_idx == final_text_len) {
        calculated_cursor_x = current_pen_x;
        calculated_cursor_y = current_line_abs_y;
    }

    *out_cursor_abs_y = calculated_cursor_y;
    *out_cursor_exact_x = calculated_cursor_x;

    #if ENABLE_DETAILED_LOGGING
    if (log_file && (current_input_byte_idx > 0 || loop_iteration_count > 0 )) { // Keep this main output log
        fprintf(log_file, "[CalculateCursorLayout] OUTPUT for idx %zu: final_abs_y: %d (line ~%d), final_exact_x: %d. Loops: %d\n",
                current_input_byte_idx, *out_cursor_abs_y, (appCtx->line_h > 0 ? *out_cursor_abs_y / appCtx->line_h : -1) , *out_cursor_exact_x, loop_iteration_count);
    }
    #endif
}


void UpdateVisibleLine(int y_coord_for_update, int line_h_val, int *first_visible_line_num_ptr) {
    if (line_h_val > 0 && first_visible_line_num_ptr) {
        int line_idx_for_update = y_coord_for_update / line_h_val; // Which absolute line number this y_coord falls into
        int old_first_visible = *first_visible_line_num_ptr;

        // The target line in the viewport (e.g., 2nd line) should correspond to line_idx_for_update
        // So, first_visible_line = line_idx_for_update - CURSOR_TARGET_VIEWPORT_LINE
        *first_visible_line_num_ptr = line_idx_for_update - CURSOR_TARGET_VIEWPORT_LINE;
        if (*first_visible_line_num_ptr < 0) { // Cannot scroll before the beginning
            *first_visible_line_num_ptr = 0;
        }
        // No check for scrolling too far past the end here, as text rendering will handle empty lines.
        // Max scroll limit can be added if needed, based on total number of lines in text.

        #if ENABLE_DETAILED_LOGGING
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

    int render_pen_x = TEXT_AREA_X;         // Current X position for rendering characters
    int render_current_abs_line_num = 0; // Absolute line number being rendered (0-indexed from start of text)

    const char *p_render_iter = text_to_type; // Iterator through the source text
    const char *p_text_end_for_render = text_to_type + final_text_len;

    // Initialize cursor draw positions (updated if cursor is visible in the viewport)
    *out_final_cursor_draw_x = -100; // Off-screen default
    *out_final_cursor_draw_y_baseline = -100;

    // Special case: if cursor is at the very beginning (index 0)
    if (current_input_byte_idx == 0) {
        int relative_line_idx_for_cursor_at_0 = 0 - first_visible_abs_line_num_val; // Relative to viewport top
        if (relative_line_idx_for_cursor_at_0 >=0 && relative_line_idx_for_cursor_at_0 < DISPLAY_LINES) { // If line 0 is visible
            *out_final_cursor_draw_x = TEXT_AREA_X;
            *out_final_cursor_draw_y_baseline = text_viewport_top_y + relative_line_idx_for_cursor_at_0 * appCtx->line_h;
        }
    }


    while(p_render_iter < p_text_end_for_render) {
        // Calculate which line in the viewport this absolute line corresponds to
        int current_viewport_line_idx = render_current_abs_line_num - first_visible_abs_line_num_val;

        // Stop rendering if we are past the visible lines in the viewport
        if (current_viewport_line_idx >= DISPLAY_LINES) {
            break;
        }

        size_t current_block_start_byte_pos_render = (size_t)(p_render_iter - text_to_type);
        TextBlockInfo render_block = get_next_text_block_func(appCtx, &p_render_iter, p_text_end_for_render, render_pen_x);

        if (render_block.num_bytes == 0 && p_render_iter >= p_text_end_for_render) break; // End of text
        if (render_block.num_bytes == 0 || !render_block.start_ptr) { // Should not happen with valid text
            if(p_render_iter < p_text_end_for_render) p_render_iter++; else break;
            continue;
        }

        // Y position on screen for the current absolute line, if it's visible
        int current_line_on_screen_y = text_viewport_top_y + current_viewport_line_idx * appCtx->line_h;

        // If the cursor is at the beginning of this block and this line is visible
        if (current_block_start_byte_pos_render == current_input_byte_idx &&
            current_viewport_line_idx >=0 && current_viewport_line_idx < DISPLAY_LINES ) {
            *out_final_cursor_draw_x = render_pen_x;
            *out_final_cursor_draw_y_baseline = current_line_on_screen_y;
        }

        if (render_block.is_newline) {
            render_current_abs_line_num++; // Move to the next absolute line
            render_pen_x = TEXT_AREA_X;    // Reset X for the new line
        } else { // Block is not a newline (word, spaces, or tab)
            int x_at_block_render_start = render_pen_x; // X before this block on the current line
            // Y on screen where this block's content will start (might change due to wrap)
            int y_on_screen_for_block_content = current_line_on_screen_y;

            // Check for word wrap for this block
            if (render_pen_x + render_block.pixel_width > TEXT_AREA_X + TEXT_AREA_W && // Exceeds width
                render_pen_x != TEXT_AREA_X && // Not already at line start
                (render_block.is_word || render_block.is_tab)) { // Only wrap words/tabs

                render_current_abs_line_num++; // Wrapped to a new absolute line
                current_viewport_line_idx = render_current_abs_line_num - first_visible_abs_line_num_val; // Update viewport line

                if (current_viewport_line_idx >= DISPLAY_LINES) break; // Wrapped past viewport

                y_on_screen_for_block_content = text_viewport_top_y + current_viewport_line_idx * appCtx->line_h;
                render_pen_x = TEXT_AREA_X; // Reset X for the new wrapped line
                x_at_block_render_start = render_pen_x; // Update block's starting X

                 // If cursor was at start of this now-wrapped block and it's visible
                 if (current_block_start_byte_pos_render == current_input_byte_idx &&
                     current_viewport_line_idx >=0 && current_viewport_line_idx < DISPLAY_LINES) {
                    *out_final_cursor_draw_x = render_pen_x;
                    *out_final_cursor_draw_y_baseline = y_on_screen_for_block_content;
                }
            }

            // Only render characters if the current line (or wrapped line) is visible
            if (current_viewport_line_idx >= 0 && current_viewport_line_idx < DISPLAY_LINES ) {
                if (!render_block.is_tab) { // Render character by character for words/spaces
                    const char *p_char_in_block_iter = render_block.start_ptr;
                    const char *p_char_in_block_iter_end = render_block.start_ptr + render_block.num_bytes;
                    size_t char_byte_offset_in_block_render = 0; // Byte offset within the current block

                    // Pen position for individual characters within the block
                    int current_char_pen_x = x_at_block_render_start;
                    // Y position for characters in this block (already adjusted for block wrap if any)
                    int current_char_pen_y_baseline = y_on_screen_for_block_content;
                    // Absolute line number for characters being rendered (can change due to inline wrap)
                    int current_char_abs_line_num_for_render = render_current_abs_line_num;

                    while(p_char_in_block_iter < p_char_in_block_iter_end) {
                         // Viewport line for the current character (can change if char wraps)
                         int current_char_render_viewport_line_idx_local = current_char_abs_line_num_for_render - first_visible_abs_line_num_val;
                         if (current_char_render_viewport_line_idx_local >= DISPLAY_LINES) goto end_char_loop_render_final_v7; // Past viewport

                        const char* current_glyph_render_start_ptr = p_char_in_block_iter; // Start of current UTF-8 char
                        Sint32 codepoint_to_render = decode_utf8(&p_char_in_block_iter, p_char_in_block_iter_end);
                        if (codepoint_to_render <= 0) { // Invalid char or end of block
                             if (p_char_in_block_iter <= current_glyph_render_start_ptr && p_char_in_block_iter < p_char_in_block_iter_end) p_char_in_block_iter++;
                             else break;
                             continue;
                        }

                        size_t char_abs_byte_pos_render = current_block_start_byte_pos_render + char_byte_offset_in_block_render;

                        // If cursor is at this character's position and it's visible
                        if (char_abs_byte_pos_render == current_input_byte_idx &&
                            current_char_render_viewport_line_idx_local >= 0 &&
                            current_char_render_viewport_line_idx_local < DISPLAY_LINES) {
                            *out_final_cursor_draw_x = current_char_pen_x;
                            *out_final_cursor_draw_y_baseline = current_char_pen_y_baseline;
                        }

                        int glyph_w_render_val = 0, glyph_h_render_val = 0;
                        int adv_render = get_codepoint_advance_and_metrics_func(appCtx, (Uint32)codepoint_to_render, appCtx->space_advance_width,
                                                                    &glyph_w_render_val, &glyph_h_render_val);

                        // Check for wrap *within* the characters of a block (e.g. very long word)
                        if (current_char_pen_x + adv_render > TEXT_AREA_X + TEXT_AREA_W && current_char_pen_x != TEXT_AREA_X) {
                            current_char_abs_line_num_for_render++;
                            current_char_render_viewport_line_idx_local = current_char_abs_line_num_for_render - first_visible_abs_line_num_val;
                            if (current_char_render_viewport_line_idx_local >= DISPLAY_LINES) goto end_char_loop_render_final_v7;

                            current_char_pen_y_baseline = text_viewport_top_y + current_char_render_viewport_line_idx_local * appCtx->line_h;
                            current_char_pen_x = TEXT_AREA_X;

                            // If cursor is at this newly wrapped char position and visible
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
                            if (char_abs_byte_pos_render + len_of_current_glyph_bytes <= final_text_len && // Check bounds for text_to_type
                                char_abs_byte_pos_render + len_of_current_glyph_bytes <= current_input_byte_idx && // Check bounds for input_buffer
                                char_abs_byte_pos_render + len_of_current_glyph_bytes <= strlen(input_buffer) ) { // Ensure input_buffer is long enough
                                 is_char_correct_val = (memcmp(current_glyph_render_start_ptr, input_buffer + char_abs_byte_pos_render, len_of_current_glyph_bytes) == 0);
                            } else {
                                is_char_correct_val = false; // Typed past end or other mismatch
                            }
                            char_render_color = is_char_correct_val ? appCtx->palette[COL_CORRECT] : appCtx->palette[COL_INCORRECT];
                        } else { char_render_color = appCtx->palette[COL_TEXT]; }

                        if (codepoint_to_render >= 32) { // Render printable characters
                            SDL_Texture* tex_to_render_final = NULL; bool rendered_on_the_fly = false;
                            if (codepoint_to_render < 128 ) { // Try cache for ASCII
                                int col_idx_for_cache_lookup;
                                if (is_char_typed) { col_idx_for_cache_lookup = is_char_correct_val ? COL_CORRECT : COL_INCORRECT; }
                                else { col_idx_for_cache_lookup = COL_TEXT; }
                                tex_to_render_final = appCtx->glyph_tex_cache[col_idx_for_cache_lookup][(int)codepoint_to_render];
                                if (tex_to_render_final) { // Use cached metrics
                                    glyph_w_render_val = appCtx->glyph_w_cache[col_idx_for_cache_lookup][(int)codepoint_to_render];
                                    glyph_h_render_val = appCtx->glyph_h_cache[col_idx_for_cache_lookup][(int)codepoint_to_render];
                                }
                            }
                            if (!tex_to_render_final && appCtx->font) { // Not cached or not ASCII, render on the fly
                                SDL_Surface* surf_otf = TTF_RenderGlyph32_Blended(appCtx->font, (Uint32)codepoint_to_render, char_render_color);
                                if (surf_otf) {
                                    tex_to_render_final = SDL_CreateTextureFromSurface(appCtx->ren, surf_otf);
                                    if(tex_to_render_final) {glyph_w_render_val = surf_otf->w; glyph_h_render_val = surf_otf->h;} // Use surface metrics
                                    else { glyph_w_render_val = adv_render; glyph_h_render_val = appCtx->line_h;} // Fallback metrics
                                    SDL_FreeSurface(surf_otf); surf_otf = NULL;
                                    rendered_on_the_fly = true;
                                } else { // Failed to render surface
                                     glyph_w_render_val = adv_render; glyph_h_render_val = appCtx->line_h; // Fallback metrics
                                }
                            }
                            if (tex_to_render_final) {
                                // Center glyph vertically if its height is less than line_h
                                int y_offset = (appCtx->line_h > glyph_h_render_val) ? (appCtx->line_h - glyph_h_render_val) / 2 : 0;
                                SDL_Rect dst_rect = {current_char_pen_x, current_char_pen_y_baseline + y_offset, glyph_w_render_val, glyph_h_render_val};
                                SDL_RenderCopy(appCtx->ren, tex_to_render_final, NULL, &dst_rect);
                                if (rendered_on_the_fly) { SDL_DestroyTexture(tex_to_render_final); tex_to_render_final = NULL; }
                            }
                        }
                        current_char_pen_x += adv_render; // Advance pen X for next char
                        char_byte_offset_in_block_render += (size_t)(p_char_in_block_iter - current_glyph_render_start_ptr); // Advance byte offset
                    }
                    end_char_loop_render_final_v7:; // Label for goto if wrapped past viewport
                    render_pen_x = current_char_pen_x; // Update main render_pen_x to where chars in block ended
                    render_current_abs_line_num = current_char_abs_line_num_for_render; // Update main abs line num
                } else { // Block is a tab, just advance pen by its calculated width
                    render_pen_x += render_block.pixel_width;
                }
            } else { // Block is not visible (scrolled out of view), just advance pen logically
                 render_pen_x += render_block.pixel_width;
            }
            // render_current_abs_line_num is updated if block itself wrapped or if chars within it wrapped.
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
        return; // Don't render if no context or cursor is hidden
    }

    // final_cursor_x_on_screen and final_cursor_y_baseline_on_screen are pre-calculated by RenderTextContent
    // to be the correct screen coordinates if the cursor is visible.

    // Check if the pre-calculated screen coordinates are valid and within the text area viewport
    if (final_cursor_x_on_screen >= TEXT_AREA_X &&
        final_cursor_x_on_screen <= TEXT_AREA_X + TEXT_AREA_W + 2 && // Allow cursor slightly past edge
        final_cursor_y_baseline_on_screen >= text_viewport_top_y &&
        final_cursor_y_baseline_on_screen < text_viewport_top_y + (DISPLAY_LINES * appCtx->line_h) ) {

        SDL_Rect cur_rect = { final_cursor_x_on_screen, final_cursor_y_baseline_on_screen, 2, appCtx->line_h };
        SDL_SetRenderDrawColor(appCtx->ren, appCtx->palette[COL_CURSOR].r, appCtx->palette[COL_CURSOR].g, appCtx->palette[COL_CURSOR].b, appCtx->palette[COL_CURSOR].a);
        SDL_RenderFillRect(appCtx->ren, &cur_rect);
    } else {
        // This case means RenderTextContent determined the cursor is not visible or at an invalid position.
        // We could try to re-calculate based on logical_cursor_abs_y here as a fallback,
        // but RenderTextContent should be the source of truth for visible cursor position.
        #if ENABLE_DETAILED_LOGGING
        if (log_file) {
            // Only log if we expected it to be visible based on logical Y but RenderTextContent said no.
            int logical_cursor_viewport_line = (appCtx->line_h > 0) ? (cursor_abs_y_logical / appCtx->line_h) - first_visible_abs_line_num : -1;
            if (logical_cursor_viewport_line >= 0 && logical_cursor_viewport_line < DISPLAY_LINES) {
                 //fprintf(log_file, "[RenderCursor] Cursor at logical_y %d (viewport line %d) but RenderTextContent gave off-screen X:%d Y:%d. Not drawing.\n",
                 //        cursor_abs_y_logical, logical_cursor_viewport_line, final_cursor_x_on_screen, final_cursor_y_baseline_on_screen);
            }
        }
        #endif
    }
}

// --- Main Function ---
int main(int argc, char **argv) {
    log_file = fopen("logs.txt", "w");
    if (log_file == NULL) {
        perror("Failed to open logs.txt for writing. Logging will be disabled for this session");
    } else {
        fprintf(log_file, "Application started. Logging to logs.txt.\n");
        fflush(log_file);
    }

    AppContext appCtx = {0};
    char *raw_text_content_main = NULL;
    size_t raw_text_len_main = 0;

    const char *text_file_path_main = (argc > 1 ? argv[1] : TEXT_FILE_PATH);
    FILE *text_file_handle_main = fopen(text_file_path_main, "rb");
    if (!text_file_handle_main) {
        perror("Failed to open text file");
        fprintf(stderr, "Attempted to open: %s\n", text_file_path_main);
        if (log_file) {
            fprintf(log_file, "ERROR: Failed to open text file: %s\n", text_file_path_main);
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

    raw_text_content_main = (char*)malloc(raw_text_len_main + 1); // +1 for null terminator
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
        // This can happen if file size changes between ftell and fread, or other read errors.
        // We'll use bytes_actually_read as the true length of content.
        fprintf(stderr, "Warning: Mismatch in file size and bytes read. Read %zu, expected %zu. Using %zu.\n", bytes_actually_read, raw_text_len_main, bytes_actually_read);
        if (log_file) {
            fprintf(log_file, "WARN: Mismatch in file size and bytes read. Read %zu, expected %zu. Using %zu.\n", bytes_actually_read, raw_text_len_main, bytes_actually_read);
        }
        raw_text_len_main = bytes_actually_read; // Correct the length to what was read
    }
    raw_text_content_main[bytes_actually_read] = '\0'; // Null-terminate the raw buffer

    size_t final_text_len_val = 0;
    char *text_to_type = PreprocessText(raw_text_content_main, bytes_actually_read, &final_text_len_val);
    free(raw_text_content_main); // Free raw buffer after preprocessing
    raw_text_content_main = NULL;
    if (!text_to_type) {
        perror("Failed to preprocess text");
        if (log_file) {
            fprintf(log_file, "ERROR: Failed to preprocess text.\n");
            fclose(log_file); log_file = NULL;
        }
        return 1;
    }

    if (!InitializeApp(&appCtx, "TypingApp Monkeytype-like")) {
        free(text_to_type);
        if (log_file) {
            fprintf(log_file, "ERROR: Failed to initialize app. appCtx.line_h = %d\n", appCtx.line_h);
            fclose(log_file); log_file = NULL;
        }
        return 1;
    }
    if (log_file) fprintf(log_file, "InitializeApp successful. appCtx.line_h = %d, space_adv: %d, tab_pixels: %d\n", appCtx.line_h, appCtx.space_advance_width, appCtx.tab_width_pixels);


    // Allocate input_buffer: enough for the processed text + some leeway for typing past the end + null terminator
    char *input_buffer = (char*)calloc(final_text_len_val + 100, 1); // +100 for leeway and null
    if (!input_buffer && (final_text_len_val + 100 > 0) ) { // check if allocation was needed
        perror("Failed to allocate input_buffer");
        CleanupApp(&appCtx); free(text_to_type);
        if (log_file) {
            fprintf(log_file, "ERROR: Failed to allocate input_buffer.\n");
            fclose(log_file); log_file = NULL;
        }
        return 1;
    }
    size_t current_input_byte_idx_main = 0;
    static size_t prev_input_idx_for_log = (size_t)-1; // For conditional logging
    static int prev_first_visible_for_log_main = -1;  // For conditional logging


    Uint32 start_time_main = 0;
    bool show_cursor_flag = true; Uint32 last_blink_time = SDL_GetTicks();
    bool quit_game_flag = false;

    SDL_StartTextInput();

    while (!quit_game_flag) {
        SDL_Event event_main_loop;
        size_t old_input_idx = current_input_byte_idx_main; // Store for predictive scroll reset

        HandleAppEvents(&event_main_loop, &current_input_byte_idx_main, input_buffer, final_text_len_val, text_to_type, &typing_started_main, &start_time_main, &quit_game_flag);

        if (quit_game_flag) break;

        // If input index changed, reset predictive scroll flag for this new index
        if (current_input_byte_idx_main != old_input_idx) {
            predictive_scroll_triggered_for_this_input_idx = false;
            y_offset_due_to_prediction_for_current_idx = 0;
        }

        // Cursor blink logic
        if (SDL_GetTicks() - last_blink_time > 500) { show_cursor_flag = !show_cursor_flag; last_blink_time = SDL_GetTicks(); }

        // Clear screen
        SDL_SetRenderDrawColor(appCtx.ren, appCtx.palette[COL_BG].r, appCtx.palette[COL_BG].g, appCtx.palette[COL_BG].b, appCtx.palette[COL_BG].a);
        SDL_RenderClear(appCtx.ren);

        // Render Timer
        int timer_h_val = 0;
        int timer_w_val = 0;
        RenderAppTimer(&appCtx, typing_started_main ? (SDL_GetTicks() - start_time_main) : 0, &timer_h_val, &timer_w_val);

        // Render Live Stats (WPM, Accuracy, etc.)
        RenderLiveStats(&appCtx, SDL_GetTicks(), start_time_main,
                        text_to_type,
                        input_buffer, current_input_byte_idx_main,
                        TEXT_AREA_X, timer_w_val, TEXT_AREA_PADDING_Y, timer_h_val);


        // Calculate Y position for the top of the text viewport (below timer/stats)
        int text_viewport_top_y_val = TEXT_AREA_PADDING_Y + timer_h_val + TEXT_AREA_PADDING_Y;

        // Calculate the logical absolute Y and exact X of the cursor based on current input
        int logical_cursor_abs_y = 0; // Absolute Y from top of entire text
        int logical_cursor_x = 0;     // X position on its line
        CalculateCursorLayout(&appCtx, text_to_type, final_text_len_val, current_input_byte_idx_main, &logical_cursor_abs_y, &logical_cursor_x);

        // Determine the Y coordinate to use for scrolling decisions (predictive or current)
        int y_for_scroll_update;

        if (predictive_scroll_triggered_for_this_input_idx) {
            // If predictive scroll was triggered for this input index, maintain that Y offset
            y_for_scroll_update = logical_cursor_abs_y + y_offset_due_to_prediction_for_current_idx;
            #if ENABLE_DETAILED_LOGGING
            if (log_file && (current_input_byte_idx_main > 0)) { // Log for relevant indices
                fprintf(log_file, "[MainLoop] Maintaining predictive Y: current_idx=%zu, logical_y=%d, offset=%d, y_for_scroll=%d\n",
                        current_input_byte_idx_main, logical_cursor_abs_y, y_offset_due_to_prediction_for_current_idx, y_for_scroll_update);
            }
            #endif
        } else {
            y_for_scroll_update = logical_cursor_abs_y; // Default to current cursor's logical Y

            // Predictive scrolling: if cursor is on the target line and next char would wrap, scroll early
            if (appCtx.line_h > 0) {
                int current_cursor_abs_line_idx = logical_cursor_abs_y / appCtx.line_h;
                // Target line in viewport where we want the cursor to ideally be before it needs to scroll
                int target_abs_line_for_cursor_in_viewport = first_visible_abs_line_num_static + CURSOR_TARGET_VIEWPORT_LINE;

                if (current_cursor_abs_line_idx == target_abs_line_for_cursor_in_viewport &&
                    current_input_byte_idx_main < final_text_len_val) { // And not at the very end of text

                    // Check layout of the *next* character position
                    size_t next_logical_pos_idx = 0;
                    const char* p_next = text_to_type + current_input_byte_idx_main;
                    decode_utf8(&p_next, text_to_type + final_text_len_val); // Advance by one char
                    next_logical_pos_idx = (size_t)(p_next - text_to_type);


                    if (next_logical_pos_idx <= final_text_len_val && next_logical_pos_idx > current_input_byte_idx_main) {
                        int y_of_next_logical_pos, x_of_next_logical_pos;
                        CalculateCursorLayout(&appCtx, text_to_type, final_text_len_val, next_logical_pos_idx, &y_of_next_logical_pos, &x_of_next_logical_pos);

                        // If the next character position is on a new line (due to wrap)
                        if (y_of_next_logical_pos > logical_cursor_abs_y) {
                            // Calculate what the first visible line *would be* if we scrolled for this y_next
                            int potential_new_first_visible = (y_of_next_logical_pos / appCtx.line_h) - CURSOR_TARGET_VIEWPORT_LINE;
                            if (potential_new_first_visible < 0) potential_new_first_visible = 0;

                            // If this potential scroll is further than current, then predictively scroll
                            if (potential_new_first_visible > first_visible_abs_line_num_static) {
                                y_for_scroll_update = y_of_next_logical_pos; // Use next char's Y for scroll update
                                y_offset_due_to_prediction_for_current_idx = y_of_next_logical_pos - logical_cursor_abs_y;
                                predictive_scroll_triggered_for_this_input_idx = true; // Flag that we used prediction
                                #if ENABLE_DETAILED_LOGGING
                                if (log_file && (current_input_byte_idx_main > 0)) {
                                    fprintf(log_file, "[MainLoop] Predictive scroll ACTIVATED: current_idx=%zu (y=%d), next_idx_check=%zu (y_next=%d). Using y_next=%d for scroll. Offset=%d. PotNewFirstVis=%d, CurFirstVis=%d\n",
                                            current_input_byte_idx_main, logical_cursor_abs_y, next_logical_pos_idx, y_of_next_logical_pos, y_for_scroll_update, y_offset_due_to_prediction_for_current_idx, potential_new_first_visible, first_visible_abs_line_num_static);
                                }
                                #endif
                            }
                        }
                    }
                }
            }
        }

        // Update the first visible line based on the (potentially predictive) y_for_scroll_update
        int current_first_visible_for_log_check = first_visible_abs_line_num_static; // For logging change
        UpdateVisibleLine(y_for_scroll_update, appCtx.line_h, &first_visible_abs_line_num_static);

        // Log changes in scrolling or cursor position for debugging
        #if ENABLE_DETAILED_LOGGING
        if (log_file && (current_input_byte_idx_main != prev_input_idx_for_log || first_visible_abs_line_num_static != prev_first_visible_for_log_main)) {
            fprintf(log_file, "[MainLoop] input_idx: %zu, logical_cursor_abs_y: %d (line ~%d), y_for_update: %d (line ~%d), logical_cursor_x: %d | PrevFirstVis: %d, CurFirstVisBeforeUpdate: %d, NewFirstVis: %d\n",
                current_input_byte_idx_main,
                logical_cursor_abs_y, (appCtx.line_h > 0 ? logical_cursor_abs_y / appCtx.line_h : -1),
                y_for_scroll_update, (appCtx.line_h > 0 ? y_for_scroll_update / appCtx.line_h : -1),
                logical_cursor_x,
                prev_first_visible_for_log_main, // Log the value from the previous frame
                current_first_visible_for_log_check, // Log the value before this frame's UpdateVisibleLine
                first_visible_abs_line_num_static); // Log the new value after UpdateVisibleLine
        }
        #endif
        prev_input_idx_for_log = current_input_byte_idx_main; // Update for next frame's log
        prev_first_visible_for_log_main = first_visible_abs_line_num_static; // Update for next frame's log

        // Render the text content and get the final screen coordinates for the cursor
        int final_cursor_x_on_screen_calc = -100; // Default off-screen
        int final_cursor_y_baseline_on_screen_calc = -100;

        RenderTextContent(&appCtx, text_to_type, final_text_len_val, input_buffer, current_input_byte_idx_main,
                          first_visible_abs_line_num_static, // Use the updated first visible line
                          text_viewport_top_y_val,
                          &final_cursor_x_on_screen_calc, &final_cursor_y_baseline_on_screen_calc);

        // Fallback for cursor position if RenderTextContent didn't place it (e.g., cursor is off-screen due to RenderTextContent's own logic)
        // This tries to place it based on logical_cursor_abs_y if that line is visible.
        if (final_cursor_x_on_screen_calc < TEXT_AREA_X || final_cursor_y_baseline_on_screen_calc < text_viewport_top_y_val ||
            final_cursor_y_baseline_on_screen_calc >= text_viewport_top_y_val + (DISPLAY_LINES * appCtx.line_h) ) {
             if (appCtx.line_h > 0) {
                int cursor_logical_line_idx = logical_cursor_abs_y / appCtx.line_h;
                int cursor_viewport_line = cursor_logical_line_idx - first_visible_abs_line_num_static;
                if (cursor_viewport_line >= 0 && cursor_viewport_line < DISPLAY_LINES) { // If the logical line is visible
                    final_cursor_x_on_screen_calc = logical_cursor_x; // Use X from CalculateCursorLayout
                    final_cursor_y_baseline_on_screen_calc = text_viewport_top_y_val + cursor_viewport_line * appCtx.line_h;
                } else { // Logical line is not visible, keep cursor off-screen
                     final_cursor_x_on_screen_calc = -100;
                     final_cursor_y_baseline_on_screen_calc = -100;
                }
            }
        }

        // Render the cursor at its final calculated screen position
        RenderAppCursor(&appCtx, show_cursor_flag, final_cursor_x_on_screen_calc, final_cursor_y_baseline_on_screen_calc,
                        logical_cursor_abs_y, // Pass logical Y for potential debug in RenderAppCursor
                        first_visible_abs_line_num_static,
                        text_viewport_top_y_val);

        // Present the rendered frame
        SDL_RenderPresent(appCtx.ren);
        if (log_file) fflush(log_file); // Flush logs frequently for debugging
        SDL_Delay(16); // Aim for ~60 FPS
    }

    SDL_StopTextInput();
    if (typing_started_main) {
        CalculateAndPrintAppStats(start_time_main, current_input_byte_idx_main, text_to_type, final_text_len_val, input_buffer);
    } else {
        printf("No typing started. No stats to display.\n");
    }

    CleanupApp(&appCtx);
    if (text_to_type) free(text_to_type); text_to_type = NULL;
    if (input_buffer) free(input_buffer); input_buffer = NULL;

    if (log_file) {
        fprintf(log_file, "Application finished gracefully.\n");
        fflush(log_file);
        fclose(log_file);
        log_file = NULL;
    }
    return 0;
}