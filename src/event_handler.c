#include "event_handler.h"
#include "utf8_utils.h" // Для decode_utf8
#include "config.h"     // Можливо, для якихось констант, пов'язаних з подіями

#include <string.h> // Для strlen, snprintf
#include <stdlib.h> // Для system()
#include <errno.h>  // Для strerror при помилці system()


// Допоміжна функція для логування, якщо appCtx->log_file_handle доступний
static void log_event_message_format(AppContext *appCtx, const char* format, ...) {
    if (appCtx && appCtx->log_file_handle && format) {
        va_list args;
        va_start(args, format);
        vfprintf(appCtx->log_file_handle, format, args);
        va_end(args);
        fprintf(appCtx->log_file_handle, "\n");
        fflush(appCtx->log_file_handle);
    }
}


void HandleAppEvents(AppContext *appCtx, SDL_Event *event,
                     size_t *current_input_byte_idx,
                     char *input_buffer, size_t final_text_len,
                     const char* text_to_type,
                     bool *quit_flag,
                     const char* actual_text_f_path, // Переданий шлях
                     const char* actual_stats_f_path) { // Переданий шлях

    if (!appCtx || !event || !current_input_byte_idx || !input_buffer || !quit_flag || !text_to_type) return;

    while (SDL_PollEvent(event)) {
        if (event->type == SDL_QUIT) {
            *quit_flag = true;
            return;
        }

        if (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_ESCAPE) {
            *quit_flag = true;
            return;
        }

        // Обробка натискання клавіш для паузи (LAlt+RAlt або LCmd+RCmd)
        if (event->type == SDL_KEYDOWN) {
            if (!event->key.repeat) { // Обробляємо тільки перше натискання, не повтори
                bool prev_l_modifier_held = appCtx->l_modifier_held;
                bool prev_r_modifier_held = appCtx->r_modifier_held;

                #if defined(_WIN32) || defined(__linux__)
                    if (event->key.keysym.sym == SDLK_LALT) appCtx->l_modifier_held = true;
                    else if (event->key.keysym.sym == SDLK_RALT) appCtx->r_modifier_held = true;
                #elif defined(__APPLE__)
                    // На macOS для Alt часто використовується SDLK_LOPTION/ROPTION, але KMOD_LALT/RALT теж можуть працювати
                    // CMD зазвичай SDLK_LGUI/RGUI
                    if (event->key.keysym.sym == SDLK_LGUI || event->key.keysym.sym == SDLK_LALT) appCtx->l_modifier_held = true;
                    else if (event->key.keysym.sym == SDLK_RGUI || event->key.keysym.sym == SDLK_RALT) appCtx->r_modifier_held = true;
                #else // Загальний випадок (менш надійно, краще використовувати scancodes, якщо є проблеми)
                    if (event->key.keysym.mod & KMOD_LALT) appCtx->l_modifier_held = true;
                    if (event->key.keysym.mod & KMOD_RALT) appCtx->r_modifier_held = true;
                #endif

                if (appCtx->l_modifier_held && appCtx->r_modifier_held && !(prev_l_modifier_held && prev_r_modifier_held)) {
                    appCtx->is_paused = !appCtx->is_paused;
                    if (appCtx->is_paused) {
                        if (appCtx->typing_started) { appCtx->time_at_pause_ms = SDL_GetTicks(); }
                        log_event_message_format(appCtx, "INFO: Game paused.");
                    } else {
                        if (appCtx->typing_started) { appCtx->start_time_ms += (SDL_GetTicks() - appCtx->time_at_pause_ms); }
                        log_event_message_format(appCtx, "INFO: Game resumed.");
                    }
                }
            }
        } else if (event->type == SDL_KEYUP) {
             if (!event->key.repeat) {
                #if defined(_WIN32) || defined(__linux__)
                    if (event->key.keysym.sym == SDLK_LALT) appCtx->l_modifier_held = false;
                    else if (event->key.keysym.sym == SDLK_RALT) appCtx->r_modifier_held = false;
                #elif defined(__APPLE__)
                    if (event->key.keysym.sym == SDLK_LGUI || event->key.keysym.sym == SDLK_LALT) appCtx->l_modifier_held = false;
                    else if (event->key.keysym.sym == SDLK_RGUI || event->key.keysym.sym == SDLK_RALT) appCtx->r_modifier_held = false;
                #else
                    // Скидання модифікаторів, якщо відповідна клавіша була відпущена
                    // Це більш складно зробити надійно лише з KMOD, оскільки KMOD може не оновлюватися ідеально
                    // для деяких систем. Перевірка scancode може бути надійнішою.
                    if (!(event->key.keysym.mod & KMOD_LALT) && appCtx->l_modifier_held &&
                        (event->key.keysym.scancode == SDL_GetScancodeFromKey(SDLK_LALT)
                         #ifdef __APPLE__
                         || event->key.keysym.scancode == SDL_GetScancodeFromKey(SDLK_LGUI)
                         #endif
                        ) ) {
                        appCtx->l_modifier_held = false;
                    }
                    if (!(event->key.keysym.mod & KMOD_RALT) && appCtx->r_modifier_held &&
                        (event->key.keysym.scancode == SDL_GetScancodeFromKey(SDLK_RALT)
                         #ifdef __APPLE__
                         || event->key.keysym.scancode == SDL_GetScancodeFromKey(SDLK_RGUI)
                         #endif
                        ) ) {
                        appCtx->r_modifier_held = false;
                    }
                #endif
            }
        }

        // Обробка команд у стані паузи (відкриття файлів)
        if (appCtx->is_paused && event->type == SDL_KEYDOWN && !event->key.repeat) {
            char command[1100] = {0}; // Буфер для системної команди
            const char* file_to_open = NULL;

            if (event->key.keysym.sym == SDLK_s) { // Відкрити файл статистики
                file_to_open = actual_stats_f_path;
                log_event_message_format(appCtx, "INFO: 's' pressed (paused state) to open stats file: %s", file_to_open ? file_to_open : "NULL_PATH");
            } else if (event->key.keysym.sym == SDLK_t) { // Відкрити текстовий файл
                file_to_open = actual_text_f_path;
                log_event_message_format(appCtx, "INFO: 't' pressed (paused state) to open text file: %s", file_to_open ? file_to_open : "NULL_PATH");
            }

            if (file_to_open && file_to_open[0] != '\0') {
                #ifdef _WIN32
                    snprintf(command, sizeof(command) -1, "explorer \"%s\"", file_to_open);
                #elif __APPLE__
                    snprintf(command, sizeof(command) -1, "open \"%s\"", file_to_open);
                #elif __linux__
                    snprintf(command, sizeof(command) -1, "xdg-open \"%s\"", file_to_open);
                #else
                    log_event_message_format(appCtx, "INFO: No system command defined for this OS to open files.");
                #endif
                command[sizeof(command)-1] = '\0';


                if (command[0] != '\0') {
                    log_event_message_format(appCtx, "Attempting to execute system command: %s", command);
                    int ret = system(command);
                    if (ret != 0) {
                         log_event_message_format(appCtx, "WARN: System command '%s' returned %d. Error: %s", command, ret, strerror(errno));
                    }
                }
            } else {
                if (event->key.keysym.sym == SDLK_s || event->key.keysym.sym == SDLK_t) {
                     log_event_message_format(appCtx, "WARN: File path is not set or empty for the requested action (s or t); cannot open.");
                }
            }
             if (appCtx && appCtx->log_file_handle) fflush(appCtx->log_file_handle);


            // Якщо була натиснута 's' або 't', не обробляємо далі як текстове введення
            if (event->key.keysym.sym == SDLK_s || event->key.keysym.sym == SDLK_t) {
                 continue;
            }
        }

        // Якщо гра на паузі, ігноруємо інше введення
        if (appCtx->is_paused) {
            continue;
        }

        // Обробка Backspace
        if (event->type == SDL_KEYDOWN) {
            if (event->key.keysym.sym == SDLK_BACKSPACE && *current_input_byte_idx > 0) {
                const char *buffer_start = input_buffer;
                const char *current_pos_ptr = input_buffer + *current_input_byte_idx;
                const char *prev_char_start_ptr_scan = buffer_start; // Вказівник на початок попереднього символу
                const char *temp_iter_scan = buffer_start; // Ітератор для сканування

                // Знаходимо початок останнього UTF-8 символу
                while(temp_iter_scan < current_pos_ptr) {
                    prev_char_start_ptr_scan = temp_iter_scan; // Запам'ятовуємо початок поточного символу
                    Sint32 cp_decoded_val = decode_utf8(&temp_iter_scan, current_pos_ptr); // Просуваємо temp_iter_scan
                    if (temp_iter_scan <= prev_char_start_ptr_scan || cp_decoded_val <=0) { // Помилка або кінець
                        // Якщо щось пішло не так, відступаємо на один байт (менш точне, але безпечне)
                        prev_char_start_ptr_scan = current_pos_ptr - 1;
                        if (prev_char_start_ptr_scan < buffer_start) prev_char_start_ptr_scan = buffer_start; // Не виходимо за межі буфера
                        break;
                    }
                }
                *current_input_byte_idx = (size_t)(prev_char_start_ptr_scan - buffer_start);
                input_buffer[*current_input_byte_idx] = '\0'; // Обрізаємо буфер
                log_event_message_format(appCtx, "Backspace. New input index: %zu. Input: '%s'", *current_input_byte_idx, input_buffer);
            }
        }

        // Обробка текстового введення
        if (event->type == SDL_TEXTINPUT) {
            if (!(appCtx->typing_started) && final_text_len > 0) { // Початок набору
                appCtx->start_time_ms = SDL_GetTicks();
                appCtx->typing_started = true;
                appCtx->total_keystrokes_for_accuracy = 0; // Скидаємо статистику для нової сесії
                appCtx->total_errors_committed_for_accuracy = 0;
                log_event_message_format(appCtx, "Typing started.");
            }

            size_t input_event_len_bytes = strlen(event->text.text);
            const char* p_event_char_iter = event->text.text;
            const char* event_text_end = event->text.text + input_event_len_bytes;
            // Індекс в target тексті, куди має потрапити введений символ для перевірки правильності
            size_t current_target_byte_offset_for_event = *current_input_byte_idx;

            // Підрахунок помилок на основі введених символів з події
            while(p_event_char_iter < event_text_end) {
                const char* p_event_char_start_loop = p_event_char_iter;
                Sint32 cp_event = decode_utf8(&p_event_char_iter, event_text_end);
                size_t event_char_len = (size_t)(p_event_char_iter - p_event_char_start_loop);

                if (cp_event <=0 || event_char_len == 0) { // Пропускаємо невалідні символи з події
                    if (p_event_char_iter < event_text_end && event_char_len == 0) p_event_char_iter++;
                    continue;
                }

                appCtx->total_keystrokes_for_accuracy++; // Рахуємо кожне дійсне натискання клавіші

                if (current_target_byte_offset_for_event < final_text_len) { // Чи є ще текст для порівняння
                    const char* p_target_char_at_offset = text_to_type + current_target_byte_offset_for_event;
                    const char* p_target_char_next_ptr_for_len = p_target_char_at_offset;
                    Sint32 cp_target = decode_utf8(&p_target_char_next_ptr_for_len, text_to_type + final_text_len);
                    size_t target_char_len = (size_t)(p_target_char_next_ptr_for_len - p_target_char_at_offset);

                    if (cp_target <=0 || cp_event != cp_target) { // Помилка: невалідний цільовий символ або невідповідність
                        appCtx->total_errors_committed_for_accuracy++;
                        if (appCtx->log_file_handle && cp_target > 0) fprintf(appCtx->log_file_handle, "Error: Typed U+%04X (event), Expected U+%04X (target)\n", cp_event, cp_target);
                        else if (appCtx->log_file_handle) fprintf(appCtx->log_file_handle, "Error: Typed U+%04X (event), Expected invalid/end of target text.\n", cp_event);
                    }
                    // Просуваємо індекс в цільовому тексті, навіть якщо була помилка
                    if(cp_target > 0 && target_char_len > 0) {
                        current_target_byte_offset_for_event += target_char_len;
                    } else { // Якщо цільовий символ невалідний, просуваємо на 1 байт
                        current_target_byte_offset_for_event++;
                    }
                } else { // Введення тексту за межами цільового тексту
                    appCtx->total_errors_committed_for_accuracy++;
                    current_target_byte_offset_for_event++; // Все одно просуваємо "очікувану" позицію
                     if (appCtx->log_file_handle) fprintf(appCtx->log_file_handle, "Error: Typed U+%04X past end of target text.\n", cp_event);
                }
            }


            // Додавання введеного тексту до буфера input_buffer
            // Запобігаємо переповненню та подвійним пробілам
            if (*current_input_byte_idx + input_event_len_bytes < final_text_len + 90 ) { // +90 - невеликий запас
                bool can_add_input = true;
                // Запобігання введенню другого пробілу поспіль
                if(input_event_len_bytes == 1 && event->text.text[0] == ' ' && *current_input_byte_idx > 0){
                    const char *end_of_current_input = input_buffer + (*current_input_byte_idx);
                    const char *last_char_ptr_in_buf_scan = input_buffer; // Початок останнього символу в буфері
                    const char *iter_ptr_buf_scan = input_buffer;
                     while(iter_ptr_buf_scan < end_of_current_input){
                        last_char_ptr_in_buf_scan = iter_ptr_buf_scan;
                        Sint32 cp_buf_temp_val = decode_utf8(&iter_ptr_buf_scan, end_of_current_input);
                        if(iter_ptr_buf_scan <= last_char_ptr_in_buf_scan || cp_buf_temp_val <= 0) { // Помилка або кінець
                            last_char_ptr_in_buf_scan = end_of_current_input -1; // Відкат на 1 байт
                            if (last_char_ptr_in_buf_scan < input_buffer) last_char_ptr_in_buf_scan = input_buffer;
                            break;
                        }
                     }
                     // Тепер last_char_ptr_in_buf_scan вказує на початок останнього символу
                     const char *temp_last_char_ptr_check = last_char_ptr_in_buf_scan;
                     Sint32 last_cp_in_buf = decode_utf8(&temp_last_char_ptr_check, end_of_current_input);
                     if (last_cp_in_buf == ' ') { // Якщо останній символ в буфері вже був пробілом
                        can_add_input = false;
                     }
                }

                if(can_add_input){
                    memcpy(input_buffer + *current_input_byte_idx, event->text.text, input_event_len_bytes);
                    (*current_input_byte_idx) += input_event_len_bytes;
                    input_buffer[*current_input_byte_idx] = '\0'; // Нуль-термінація
                }
            } else {
                log_event_message_format(appCtx, "WARN: Input buffer near full or event text too long. Input from event '%s' ignored.", event->text.text);
            }
        }
    }
}