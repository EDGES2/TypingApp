#include "layout_logic.h"
#include "text_processing.h" // Для TextBlockInfo, get_next_text_block_func, get_codepoint_advance_and_metrics_func
#include "utf8_utils.h"      // Для decode_utf8
#include "config.h"          // Для TEXT_AREA_X, TEXT_AREA_W, CURSOR_TARGET_VIEWPORT_LINE


void CalculateCursorLayout(AppContext *appCtx, const char *text_to_type, size_t final_text_len,
                           size_t current_input_byte_idx, int *out_cursor_abs_y_line_start, int *out_cursor_exact_x_on_line) {
    if (!appCtx || !text_to_type || !out_cursor_abs_y_line_start || !out_cursor_exact_x_on_line || !appCtx->font || appCtx->line_h <= 0) {
        if(out_cursor_abs_y_line_start) *out_cursor_abs_y_line_start = 0;
        if(out_cursor_exact_x_on_line) *out_cursor_exact_x_on_line = TEXT_AREA_X;
        return;
    }

    int calculated_cursor_y_abs_line_start = 0; // Y координата початку рядка, де знаходиться курсор
    int calculated_cursor_x_on_this_line = TEXT_AREA_X; // Точна X позиція курсора на його рядку
    int current_pen_x_on_line_iterator = TEXT_AREA_X; // X позиція для поточного блоку
    int current_abs_line_num_iterator = 0; // Абсолютний номер поточного рядка

    const char *p_iter = text_to_type; // Ітератор по тексту
    const char *p_end = text_to_type + final_text_len; // Кінець тексту
    size_t processed_bytes_total_iterator = 0; // Кількість оброблених байт
    bool cursor_position_found_this_pass = false; // Прапорець, чи знайдено позицію курсора

    // Якщо курсор на самому початку
    if (current_input_byte_idx == 0) {
        *out_cursor_abs_y_line_start = 0;
        *out_cursor_exact_x_on_line = TEXT_AREA_X;
        return;
    }

    while(p_iter < p_end && !cursor_position_found_this_pass) {
        size_t bytes_at_block_start = processed_bytes_total_iterator;
        int pen_x_at_block_start_on_current_line = current_pen_x_on_line_iterator;
        int abs_line_num_at_block_start = current_abs_line_num_iterator;

        const char* p_iter_before_get_next_block = p_iter;
        TextBlockInfo current_block = get_next_text_block_func(appCtx, &p_iter, p_end, pen_x_at_block_start_on_current_line);

        if (current_block.num_bytes == 0) { // Пропускаємо порожні або невалідні блоки
            if(p_iter < p_end && p_iter == p_iter_before_get_next_block) {p_iter++;} // Гарантуємо просування
            if (p_iter > p_iter_before_get_next_block) {
                 processed_bytes_total_iterator = (size_t)(p_iter - text_to_type);
            } // else processed_bytes_total_iterator не змінюється, якщо p_iter не просунувся
            if (p_iter >= p_end) break;
            continue;
        }

        // Початкові координати для символів цього блоку
        int y_for_chars_in_this_block_abs_line_start = abs_line_num_at_block_start * appCtx->line_h;
        int x_for_chars_in_this_block_start_on_line = pen_x_at_block_start_on_current_line;

        if (current_block.is_newline) {
            current_abs_line_num_iterator = abs_line_num_at_block_start + 1;
            current_pen_x_on_line_iterator = TEXT_AREA_X;
            // Y координата для самого символу \n залишається на поточному рядку
            y_for_chars_in_this_block_abs_line_start = (abs_line_num_at_block_start) * appCtx->line_h;
             // X для \n не важливий, але логічно він на початку наступного
            x_for_chars_in_this_block_start_on_line = TEXT_AREA_X;
        } else { // Не новий рядок
            bool must_wrap_this_block = false;
            // Перевірка на перенос слова (word wrap)
            if (pen_x_at_block_start_on_current_line != TEXT_AREA_X && // Не на початку рядка
                (current_block.is_word || current_block.is_tab)) { // Тільки для слів або табуляцій
                if (pen_x_at_block_start_on_current_line + current_block.pixel_width > TEXT_AREA_X + TEXT_AREA_W) {
                    must_wrap_this_block = true;
                }
                else if (current_block.is_word) { // Додаткова перевірка для "висячих" пробілів
                    const char *p_peek_next = p_iter; // p_iter вже вказує на початок наступного блоку
                    if (p_peek_next < p_end) {
                        const char *temp_peek_ptr = p_peek_next;
                        int pen_x_after_current_block = pen_x_at_block_start_on_current_line + current_block.pixel_width;
                        TextBlockInfo next_block_peek = get_next_text_block_func(appCtx, &temp_peek_ptr, p_end, pen_x_after_current_block);

                        // Якщо наступний блок - це пробіл(и), і він не поміщається
                        if (next_block_peek.num_bytes > 0 && !next_block_peek.is_word && !next_block_peek.is_newline && !next_block_peek.is_tab) {
                            const char* space_char_ptr = next_block_peek.start_ptr;
                            Sint32 cp_space = decode_utf8(&space_char_ptr, next_block_peek.start_ptr + next_block_peek.num_bytes);
                            if (cp_space == ' ') { // Перевіряємо перший символ блоку пробілів
                                int space_width = get_codepoint_advance_and_metrics_func(appCtx, (Uint32)cp_space, appCtx->space_advance_width, NULL, NULL);
                                if (space_width > 0 && (pen_x_after_current_block + space_width > TEXT_AREA_X + TEXT_AREA_W)) {
                                    must_wrap_this_block = true; // Переносимо поточне слово
                                }
                            }
                        }
                    }
                }
            }

            if (must_wrap_this_block) {
                current_abs_line_num_iterator = abs_line_num_at_block_start + 1;
                y_for_chars_in_this_block_abs_line_start = current_abs_line_num_iterator * appCtx->line_h;
                x_for_chars_in_this_block_start_on_line = TEXT_AREA_X;
            }
            current_pen_x_on_line_iterator = x_for_chars_in_this_block_start_on_line + current_block.pixel_width;
        }

        // Якщо курсор знаходиться всередині поточного блоку
        if (!cursor_position_found_this_pass &&
            current_input_byte_idx >= bytes_at_block_start &&
            current_input_byte_idx < bytes_at_block_start + current_block.num_bytes) {

            calculated_cursor_y_abs_line_start = y_for_chars_in_this_block_abs_line_start;
            calculated_cursor_x_on_this_line = x_for_chars_in_this_block_start_on_line; // Починаємо з X початку блоку на його рядку

            const char* p_char_iter_in_block = current_block.start_ptr;
            const char* target_cursor_ptr_in_text = text_to_type + current_input_byte_idx; // Де має бути курсор

            // Ітеруємо по символах всередині блоку до позиції курсора
            while (p_char_iter_in_block < target_cursor_ptr_in_text &&
                   p_char_iter_in_block < current_block.start_ptr + current_block.num_bytes) { // Не виходимо за межі блоку
                const char* temp_char_start_in_block_loop = p_char_iter_in_block;
                Sint32 cp_in_block = decode_utf8(&p_char_iter_in_block, p_end); // p_char_iter_in_block просувається

                if (cp_in_block <= 0 ) { // Помилка або кінець
                    if (p_char_iter_in_block <= temp_char_start_in_block_loop) p_char_iter_in_block = temp_char_start_in_block_loop + 1; // Гарантоване просування
                    break;
                }
                // Важливо: якщо p_char_iter_in_block ПЕРЕСТРИБНУВ target_cursor_ptr_in_text,
                // це означає, що курсор перед поточним cp_in_block.
                // Тоді calculated_cursor_x_on_this_line вже правильний.
                if (p_char_iter_in_block > target_cursor_ptr_in_text && target_cursor_ptr_in_text > temp_char_start_in_block_loop) {
                     p_char_iter_in_block = temp_char_start_in_block_loop; // Відкат, щоб не додавати ширину цього символу
                    break;
                }


                int adv_char_in_block = 0;
                if (current_block.is_tab && cp_in_block == '\t') { // Спеціальна обробка для табуляції всередині блоку (малоймовірно, але можливо)
                    int offset_in_line_inner = calculated_cursor_x_on_this_line - TEXT_AREA_X;
                    adv_char_in_block = appCtx->tab_width_pixels - (offset_in_line_inner % appCtx->tab_width_pixels);
                    if (adv_char_in_block == 0 && offset_in_line_inner >=0) adv_char_in_block = appCtx->tab_width_pixels;
                    if (adv_char_in_block <=0) adv_char_in_block = appCtx->tab_width_pixels;
                } else {
                    adv_char_in_block = get_codepoint_advance_and_metrics_func(appCtx, (Uint32)cp_in_block, appCtx->space_advance_width, NULL, NULL);
                }

                // Перевірка на перенос всередині дуже довгого слова (без пробілів)
                if (calculated_cursor_x_on_this_line + adv_char_in_block > TEXT_AREA_X + TEXT_AREA_W && calculated_cursor_x_on_this_line != TEXT_AREA_X ) {
                    calculated_cursor_y_abs_line_start += appCtx->line_h; // Перехід на новий логічний рядок
                    calculated_cursor_x_on_this_line = TEXT_AREA_X;    // Позиція X скидається
                }
                calculated_cursor_x_on_this_line += adv_char_in_block; // Додаємо ширину символу
            }
            cursor_position_found_this_pass = true;
        }
        processed_bytes_total_iterator = (size_t)(p_iter - text_to_type); // Оновлюємо загальну кількість оброблених байт

        // Якщо курсор точно в кінці поточного блоку
        if (!cursor_position_found_this_pass && processed_bytes_total_iterator == current_input_byte_idx) {
            calculated_cursor_x_on_this_line = current_pen_x_on_line_iterator; // X - це кінець поточного блоку
            calculated_cursor_y_abs_line_start = current_abs_line_num_iterator * appCtx->line_h; // Y - це початок поточного рядка
            cursor_position_found_this_pass = true;
        }
    }

    // Якщо курсор в самому кінці тексту (після всіх блоків)
    if (!cursor_position_found_this_pass && current_input_byte_idx == final_text_len) {
        calculated_cursor_x_on_this_line = current_pen_x_on_line_iterator;
        calculated_cursor_y_abs_line_start = current_abs_line_num_iterator * appCtx->line_h;
    }

    *out_cursor_abs_y_line_start = calculated_cursor_y_abs_line_start;
    *out_cursor_exact_x_on_line = calculated_cursor_x_on_this_line;
}


