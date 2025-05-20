// main.c
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// #include <time.h> // Clang-Tidy: Possibly unused (SDL_GetTicks використовується)
#include <stdbool.h>
#include <ctype.h>

#define WINDOW_W     800
#define WINDOW_H     200
#define FONT_SIZE    14
#define MAX_TEXT_LEN 10000
#ifndef TEXT_FILE_PATH
#define TEXT_FILE_PATH "text.txt"
#endif

#define TEXT_AREA_X 10
#define TEXT_AREA_PADDING_Y 10
#define TEXT_AREA_W (WINDOW_W - (2 * TEXT_AREA_X))
#define DISPLAY_LINES 3
#define CURSOR_TARGET_VIEWPORT_LINE 1
#define TAB_SIZE_IN_SPACES 4

enum { COL_BG, COL_TEXT, COL_CORRECT, COL_INCORRECT, COL_CURSOR, N_COLORS };

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

int get_codepoint_advance_and_metrics(TTF_Font *font, Uint32 codepoint, int fallback_adv,
                                     int *out_char_w, int *out_char_h,
                                     const int glyph_cache_adv[],
                                     const int glyph_cache_w[][128], const int glyph_cache_h[][128]) {
    int adv = 0;
    int char_w = 0;
    int char_h = 0;
    if (font) { // Clang-Tidy: Pointer may be null (додано перевірку)
        char_h = TTF_FontHeight(font);
    } else { // Якщо font == NULL, неможливо отримати висоту
        if (out_char_w) *out_char_w = fallback_adv;
        if (out_char_h) *out_char_h = FONT_SIZE; // Приблизна висота
        return fallback_adv;
    }


    if (codepoint < 128 && codepoint >= 32) {
        adv = glyph_cache_adv[codepoint];
        // Використовуємо COL_TEXT для розмірів з кешу, оскільки вони не залежать від поточного кольору рендерингу
        if (glyph_cache_w[COL_TEXT][codepoint] > 0) char_w = glyph_cache_w[COL_TEXT][codepoint];
        else char_w = adv;
        if (glyph_cache_h[COL_TEXT][codepoint] > 0) char_h = glyph_cache_h[COL_TEXT][codepoint];
        // else char_h залишається TTF_FontHeight(font);
        if (adv == 0 && fallback_adv > 0) adv = fallback_adv;
    } else {
        if (font && TTF_GlyphMetrics32(font, codepoint, NULL, NULL, NULL, NULL, &adv) != 0) {
            adv = fallback_adv;
        } else if (!font) {
            adv = fallback_adv;
        }
        char_w = adv;
    }
    if (adv <= 0 && codepoint != '\n' && codepoint != '\t') adv = fallback_adv;

    if (out_char_w) *out_char_w = (char_w > 0) ? char_w : adv;
    if (out_char_h) *out_char_h = (char_h > 0) ? char_h : TTF_FontHeight(font); // Висота з TTF_FontHeight надійніша

    return adv;
}

typedef struct {
    const char* start_ptr;
    size_t num_bytes;
    int pixel_width;
    bool is_word;
    bool is_newline;
    bool is_tab;
} TextBlockInfo;

TextBlockInfo get_next_text_block(const char **text_parser_ptr, const char *text_end, TTF_Font *font,
                                  int space_char_adv, int tab_width_pixels, int fallback_char_adv,
                                  int current_pen_x_for_tab_calc,
                                  const int glyph_cache_adv[],
                                  const int glyph_cache_w[][128], const int glyph_cache_h[][128]) {
    TextBlockInfo block = {0};
    if (!text_parser_ptr || !*text_parser_ptr || *text_parser_ptr >= text_end) return block;

    block.start_ptr = *text_parser_ptr;
    const char *p_runner_initial = *text_parser_ptr;
    Sint32 first_cp_in_block = decode_utf8(text_parser_ptr, text_end);

    if (first_cp_in_block <= 0) {
        if (*text_parser_ptr == p_runner_initial && *text_parser_ptr < text_end) {
            (*text_parser_ptr)++;
        }
        return block;
    }
    *text_parser_ptr = p_runner_initial;

    if (first_cp_in_block == '\n') {
        block.is_newline = true;
        decode_utf8(text_parser_ptr, text_end);
        block.pixel_width = 0;
    } else if (first_cp_in_block == '\t') {
        block.is_tab = true;
        decode_utf8(text_parser_ptr, text_end);
        if (tab_width_pixels > 0) {
            int offset_in_line = current_pen_x_for_tab_calc - TEXT_AREA_X;
             // Clang-Tidy: Condition is always true when reached (offset_in_line >= 0) - видалено, якщо current_pen_x_for_tab_calc завжди >= TEXT_AREA_X
            block.pixel_width = tab_width_pixels - (offset_in_line % tab_width_pixels);
            if (block.pixel_width == 0) block.pixel_width = tab_width_pixels;
            if (block.pixel_width <=0) block.pixel_width = tab_width_pixels;
        } else {
            block.pixel_width = space_char_adv;
        }
    } else {
        block.is_word = (first_cp_in_block != ' ');

        while(*text_parser_ptr < text_end) {
            const char* char_scan_start = *text_parser_ptr;
            Sint32 cp = decode_utf8(text_parser_ptr, text_end);

            if (cp <= 0 || cp == '\n' || cp == '\t') {
                *text_parser_ptr = char_scan_start;
                break;
            }
            bool current_char_is_space = (cp == ' ');
            if (current_char_is_space == block.is_word) {
                *text_parser_ptr = char_scan_start;
                break;
            }
            block.pixel_width += get_codepoint_advance_and_metrics(font, (Uint32)cp, fallback_char_adv,
                                                                 NULL, NULL,
                                                                 glyph_cache_adv, glyph_cache_w, glyph_cache_h);
        }
    }
    block.num_bytes = (*text_parser_ptr) - block.start_ptr;
    return block;
}


