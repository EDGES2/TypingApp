#include "layout_logic.h"
#include "text_processing.h" // For TextBlockInfo, get_next_text_block_func, get_codepoint_advance_and_metrics_func
#include "utf8_utils.h"      // For decode_utf8
#include "config.h"          // For TEXT_AREA_X, TEXT_AREA_W, CURSOR_TARGET_VIEWPORT_LINE
#include <stdio.h>           // For fprintf if logging is added here (e.g. in AppContext)

void CalculateCursorLayout(AppContext *appCtx, const char *text_to_type, size_t final_text_len,
                           size_t current_input_byte_idx, int *out_cursor_abs_y_line_start, int *out_cursor_exact_x_on_line) {
    if (!appCtx || !text_to_type || !out_cursor_abs_y_line_start || !out_cursor_exact_x_on_line || !appCtx->font || appCtx->line_h <= 0) {
        if(out_cursor_abs_y_line_start) *out_cursor_abs_y_line_start = 0;
        if(out_cursor_exact_x_on_line) *out_cursor_exact_x_on_line = TEXT_AREA_X;
        return;
    }

    int calculated_cursor_y_abs_line_start = 0; // Y coordinate of the beginning of the line where the cursor is
    int calculated_cursor_x_on_this_line = TEXT_AREA_X; // Exact X position of the cursor on its line
    int current_pen_x_on_line_iterator = TEXT_AREA_X; // X position for the current block
    int current_abs_line_num_iterator = 0; // Absolute number of the current line

    const char *p_iter = text_to_type; // Iterator through text
    const char *p_end = text_to_type + final_text_len; // End of text
    size_t processed_bytes_total_iterator = 0; // Number of processed bytes
    bool cursor_position_found_this_pass = false; // Flag indicating if cursor position has been found

    // If the cursor is at the very beginning
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

        if (current_block.num_bytes == 0) { // Skip empty or invalid blocks
            if(p_iter < p_end && p_iter == p_iter_before_get_next_block) {p_iter++;} // Ensure advancement
            if (p_iter > p_iter_before_get_next_block) {
                 processed_bytes_total_iterator = (size_t)(p_iter - text_to_type);
            } // else processed_bytes_total_iterator does not change if p_iter did not advance
            if (p_iter >= p_end) break;
            continue;
        }

        // Initial coordinates for characters in this block
        int y_for_chars_in_this_block_abs_line_start = abs_line_num_at_block_start * appCtx->line_h;
        int x_for_chars_in_this_block_start_on_line = pen_x_at_block_start_on_current_line;

        if (current_block.is_newline) {
            current_abs_line_num_iterator = abs_line_num_at_block_start + 1;
            current_pen_x_on_line_iterator = TEXT_AREA_X;
            // Y coordinate for the \n character itself remains on the current line
            y_for_chars_in_this_block_abs_line_start = (abs_line_num_at_block_start) * appCtx->line_h;
             // X for \n is not important, but logically it's at the beginning of the next one
            x_for_chars_in_this_block_start_on_line = TEXT_AREA_X;
        } else { // Not a new line
            bool must_wrap_this_block = false;
            // Check for word wrap
            if (pen_x_at_block_start_on_current_line != TEXT_AREA_X && // Not at the beginning of the line
                (current_block.is_word || current_block.is_tab)) { // Only for words or tabs
                if (pen_x_at_block_start_on_current_line + current_block.pixel_width > TEXT_AREA_X + TEXT_AREA_W) {
                    must_wrap_this_block = true;
                }
                else if (current_block.is_word) { // Additional check for "hanging" spaces
                    const char *p_peek_next = p_iter; // p_iter already points to the beginning of the next block
                    if (p_peek_next < p_end) {
                        const char *temp_peek_ptr = p_peek_next;
                        int pen_x_after_current_block = pen_x_at_block_start_on_current_line + current_block.pixel_width;
                        TextBlockInfo next_block_peek = get_next_text_block_func(appCtx, &temp_peek_ptr, p_end, pen_x_after_current_block);

                        // If the next block is space(s), and it doesn't fit
                        if (next_block_peek.num_bytes > 0 && !next_block_peek.is_word && !next_block_peek.is_newline && !next_block_peek.is_tab) {
                            const char* space_char_ptr = next_block_peek.start_ptr;
                            Sint32 cp_space = decode_utf8(&space_char_ptr, next_block_peek.start_ptr + next_block_peek.num_bytes);
                            if (cp_space == ' ') { // Check the first character of the space block
                                int space_width = get_codepoint_advance_and_metrics_func(appCtx, (Uint32)cp_space, appCtx->space_advance_width, NULL, NULL);
                                if (space_width > 0 && (pen_x_after_current_block + space_width > TEXT_AREA_X + TEXT_AREA_W)) {
                                    must_wrap_this_block = true; // Wrap the current word
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

        // If the cursor is inside the current block
        if (!cursor_position_found_this_pass &&
            current_input_byte_idx >= bytes_at_block_start &&
            current_input_byte_idx < bytes_at_block_start + current_block.num_bytes) {

            calculated_cursor_y_abs_line_start = y_for_chars_in_this_block_abs_line_start;
            calculated_cursor_x_on_this_line = x_for_chars_in_this_block_start_on_line; // Start with the X of the block's beginning on its line

            const char* p_char_iter_in_block = current_block.start_ptr;
            const char* target_cursor_ptr_in_text = text_to_type + current_input_byte_idx; // Where the cursor should be

            // Iterate through characters within the block up to the cursor position
            while (p_char_iter_in_block < target_cursor_ptr_in_text &&
                   p_char_iter_in_block < current_block.start_ptr + current_block.num_bytes) { // Do not go beyond the block limits
                const char* temp_char_start_in_block_loop = p_char_iter_in_block;
                Sint32 cp_in_block = decode_utf8(&p_char_iter_in_block, p_end); // p_char_iter_in_block advances

                if (cp_in_block <= 0 ) { // Error or end
                    if (p_char_iter_in_block <= temp_char_start_in_block_loop) p_char_iter_in_block = temp_char_start_in_block_loop + 1; // Guaranteed advancement
                    break;
                }
                // Important: if p_char_iter_in_block JUMPED OVER target_cursor_ptr_in_text,
                // it means the cursor is before the current cp_in_block.
                // Then calculated_cursor_x_on_this_line is already correct.
                if (p_char_iter_in_block > target_cursor_ptr_in_text && target_cursor_ptr_in_text > temp_char_start_in_block_loop) {
                     p_char_iter_in_block = temp_char_start_in_block_loop; // Rollback to avoid adding the width of this character
                    break;
                }


                int adv_char_in_block = 0;
                if (current_block.is_tab && cp_in_block == '\t') { // Special handling for tab within a block (unlikely, but possible)
                    int offset_in_line_inner = calculated_cursor_x_on_this_line - TEXT_AREA_X;
                    adv_char_in_block = appCtx->tab_width_pixels - (offset_in_line_inner % appCtx->tab_width_pixels);
                    if (adv_char_in_block == 0 && offset_in_line_inner >=0) adv_char_in_block = appCtx->tab_width_pixels;
                    if (adv_char_in_block <=0) adv_char_in_block = appCtx->tab_width_pixels;
                } else {
                    adv_char_in_block = get_codepoint_advance_and_metrics_func(appCtx, (Uint32)cp_in_block, appCtx->space_advance_width, NULL, NULL);
                }

                // Check for wrapping within a very long word (without spaces)
                if (calculated_cursor_x_on_this_line + adv_char_in_block > TEXT_AREA_X + TEXT_AREA_W && calculated_cursor_x_on_this_line != TEXT_AREA_X ) {
                    calculated_cursor_y_abs_line_start += appCtx->line_h; // Move to a new logical line
                    calculated_cursor_x_on_this_line = TEXT_AREA_X;    // X position is reset
                }
                calculated_cursor_x_on_this_line += adv_char_in_block; // Add character width
            }
            cursor_position_found_this_pass = true;
        }
        processed_bytes_total_iterator = (size_t)(p_iter - text_to_type); // Update the total number of processed bytes

        // If the cursor is exactly at the end of the current block
        if (!cursor_position_found_this_pass && processed_bytes_total_iterator == current_input_byte_idx) {
            calculated_cursor_x_on_this_line = current_pen_x_on_line_iterator; // X is the end of the current block
            calculated_cursor_y_abs_line_start = current_abs_line_num_iterator * appCtx->line_h; // Y is the beginning of the current line
            cursor_position_found_this_pass = true;
        }
    }

    // If the cursor is at the very end of the text (after all blocks)
    if (!cursor_position_found_this_pass && current_input_byte_idx == final_text_len) {
        calculated_cursor_x_on_this_line = current_pen_x_on_line_iterator;
        calculated_cursor_y_abs_line_start = current_abs_line_num_iterator * appCtx->line_h;
    }

    *out_cursor_abs_y_line_start = calculated_cursor_y_abs_line_start;
    *out_cursor_exact_x_on_line = calculated_cursor_x_on_this_line;
}


void UpdateVisibleLine(AppContext *appCtx, int y_coord_for_update_abs) {
    if (!appCtx || appCtx->line_h <= 0) return;

    // Target absolute line where the cursor is (or scroll focus point)
    int target_cursor_abs_line_idx = y_coord_for_update_abs / appCtx->line_h;

    // Calculate the first visible line so that the target line is on CURSOR_TARGET_VIEWPORT_LINE
    appCtx->first_visible_abs_line_num = target_cursor_abs_line_idx - CURSOR_TARGET_VIEWPORT_LINE;
    if (appCtx->first_visible_abs_line_num < 0) {
        appCtx->first_visible_abs_line_num = 0; // Cannot be less than 0
    }
}

// MODIFIED FUNCTION TO PREVENT SCROLL OSCILLATION
void PerformPredictiveScrollUpdate(AppContext *appCtx,
                                   const char *text_to_type,
                                   size_t final_text_len,
                                   size_t current_input_byte_idx,
                                   int current_logical_cursor_abs_y) {
    if (!appCtx || appCtx->is_paused || appCtx->line_h <= 0) {
        // Ensure flags are reset if paused or invalid state.
        // The main loop handles resetting these flags when current_input_byte_idx changes.
        // If paused, reset flags here to be safe, as unpausing might not trigger input_idx change.
        if (appCtx && appCtx->is_paused) { // Check appCtx not null before dereferencing
             appCtx->predictive_scroll_triggered_this_input_idx = false;
             appCtx->y_offset_due_to_prediction_for_current_idx = 0;
        }
        // If not paused but other conditions (like appCtx null or line_h <=0) fail,
        // UpdateVisibleLine will still be called with current_logical_cursor_abs_y.
        if (appCtx) { // Only call UpdateVisibleLine if appCtx is valid to prevent crash if appCtx is NULL
            UpdateVisibleLine(appCtx, current_logical_cursor_abs_y);
        }
        return;
    }

    int y_coord_for_scroll_update_final;

    // If predictive scroll was already triggered FOR THIS current_input_byte_idx,
    // use the y-offset determined at that point to maintain a stable scroll position.
    // The flags are reset in main.c when current_input_byte_idx changes.
    if (appCtx->predictive_scroll_triggered_this_input_idx) {
        y_coord_for_scroll_update_final = current_logical_cursor_abs_y + appCtx->y_offset_due_to_prediction_for_current_idx;
        // Optional: Log that we are using a stored prediction
        // if(appCtx->log_file_handle) {
        //    fprintf(appCtx->log_file_handle, "Maintained predictive scroll: current_Y=%d, stored_offset=%d, final_Y_for_update=%d\n",
        //            current_logical_cursor_abs_y, appCtx->y_offset_due_to_prediction_for_current_idx, y_coord_for_scroll_update_final);
        //}
    } else {
        // Predictive scroll was not triggered for this input index yet. Attempt to calculate it.
        y_coord_for_scroll_update_final = current_logical_cursor_abs_y; // Default: scroll to current cursor line

        int current_abs_line_of_cursor = current_logical_cursor_abs_y / appCtx->line_h;

        // For deciding IF we should predict NOW, we check against the viewport *as if* we didn't predict.
        // This determines the target line if no prediction were to occur from this point.
        int first_visible_line_if_no_prediction = (current_logical_cursor_abs_y / appCtx->line_h) - CURSOR_TARGET_VIEWPORT_LINE;
        if (first_visible_line_if_no_prediction < 0) first_visible_line_if_no_prediction = 0;

        int target_abs_line_for_viewport_focus_if_no_prediction = first_visible_line_if_no_prediction + CURSOR_TARGET_VIEWPORT_LINE;

        // Condition to check if the cursor is currently on the line that would trigger a lookahead
        // based on a non-predictively scrolled viewport.
        if (current_abs_line_of_cursor == target_abs_line_for_viewport_focus_if_no_prediction &&
            current_input_byte_idx < final_text_len) {

            size_t next_char_byte_idx_in_doc = 0;
            const char* p_next_char_scanner = text_to_type + current_input_byte_idx;
            const char* temp_scan_ptr_next = p_next_char_scanner;
            Sint32 cp_next_char = decode_utf8(&temp_scan_ptr_next, text_to_type + final_text_len);

            if (cp_next_char > 0 && temp_scan_ptr_next > p_next_char_scanner) {
                next_char_byte_idx_in_doc = (size_t)(temp_scan_ptr_next - text_to_type);
            } else {
                next_char_byte_idx_in_doc = current_input_byte_idx + 1;
                if(next_char_byte_idx_in_doc > final_text_len) next_char_byte_idx_in_doc = final_text_len;
            }

            if (next_char_byte_idx_in_doc <= final_text_len && next_char_byte_idx_in_doc > current_input_byte_idx) {
                int y_of_next_char_logical, x_of_next_char_logical;
                CalculateCursorLayout(appCtx, text_to_type, final_text_len, next_char_byte_idx_in_doc,
                                      &y_of_next_char_logical, &x_of_next_char_logical);

                if (y_of_next_char_logical > current_logical_cursor_abs_y) { // Next character is on a new logical line
                    // Calculate where the viewport *would* be if centered on this y_of_next_char_logical
                    int potential_new_first_visible_abs_line_if_predict = (y_of_next_char_logical / appCtx->line_h) - CURSOR_TARGET_VIEWPORT_LINE;
                    if (potential_new_first_visible_abs_line_if_predict < 0) potential_new_first_visible_abs_line_if_predict = 0;

                    // Only trigger predictive scroll if this new viewport start is actually different (further down)
                    // than the viewport start if we just scrolled to the current cursor normally.
                    if (potential_new_first_visible_abs_line_if_predict > first_visible_line_if_no_prediction) {
                        y_coord_for_scroll_update_final = y_of_next_char_logical;
                        appCtx->y_offset_due_to_prediction_for_current_idx = y_of_next_char_logical - current_logical_cursor_abs_y;
                        appCtx->predictive_scroll_triggered_this_input_idx = true;
                        if(appCtx->log_file_handle) { // Optional: Log when predictive scroll is activated
                             fprintf(appCtx->log_file_handle, "Predictive scroll ACTIVATED: next_char_Y_abs=%d, current_cursor_Y_abs=%d, new_offset_Y=%d. Target viewport line for this decision: %d\n",
                                 y_of_next_char_logical, current_logical_cursor_abs_y, appCtx->y_offset_due_to_prediction_for_current_idx, target_abs_line_for_viewport_focus_if_no_prediction);
                        }
                    }
                }
            }
        }
        // If predictive scroll was not triggered in this pass,
        // y_coord_for_scroll_update_final remains current_logical_cursor_abs_y (its initial value in this 'else' block),
        // and appCtx->predictive_scroll_triggered_this_input_idx remains false
        // (it was reset by main loop on input change, or was already false).
    }

    UpdateVisibleLine(appCtx, y_coord_for_scroll_update_final);
}