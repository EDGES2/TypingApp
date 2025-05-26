#include "text_processing.h"
#include "utf8_utils.h" // For decode_utf8
#include "config.h"     // For FONT_SIZE, TAB_SIZE_IN_SPACES, TEXT_AREA_X
#include <string.h>     // For memcpy, strerror
#include <stdlib.h>     // For malloc, realloc, free
#include <errno.h>      // For errno
#include <math.h> // For roundf

// Helper function for logging if appCtx->log_file_handle is available
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

    // First pass: normalize line breaks, replace some characters
    size_t temp_buffer_capacity = raw_text_len * 2 + 10; // Margin for replacements
    char *temp_buffer = (char*)malloc(temp_buffer_capacity);
    if (!temp_buffer) {
        log_message_format(appCtx, "Error: Failed to allocate temporary buffer in PreprocessText: %s", strerror(errno));
        perror("Failed to allocate temporary buffer in PreprocessText");
        *out_final_text_len = 0;
        return NULL;
    }

    size_t temp_w_idx = 0; // Write index in temp_buffer
    const char* p_read = raw_text_buffer;
    const char* p_read_end = raw_text_buffer + raw_text_len;

    while (p_read < p_read_end) {
        // Check for buffer overflow before writing
        if (temp_w_idx + 4 >= temp_buffer_capacity) { // +4 for the longest possible replacement (e.g., a UTF-8 character)
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

        // Handling \r\n and \r
        if (*p_read == '\r') {
            p_read++;
            if (p_read < p_read_end && *p_read == '\n') { // \r\n
                p_read++;
            }
            // Replace with a single \n
            temp_buffer[temp_w_idx++] = '\n';
            continue;
        }

        // Replace "--" with em-dash (—) U+2014 (E2 80 94)
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

        if (cp <= 0) { // Decoding error or end
            if (orig_len == 0 && p_read < p_read_end) { // If decode_utf8 couldn't advance, do it manually
                p_read++;
            }
            continue; // Skip invalid characters
        }

        // Typographic replacements
        if (cp == 0x2014) { /* em dash — */ temp_buffer[temp_w_idx++] = (char)0xE2; temp_buffer[temp_w_idx++] = (char)0x80; temp_buffer[temp_w_idx++] = (char)0x93; /* en dash – */ continue; } // Replace em with en dash for consistency
        if (cp == 0x2026) { /* ellipsis … */ temp_buffer[temp_w_idx++] = '.'; temp_buffer[temp_w_idx++] = '.'; temp_buffer[temp_w_idx++] = '.'; continue; }
        if (cp == 0x2018 || cp == 0x2019 || cp == 0x201C || cp == 0x201D) { /* ’ ‘ “ ” */ temp_buffer[temp_w_idx++] = '\''; continue; }


        // Copying the original character (or its UTF-8 sequence)
        if (temp_w_idx + orig_len <= temp_buffer_capacity) {
            memcpy(temp_buffer + temp_w_idx, char_start, orig_len);
            temp_w_idx += orig_len;
        } else {
            log_message(appCtx, "Error: Buffer overflow in PreprocessText Pass 1, character copy.");
            break; // Exit loop if buffer is full
        }
    }
    temp_buffer[temp_w_idx] = '\0'; // Null-termination

    // Second pass: remove extra spaces and line breaks
    char *processed_text = (char*)malloc(temp_w_idx + 1); // Buffer for the final text
    if (!processed_text) {
        log_message_format(appCtx, "Error: Failed to allocate processed_text in PreprocessText (Pass 2): %s", strerror(errno));
        perror("Failed to allocate processed_text in PreprocessText (Pass 2)");
        free(temp_buffer);
        *out_final_text_len = 0;
        return NULL;
    }

    size_t final_pt_idx = 0; // Write index in processed_text
    const char* p2_read = temp_buffer;
    const char* p2_read_end = temp_buffer + temp_w_idx;

    int consecutive_newlines = 0;
    bool last_char_output_was_space = true; // Start as if there was a space before the text (to avoid adding a space at the beginning)
    bool content_has_started = false; // Has significant content started already (not spaces at the beginning)

    // Skip leading spaces/line breaks
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

        if(cp2 <= 0) { // Skip invalid or null characters
            if (char_len_pass2 == 0 && p2_read < p2_read_end) p2_read++; // Manual advancement if decode_utf8 failed
            continue;
        }

        if (cp2 == '\n') {
            consecutive_newlines++;
        } else { // Not a line break
            if (consecutive_newlines > 0) { // There were line breaks before this character
                if (content_has_started) {
                    // Remove space before the line break if it was there
                    if (final_pt_idx > 0 && processed_text[final_pt_idx - 1] == ' ') {
                        final_pt_idx--;
                    }
                    if (consecutive_newlines >= 2) { // Two or more line breaks -> one line break (new paragraph)
                         if (final_pt_idx == 0 || (final_pt_idx > 0 && processed_text[final_pt_idx - 1] != '\n')) { // Avoid double line breaks
                            if (final_pt_idx < temp_w_idx) processed_text[final_pt_idx++] = '\n'; else break;
                         }
                    } else { // One line break -> space (if text doesn't end with a space/line break)
                        if (final_pt_idx > 0 && processed_text[final_pt_idx - 1] != ' ' && processed_text[final_pt_idx - 1] != '\n') {
                            if (final_pt_idx < temp_w_idx) processed_text[final_pt_idx++] = ' '; else break;
                        } else if (final_pt_idx == 0 && content_has_started ) { // If this is the first content character after line breaks
                            // do not add a space at the beginning
                        }
                    }
                }
                last_char_output_was_space = true; // After processing line breaks, assume there was a space
            }
            consecutive_newlines = 0;

            if (cp2 == ' ' || cp2 == '\t') { // Current character is a space or tab
                if (content_has_started && !last_char_output_was_space) { // Add one space if the previous one was not a space
                    if (final_pt_idx < temp_w_idx) processed_text[final_pt_idx++] = ' '; else break;
                }
                last_char_output_was_space = true;
            } else { // Current character is not a space/tab/line break
                // Copy the character
                if (final_pt_idx + char_len_pass2 <= temp_w_idx) { // Check for overflow
                    memcpy(processed_text + final_pt_idx, char_start_pass2_original, char_len_pass2);
                    final_pt_idx += char_len_pass2;
                } else break;
                last_char_output_was_space = false;
                content_has_started = true; // Mark that text has started
            }
        }
    }

    // Handling line breaks at the end of the text
    if (consecutive_newlines > 0 && content_has_started) {
        if (final_pt_idx > 0 && processed_text[final_pt_idx - 1] == ' ') final_pt_idx--; // Remove space before the final line break
        if (consecutive_newlines >= 2) { // If the text ended with >=2 line breaks
             if (final_pt_idx == 0 || (final_pt_idx > 0 && processed_text[final_pt_idx - 1] != '\n')) {
                if (final_pt_idx < temp_w_idx) processed_text[final_pt_idx++] = '\n';
             }
        }
        // If there was one line break at the end, it's ignored (like trailing spaces below)
    }

    // Remove trailing spaces and line breaks
    while (final_pt_idx > 0 && (processed_text[final_pt_idx-1] == ' ' || processed_text[final_pt_idx-1] == '\n')) {
        final_pt_idx--;
    }
    processed_text[final_pt_idx] = '\0';
    *out_final_text_len = final_pt_idx;

    free(temp_buffer); // Free the temporary buffer

    // Optimize the size of the final buffer
    char* final_text = (char*)realloc(processed_text, final_pt_idx + 1);
    if (!final_text && final_pt_idx > 0) { // If realloc failed, but there is data
        log_message_format(appCtx, "Warning: realloc failed for final_text, returning original buffer. Length: %zu", final_pt_idx);
        return processed_text; // Return the previous buffer
    }
    if (!final_text && final_pt_idx == 0 && processed_text != NULL) { // If realloc failed and the text is empty
        free(processed_text); // processed_text definitely exists if we are here
        final_text = (char*)malloc(1); // Create an empty string
        if(final_text) final_text[0] = '\0';
        else { log_message(appCtx, "Error: malloc for empty final_text failed after realloc failure."); }
        return final_text;
    }
    // If final_text == NULL and processed_text was also NULL (unlikely, but possible if malloc for processed_text failed)
    // or if realloc returned NULL for an empty string (final_pt_idx == 0)
    // Return final_text (which can be NULL, or a pointer to a new buffer, or the same processed_text)
    return final_text ? final_text : processed_text; // realloc might return the same pointer
}


