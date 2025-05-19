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
#define WINDOW_H     200 // Висота вікна, можна налаштувати
#define FONT_SIZE    14
#define MAX_TEXT_LEN 10000
#ifndef TEXT_FILE_PATH
#define TEXT_FILE_PATH "text.txt"
#endif

// Константи для текстової області та відображення
#define TEXT_AREA_X 10
#define TEXT_AREA_PADDING_Y 10
#define TEXT_AREA_W (WINDOW_W - (2 * TEXT_AREA_X)) // Ширина текстової області
#define DISPLAY_LINES 3 // Кількість рядків для одночасного відображення
#define CURSOR_TARGET_VIEWPORT_LINE 1 // Цільовий рядок для курсора (0-індексований) у видимому вікні
#define TAB_SIZE_IN_SPACES 4 // Кількість пробілів для одного символу табуляції

enum { COL_BG, COL_TEXT, COL_CORRECT, COL_INCORRECT, COL_CURSOR, N_COLORS };

int main(int argc, char **argv) {
    // 1) Load text
    const char *path = (argc > 1 ? argv[1] : TEXT_FILE_PATH);
    FILE *f = fopen(path, "r");
    if (!f) {
        perror("Failed to open text file");
        fprintf(stderr, "Attempted to open: %s\n", path);
        return 1;
    }
    char *text = malloc(MAX_TEXT_LEN);
    if (!text) { perror("Failed to allocate memory for text"); fclose(f); return 1; }
    size_t text_len = fread(text, 1, MAX_TEXT_LEN - 1, f);
    fclose(f);
    text[text_len] = '\0'; // Забезпечуємо нуль-термінацію

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
    TTF_Font *font = NULL;
    const char* font_paths[] = {
        // Спробуйте додати специфічні для вашої системи шляхи сюди, якщо стандартні не працюють
        "Arial.ttf", // Спробувати локальний файл спочатку
        "/System/Library/Fonts/Arial.ttf", // macOS
        "/Library/Fonts/Arial Unicode.ttf", // macOS fallback
        "/usr/share/fonts/truetype/msttcorefonts/Arial.ttf", // Linux (якщо встановлено MS Core Fonts)
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", // Linux (поширений шрифт)
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", // Linux (ще один поширений)
        NULL // Кінець списку
    };
    for (int i = 0; font_paths[i] != NULL; ++i) {
        #if SDL_TTF_VERSION_ATLEAST(2,20,0)
            font = TTF_OpenFontDPI(font_paths[i], FONT_SIZE, (int)(72 * scale_x), (int)(72 * scale_y));
        #else
            font = TTF_OpenFont(font_paths[i], FONT_SIZE);
        #endif
        if (font) {
            printf("Loaded font: %s\n", font_paths[i]);
            break;
        }
    }
    if (!font) {
        fprintf(stderr, "Failed to load font from any known path: %s\n", TTF_GetError());
        SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); TTF_Quit(); SDL_Quit(); free(text);
        return 1;
    }

    // 5) Palette & glyph cache + metrics
    SDL_Color palette[N_COLORS];
    palette[COL_BG]        = (SDL_Color){50, 52, 55, 255};   // background
    palette[COL_TEXT]      = (SDL_Color){100,102,105,255};   // to-type
    palette[COL_CORRECT]   = (SDL_Color){201,200,190,255};   // correct
    palette[COL_INCORRECT] = (SDL_Color){200,  0,  0,255};   // incorrect
    palette[COL_CURSOR]    = (SDL_Color){255,200,  0,255};   // cursor

    int ascent = TTF_FontAscent(font);
    int descent = TTF_FontDescent(font);
    int line_h = TTF_FontLineSkip(font); // Рекомендована висота рядка від TTF
    if (line_h <= 0) line_h = ascent - descent; // Fallback, якщо FontLineSkip неадекватний
    if (line_h <= 0) line_h = FONT_SIZE + 4; // Абсолютний fallback для висоти рядка

    SDL_Texture *glyph_tex[N_COLORS][128] = {{NULL}};
    int glyph_adv[128] = {0}; // Ширина просування гліфа
    int glyph_w[N_COLORS][128] = {{0}}; // Ширина текстури гліфа
    int glyph_h[N_COLORS][128] = {{0}}; // Висота текстури гліфа

    for (int c = 32; c < 127; c++) { // Кешуємо лише друковані ASCII символи
        int minx, maxx, miny, maxy, adv;
        if (TTF_GlyphMetrics(font, (Uint16)c, &minx, &maxx, &miny, &maxy, &adv) != 0) {
            // Якщо не вдалося отримати метрики, використовуємо приблизне значення
            glyph_adv[c] = FONT_SIZE / 2;
        } else {
            glyph_adv[c] = adv;
        }

        for (int col = COL_TEXT; col <= COL_INCORRECT; col++) { // Для COL_TEXT, COL_CORRECT, COL_INCORRECT
            SDL_Surface *surf = TTF_RenderGlyph_Blended(font, (Uint16)c, palette[col]);
            if (!surf) {
                 fprintf(stderr, "Failed to render glyph for char %c (ASCII %d): %s\n", c, c, TTF_GetError());
                 continue;
            }
            glyph_w[col][c] = surf->w;
            glyph_h[col][c] = surf->h;
            glyph_tex[col][c] = SDL_CreateTextureFromSurface(ren, surf);
            SDL_FreeSurface(surf);
            if (!glyph_tex[col][c]) {
                 fprintf(stderr, "Failed to create texture for glyph %c (ASCII %d)\n", c, c);
            }
        }
    }

    int space_advance_width = glyph_adv[' '];
    if (space_advance_width <= 0) {
        TTF_GlyphMetrics(font, ' ', NULL, NULL, NULL, NULL, &space_advance_width);
        if (space_advance_width <= 0) space_advance_width = FONT_SIZE / 3;
    }
    const int TAB_WIDTH_IN_PIXELS = TAB_SIZE_IN_SPACES * space_advance_width;


    // 7) Input state
    char *input = calloc(text_len + 1, 1);
    if (!input && text_len > 0) { // text_len > 0, бо для порожнього тексту input не потрібен
        perror("Failed to allocate memory for input");
        TTF_CloseFont(font); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); TTF_Quit(); SDL_Quit(); free(text);
        return 1;
    }
    size_t idx = 0; // Поточна позиція вводу
    Uint32 start_time = 0; // Час початку набору
    bool typing_started = false;
    SDL_StartTextInput();
    bool show_cursor = true;
    Uint32 last_blink_time = SDL_GetTicks();
    int timer_text_h_fallback = line_h; // Запасна висота для таймера


    while (1) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) goto cleanup;
            if (ev.type == SDL_KEYDOWN) {
                if (ev.key.keysym.sym == SDLK_ESCAPE) goto cleanup;
                if (ev.key.keysym.sym == SDLK_BACKSPACE && idx > 0) {
                    input[--idx] = '\0';
                }
            }
            if (ev.type == SDL_TEXTINPUT && idx < text_len) {
                if (!typing_started && text_len > 0) {
                    start_time = SDL_GetTicks();
                    typing_started = true;
                }
                // Дозволяємо введення, якщо це не подвійний пробіл поспіль
                if (!(idx > 0 && input[idx-1] == ' ' && ev.text.text[0] == ' ')) {
                     input[idx++] = ev.text.text[0]; // Беремо лише перший символ з вводу
                }
            }
        }

        // Блимання курсора
        if (SDL_GetTicks() - last_blink_time > 500) {
            show_cursor = !show_cursor;
            last_blink_time = SDL_GetTicks();
        }
        // Очищення екрану
        SDL_SetRenderDrawColor(ren, palette[COL_BG].r, palette[COL_BG].g, palette[COL_BG].b, palette[COL_BG].a);
        SDL_RenderClear(ren);

        // Таймер
        Uint32 elapsed_ms = typing_started ? (SDL_GetTicks() - start_time) : 0;
        Uint32 elapsed_s = elapsed_ms / 1000;
        int m = elapsed_s / 60, s = elapsed_s % 60;
        char timer_buf[16];
        snprintf(timer_buf, sizeof(timer_buf), "%02d:%02d", m, s);

        int current_timer_w = 0, current_timer_h = 0;
        SDL_Surface *timer_surf = TTF_RenderText_Blended(font, timer_buf, palette[COL_CURSOR]);
        if (timer_surf) {
            SDL_Texture *timer_tex = SDL_CreateTextureFromSurface(ren, timer_surf);
            current_timer_w = timer_surf->w;
            current_timer_h = timer_surf->h; // Отримуємо актуальну висоту таймера
            SDL_Rect rtimer = { TEXT_AREA_X, TEXT_AREA_PADDING_Y, current_timer_w, current_timer_h };
            if (timer_tex) {
                SDL_RenderCopy(ren, timer_tex, NULL, &rtimer);
                SDL_DestroyTexture(timer_tex);
            }
            SDL_FreeSurface(timer_surf);
        } else {
            current_timer_h = timer_text_h_fallback; // Використовуємо запасну висоту
        }
        if (current_timer_h < line_h / 2 && line_h > 0) current_timer_h = line_h; // Гарантуємо мінімальну висоту


        // --- Логіка рендерингу тексту та прокрутки ---
        // Y-координата початку видимої області тексту (після таймера та відступу)
        int text_viewport_top_y = TEXT_AREA_PADDING_Y + current_timer_h + TEXT_AREA_PADDING_Y * 2;

        // 1. Розрахунок абсолютної позиції курсора (рядок та x) для визначення прокрутки
        int cursor_abs_y_start_of_line = 0; // Абсолютна Y-координата початку рядка курсора
        int cursor_char_abs_x = TEXT_AREA_X;   // Абсолютна X-координата символу під курсором

        int layout_x = TEXT_AREA_X; // Поточна X-позиція при розрахунку розкладки
        int current_line_abs_y = 0; // Y-координата поточного рядка при розкладці (відносно 0)

        for (size_t j = 0; j <= text_len; ++j) { // <= для позиції курсора в кінці тексту
            if (j == idx) { // Знайшли позицію курсора
                cursor_char_abs_x = layout_x;
                cursor_abs_y_start_of_line = current_line_abs_y;
                break;
            }
            if (j >= text_len) break; // Досягли кінця тексту

            unsigned char current_char_for_layout = (unsigned char)text[j];
            if (current_char_for_layout == '\0') break;

            int entity_width = 0; // Ширина поточного елемента (символ або таб)

            if (current_char_for_layout == '\n') {
                current_line_abs_y += line_h;
                layout_x = TEXT_AREA_X;
                continue;
            } else if (current_char_for_layout == '\t') {
                if (TAB_WIDTH_IN_PIXELS > 0 && space_advance_width > 0) { // Перевірка, чи визначена ширина табуляції
                    int offset_in_line = layout_x - TEXT_AREA_X;
                    entity_width = TAB_WIDTH_IN_PIXELS - (offset_in_line % TAB_WIDTH_IN_PIXELS);
                    // Якщо вже на позиції табуляції, перейти до наступної
                    if (entity_width == 0 && offset_in_line >=0 ) entity_width = TAB_WIDTH_IN_PIXELS;
                    // Гарантувати позитивний зсув, якщо щось пішло не так
                    if (entity_width <=0 ) entity_width = TAB_WIDTH_IN_PIXELS;
                } else { // Якщо ширина табуляції не визначена, використовуємо ширину одного пробілу
                    entity_width = space_advance_width > 0 ? space_advance_width : FONT_SIZE / 3;
                }
            } else { // Звичайний символ
                // Використовуємо glyph_adv, якщо символ є в кеші, інакше ширину пробілу або fallback
                entity_width = (current_char_for_layout >= 32 && current_char_for_layout < 127 && glyph_adv[current_char_for_layout] > 0)
                               ? glyph_adv[current_char_for_layout]
                               : (space_advance_width > 0 ? space_advance_width : FONT_SIZE / 2);
            }

            // Перевірка на перенос рядка
            if (layout_x + entity_width > TEXT_AREA_X + TEXT_AREA_W && layout_x != TEXT_AREA_X) {
                current_line_abs_y += line_h;
                layout_x = TEXT_AREA_X;
            }
            layout_x += entity_width;
        }

        // 2. Визначення зсуву прокрутки (scroll_offset_y)
        int target_cursor_line_on_screen_y = text_viewport_top_y + (CURSOR_TARGET_VIEWPORT_LINE * line_h);
        int scroll_offset_y = cursor_abs_y_start_of_line - (target_cursor_line_on_screen_y - text_viewport_top_y);
        if (scroll_offset_y < 0) {
            scroll_offset_y = 0; // Прокрутка не може бути від'ємною
        }

        // 3. Рендеринг видимих рядків тексту
        int pen_x = TEXT_AREA_X; // Поточна X-позиція для рендерингу
        int current_render_line_abs_y = 0; // Абсолютна Y поточного рядка, що рендериться
        // Початкова екранна Y для першого рядка тексту (з урахуванням прокрутки)
        int pen_y_on_screen = text_viewport_top_y - scroll_offset_y;

        int final_cursor_draw_x = 0; // Остаточна X-координата для малювання курсора
        int final_cursor_draw_y_baseline = 0; // Остаточна Y-координата (верхня лінія) для курсора


        for (size_t i = 0; i < text_len; ++i) { // Ітеруємо по всьому тексту для розкладки
            unsigned char ch_to_render = (unsigned char)text[i];
            if (ch_to_render == '\0') break; // Кінець тексту

            // Якщо поточна позиція відповідає індексу вводу, зберігаємо координати для курсора
            if (i == idx) {
                final_cursor_draw_x = pen_x;
                final_cursor_draw_y_baseline = pen_y_on_screen;
            }

            int entity_width_render = 0; // Ширина поточного елемента для рендерингу
            bool is_tab_char = false; // Прапорець, чи є поточний символ табуляцією

            if (ch_to_render == '\n') {
                current_render_line_abs_y += line_h;
                pen_y_on_screen = text_viewport_top_y + current_render_line_abs_y - scroll_offset_y;
                pen_x = TEXT_AREA_X;
                continue; // Переходимо до наступного символу
            } else if (ch_to_render == '\t') {
                is_tab_char = true;
                if (TAB_WIDTH_IN_PIXELS > 0 && space_advance_width > 0) {
                    int offset_in_line = pen_x - TEXT_AREA_X;
                    entity_width_render = TAB_WIDTH_IN_PIXELS - (offset_in_line % TAB_WIDTH_IN_PIXELS);
                     if (entity_width_render == 0 && offset_in_line >=0) entity_width_render = TAB_WIDTH_IN_PIXELS;
                     if (entity_width_render <=0 ) entity_width_render = TAB_WIDTH_IN_PIXELS;
                } else {
                    entity_width_render = space_advance_width > 0 ? space_advance_width : FONT_SIZE / 3;
                }
            } else { // Звичайний символ
                 entity_width_render = (ch_to_render >= 32 && ch_to_render < 127 && glyph_adv[ch_to_render] > 0)
                               ? glyph_adv[ch_to_render]
                               : (space_advance_width > 0 ? space_advance_width : FONT_SIZE / 2);
            }

            // Перевірка на перенос рядка для рендерингу
            if (pen_x + entity_width_render > TEXT_AREA_X + TEXT_AREA_W && pen_x != TEXT_AREA_X) {
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
            // Умова видимості: (pen_y_on_screen + line_h) > верхня_межа_вікна && pen_y_on_screen < нижня_межа_вікна
            if (pen_y_on_screen + line_h >= text_viewport_top_y - line_h && // Показувати частково видимі рядки зверху
                pen_y_on_screen < text_viewport_top_y + (DISPLAY_LINES * line_h) + line_h) { // і знизу

                // Не рендеримо гліф для табуляції, лише просуваємо pen_x
                if (!is_tab_char && ch_to_render >= 32 && ch_to_render < 127) {
                    int col_idx;
                    if (i < idx) { // Набраний символ
                        col_idx = (input[i] == text[i] ? COL_CORRECT : COL_INCORRECT);
                    } else { // Символ, що очікує набору
                        col_idx = COL_TEXT;
                    }

                    if (glyph_tex[col_idx][ch_to_render]) { // Перевірка, чи текстура існує
                        SDL_Rect dst = {
                            pen_x,
                            // Вертикальне центрування гліфа в межах line_h
                            pen_y_on_screen + (line_h - glyph_h[col_idx][ch_to_render]) / 2,
                            glyph_w[col_idx][ch_to_render],
                            glyph_h[col_idx][ch_to_render]
                        };
                        SDL_RenderCopy(ren, glyph_tex[col_idx][ch_to_render], NULL, &dst);
                    }
                }
            }
            pen_x += entity_width_render; // Просуваємо pen_x на ширину елемента
        }

        // Якщо курсор в кінці тексту (після останнього символу)
        if (idx == text_len) {
            final_cursor_draw_x = pen_x; // X-позиція після останнього відрендереного елемента
            final_cursor_draw_y_baseline = pen_y_on_screen; // Y-позиція на тому ж рядку
            // Якщо текст закінчується на \n, курсор має бути на новому рядку
            if (text_len > 0 && text[text_len - 1] == '\n') {
                final_cursor_draw_x = TEXT_AREA_X;
                // pen_y_on_screen вже налаштований на наступний рядок після обробки останнього '\n'
                // Якщо останній \n був оброблений, current_render_line_abs_y вже збільшено.
                // final_cursor_draw_y_baseline = text_viewport_top_y + current_render_line_abs_y - scroll_offset_y;
                // Цей рядок вже має бути правильним з основного циклу, оскільки \n обробляється там
            }
        }

        // Малювання курсора
        if (show_cursor && idx <= text_len) { // Дозволяємо курсор після останнього символу
            // Перевірка, чи курсор у видимій зоні
             if (final_cursor_draw_y_baseline + line_h >= text_viewport_top_y - line_h &&
                final_cursor_draw_y_baseline < text_viewport_top_y + (DISPLAY_LINES * line_h) + line_h) {
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
        for(size_t i=0; i < idx && i < text_len; ++i) { // Додав i < text_len для безпеки
            if(input[i] == text[i]) {
                correct_chars++;
            }
        }
    }

    double wpm = 0;
    // Розраховуємо WPM тільки якщо був набраний хоча б один правильний символ і пройшов час
    if (total_s > 0.001 && correct_chars > 0) {
        wpm = ((double)correct_chars / 5.0) / (total_s / 60.0);
    }
    double accuracy = (idx > 0) ? ((double)correct_chars / idx) * 100.0 : 0.0;

    printf("Typed: %zu chars\n", idx);
    printf("Correct: %zu chars\n", correct_chars);
    printf("Accuracy: %.2f%%\n", accuracy);
    printf("Time: %.2fs\n", total_s);
    printf("WPM: %.2f\n", wpm);

    // Звільнення ресурсів
    for (int c = 32; c < 127; c++) {
        for (int col = COL_TEXT; col <= COL_INCORRECT; col++) {
            if (glyph_tex[col][c]) {
                SDL_DestroyTexture(glyph_tex[col][c]);
            }
        }
    }

    if (font) TTF_CloseFont(font);
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    TTF_Quit();
    SDL_Quit();
    if (text) free(text);
    if (input) free(input);
    return 0;
}