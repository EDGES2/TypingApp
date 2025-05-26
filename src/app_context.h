#ifndef APP_CONTEXT_H
#define APP_CONTEXT_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdbool.h>
#include <stdio.h> // For FILE*
#include "config.h" // For N_COLORS

typedef struct {
    SDL_Window *win;
    SDL_Renderer *ren;
    TTF_Font *font;
    SDL_Color palette[N_COLORS];
    int line_h; // Height of a single text line

    // Glyph cache for ASCII characters (32-126)
    SDL_Texture *glyph_tex_cache[N_COLORS][128];
    int glyph_adv_cache[128]; // Advance width (advance)
    int glyph_w_cache[N_COLORS][128]; // Glyph width
    int glyph_h_cache[N_COLORS][128]; // Glyph height

    int space_advance_width; // Advance width for space
    int tab_width_pixels;    // Tab width in pixels

    // Program state
    bool typing_started;
    Uint32 start_time_ms;
    Uint32 time_at_pause_ms; // Time when pause was pressed
    bool is_paused;
    bool l_modifier_held; // LAlt or LCmd
    bool r_modifier_held; // RAlt or RCmd

    // Statistics
    unsigned long long total_keystrokes_for_accuracy;
    unsigned long long total_errors_committed_for_accuracy;

    // For logging
    FILE *log_file_handle;

    // For display and scrolling
    int first_visible_abs_line_num;
    bool predictive_scroll_triggered_this_input_idx;
    int y_offset_due_to_prediction_for_current_idx;

} AppContext;

bool InitializeApp(AppContext *appCtx, const char* title);
void CleanupApp(AppContext *appCtx);

#endif // APP_CONTEXT_H