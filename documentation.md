TypingApp Documentation
=======================

1. Overview
-----------
TypingApp is a desktop application designed to help users practice and improve their typing speed and accuracy.
It provides a simple interface for typing pre-defined or user-supplied text, and it tracks performance metrics such
as Words Per Minute (WPM) and accuracy. The application is built using C, SDL2, and SDL2_ttf, and uses CMake as its
build system. It is designed with cross-platform compatibility in mind, with specific configurations for macOS, Windows,
and Linux.

2. Features
-----------
* **Typing Practice**: Users can type text loaded from a `text.txt` file.
* **Performance Metrics**:
  * **Timer**: Tracks the time elapsed during a typing session.
  * **WPM Calculation**: Displays live and final Words Per Minute (Net WPM based on 5 characters per word).
  * **Accuracy Tracking**: Calculates and displays typing accuracy based on committed errors.
  * **Word Count**: Shows a live count of typed words.
* **Statistics Logging**: Saves session statistics (timestamp, WPM, accuracy, time taken, keystroke counts) to
  a `stats.txt` file for review.
* **Customizable Text**: Users can provide their own text for practice by modifying `text.txt` located in the
  application's preference directory.
* **Text Handling**:
  * Loads initial text from `text.txt`; if not found or empty, copies a default bundled text or uses a platform-specific placeholder text (e.g., instructions on how to add text using the pause menu).
  * Saves any untyped portion of the text back to `text.txt` upon exiting a session.
  * UTF-8 Support: Handles and renders UTF-8 encoded text.
  * Text Preprocessing: Normalizes line endings (CRLF, CR to LF) and performs some typographic replacements
    (e.g., the ellipsis character U+2026 to three periods "...", "--" to em-dash (which is then normalized to en-dash U+2013), smart quotes (U+2018, U+2019, U+201C, U+201D) to simple apostrophes "'") before display.
* **User Interface**:
  * Displays text across multiple lines with word wrapping.
  * Predictive scrolling to keep the current typing line in a comfortable view position.
  * Visual feedback for correct and incorrect keystrokes through text coloring.
  * Blinking cursor to indicate the current typing position.
* **Application Controls**:
  * Pause/Resume: Typing sessions can be paused (Left Alt + Right Alt on Windows/Linux; Left Command + Right Command, or Left Alt + Right Alt on macOS) and resumed.
  * File Access Shortcuts: While paused, users can press 't' to open the current `text.txt` or 's' to open `stats.txt`
    in the default system editor/viewer.
* **Technical Features**:
  * Glyph Caching: Caches frequently used ASCII character (32-126) textures for faster rendering.
  * HiDPI/Retina Scaling: Adapts rendering for high-resolution displays using SDL's features.
  * Logging: Optional diagnostic logging to `logs.txt` (controlled by `ENABLE_GAME_LOGS` in `config.h`) in the user's
    preference directory.

3. Building the Project
-----------------------
* **Dependencies**:
  * SDL2 (Simple DirectMedia Layer library)
  * SDL2_ttf (SDL2 TrueType Font rendering library)
  * A C compiler supporting C23 standard (e.g., GCC, Clang). [cite: 1]
* **Build System**: CMake (version 3.20 or higher recommended). [cite: 1]
* **General Build Steps**:
  1.  Ensure CMake and the required C compiler are installed.
  2.  Install SDL2 and SDL2_ttf development libraries.
  3.  Navigate to the project's root directory.
  4.  Create a build directory (e.g., `mkdir build && cd build`).
  5.  Run CMake to configure the project (e.g., `cmake ..`). Platform-specific path configurations might be needed
      here (see below).
  6.  Compile the project using the generated build files (e.g., `make` or `ninja`).
  7.  The executable will be typically found in the build directory or a subdirectory like `build/src/` (macOS) or `build/bin/` (Windows after install step or next to executable in build dir for direct run).
* **Platform-Specific Notes for SDL2/TTF Paths**:
  * **macOS**: The `CMakeLists.txt` is configured to find SDL2 and SDL2_ttf via `pkg-config`, assuming they are
    installed via Homebrew (default prefix `/opt/homebrew` for Apple Silicon, `/usr/local` might be used for Intel). The deployment target is set to macOS 13.0. [cite: 1]
  * **Windows (MinGW)**: You need to provide the paths to your SDL2 and SDL2_ttf MinGW 64-bit development libraries.
    This is typically done by setting the `CMAKE_PREFIX_PATH` variable when running CMake. For example:
    `cmake .. -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH="C:/path/to/sdl2;C:/path/to/sdl2_ttf"` [cite: 1]
  * **Linux**: The `CMakeLists.txt` uses `pkg-config` to find SDL2 and SDL2_ttf, which is standard for most Linux
    distributions. Ensure `libsdl2-dev` and `libsdl2-ttf-dev` (or similarly named packages) are installed. [cite: 1]

