// main.c
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h> // Для типу bool

#define WINDOW_W     800
#define WINDOW_H     200 // Збільшимо висоту для кількох рядків тексту
#define FONT_SIZE    22
#define MAX_TEXT_LEN 10000
#ifndef TEXT_FILE_PATH
#define TEXT_FILE_PATH "text.txt"
#endif

// Визначаємо константи для текстової області та відображення
#define TEXT_AREA_X 10
#define TEXT_AREA_PADDING_Y 10 // Відступ зверху та знизу для текстової області
#define TEXT_AREA_W (WINDOW_W - 20)
#define DISPLAY_LINES 3 // Кількість рядків для одночасного відображення
#define CURSOR_TARGET_VIEWPORT_LINE 1 // Цільовий рядок для курсора (0-індексований) у видимому вікні

enum { COL_BG, COL_TEXT, COL_CORRECT, COL_INCORRECT, COL_CURSOR, N_COLORS };

int main(int argc, char **argv) {
    // 1) Load text
    const char *path = (argc > 1 ? argv[1] : TEXT_FILE_PATH);
    FILE *f = fopen(path, "r");
    if (!f) { perror("Failed to open text file"); return 1; }
    char *text = malloc(MAX_TEXT_LEN);
    if (!text) { perror("Failed to allocate memory for text"); fclose(f); return 1; }
    size_t text_len = fread(text, 1, MAX_TEXT_LEN - 1, f);
    fclose(f);
    while (text_len > 0 && (text[text_len - 1] == '\n' || text[text_len - 1] == '\r')) text_len--; // Видаляємо кінцеві переноси рядків
    text[text_len] = '\0';

    // 2) Init SDL + TTF
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        free(text);
        return 1;
    }
    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init error: %s\n", TTF_GetError());
        SDL_Quit();
        free(text);
        return 1;
    }
    TTF_SetFontHinting(NULL, TTF_HINTING_LIGHT);

    // 3) Window & renderer with High-DPI support
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");
    SDL_Window *win = SDL_CreateWindow(
        "TypingApp Monkeytype-like",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_W, WINDOW_H,
        SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI
    );
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow error: %s\n", SDL_GetError());
        TTF_Quit(); SDL_Quit(); free(text); return 1;
    }
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) {
        fprintf(stderr, "SDL_CreateRenderer error: %s\n", SDL_GetError());
        SDL_DestroyWindow(win); TTF_Quit(); SDL_Quit(); free(text); return 1;
    }


    // Adjust for DPI
    int physW, physH;
    SDL_GetRendererOutputSize(ren, &physW, &physH);
    float scale_x = (float)physW / WINDOW_W;
    float scale_y = (float)physH / WINDOW_H;
    SDL_RenderSetScale(ren, scale_x, scale_y);

    // 4) Load font with DPI
#if SDL_TTF_VERSION_ATLEAST(2,20,0)
    TTF_Font *font = TTF_OpenFontDPI(
        "/System/Library/Fonts/Arial.ttf", // Шлях до шрифту для macOS
        FONT_SIZE,
        (int)(72 * scale_x),
        (int)(72 * scale_y)
    );
#else
    TTF_Font *font = TTF_OpenFont("/System/Library/Fonts/Arial.ttf", FONT_SIZE);
