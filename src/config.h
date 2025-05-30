#ifndef CONFIG_H
#define CONFIG_H

// --- Application Constants ---
#define WINDOW_W     800
#define WINDOW_H     200 // 256 for 5 lines
#define FONT_SIZE    28
#define UI_FONT_SIZE 30
#define MAX_TEXT_LEN (5 * 1024 * 1024) // Maximum length of raw text

#ifndef TEXT_FILE_PATH_BASENAME
#define TEXT_FILE_PATH_BASENAME "text.txt"
#endif
#ifndef STATS_FILE_BASENAME
#define STATS_FILE_BASENAME "stats.txt"
#endif

// These definitions will be replaced by values from CMake if specified there.
#ifndef PROJECT_NAME_STR
#define PROJECT_NAME_STR "TypingApp"
#endif
#ifndef COMPANY_NAME_STR
#define COMPANY_NAME_STR "com.typingapp"
#endif

#define TEXT_AREA_X 10
#define TEXT_AREA_PADDING_Y 10
#define TEXT_AREA_W (WINDOW_W - (2 * TEXT_AREA_X))
#define DISPLAY_LINES 3 // Number of text lines displayed simultaneously
#define CURSOR_TARGET_VIEWPORT_LINE 1 // On which viewport line (0-indexed) the cursor should be
#define TAB_SIZE_IN_SPACES 4 // Number of spaces for a single tab character

// Set to 1 to enable logging to a file.
// The log file will be created in the user's settings directory.
#define ENABLE_GAME_LOGS 0

// Colors
enum {
    COL_BG,          // Background
    COL_TEXT,        // Main text (not yet typed)
    COL_CORRECT,     // Correctly typed text
    COL_INCORRECT,   // Incorrectly typed text
    COL_CURSOR,      // Cursor color
    N_COLORS         // Number of colors in the palette
};

#endif // CONFIG_H