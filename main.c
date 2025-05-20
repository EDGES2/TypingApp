// main.c
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

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

Sint32 decode_utf8(const char **s_ptr, const char *s_end) {
    if (!s_ptr || !*s_ptr || *s_ptr >= s_end) return 0;
    const unsigned char *s = (const unsigned char *)*s_ptr;
    unsigned char c1 = *s;
    Sint32 codepoint;
    int len = 0;

    if (c1 < 0x80) { codepoint = c1; len = 1; }
    else if ((c1 & 0xE0) == 0xC0) {
        if (s + 1 >= (const unsigned char*)s_end || (s[1] & 0xC0) != 0x80) return -1;
        codepoint = ((c1 & 0x1F) << 6) | (s[1] & 0x3F); len = 2;
    } else if ((c1 & 0xF0) == 0xE0) {
        if (s + 2 >= (const unsigned char*)s_end || (s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80) return -1;
        codepoint = ((c1 & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F); len = 3;
    } else if ((c1 & 0xF8) == 0xF0) {
        if (s + 3 >= (const unsigned char*)s_end || (s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 || (s[3] & 0xC0) != 0x80) return -1;
        codepoint = ((c1 & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F); len = 4;
    } else { return -1; }
    (*s_ptr) += len;
    return codepoint;
}

int get_codepoint_advance_and_metrics(TTF_Font *font, Uint32 codepoint, int fallback_adv,
                                     int *out_char_w, int *out_char_h,
                                     int glyph_cache_adv[],
                                     int glyph_cache_w[][128], int glyph_cache_h[][128]) {
    int adv = 0;
    int char_w = 0;
    int char_h = TTF_FontHeight(font);

    if (codepoint < 128 && codepoint >= 32) {
        adv = glyph_cache_adv[codepoint];
        if (glyph_cache_w[COL_TEXT][codepoint] > 0) char_w = glyph_cache_w[COL_TEXT][codepoint];
        if (glyph_cache_h[COL_TEXT][codepoint] > 0) char_h = glyph_cache_h[COL_TEXT][codepoint];
        if (adv == 0 && fallback_adv > 0) adv = fallback_adv;
    } else {
        if (TTF_GlyphMetrics32(font, codepoint, NULL, NULL, NULL, NULL, &adv) != 0) {
            adv = fallback_adv;
        }
        char_w = adv;
    }
    if (adv <= 0 && codepoint != '\t' && codepoint != '\n') adv = fallback_adv;

    if (out_char_w) *out_char_w = (char_w > 0) ? char_w : adv;
    if (out_char_h) *out_char_h = (char_h > 0) ? char_h : TTF_FontHeight(font);

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
                                  int glyph_cache_adv[],
                                  int glyph_cache_w[][128], int glyph_cache_h[][128]) {
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
            block.pixel_width = tab_width_pixels - (offset_in_line % tab_width_pixels);
            if (block.pixel_width == 0 && offset_in_line >=0) block.pixel_width = tab_width_pixels;
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
            block.pixel_width += get_codepoint_advance_and_metrics(font, cp, fallback_char_adv,
                                                                 NULL, NULL,
                                                                 glyph_cache_adv, glyph_cache_w, glyph_cache_h);
        }
    }
    block.num_bytes = (*text_parser_ptr) - block.start_ptr;
    return block;
}


int main(int argc, char **argv) {
    // 1) Load text
    const char *path = (argc > 1 ? argv[1] : TEXT_FILE_PATH);
    FILE *f = fopen(path, "rb");
    if (!f) { perror("Failed to open text file"); fprintf(stderr, "Attempted to open: %s\n", path); return 1; }
    char *text_orig_buffer = malloc(MAX_TEXT_LEN);
    if (!text_orig_buffer) { perror("Failed to allocate memory for text_orig_buffer"); fclose(f); return 1; }
    size_t text_len_orig = fread(text_orig_buffer, 1, MAX_TEXT_LEN - 1, f);
    fclose(f);
    text_orig_buffer[text_len_orig] = '\0';

    char *processed_text_buffer = malloc(MAX_TEXT_LEN);
    if (!processed_text_buffer) { perror("Failed to allocate memory for processed_text_buffer"); free(text_orig_buffer); return 1;}

    size_t final_text_write_idx = 0;
    const char* current_read_ptr = text_orig_buffer;
    const char* text_orig_buffer_end = text_orig_buffer + text_len_orig;

    while(current_read_ptr < text_orig_buffer_end) {
        const char* char_sequence_start_ptr = current_read_ptr;
        Sint32 codepoint = decode_utf8(&current_read_ptr, text_orig_buffer_end);

        if (codepoint <= 0) {
            if (current_read_ptr <= char_sequence_start_ptr && current_read_ptr < text_orig_buffer_end) {
                if (final_text_write_idx < MAX_TEXT_LEN - 1) {
                    processed_text_buffer[final_text_write_idx++] = '?';
                }
                current_read_ptr++;
            }
            continue;
        }

        if (codepoint == 0x2018 || codepoint == 0x2019 ||
            codepoint == 0x201C || codepoint == 0x201D) {
            if (final_text_write_idx < MAX_TEXT_LEN -1) {
                processed_text_buffer[final_text_write_idx++] = '\'';
            }
        } else {
            size_t char_byte_length = current_read_ptr - char_sequence_start_ptr;
            if (final_text_write_idx + char_byte_length < MAX_TEXT_LEN -1) {
                memcpy(processed_text_buffer + final_text_write_idx, char_sequence_start_ptr, char_byte_length);
                final_text_write_idx += char_byte_length;
            } else {
                fprintf(stderr, "Warning: processed_text_buffer overflow during quote replacement.\n");
                break;
            }
        }
    }
    size_t final_text_len = final_text_write_idx;
    processed_text_buffer[final_text_len] = '\0';
    free(text_orig_buffer);
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

    int physW, physH; SDL_GetRendererOutputSize(ren, &physW, &physH);
    float scale_x = (float)physW / WINDOW_W; float scale_y = (float)physH / WINDOW_H;
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

    SDL_Texture *glyph_tex_cache[N_COLORS][128] = {NULL};
    int glyph_adv_cache[128] = {0};
    int glyph_w_cache[N_COLORS][128] = {0};
    int glyph_h_cache[N_COLORS][128] = {0};

    for (int c = 32; c < 127; c++) {
        int adv_val;
        if (TTF_GlyphMetrics(font, (Uint16)c, NULL, NULL, NULL, NULL, &adv_val) != 0) {
            adv_val = FONT_SIZE / 2;
        }
        glyph_adv_cache[c] = (adv_val > 0) ? adv_val : FONT_SIZE/2;

        for (int col = COL_TEXT; col <= COL_INCORRECT; col++) {
            SDL_Surface *surf = TTF_RenderGlyph_Blended(font, (Uint16)c, palette[col]);
            if (!surf) continue;
            glyph_w_cache[col][c] = surf->w;
            glyph_h_cache[col][c] = surf->h;
            glyph_tex_cache[col][c] = SDL_CreateTextureFromSurface(ren, surf);
            SDL_FreeSurface(surf);
        }
    }

    int space_advance_width_val = glyph_adv_cache[' '];
    if (space_advance_width_val <= 0) space_advance_width_val = FONT_SIZE / 3;
    const int TAB_WIDTH_IN_PIXELS_VAL = TAB_SIZE_IN_SPACES * space_advance_width_val;

    // 7) Input state
    char *input_buffer = calloc(final_text_len + 100, 1);
    if (!input_buffer && final_text_len > 0) { perror("Failed to allocate input_buffer"); /* ... cleanup ... */ free(text_to_type); return 1; }
    size_t current_input_byte_idx = 0;
    Uint32 start_time = 0; bool typing_started = false;
    SDL_StartTextInput();
    bool show_cursor = true; Uint32 last_blink_time = SDL_GetTicks();
    int timer_text_h_fallback = line_h;

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
                    current_input_byte_idx = last_full_char_start_bk - start_of_buffer_bk;
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
        int current_timer_w = 0, current_timer_h = 0; SDL_Surface *timer_surf = TTF_RenderText_Blended(font, timer_buf, palette[COL_CURSOR]);
        if (timer_surf) {
            SDL_Texture *timer_tex = SDL_CreateTextureFromSurface(ren, timer_surf);
            current_timer_w = timer_surf->w; current_timer_h = timer_surf->h;
            SDL_Rect rtimer = { TEXT_AREA_X, TEXT_AREA_PADDING_Y, current_timer_w, current_timer_h };
            if (timer_tex) { SDL_RenderCopy(ren, timer_tex, NULL, &rtimer); SDL_DestroyTexture(timer_tex); }
            SDL_FreeSurface(timer_surf);
        } else { current_timer_h = timer_text_h_fallback; }
        if (current_timer_h < line_h / 2 && line_h > 0) current_timer_h = line_h;


        int text_viewport_top_y = TEXT_AREA_PADDING_Y + current_timer_h + TEXT_AREA_PADDING_Y * 2;

        int cursor_abs_y_start_of_line = 0; int cursor_char_abs_x = TEXT_AREA_X;
        int current_layout_pen_x = TEXT_AREA_X; int current_layout_line_abs_y = 0;
        const char *p_layout_iter = text_to_type; const char *p_text_end_for_layout = text_to_type + final_text_len;
        size_t layout_processed_bytes = 0;

        while(p_layout_iter < p_text_end_for_layout) {
            if (layout_processed_bytes == current_input_byte_idx) {
                cursor_char_abs_x = current_layout_pen_x;
                cursor_abs_y_start_of_line = current_layout_line_abs_y;
            }
            TextBlockInfo block = get_next_text_block(&p_layout_iter, p_text_end_for_layout, font,
                                                  space_advance_width_val, TAB_WIDTH_IN_PIXELS_VAL,
                                                  space_advance_width_val, current_layout_pen_x,
                                                  glyph_adv_cache, glyph_w_cache, glyph_h_cache);
            if (block.num_bytes == 0 && p_layout_iter >= p_text_end_for_layout) break;
            if (block.num_bytes == 0) {
                if(p_layout_iter < p_text_end_for_layout) p_layout_iter++; else break;
                continue;
            }

            if (block.is_newline) {
                current_layout_line_abs_y += line_h; current_layout_pen_x = TEXT_AREA_X;
            } else {
                if (current_layout_pen_x + block.pixel_width > TEXT_AREA_X + TEXT_AREA_W &&
                    current_layout_pen_x != TEXT_AREA_X && block.is_word) {
                    current_layout_line_abs_y += line_h; current_layout_pen_x = TEXT_AREA_X;
                }
                current_layout_pen_x += block.pixel_width;
            }
            layout_processed_bytes += block.num_bytes;
             if (layout_processed_bytes == current_input_byte_idx) {
                cursor_char_abs_x = current_layout_pen_x;
                cursor_abs_y_start_of_line = current_layout_line_abs_y;
            }
        }
        if (current_input_byte_idx == final_text_len) {
             cursor_char_abs_x = current_layout_pen_x;
             cursor_abs_y_start_of_line = current_layout_line_abs_y;
        }

        int scroll_offset_y = 0;
        int target_screen_line_for_cursor_top_y = text_viewport_top_y + CURSOR_TARGET_VIEWPORT_LINE * line_h;
        scroll_offset_y = cursor_abs_y_start_of_line - (target_screen_line_for_cursor_top_y - text_viewport_top_y);
        if (scroll_offset_y < 0) scroll_offset_y = 0;

        int render_pen_x = TEXT_AREA_X;
        int render_current_line_abs_y = 0;
        int render_pen_y_on_screen = text_viewport_top_y - scroll_offset_y;

        int final_cursor_draw_x = TEXT_AREA_X;
        int final_cursor_draw_y_baseline = render_pen_y_on_screen;
        if (current_input_byte_idx == 0) {
             final_cursor_draw_x = render_pen_x;
             final_cursor_draw_y_baseline = render_pen_y_on_screen;
        }

        const char *p_render_iter = text_to_type;
        const char *p_text_end_for_render = text_to_type + final_text_len;
        int lines_on_screen_count = 0;
        bool first_line_in_viewport_counted = false;


        while(p_render_iter < p_text_end_for_render) {
            if (lines_on_screen_count >= DISPLAY_LINES &&
                render_pen_y_on_screen >= text_viewport_top_y + DISPLAY_LINES * line_h) {
                break;
            }

            size_t current_block_start_byte_pos_render = p_render_iter - text_to_type;
            TextBlockInfo render_block = get_next_text_block(&p_render_iter, p_text_end_for_render, font,
                                                         space_advance_width_val, TAB_WIDTH_IN_PIXELS_VAL,
                                                         space_advance_width_val, render_pen_x,
                                                         glyph_adv_cache, glyph_w_cache, glyph_h_cache);
            if (render_block.num_bytes == 0 && p_render_iter >= p_text_end_for_render) break;
            if (render_block.num_bytes == 0) {
                if(p_render_iter < p_text_end_for_render) p_render_iter++; else break;
                continue;
            }

            if (current_block_start_byte_pos_render == current_input_byte_idx) {
                final_cursor_draw_x = render_pen_x;
                final_cursor_draw_y_baseline = render_pen_y_on_screen;
            }

            bool new_line_started_on_screen = false;
            if (render_block.is_newline) {
                render_current_line_abs_y += line_h;
                render_pen_y_on_screen = text_viewport_top_y + render_current_line_abs_y - scroll_offset_y;
                render_pen_x = TEXT_AREA_X;
                new_line_started_on_screen = true;

            } else {
                if (render_pen_x + render_block.pixel_width > TEXT_AREA_X + TEXT_AREA_W &&
                    render_pen_x != TEXT_AREA_X && render_block.is_word) {
                    render_current_line_abs_y += line_h;
                    render_pen_y_on_screen = text_viewport_top_y + render_current_line_abs_y - scroll_offset_y;
                    render_pen_x = TEXT_AREA_X;
                    new_line_started_on_screen = true;
                }

                bool block_is_visible_vertically = (render_pen_y_on_screen + line_h > text_viewport_top_y - line_h/2 &&
                                                    render_pen_y_on_screen < text_viewport_top_y + DISPLAY_LINES * line_h + line_h/2);

                if (block_is_visible_vertically) {
                    if (new_line_started_on_screen || !first_line_in_viewport_counted) {
                         if (render_pen_y_on_screen >= text_viewport_top_y - line_h/2 &&
                             render_pen_y_on_screen < text_viewport_top_y + DISPLAY_LINES * line_h + line_h/2) {
                            if (!first_line_in_viewport_counted) {
                                lines_on_screen_count = 1;
                                first_line_in_viewport_counted = true;
                            } else if (new_line_started_on_screen) {
                                lines_on_screen_count++;
                            }
                        }
                    }

                    if (!render_block.is_tab) {
                        const char *p_char_in_block_iter = render_block.start_ptr;
                        const char *p_char_in_block_iter_end = render_block.start_ptr + render_block.num_bytes;
                        size_t char_byte_offset_in_block_render = 0;

                        while(p_char_in_block_iter < p_char_in_block_iter_end) {
                            const char* current_glyph_render_start_ptr = p_char_in_block_iter;
                            Sint32 codepoint_to_render = decode_utf8(&p_char_in_block_iter, p_char_in_block_iter_end);
                            if (codepoint_to_render <= 0) break;

                            size_t char_abs_byte_pos_render = (render_block.start_ptr - text_to_type) + char_byte_offset_in_block_render;
                            if (char_abs_byte_pos_render == current_input_byte_idx) {
                                final_cursor_draw_x = render_pen_x;
                                final_cursor_draw_y_baseline = render_pen_y_on_screen;
                            }

                            int glyph_w_render_val = 0, glyph_h_render_val = 0;
                            // Виправлено: виклик get_codepoint_advance_and_metrics без зайвого аргументу (SDL_Color)
                            int adv_render = get_codepoint_advance_and_metrics(font, codepoint_to_render, space_advance_width_val,
                                                                        &glyph_w_render_val, &glyph_h_render_val,
                                                                        glyph_adv_cache, glyph_w_cache, glyph_h_cache);

                            if (render_pen_x + adv_render > TEXT_AREA_X + TEXT_AREA_W && render_pen_x != TEXT_AREA_X) {
                                render_current_line_abs_y += line_h;
                                render_pen_y_on_screen = text_viewport_top_y + render_current_line_abs_y - scroll_offset_y;
                                render_pen_x = TEXT_AREA_X;
                                bool inner_new_line_started = true; // Позначка для внутрішнього переносу
                                if (char_abs_byte_pos_render == current_input_byte_idx) {
                                    final_cursor_draw_x = render_pen_x;
                                    final_cursor_draw_y_baseline = render_pen_y_on_screen;
                                }
                                 if(render_pen_y_on_screen >= text_viewport_top_y - line_h/2 && render_pen_y_on_screen < text_viewport_top_y + DISPLAY_LINES * line_h + line_h/2) {
                                    if (!first_line_in_viewport_counted && render_pen_y_on_screen >= text_viewport_top_y - line_h/2) { // Якщо це перший рядок у viewport
                                        lines_on_screen_count = 1;
                                        first_line_in_viewport_counted = true;
                                    } else if (first_line_in_viewport_counted && inner_new_line_started) {
                                         lines_on_screen_count++;
                                    }
                                }
                                if (lines_on_screen_count > DISPLAY_LINES && render_pen_y_on_screen >= text_viewport_top_y + DISPLAY_LINES * line_h) goto end_render_loop_label_main_inner;
                            }
                            if (lines_on_screen_count > DISPLAY_LINES && render_pen_y_on_screen >= text_viewport_top_y + DISPLAY_LINES * line_h) goto end_render_loop_label_main_inner;


                            SDL_Color char_render_color;
                            bool is_char_typed = char_abs_byte_pos_render < current_input_byte_idx;
                            bool is_char_correct_val = false; // Виправлено: ініціалізація та область видимості

                            if (is_char_typed) {
                                size_t len_of_current_glyph_bytes = p_char_in_block_iter - current_glyph_render_start_ptr;
                                if (char_abs_byte_pos_render + len_of_current_glyph_bytes <= current_input_byte_idx) {
                                     is_char_correct_val = (memcmp(current_glyph_render_start_ptr, input_buffer + char_abs_byte_pos_render, len_of_current_glyph_bytes) == 0);
                                }
                                char_render_color = is_char_correct_val ? palette[COL_CORRECT] : palette[COL_INCORRECT];
                            } else {
                                char_render_color = palette[COL_TEXT];
                            }

                            if (codepoint_to_render >= 32) {
                                SDL_Texture* tex_to_render_final = NULL;
                                bool rendered_on_the_fly = false;
                                if (codepoint_to_render < 128) {
                                    int col_idx_for_cache_lookup; // Виправлено: логіка визначення індексу кольору
                                    if (is_char_typed) {
                                        col_idx_for_cache_lookup = is_char_correct_val ? COL_CORRECT : COL_INCORRECT;
                                    } else {
                                        col_idx_for_cache_lookup = COL_TEXT;
                                    }
                                    tex_to_render_final = glyph_tex_cache[col_idx_for_cache_lookup][(int)codepoint_to_render];
                                }
                                if (!tex_to_render_final) {
                                    SDL_Surface* surf_otf = TTF_RenderGlyph32_Blended(font, codepoint_to_render, char_render_color);
                                    if (surf_otf) {
                                        tex_to_render_final = SDL_CreateTextureFromSurface(ren, surf_otf);
                                        glyph_w_render_val = surf_otf->w; glyph_h_render_val = surf_otf->h;
                                        SDL_FreeSurface(surf_otf);
                                        rendered_on_the_fly = true;
                                    }
                                }
                                if (tex_to_render_final) {
                                    SDL_Rect dst_rect = {render_pen_x, render_pen_y_on_screen + (line_h - glyph_h_render_val) / 2, glyph_w_render_val, glyph_h_render_val};
                                    SDL_RenderCopy(ren, tex_to_render_final, NULL, &dst_rect);
                                    if (rendered_on_the_fly) {
                                        SDL_DestroyTexture(tex_to_render_final);
                                    }
                                }
                            }
                            render_pen_x += adv_render;
                            char_byte_offset_in_block_render += (p_char_in_block_iter - current_glyph_render_start_ptr);
                        }
                        end_render_loop_label_main_inner:;
                    } else {
                        render_pen_x += render_block.pixel_width;
                    }
                }
                 else if (render_pen_y_on_screen >= text_viewport_top_y + DISPLAY_LINES * line_h + line_h/2){
                    goto end_render_loop_label_main_outer;
                }
            }

            if (current_block_start_byte_pos_render + render_block.num_bytes == current_input_byte_idx) {
                final_cursor_draw_x = render_pen_x;
                final_cursor_draw_y_baseline = render_pen_y_on_screen;
            }
        }
        end_render_loop_label_main_outer:;


        if (show_cursor && current_input_byte_idx <= final_text_len) {
             if (final_cursor_draw_y_baseline + line_h >= text_viewport_top_y - line_h/2 &&
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

            size_t orig_char_len = p_orig - p_orig_char_start;
            size_t input_char_len = p_input - p_input_char_start;

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