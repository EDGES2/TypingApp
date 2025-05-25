#include "text_processing.h"
#include "utf8_utils.h" // Для decode_utf8
#include "config.h"     // Для FONT_SIZE, TAB_SIZE_IN_SPACES, TEXT_AREA_X
#include <string.h>     // Для memcpy, strerror
#include <stdlib.h>     // Для malloc, realloc, free
#include <errno.h>      // Для errno

// Допоміжна функція для логування, якщо appCtx->log_file_handle доступний
static void log_message(AppContext *appCtx, const char* message) {
    if (appCtx && appCtx->log_file_handle && message) {
        fprintf(appCtx->log_file_handle, "%s\n", message);
        fflush(appCtx->log_file_handle);
    }
}
static void log_message_format(AppContext *appCtx, const char* format, ...) {
    if (appCtx && appCtx->log_file_handle && format) {
        va_list args;
        va_start(args, format);
        vfprintf(appCtx->log_file_handle, format, args);
        va_end(args);
        fprintf(appCtx->log_file_handle, "\n");
        fflush(appCtx->log_file_handle);
    }
}


char* PreprocessText(AppContext *appCtx, const char* raw_text_buffer, size_t raw_text_len, size_t* out_final_text_len) {
    if (raw_text_buffer == NULL || out_final_text_len == NULL) {
        if (out_final_text_len) *out_final_text_len = 0;
        return NULL;
    }
    if (raw_text_len == 0) {
        *out_final_text_len = 0;
        char* empty_str = (char*)malloc(1);
        if (empty_str) empty_str[0] = '\0';
        else { log_message(appCtx, "Error: malloc failed for empty_str in PreprocessText"); }
        return empty_str;
    }

    // Перший прохід: нормалізація переносів рядків, заміна деяких символів
    size_t temp_buffer_capacity = raw_text_len * 2 + 10; // Запас для замін
    char *temp_buffer = (char*)malloc(temp_buffer_capacity);
    if (!temp_buffer) {
        log_message_format(appCtx, "Error: Failed to allocate temporary buffer in PreprocessText: %s", strerror(errno));
        perror("Failed to allocate temporary buffer in PreprocessText");
        *out_final_text_len = 0;
        return NULL;
    }

    size_t temp_w_idx = 0; // Індекс запису в temp_buffer
    const char* p_read = raw_text_buffer;
    const char* p_read_end = raw_text_buffer + raw_text_len;

    while (p_read < p_read_end) {
        // Перевірка на переповнення буфера перед записом
        if (temp_w_idx + 4 >= temp_buffer_capacity) { // +4 для найдовшої можливої заміни (наприклад, UTF-8 символу)
            temp_buffer_capacity = temp_buffer_capacity * 2 + 4;
            char *new_temp_buffer = (char *)realloc(temp_buffer, temp_buffer_capacity);
            if (!new_temp_buffer) {
                log_message_format(appCtx, "Error: Failed to reallocate temporary buffer (Pass 1): %s", strerror(errno));
                perror("Failed to reallocate temporary buffer in PreprocessText (Pass 1)");
                free(temp_buffer);
                *out_final_text_len = 0;
                return NULL;
            }
            temp_buffer = new_temp_buffer;
        }

        // Обробка \r\n та \r
        if (*p_read == '\r') {
            p_read++;
            if (p_read < p_read_end && *p_read == '\n') { // \r\n
                p_read++;
            }
            // Замінюємо на єдиний \n
            temp_buffer[temp_w_idx++] = '\n';
            continue;
        }

        // Заміна "--" на em-dash (—) U+2014 (E2 80 94)
        if (*p_read == '-' && (p_read + 1 < p_read_end) && *(p_read + 1) == '-') {
            temp_buffer[temp_w_idx++] = (char)0xE2;
            temp_buffer[temp_w_idx++] = (char)0x80;
            temp_buffer[temp_w_idx++] = (char)0x94; // em-dash
            p_read += 2;
            continue;
        }

        const char *char_start = p_read;
        Sint32 cp = decode_utf8(&p_read, p_read_end);
        size_t orig_len = (size_t)(p_read - char_start);

        if (cp <= 0) { // Помилка декодування або кінець
            if (orig_len == 0 && p_read < p_read_end) { // Якщо decode_utf8 не зміг просунутися, робимо це вручну
                p_read++;
            }
            continue; // Пропускаємо невалідні символи
        }

        // Заміни типографіки
        if (cp == 0x2014) { /* em dash — */ temp_buffer[temp_w_idx++] = (char)0xE2; temp_buffer[temp_w_idx++] = (char)0x80; temp_buffer[temp_w_idx++] = (char)0x93; /* en dash – */ continue; } // Заміна em на en dash для консистентності
        if (cp == 0x2026) { /* ellipsis … */ temp_buffer[temp_w_idx++] = '.'; temp_buffer[temp_w_idx++] = '.'; temp_buffer[temp_w_idx++] = '.'; continue; }
        if (cp == 0x2018 || cp == 0x2019 || cp == 0x201C || cp == 0x201D) { /* ’ ‘ “ ” */ temp_buffer[temp_w_idx++] = '\''; continue; }


        // Копіювання оригінального символу (або його UTF-8 послідовності)
        if (temp_w_idx + orig_len <= temp_buffer_capacity) {
            memcpy(temp_buffer + temp_w_idx, char_start, orig_len);
            temp_w_idx += orig_len;
        } else {
            log_message(appCtx, "Error: Buffer overflow in PreprocessText Pass 1, character copy.");
            break; // Вихід з циклу, якщо буфер переповнено
        }
    }
    temp_buffer[temp_w_idx] = '\0'; // Нуль-термінація

    // Другий прохід: видалення зайвих пробілів та переносів рядків
    char *processed_text = (char*)malloc(temp_w_idx + 1); // Буфер для фінального тексту
    if (!processed_text) {
        log_message_format(appCtx, "Error: Failed to allocate processed_text in PreprocessText (Pass 2): %s", strerror(errno));
        perror("Failed to allocate processed_text in PreprocessText (Pass 2)");
        free(temp_buffer);
        *out_final_text_len = 0;
        return NULL;
    }

    size_t final_pt_idx = 0; // Індекс запису в processed_text
    const char* p2_read = temp_buffer;
    const char* p2_read_end = temp_buffer + temp_w_idx;

    int consecutive_newlines = 0;
    bool last_char_output_was_space = true; // Починаємо так, ніби перед текстом був пробіл (щоб не додавати пробіл на початку)
    bool content_has_started = false; // Чи почався вже значущий контент (не пробіли на початку)

    // Пропускаємо початкові пробіли/переноси
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

        if(cp2 <= 0) { // Пропускаємо невалідні або нульові символи
            if (char_len_pass2 == 0 && p2_read < p2_read_end) p2_read++; // Ручне просування, якщо decode_utf8 не зміг
            continue;
        }

        if (cp2 == '\n') {
            consecutive_newlines++;
        } else { // Не перенос рядка
            if (consecutive_newlines > 0) { // Були переноси перед цим символом
                if (content_has_started) {
                    // Видаляємо пробіл перед переносом, якщо він був
                    if (final_pt_idx > 0 && processed_text[final_pt_idx - 1] == ' ') {
                        final_pt_idx--;
                    }
                    if (consecutive_newlines >= 2) { // Два або більше переноси -> один перенос (новий абзац)
                         if (final_pt_idx == 0 || (final_pt_idx > 0 && processed_text[final_pt_idx - 1] != '\n')) { // Уникаємо подвійних переносів
                            if (final_pt_idx < temp_w_idx) processed_text[final_pt_idx++] = '\n'; else break;
                         }
                    } else { // Один перенос -> пробіл (якщо текст не закінчується пробілом/переносом)
                        if (final_pt_idx > 0 && processed_text[final_pt_idx - 1] != ' ' && processed_text[final_pt_idx - 1] != '\n') {
                            if (final_pt_idx < temp_w_idx) processed_text[final_pt_idx++] = ' '; else break;
                        } else if (final_pt_idx == 0 && content_has_started ) { // Якщо це перший символ контенту після переносів
                            // не додаємо пробіл на початку
                        }
                    }
                }
                last_char_output_was_space = true; // Після обробки переносів, вважаємо, що був пробіл
            }
            consecutive_newlines = 0;

            if (cp2 == ' ' || cp2 == '\t') { // Поточний символ - пробіл або таб
                if (content_has_started && !last_char_output_was_space) { // Додаємо один пробіл, якщо попередній не був пробілом
                    if (final_pt_idx < temp_w_idx) processed_text[final_pt_idx++] = ' '; else break;
                }
                last_char_output_was_space = true;
            } else { // Поточний символ - не пробіл/таб/перенос
                // Копіюємо символ
                if (final_pt_idx + char_len_pass2 <= temp_w_idx) { // Перевірка на переповнення
                    memcpy(processed_text + final_pt_idx, char_start_pass2_original, char_len_pass2);
                    final_pt_idx += char_len_pass2;
                } else break;
                last_char_output_was_space = false;
                content_has_started = true; // Позначка, що текст почався
            }
        }
    }

    // Обробка переносів в кінці тексту
    if (consecutive_newlines > 0 && content_has_started) {
        if (final_pt_idx > 0 && processed_text[final_pt_idx - 1] == ' ') final_pt_idx--; // Видаляємо пробіл перед фінальним переносом
        if (consecutive_newlines >= 2) { // Якщо текст закінчувався >=2 переносами
             if (final_pt_idx == 0 || (final_pt_idx > 0 && processed_text[final_pt_idx - 1] != '\n')) {
                if (final_pt_idx < temp_w_idx) processed_text[final_pt_idx++] = '\n';
             }
        }
        // Якщо був один перенос в кінці, він ігнорується (як і кінцеві пробіли нижче)
    }

    // Видалення кінцевих пробілів та переносів рядків
    while (final_pt_idx > 0 && (processed_text[final_pt_idx-1] == ' ' || processed_text[final_pt_idx-1] == '\n')) {
        final_pt_idx--;
    }
    processed_text[final_pt_idx] = '\0';
    *out_final_text_len = final_pt_idx;

    free(temp_buffer); // Звільняємо тимчасовий буфер

    // Оптимізація розміру фінального буфера
    char* final_text = (char*)realloc(processed_text, final_pt_idx + 1);
    if (!final_text && final_pt_idx > 0) { // Якщо realloc не вдався, але є дані
        log_message_format(appCtx, "Warning: realloc failed for final_text, returning original buffer. Length: %zu", final_pt_idx);
        return processed_text; // Повертаємо попередній буфер
    }
    if (!final_text && final_pt_idx == 0 && processed_text != NULL) { // Якщо realloc не вдався і текст порожній
        free(processed_text); // processed_text точно існує, якщо ми тут
        final_text = (char*)malloc(1); // Створюємо порожній рядок
        if(final_text) final_text[0] = '\0';
        else { log_message(appCtx, "Error: malloc for empty final_text failed after realloc failure."); }
        return final_text;
    }
    // Якщо final_text == NULL і processed_text також був NULL (малоймовірно, але можливо, якщо malloc для processed_text не вдався)
    // або якщо realloc повернув NULL для порожнього рядка (final_pt_idx == 0)
    // Повертаємо final_text (який може бути NULL, або вказівником на новий буфер, або тим самим processed_text)
    return final_text ? final_text : processed_text; // realloc може повернути той самий вказівник
}


