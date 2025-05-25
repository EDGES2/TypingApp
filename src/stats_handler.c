#include "stats_handler.h"
#include "config.h" // Можливо, для якихось констант, пов'язаних зі статистикою
#include <stdio.h>  // Для printf, fprintf, fopen, fclose, snprintf
#include <time.h>   // Для time, strftime, localtime
#include <string.h> // Для strerror
#include <errno.h>  // Для errno

// Допоміжна функція для логування
static void log_stats_message_format(AppContext *appCtx, const char* format, ...) {
    if (appCtx && appCtx->log_file_handle && format) {
        va_list args;
        va_start(args, format);
        vfprintf(appCtx->log_file_handle, format, args);
        va_end(args);
        fprintf(appCtx->log_file_handle, "\n");
        fflush(appCtx->log_file_handle);
    }
}


void CalculateAndPrintAppStats(AppContext *appCtx,
                               const char* actual_stats_f_path) {
    if (!appCtx) return;

    if (!appCtx->typing_started) {
        printf("No typing done. No stats to display.\n");
        log_stats_message_format(appCtx, "CalculateAndPrintAppStats: No typing done.");
        return;
    }

    // Визначаємо час закінчення набору
    Uint32 end_time_ms_val = appCtx->is_paused ? appCtx->time_at_pause_ms : SDL_GetTicks();
    float time_taken_seconds = (float)(end_time_ms_val - appCtx->start_time_ms) / 1000.0f;

    // Перевірка на валідність часу та натискань
    if (time_taken_seconds <= 0.001f && appCtx->total_keystrokes_for_accuracy == 0) {
        printf("Time taken is too short or no characters typed for meaningful stats.\n");
        log_stats_message_format(appCtx, "CalculateAndPrintAppStats: Time too short or no keystrokes.");
        return;
    }
    if (time_taken_seconds <= 0.001f) time_taken_seconds = 0.001f; // Мінімальний час для уникнення ділення на нуль

    // Розрахунок статистики
    size_t final_correct_keystrokes = (appCtx->total_keystrokes_for_accuracy >= appCtx->total_errors_committed_for_accuracy) ?
                                     (appCtx->total_keystrokes_for_accuracy - appCtx->total_errors_committed_for_accuracy) : 0;

    float net_words = (float)final_correct_keystrokes / 5.0f; // WPM базується на 5 символах на слово
    float wpm = (time_taken_seconds > 0.0001f) ? (net_words / (time_taken_seconds / 60.0f)) : 0.0f;
    if (wpm < 0.0f) wpm = 0.0f; // WPM не може бути від'ємним

    float accuracy = 0.0f;
    if (appCtx->total_keystrokes_for_accuracy > 0) {
        accuracy = ((float)final_correct_keystrokes / (float)appCtx->total_keystrokes_for_accuracy) * 100.0f;
    }
    if (accuracy < 0.0f) accuracy = 0.0f;
    if (accuracy > 100.0f && appCtx->total_keystrokes_for_accuracy > 0) accuracy = 100.0f; // Обмеження зверху

    // Виведення статистики в консоль
    printf("\n--- Typing Stats (Final) ---\n");
    printf("Time Taken: %.2f seconds\n", time_taken_seconds);
    printf("WPM (Net): %.2f\n", wpm);
    printf("Correct Keystrokes: %zu\n", final_correct_keystrokes);
    printf("Total Keystrokes (Accuracy Basis): %llu\n", appCtx->total_keystrokes_for_accuracy);
    printf("Committed Errors: %llu\n", appCtx->total_errors_committed_for_accuracy);
    printf("Accuracy (Keystroke-based): %.2f%%\n", accuracy);
    printf("--------------------\n");

    // Запис статистики у файл
    if (actual_stats_f_path && actual_stats_f_path[0] != '\0') {
        FILE *stats_file_handle = fopen(actual_stats_f_path, "a"); // Відкриваємо для дозапису
        if (stats_file_handle) {
            time_t now = time(NULL);
            char time_str[26]; // Буфер для мітки часу
            if (strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now)) == 0) {
                strcpy(time_str, "TimestampError"); // Якщо strftime не вдалося
            }

            fprintf(stats_file_handle, "%s | WPM: %.2f | Accuracy: %.2f%% | Time: %.1fs | Correct Ks: %zu | Total Ks: %llu | Errors: %llu\n",
                    time_str, wpm, accuracy, time_taken_seconds,
                    final_correct_keystrokes, appCtx->total_keystrokes_for_accuracy, appCtx->total_errors_committed_for_accuracy);
            fclose(stats_file_handle);
            log_stats_message_format(appCtx, "Stats successfully appended to '%s'", actual_stats_f_path);
        } else {
            log_stats_message_format(appCtx, "ERROR: Failed to open/append to stats file '%s': %s", actual_stats_f_path, strerror(errno));
            perror("Failed to open stats file for appending");
            fprintf(stderr, "Could not open or create stats file at: %s\n", actual_stats_f_path);
        }
    } else {
        log_stats_message_format(appCtx, "Warning: Stats file path is empty. Cannot save stats to file.");
    }
}