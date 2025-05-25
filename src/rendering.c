#include "rendering.h"
#include "text_processing.h" // Для get_codepoint_advance_and_metrics_func, TextBlockInfo, get_next_text_block_func
#include "utf8_utils.h"      // Для decode_utf8
#include "config.h"          // Для TEXT_AREA_X, TEXT_AREA_W, DISPLAY_LINES, COL_CURSOR etc.
#include <stdio.h>           // Для snprintf
#include <string.h>          // Для memcmp


// Допоміжна функція для логування, якщо appCtx->log_file_handle доступний
static void log_render_message_format(AppContext *appCtx, const char* format, ...) {
    if (appCtx && appCtx->log_file_handle && format) {
        va_list args;
        va_start(args, format);
        vfprintf(appCtx->log_file_handle, format, args);
        va_end(args);
        fprintf(appCtx->log_file_handle, "\n");
        fflush(appCtx->log_file_handle);
    }
}

void RenderAppTimer(AppContext *appCtx, int *out_timer_h, int *out_timer_w) {
    if (!appCtx || !appCtx->font || !appCtx->ren || !out_timer_h || !out_timer_w) return;

    Uint32 elapsed_ms_param;
    if (appCtx->typing_started) {
        if (appCtx->is_paused) {
            elapsed_ms_param = appCtx->time_at_pause_ms - appCtx->start_time_ms;
        } else {
            elapsed_ms_param = SDL_GetTicks() - appCtx->start_time_ms;
        }
    } else {
        elapsed_ms_param = 0;
    }

    Uint32 elapsed_s;
    char timer_buf[40]; // Буфер для рядка таймера

    if (appCtx->is_paused) {
        if (appCtx->typing_started) {
            elapsed_s = elapsed_ms_param / 1000;
            int m = (int)(elapsed_s / 60);
            int s = (int)(elapsed_s % 60);
            snprintf(timer_buf, sizeof(timer_buf)-1, "%02d:%02d (Paused)", m, s);
        } else {
            snprintf(timer_buf, sizeof(timer_buf)-1, "00:00 (Paused)");
        }
    } else {
        elapsed_s = elapsed_ms_param / 1000;
        int m = (int)(elapsed_s / 60);
        int s = (int)(elapsed_s % 60);
        snprintf(timer_buf, sizeof(timer_buf)-1, "%02d:%02d", m, s);
    }
    timer_buf[sizeof(timer_buf)-1] = '\0';


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
            log_render_message_format(appCtx, "Error: Failed to create timer texture from surface: %s", SDL_GetError());
            TTF_SizeText(appCtx->font, timer_buf, &current_timer_w_val, &current_timer_h_val); // Отримуємо розміри хоча б
            if(current_timer_h_val <=0) current_timer_h_val = appCtx->line_h;
        }
        SDL_FreeSurface(timer_surf);
    } else {
        log_render_message_format(appCtx, "Error: Failed to render timer text surface: %s", TTF_GetError());
        TTF_SizeText(appCtx->font, timer_buf, &current_timer_w_val, &current_timer_h_val); // Отримуємо розміри хоча б
        if(current_timer_h_val <=0) current_timer_h_val = appCtx->line_h;
    }

    if (current_timer_w_val <= 0) current_timer_w_val = 50; // Мінімальна ширина, щоб уникнути проблем з позиціонуванням
    *out_timer_h = current_timer_h_val > 0 ? current_timer_h_val : appCtx->line_h;
    *out_timer_w = current_timer_w_val;
}

