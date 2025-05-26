#ifndef EVENT_HANDLER_H
#define EVENT_HANDLER_H

#include "app_context.h"
#include <SDL2/SDL_events.h> // For SDL_Event

// File paths are needed to open files, so pass them
void HandleAppEvents(AppContext *appCtx, SDL_Event *event,
                     size_t *current_input_byte_idx,
                     char *input_buffer, size_t final_text_len,
                     const char* text_to_type,
                     bool *quit_flag,
                     const char* actual_text_f_path,
                     const char* actual_stats_f_path);

#endif // EVENT_HANDLER_H