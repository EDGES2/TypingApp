#include "utf8_utils.h"

Sint32 decode_utf8(const char **s_ptr, const char *s_end_const_char) {
    if (!s_ptr || !*s_ptr || *s_ptr >= s_end_const_char) return 0; // End of string or invalid pointer
    const unsigned char *s = (const unsigned char *)*s_ptr;
    const unsigned char *s_end = (const unsigned char *)s_end_const_char;
    unsigned char c1 = *s;
    Sint32 codepoint;
    int len = 0;

    if (c1 < 0x80) { // 1-byte character (0xxxxxxx)
        codepoint = c1;
        len = 1;
    } else if ((c1 & 0xE0) == 0xC0) { // 2-byte character (110xxxxx 10xxxxxx)
        if (s + 1 >= s_end || (s[1] & 0xC0) != 0x80) return -1; // Invalid sequence
        codepoint = ((Sint32)(c1 & 0x1F) << 6) | (Sint32)(s[1] & 0x3F);
        len = 2;
    } else if ((c1 & 0xF0) == 0xE0) { // 3-byte character (1110xxxx 10xxxxxx 10xxxxxx)
        if (s + 2 >= s_end || (s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80) return -1;
        codepoint = ((Sint32)(c1 & 0x0F) << 12) | ((Sint32)(s[1] & 0x3F) << 6) | (Sint32)(s[2] & 0x3F);
        len = 3;
    } else if ((c1 & 0xF8) == 0xF0) { // 4-byte character (11110xxx 10xxxxxx 10xxxxxx 10xxxxxx)
        if (s + 3 >= s_end || (s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 || (s[3] & 0xC0) != 0x80) return -1;
        codepoint = ((Sint32)(c1 & 0x07) << 18) | ((Sint32)(s[1] & 0x3F) << 12) | ((Sint32)(s[2] & 0x3F) << 6) | (Sint32)(s[3] & 0x3F);
        len = 4;
    } else { // Invalid starting byte
        return -1;
    }

    (*s_ptr) += len; // Move the pointer to the next character
    return codepoint;
}

size_t CountUTF8Chars(const char* text, size_t text_byte_len) {
    size_t char_count = 0;
    const char* p = text;
    const char* end = text + text_byte_len;
    while (p < end) {
        const char* char_start_before_decode = p;
        Sint32 cp = decode_utf8(&p, end);

        if (p > char_start_before_decode) { // If the decoder advanced the pointer
             if (cp > 0) { // Valid codepoint
                char_count++;
             } else if (cp == 0 && p < end) { // Null-terminator within the string (count as a character)
                char_count++;
             } else if (cp == -1) { // Decoding error (count as one "erroneous" character)
                 char_count++;
             } else if (cp == 0 && p == end) { // Reached the end after successfully decoding the last character (not 0)
                 break; // If the last character was successfully decoded and it wasn't \0, it's already counted
             }
        } else { // Decoder couldn't advance the pointer (unlikely if cp != -1, but for safety)
            // If we are here, it means p hasn't changed, possibly end of string or an error decode_utf8 couldn't handle
            // If p < end, it's likely an incorrect byte that decode_utf8 didn't process as part of a multi-byte sequence.
            // Skip one byte to avoid an infinite loop, and count it as one character.
            if (p < end) {
                p++;
                char_count++;
            } else {
                break; // Reached end of string
            }
        }
    }
    return char_count;
}