#include "rendering.h"
#include "text_processing.h" // For get_codepoint_advance_and_metrics_func, TextBlockInfo, get_next_text_block_func
#include "utf8_utils.h"      // For decode_utf8
#include "config.h"          // For TEXT_AREA_X, TEXT_AREA_W, DISPLAY_LINES, COL_CURSOR etc.
#include <stdio.h>           // For snprintf
#include <string.h>          // For memcmp
#include <math.h>            // For roundf

// Helper function for logging if appCtx->log_file_handle is available
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

void RenderAppTimer(AppContext *appCtx, int *out_timer_h_logical, int *out_timer_w_logical) {
    // Ensure AppContext and essential pointers are valid, especially ui_font
    if (!appCtx || !appCtx->ui_font || !appCtx->ren || !out_timer_h_logical || !out_timer_w_logical) {
        if (out_timer_h_logical) *out_timer_h_logical = 0;
        if (out_timer_w_logical) *out_timer_w_logical = 0;
        return;
    }

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
    char timer_buf[40]; // Buffer for timer string

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

    int scaled_timer_pixel_h = 0; // Height of the rendered text surface in scaled pixels
    int scaled_timer_pixel_w = 0; // Width of the rendered text surface in scaled pixels

    // Render text using the UI font
    SDL_Surface *timer_surf = TTF_RenderText_Blended(appCtx->ui_font, timer_buf, appCtx->palette[COL_CURSOR]);

    if (timer_surf) {
        SDL_Texture *timer_tex = SDL_CreateTextureFromSurface(appCtx->ren, timer_surf);
        if (timer_tex) {
            scaled_timer_pixel_w = timer_surf->w; // Physical pixel width from surface
            scaled_timer_pixel_h = timer_surf->h; // Physical pixel height from surface

            // Convert physical pixel dimensions from surface to logical dimensions for dst_rect
            int logical_w = (appCtx->scale_x_factor > 0.01f && scaled_timer_pixel_w > 0) ? (int)roundf((float)scaled_timer_pixel_w / appCtx->scale_x_factor) : scaled_timer_pixel_w;
            int logical_h = (appCtx->scale_y_factor > 0.01f && scaled_timer_pixel_h > 0) ? (int)roundf((float)scaled_timer_pixel_h / appCtx->scale_y_factor) : scaled_timer_pixel_h;
            if (scaled_timer_pixel_w > 0 && logical_w <= 0) logical_w = 1;
            if (scaled_timer_pixel_h > 0 && logical_h <= 0) logical_h = 1;


            SDL_Rect rtimer = { TEXT_AREA_X, TEXT_AREA_PADDING_Y, logical_w, logical_h };
            SDL_RenderCopy(appCtx->ren, timer_tex, NULL, &rtimer); // dst_rect uses logical dimensions
            SDL_DestroyTexture(timer_tex);
        } else {
            log_render_message_format(appCtx, "Error: Failed to create timer texture from surface: %s", SDL_GetError());
            // Fallback: Get dimensions using TTF_SizeText (returns in pixels for the loaded font)
            TTF_SizeText(appCtx->ui_font, timer_buf, &scaled_timer_pixel_w, &scaled_timer_pixel_h);
        }
        SDL_FreeSurface(timer_surf);
    } else {
        log_render_message_format(appCtx, "Error: Failed to render timer text surface: %s", TTF_GetError());
        // Fallback: Get dimensions using TTF_SizeText
        TTF_SizeText(appCtx->ui_font, timer_buf, &scaled_timer_pixel_w, &scaled_timer_pixel_h);
    }

    // Calculate final logical dimensions to output
    int final_logical_w = (appCtx->scale_x_factor > 0.01f && scaled_timer_pixel_w > 0) ? (int)roundf((float)scaled_timer_pixel_w / appCtx->scale_x_factor) : scaled_timer_pixel_w;
    int final_logical_h = (appCtx->scale_y_factor > 0.01f && scaled_timer_pixel_h > 0) ? (int)roundf((float)scaled_timer_pixel_h / appCtx->scale_y_factor) : scaled_timer_pixel_h;

    if (final_logical_w <= 0) final_logical_w = 50; // Minimum logical width to avoid positioning problems
    if (final_logical_h <= 0) final_logical_h = appCtx->ui_line_h > 0 ? appCtx->ui_line_h : 1; // Use logical ui_line_h as fallback

    *out_timer_w_logical = final_logical_w;
    *out_timer_h_logical = final_logical_h;
}

