#include "utf8_utils.h"

Sint32 decode_utf8(const char **s_ptr, const char *s_end_const_char) {
    if (!s_ptr || !*s_ptr || *s_ptr >= s_end_const_char) return 0; // Кінець рядка або невалідний вказівник
    const unsigned char *s = (const unsigned char *)*s_ptr;
    const unsigned char *s_end = (const unsigned char *)s_end_const_char;
    unsigned char c1 = *s;
    Sint32 codepoint;
    int len = 0;

    if (c1 < 0x80) { // 1-байтовий символ (0xxxxxxx)
        codepoint = c1;
        len = 1;
    } else if ((c1 & 0xE0) == 0xC0) { // 2-байтовий символ (110xxxxx 10xxxxxx)
        if (s + 1 >= s_end || (s[1] & 0xC0) != 0x80) return -1; // Неправильна послідовність
        codepoint = ((Sint32)(c1 & 0x1F) << 6) | (Sint32)(s[1] & 0x3F);
        len = 2;
    } else if ((c1 & 0xF0) == 0xE0) { // 3-байтовий символ (1110xxxx 10xxxxxx 10xxxxxx)
        if (s + 2 >= s_end || (s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80) return -1;
        codepoint = ((Sint32)(c1 & 0x0F) << 12) | ((Sint32)(s[1] & 0x3F) << 6) | (Sint32)(s[2] & 0x3F);
        len = 3;
    } else if ((c1 & 0xF8) == 0xF0) { // 4-байтовий символ (11110xxx 10xxxxxx 10xxxxxx 10xxxxxx)
        if (s + 3 >= s_end || (s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 || (s[3] & 0xC0) != 0x80) return -1;
        codepoint = ((Sint32)(c1 & 0x07) << 18) | ((Sint32)(s[1] & 0x3F) << 12) | ((Sint32)(s[2] & 0x3F) << 6) | (Sint32)(s[3] & 0x3F);
        len = 4;
    } else { // Неправильний початковий байт
        return -1;
    }

    (*s_ptr) += len; // Пересуваємо вказівник на наступний символ
    return codepoint;
}

size_t CountUTF8Chars(const char* text, size_t text_byte_len) {
    size_t char_count = 0;
    const char* p = text;
    const char* end = text + text_byte_len;
    while (p < end) {
        const char* char_start_before_decode = p;
        Sint32 cp = decode_utf8(&p, end);

        if (p > char_start_before_decode) { // Якщо декодер просунув вказівник
             if (cp > 0) { // Валідний codepoint
                char_count++;
             } else if (cp == 0 && p < end) { // Null-термінатор всередині рядка (рахуємо як символ)
                char_count++;
             } else if (cp == -1) { // Помилка декодування (рахуємо як один "помилковий" символ)
                 char_count++;
             } else if (cp == 0 && p == end) { // Досягли кінця після успішного декодування останнього символу (не 0)
                 break; // Якщо останній символ був успішно декодований і це не був \0, він вже порахований
             }
        } else { // Декодер не зміг просунути вказівник (малоймовірно, якщо cp != -1, але для безпеки)
            // Якщо ми тут, це означає, що p не змінився, можливо, кінець рядка або помилка, яку decode_utf8 не зміг обробити
            // Якщо p < end, то це, ймовірно, некоректний байт, який decode_utf8 не обробив як частину багатобайтової послідовності.
            // Пропускаємо один байт, щоб уникнути нескінченного циклу, і рахуємо його як один символ.
            if (p < end) {
                p++;
                char_count++;
            } else {
                break; // Досягли кінця рядка
            }
        }
    }
    return char_count;
}