int get_codepoint_advance_and_metrics_func(AppContext *appCtx, Uint32 codepoint, int fallback_adv, int *out_char_w, int *out_char_h) {
    int adv = 0;
    int char_w = 0;
    int char_h = 0;

    if (!appCtx || !appCtx->font) {
        if (out_char_w) *out_char_w = fallback_adv;
        if (out_char_h) *out_char_h = FONT_SIZE; // Використовуємо FONT_SIZE як резерв
        return fallback_adv;
    }

    char_h = TTF_FontHeight(appCtx->font); // Базова висота

    if (codepoint < 128 && codepoint >= 32) { // Для ASCII символів з кешу
        adv = appCtx->glyph_adv_cache[codepoint];
        // Беремо розміри з кешу для COL_TEXT, оскільки вони не залежать від кольору для геометрії
        if (appCtx->glyph_w_cache[COL_TEXT][codepoint] > 0) char_w = appCtx->glyph_w_cache[COL_TEXT][codepoint];
        else char_w = adv; // Якщо ширина в кеші 0, використовуємо advance

        if (appCtx->glyph_h_cache[COL_TEXT][codepoint] > 0) char_h = appCtx->glyph_h_cache[COL_TEXT][codepoint];
        // else char_h залишається TTF_FontHeight(appCtx->font)

        if (adv == 0 && fallback_adv > 0) adv = fallback_adv; // Якщо advance з кешу 0
    } else { // Для не-ASCII або контрольних символів (якщо вони взагалі передаються сюди)
        if (TTF_GlyphMetrics32(appCtx->font, codepoint, NULL, NULL, NULL, NULL, &adv) != 0) {
            // Помилка отримання метрик, використовуємо fallback
            adv = fallback_adv;
        }
        // Для некешованих символів, припускаємо, що ширина гліфа дорівнює його просуванню
        char_w = adv;
    }

    if (adv <= 0 && codepoint != '\n' && codepoint != '\t') adv = fallback_adv; // Забезпечуємо позитивне просування для друкованих символів

    if (out_char_w) *out_char_w = (char_w > 0) ? char_w : adv;
    if (out_char_h) *out_char_h = (char_h > 0) ? char_h : TTF_FontHeight(appCtx->font); // Завжди повертаємо валідну висоту

    return adv;
}