4. Project Structure
--------------------
The project is organized into several directories:
* `src/`: Contains all C source code (`.c`) and header files (`.h`) for the application modules.
* `assets/`: Contains static resources used by the application, such as:
  * `text.txt`: The default typing text. The `CMakeLists.txt` uses this file (expected at `assets/text.txt`) to be copied to the user's preference directory on first run (via application logic) and into the application bundle (macOS) or installation directory (Windows). [cite: 1]
  * `appicon.icns`: Application icon for macOS (path: `assets/appicon.icns`). [cite: 16]
  * `appicon.ico`: Application icon for Windows (path: `assets/appicon.ico`). [cite: 73]
  * `dmg_background.png`: Background image for the macOS DMG installer (path: `assets/dmg_background.png`). [cite: 44]
* `scripts/`: Contains helper scripts.
  * `fix_inner_deps.sh.in`: Template script used on macOS to fix library paths in the application bundle for
    portability (path: `scripts/fix_inner_deps.sh.in`). [cite: 32]
* `CMakeLists.txt`: The main CMake build script that defines how the project is compiled and packaged.
* `README.md`: (Presumably) General information about the project.
* `documentation.md`: This file.

5. Module Descriptions
----------------------
The application's C code is modularized for better organization and maintainability:
* **`main.c`**: The main entry point of the application. It initializes all necessary sub-modules, manages the main
  application loop (event handling, rendering updates), and handles cleanup on exit.
* **`app_context.c/.h`**: Defines and manages the global `AppContext` structure. This includes SDL/TTF initialization
  and cleanup, window and renderer creation, font loading from a list of common system paths (including HiDPI-aware loading using `TTF_OpenFontDPI`), color palette setup, ASCII (32-126) glyph texture caching for performance, and managing shared application state variables (like pause status, timing, error counts, HiDPI scale factors). It also handles log file initialization.
* **`config.h`**: A central header file for global application constants such as window dimensions, font sizes (`FONT_SIZE`, `UI_FONT_SIZE`), text area layout, maximum text length, default filenames (`PROJECT_NAME_STR`, `COMPANY_NAME_STR` have fallbacks here if not defined by build system), and color definitions. It also contains the `ENABLE_GAME_LOGS` macro to toggle diagnostic logging.
* **`event_handler.c/.h`**: Responsible for processing all SDL events. This includes handling window quit events,
  keyboard input (Escape key, Backspace), text input events via `SDL_TEXTINPUT` (handling UTF-8), and special key combinations for
  pausing/resuming (LAlt+RAlt on Windows/Linux; LCmd+RCmd or LAlt+RAlt on macOS, checking specific syms like `SDLK_LGUI`, `SDLK_LALT`) and opening text/stats files ('t'/'s' while paused). It updates the input
  buffer and tracks typing errors based on input.
* **`file_paths.c/.h`**: Manages the determination and handling of file paths for user-specific data (`text.txt`,
  `stats.txt`) and the default bundled `text.txt`. It uses `SDL_GetPrefPath` to find appropriate user directories and
  `SDL_GetBasePath` for bundled resources. This module contains functions to load the initial text (copying from default
  or using a platform-specific placeholder if necessary) and to save the remaining untyped text back to the user's `text.txt` file upon
  session completion.
* **`layout_logic.c/.h`**: Contains the logic for calculating the visual layout of the text being typed. This includes
  determining the absolute line and x-coordinate of the cursor based on the input text and current position
  (`CalculateCursorLayout`). It also implements word wrapping (considering hanging spaces) and manages scrolling behavior, including a predictive
  scrolling feature (`PerformPredictiveScrollUpdate`, `UpdateVisibleLine`) to keep the active typing line within the viewport.
* **`rendering.c/.h`**: Handles all drawing operations. This module is responsible for rendering the application timer,
  live statistics (WPM, accuracy, word count using `ui_font`), the main text content (with different colors for untyped, correctly typed,
  and incorrectly typed characters, using `font` and its cache or on-the-fly rendering for non-cached glyphs), and the blinking cursor. It correctly applies HiDPI scaling factors for dimensions and rendering.
* **`stats_handler.c/.h`**: Calculates final typing statistics (WPM based on 5 chars/word, accuracy, time taken, keystroke counts) at the end
  of a typing session. It prints these stats to the console and appends them with a timestamp to the `stats.txt` file
  located in the user's preference directory.