int get_codepoint_advance_and_metrics_func(AppContext *appCtx, Uint32 codepoint, int fallback_adv_logical, int *out_char_w_logical, int *out_char_h_logical) {
    int final_adv_logical;
    int char_w_val_logical = 0;
    int char_h_val_logical = 0;

    if (!appCtx || !appCtx->font) {
        if (out_char_w_logical) *out_char_w_logical = fallback_adv_logical > 0 ? fallback_adv_logical : 1;
        if (out_char_h_logical) *out_char_h_logical = FONT_SIZE > 0 ? FONT_SIZE : 1;
        return fallback_adv_logical > 0 ? fallback_adv_logical : 1;
    }

    // Використовуємо логічну висоту рядка як базу для висоти символу
    int base_logical_h = appCtx->line_h > 0 ? appCtx->line_h : FONT_SIZE;
    if (base_logical_h <= 0) base_logical_h = 1;


    if (codepoint < 128 && codepoint >= 32) { // ASCII from cache
        final_adv_logical = appCtx->glyph_adv_cache[codepoint];
        char_w_val_logical = appCtx->glyph_w_cache[COL_TEXT][codepoint];
        char_h_val_logical = appCtx->glyph_h_cache[COL_TEXT][codepoint];
    } else {
        if (codepoint == '\t') {
            // Для табуляції, ширина розраховується динамічно в get_next_text_block_func.
            // Тут можна повернути ширину одного пробілу як placeholder, якщо потрібно.
            final_adv_logical = appCtx->space_advance_width > 0 ? appCtx->space_advance_width : (fallback_adv_logical > 0 ? fallback_adv_logical : 1);
            char_w_val_logical = final_adv_logical;
            char_h_val_logical = base_logical_h;
        } else if (codepoint == '\n') {
            final_adv_logical = 0;
            char_w_val_logical = 0;
            char_h_val_logical = base_logical_h;
        } else { // Other non-cached characters
            int scaled_adv_px_otf, scaled_min_x_otf, scaled_max_x_otf, scaled_min_y_otf, scaled_max_y_otf;
            // TTF_GlyphMetrics32 returns values in pixels for the loaded (DPI-aware) font
            if (TTF_GlyphMetrics32(appCtx->font, codepoint, &scaled_min_x_otf, &scaled_max_x_otf, &scaled_min_y_otf, &scaled_max_y_otf, &scaled_adv_px_otf) != 0) {
                final_adv_logical = fallback_adv_logical;
                char_w_val_logical = fallback_adv_logical; // Use fallback as logical
                char_h_val_logical = base_logical_h;     // Use base logical height
            } else {
                final_adv_logical = (appCtx->scale_x_factor > 0.01f) ? (int)roundf((float)scaled_adv_px_otf / appCtx->scale_x_factor) : scaled_adv_px_otf;

                int scaled_w_px_otf = scaled_max_x_otf - scaled_min_x_otf;
                char_w_val_logical = (appCtx->scale_x_factor > 0.01f && scaled_w_px_otf > 0) ? (int)roundf((float)scaled_w_px_otf / appCtx->scale_x_factor) : scaled_w_px_otf;

                // If calculated logical width is 0 or less, but advance is positive, use advance.
                if (char_w_val_logical <= 0 && final_adv_logical > 0) char_w_val_logical = final_adv_logical;
                else if (char_w_val_logical <= 0 && scaled_w_px_otf > 0) char_w_val_logical = 1; // Min logical width if surf had width

                // For character height, it's generally safer to use the logical line height.
                char_h_val_logical = base_logical_h;
            }
        }
    }

    // Ensure minimum positive logical values for drawable characters
    if (codepoint >= 32) { // Printable character
        if (final_adv_logical <= 0) final_adv_logical = fallback_adv_logical > 0 ? fallback_adv_logical : 1;
        if (char_w_val_logical <= 0) char_w_val_logical = final_adv_logical; // Width at least advance
        if (char_h_val_logical <= 0) char_h_val_logical = base_logical_h;
    }


    if (out_char_w_logical) *out_char_w_logical = char_w_val_logical;
    if (out_char_h_logical) *out_char_h_logical = char_h_val_logical;

    return final_adv_logical;
}