int main(int argc, char **argv) {
    // 1) Load and preprocess text
    const char *path = (argc > 1 ? argv[1] : TEXT_FILE_PATH);
    FILE *f = fopen(path, "rb");
    if (!f) { perror("Failed to open text file"); fprintf(stderr, "Attempted to open: %s\n", path); return 1; }

    char *raw_text_buffer = (char*)malloc(MAX_TEXT_LEN);
    if (!raw_text_buffer) { perror("Failed to allocate memory for raw_text_buffer"); fclose(f); return 1; }
    size_t raw_text_len = fread(raw_text_buffer, 1, MAX_TEXT_LEN - 1, f);
    fclose(f);
    raw_text_buffer[raw_text_len] = '\0';

    char *processed_text_buffer = (char*)malloc(MAX_TEXT_LEN);
    if (!processed_text_buffer) { perror("Failed to allocate memory for processed_text_buffer"); free(raw_text_buffer); return 1;}

    size_t pt_idx = 0;
    const char* p_read = raw_text_buffer;
    const char* p_read_end = raw_text_buffer + raw_text_len;
    bool at_line_start = true;
    bool last_char_written_was_space = true;
    int newlines_in_a_row = 0;

    while(p_read < p_read_end) {
        const char* char_start_original = p_read;
        Sint32 cp = decode_utf8(&p_read, p_read_end);
        size_t char_len_original = (size_t)(p_read - char_start_original);

        if (cp <= 0) {
            if (char_len_original == 0 && p_read < p_read_end) p_read++;
            continue;
        }

        bool replaced_quote = false;
        if (cp == 0x2018 || cp == 0x2019 || cp == 0x201C || cp == 0x201D) {
            cp = '\'';
            replaced_quote = true;
        }

        if (cp == '\n') {
            newlines_in_a_row++;
            if (pt_idx > 0 && processed_text_buffer[pt_idx-1] == ' ') {
                pt_idx--;
            }
            // Додаємо \n тільки якщо це перший \n після контенту, АБО якщо це перший \n у файлі, але тільки якщо був контент до нього.
            // Effectively, multiple newlines become one, but leading newlines in file are ignored.
            if (newlines_in_a_row == 1 && (pt_idx > 0 || (pt_idx == 0 && !at_line_start /* був контент до цього першого \n*/ )) ) {
                if (pt_idx < MAX_TEXT_LEN - 1) processed_text_buffer[pt_idx++] = '\n';
            }
            at_line_start = true;
            last_char_written_was_space = true;
        } else if (isspace((unsigned char)cp)) {
            if (!at_line_start && !last_char_written_was_space) {
                if (pt_idx < MAX_TEXT_LEN - 1) processed_text_buffer[pt_idx++] = ' ';
                last_char_written_was_space = true;
            }
        } else {
            if (at_line_start && newlines_in_a_row == 0 && pt_idx > 0 && processed_text_buffer[pt_idx-1] != '\n' && !last_char_written_was_space) {
                 if (pt_idx < MAX_TEXT_LEN - 1) processed_text_buffer[pt_idx++] = ' ';
            }

            if (replaced_quote) {
                 if (pt_idx < MAX_TEXT_LEN - 1) processed_text_buffer[pt_idx++] = (char)cp;
            } else {
                if (pt_idx + char_len_original < MAX_TEXT_LEN -1) {
                    memcpy(processed_text_buffer + pt_idx, char_start_original, char_len_original);
                    pt_idx += char_len_original;
                } else { break; }
            }
            at_line_start = false;
            last_char_written_was_space = false;
            newlines_in_a_row = 0;
        }
    }
    while (pt_idx > 0 && (processed_text_buffer[pt_idx-1] == '\n' || processed_text_buffer[pt_idx-1] == ' ')) {
        pt_idx--;
    }
    processed_text_buffer[pt_idx] = '\0';
    size_t final_text_len = pt_idx;
    free(raw_text_buffer);
    char * const text_to_type = processed_text_buffer;

    // 2) Init SDL + TTF
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError()); free(text_to_type); return 1; }
    if (TTF_Init() != 0) { fprintf(stderr, "TTF_Init error: %s\n", TTF_GetError()); SDL_Quit(); free(text_to_type); return 1; }
    TTF_SetFontHinting(NULL, TTF_HINTING_LIGHT);

    // 3) Window & renderer
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");
    SDL_Window *win = SDL_CreateWindow("TypingApp Monkeytype-like", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_W, WINDOW_H, SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) { fprintf(stderr, "SDL_CreateWindow error: %s\n", SDL_GetError()); TTF_Quit(); SDL_Quit(); free(text_to_type); return 1; }
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) { fprintf(stderr, "SDL_CreateRenderer error: %s\n", SDL_GetError()); SDL_DestroyWindow(win); TTF_Quit(); SDL_Quit(); free(text_to_type); return 1; }

    int physW_val, physH_val;
    SDL_GetRendererOutputSize(ren, &physW_val, &physH_val);
    float scale_x = (float)physW_val / WINDOW_W; float scale_y = (float)physH_val / WINDOW_H;
    SDL_RenderSetScale(ren, scale_x, scale_y);

    // 4) Load font
    TTF_Font *font = NULL;
    const char* font_paths[] = { "Arial.ttf", "/System/Library/Fonts/Arial.ttf", "/Library/Fonts/Arial Unicode.ttf", "/usr/share/fonts/truetype/msttcorefonts/Arial.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", NULL };
    for (int i = 0; font_paths[i] != NULL; ++i) {
        #if SDL_TTF_VERSION_ATLEAST(2,20,0)
            font = TTF_OpenFontDPI(font_paths[i], FONT_SIZE, (int)(72 * scale_x), (int)(72 * scale_y));
        #else
            font = TTF_OpenFont(font_paths[i], FONT_SIZE);
        #endif
        if (font) { printf("Loaded font: %s\n", font_paths[i]); break; }
    }
    if (!font) { fprintf(stderr, "Failed to load font: %s\n", TTF_GetError()); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); TTF_Quit(); SDL_Quit(); free(text_to_type); return 1;}

    // 5) Palette & glyph cache + metrics
    SDL_Color palette[N_COLORS];
    palette[COL_BG]        = (SDL_Color){50, 52, 55, 255}; palette[COL_TEXT]      = (SDL_Color){100,102,105,255};
    palette[COL_CORRECT]   = (SDL_Color){201,200,190,255}; palette[COL_INCORRECT] = (SDL_Color){200,  0,  0,255};
    palette[COL_CURSOR]    = (SDL_Color){255,200,  0,255};

    int line_h = TTF_FontLineSkip(font);
    if (line_h <= 0) line_h = TTF_FontHeight(font);
    if (line_h <= 0) line_h = FONT_SIZE + 4;

    SDL_Texture *glyph_tex_cache[N_COLORS][128] = {0};
    int glyph_adv_cache[128] = {0};
    int glyph_w_cache[N_COLORS][128] = {0};
    int glyph_h_cache[N_COLORS][128] = {0};

    for (int c = 32; c < 127; c++) {
        int adv_val;
        if (TTF_GlyphMetrics(font, (Uint16)c, NULL, NULL, NULL, NULL, &adv_val) != 0) {
            adv_val = FONT_SIZE / 2;
        }
        glyph_adv_cache[c] = (adv_val > 0) ? adv_val : FONT_SIZE/2;

        for (int col_idx = COL_TEXT; col_idx <= COL_INCORRECT; col_idx++) {
            SDL_Surface *surf = TTF_RenderGlyph_Blended(font, (Uint16)c, palette[col_idx]);
            if (!surf) continue;
            glyph_w_cache[col_idx][c] = surf->w;
            glyph_h_cache[col_idx][c] = surf->h;
            glyph_tex_cache[col_idx][c] = SDL_CreateTextureFromSurface(ren, surf);
            if (!glyph_tex_cache[col_idx][c]) {
                 fprintf(stderr, "Warning: Failed to create texture for glyph %c (ASCII %d) color %d\n", c, c, col_idx);
            }
            SDL_FreeSurface(surf);
        }
    }

    int space_advance_width_val = glyph_adv_cache[' '];
    if (space_advance_width_val <= 0) space_advance_width_val = FONT_SIZE / 3;
    const int TAB_WIDTH_IN_PIXELS_VAL = (space_advance_width_val > 0) ? (TAB_SIZE_IN_SPACES * space_advance_width_val) : (TAB_SIZE_IN_SPACES * (FONT_SIZE / 3));


    // 7) Input state
    char *input_buffer = (char*)calloc(final_text_len + 100, 1);
    if (!input_buffer && final_text_len > 0) { perror("Failed to allocate input_buffer"); /* ... cleanup ... */ free(text_to_type); return 1; }
    size_t current_input_byte_idx = 0;
    Uint32 start_time = 0; bool typing_started = false;
    SDL_StartTextInput();
    bool show_cursor = true; Uint32 last_blink_time = SDL_GetTicks();

    static int first_visible_abs_line_num = 0;

    // --- Основний цикл ---
    while (1) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) goto cleanup;
            if (ev.type == SDL_KEYDOWN) {
                if (ev.key.keysym.sym == SDLK_ESCAPE) goto cleanup;
                if (ev.key.keysym.sym == SDLK_BACKSPACE && current_input_byte_idx > 0) {
                    const char *start_of_buffer_bk = input_buffer;
                    const char *current_end_of_input_bk = input_buffer + current_input_byte_idx;
                    const char *ptr_to_prev_char_bk = start_of_buffer_bk;
                    const char *last_full_char_start_bk = start_of_buffer_bk;

                    while(ptr_to_prev_char_bk < current_end_of_input_bk) {
                        const char *char_being_decoded_start_bk = ptr_to_prev_char_bk;
                        Sint32 cp_bk = decode_utf8(&ptr_to_prev_char_bk, current_end_of_input_bk);

                        if (ptr_to_prev_char_bk > current_end_of_input_bk) {
                             ptr_to_prev_char_bk = char_being_decoded_start_bk;
                             break;
                        }
                        if (cp_bk <= 0) {
                             ptr_to_prev_char_bk = char_being_decoded_start_bk;
                             break;
                        }
                        if (ptr_to_prev_char_bk <= current_end_of_input_bk) {
                            last_full_char_start_bk = char_being_decoded_start_bk;
                        }
                         if (ptr_to_prev_char_bk == current_end_of_input_bk) {
                             break;
                        }
                    }
                    current_input_byte_idx = (size_t)(last_full_char_start_bk - start_of_buffer_bk);
                    input_buffer[current_input_byte_idx] = '\0';
                }
            }
            if (ev.type == SDL_TEXTINPUT) {
                if (!typing_started && final_text_len > 0) { start_time = SDL_GetTicks(); typing_started = true; }
                size_t input_event_len = strlen(ev.text.text);
                if (current_input_byte_idx + input_event_len <= final_text_len &&
                    current_input_byte_idx + input_event_len < final_text_len + 90 ) {

                    bool can_add_input = true;
                    if(input_event_len == 1 && ev.text.text[0] == ' ' && current_input_byte_idx > 0){
                        if(input_buffer[current_input_byte_idx-1] == ' ') can_add_input = false;
                    }
                    if(can_add_input){
                       strncat(input_buffer, ev.text.text, input_event_len);
                       current_input_byte_idx += input_event_len;
                    }
                }
            }
        }

        if (SDL_GetTicks() - last_blink_time > 500) { show_cursor = !show_cursor; last_blink_time = SDL_GetTicks(); }
        SDL_SetRenderDrawColor(ren, palette[COL_BG].r, palette[COL_BG].g, palette[COL_BG].b, palette[COL_BG].a);
        SDL_RenderClear(ren);
        Uint32 elapsed_ms = typing_started ? (SDL_GetTicks() - start_time) : 0;
        Uint32 elapsed_s = elapsed_ms / 1000; int m = elapsed_s / 60, s = elapsed_s % 60; char timer_buf[16];
        snprintf(timer_buf, sizeof(timer_buf), "%02d:%02d", m, s);
        int current_timer_w_val = 0;
        int current_timer_h = 0;
        SDL_Surface *timer_surf = TTF_RenderText_Blended(font, timer_buf, palette[COL_CURSOR]);
        if (timer_surf) {
            SDL_Texture *timer_tex = SDL_CreateTextureFromSurface(ren, timer_surf);
            if (timer_tex) {
                 current_timer_w_val = timer_surf->w; current_timer_h = timer_surf->h;
                 SDL_Rect rtimer = { TEXT_AREA_X, TEXT_AREA_PADDING_Y, current_timer_w_val, current_timer_h };
                 SDL_RenderCopy(ren, timer_tex, NULL, &rtimer);
                 SDL_DestroyTexture(timer_tex);
            } else {
                 current_timer_h = line_h;
            }
            SDL_FreeSurface(timer_surf);
        } else { current_timer_h = line_h; }
        if (current_timer_h <= 0 && line_h > 0) current_timer_h = line_h; // Забезпечити позитивну висоту


        int text_viewport_top_y = TEXT_AREA_PADDING_Y + current_timer_h + TEXT_AREA_PADDING_Y * 2;

        int cursor_abs_y_start_of_this_line_val = 0;
        int cursor_char_exact_x_val = TEXT_AREA_X;
        int layout_pen_x_val = TEXT_AREA_X;
        int layout_current_abs_line_y_val = 0;
        const char *p_layout_iter_val = text_to_type;
        const char *p_text_end_for_layout_val = text_to_type + final_text_len;
        size_t layout_processed_bytes_val = 0;
        bool cursor_pos_determined_this_pass_layout = false;

        if (current_input_byte_idx == 0) {
            cursor_char_exact_x_val = TEXT_AREA_X;
            cursor_abs_y_start_of_this_line_val = 0;
            cursor_pos_determined_this_pass_layout = true;
        }

        while(p_layout_iter_val < p_text_end_for_layout_val && !cursor_pos_determined_this_pass_layout) {
            size_t bytes_before_block_processing = layout_processed_bytes_val;
            int x_at_block_start = layout_pen_x_val;
            int y_at_block_start = layout_current_abs_line_y_val;

            TextBlockInfo block_layout = get_next_text_block(&p_layout_iter_val, p_text_end_for_layout_val, font,
                                                  space_advance_width_val, TAB_WIDTH_IN_PIXELS_VAL,
                                                  space_advance_width_val, layout_pen_x_val,
                                                  glyph_adv_cache, glyph_w_cache, glyph_h_cache);

            if (block_layout.num_bytes == 0) {
                if(p_layout_iter_val < p_text_end_for_layout_val) p_layout_iter_val++; else break;
                continue;
            }

            if (!cursor_pos_determined_this_pass_layout && current_input_byte_idx >= bytes_before_block_processing &&
                current_input_byte_idx < bytes_before_block_processing + block_layout.num_bytes) {
                cursor_abs_y_start_of_this_line_val = y_at_block_start;
                cursor_char_exact_x_val = x_at_block_start;
                const char* p_char_in_block_csr = block_layout.start_ptr;
                // const char* p_target_byte_csr = text_to_type + current_input_byte_idx;

                size_t bytes_to_walk_in_block = current_input_byte_idx - bytes_before_block_processing;
                size_t bytes_walked_in_block_csr = 0;

                const char* p_char_end_limit_csr = block_layout.start_ptr + block_layout.num_bytes;

                while(bytes_walked_in_block_csr < bytes_to_walk_in_block && p_char_in_block_csr < p_char_end_limit_csr) {
                    // const char* temp_char_start_for_adv_csr = p_char_in_block_csr;
                    Sint32 cp_inner_csr = decode_utf8(&p_char_in_block_csr, p_char_end_limit_csr);
                    if (cp_inner_csr <= 0) break;

                    int adv_inner_csr = get_codepoint_advance_and_metrics(font, (Uint32)cp_inner_csr, space_advance_width_val,
                                                                    NULL, NULL, glyph_adv_cache,
                                                                    glyph_w_cache, glyph_h_cache);

                    if (cursor_char_exact_x_val + adv_inner_csr > TEXT_AREA_X + TEXT_AREA_W && cursor_char_exact_x_val != TEXT_AREA_X) {
                        cursor_abs_y_start_of_this_line_val += line_h;
                        cursor_char_exact_x_val = TEXT_AREA_X;
                    }
                    cursor_char_exact_x_val += adv_inner_csr;
                    bytes_walked_in_block_csr = (size_t)(p_char_in_block_csr - block_layout.start_ptr);
                }
                cursor_pos_determined_this_pass_layout = true;
            }

            if (block_layout.is_newline) {
                layout_current_abs_line_y_val += line_h;
                layout_pen_x_val = TEXT_AREA_X;
            } else {
                if (layout_pen_x_val + block_layout.pixel_width > TEXT_AREA_X + TEXT_AREA_W &&
                    layout_pen_x_val != TEXT_AREA_X && block_layout.is_word) {
                    layout_current_abs_line_y_val += line_h;
                    layout_pen_x_val = TEXT_AREA_X;
                }
                layout_pen_x_val += block_layout.pixel_width;
            }
            layout_processed_bytes_val += block_layout.num_bytes;

            if (!cursor_pos_determined_this_pass_layout && layout_processed_bytes_val == current_input_byte_idx) {
                cursor_char_exact_x_val = layout_pen_x_val;
                cursor_abs_y_start_of_this_line_val = layout_current_abs_line_y_val;
                cursor_pos_determined_this_pass_layout = true;
            }
        }
        if (!cursor_pos_determined_this_pass_layout && current_input_byte_idx == final_text_len) {
             cursor_char_exact_x_val = layout_pen_x_val;
             cursor_abs_y_start_of_this_line_val = layout_current_abs_line_y_val;
        }

        // Тимчасово спрощена логіка прокрутки для стабілізації
        if (line_h > 0) {
            int cursor_current_abs_line_idx = cursor_abs_y_start_of_this_line_val / line_h;
            first_visible_abs_line_num = cursor_current_abs_line_idx - CURSOR_TARGET_VIEWPORT_LINE;
            if (first_visible_abs_line_num < 0) {
                first_visible_abs_line_num = 0;
            }
        }


        int render_pen_x = TEXT_AREA_X;
        int render_current_abs_line_num = 0;

        int final_cursor_draw_x = TEXT_AREA_X;
        int final_cursor_draw_y_baseline = text_viewport_top_y;

        const char *p_render_iter = text_to_type;
        const char *p_text_end_for_render = text_to_type + final_text_len;
        int lines_drawn_in_frame_count = 0;


        while(p_render_iter < p_text_end_for_render) {
            int current_viewport_line_idx = render_current_abs_line_num - first_visible_abs_line_num;

            if (current_viewport_line_idx >= DISPLAY_LINES) {
                break;
            }

            size_t current_block_start_byte_pos_render = (size_t)(p_render_iter - text_to_type);
            TextBlockInfo render_block = get_next_text_block(&p_render_iter, p_text_end_for_render, font,
                                                         space_advance_width_val, TAB_WIDTH_IN_PIXELS_VAL,
                                                         space_advance_width_val, render_pen_x,
                                                         glyph_adv_cache, glyph_w_cache, glyph_h_cache);
            if (render_block.num_bytes == 0 && p_render_iter >= p_text_end_for_render) break;
            if (render_block.num_bytes == 0) {
                if(p_render_iter < p_text_end_for_render) p_render_iter++; else break;
                continue;
            }

            int current_line_on_screen_y = text_viewport_top_y + current_viewport_line_idx * line_h;

            if (current_block_start_byte_pos_render == current_input_byte_idx) {
                final_cursor_draw_x = render_pen_x;
                final_cursor_draw_y_baseline = current_line_on_screen_y;
            }

            if (render_block.is_newline) {
                if (current_viewport_line_idx >=0 && current_viewport_line_idx < DISPLAY_LINES) {
                     // lines_drawn_in_frame_count++; // Рахуємо тільки коли рендеримо вміст рядка
                }
                render_current_abs_line_num++;
                render_pen_x = TEXT_AREA_X;
            } else {
                bool word_wrapped_on_render = false; // Змінено ім'я
                if (render_pen_x + render_block.pixel_width > TEXT_AREA_X + TEXT_AREA_W &&
                    render_pen_x != TEXT_AREA_X && render_block.is_word) {
                    render_current_abs_line_num++;
                    current_viewport_line_idx = render_current_abs_line_num - first_visible_abs_line_num;
                    if (current_viewport_line_idx >= DISPLAY_LINES) break;
                    current_line_on_screen_y = text_viewport_top_y + current_viewport_line_idx * line_h;
                    render_pen_x = TEXT_AREA_X;
                    word_wrapped_on_render = true;
                }

                if (current_viewport_line_idx >= 0 && current_viewport_line_idx < DISPLAY_LINES) {
                     if (render_pen_x == TEXT_AREA_X && lines_drawn_in_frame_count == current_viewport_line_idx ) {
                         lines_drawn_in_frame_count++;
                    }

                    if (!render_block.is_tab) {
                        const char *p_char_in_block_iter = render_block.start_ptr;
                        const char *p_char_in_block_iter_end = render_block.start_ptr + render_block.num_bytes;
                        size_t char_byte_offset_in_block_render = 0;

                        while(p_char_in_block_iter < p_char_in_block_iter_end) {
                            if (current_viewport_line_idx >= DISPLAY_LINES) goto end_char_loop_render_main_corrected_label_no_tidy_v5;

                            const char* current_glyph_render_start_ptr = p_char_in_block_iter;
                            Sint32 codepoint_to_render = decode_utf8(&p_char_in_block_iter, p_char_in_block_iter_end);
                            if (codepoint_to_render <= 0) break;

                            size_t char_abs_byte_pos_render = (render_block.start_ptr - text_to_type) + char_byte_offset_in_block_render;
                            if (char_abs_byte_pos_render == current_input_byte_idx) {
                                final_cursor_draw_x = render_pen_x;
                                final_cursor_draw_y_baseline = current_line_on_screen_y;
                            }

                            int glyph_w_render_val = 0, glyph_h_render_val = 0;
                            int adv_render = get_codepoint_advance_and_metrics(font, (Uint32)codepoint_to_render, space_advance_width_val,
                                                                        &glyph_w_render_val, &glyph_h_render_val,
                                                                        glyph_adv_cache, glyph_w_cache, glyph_h_cache);

                            // bool emergency_char_wrap_occurred_render = false; // Clang-Tidy: Unused
                            if (render_pen_x + adv_render > TEXT_AREA_X + TEXT_AREA_W && render_pen_x != TEXT_AREA_X) {
                                // if (current_viewport_line_idx >=0 && current_viewport_line_idx < DISPLAY_LINES) {
                                //      lines_drawn_in_frame_count++;
                                // }
                                render_current_abs_line_num++;
                                current_viewport_line_idx = render_current_abs_line_num - first_visible_abs_line_num;
                                if (current_viewport_line_idx >= DISPLAY_LINES) goto end_char_loop_render_main_corrected_label_no_tidy_v5;
                                current_line_on_screen_y = text_viewport_top_y + current_viewport_line_idx * line_h;
                                render_pen_x = TEXT_AREA_X;
                                // emergency_char_wrap_occurred_render = true;
                                if (char_abs_byte_pos_render == current_input_byte_idx) {
                                    final_cursor_draw_x = render_pen_x;
                                    final_cursor_draw_y_baseline = current_line_on_screen_y;
                                }
                                 if (lines_drawn_in_frame_count == current_viewport_line_idx) {
                                     lines_drawn_in_frame_count++;
                                 }
                            }

                            SDL_Color char_render_color;
                            bool is_char_typed = char_abs_byte_pos_render < current_input_byte_idx;
                            bool is_char_correct_val = false;
                            if (is_char_typed) {
                                size_t len_of_current_glyph_bytes = (size_t)(p_char_in_block_iter - current_glyph_render_start_ptr);
                                if (char_abs_byte_pos_render + len_of_current_glyph_bytes <= current_input_byte_idx) {
                                     is_char_correct_val = (memcmp(current_glyph_render_start_ptr, input_buffer + char_abs_byte_pos_render, len_of_current_glyph_bytes) == 0);
                                }
                                char_render_color = is_char_correct_val ? palette[COL_CORRECT] : palette[COL_INCORRECT];
                            } else { char_render_color = palette[COL_TEXT]; }

                            if (codepoint_to_render >= 32) {
                                SDL_Texture* tex_to_render_final = NULL; bool rendered_on_the_fly = false;
                                if (codepoint_to_render < 128) {
                                    int col_idx_for_cache_lookup;
                                    if (is_char_typed) { col_idx_for_cache_lookup = is_char_correct_val ? COL_CORRECT : COL_INCORRECT; }
                                    else { col_idx_for_cache_lookup = COL_TEXT; }
                                    tex_to_render_final = glyph_tex_cache[col_idx_for_cache_lookup][(int)codepoint_to_render];
                                }
                                if (!tex_to_render_final) {
                                    SDL_Surface* surf_otf = TTF_RenderGlyph32_Blended(font, (Uint32)codepoint_to_render, char_render_color);
                                    if (surf_otf) {
                                        tex_to_render_final = SDL_CreateTextureFromSurface(ren, surf_otf);
                                        if(tex_to_render_final) {glyph_w_render_val = surf_otf->w; glyph_h_render_val = surf_otf->h;}
                                        SDL_FreeSurface(surf_otf); rendered_on_the_fly = true;
                                    }
                                }
                                if (tex_to_render_final) {
                                    SDL_Rect dst_rect = {render_pen_x, current_line_on_screen_y + (line_h - glyph_h_render_val) / 2, glyph_w_render_val, glyph_h_render_val};
                                    SDL_RenderCopy(ren, tex_to_render_final, NULL, &dst_rect);
                                    if (rendered_on_the_fly) SDL_DestroyTexture(tex_to_render_final);
                                }
                            }
                            render_pen_x += adv_render;
                            char_byte_offset_in_block_render += (size_t)(p_char_in_block_iter - current_glyph_render_start_ptr);
                        }
                        end_char_loop_render_main_corrected_label_no_tidy_v5:;
                    } else {
                        render_pen_x += render_block.pixel_width;
                    }
                } else if (current_viewport_line_idx >= DISPLAY_LINES && lines_drawn_in_frame_count >= DISPLAY_LINES) {
                    // Clang-Tidy: Condition is always false (можливо, через попередній break)
                    // Видалено, оскільки зовнішній цикл має зупинити рендеринг
                    // break;
                }
            }

            if (current_block_start_byte_pos_render + render_block.num_bytes == current_input_byte_idx) {
                final_cursor_draw_x = render_pen_x;
                final_cursor_draw_y_baseline = current_line_on_screen_y;
            }
        }


        if (show_cursor && current_input_byte_idx <= final_text_len) {
            int cursor_viewport_line_idx = -1;
            if (line_h > 0) cursor_viewport_line_idx = (cursor_abs_y_start_of_this_line_val / line_h) - first_visible_abs_line_num;

            if (line_h > 0 && cursor_viewport_line_idx >=0 && cursor_viewport_line_idx < DISPLAY_LINES &&
                final_cursor_draw_y_baseline >= text_viewport_top_y - line_h/2 && // Clang-Tidy: Condition always true (може бути помилкою)
                final_cursor_draw_y_baseline < text_viewport_top_y + (DISPLAY_LINES * line_h) + line_h/2) {
                 SDL_Rect cur_rect = { final_cursor_draw_x, final_cursor_draw_y_baseline, 2, line_h };
                 SDL_SetRenderDrawColor(ren, palette[COL_CURSOR].r, palette[COL_CURSOR].g, palette[COL_CURSOR].b, palette[COL_CURSOR].a);
                 SDL_RenderFillRect(ren, &cur_rect);
            }
        }

        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

