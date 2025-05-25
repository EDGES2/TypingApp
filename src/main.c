#define SDL_MAIN_HANDLED // Потрібно, якщо main() не в SDL форматі

#include "config.h"
#include "app_context.h"
#include "file_paths.h"
#include "text_processing.h"
#include "event_handler.h"
#include "layout_logic.h"
#include "rendering.h"
#include "stats_handler.h"

#include <SDL2/SDL.h> // Для SDL_Delay, SDL_GetTicks, SDL_StartTextInput, SDL_StopTextInput
#include <stdio.h>    // Для perror
#include <stdlib.h>   // Для free, calloc

int main(int argc, char **argv) {
    (void)argc; (void)argv; // Прибираємо попередження про невикористані параметри

    AppContext appCtx = {0}; // Ініціалізуємо нулями
    FilePaths filePaths = {0}; // Ініціалізуємо шляхи нулями

    // Ініціалізація SDL, TTF, вікна, рендерера, шрифту, лог-файлу тощо.
    // Log-файл ініціалізується всередині InitializeApp
    if (!InitializeApp(&appCtx, PROJECT_NAME_STR)) {
        // Помилка вже залогована всередині InitializeApp
        return 1;
    }
    // Логування після успішної ініціалізації контексту
    if (appCtx.log_file_handle) {
         fprintf(appCtx.log_file_handle, "Main function started. AppContext initialized.\n");
         fflush(appCtx.log_file_handle);
    }


    InitializeFilePaths(&appCtx, &filePaths); // Ініціалізуємо шляхи до файлів

    size_t raw_text_len = 0;
    char *raw_text_content = LoadInitialText(&appCtx, &filePaths, &raw_text_len);
    if (!raw_text_content) {
        if (appCtx.log_file_handle) fprintf(appCtx.log_file_handle, "CRITICAL: Failed to load initial text content in main.\n");
        CleanupApp(&appCtx);
        return 1;
    }

    size_t final_text_len = 0;
    char *text_to_type = PreprocessText(&appCtx, raw_text_content, raw_text_len, &final_text_len);
    free(raw_text_content); // raw_text_content більше не потрібен
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


    // Буфер для введеного користувачем тексту. +100 для невеликого запасу.
    char *input_buffer = (char*)calloc(final_text_len + 100, 1);
    if (!input_buffer && (final_text_len + 100 > 0)) { // Перевірка, чи calloc не повернув NULL
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

    SDL_StartTextInput(); // Починаємо приймати текстове введення

    // Головний цикл програми
    while (!quit_game_flag) {
        SDL_Event event;
        size_t old_input_idx = current_input_byte_idx; // Для перевірки зміни індексу вводу

        HandleAppEvents(&appCtx, &event, &current_input_byte_idx, input_buffer,
                        final_text_len, text_to_type, &quit_game_flag,
                        filePaths.actual_text_file_path, filePaths.actual_stats_file_path);

        if (quit_game_flag) break;

        // Якщо індекс вводу змінився, скидаємо прапорці предиктивного скролу
        if (current_input_byte_idx != old_input_idx) {
            appCtx.predictive_scroll_triggered_this_input_idx = false;
            appCtx.y_offset_due_to_prediction_for_current_idx = 0;
        }

        // Оновлення стану блимання курсора
        if (!appCtx.is_paused && SDL_GetTicks() - last_blink_time > 500) {
            show_cursor_flag = !show_cursor_flag;
            last_blink_time = SDL_GetTicks();
        } else if (appCtx.is_paused) {
            show_cursor_flag = true; // Курсор завжди видимий на паузі
        }

        // Очищення екрану
        SDL_SetRenderDrawColor(appCtx.ren, appCtx.palette[COL_BG].r, appCtx.palette[COL_BG].g, appCtx.palette[COL_BG].b, appCtx.palette[COL_BG].a);
        SDL_RenderClear(appCtx.ren);

        // Рендеринг таймера та отримання його розмірів
        int timer_h = 0, timer_w = 0;
        RenderAppTimer(&appCtx, &timer_h, &timer_w);

        // Рендеринг живої статистики
        RenderLiveStats(&appCtx, input_buffer, current_input_byte_idx,
                        TEXT_AREA_X, timer_w, TEXT_AREA_PADDING_Y, timer_h);

        // Визначення верхньої координати текстового поля
        int text_viewport_top_y = TEXT_AREA_PADDING_Y + timer_h + TEXT_AREA_PADDING_Y;

        // Розрахунок логічної позиції курсора
        int logical_cursor_abs_y = 0; // Y координата початку рядка курсора
        int logical_cursor_x_on_line = 0; // X координата курсора на його рядку
        CalculateCursorLayout(&appCtx, text_to_type, final_text_len, current_input_byte_idx,
                              &logical_cursor_abs_y, &logical_cursor_x_on_line);

        // Оновлення видимої області тексту (скролінг)
        // predictive_scroll_triggered та y_offset_due_to_prediction встановлюються всередині PerformPredictiveScrollUpdate
        PerformPredictiveScrollUpdate(&appCtx, text_to_type, final_text_len, current_input_byte_idx, logical_cursor_abs_y);


        // Рендеринг текстового контенту та отримання фінальних координат для малювання курсора
        int final_cursor_draw_x = -100, final_cursor_draw_y_baseline = -100;
        RenderTextContent(&appCtx, text_to_type, final_text_len, input_buffer,
                          current_input_byte_idx,
                          text_viewport_top_y, &final_cursor_draw_x, &final_cursor_draw_y_baseline);

        // Рендеринг курсора
        RenderAppCursor(&appCtx, show_cursor_flag, final_cursor_draw_x, final_cursor_draw_y_baseline, text_viewport_top_y);

        SDL_RenderPresent(appCtx.ren); // Оновлення екрану
        SDL_Delay(16); // Обмеження FPS (приблизно 60 FPS)
    }

    SDL_StopTextInput(); // Зупиняємо приймання текстового введення

    // Обчислення та збереження фінальної статистики
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

    // Звільнення ресурсів
    if (text_to_type) free(text_to_type);
    if (input_buffer) free(input_buffer);
    CleanupApp(&appCtx); // Звільняє SDL, TTF, шрифт, текстури, закриває лог-файл

    return 0;
}