TextBlockInfo get_next_text_block_func(AppContext *appCtx, const char **text_parser_ptr_ref, const char *text_end, int current_pen_x_for_tab_calc) {
    TextBlockInfo block = {0};
    if (!text_parser_ptr_ref || !*text_parser_ptr_ref || *text_parser_ptr_ref >= text_end || !appCtx || !appCtx->font) {
        return block; // Return an empty block
    }

    block.start_ptr = *text_parser_ptr_ref;
    const char *p_initial_for_block = *text_parser_ptr_ref; // Initial position for this call
    const char *temp_scanner = *text_parser_ptr_ref; // Temporary scanner for the first character

    Sint32 first_cp_in_block = decode_utf8(&temp_scanner, text_end);

    if (first_cp_in_block <= 0) { // Error or end of line at the very beginning
        // Advance the main pointer if temp_scanner advanced or if it's just an invalid byte
        if (temp_scanner > *text_parser_ptr_ref) {
            *text_parser_ptr_ref = temp_scanner;
        } else if (*text_parser_ptr_ref < text_end) { // If it didn't advance, but not the end, skip one byte
            (*text_parser_ptr_ref)++;
        }
        block.num_bytes = (size_t)(*text_parser_ptr_ref - block.start_ptr);
        return block; // Return a block with 0 or 1 byte (for an invalid one)
    }

    // Handling special characters
    if (first_cp_in_block == '\n') {
        block.is_newline = true;
        decode_utf8(text_parser_ptr_ref, text_end); // Advance the main pointer
        block.pixel_width = 0;
    } else if (first_cp_in_block == '\t') {
        block.is_tab = true;
        decode_utf8(text_parser_ptr_ref, text_end); // Advance the main pointer
        if (appCtx->tab_width_pixels > 0) {
            int offset_in_line = current_pen_x_for_tab_calc - TEXT_AREA_X;
            block.pixel_width = appCtx->tab_width_pixels - (offset_in_line % appCtx->tab_width_pixels);
            // If the cursor is exactly at a tab position, the width should be the full tab width
            if (block.pixel_width == 0 && offset_in_line >=0) block.pixel_width = appCtx->tab_width_pixels;
            if (block.pixel_width <=0) block.pixel_width = appCtx->tab_width_pixels; // Ensure positive width
        } else {
             // Fallback logic if tab_width_pixels is not initialized (unlikely)
            block.pixel_width = appCtx->space_advance_width * TAB_SIZE_IN_SPACES;
        }
    } else {
        // Handling words or sequences of spaces
        bool first_char_was_space = (first_cp_in_block == ' ');
        block.is_word = !first_char_was_space; // If not a space, then it's a word

        // *text_parser_ptr_ref is still at the beginning of the block here. Start advancing it.
        while(*text_parser_ptr_ref < text_end) {
            const char* peek_ptr = *text_parser_ptr_ref; // "Peek" ahead
            Sint32 cp = decode_utf8(&peek_ptr, text_end);

            if (cp <= 0 || cp == '\n' || cp == '\t') { // End of block on error, \n or \t
                break;
            }

            bool current_char_is_space = (cp == ' ');
            if (current_char_is_space != first_char_was_space) { // Change of character type (word/space)
                break;
            }

            // If all is well, advance the main pointer and add the width
            *text_parser_ptr_ref = peek_ptr;
            block.pixel_width += get_codepoint_advance_and_metrics_func(appCtx, (Uint32)cp, appCtx->space_advance_width, NULL, NULL);
        }
    }
    block.num_bytes = (size_t)(*text_parser_ptr_ref - block.start_ptr);
    return block;
}