cleanup:
    SDL_StopTextInput();
    double total_s = 0; size_t correct_chars_count = 0;
    if (typing_started) {
        total_s = (SDL_GetTicks() - start_time) / 1000.0;
        const char* p_orig = text_to_type; const char* p_orig_end = text_to_type + final_text_len;
        const char* p_input = input_buffer; const char* p_input_end = input_buffer + current_input_byte_idx;
        while(p_orig < p_orig_end && p_input < p_input_end) {
            const char* p_orig_char_start = p_orig;
            Sint32 cp_orig = decode_utf8(&p_orig, p_orig_end);
            const char* p_input_char_start = p_input;
            Sint32 cp_input = decode_utf8(&p_input, p_input_end);

            if (cp_orig <=0 || cp_input <=0) break;

            size_t orig_char_len = (size_t)(p_orig - p_orig_char_start);
            size_t input_char_len = (size_t)(p_input - p_input_char_start);

            if (orig_char_len == input_char_len && memcmp(p_orig_char_start, p_input_char_start, orig_char_len) == 0) {
                correct_chars_count++;
            }
        }
    }
    double wpm = 0; if (total_s > 0.001 && correct_chars_count > 0) { wpm = ((double)correct_chars_count / 5.0) / (total_s / 60.0); }

    size_t typed_symbols_for_accuracy = 0;
    const char* p_count_typed_iter = text_to_type;
    const char* p_text_covered_by_input_end = text_to_type + current_input_byte_idx;
    if (p_text_covered_by_input_end > text_to_type + final_text_len) {
        p_text_covered_by_input_end = text_to_type + final_text_len;
    }
    while(p_count_typed_iter < p_text_covered_by_input_end) {
        Sint32 cp = decode_utf8(&p_count_typed_iter, p_text_covered_by_input_end);
        if (cp <=0) break;
        typed_symbols_for_accuracy++;
    }
    double accuracy = (typed_symbols_for_accuracy > 0) ? ((double)correct_chars_count / typed_symbols_for_accuracy) * 100.0 : 0.0;

    printf("Target symbols considered for accuracy: %zu\n", typed_symbols_for_accuracy);
    printf("Correct symbols: %zu\n", correct_chars_count);
    printf("Accuracy: %.2f%%\n", accuracy);
    printf("Time: %.2fs\n", total_s);
    printf("WPM: %.2f\n", wpm);

    for (int c = 32; c < 127; c++) { for (int col = COL_TEXT; col <= COL_INCORRECT; col++) { if (glyph_tex_cache[col][c]) SDL_DestroyTexture(glyph_tex_cache[col][c]);}}
    if (font) TTF_CloseFont(font);
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    TTF_Quit(); SDL_Quit();
    if (text_to_type) free(text_to_type);
    if (input_buffer) free(input_buffer);
    return 0;
}