#ifndef FILE_PATHS_H
#define FILE_PATHS_H

#include "app_context.h" // For AppContext (if log_file_handle is used here)
#include <stddef.h>      // For size_t

// Maximum file path length
#define MAX_PATH_LEN 1024

// Structure for storing paths
typedef struct {
    char actual_text_file_path[MAX_PATH_LEN];
    char actual_stats_file_path[MAX_PATH_LEN];
    char default_text_file_in_bundle_path[MAX_PATH_LEN];
} FilePaths;

// Path initialization
void InitializeFilePaths(AppContext *appCtx, FilePaths *paths);

// Loading initial text
// Returns a pointer to a dynamically allocated buffer with text,
// or NULL in case of error. out_raw_text_len is set accordingly.
char* LoadInitialText(AppContext *appCtx, FilePaths *paths, size_t* out_raw_text_len);

// Saving remaining text
void SaveRemainingText(AppContext *appCtx, FilePaths *paths,
                       const char *text_to_type, size_t final_text_len,
                       size_t current_input_byte_idx);

#endif // FILE_PATHS_H