#include "file_paths.h"
#include "config.h" // For TEXT_FILE_PATH_BASENAME, STATS_FILE_BASENAME, COMPANY_NAME_STR, PROJECT_NAME_STR, MAX_TEXT_LEN
#include <SDL2/SDL_filesystem.h> // For SDL_GetPrefPath, SDL_GetBasePath
#include <stdio.h>  // For snprintf, fopen, fclose, fread, fwrite, fseek, ftell, perror
#include <string.h> // For strcpy, strncpy, strlen, strerror, strdup
#include <stdlib.h> // For malloc, free
#include <errno.h>  // For errno

#ifdef _WIN32
#include <windows.h> // For MultiByteToWideChar
#include <wchar.h>   // For wchar_t
#endif

FILE* fopen_unicode_path(const char *utf8_path, const char *mode) {
#ifdef _WIN32
    if (!utf8_path || !mode) {
        return NULL;
    }

    // Convert the file open mode (mode) to wchar_t*
    wchar_t w_mode[10] = {0}; // Sufficient for standard modes "r", "w", "a", "rb", etc.
    // MultiByteToWideChar(CP_ACP, 0, mode, -1, w_mode, sizeof(w_mode)/sizeof(w_mode[0])); // If mode is also UTF-8
    // Or, if mode is always ASCII, it can be simpler:
    size_t mode_len = strlen(mode);
    if (mode_len >= sizeof(w_mode)/sizeof(w_mode[0])) { // Buffer overflow check
        return NULL;
    }
    for (size_t i = 0; i <= mode_len; ++i) { // <= to copy the null-terminator
        w_mode[i] = (wchar_t)mode[i];
    }


    // Determine the required buffer size for the UTF-16 path
    int required_wchars = MultiByteToWideChar(CP_UTF8, 0, utf8_path, -1, NULL, 0);
    if (required_wchars == 0) {
        // Conversion error, GetLastError() can be logged
        // fprintf(stderr, "MultiByteToWideChar (size check) failed for path %s: %lu\n", utf8_path, GetLastError());
        return NULL; // Or fall back to fopen(utf8_path, mode) as a last resort, although this brings back the original problem
    }

    wchar_t *w_path = (wchar_t *)malloc(required_wchars * sizeof(wchar_t));
    if (!w_path) {
        // Memory allocation error
        // perror("malloc for w_path failed");
        return NULL;
    }

    // Convert the UTF-8 path to UTF-16
    if (MultiByteToWideChar(CP_UTF8, 0, utf8_path, -1, w_path, required_wchars) == 0) {
        // Conversion error
        // fprintf(stderr, "MultiByteToWideChar (conversion) failed for path %s: %lu\n", utf8_path, GetLastError());
        free(w_path);
        return NULL;
    }

    FILE *file = _wfopen(w_path, w_mode);
    // if (!file) {
    //     // wchar_t err_buf[256];
    //     // if (_wcserror_s(err_buf, sizeof(err_buf)/sizeof(err_buf[0]), errno) == 0) {
    //     //     fwprintf(stderr, L"_wfopen failed for %s: %s (errno %d)\n", w_path, err_buf, errno);
    //     // } else {
    //     //     fwprintf(stderr, L"_wfopen failed for %s (errno %d)\n", w_path, errno);
    //     // }
    // }
    free(w_path);
    return file;

#else
    // For non-Windows platforms, use standard fopen
    return fopen(utf8_path, mode);
#endif
}

// Helper function for logging
static void log_paths_message_format(AppContext *appCtx, const char* format, ...) {
    if (appCtx && appCtx->log_file_handle && format) {
        va_list args;
        va_start(args, format);
        vfprintf(appCtx->log_file_handle, format, args);
        va_end(args);
        fprintf(appCtx->log_file_handle, "\n");
        fflush(appCtx->log_file_handle);
    }
}