void RenderLiveStats(AppContext *appCtx,
                     const char *input_buffer, size_t current_input_byte_idx,
                     int timer_x_pos_logical, int timer_width_logical,
                     int timer_y_pos_logical, int timer_height_logical) { // Parameters are logical

    // Check for ui_font; main font 'font' is not used here for rendering stats
    if (!appCtx || !appCtx->ui_font || !appCtx->ren || !appCtx->typing_started ) return;

    float elapsed_seconds;
    if (appCtx->is_paused) {
        elapsed_seconds = (float)(appCtx->time_at_pause_ms - appCtx->start_time_ms) / 1000.0f;
    } else {
        elapsed_seconds = (float)(SDL_GetTicks() - appCtx->start_time_ms) / 1000.0f;
    }

    if (elapsed_seconds < 0.05f && appCtx->total_keystrokes_for_accuracy > 0) elapsed_seconds = 0.05f;
    else if (elapsed_seconds < 0.001f) elapsed_seconds = 0.001f;

    float elapsed_minutes = elapsed_seconds / 60.0f;
    float live_accuracy = 100.0f;
    if (appCtx->total_keystrokes_for_accuracy > 0) {
        live_accuracy = ((float)(appCtx->total_keystrokes_for_accuracy - appCtx->total_errors_committed_for_accuracy) / (float)appCtx->total_keystrokes_for_accuracy) * 100.0f;
        if (live_accuracy < 0.0f) live_accuracy = 0.0f;
        if (live_accuracy > 100.0f) live_accuracy = 100.0f;
    }

    size_t live_correct_keystrokes = (appCtx->total_keystrokes_for_accuracy >= appCtx->total_errors_committed_for_accuracy) ?
                                     (appCtx->total_keystrokes_for_accuracy - appCtx->total_errors_committed_for_accuracy) : 0;
    float live_net_words_for_wpm = (float)live_correct_keystrokes / 5.0f;
    float live_wpm = (elapsed_minutes > 0.0001f) ? (live_net_words_for_wpm / elapsed_minutes) : 0.0f;
    if (live_wpm < 0.0f) live_wpm = 0.0f;

    int live_typed_words_count = 0;
    if (current_input_byte_idx > 0 && input_buffer) {
        bool in_word_flag = false;
        const char* p_word_scan_iter = input_buffer;
        const char* p_word_scan_end_iter = input_buffer + current_input_byte_idx;
        while(p_word_scan_iter < p_word_scan_end_iter) {
            const char* temp_p_word_start = p_word_scan_iter;
            Sint32 cp_word = decode_utf8(&p_word_scan_iter, p_word_scan_end_iter);
            if (cp_word <= 0) {
                if (p_word_scan_iter <= temp_p_word_start && p_word_scan_iter < p_word_scan_end_iter) p_word_scan_iter++; else break;
                continue;
            }
            bool is_current_char_separator = (cp_word == ' ' || cp_word == '\n' || cp_word == '\t');
            if (!is_current_char_separator) {
                if (!in_word_flag) {
                    live_typed_words_count++;
                    in_word_flag = true;
                }
            } else {
                in_word_flag = false;
            }
        }
    }

    char wpm_buf[32], acc_buf[32], words_buf[32];
    snprintf(wpm_buf, sizeof(wpm_buf)-1, "WPM: %.0f", live_wpm); wpm_buf[sizeof(wpm_buf)-1] = '\0';
    snprintf(acc_buf, sizeof(acc_buf)-1, "Acc: %.0f%%", live_accuracy); acc_buf[sizeof(acc_buf)-1] = '\0';
    snprintf(words_buf, sizeof(words_buf)-1, "Words: %d", live_typed_words_count); words_buf[sizeof(words_buf)-1] = '\0';

    SDL_Color stat_color = appCtx->palette[COL_TEXT];
    int current_x_render_pos_logical = timer_x_pos_logical + timer_width_logical + 20; // Logical X after the timer

    // Align all UI elements (timer, stats) to the same top Y coordinate for simplicity and visual neatness
    int stats_y_render_pos_logical = timer_y_pos_logical;

    SDL_Surface *surf;
    SDL_Texture *tex;
    SDL_Rect dst_logical; // Destination rectangle in logical coordinates

    // Render WPM
    surf = TTF_RenderText_Blended(appCtx->ui_font, wpm_buf, stat_color); // Use ui_font
    if (surf) {
        tex = SDL_CreateTextureFromSurface(appCtx->ren, surf);
        if (tex) {
            // Convert surface dimensions (scaled pixels) to logical dimensions
            int logical_w = (appCtx->scale_x_factor > 0.01f && surf->w > 0) ? (int)roundf((float)surf->w / appCtx->scale_x_factor) : surf->w;
            int logical_h = (appCtx->scale_y_factor > 0.01f && surf->h > 0) ? (int)roundf((float)surf->h / appCtx->scale_y_factor) : surf->h;
            if (surf->w > 0 && logical_w <= 0) logical_w = 1;
            if (surf->h > 0 && logical_h <= 0) logical_h = 1;

            dst_logical = (SDL_Rect){current_x_render_pos_logical, stats_y_render_pos_logical, logical_w, logical_h};
            SDL_RenderCopy(appCtx->ren, tex, NULL, &dst_logical);
            current_x_render_pos_logical += logical_w + 15; // Advance by logical width + padding
            SDL_DestroyTexture(tex);
        } else { log_render_message_format(appCtx, "Error creating WPM texture: %s", SDL_GetError()); }
        SDL_FreeSurface(surf);
    } else { log_render_message_format(appCtx, "Error rendering WPM surface: %s", TTF_GetError()); }

    // Render Accuracy
    surf = TTF_RenderText_Blended(appCtx->ui_font, acc_buf, stat_color); // Use ui_font
    if (surf) {
        tex = SDL_CreateTextureFromSurface(appCtx->ren, surf);
        if (tex) {
            int logical_w = (appCtx->scale_x_factor > 0.01f && surf->w > 0) ? (int)roundf((float)surf->w / appCtx->scale_x_factor) : surf->w;
            int logical_h = (appCtx->scale_y_factor > 0.01f && surf->h > 0) ? (int)roundf((float)surf->h / appCtx->scale_y_factor) : surf->h;
            if (surf->w > 0 && logical_w <= 0) logical_w = 1;
            if (surf->h > 0 && logical_h <= 0) logical_h = 1;

            dst_logical = (SDL_Rect){current_x_render_pos_logical, stats_y_render_pos_logical, logical_w, logical_h};
            SDL_RenderCopy(appCtx->ren, tex, NULL, &dst_logical);
            current_x_render_pos_logical += logical_w + 15;
            SDL_DestroyTexture(tex);
        } else { log_render_message_format(appCtx, "Error creating Accuracy texture: %s", SDL_GetError()); }
        SDL_FreeSurface(surf);
    } else { log_render_message_format(appCtx, "Error rendering Accuracy surface: %s", TTF_GetError()); }

    // Render Words
    surf = TTF_RenderText_Blended(appCtx->ui_font, words_buf, stat_color); // Use ui_font
    if (surf) {
        tex = SDL_CreateTextureFromSurface(appCtx->ren, surf);
        if (tex) {
            int logical_w = (appCtx->scale_x_factor > 0.01f && surf->w > 0) ? (int)roundf((float)surf->w / appCtx->scale_x_factor) : surf->w;
            int logical_h = (appCtx->scale_y_factor > 0.01f && surf->h > 0) ? (int)roundf((float)surf->h / appCtx->scale_y_factor) : surf->h;
            if (surf->w > 0 && logical_w <= 0) logical_w = 1;
            if (surf->h > 0 && logical_h <= 0) logical_h = 1;

            dst_logical = (SDL_Rect){current_x_render_pos_logical, stats_y_render_pos_logical, logical_w, logical_h};
            SDL_RenderCopy(appCtx->ren, tex, NULL, &dst_logical);
            SDL_DestroyTexture(tex);
        } else { log_render_message_format(appCtx, "Error creating Words texture: %s", SDL_GetError()); }
        SDL_FreeSurface(surf);
    } else { log_render_message_format(appCtx, "Error rendering Words surface: %s", TTF_GetError()); }
}

