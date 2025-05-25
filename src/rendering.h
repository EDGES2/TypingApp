#ifndef RENDERING_H
#define RENDERING_H

#include "app_context.h"

void RenderAppTimer(AppContext *appCtx, int *out_timer_h, int *out_timer_w);

void RenderLiveStats(AppContext *appCtx,
                     const char *input_buffer, size_t current_input_byte_idx, // text_to_type не потрібен тут
                     int timer_x_pos, int timer_width, int timer_y_pos, int timer_height);

void RenderTextContent(AppContext *appCtx, const char *text_to_type, size_t final_text_len,
                       const char *input_buffer, size_t current_input_byte_idx,
                       int text_viewport_top_y,
                       int *out_final_cursor_draw_x, int *out_final_cursor_draw_y_baseline);

void RenderAppCursor(AppContext *appCtx, bool show_cursor_param, int final_cursor_x_on_screen,
                     int final_cursor_y_baseline_on_screen, int text_viewport_top_y);

#endif // RENDERING_H