void InitializeFilePaths(AppContext *appCtx, FilePaths *paths) {
    if (!paths) return;

    paths->actual_text_file_path[0] = '\0';
    paths->actual_stats_file_path[0] = '\0';
    paths->default_text_file_in_bundle_path[0] = '\0';

    // Determining paths for user files (text.txt, stats.txt)
    char* pref_path_str = SDL_GetPrefPath(COMPANY_NAME_STR, PROJECT_NAME_STR);
    if (pref_path_str) {
        snprintf(paths->actual_text_file_path, MAX_PATH_LEN -1, "%s%s", pref_path_str, TEXT_FILE_PATH_BASENAME);
        snprintf(paths->actual_stats_file_path, MAX_PATH_LEN -1, "%s%s", pref_path_str, STATS_FILE_BASENAME);
        paths->actual_text_file_path[MAX_PATH_LEN-1] = '\0';
        paths->actual_stats_file_path[MAX_PATH_LEN-1] = '\0';

        log_paths_message_format(appCtx, "User data directory (from SDL_GetPrefPath): %s", pref_path_str);
        log_paths_message_format(appCtx, "User text file path set to: %s", paths->actual_text_file_path);
        log_paths_message_format(appCtx, "User stats file path set to: %s", paths->actual_stats_file_path);
        SDL_free(pref_path_str);
    } else {
        log_paths_message_format(appCtx, "Warning: SDL_GetPrefPath() failed: %s. Falling back for user data paths.", SDL_GetError());
        char* base_path_fallback = SDL_GetBasePath();
        if (base_path_fallback) {
            snprintf(paths->actual_text_file_path, MAX_PATH_LEN - 1, "%s%s", base_path_fallback, TEXT_FILE_PATH_BASENAME);
            snprintf(paths->actual_stats_file_path, MAX_PATH_LEN - 1, "%s%s", base_path_fallback, STATS_FILE_BASENAME);
            paths->actual_text_file_path[MAX_PATH_LEN-1] = '\0';
            paths->actual_stats_file_path[MAX_PATH_LEN-1] = '\0';
            log_paths_message_format(appCtx, "Base path (from SDL_GetBasePath for fallback): %s", base_path_fallback);
            SDL_free(base_path_fallback);
        } else {
            log_paths_message_format(appCtx, "Warning: SDL_GetBasePath() also failed: %s. Using CWD for data files.", SDL_GetError());
            strncpy(paths->actual_text_file_path, TEXT_FILE_PATH_BASENAME, MAX_PATH_LEN - 1); paths->actual_text_file_path[MAX_PATH_LEN-1] = '\0';
            strncpy(paths->actual_stats_file_path, STATS_FILE_BASENAME, MAX_PATH_LEN - 1); paths->actual_stats_file_path[MAX_PATH_LEN-1] = '\0';
        }
        log_paths_message_format(appCtx, "Fallback user text file path: %s", paths->actual_text_file_path);
        log_paths_message_format(appCtx, "Fallback user stats file path: %s", paths->actual_stats_file_path);
    }

    // Determining the path to the default text.txt in the application package/directory
    char* bundle_resources_path_base = SDL_GetBasePath();
    if (bundle_resources_path_base) {
        #if defined(__APPLE__)
            // In a macOS bundle, resources are located in MyCoolApp.app/Contents/Resources/
            snprintf(paths->default_text_file_in_bundle_path, MAX_PATH_LEN -1, "%s../Resources/%s", bundle_resources_path_base, TEXT_FILE_PATH_BASENAME);
        #elif defined(_WIN32)
            // For Windows, if installed, it might be one level up, or next to the .exe for local builds
            snprintf(paths->default_text_file_in_bundle_path, MAX_PATH_LEN -1, "%s../%s", bundle_resources_path_base, TEXT_FILE_PATH_BASENAME); // Attempt for installation
            FILE *test_f = fopen(paths->default_text_file_in_bundle_path, "rb"); // Note: This fopen might still use the old logic if not yet replaced
            if (test_f) {
                fclose(test_f);
                log_paths_message_format(appCtx, "INFO: Found default text.txt at potential installed location: %s", paths->default_text_file_in_bundle_path);
            } else { // If not found, try next to the .exe (for development/portable version)
                snprintf(paths->default_text_file_in_bundle_path, MAX_PATH_LEN -1, "%s%s", bundle_resources_path_base, TEXT_FILE_PATH_BASENAME);
                 log_paths_message_format(appCtx, "INFO: Default text.txt not found at '%s../%s'. Trying '%s%s'.", bundle_resources_path_base, TEXT_FILE_PATH_BASENAME, bundle_resources_path_base, TEXT_FILE_PATH_BASENAME);
            }
        #elif defined(__linux__)
            // For Linux, usually next to the executable file
            snprintf(paths->default_text_file_in_bundle_path, MAX_PATH_LEN -1, "%s%s", bundle_resources_path_base, TEXT_FILE_PATH_BASENAME);
        #else // Other systems
            snprintf(paths->default_text_file_in_bundle_path, MAX_PATH_LEN -1, "%s%s", bundle_resources_path_base, TEXT_FILE_PATH_BASENAME);
        #endif
        paths->default_text_file_in_bundle_path[MAX_PATH_LEN-1] = '\0';
        log_paths_message_format(appCtx, "Base path (for default_text_file_in_bundle logic): %s", bundle_resources_path_base);
        SDL_free(bundle_resources_path_base);
    } else {
        strncpy(paths->default_text_file_in_bundle_path, TEXT_FILE_PATH_BASENAME, MAX_PATH_LEN -1); // Fallback option - current directory
        paths->default_text_file_in_bundle_path[MAX_PATH_LEN-1] = '\0';
        log_paths_message_format(appCtx, "Warning: SDL_GetBasePath() failed for determining default text.txt path. Using CWD: %s", paths->default_text_file_in_bundle_path);
    }
    log_paths_message_format(appCtx, "Path to default text.txt (source for initial copy): %s", paths->default_text_file_in_bundle_path);
    if (appCtx && appCtx->log_file_handle) fflush(appCtx->log_file_handle);
}


