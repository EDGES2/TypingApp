#ifndef TEXT_PROCESSING_H
#define TEXT_PROCESSING_H

#include "app_context.h" // Потрібен для AppContext
#include <stdbool.h>
#include <stddef.h> // For size_t

// Структура для інформації про блок тексту (слово, пробіл, перенос рядка)
typedef struct {
    const char* start_ptr; // Вказівник на початок блоку в оригінальному тексті
    size_t num_bytes;      // Кількість байт у блоці
    int pixel_width;       // Ширина блоку в пікселях
    bool is_word;          // Чи є блок словом
    bool is_newline;       // Чи є блок символом нового рядка
    bool is_tab;           // Чи є блок символом табуляції
} TextBlockInfo;

char* PreprocessText(AppContext *appCtx, const char* raw_text_buffer, size_t raw_text_len, size_t* out_final_text_len);

int get_codepoint_advance_and_metrics_func(AppContext *appCtx, Uint32 codepoint, int fallback_adv, int *out_char_w, int *out_char_h);

TextBlockInfo get_next_text_block_func(AppContext *appCtx, const char **text_parser_ptr_ref, const char *text_end, int current_pen_x_for_tab_calc);

#endif // TEXT_PROCESSING_H