* **`text_processing.c/.h`**: Contains functions for text manipulation. `PreprocessText` normalizes raw input text
  (handles different line endings `\r\n, \r` to `\n`, replaces `--` with em-dash U+2014, then normalizes U+2014 to en-dash U+2013, replaces U+2026 ellipsis with `...`, and smart quotes U+2018/U+2019/U+201C/U+201D with `'`. It also removes extra spaces and trims leading/trailing whitespace). `get_next_text_block_func` breaks the processed text into logical blocks (words,
  sequences of spaces, newlines, tabs) for layout and rendering, calculating tab widths based on current pen position. `get_codepoint_advance_and_metrics_func` retrieves
  font metrics (logical advance, width, height) for individual characters, using cache for ASCII and `TTF_GlyphMetrics32` for others, applying scaling.
* **`utf8_utils.c/.h`**: Provides utility functions for working with UTF-8 encoded strings. `decode_utf8` decodes
  a single UTF-8 character from a string and advances a pointer past it. `CountUTF8Chars` counts the number of UTF-8
  characters in a byte string.

6. Usage
--------
* **Starting the Application**: Launch the compiled executable. On the first run, it will create a preference directory
  (e.g., `~/Library/Application Support/com.typingapp.TypingApp/` on macOS, `%APPDATA%/com.typingapp/TypingApp/` on Windows, or a similar path on Linux, based on `SDL_GetPrefPath` with `COMPANY_NAME_STR` and `PROJECT_NAME_STR`)
  and place `text.txt` and (after a session) `stats.txt` there.
* **Typing**: The text from `text.txt` will be displayed. Begin typing. Correctly typed characters will change color
  (e.g., to a light gray/beige `COL_CORRECT`), and incorrectly typed characters will be highlighted (e.g., in red `COL_INCORRECT`). Untyped text remains in `COL_TEXT`.
* **Live Statistics**: As you type, live WPM, accuracy, and word count are displayed at the top of the window alongside
  the timer.
* **Pause/Resume**:
  * Press Left Alt + Right Alt simultaneously (on Windows/Linux) or Left Command + Right Command (on macOS,
    Left Alt + Right Alt may also work as per code) to pause the typing session. The timer will stop, and "(Paused)" will be displayed.
  * Press the same key combination again to resume.
* **Accessing Text/Stats Files**:
  * While paused, press the 't' key to open the `text.txt` file currently being used by the application in your
    system's default text editor. This allows you to easily change the practice text.
  * While paused, press the 's' key to open the `stats.txt` file in your system's default text editor or viewer,
    allowing you to review your past performance.
* **Exiting**: Close the window or press the Escape key to exit the application. If a typing session was in progress,
  the remaining untyped text will be saved back to `text.txt`, and final statistics will be recorded.

7. Configuration
----------------
* **`text.txt`**: The primary way to configure the typing content is by editing the `text.txt` file. This file is
  located in the application's preference directory, which can be opened by pausing the game and pressing 't'.
* **`config.h`**: Several aspects of the application's behavior and appearance are defined as constants in
  `src/config.h`. These require recompilation to change:
  * `WINDOW_W`, `WINDOW_H`: Default window width and height.
  * `FONT_SIZE`, `UI_FONT_SIZE`: Default font sizes for the main typing text and UI elements (timer, stats) respectively.
  * `MAX_TEXT_LEN`: Maximum raw text length the application will attempt to load.
  * `TEXT_FILE_PATH_BASENAME`, `STATS_FILE_BASENAME`: Basenames for text and stats files.
  * `PROJECT_NAME_STR`, `COMPANY_NAME_STR`: Used for preference path creation (have default values if not overridden by the build system).
  * `TEXT_AREA_X`, `TEXT_AREA_PADDING_Y`, `TEXT_AREA_W`: Define the text rendering area layout.
  * `DISPLAY_LINES`: Number of text lines shown at once.
  * `CURSOR_TARGET_VIEWPORT_LINE`: The line in the viewport where the cursor aims to be positioned by scrolling.
  * `TAB_SIZE_IN_SPACES`: How many spaces a tab character represents.
  * `ENABLE_GAME_LOGS`: Set to 1 to enable detailed logging to `logs.txt`, or 0 to disable.
  * Color definitions (e.g., `COL_BG`, `COL_TEXT`, `COL_CORRECT`, `COL_INCORRECT`, `COL_CURSOR`) for various UI elements, defined as an enum and used with the `palette` array.

8. Data Files
-------------
The application uses the following files, typically stored in a user-specific preference directory (path varies by OS
but is based on `SDL_GetPrefPath` and logged if `ENABLE_GAME_LOGS` is on):
* **`text.txt`**: Stores the text used for typing practice. This file is read at startup and can be modified by the
  user. The application will save the untyped portion of the text back to this file when a session ends partway through.
* **`stats.txt`**: A plain text file where statistics for each completed typing session are appended. Each entry includes
  a timestamp, WPM, accuracy, time taken, and keystroke details.
* **`logs.txt`**: If logging is enabled (`ENABLE_GAME_LOGS=1` in `config.h`), this file contains diagnostic information
  and logs of application events, errors, and operations. This is useful for debugging.