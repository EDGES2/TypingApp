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

// Helper function to get the starting byte position of the UTF-8 character
// immediately preceding the character at current_pos_ptr in the buffer.
static const char* get_start_of_previous_utf8_char_robust(const char *buffer_start, const char *current_pos_ptr) {
    if (current_pos_ptr <= buffer_start) {
        return buffer_start;
    }

    const char *prev_char_start = buffer_start;
    const char *iter = buffer_start;
    while(iter < current_pos_ptr) {
        prev_char_start = iter; // Remember the start of the current char being scanned
        Sint32 cp = decode_utf8(&iter, current_pos_ptr); // iter advances here
        if (iter <= prev_char_start || cp <= 0) { // Error in decoding or decode_utf8 didn't advance
                                                  // (cp <= 0 includes invalid chars and null terminator)
            // Fallback: step back one byte from original current_pos_ptr.
            // This mimics the original single char backspace's error handling more closely.
            // If current_pos_ptr points right after an invalid sequence that decode_utf8 skipped by one byte,
            // this ensures we go back before that.
            if (current_pos_ptr > buffer_start) {
                 // This is a simplified fallback. A truly robust solution for arbitrary invalid UTF-8
                 // might require more complex byte-level analysis if decode_utf8 itself can't backtrack.
                 // However, for typical scenarios, this should be okay.
                return current_pos_ptr - 1;
            }
            return buffer_start;
        }
    }
    // iter is now == current_pos_ptr, so prev_char_start holds the beginning of the last char
    return prev_char_start;
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

        // --- Modifier Key State Update ---
        if (event->type == SDL_KEYDOWN || event->type == SDL_KEYUP) {
            bool key_is_down = (event->type == SDL_KEYDOWN);
            if (event->key.repeat && key_is_down) { // Ignore repeats for modifier state changes, but not for backspace repeat.
                                                 // This block is only for setting modifier flags.
                // continue; // No, this would skip all other keydown processing for repeats.
                           // Let's allow repeat for backspace etc.
                           // The modifier flags themselves are set once on initial press/release.
            } else { // Process only first press/release for modifiers for pause.
                 // For backspace, repeats are handled later. This 'else' is for pause logic modifiers.

#if defined(_WIN32) || defined(__linux__)
                if (event->key.keysym.sym == SDLK_LALT) appCtx->l_alt_modifier_held = key_is_down;
                else if (event->key.keysym.sym == SDLK_RALT) appCtx->r_alt_modifier_held = key_is_down;
#elif defined(__APPLE__)
                if (event->key.keysym.sym == SDLK_LGUI) appCtx->l_cmd_modifier_held = key_is_down;
                else if (event->key.keysym.sym == SDLK_RGUI) appCtx->r_cmd_modifier_held = key_is_down;
                // LALT for Option key (used for word delete and potentially pause)
                if (event->key.keysym.sym == SDLK_LALT) appCtx->l_alt_modifier_held = key_is_down;
                else if (event->key.keysym.sym == SDLK_RALT) appCtx->r_alt_modifier_held = key_is_down;
#else // Generic, less reliable KMOD check
                if (event->key.keysym.sym == SDLK_LALT || (event->key.keysym.mod & KMOD_LALT)) appCtx->l_alt_modifier_held = key_is_down;
                if (event->key.keysym.sym == SDLK_RALT || (event->key.keysym.mod & KMOD_RALT)) appCtx->r_alt_modifier_held = key_is_down;
                // No standard KMOD for CMD/GUI, so this part is tricky for generic.
                // It's better to rely on specific syms for cmd-like keys if needed on other platforms.
#endif
            }
        }

        // --- Pause Toggle Logic ---
        if (event->type == SDL_KEYDOWN && !event->key.repeat) {
            bool toggle_pause = false;
            // Store current state to check if this is the *first* time the combo is achieved
            // This helps prevent toggling multiple times if keys are held or pressed staggered.
            // For this check, we need the state *before* the current event was processed for modifiers.
            // So, this check needs to be carefully placed or use a snapshot.
            // Let's assume modifier flags are updated above, then we check.
            // The "prev_*" logic was about preventing toggle on *holding* both keys down.
            // A simpler way for "newly pressed combo" is to toggle only if not already paused, or if paused.
            // Let's use a flag to ensure one toggle per keydown combo event.

            // The condition for toggling should be:
            // 1. Both required keys are currently down.
            // 2. We haven't already processed this specific combo trigger in this event loop.
            // The `!event->key.repeat` handles the "newly pressed" aspect for the *individual* key causing the event.
            // The critical part is that *both* keys must be down.

#if defined(_WIN32) || defined(__linux__)
            if (appCtx->l_alt_modifier_held && appCtx->r_alt_modifier_held) {
                // To ensure it only triggers once when the second key of the pair is pressed:
                // Check if the *other* key was already held.
                if (event->key.keysym.sym == SDLK_LALT && !appCtx->is_paused) { // LALT pressed, RALT was already held
                     // if r_alt was held, and now l_alt is pressed
                } else if (event->key.keysym.sym == SDLK_RALT && !appCtx->is_paused) { // RALT pressed, LALT was already held
                    // if l_alt was held, and now r_alt is pressed
                }
                // The above is too complex. Simpler: if both are down, and the sym is one of them, toggle.
                if (event->key.keysym.sym == SDLK_LALT || event->key.keysym.sym == SDLK_RALT) {
                    toggle_pause = true;
                }
            }
#elif defined(__APPLE__)
            bool cmd_pair_active = appCtx->l_cmd_modifier_held && appCtx->r_cmd_modifier_held;
            bool alt_pair_active = appCtx->l_alt_modifier_held && appCtx->r_alt_modifier_held;

            if (cmd_pair_active) {
                 if (event->key.keysym.sym == SDLK_LGUI || event->key.keysym.sym == SDLK_RGUI) {
                    toggle_pause = true;
                 }
            }
            if (!toggle_pause && alt_pair_active) { // Check alt pair only if cmd pair didn't trigger
                 if (event->key.keysym.sym == SDLK_LALT || event->key.keysym.sym == SDLK_RALT) {
                    toggle_pause = true;
                 }
            }
#endif
            if (toggle_pause) {
                // Check if the *specific key causing this event* is one of the pair,
                // and that this is not a repeat. This prevents multiple toggles if both
                // keys are pressed nearly simultaneously generating two KEYDOWN events before
                // the pause state changes the flow.
                // The `!event->key.repeat` helps.
                // The `toggle_pause` variable itself, if reset each event loop, helps ensure one action per distinct combo.

                // The critical check: only toggle if the state is changing from "one mod down" to "both mods down"
                // This means, the key that was just pressed *completed* the pair.
                bool can_toggle_this_event = false;
#if defined(_WIN32) || defined(__linux__)
                if (event->key.keysym.sym == SDLK_LALT && appCtx->r_alt_modifier_held) can_toggle_this_event = true;
                if (event->key.keysym.sym == SDLK_RALT && appCtx->l_alt_modifier_held) can_toggle_this_event = true;
#elif defined(__APPLE__)
                if (appCtx->l_cmd_modifier_held && appCtx->r_cmd_modifier_held) { // CMD pair takes precedence
                    if (event->key.keysym.sym == SDLK_LGUI && appCtx->r_cmd_modifier_held) can_toggle_this_event = true;
                    if (event->key.keysym.sym == SDLK_RGUI && appCtx->l_cmd_modifier_held) can_toggle_this_event = true;
                }
                if (!can_toggle_this_event && appCtx->l_alt_modifier_held && appCtx->r_alt_modifier_held) { // Alt pair
                    if (event->key.keysym.sym == SDLK_LALT && appCtx->r_alt_modifier_held) can_toggle_this_event = true;
                    if (event->key.keysym.sym == SDLK_RALT && appCtx->l_alt_modifier_held) can_toggle_this_event = true;
                }
#endif

                if (can_toggle_this_event) {
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
        }


        // Handling commands in paused state (opening files)
        // This part of the code does not need changes related to pause key logic itself.
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

        // If the game is paused, ignore other input (like text input, backspace)
        if (appCtx->is_paused) {
            continue;
        }

        // Backspace handling (normal and word delete)
        if (event->type == SDL_KEYDOWN) { // Note: SDL_KEYDOWN can repeat if key is held.
                                         // The !event->key.repeat check is usually for actions you want once per physical press.
                                         // For backspace (single or word), repeating is often desired.
            if (event->key.keysym.sym == SDLK_BACKSPACE && *current_input_byte_idx > 0) {
                bool word_delete_modifier_active = false;
                #if defined(__APPLE__)
                    // On macOS, LOption (LAlt) + Backspace
                    if (appCtx->l_alt_modifier_held) { // We are using l_alt_modifier_held (SDLK_LALT)
                        word_delete_modifier_active = true;
                    }
                #else // Windows, Linux
                    // On Windows/Linux, LCtrl + Backspace
                    SDL_Keymod current_mods = SDL_GetModState();
                    if (current_mods & KMOD_LCTRL || current_mods & KMOD_RCTRL) {
                        word_delete_modifier_active = true;
                    }
                #endif

                if (word_delete_modifier_active) {
                    // --- Delete Word Logic ---
                    const char *buffer_start = input_buffer;
                    const char *p = input_buffer + *current_input_byte_idx; // Start at current cursor position (after last char)

                    if (p > buffer_start) { // Only if there's something to delete
                        // 1. Move backwards over any trailing whitespace (from current cursor position)
                        while (p > buffer_start) {
                            const char *prev_char_start_ptr = get_start_of_previous_utf8_char_robust(buffer_start, p);
                            const char *char_to_check_ptr = prev_char_start_ptr; // Copy to not alter prev_char_start_ptr if decode_utf8 modifies its arg
                            Sint32 cp = decode_utf8(&char_to_check_ptr, p);

                            // Consider common whitespace characters
                            if (cp == ' ' || cp == '\n' || cp == '\r' || cp == '\t') {
                                p = prev_char_start_ptr; // Move p to the beginning of this whitespace char
                            } else {
                                break; // Found a non-whitespace character
                            }
                        }

                        // 2. Move backwards over the word (non-whitespace characters)
                        while (p > buffer_start) {
                            const char *prev_char_start_ptr = get_start_of_previous_utf8_char_robust(buffer_start, p);
                            const char *char_to_check_ptr = prev_char_start_ptr;
                            Sint32 cp = decode_utf8(&char_to_check_ptr, p);

                            if (cp > 0 && cp != ' ' && cp != '\n' && cp != '\r' && cp != '\t') {
                                p = prev_char_start_ptr; // Move p to the beginning of this non-whitespace char
                            } else {
                                break; // Found whitespace, null, or error - stop
                            }
                        }
                    }
                    *current_input_byte_idx = (size_t)(p - buffer_start);
                    input_buffer[*current_input_byte_idx] = '\0'; // Truncate the buffer
                    log_event_message_format(appCtx, "Word Backspace. New input index: %zu. Input: '%s'", *current_input_byte_idx, input_buffer);

                } else {
                    // --- Normal Single Character Backspace Logic ---
                    const char *buffer_start = input_buffer;
                    const char *current_pos_ptr = input_buffer + *current_input_byte_idx;
                    const char* prev_char_start_ptr = get_start_of_previous_utf8_char_robust(buffer_start, current_pos_ptr);

                    *current_input_byte_idx = (size_t)(prev_char_start_ptr - buffer_start);
                    input_buffer[*current_input_byte_idx] = '\0'; // Truncate the buffer
                    log_event_message_format(appCtx, "Backspace. New input index: %zu. Input: '%s'", *current_input_byte_idx, input_buffer);
                }
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
                    // size_t target_char_len = (size_t)(p_target_char_next_ptr_for_len - p_target_char_at_offset); // Not used in this block after this

                    if (cp_target <=0 || cp_event != cp_target) { // Error: invalid target character or mismatch
                        appCtx->total_errors_committed_for_accuracy++;
                        if (appCtx->log_file_handle && cp_target > 0) fprintf(appCtx->log_file_handle, "Error: Typed U+%04X (event), Expected U+%04X (target)\n", cp_event, cp_target);
                        else if (appCtx->log_file_handle) fprintf(appCtx->log_file_handle, "Error: Typed U+%04X (event), Expected invalid/end of target text.\n", cp_event);
                    }
                    // Advance the index in the target text, even if there was an error
                    // This logic was buggy, it should advance by the length of the *target* character that was *expected*
                    const char* temp_target_advancer = text_to_type + current_target_byte_offset_for_event;
                    const char* temp_target_advancer_orig = temp_target_advancer;
                    if (current_target_byte_offset_for_event < final_text_len) { // Check again to not read past buffer
                        Sint32 cp_target_for_advance = decode_utf8(&temp_target_advancer, text_to_type + final_text_len);
                        if (cp_target_for_advance > 0 && temp_target_advancer > temp_target_advancer_orig) {
                             current_target_byte_offset_for_event += (size_t)(temp_target_advancer - temp_target_advancer_orig);
                        } else { // Invalid target char or end of text, advance by 1 byte in target offset
                            current_target_byte_offset_for_event++;
                        }
                    } else { // Already at or past end of target text, just increment
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
                    // Use the robust helper to get the last char in the buffer
                    const char *last_char_start_in_buf = get_start_of_previous_utf8_char_robust(input_buffer, end_of_current_input);
                    const char *temp_last_char_ptr_check = last_char_start_in_buf; // Make a copy to pass to decode_utf8
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