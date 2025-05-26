#include "event_handler.h"
#include "utf8_utils.h" // For decode_utf8
#include "config.h"     // Possibly for some constants related to events

#include <string.h> // For strlen, snprintf
#include <stdlib.h> // For system()
#include <errno.h>  // For strerror on system() error


// Helper function for logging if appCtx->log_file_handle is available
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
                     const char* actual_text_f_path, // Passed path
                     const char* actual_stats_f_path) { // Passed path

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

        // Handling key presses for pause (LAlt+RAlt or LCmd+RCmd)
        if (event->type == SDL_KEYDOWN) {
            if (!event->key.repeat) { // Process only the first press, not repeats
                bool prev_l_modifier_held = appCtx->l_modifier_held;
                bool prev_r_modifier_held = appCtx->r_modifier_held;

                #if defined(_WIN32) || defined(__linux__)
                    if (event->key.keysym.sym == SDLK_LALT) appCtx->l_modifier_held = true;
                    else if (event->key.keysym.sym == SDLK_RALT) appCtx->r_modifier_held = true;
                #elif defined(__APPLE__)
                    // On macOS, SDLK_LOPTION/ROPTION is often used for Alt, but KMOD_LALT/RALT might also work
                    // CMD is usually SDLK_LGUI/RGUI
                    if (event->key.keysym.sym == SDLK_LGUI || event->key.keysym.sym == SDLK_LALT) appCtx->l_modifier_held = true;
                    else if (event->key.keysym.sym == SDLK_RGUI || event->key.keysym.sym == SDLK_RALT) appCtx->r_modifier_held = true;
                #else // General case (less reliable, better to use scancodes if there are issues)
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
                    else if (event->key.keysym.sym == SDLK_RGUI || event->key.keysym.sym == SDLK_RALT) appCtx->l_modifier_held = false;
                #else
                    // Reset modifiers if the corresponding key was released
                    // This is more difficult to do reliably with KMOD alone, as KMOD might not update perfectly
                    // for some systems. Checking scancode might be more reliable.
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

        // Handling commands in paused state (opening files)
        if (appCtx->is_paused && event->type == SDL_KEYDOWN && !event->key.repeat) {
            char command[1100] = {0}; // Buffer for system command
            const char* file_to_open = NULL;

            if (event->key.keysym.sym == SDLK_s) { // Open statistics file
                file_to_open = actual_stats_f_path;
                log_event_message_format(appCtx, "INFO: 's' pressed (paused state) to open stats file: %s", file_to_open ? file_to_open : "NULL_PATH");
            } else if (event->key.keysym.sym == SDLK_t) { // Open text file
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


            // If 's' or 't' was pressed, do not process further as text input
            if (event->key.keysym.sym == SDLK_s || event->key.keysym.sym == SDLK_t) {
                 continue;
            }
        }

        // If the game is paused, ignore other input
        if (appCtx->is_paused) {
            continue;
        }

        // Backspace handling
        if (event->type == SDL_KEYDOWN) {
            if (event->key.keysym.sym == SDLK_BACKSPACE && *current_input_byte_idx > 0) {
                const char *buffer_start = input_buffer;
                const char *current_pos_ptr = input_buffer + *current_input_byte_idx;
                const char *prev_char_start_ptr_scan = buffer_start; // Pointer to the beginning of the previous character
                const char *temp_iter_scan = buffer_start; // Iterator for scanning

                // Find the beginning of the last UTF-8 character
                while(temp_iter_scan < current_pos_ptr) {
                    prev_char_start_ptr_scan = temp_iter_scan; // Remember the beginning of the current character
                    Sint32 cp_decoded_val = decode_utf8(&temp_iter_scan, current_pos_ptr); // Advance temp_iter_scan
                    if (temp_iter_scan <= prev_char_start_ptr_scan || cp_decoded_val <=0) { // Error or end
                        // If something went wrong, step back one byte (less precise, but safe)
                        prev_char_start_ptr_scan = current_pos_ptr - 1;
                        if (prev_char_start_ptr_scan < buffer_start) prev_char_start_ptr_scan = buffer_start; // Do not go out of buffer bounds
                        break;
                    }
                }
                *current_input_byte_idx = (size_t)(prev_char_start_ptr_scan - buffer_start);
                input_buffer[*current_input_byte_idx] = '\0'; // Truncate the buffer
                log_event_message_format(appCtx, "Backspace. New input index: %zu. Input: '%s'", *current_input_byte_idx, input_buffer);
            }
        }

        // Text input handling
        if (event->type == SDL_TEXTINPUT) {
            if (!(appCtx->typing_started) && final_text_len > 0) { // Start of typing
                appCtx->start_time_ms = SDL_GetTicks();
                appCtx->typing_started = true;
                appCtx->total_keystrokes_for_accuracy = 0; // Reset statistics for the new session
                appCtx->total_errors_committed_for_accuracy = 0;
                log_event_message_format(appCtx, "Typing started.");
            }

            size_t input_event_len_bytes = strlen(event->text.text);
            const char* p_event_char_iter = event->text.text;
            const char* event_text_end = event->text.text + input_event_len_bytes;
            // Index in the target text where the entered character should go for correctness check
            size_t current_target_byte_offset_for_event = *current_input_byte_idx;

            // Error counting based on characters from the event
            while(p_event_char_iter < event_text_end) {
                const char* p_event_char_start_loop = p_event_char_iter;
                Sint32 cp_event = decode_utf8(&p_event_char_iter, event_text_end);
                size_t event_char_len = (size_t)(p_event_char_iter - p_event_char_start_loop);

                if (cp_event <=0 || event_char_len == 0) { // Skip invalid characters from the event
                    if (p_event_char_iter < event_text_end && event_char_len == 0) p_event_char_iter++;
                    continue;
                }

                appCtx->total_keystrokes_for_accuracy++; // Count every valid key press

                if (current_target_byte_offset_for_event < final_text_len) { // Is there still text to compare
                    const char* p_target_char_at_offset = text_to_type + current_target_byte_offset_for_event;
                    const char* p_target_char_next_ptr_for_len = p_target_char_at_offset;
                    Sint32 cp_target = decode_utf8(&p_target_char_next_ptr_for_len, text_to_type + final_text_len);
                    size_t target_char_len = (size_t)(p_target_char_next_ptr_for_len - p_target_char_at_offset);

                    if (cp_target <=0 || cp_event != cp_target) { // Error: invalid target character or mismatch
                        appCtx->total_errors_committed_for_accuracy++;
                        if (appCtx->log_file_handle && cp_target > 0) fprintf(appCtx->log_file_handle, "Error: Typed U+%04X (event), Expected U+%04X (target)\n", cp_event, cp_target);
                        else if (appCtx->log_file_handle) fprintf(appCtx->log_file_handle, "Error: Typed U+%04X (event), Expected invalid/end of target text.\n", cp_event);
                    }
                    // Advance the index in the target text, even if there was an error
                    if(cp_target > 0 && target_char_len > 0) {
                        current_target_byte_offset_for_event += target_char_len;
                    } else { // If the target character is invalid, advance by 1 byte
                        current_target_byte_offset_for_event++;
                    }
                } else { // Text input beyond the target text
                    appCtx->total_errors_committed_for_accuracy++;
                    current_target_byte_offset_for_event++; // Still advance the "expected" position
                     if (appCtx->log_file_handle) fprintf(appCtx->log_file_handle, "Error: Typed U+%04X past end of target text.\n", cp_event);
                }
            }


            // Adding entered text to the input_buffer
            // Prevent overflow and double spaces
            if (*current_input_byte_idx + input_event_len_bytes < final_text_len + 90 ) { // +90 - a small margin
                bool can_add_input = true;
                // Prevent entering a second consecutive space
                if(input_event_len_bytes == 1 && event->text.text[0] == ' ' && *current_input_byte_idx > 0){
                    const char *end_of_current_input = input_buffer + (*current_input_byte_idx);
                    const char *last_char_ptr_in_buf_scan = input_buffer; // Beginning of the last character in the buffer
                    const char *iter_ptr_buf_scan = input_buffer;
                     while(iter_ptr_buf_scan < end_of_current_input){
                        last_char_ptr_in_buf_scan = iter_ptr_buf_scan;
                        Sint32 cp_buf_temp_val = decode_utf8(&iter_ptr_buf_scan, end_of_current_input);
                        if(iter_ptr_buf_scan <= last_char_ptr_in_buf_scan || cp_buf_temp_val <= 0) { // Error or end
                            last_char_ptr_in_buf_scan = end_of_current_input -1; // Rollback 1 byte
                            if (last_char_ptr_in_buf_scan < input_buffer) last_char_ptr_in_buf_scan = input_buffer;
                            break;
                        }
                     }
                     // Now last_char_ptr_in_buf_scan points to the beginning of the last character
                     const char *temp_last_char_ptr_check = last_char_ptr_in_buf_scan;
                     Sint32 last_cp_in_buf = decode_utf8(&temp_last_char_ptr_check, end_of_current_input);
                     if (last_cp_in_buf == ' ') { // If the last character in the buffer was already a space
                        can_add_input = false;
                     }
                }

                if(can_add_input){
                    memcpy(input_buffer + *current_input_byte_idx, event->text.text, input_event_len_bytes);
                    (*current_input_byte_idx) += input_event_len_bytes;
                    input_buffer[*current_input_byte_idx] = '\0'; // Null-termination
                }
            } else {
                log_event_message_format(appCtx, "WARN: Input buffer near full or event text too long. Input from event '%s' ignored.", event->text.text);
            }
        }
    }
}