void UpdateVisibleLine(AppContext *appCtx, int y_coord_for_update_abs) {
    if (!appCtx || appCtx->line_h <= 0) return;

    // Цільовий абсолютний рядок, де знаходиться курсор (або точка фокусу для скролу)
    int target_cursor_abs_line_idx = y_coord_for_update_abs / appCtx->line_h;

    // Розраховуємо перший видимий рядок так, щоб цільовий рядок був на CURSOR_TARGET_VIEWPORT_LINE
    appCtx->first_visible_abs_line_num = target_cursor_abs_line_idx - CURSOR_TARGET_VIEWPORT_LINE;
    if (appCtx->first_visible_abs_line_num < 0) {
        appCtx->first_visible_abs_line_num = 0; // Не може бути менше 0
    }
}

void PerformPredictiveScrollUpdate(AppContext *appCtx,
                                   const char *text_to_type,
                                   size_t final_text_len,
                                   size_t current_input_byte_idx,
                                   int current_logical_cursor_abs_y) {
    if (!appCtx || appCtx->is_paused || appCtx->line_h <= 0) {
        appCtx->predictive_scroll_triggered_this_input_idx = false; // Скидаємо, якщо неактуально
        appCtx->y_offset_due_to_prediction_for_current_idx = 0;
        return;
    }

    // Скидаємо прапорці перед кожною новою перевіркою для поточного індексу вводу
    // (це робиться в головному циклі при зміні current_input_byte_idx)
    // Тут ми тільки встановлюємо їх, якщо потрібно

    int y_coord_for_scroll_update_final = current_logical_cursor_abs_y; // Початково = поточна позиція курсора

    // Перевіряємо, чи потрібно предиктивно скролити
    // Це відбувається, якщо курсор знаходиться на "цільовій лінії для фокусу" (CURSOR_TARGET_VIEWPORT_LINE)
    // і наступний символ, який буде введено, спричинить перехід на новий рядок.
    int current_abs_line_of_cursor = current_logical_cursor_abs_y / appCtx->line_h;
    int target_abs_line_for_viewport_focus = appCtx->first_visible_abs_line_num + CURSOR_TARGET_VIEWPORT_LINE;

    if (current_abs_line_of_cursor == target_abs_line_for_viewport_focus && current_input_byte_idx < final_text_len) {
        size_t next_char_byte_idx_in_doc = 0; // Байт-індекс наступного символу в документі
        const char* p_next_char_scanner = text_to_type + current_input_byte_idx;
        const char* temp_scan_ptr_next = p_next_char_scanner;
        Sint32 cp_next_char = decode_utf8(&temp_scan_ptr_next, text_to_type + final_text_len);

        if (cp_next_char > 0 && temp_scan_ptr_next > p_next_char_scanner) { // Успішно декодовано наступний символ
            next_char_byte_idx_in_doc = (size_t)(temp_scan_ptr_next - text_to_type);
        } else { // Немає валідного наступного символу, або кінець тексту
            next_char_byte_idx_in_doc = current_input_byte_idx + 1; // Припускаємо просування на 1 байт
            if(next_char_byte_idx_in_doc > final_text_len) next_char_byte_idx_in_doc = final_text_len;
        }

        // Якщо наступний символ існує і знаходиться після поточного курсора
        if (next_char_byte_idx_in_doc <= final_text_len && next_char_byte_idx_in_doc > current_input_byte_idx) {
            int y_of_next_char_logical, x_of_next_char_logical; // Логічні координати наступного символу
            CalculateCursorLayout(appCtx, text_to_type, final_text_len, next_char_byte_idx_in_doc,
                                  &y_of_next_char_logical, &x_of_next_char_logical);

            // Якщо наступний символ опиниться на новому логічному рядку
            if (y_of_next_char_logical > current_logical_cursor_abs_y) {
                // Перевіряємо, чи цей новий рядок вимагатиме скролу
                int potential_new_first_visible_abs_line = (y_of_next_char_logical / appCtx->line_h) - CURSOR_TARGET_VIEWPORT_LINE;
                if (potential_new_first_visible_abs_line < 0) potential_new_first_visible_abs_line = 0;

                if (potential_new_first_visible_abs_line > appCtx->first_visible_abs_line_num) {
                    // Так, потрібно предиктивно скролити
                    y_coord_for_scroll_update_final = y_of_next_char_logical; // Скролимо до позиції наступного символу
                    appCtx->y_offset_due_to_prediction_for_current_idx = y_of_next_char_logical - current_logical_cursor_abs_y;
                    appCtx->predictive_scroll_triggered_this_input_idx = true;
                }
            }
        }
    }
    // Якщо предиктивний скрол не спрацював, y_coord_for_scroll_update_final залишається current_logical_cursor_abs_y
    // і прапорці predictive_scroll_triggered_this_input_idx / y_offset_due_to_prediction_for_current_idx
    // будуть скинуті в головному циклі при наступному вводі.

    // Оновлюємо видимий рядок на основі розрахованої y_coord_for_scroll_update_final
    UpdateVisibleLine(appCtx, y_coord_for_scroll_update_final);
}