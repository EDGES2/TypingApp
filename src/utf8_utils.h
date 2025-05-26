#ifndef UTF8_UTILS_H
#define UTF8_UTILS_H

#include <SDL2/SDL_stdinc.h> // For Sint32, size_t

Sint32 decode_utf8(const char **s_ptr, const char *s_end_const_char);
size_t CountUTF8Chars(const char* text, size_t text_byte_len);

#endif // UTF8_UTILS_H