void RenderLiveStats(AppContext *appCtx,
                     const char *input_buffer, size_t current_input_byte_idx,
                     int timer_x_pos, int timer_width, int timer_y_pos, int timer_height) {

    if (!appCtx || !appCtx->font || !appCtx->ren || !appCtx->typing_started ) return; // Статистика тільки якщо друк почався

    float elapsed_seconds;
    if (appCtx->is_paused) {
        elapsed_seconds = (float)(appCtx->time_at_pause_ms - appCtx->start_time_ms) / 1000.0f;
    } else {
        elapsed_seconds = (float)(SDL_GetTicks() - appCtx->start_time_ms) / 1000.0f;
    }

    // Запобігання діленню на нуль або занадто малий час
    if (elapsed_seconds < 0.05f && appCtx->total_keystrokes_for_accuracy > 0) elapsed_seconds = 0.05f;
    else if (elapsed_seconds < 0.001f) elapsed_seconds = 0.001f; // Мінімальний час для розрахунків

    float elapsed_minutes = elapsed_seconds / 60.0f;
    float live_accuracy = 100.0f;
    if (appCtx->total_keystrokes_for_accuracy > 0) {
        live_accuracy = ((float)(appCtx->total_keystrokes_for_accuracy - appCtx->total_errors_committed_for_accuracy) / (float)appCtx->total_keystrokes_for_accuracy) * 100.0f;
        if (live_accuracy < 0.0f) live_accuracy = 0.0f;
        if (live_accuracy > 100.0f) live_accuracy = 100.0f; // Обмеження зверху
    }

    size_t live_correct_keystrokes = (appCtx->total_keystrokes_for_accuracy >= appCtx->total_errors_committed_for_accuracy) ?
                                     (appCtx->total_keystrokes_for_accuracy - appCtx->total_errors_committed_for_accuracy) : 0;
    float live_net_words_for_wpm = (float)live_correct_keystrokes / 5.0f; // WPM базується на 5 символах на слово
    float live_wpm = (elapsed_minutes > 0.0001f) ? (live_net_words_for_wpm / elapsed_minutes) : 0.0f;
    if (live_wpm < 0.0f) live_wpm = 0.0f;

    // Підрахунок набраних слів на основі input_buffer
    int live_typed_words_count = 0;
    if (current_input_byte_idx > 0 && input_buffer) {
        bool in_word_flag = false;
        const char* p_word_scan_iter = input_buffer;
        const char* p_word_scan_end_iter = input_buffer + current_input_byte_idx;
        while(p_word_scan_iter < p_word_scan_end_iter) {
            const char* temp_p_word_start = p_word_scan_iter;
            Sint32 cp_word = decode_utf8(&p_word_scan_iter, p_word_scan_end_iter);
            if (cp_word <= 0) { // Помилка або кінець
                if (p_word_scan_iter <= temp_p_word_start && p_word_scan_iter < p_word_scan_end_iter) p_word_scan_iter++; else break;
                continue;
            }

            bool is_current_char_separator = (cp_word == ' ' || cp_word == '\n' || cp_word == '\t');

            if (!is_current_char_separator) { // Якщо це не роздільник
                if (!in_word_flag) { // І ми ще не в слові
                    live_typed_words_count++; // Починаємо нове слово
                    in_word_flag = true;
                }
            } else { // Якщо це роздільник
                in_word_flag = false; // Закінчуємо слово
            }
        }
    }

    char wpm_buf[32], acc_buf[32], words_buf[32];
    snprintf(wpm_buf, sizeof(wpm_buf)-1, "WPM: %.0f", live_wpm); wpm_buf[sizeof(wpm_buf)-1] = '\0';
    snprintf(acc_buf, sizeof(acc_buf)-1, "Acc: %.0f%%", live_accuracy); acc_buf[sizeof(acc_buf)-1] = '\0';
    snprintf(words_buf, sizeof(words_buf)-1, "Words: %d", live_typed_words_count); words_buf[sizeof(words_buf)-1] = '\0';

    SDL_Color stat_color = appCtx->palette[COL_TEXT]; // Колір для статистики
    int current_x_render_pos = timer_x_pos + timer_width + 20; // Позиція X після таймера
    // Вертикальне вирівнювання статистики по центру відносно таймера
    int stats_y_render_pos = timer_y_pos + (timer_height - appCtx->line_h) / 2;
    if (stats_y_render_pos < TEXT_AREA_PADDING_Y) stats_y_render_pos = TEXT_AREA_PADDING_Y; // Не вище верхнього паддінгу

    SDL_Surface *surf;
    SDL_Texture *tex;
    SDL_Rect dst;

    // Рендеринг WPM
    surf = TTF_RenderText_Blended(appCtx->font, wpm_buf, stat_color);
    if (surf) {
        tex = SDL_CreateTextureFromSurface(appCtx->ren, surf);
        if (tex) {
            dst = (SDL_Rect){current_x_render_pos, stats_y_render_pos, surf->w, surf->h};
            SDL_RenderCopy(appCtx->ren, tex, NULL, &dst);
            current_x_render_pos += surf->w + 15; // Відступ для наступного елемента
            SDL_DestroyTexture(tex);
        } else { log_render_message_format(appCtx, "Error creating WPM texture: %s", SDL_GetError()); }
        SDL_FreeSurface(surf);
    } else { log_render_message_format(appCtx, "Error rendering WPM surface: %s", TTF_GetError()); }

    // Рендеринг Accuracy
    surf = TTF_RenderText_Blended(appCtx->font, acc_buf, stat_color);
    if (surf) {
        tex = SDL_CreateTextureFromSurface(appCtx->ren, surf);
        if (tex) {
            dst = (SDL_Rect){current_x_render_pos, stats_y_render_pos, surf->w, surf->h};
            SDL_RenderCopy(appCtx->ren, tex, NULL, &dst);
            current_x_render_pos += surf->w + 15;
            SDL_DestroyTexture(tex);
        } else { log_render_message_format(appCtx, "Error creating Accuracy texture: %s", SDL_GetError()); }
        SDL_FreeSurface(surf);
    } else { log_render_message_format(appCtx, "Error rendering Accuracy surface: %s", TTF_GetError()); }

    // Рендеринг Words
    surf = TTF_RenderText_Blended(appCtx->font, words_buf, stat_color);
    if (surf) {
        tex = SDL_CreateTextureFromSurface(appCtx->ren, surf);
        if (tex) {
            dst = (SDL_Rect){current_x_render_pos, stats_y_render_pos, surf->w, surf->h};
            SDL_RenderCopy(appCtx->ren, tex, NULL, &dst);
            // current_x_render_pos += surf->w + 15; // Не потрібно, якщо це останній елемент
            SDL_DestroyTexture(tex);
        } else { log_render_message_format(appCtx, "Error creating Words texture: %s", SDL_GetError()); }
        SDL_FreeSurface(surf);
    } else { log_render_message_format(appCtx, "Error rendering Words surface: %s", TTF_GetError()); }
}


