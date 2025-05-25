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
    int line_h; // Висота одного рядка тексту

    // Кеш гліфів для ASCII символів (32-126)
    SDL_Texture *glyph_tex_cache[N_COLORS][128];
    int glyph_adv_cache[128]; // Ширина просування (advance)
    int glyph_w_cache[N_COLORS][128]; // Ширина гліфа
    int glyph_h_cache[N_COLORS][128]; // Висота гліфа

    int space_advance_width; // Ширина просування для пробілу
    int tab_width_pixels;    // Ширина табуляції в пікселях

    // Стан програми
    bool typing_started;
    Uint32 start_time_ms;
    Uint32 time_at_pause_ms; // Час, коли була натиснута пауза
    bool is_paused;
    bool l_modifier_held; // LAlt або LCmd
    bool r_modifier_held; // RAlt або RCmd

    // Статистика
    unsigned long long total_keystrokes_for_accuracy;
    unsigned long long total_errors_committed_for_accuracy;

    // Для логування
    FILE *log_file_handle;

    // Для відображення та прокрутки
    int first_visible_abs_line_num;
    bool predictive_scroll_triggered_this_input_idx;
    int y_offset_due_to_prediction_for_current_idx;

} AppContext;

bool InitializeApp(AppContext *appCtx, const char* title);
void CleanupApp(AppContext *appCtx);

#endif // APP_CONTEXT_H