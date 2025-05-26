#define SDL_MAIN_HANDLED // Needed if main() is not in SDL format

#include "config.h"
#include "app_context.h"
#include "file_paths.h"
#include "text_processing.h"
#include "event_handler.h"
#include "layout_logic.h"
#include "rendering.h"
#include "stats_handler.h"

#include <SDL2/SDL.h> // For SDL_Delay, SDL_GetTicks, SDL_StartTextInput, SDL_StopTextInput
#include <stdio.h>    // For perror
#include <stdlib.h>   // For free, calloc

int main(int argc, char **argv) {
    (void)argc; (void)argv; // Suppress warnings about unused parameters

    AppContext appCtx = {0}; // Initialize with zeros
    FilePaths filePaths = {0}; // Initialize paths with zeros

    // Initialization of SDL, TTF, window, renderer, font, log file, etc.
    // Log file is initialized inside InitializeApp
    if (!InitializeApp(&appCtx, PROJECT_NAME_STR)) {
        // Error is already logged inside InitializeApp
        return 1;
    }
    // Logging after successful context initialization
    if (appCtx.log_file_handle) {
         fprintf(appCtx.log_file_handle, "Main function started. AppContext initialized.\n");
         fflush(appCtx.log_file_handle);
    }


    InitializeFilePaths(&appCtx, &filePaths); // Initialize file paths

    size_t raw_text_len = 0;
    char *raw_text_content = LoadInitialText(&appCtx, &filePaths, &raw_text_len);
    if (!raw_text_content) {
        if (appCtx.log_file_handle) fprintf(appCtx.log_file_handle, "CRITICAL: Failed to load initial text content in main.\n");
        CleanupApp(&appCtx);
        return 1;
    }

    size_t final_text_len = 0;
    char *text_to_type = PreprocessText(&appCtx, raw_text_content, raw_text_len, &final_text_len);
    free(raw_text_content); // raw_text_content is no longer needed
    raw_text_content = NULL;

    if (!text_to_type) {
        if (appCtx.log_file_handle) fprintf(appCtx.log_file_handle, "CRITICAL: Failed to preprocess text in main.\n");
        CleanupApp(&appCtx);
        return 1;
    }
    if (appCtx.log_file_handle && final_text_len == 0) {
        fprintf(appCtx.log_file_handle, "Warning from main: Text content after preprocessing is empty.\n");
         fflush(appCtx.log_file_handle);
    }


    // Buffer for user-entered text. +100 for a small margin.
    char *input_buffer = (char*)calloc(final_text_len + 100, 1);
    if (!input_buffer && (final_text_len + 100 > 0)) { // Check if calloc did not return NULL
        perror("Failed to allocate input buffer in main");
        if (appCtx.log_file_handle) fprintf(appCtx.log_file_handle, "CRITICAL: Failed to allocate input buffer in main.\n");
        free(text_to_type);
        CleanupApp(&appCtx);
        return 1;
    }

    size_t current_input_byte_idx = 0;
    bool show_cursor_flag = true;
    Uint32 last_blink_time = SDL_GetTicks();
    bool quit_game_flag = false;

    SDL_StartTextInput(); // Start accepting text input

    // Main program loop
    while (!quit_game_flag) {
        SDL_Event event;
        size_t old_input_idx = current_input_byte_idx; // To check for input index change

        HandleAppEvents(&appCtx, &event, &current_input_byte_idx, input_buffer,
                        final_text_len, text_to_type, &quit_game_flag,
                        filePaths.actual_text_file_path, filePaths.actual_stats_file_path);

        if (quit_game_flag) break;

        // If the input index changed, reset predictive scroll flags
        if (current_input_byte_idx != old_input_idx) {
            appCtx.predictive_scroll_triggered_this_input_idx = false;
            appCtx.y_offset_due_to_prediction_for_current_idx = 0;
        }

        // Update cursor blink state
        if (!appCtx.is_paused && SDL_GetTicks() - last_blink_time > 500) {
            show_cursor_flag = !show_cursor_flag;
            last_blink_time = SDL_GetTicks();
        } else if (appCtx.is_paused) {
            show_cursor_flag = true; // Cursor is always visible when paused
        }

        // Clear screen
        SDL_SetRenderDrawColor(appCtx.ren, appCtx.palette[COL_BG].r, appCtx.palette[COL_BG].g, appCtx.palette[COL_BG].b, appCtx.palette[COL_BG].a);
        SDL_RenderClear(appCtx.ren);

        // Render timer and get its dimensions
        int timer_h = 0, timer_w = 0;
        RenderAppTimer(&appCtx, &timer_h, &timer_w);

        // Render live statistics
        RenderLiveStats(&appCtx, input_buffer, current_input_byte_idx,
                        TEXT_AREA_X, timer_w, TEXT_AREA_PADDING_Y, timer_h);

        // Determine the top coordinate of the text area
        int text_viewport_top_y = TEXT_AREA_PADDING_Y + timer_h + TEXT_AREA_PADDING_Y;

        // Calculate logical cursor position
        int logical_cursor_abs_y = 0; // Y coordinate of the cursor's line start
        int logical_cursor_x_on_line = 0; // X coordinate of the cursor on its line
        CalculateCursorLayout(&appCtx, text_to_type, final_text_len, current_input_byte_idx,
                              &logical_cursor_abs_y, &logical_cursor_x_on_line);

        // Update visible text area (scrolling)
        // predictive_scroll_triggered and y_offset_due_to_prediction are set inside PerformPredictiveScrollUpdate
        PerformPredictiveScrollUpdate(&appCtx, text_to_type, final_text_len, current_input_byte_idx, logical_cursor_abs_y);


        // Render text content and get final coordinates for drawing the cursor
        int final_cursor_draw_x = -100, final_cursor_draw_y_baseline = -100;
        RenderTextContent(&appCtx, text_to_type, final_text_len, input_buffer,
                          current_input_byte_idx,
                          text_viewport_top_y, &final_cursor_draw_x, &final_cursor_draw_y_baseline);

        // Render cursor
        RenderAppCursor(&appCtx, show_cursor_flag, final_cursor_draw_x, final_cursor_draw_y_baseline, text_viewport_top_y);

        SDL_RenderPresent(appCtx.ren); // Update screen
        SDL_Delay(16); // Limit FPS (approximately 60 FPS)
    }

    SDL_StopTextInput(); // Stop accepting text input

    // Calculate and save final statistics
    if (appCtx.typing_started) {
        CalculateAndPrintAppStats(&appCtx, filePaths.actual_stats_file_path);
        SaveRemainingText(&appCtx, &filePaths, text_to_type, final_text_len, current_input_byte_idx);
    } else {
        printf("No typing started. Stats not saved. Text file not modified.\n");
        if (appCtx.log_file_handle) {
            fprintf(appCtx.log_file_handle, "No typing started in main. Text file '%s' not modified.\n",
                    filePaths.actual_text_file_path[0] ? filePaths.actual_text_file_path : "UNKNOWN_PATH");
        }
    }

    // Free resources
    if (text_to_type) free(text_to_type);
    if (input_buffer) free(input_buffer);
    CleanupApp(&appCtx); // Frees SDL, TTF, font, textures, closes log file

    return 0;
}