void RenderTextContent(AppContext *appCtx, const char *text_to_type, size_t final_text_len,
                       const char *input_buffer, size_t current_input_byte_idx,
                       int text_viewport_top_y, // Y координата верхньої частини текстового поля
                       int *out_final_cursor_draw_x, int *out_final_cursor_draw_y_baseline) {

    if (!appCtx || !text_to_type || !input_buffer || !out_final_cursor_draw_x || !out_final_cursor_draw_y_baseline || !appCtx->font || appCtx->line_h <=0) {
        if (out_final_cursor_draw_x) *out_final_cursor_draw_x = -100; // Поза екраном
        if (out_final_cursor_draw_y_baseline) *out_final_cursor_draw_y_baseline = -100;
        return;
    }

    int render_pen_x = TEXT_AREA_X; // Поточна X позиція для рендерингу на рядку
    int render_current_abs_line_num = 0; // Абсолютний номер поточного рядка, що рендериться
    const char *p_render_iter = text_to_type; // Ітератор по тексту для рендерингу
    const char *p_text_end_for_render = text_to_type + final_text_len;

    // Ініціалізація вихідних позицій курсора (на випадок, якщо він не буде знайдений явно)
    *out_final_cursor_draw_x = -100;
    *out_final_cursor_draw_y_baseline = -100;

    // Обробка випадку, коли курсор на самому початку тексту
    if (current_input_byte_idx == 0) {
        int relative_line_idx_for_cursor_at_start = 0 - appCtx->first_visible_abs_line_num;
        if (relative_line_idx_for_cursor_at_start >=0 && relative_line_idx_for_cursor_at_start < DISPLAY_LINES) {
            *out_final_cursor_draw_x = TEXT_AREA_X;
            *out_final_cursor_draw_y_baseline = text_viewport_top_y + relative_line_idx_for_cursor_at_start * appCtx->line_h;
        }
    }

    while(p_render_iter < p_text_end_for_render) {
        // Визначаємо, чи поточний рядок для рендерингу знаходиться у видимому в'юпорті
        int current_viewport_line_idx = render_current_abs_line_num - appCtx->first_visible_abs_line_num;
        if (current_viewport_line_idx >= DISPLAY_LINES) break; // Вийшли за межі видимих рядків

        size_t block_start_byte_offset_in_doc = (size_t)(p_render_iter - text_to_type);
        TextBlockInfo block = get_next_text_block_func(appCtx, &p_render_iter, p_text_end_for_render, render_pen_x);

        if (block.num_bytes == 0 && p_render_iter >= p_text_end_for_render) break; // Кінець тексту
        if (block.num_bytes == 0 || !block.start_ptr) { // Пропускаємо порожні або невалідні блоки
            if(p_render_iter < p_text_end_for_render) p_render_iter++; else break;
            continue;
        }

        // Y координата базової лінії для поточного рядка на екрані
        int line_on_screen_y_baseline = text_viewport_top_y + current_viewport_line_idx * appCtx->line_h;

        // Якщо початок цього блоку - це позиція курсора
        if (block_start_byte_offset_in_doc == current_input_byte_idx &&
            current_viewport_line_idx >=0 && current_viewport_line_idx < DISPLAY_LINES ) {
            *out_final_cursor_draw_x = render_pen_x;
            *out_final_cursor_draw_y_baseline = line_on_screen_y_baseline;
        }

        if (block.is_newline) {
            render_current_abs_line_num++;
            render_pen_x = TEXT_AREA_X; // Перехід на новий рядок
        } else {
            int x_block_starts_on_this_line = render_pen_x;
            int y_baseline_for_block_content = line_on_screen_y_baseline;
            bool wrap_this_block = false; // Чи потрібно переносити цей блок

            // Логіка переносу слів (word wrap)
            if (render_pen_x != TEXT_AREA_X && block.is_word) { // Тільки для слів, не для пробілів на початку рядка
                if (render_pen_x + block.pixel_width > TEXT_AREA_X + TEXT_AREA_W) {
                    wrap_this_block = true;
                } else {
                    // "Заглядаємо" на наступний блок, щоб перевірити, чи не призведе він до переносу цього слова
                    // (наприклад, якщо слово поміщається, але наступний пробіл - ні)
                    const char *p_peek_next_char_after_block = p_render_iter; // p_render_iter вже вказує на наступний блок
                    if (p_peek_next_char_after_block < p_text_end_for_render) {
                        const char *temp_peek_iter = p_peek_next_char_after_block; // Копія для get_next_text_block_func
                        int pen_x_after_this_block = render_pen_x + block.pixel_width;
                        TextBlockInfo next_block_after_this = get_next_text_block_func(appCtx, &temp_peek_iter, p_text_end_for_render, pen_x_after_this_block);

                        if (next_block_after_this.num_bytes > 0 && !next_block_after_this.is_word &&
                            !next_block_after_this.is_newline && !next_block_after_this.is_tab) { // Якщо наступний блок - це пробіли
                            const char* space_block_char_ptr = next_block_after_this.start_ptr;
                            Sint32 cp_space_in_next_block = decode_utf8(&space_block_char_ptr, next_block_after_this.start_ptr + next_block_after_this.num_bytes);
                            if (cp_space_in_next_block == ' ') { // Якщо це дійсно пробіл
                                int space_char_width = get_codepoint_advance_and_metrics_func(appCtx, (Uint32)cp_space_in_next_block, appCtx->space_advance_width,NULL,NULL);
                                if (space_char_width > 0 && (pen_x_after_this_block + space_char_width > TEXT_AREA_X + TEXT_AREA_W)) {
                                    wrap_this_block = true; // Переносимо поточне слово, щоб пробіл не висів
                                }
                            }
                        }
                    }
                }
            }

            if (wrap_this_block) {
                render_current_abs_line_num++;
                current_viewport_line_idx = render_current_abs_line_num - appCtx->first_visible_abs_line_num;
                if (current_viewport_line_idx >= DISPLAY_LINES) break; // Вийшли за межі

                y_baseline_for_block_content = text_viewport_top_y + current_viewport_line_idx * appCtx->line_h;
                render_pen_x = TEXT_AREA_X; // Починаємо зліва
                x_block_starts_on_this_line = render_pen_x;

                // Оновлення позиції курсора, якщо він на початку цього перенесеного блоку
                if (block_start_byte_offset_in_doc == current_input_byte_idx &&
                    current_viewport_line_idx >=0 && current_viewport_line_idx < DISPLAY_LINES) {
                    *out_final_cursor_draw_x = render_pen_x;
                    *out_final_cursor_draw_y_baseline = y_baseline_for_block_content;
                }
            }

            // Рендеринг блоку, якщо він у видимому в'юпорті
            if (current_viewport_line_idx >= 0 && current_viewport_line_idx < DISPLAY_LINES) {
                if (!block.is_tab) { // Табуляції обробляються просуванням пера, не рендерингом символів
                    const char *p_char_in_block = block.start_ptr;
                    const char *p_char_end_in_block = block.start_ptr + block.num_bytes;
                    size_t char_offset_within_block = 0; // Зміщення поточного символу всередині блоку
                    int char_render_px = x_block_starts_on_this_line;
                    int char_render_py_baseline = y_baseline_for_block_content;
                    int char_current_abs_line_num_for_render = render_current_abs_line_num; // Абсолютний рядок для поточного символу

                    while(p_char_in_block < p_char_end_in_block) {
                        int char_current_viewport_line_for_render = char_current_abs_line_num_for_render - appCtx->first_visible_abs_line_num;
                        if (char_current_viewport_line_for_render >= DISPLAY_LINES) goto end_char_loop_render_local; // Вихід з внутрішнього циклу

                        const char* glyph_start_ptr_in_block = p_char_in_block;
                        Sint32 cp_to_render = decode_utf8(&p_char_in_block, p_char_end_in_block);
                        size_t glyph_byte_len = (size_t)(p_char_in_block - glyph_start_ptr_in_block);

                        if (cp_to_render <= 0 || glyph_byte_len == 0) { // Пропускаємо невалідні
                            if (p_char_in_block <= glyph_start_ptr_in_block && p_char_in_block < p_char_end_in_block) p_char_in_block++; else break;
                            continue;
                        }

                        // Перевірка, чи поточний символ - це позиція курсора
                        size_t char_absolute_byte_pos_in_doc = block_start_byte_offset_in_doc + char_offset_within_block;
                        if (char_absolute_byte_pos_in_doc == current_input_byte_idx &&
                            char_current_viewport_line_for_render >= 0 && char_current_viewport_line_for_render < DISPLAY_LINES) {
                            *out_final_cursor_draw_x = char_render_px;
                            *out_final_cursor_draw_y_baseline = char_render_py_baseline;
                        }

                        int glyph_w_metric=0, glyph_h_metric=0;
                        int advance = get_codepoint_advance_and_metrics_func(appCtx, (Uint32)cp_to_render, appCtx->space_advance_width, &glyph_w_metric, &glyph_h_metric);

                        // Перенос всередині блоку (дуже довге слово без пробілів)
                        if (char_render_px + advance > TEXT_AREA_X + TEXT_AREA_W && char_render_px != TEXT_AREA_X ) {
                            char_current_abs_line_num_for_render++;
                            char_current_viewport_line_for_render = char_current_abs_line_num_for_render - appCtx->first_visible_abs_line_num;
                            if (char_current_viewport_line_for_render >= DISPLAY_LINES) goto end_char_loop_render_local;

                            char_render_py_baseline = text_viewport_top_y + char_current_viewport_line_for_render * appCtx->line_h;
                            char_render_px = TEXT_AREA_X;

                            // Оновлення позиції курсора, якщо він на початку перенесеної частини
                            if (char_absolute_byte_pos_in_doc == current_input_byte_idx &&
                                char_current_viewport_line_for_render >=0 && char_current_viewport_line_for_render < DISPLAY_LINES) {
                                *out_final_cursor_draw_x = char_render_px;
                                *out_final_cursor_draw_y_baseline = char_render_py_baseline;
                            }
                        }

                        // Визначення кольору символу
                        SDL_Color render_color;
                        bool char_is_typed = char_absolute_byte_pos_in_doc < current_input_byte_idx;
                        bool char_is_correct = false;
                        if(char_is_typed){
                            // Перевіряємо, чи весь UTF-8 символ був введений і чи він правильний
                            if(char_absolute_byte_pos_in_doc + glyph_byte_len <= current_input_byte_idx) { // Якщо символ повністю введений
                                char_is_correct = (memcmp(glyph_start_ptr_in_block, input_buffer + char_absolute_byte_pos_in_doc, glyph_byte_len) == 0);
                            } // Якщо символ введений частково, він вважається неправильним
                            render_color = char_is_correct ? appCtx->palette[COL_CORRECT] : appCtx->palette[COL_INCORRECT];
                        } else {
                            render_color = appCtx->palette[COL_TEXT];
                        }

                        // Рендеринг символу, якщо він видимий
                        if(cp_to_render >= 32){ // Рендеримо тільки друковані символи
                            SDL_Texture* tex_to_render =NULL;
                            bool use_otf_render = false; // Чи використовується рендеринг "на льоту" (не з кешу)

                            if(cp_to_render < 128){ // Спробувати взяти з кешу для ASCII
                                int cache_color_idx = char_is_typed ? (char_is_correct ? COL_CORRECT : COL_INCORRECT) : COL_TEXT;
                                tex_to_render = appCtx->glyph_tex_cache[cache_color_idx][(int)cp_to_render];
                                if(tex_to_render){ // Якщо є в кеші, використовуємо кешовані розміри
                                    glyph_w_metric = appCtx->glyph_w_cache[cache_color_idx][(int)cp_to_render];
                                    glyph_h_metric = appCtx->glyph_h_cache[cache_color_idx][(int)cp_to_render];
                                }
                            }

                            if(!tex_to_render && appCtx->font){ // Якщо не в кеші або не ASCII, рендеримо "на льоту"
                                SDL_Surface* surf_otf = TTF_RenderGlyph32_Blended(appCtx->font, (Uint32)cp_to_render, render_color);
                                if(surf_otf){
                                    tex_to_render = SDL_CreateTextureFromSurface(appCtx->ren, surf_otf);
                                    if(tex_to_render){
                                        glyph_w_metric = surf_otf->w;
                                        glyph_h_metric = surf_otf->h;
                                    } else { log_render_message_format(appCtx, "RenderTextContent: OTF Tex Error for U+%04X: %s", cp_to_render, SDL_GetError()); }
                                    SDL_FreeSurface(surf_otf);
                                    use_otf_render = true; // Цю текстуру потрібно буде звільнити
                                } else { log_render_message_format(appCtx, "RenderTextContent: OTF Surf Error for U+%04X: %s", cp_to_render, TTF_GetError()); }
                            }

                            if(tex_to_render){
                                // Забезпечення валідних розмірів для рендерингу
                                if(glyph_w_metric == 0 && advance > 0) glyph_w_metric = advance;
                                if(glyph_h_metric == 0) glyph_h_metric = appCtx->line_h;

                                // Вертикальне центрування гліфа відносно line_h
                                int y_offset_for_glyph = (appCtx->line_h > glyph_h_metric) ? (appCtx->line_h - glyph_h_metric) / 2 : 0;
                                SDL_Rect dst_rect = {char_render_px, char_render_py_baseline + y_offset_for_glyph, glyph_w_metric, glyph_h_metric};
                                SDL_RenderCopy(appCtx->ren, tex_to_render, NULL, &dst_rect);
                                if(use_otf_render) SDL_DestroyTexture(tex_to_render); // Звільняємо текстуру, створену "на льоту"
                            }
                        }
                        char_render_px += advance; // Просуваємо перо
                        char_offset_within_block += glyph_byte_len; // Просуваємо зміщення в байтах
                    }
                    end_char_loop_render_local:; // Мітка для виходу з внутрішнього циклу
                    render_pen_x = char_render_px; // Оновлюємо X позицію для наступного блоку
                    render_current_abs_line_num = char_current_abs_line_num_for_render; // Оновлюємо номер рядка
                } else { // Це табуляція
                    render_pen_x += block.pixel_width; // Просто просуваємо перо
                }
            } else { // Блок не у видимому в'юпорті, але потрібно просунути перо
                render_pen_x += block.pixel_width;
            }
        }

        // Якщо кінець цього блоку - це позиція курсора
        if (block_start_byte_offset_in_doc + block.num_bytes == current_input_byte_idx) {
            int final_block_viewport_line = render_current_abs_line_num - appCtx->first_visible_abs_line_num;
            if (final_block_viewport_line >=0 && final_block_viewport_line < DISPLAY_LINES) {
                *out_final_cursor_draw_x = render_pen_x;
                *out_final_cursor_draw_y_baseline = text_viewport_top_y + final_block_viewport_line * appCtx->line_h;
            }
        }
    }

    // Обробка випадку, коли курсор в самому кінці тексту
    if (current_input_byte_idx == final_text_len) {
        int final_text_end_viewport_line = render_current_abs_line_num - appCtx->first_visible_abs_line_num;
        if (final_text_end_viewport_line >=0 && final_text_end_viewport_line < DISPLAY_LINES) {
            *out_final_cursor_draw_x = render_pen_x;
            *out_final_cursor_draw_y_baseline = text_viewport_top_y + final_text_end_viewport_line * appCtx->line_h;
        }
    }
}


