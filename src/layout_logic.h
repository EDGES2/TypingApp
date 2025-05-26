#ifndef LAYOUT_LOGIC_H
#define LAYOUT_LOGIC_H

#include "app_context.h"

void CalculateCursorLayout(AppContext *appCtx, const char *text_to_type, size_t final_text_len,
                           size_t current_input_byte_idx, int *out_cursor_abs_y_line_start, int *out_cursor_exact_x_on_line);

void UpdateVisibleLine(AppContext *appCtx, int y_coord_for_update_abs);

// Function for predictive scrolling, to be called from the main loop
void PerformPredictiveScrollUpdate(AppContext *appCtx,
                                   const char *text_to_type,
                                   size_t final_text_len,
                                   size_t current_input_byte_idx,
                                   int current_logical_cursor_abs_y);
#endif // LAYOUT_LOGIC_H