// RenderTextContent remains unchanged as it uses appCtx->font and its specific caches/metrics
void RenderTextContent(AppContext *appCtx, const char *text_to_type, size_t final_text_len,
                       const char *input_buffer, size_t current_input_byte_idx,
                       int text_viewport_top_y,
                       int *out_final_cursor_draw_x, int *out_final_cursor_draw_y_baseline) {

    // Ensure AppContext and essential pointers are valid, especially the main font
    if (!appCtx || !text_to_type || !input_buffer || !out_final_cursor_draw_x || !out_final_cursor_draw_y_baseline || !appCtx->font || appCtx->line_h <=0) {
        if (out_final_cursor_draw_x) *out_final_cursor_draw_x = -100;
        if (out_final_cursor_draw_y_baseline) *out_final_cursor_draw_y_baseline = -100;
        return;
    }

    int render_pen_x = TEXT_AREA_X;
    int render_current_abs_line_num = 0;
    const char *p_render_iter = text_to_type;
    const char *p_text_end_for_render = text_to_type + final_text_len;

    *out_final_cursor_draw_x = -100;
    *out_final_cursor_draw_y_baseline = -100;

    if (current_input_byte_idx == 0) {
        int relative_line_idx_for_cursor_at_start = 0 - appCtx->first_visible_abs_line_num;
        if (relative_line_idx_for_cursor_at_start >=0 && relative_line_idx_for_cursor_at_start < DISPLAY_LINES) {
            *out_final_cursor_draw_x = TEXT_AREA_X;
            *out_final_cursor_draw_y_baseline = text_viewport_top_y + relative_line_idx_for_cursor_at_start * appCtx->line_h;
        }
    }

    while(p_render_iter < p_text_end_for_render) {
        int current_viewport_line_idx = render_current_abs_line_num - appCtx->first_visible_abs_line_num;
        if (current_viewport_line_idx >= DISPLAY_LINES) break;

        size_t block_start_byte_offset_in_doc = (size_t)(p_render_iter - text_to_type);
        TextBlockInfo block = get_next_text_block_func(appCtx, &p_render_iter, p_text_end_for_render, render_pen_x);

        if (block.num_bytes == 0 && p_render_iter >= p_text_end_for_render) break;
        if (block.num_bytes == 0 || !block.start_ptr) {
            if(p_render_iter < p_text_end_for_render) p_render_iter++; else break;
            continue;
        }

        int line_on_screen_y_baseline = text_viewport_top_y + current_viewport_line_idx * appCtx->line_h;

        if (block_start_byte_offset_in_doc == current_input_byte_idx &&
            current_viewport_line_idx >=0 && current_viewport_line_idx < DISPLAY_LINES ) {
            *out_final_cursor_draw_x = render_pen_x;
            *out_final_cursor_draw_y_baseline = line_on_screen_y_baseline;
        }

        if (block.is_newline) {
            render_current_abs_line_num++;
            render_pen_x = TEXT_AREA_X;
        } else {
            int x_block_starts_on_this_line = render_pen_x;
            int y_baseline_for_block_content = line_on_screen_y_baseline;
            bool wrap_this_block = false;

            if (render_pen_x != TEXT_AREA_X && block.is_word) {
                if (render_pen_x + block.pixel_width > TEXT_AREA_X + TEXT_AREA_W) {
                    wrap_this_block = true;
                } else {
                    const char *p_peek_next_char_after_block = p_render_iter;
                    if (p_peek_next_char_after_block < p_text_end_for_render) {
                        const char *temp_peek_iter = p_peek_next_char_after_block;
                        int pen_x_after_this_block = render_pen_x + block.pixel_width;
                        TextBlockInfo next_block_after_this = get_next_text_block_func(appCtx, &temp_peek_iter, p_text_end_for_render, pen_x_after_this_block);

                        if (next_block_after_this.num_bytes > 0 && !next_block_after_this.is_word &&
                            !next_block_after_this.is_newline && !next_block_after_this.is_tab) {
                            const char* space_block_char_ptr = next_block_after_this.start_ptr;
                            Sint32 cp_space_in_next_block = decode_utf8(&space_block_char_ptr, next_block_after_this.start_ptr + next_block_after_this.num_bytes);
                            if (cp_space_in_next_block == ' ') {
                                int space_char_width = get_codepoint_advance_and_metrics_func(appCtx, (Uint32)cp_space_in_next_block, appCtx->space_advance_width,NULL,NULL);
                                if (space_char_width > 0 && (pen_x_after_this_block + space_char_width > TEXT_AREA_X + TEXT_AREA_W)) {
                                    wrap_this_block = true;
                                }
                            }
                        }
                    }
                }
            }

            if (wrap_this_block) {
                render_current_abs_line_num++;
                current_viewport_line_idx = render_current_abs_line_num - appCtx->first_visible_abs_line_num;
                if (current_viewport_line_idx >= DISPLAY_LINES) break;

                y_baseline_for_block_content = text_viewport_top_y + current_viewport_line_idx * appCtx->line_h;
                render_pen_x = TEXT_AREA_X;
                x_block_starts_on_this_line = render_pen_x;

                if (block_start_byte_offset_in_doc == current_input_byte_idx &&
                    current_viewport_line_idx >=0 && current_viewport_line_idx < DISPLAY_LINES) {
                    *out_final_cursor_draw_x = render_pen_x;
                    *out_final_cursor_draw_y_baseline = y_baseline_for_block_content;
                }
            }

            if (current_viewport_line_idx >= 0 && current_viewport_line_idx < DISPLAY_LINES) {
                if (!block.is_tab) {
                    const char *p_char_in_block = block.start_ptr;
                    const char *p_char_end_in_block = block.start_ptr + block.num_bytes;
                    size_t char_offset_within_block = 0;
                    int char_render_px = x_block_starts_on_this_line;
                    int char_render_py_baseline = y_baseline_for_block_content;
                    int char_current_abs_line_num_for_render = render_current_abs_line_num;

                    while(p_char_in_block < p_char_end_in_block) {
                        int char_current_viewport_line_for_render = char_current_abs_line_num_for_render - appCtx->first_visible_abs_line_num;
                        if (char_current_viewport_line_for_render >= DISPLAY_LINES) goto end_char_loop_render_local;

                        const char* glyph_start_ptr_in_block = p_char_in_block;
                        Sint32 cp_to_render = decode_utf8(&p_char_in_block, p_char_end_in_block);
                        size_t glyph_byte_len = (size_t)(p_char_in_block - glyph_start_ptr_in_block);

                        if (cp_to_render <= 0 || glyph_byte_len == 0) {
                            if (p_char_in_block <= glyph_start_ptr_in_block && p_char_in_block < p_char_end_in_block) p_char_in_block++; else break;
                            continue;
                        }

                        size_t char_absolute_byte_pos_in_doc = block_start_byte_offset_in_doc + char_offset_within_block;
                        if (char_absolute_byte_pos_in_doc == current_input_byte_idx &&
                            char_current_viewport_line_for_render >= 0 && char_current_viewport_line_for_render < DISPLAY_LINES) {
                            *out_final_cursor_draw_x = char_render_px;
                            *out_final_cursor_draw_y_baseline = char_render_py_baseline;
                        }

                        int glyph_w_metric = 0, glyph_h_metric = 0; // These will be filled with logical metrics
                        int advance = get_codepoint_advance_and_metrics_func(appCtx, (Uint32)cp_to_render, appCtx->space_advance_width, &glyph_w_metric, &glyph_h_metric);

                        if (char_render_px + advance > TEXT_AREA_X + TEXT_AREA_W && char_render_px != TEXT_AREA_X ) {
                            char_current_abs_line_num_for_render++;
                            char_current_viewport_line_for_render = char_current_abs_line_num_for_render - appCtx->first_visible_abs_line_num;
                            if (char_current_viewport_line_for_render >= DISPLAY_LINES) goto end_char_loop_render_local;

                            char_render_py_baseline = text_viewport_top_y + char_current_viewport_line_for_render * appCtx->line_h;
                            char_render_px = TEXT_AREA_X;

                            if (char_absolute_byte_pos_in_doc == current_input_byte_idx &&
                                char_current_viewport_line_for_render >=0 && char_current_viewport_line_for_render < DISPLAY_LINES) {
                                *out_final_cursor_draw_x = char_render_px;
                                *out_final_cursor_draw_y_baseline = char_render_py_baseline;
                            }
                        }

                        SDL_Color render_color;
                        bool char_is_typed = char_absolute_byte_pos_in_doc < current_input_byte_idx;
                        bool char_is_correct = false;
                        if(char_is_typed){
                            if(char_absolute_byte_pos_in_doc + glyph_byte_len <= current_input_byte_idx) {
                                char_is_correct = (memcmp(glyph_start_ptr_in_block, input_buffer + char_absolute_byte_pos_in_doc, glyph_byte_len) == 0);
                            }
                            render_color = char_is_correct ? appCtx->palette[COL_CORRECT] : appCtx->palette[COL_INCORRECT];
                        } else {
                            render_color = appCtx->palette[COL_TEXT];
                        }

                        if(cp_to_render >= 32){
                            SDL_Texture* tex_to_render =NULL;
                            bool use_otf_render = false;

                            if(cp_to_render < 128){ // Try to get from cache for ASCII
                                int cache_color_idx = char_is_typed ? (char_is_correct ? COL_CORRECT : COL_INCORRECT) : COL_TEXT;
                                tex_to_render = appCtx->glyph_tex_cache[cache_color_idx][(int)cp_to_render]; // This is a hi-res texture
                                // glyph_w_metric and glyph_h_metric were already obtained from get_codepoint_advance_and_metrics_func
                                // which uses cached logical metrics for these.
                            }

                            if(!tex_to_render && appCtx->font){ // If not in cache or not ASCII, render "on-the-fly"
                                SDL_Surface* surf_otf = TTF_RenderGlyph32_Blended(appCtx->font, (Uint32)cp_to_render, render_color); // surf_otf is hi-res
                                if(surf_otf){
                                    tex_to_render = SDL_CreateTextureFromSurface(appCtx->ren, surf_otf); // Texture is hi-res
                                    if(tex_to_render){
                                        // glyph_w_metric and glyph_h_metric (logical) were already correctly obtained
                                        // by get_codepoint_advance_and_metrics_func for non-cached characters.
                                        // No need to recalculate them from surf_otf->w/h here, as that function handles it.
                                    } else { log_render_message_format(appCtx, "RenderTextContent: OTF Tex Error for U+%04X: %s", cp_to_render, SDL_GetError()); }
                                    SDL_FreeSurface(surf_otf);
                                    use_otf_render = true;
                                } else { log_render_message_format(appCtx, "RenderTextContent: OTF Surf Error for U+%04X: %s", cp_to_render, TTF_GetError()); }
                            }

                            if(tex_to_render){
                                // Ensure logical metrics are valid for rendering
                                if(glyph_w_metric == 0 && advance > 0) glyph_w_metric = advance; // Use logical advance
                                if(glyph_h_metric == 0) glyph_h_metric = appCtx->line_h; // Use logical line height

                                // Vertical centering of the glyph relative to logical line_h
                                int y_offset_for_glyph = (appCtx->line_h > glyph_h_metric) ? (appCtx->line_h - glyph_h_metric) / 2 : 0; // All are logical units
                                SDL_Rect dst_rect = {char_render_px, char_render_py_baseline + y_offset_for_glyph, glyph_w_metric, glyph_h_metric}; // dst_rect is logical
                                SDL_RenderCopy(appCtx->ren, tex_to_render, NULL, &dst_rect);
                                if(use_otf_render) SDL_DestroyTexture(tex_to_render);
                            }
                        }
                        char_render_px += advance; // Advance by logical advance
                        char_offset_within_block += glyph_byte_len;
                    }
                    end_char_loop_render_local:;
                    render_pen_x = char_render_px;
                    render_current_abs_line_num = char_current_abs_line_num_for_render;
                } else {
                    render_pen_x += block.pixel_width; // block.pixel_width is already logical
                }
            } else {
                render_pen_x += block.pixel_width; // block.pixel_width is already logical
            }
        }

        if (block_start_byte_offset_in_doc + block.num_bytes == current_input_byte_idx) {
            int final_block_viewport_line = render_current_abs_line_num - appCtx->first_visible_abs_line_num;
            if (final_block_viewport_line >=0 && final_block_viewport_line < DISPLAY_LINES) {
                *out_final_cursor_draw_x = render_pen_x;
                *out_final_cursor_draw_y_baseline = text_viewport_top_y + final_block_viewport_line * appCtx->line_h;
            }
        }
    }

    if (current_input_byte_idx == final_text_len) {
        int final_text_end_viewport_line = render_current_abs_line_num - appCtx->first_visible_abs_line_num;
        if (final_text_end_viewport_line >=0 && final_text_end_viewport_line < DISPLAY_LINES) {
            *out_final_cursor_draw_x = render_pen_x;
            *out_final_cursor_draw_y_baseline = text_viewport_top_y + final_text_end_viewport_line * appCtx->line_h;
        }
    }
}