#endif
    if (!font) font = TTF_OpenFont("/Library/Fonts/Arial Unicode.ttf", FONT_SIZE); // Ще один варіант для macOS
    if (!font) font = TTF_OpenFont("Arial.ttf", FONT_SIZE); // Спробувати локальний файл
    if (!font) font = TTF_OpenFont("/usr/share/fonts/truetype/msttcorefonts/Arial.ttf", FONT_SIZE); // Для Linux (якщо встановлено)
    if (!font) font = TTF_OpenFont("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", FONT_SIZE); // Альтернатива для Linux

    if (!font) {
        fprintf(stderr, "Failed to load font: %s\n", TTF_GetError());
        SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); TTF_Quit(); SDL_Quit(); free(text);
        return 1;
    }

    // 5) Palette & glyph cache + metrics
    SDL_Color palette[N_COLORS] = {
        {50, 52, 55, 255},   // background
        {100,102,105,255},   // to-type
        {201,200,190,255},   // correct
        {200,  0,  0,255},   // incorrect
        {255,200,  0,255},   // cursor
    };

    int ascent = TTF_FontAscent(font);
    int descent = TTF_FontDescent(font); // descent is negative or zero
    int line_h = ascent - descent; // TTF_FontLineSkip(font) might be better for line spacing

    SDL_Texture *glyph_tex[N_COLORS][128] = {{NULL}};
    int glyph_adv[128], glyph_w[N_COLORS][128], glyph_h[N_COLORS][128];
    for (int c = 32; c < 127; c++) {
        int minx, maxx, miny, maxy, adv;
        if (TTF_GlyphMetrics(font, c, &minx, &maxx, &miny, &maxy, &adv) != 0) {
            // Error getting metrics, skip or use default
            glyph_adv[c] = FONT_SIZE / 2; // приблизно
        } else {
            glyph_adv[c] = adv;
        }

        for (int col = COL_TEXT; col <= COL_INCORRECT; col++) {
            SDL_Surface *surf = TTF_RenderGlyph_Blended(font, c, palette[col]);
            if (!surf) {
                 fprintf(stderr, "Failed to render glyph for char %c: %s\n", c, TTF_GetError());
                 // Можна створити "запасний" гліф або пропустити
                 continue;
            }
            glyph_w[col][c] = surf->w;
            glyph_h[col][c] = surf->h;
            glyph_tex[col][c] = SDL_CreateTextureFromSurface(ren, surf);
            SDL_FreeSurface(surf);
            if (!glyph_tex[col][c]) {
                 fprintf(stderr, "Failed to create texture for glyph %c\n", c);
            }
        }
    }
    // 6) НЕМАЄ попереднього рендерингу фонового тексту (tex_bg видалено)

    // 7) Input state
    char *input = calloc(text_len + 1, 1);
    if (!input && text_len > 0) {
        perror("Failed to allocate memory for input");
        // ... (cleanup font, SDL, etc.)
        free(text); return 1;
    }
    size_t idx = 0;
    Uint32 start_time = 0; // Ініціалізуємо тут, встановимо при першому введенні
    bool typing_started = false;
    SDL_StartTextInput();
    bool show_cursor = true;
    Uint32 last_blink_time = SDL_GetTicks();

    // Висота таймера (приблизна, для розрахунку текстової області)
    int timer_text_h = line_h; // Припускаємо, що висота тексту таймера схожа на висоту рядка

    while (1) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) goto cleanup;
            if (ev.type == SDL_KEYDOWN) {
                if (ev.key.keysym.sym == SDLK_ESCAPE) goto cleanup;
                if (ev.key.keysym.sym == SDLK_BACKSPACE && idx > 0) {
                    if (!typing_started && text_len > 0) { // Початок набору з Backspace не рахується
                        // нічого не робимо для таймера
                    }
                    input[--idx] = '\0';
                }
            }
            if (ev.type == SDL_TEXTINPUT && idx < text_len) {
                if (!typing_started && text_len > 0) {
                    start_time = SDL_GetTicks();
                    typing_started = true;
                }
                // Ігноруємо введення пробілу, якщо попередній символ вже пробіл (запобігання подвійним пробілам у вводі)
                // Або якщо це перший символ і він пробіл (Monkeytype зазвичай не дозволяє починати з пробілу на порожньому місці)
                // Для простоти, дозволимо введення будь-якого символу з ev.text.text[0]
                if (!(idx > 0 && input[idx-1] == ' ' && ev.text.text[0] == ' ') &&
                    !(idx == 0 && ev.text.text[0] == ' ' && text[0] != ' ')) { // Не дозволяти вводити пробіл на початку, якщо текст не починається з пробілу
                     strncpy(&input[idx], ev.text.text, text_len - idx); // Копіюємо, щоб уникнути переповнення
                     input[idx] = ev.text.text[0]; // Гарантуємо один символ
                     idx++;
                }
            }
        }
        // cursor blink
        if (SDL_GetTicks() - last_blink_time > 500) {
            show_cursor = !show_cursor;
            last_blink_time = SDL_GetTicks();
        }
        // clear
        SDL_SetRenderDrawColor(ren, palette[COL_BG].r, palette[COL_BG].g, palette[COL_BG].b, palette[COL_BG].a);
        SDL_RenderClear(ren);

        // timer
        Uint32 elapsed_ms = 0;
        if (typing_started) {
            elapsed_ms = SDL_GetTicks() - start_time;
        }
        Uint32 elapsed_s = elapsed_ms / 1000;
        int m = elapsed_s / 60, s = elapsed_s % 60;
        char timer_buf[16];
        snprintf(timer_buf, sizeof(timer_buf), "%02d:%02d", m, s);

        SDL_Surface *timer_surf = TTF_RenderText_Blended(font, timer_buf, palette[COL_CURSOR]);
        int current_timer_w = 0, current_timer_h = 0;
        if (timer_surf) {
            SDL_Texture *timer_tex = SDL_CreateTextureFromSurface(ren, timer_surf);
            current_timer_w = timer_surf->w;
            current_timer_h = timer_surf->h; // Отримуємо актуальну висоту таймера
            SDL_Rect rtimer = { TEXT_AREA_X, TEXT_AREA_PADDING_Y, current_timer_w, current_timer_h };
            if (timer_tex) SDL_RenderCopy(ren, timer_tex, NULL, &rtimer);
            SDL_FreeSurface(timer_surf);
            SDL_DestroyTexture(timer_tex);
        } else { // Fallback, if timer surface fails
             current_timer_h = timer_text_h; // Use estimate
        }


        // --- Логіка рендерингу тексту та прокрутки ---
        int text_viewport_top_y = TEXT_AREA_PADDING_Y + current_timer_h + TEXT_AREA_PADDING_Y;

        // 1. Розрахунок абсолютної позиції курсора (рядок та x) для визначення прокрутки
        int cursor_abs_y_start_of_line = 0; // Абсолютна Y-координата початку рядка курсора (відносно 0)
        int cursor_char_abs_x = TEXT_AREA_X;   // Абсолютна X-координата символу під курсором

        int layout_x = TEXT_AREA_X;
        int current_line_abs_y = 0; // Y-координата поточного рядка при розкладці

        for (size_t j = 0; j <= text_len; ++j) { // <= для позиції курсора в кінці тексту
            if (j == idx) {
                cursor_char_abs_x = layout_x;
                cursor_abs_y_start_of_line = current_line_abs_y;
                break;
            }
             if (j == text_len) break; // Якщо idx > text_len (не повинно траплятися)

            unsigned char current_char_for_layout = (unsigned char)text[j];
            if (current_char_for_layout == '\0') break; // Кінець тексту

            if (current_char_for_layout == '\n') {
                current_line_abs_y += line_h;
                layout_x = TEXT_AREA_X;
                continue;
            }
            // Перевірка, чи символ є в кеші (ASCII 32-126)
            int char_w_layout = (current_char_for_layout >= 32 && current_char_for_layout < 127) ? glyph_adv[current_char_for_layout] : (FONT_SIZE / 2);

            if (layout_x + char_w_layout > TEXT_AREA_X + TEXT_AREA_W && layout_x != TEXT_AREA_X) { // Перенос слова/символу
                current_line_abs_y += line_h;
                layout_x = TEXT_AREA_X;
            }
            layout_x += char_w_layout;
        }

        // 2. Визначення зсуву прокрутки (scroll_offset_y)
        int target_cursor_line_on_screen_y = text_viewport_top_y + (CURSOR_TARGET_VIEWPORT_LINE * line_h);
        int scroll_offset_y = cursor_abs_y_start_of_line - (target_cursor_line_on_screen_y - text_viewport_top_y);
        if (scroll_offset_y < 0) {
            scroll_offset_y = 0;
        }

        // 3. Рендеринг видимих рядків тексту
        int pen_x = TEXT_AREA_X;
        int current_render_line_abs_y = 0; // Абсолютна Y поточного рядка, що рендериться
        int pen_y_on_screen = text_viewport_top_y - scroll_offset_y; // Початкова екранна Y для першого рядка тексту

        int final_cursor_draw_x = 0;
        int final_cursor_draw_y_baseline = 0;


        for (size_t i = 0; i < text_len; ++i) {
            unsigned char ch_to_render = (unsigned char)text[i];
            if (ch_to_render == '\0') break;

            // Позиція курсора, якщо він тут
            if (i == idx) {
                final_cursor_draw_x = pen_x;
                final_cursor_draw_y_baseline = pen_y_on_screen;
            }

            if (ch_to_render == '\n') {
                current_render_line_abs_y += line_h;
                pen_y_on_screen = text_viewport_top_y + current_render_line_abs_y - scroll_offset_y;
                pen_x = TEXT_AREA_X;
                continue;
            }

            // Перевірка, чи символ є в кеші (ASCII 32-126)
            bool is_renderable_char = (ch_to_render >= 32 && ch_to_render < 127);
            int char_w_render = is_renderable_char ? glyph_adv[ch_to_render] : (FONT_SIZE / 2);


            if (pen_x + char_w_render > TEXT_AREA_X + TEXT_AREA_W && pen_x != TEXT_AREA_X) { // Перенос
                current_render_line_abs_y += line_h;
                pen_y_on_screen = text_viewport_top_y + current_render_line_abs_y - scroll_offset_y;
                pen_x = TEXT_AREA_X;
                // Якщо курсор мав бути на початку цього перенесеного рядка
                if (i == idx) {
                    final_cursor_draw_x = pen_x;
                    final_cursor_draw_y_baseline = pen_y_on_screen;
                }
            }

            // Рендеринг символу, якщо він у видимій зоні
            if (pen_y_on_screen + line_h >= text_viewport_top_y &&
                pen_y_on_screen < text_viewport_top_y + (DISPLAY_LINES * line_h) + line_h / 2) { // + line_h/2 для частково видимих рядків
                if (is_renderable_char) {
                    int col_idx;
                    if (i < idx) { // Набраний символ
                        col_idx = (input[i] == text[i] ? COL_CORRECT : COL_INCORRECT);
                    } else { // Символ, що очікує набору
                        col_idx = COL_TEXT;
                    }

                    if (glyph_tex[col_idx][ch_to_render]) { // Перевірка, чи текстура існує
                        SDL_Rect dst = {
                            pen_x,
                            pen_y_on_screen + (line_h - glyph_h[col_idx][ch_to_render]) / 2,
                            glyph_w[col_idx][ch_to_render],
                            glyph_h[col_idx][ch_to_render]
                        };
                        SDL_RenderCopy(ren, glyph_tex[col_idx][ch_to_render], NULL, &dst);
                    }
                }
            }
            pen_x += char_w_render;
        }

        // Якщо курсор в кінці тексту
        if (idx == text_len) {
            final_cursor_draw_x = pen_x;
            final_cursor_draw_y_baseline = pen_y_on_screen;
             // Якщо текст закінчується на \n, курсор має бути на новому рядку
            if (text_len > 0 && text[text_len - 1] == '\n') {
                final_cursor_draw_x = TEXT_AREA_X;
                current_render_line_abs_y += line_h; // Наступний рядок після \n
                final_cursor_draw_y_baseline = text_viewport_top_y + current_render_line_abs_y - scroll_offset_y;
            }
        }

        // Draw cursor
        if (show_cursor && idx <= text_len) { // Дозволяємо курсор після останнього символу
            if (final_cursor_draw_y_baseline + line_h >= text_viewport_top_y &&
                final_cursor_draw_y_baseline < text_viewport_top_y + (DISPLAY_LINES * line_h) + line_h / 2) {
                SDL_Rect cur_rect = { final_cursor_draw_x, final_cursor_draw_y_baseline, 2, line_h };
                SDL_SetRenderDrawColor(ren, palette[COL_CURSOR].r, palette[COL_CURSOR].g, palette[COL_CURSOR].b, palette[COL_CURSOR].a);
                SDL_RenderFillRect(ren, &cur_rect);
            }
        }

        SDL_RenderPresent(ren);
        SDL_Delay(16); // ~60 FPS
    }