char* LoadInitialText(AppContext *appCtx, FilePaths *paths, size_t* out_raw_text_len) {
    if (!paths || !out_raw_text_len) { if(out_raw_text_len) *out_raw_text_len = 0; return NULL; }
    *out_raw_text_len = 0;
    char *raw_text_content = NULL;

    // 1. Try to open the user file
    FILE *text_file_handle = fopen_unicode_path(paths->actual_text_file_path, "rb"); // USING THE NEW FUNCTION

    if (!text_file_handle) { // User file not found
        log_paths_message_format(appCtx, "User-specific text.txt not found at '%s'. Attempting to copy from default: '%s'. Error (user file): %s",
                                 paths->actual_text_file_path, paths->default_text_file_in_bundle_path, strerror(errno)); // errno might be less relevant if _wfopen failed

        FILE *default_file_handle = fopen_unicode_path(paths->default_text_file_in_bundle_path, "rb"); // USING THE NEW FUNCTION
        if (default_file_handle) { // Default file found
            fseek(default_file_handle, 0, SEEK_END);
            long default_file_size_long = ftell(default_file_handle);

            if (default_file_size_long > 0 && default_file_size_long < MAX_TEXT_LEN) {
                size_t default_file_size = (size_t)default_file_size_long;
                char* temp_copy_buffer = (char*)malloc(default_file_size + 1);
                if (temp_copy_buffer) {
                    fseek(default_file_handle, 0, SEEK_SET);
                    if (fread(temp_copy_buffer, 1, default_file_size, default_file_handle) == default_file_size) {
                        temp_copy_buffer[default_file_size] = '\0'; // Null-termination for strdup
                        // Copy content to the user file
                        FILE* user_file_write_handle = fopen_unicode_path(paths->actual_text_file_path, "wb"); // USING THE NEW FUNCTION
                        if (user_file_write_handle) {
                            if (fwrite(temp_copy_buffer, 1, default_file_size, user_file_write_handle) == default_file_size) {
                                log_paths_message_format(appCtx, "Successfully copied default text to user's path '%s'", paths->actual_text_file_path);
                                raw_text_content = strdup(temp_copy_buffer); // Use the copied text
                                if(raw_text_content) *out_raw_text_len = default_file_size; else log_paths_message_format(appCtx, "Error: strdup failed for copied text.");
                            } else {
                                log_paths_message_format(appCtx, "Error writing copied text to user's path '%s': %s", paths->actual_text_file_path, strerror(errno));
                            }
                            fclose(user_file_write_handle);
                        } else {
                            log_paths_message_format(appCtx, "Error: Could not open user text file '%s' for writing the copy: %s", paths->actual_text_file_path, strerror(errno));
                        }
                    } else {
                        log_paths_message_format(appCtx, "Error reading content from default text file '%s': %s", paths->default_text_file_in_bundle_path, strerror(errno));
                    }
                    free(temp_copy_buffer);
                } else { log_paths_message_format(appCtx, "Error: malloc failed for default text copy buffer."); }
            } else {
                if (default_file_size_long <= 0) log_paths_message_format(appCtx, "Default text file '%s' is empty or size error (Size: %ld).", paths->default_text_file_in_bundle_path, default_file_size_long);
                else log_paths_message_format(appCtx, "Default text file '%s' is too large (Size: %ld, Max: %d).", paths->default_text_file_in_bundle_path, default_file_size_long, MAX_TEXT_LEN);
            }
            fclose(default_file_handle);
        } else { // Default file also not found
            log_paths_message_format(appCtx, "Error: Default text file '%s' also not found/readable: %s.", paths->default_text_file_in_bundle_path, strerror(errno));
        }
    } else { // User file successfully opened
        log_paths_message_format(appCtx, "Successfully opened existing user text file: %s", paths->actual_text_file_path);
        fseek(text_file_handle, 0, SEEK_END);
        long file_size_long = ftell(text_file_handle);

        if (file_size_long > 0 && file_size_long < MAX_TEXT_LEN) {
            *out_raw_text_len = (size_t)file_size_long;
            fseek(text_file_handle, 0, SEEK_SET);
            raw_text_content = (char*)malloc(*out_raw_text_len + 1);
            if (raw_text_content) {
                size_t bytes_read = fread(raw_text_content, 1, *out_raw_text_len, text_file_handle);
                if (bytes_read != *out_raw_text_len) {
                    log_paths_message_format(appCtx, "WARN: fread mismatch from '%s'. Expected %zu, got %zu. Error: %s", paths->actual_text_file_path, *out_raw_text_len, bytes_read, strerror(errno));
                    *out_raw_text_len = bytes_read; // Update to actually read bytes
                }
                raw_text_content[*out_raw_text_len] = '\0';
            } else {
                log_paths_message_format(appCtx, "Error: malloc failed for text content from '%s': %s", paths->actual_text_file_path, strerror(errno));
                *out_raw_text_len = 0;
            }
        } else if (file_size_long == 0) {
            log_paths_message_format(appCtx, "User text file '%s' is empty.", paths->actual_text_file_path);
            // raw_text_content remains NULL, *out_raw_text_len = 0
        } else {
            log_paths_message_format(appCtx, "Error: File '%s' size error (%ld bytes) or too large (Max: %d).", paths->actual_text_file_path, file_size_long, MAX_TEXT_LEN);
            // raw_text_content remains NULL, *out_raw_text_len = 0
        }
        fclose(text_file_handle);
    }

    // If text is not loaded after all attempts (or the file is empty), use placeholder
    if (!raw_text_content || *out_raw_text_len == 0) {
        if (raw_text_content) { free(raw_text_content); raw_text_content = NULL; } // If it was empty but allocated
        log_paths_message_format(appCtx, "Text file is empty or could not be loaded. Initializing with placeholder text.");

        const char* placeholder_text_str = NULL;
        #if defined(_WIN32)
            placeholder_text_str = "Press \"Left alt\" + \"Right alt\" and then \"t\" to add your own text";
        #elif defined(__APPLE__)
            placeholder_text_str = "Press \"Left CMD\" + \"Right CMD\" and then \"t\" to add your own text";
        #else
            placeholder_text_str = "Pause (LAlt+RAlt or LCmd+RCmd) then 't' to add your own text";
        #endif

        raw_text_content = strdup(placeholder_text_str);
        if (!raw_text_content) {
            log_paths_message_format(appCtx, "CRITICAL: strdup for placeholder text failed.");
            *out_raw_text_len = 0; return NULL;
        }
        *out_raw_text_len = strlen(raw_text_content);

        // Attempt to write placeholder to the user file if the path is known
        if (paths->actual_text_file_path[0] != '\0') {
            FILE* user_file_write_placeholder = fopen_unicode_path(paths->actual_text_file_path, "wb"); // USING THE NEW FUNCTION
            if (user_file_write_placeholder) {
                if (fwrite(raw_text_content, 1, *out_raw_text_len, user_file_write_placeholder) == *out_raw_text_len) {
                    log_paths_message_format(appCtx, "Successfully wrote placeholder text to user's path '%s'", paths->actual_text_file_path);
                } else {
                    log_paths_message_format(appCtx, "Error writing placeholder text to user's path '%s': %s", paths->actual_text_file_path, strerror(errno));
                }
                fclose(user_file_write_placeholder);
            } else {
                log_paths_message_format(appCtx, "Error: Could not open user text file '%s' for writing placeholder: %s", paths->actual_text_file_path, strerror(errno));
            }
        }
    }
    if (appCtx && appCtx->log_file_handle) fflush(appCtx->log_file_handle);
    return raw_text_content;
}