void RenderAppCursor(AppContext *appCtx, bool show_cursor_param, int final_cursor_x_on_screen,
                     int final_cursor_y_baseline_on_screen, int text_viewport_top_y) {

    bool actually_show_cursor = appCtx->is_paused ? true : show_cursor_param; // Курсор завжди видимий на паузі
    if (!appCtx || !appCtx->ren || !actually_show_cursor) return;

    // Перевірка, чи курсор знаходиться в межах видимої текстової області
    if (final_cursor_x_on_screen >= TEXT_AREA_X &&
        final_cursor_x_on_screen <= TEXT_AREA_X + TEXT_AREA_W + 2 && // +2 для невеликого запасу
        final_cursor_y_baseline_on_screen >= text_viewport_top_y &&
        final_cursor_y_baseline_on_screen < text_viewport_top_y + (DISPLAY_LINES * appCtx->line_h) ) {

        SDL_Rect cursor_rect = { final_cursor_x_on_screen, final_cursor_y_baseline_on_screen, 2, appCtx->line_h };
        SDL_SetRenderDrawColor(appCtx->ren, appCtx->palette[COL_CURSOR].r, appCtx->palette[COL_CURSOR].g, appCtx->palette[COL_CURSOR].b, appCtx->palette[COL_CURSOR].a);
        SDL_RenderFillRect(appCtx->ren, &cursor_rect);
    }
}