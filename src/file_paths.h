#ifndef FILE_PATHS_H
#define FILE_PATHS_H

#include "app_context.h" // Для AppContext (якщо log_file_handle використовується тут)
#include <stddef.h>      // Для size_t

// Максимальна довжина шляху до файлу
#define MAX_PATH_LEN 1024

// Структура для зберігання шляхів
typedef struct {
    char actual_text_file_path[MAX_PATH_LEN];
    char actual_stats_file_path[MAX_PATH_LEN];
    char default_text_file_in_bundle_path[MAX_PATH_LEN];
} FilePaths;

// Ініціалізація шляхів
void InitializeFilePaths(AppContext *appCtx, FilePaths *paths);

// Завантаження початкового тексту
// Повертає вказівник на динамічно виділений буфер з текстом,
// або NULL у випадку помилки. out_raw_text_len встановлюється відповідно.
char* LoadInitialText(AppContext *appCtx, FilePaths *paths, size_t* out_raw_text_len);

// Збереження залишку тексту
void SaveRemainingText(AppContext *appCtx, FilePaths *paths,
                       const char *text_to_type, size_t final_text_len,
                       size_t current_input_byte_idx);

#endif // FILE_PATHS_H