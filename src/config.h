#ifndef CONFIG_H
#define CONFIG_H

// --- Application Constants ---
#define WINDOW_W     800
#define WINDOW_H     200
#define FONT_SIZE    14
#define MAX_TEXT_LEN (5 * 1024 * 1024) // Максимальна довжина необробленого тексту

#ifndef TEXT_FILE_PATH_BASENAME
#define TEXT_FILE_PATH_BASENAME "text.txt"
#endif
#ifndef STATS_FILE_BASENAME
#define STATS_FILE_BASENAME "stats.txt"
#endif

// Ці визначення будуть замінені значеннями з CMake, якщо вони там вказані.
#ifndef PROJECT_NAME_STR
#define PROJECT_NAME_STR "TypingApp"
#endif
#ifndef COMPANY_NAME_STR
#define COMPANY_NAME_STR "com.typingapp"
#endif

#define TEXT_AREA_X 10
#define TEXT_AREA_PADDING_Y 10
#define TEXT_AREA_W (WINDOW_W - (2 * TEXT_AREA_X))
#define DISPLAY_LINES 3 // Кількість рядків тексту, що відображаються одночасно
#define CURSOR_TARGET_VIEWPORT_LINE 1 // На якому рядку в'юпорта (0-індексований) має бути курсор
#define TAB_SIZE_IN_SPACES 4 // Кількість пробілів для одного символу табуляції

// Встановіть в 1, щоб увімкнути логування у файл.
// Файл логів буде створено у теці налаштувань користувача.
#define ENABLE_GAME_LOGS 1

// Кольори
enum {
    COL_BG,          // Фон
    COL_TEXT,        // Основний текст (ще не набраний)
    COL_CORRECT,     // Правильно набраний текст
    COL_INCORRECT,   // Неправильно набраний текст
    COL_CURSOR,      // Колір курсора
    N_COLORS         // Кількість кольорів у палітрі
};

#endif // CONFIG_H