cleanup:
    SDL_StopTextInput();
    double total_s = 0;
    size_t correct_chars = 0;
    if (typing_started) {
        total_s = (SDL_GetTicks() - start_time) / 1000.0;
        for(size_t i=0; i<idx; ++i) {
            if(input[i] == text[i]) {
                correct_chars++;
            }
        }
    }

    double wpm = 0;
    if (total_s > 0) {
        wpm = ((double)correct_chars / 5.0) / (total_s / 60.0);
    }
    double accuracy = (idx > 0) ? (double)correct_chars / idx * 100.0 : 0.0;

    printf("Typed: %zu chars\n", idx);
    printf("Correct: %zu chars\n", correct_chars);
    printf("Accuracy: %.2f%%\n", accuracy);
    printf("Time: %.2fs\n", total_s);
    printf("WPM: %.2f\n", wpm);

    for (int c = 32; c < 127; c++) {
        for (int col = COL_TEXT; col <= COL_INCORRECT; col++) {
            if (glyph_tex[col][c]) SDL_DestroyTexture(glyph_tex[col][c]);
        }
    }
    // SDL_DestroyTexture(tex_bg); // tex_bg більше не існує

    if (font) TTF_CloseFont(font);
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    TTF_Quit();
    SDL_Quit();
    if (text) free(text);
    if (input) free(input);
    return 0;
}