TextBlockInfo get_next_text_block_func(AppContext *appCtx, const char **text_parser_ptr_ref, const char *text_end, int current_pen_x_for_tab_calc) {
    TextBlockInfo block = {0};
    if (!text_parser_ptr_ref || !*text_parser_ptr_ref || *text_parser_ptr_ref >= text_end || !appCtx || !appCtx->font) {
        return block; // Повертаємо порожній блок
    }

    block.start_ptr = *text_parser_ptr_ref;
    const char *p_initial_for_block = *text_parser_ptr_ref; // Початкова позиція для цього виклику
    const char *temp_scanner = *text_parser_ptr_ref; // Тимчасовий сканер для першого символу

    Sint32 first_cp_in_block = decode_utf8(&temp_scanner, text_end);

    if (first_cp_in_block <= 0) { // Помилка або кінець рядка на самому початку
        // Просуваємо основний вказівник, якщо temp_scanner просунувся або якщо це просто невалідний байт
        if (temp_scanner > *text_parser_ptr_ref) {
            *text_parser_ptr_ref = temp_scanner;
        } else if (*text_parser_ptr_ref < text_end) { // Якщо не просунувся, але не кінець, пропускаємо один байт
            (*text_parser_ptr_ref)++;
        }
        block.num_bytes = (size_t)(*text_parser_ptr_ref - block.start_ptr);
        return block; // Повертаємо блок з 0 або 1 байтом (для невалідного)
    }

    // Обробка спеціальних символів
    if (first_cp_in_block == '\n') {
        block.is_newline = true;
        decode_utf8(text_parser_ptr_ref, text_end); // Просуваємо основний вказівник
        block.pixel_width = 0;
    } else if (first_cp_in_block == '\t') {
        block.is_tab = true;
        decode_utf8(text_parser_ptr_ref, text_end); // Просуваємо основний вказівник
        if (appCtx->tab_width_pixels > 0) {
            int offset_in_line = current_pen_x_for_tab_calc - TEXT_AREA_X;
            block.pixel_width = appCtx->tab_width_pixels - (offset_in_line % appCtx->tab_width_pixels);
            // Якщо курсор точно на позиції табуляції, ширина має бути повною шириною табуляції
            if (block.pixel_width == 0 && offset_in_line >=0) block.pixel_width = appCtx->tab_width_pixels;
            if (block.pixel_width <=0) block.pixel_width = appCtx->tab_width_pixels; // Забезпечення позитивної ширини
        } else {
             // Резервна логіка, якщо tab_width_pixels не ініціалізовано (малоймовірно)
            block.pixel_width = appCtx->space_advance_width * TAB_SIZE_IN_SPACES;
        }
    } else {
        // Обробка слів або послідовностей пробілів
        bool first_char_was_space = (first_cp_in_block == ' ');
        block.is_word = !first_char_was_space; // Якщо не пробіл, то слово

        // *text_parser_ptr_ref тут все ще на початку блоку. Починаємо його просувати.
        while(*text_parser_ptr_ref < text_end) {
            const char* peek_ptr = *text_parser_ptr_ref; // "Заглядаємо" вперед
            Sint32 cp = decode_utf8(&peek_ptr, text_end);

            if (cp <= 0 || cp == '\n' || cp == '\t') { // Кінець блоку при помилці, \n або \t
                break;
            }

            bool current_char_is_space = (cp == ' ');
            if (current_char_is_space != first_char_was_space) { // Зміна типу символу (слово/пробіл)
                break;
            }

            // Якщо все добре, просуваємо основний вказівник та додаємо ширину
            *text_parser_ptr_ref = peek_ptr;
            block.pixel_width += get_codepoint_advance_and_metrics_func(appCtx, (Uint32)cp, appCtx->space_advance_width, NULL, NULL);
        }
    }
    block.num_bytes = (size_t)(*text_parser_ptr_ref - block.start_ptr);
    return block;
}