// RenderAppCursor remains unchanged as it uses logical appCtx->line_h
void RenderAppCursor(AppContext *appCtx, bool show_cursor_param, int final_cursor_x_on_screen,
                     int final_cursor_y_baseline_on_screen, int text_viewport_top_y) {

    bool actually_show_cursor = appCtx->is_paused ? true : show_cursor_param;
    if (!appCtx || !appCtx->ren || !actually_show_cursor) return;

    if (final_cursor_x_on_screen >= TEXT_AREA_X &&
        final_cursor_x_on_screen <= TEXT_AREA_X + TEXT_AREA_W + 2 &&
        final_cursor_y_baseline_on_screen >= text_viewport_top_y &&
        final_cursor_y_baseline_on_screen < text_viewport_top_y + (DISPLAY_LINES * appCtx->line_h) ) {

        SDL_Rect cursor_rect = { final_cursor_x_on_screen, final_cursor_y_baseline_on_screen, 2, appCtx->line_h }; // width 2 is logical
        SDL_SetRenderDrawColor(appCtx->ren, appCtx->palette[COL_CURSOR].r, appCtx->palette[COL_CURSOR].g, appCtx->palette[COL_CURSOR].b, appCtx->palette[COL_CURSOR].a);
        SDL_RenderFillRect(appCtx->ren, &cursor_rect);
    }
}