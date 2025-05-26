#ifndef TEXT_PROCESSING_H
#define TEXT_PROCESSING_H

#include "app_context.h" // Needed for AppContext
#include <stdbool.h>
#include <stddef.h> // For size_t

// Structure for text block information (word, space, line break)
typedef struct {
    const char* start_ptr; // Pointer to the beginning of the block in the original text
    size_t num_bytes;      // Number of bytes in the block
    int pixel_width;       // Block width in pixels
    bool is_word;          // Is the block a word
    bool is_newline;       // Is the block a newline character
    bool is_tab;           // Is the block a tab character
} TextBlockInfo;

char* PreprocessText(AppContext *appCtx, const char* raw_text_buffer, size_t raw_text_len, size_t* out_final_text_len);

int get_codepoint_advance_and_metrics_func(AppContext *appCtx, Uint32 codepoint, int fallback_adv, int *out_char_w, int *out_char_h);

TextBlockInfo get_next_text_block_func(AppContext *appCtx, const char **text_parser_ptr_ref, const char *text_end, int current_pen_x_for_tab_calc);

#endif // TEXT_PROCESSING_H