void SaveRemainingText(AppContext *appCtx, FilePaths *paths,
                       const char *text_to_type, size_t final_text_len,
                       size_t current_input_byte_idx) {
    if (!appCtx || !paths || !text_to_type || !appCtx->typing_started) {
        log_paths_message_format(appCtx, "SaveRemainingText: Conditions not met for saving (appCtx null, paths null, text_to_type null, or typing not started).");
        return;
    }

    if (current_input_byte_idx > 0 && current_input_byte_idx <= final_text_len) {
        if (paths->actual_text_file_path[0] != '\0') {
            FILE *output_file_handle = fopen_unicode_path(paths->actual_text_file_path, "w"); // Open for writing (overwrite) // USING THE NEW FUNCTION
            if (output_file_handle) {
                const char *remaining_text_ptr = text_to_type + current_input_byte_idx;
                size_t remaining_len = final_text_len - current_input_byte_idx;

                if (remaining_len > 0) {
                    if (fwrite(remaining_text_ptr, 1, remaining_len, output_file_handle) != remaining_len) {
                        log_paths_message_format(appCtx, "ERROR: Failed to write all remaining text to '%s'. Error: %s", paths->actual_text_file_path, strerror(errno));
                        perror("Error writing remaining text to file");
                    } else {
                        log_paths_message_format(appCtx, "Successfully wrote remaining %zu bytes of text to '%s'.", remaining_len, paths->actual_text_file_path);
                    }
                } else { // All text has been typed
                    log_paths_message_format(appCtx, "All text processed. User text file '%s' is now effectively empty (or will be truncated).", paths->actual_text_file_path);
                    // fwrite with remaining_len=0 will write nothing, the file will be empty
                }
                fclose(output_file_handle);
            } else {
                log_paths_message_format(appCtx, "ERROR: Could not open user text file '%s' for writing remaining text. Error: %s", paths->actual_text_file_path, strerror(errno));
                perror("Error opening text file for writing remaining text");
            }
        } else {
            log_paths_message_format(appCtx, "ERROR: User text file path is empty. Cannot update text file with remaining text.");
        }
    } else {
        char temp_log_path_buffer[MAX_PATH_LEN];
        snprintf(temp_log_path_buffer, MAX_PATH_LEN -1, "%s", paths->actual_text_file_path[0] ? paths->actual_text_file_path : "UNKNOWN_PATH");
        temp_log_path_buffer[MAX_PATH_LEN-1] = '\0';

        if (current_input_byte_idx == 0) {
            log_paths_message_format(appCtx, "No text processed (0 bytes typed). Text file '%s' not modified by SaveRemainingText.", temp_log_path_buffer);
        } else { // current_input_byte_idx > final_text_len (unlikely, but possible with errors)
            log_paths_message_format(appCtx, "WARN: current_input_byte_idx (%zu) > final_text_len (%zu). Text file '%s' not modified by SaveRemainingText.", current_input_byte_idx, final_text_len, temp_log_path_buffer);
        }
    }
}