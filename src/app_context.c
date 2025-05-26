#include "app_context.h"
#include "config.h" // For FONT_SIZE, PROJECT_NAME_STR, COMPANY_NAME_STR, ENABLE_GAME_LOGS
#include <SDL2/SDL_filesystem.h> // For SDL_GetPrefPath
#include <string.h> // For memset

bool InitializeApp(AppContext *appCtx, const char* title) {
    if (!appCtx) return false;
    memset(appCtx, 0, sizeof(AppContext)); // Initialize the entire structure with zeros

    // Log file initialization (moved from main.c for centralization)
#if ENABLE_GAME_LOGS
    char log_file_path_buffer[1024];
    char* pref_path_for_logs_tmp = SDL_GetPrefPath(COMPANY_NAME_STR, PROJECT_NAME_STR);
    if (pref_path_for_logs_tmp) {
        snprintf(log_file_path_buffer, sizeof(log_file_path_buffer), "%slogs.txt", pref_path_for_logs_tmp);
        SDL_free(pref_path_for_logs_tmp);
    } else {
        strcpy(log_file_path_buffer, "TypingApp_logs.txt"); // Fallback path
        fprintf(stderr, "Warning: SDL_GetPrefPath() failed. Attempting to write log to: %s\n", log_file_path_buffer);
    }
    appCtx->log_file_handle = fopen(log_file_path_buffer, "w");
    if (appCtx->log_file_handle == NULL) {
        perror("CRITICAL_STDERR: Failed to open log file in InitializeApp");
        fprintf(stderr, "Log file path attempted: %s\n", log_file_path_buffer);
        // Continue without a log file if it couldn't be opened
    } else {
        fputs("Application context initialization started.\n", appCtx->log_file_handle);
        fprintf(appCtx->log_file_handle, "Log file initialized at: %s\n", log_file_path_buffer);
        fflush(appCtx->log_file_handle);
    }
#else
    appCtx->log_file_handle = NULL;
#endif

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        if(appCtx->log_file_handle) fprintf(appCtx->log_file_handle, "SDL_Init error: %s\n", SDL_GetError());
        fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        return false;
    }
    if (TTF_Init() != 0) {
        if(appCtx->log_file_handle) fprintf(appCtx->log_file_handle, "TTF_Init error: %s\n", TTF_GetError());
        fprintf(stderr, "TTF_Init error: %s\n", TTF_GetError());
        SDL_Quit();
        return false;
    }

    TTF_SetFontHinting(NULL, TTF_HINTING_LIGHT); // Try different values if font rendering is poor
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best"); // or "linear", "nearest"

    appCtx->win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_W, WINDOW_H, SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!appCtx->win) {
        if(appCtx->log_file_handle) fprintf(appCtx->log_file_handle, "SDL_CreateWindow error: %s\n", SDL_GetError());
        fprintf(stderr, "SDL_CreateWindow error: %s\n", SDL_GetError());
        TTF_Quit(); SDL_Quit(); return false;
    }

    appCtx->ren = SDL_CreateRenderer(appCtx->win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!appCtx->ren) {
        if(appCtx->log_file_handle) fprintf(appCtx->log_file_handle, "SDL_CreateRenderer error: %s\n", SDL_GetError());
        fprintf(stderr, "SDL_CreateRenderer error: %s\n", SDL_GetError());
        SDL_DestroyWindow(appCtx->win); TTF_Quit(); SDL_Quit(); return false;
    }

    // Scaling handling for HiDPI/Retina displays
    int physW_val, physH_val;
    SDL_GetRendererOutputSize(appCtx->ren, &physW_val, &physH_val);
    float scale_x = (float)physW_val / WINDOW_W;
    float scale_y = (float)physH_val / WINDOW_H;
    SDL_RenderSetScale(appCtx->ren, scale_x, scale_y);

    // Font loading logic
    const char* font_paths[] = {
        "Arial Unicode.ttf", "arial.ttf",
#ifdef _WIN32
        "C:/Windows/Fonts/arialuni.ttf", "C:/Windows/Fonts/arial.ttf", "C:/Windows/Fonts/verdana.ttf",
        "C:/Windows/Fonts/tahoma.ttf", "C:/Windows/Fonts/times.ttf", "C:/Windows/Fonts/consola.ttf",
#endif
        "/System/Library/Fonts/Arial Unicode.ttf", "/System/Library/Fonts/Arial.ttf", "/Library/Fonts/Arial Unicode.ttf",
        "/usr/share/fonts/truetype/msttcorefonts/Arial.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        NULL
    };

    if(appCtx->log_file_handle) fprintf(appCtx->log_file_handle, "Attempting to load fonts...\n");
    printf("Attempting to load fonts...\n");

    for (int i = 0; font_paths[i] != NULL; ++i) {
        if(appCtx->log_file_handle) fprintf(appCtx->log_file_handle, "Trying font path: %s\n", font_paths[i]);
        printf("Trying font path: %s\n", font_paths[i]);

        #if SDL_TTF_VERSION_ATLEAST(2,20,0)
            appCtx->font = TTF_OpenFontDPI(font_paths[i], FONT_SIZE, (unsigned int)(72 * scale_x), (unsigned int)(72 * scale_y));
        #else
            appCtx->font = TTF_OpenFont(font_paths[i], FONT_SIZE);
        #endif

        if (appCtx->font) {
            if(appCtx->log_file_handle) fprintf(appCtx->log_file_handle, "Successfully loaded font from: %s\n", font_paths[i]);
            printf("Successfully loaded font from: %s\n", font_paths[i]);
            break;
        } else {
            if(appCtx->log_file_handle) fprintf(appCtx->log_file_handle, "Failed to load font from %s: %s\n", font_paths[i], TTF_GetError());
            printf("Failed to load font from %s: %s\n", font_paths[i], TTF_GetError());
        }
    }

    if (!appCtx->font) {
        fprintf(stderr, "Failed to load font after trying all paths: %s\n", TTF_GetError());
        if(appCtx->log_file_handle) fprintf(appCtx->log_file_handle, "CRITICAL: Failed to load any font. Last error: %s\n", TTF_GetError());
        SDL_DestroyRenderer(appCtx->ren); SDL_DestroyWindow(appCtx->win);
        TTF_Quit(); SDL_Quit();
        if(appCtx->log_file_handle) fclose(appCtx->log_file_handle);
        return false;
    }

    // Setup palette
    appCtx->palette[COL_BG]        = (SDL_Color){50,52,55,255};
    appCtx->palette[COL_TEXT]      = (SDL_Color){100,102,105,255};
    appCtx->palette[COL_CORRECT]   = (SDL_Color){201,200,190,255};
    appCtx->palette[COL_INCORRECT] = (SDL_Color){200,0,0,255};
    appCtx->palette[COL_CURSOR]    = (SDL_Color){255,200,0,255};

    appCtx->line_h = TTF_FontLineSkip(appCtx->font);
    if (appCtx->line_h <= 0) appCtx->line_h = TTF_FontHeight(appCtx->font);
    if (appCtx->line_h <= 0) appCtx->line_h = FONT_SIZE + 4; // Fallback value

    // Cache ASCII glyphs
    for (int c = 32; c < 127; c++) { // Cache only printable ASCII characters
        int adv_val;
        if (TTF_GlyphMetrics(appCtx->font, (Uint16)c, NULL, NULL, NULL, NULL, &adv_val) != 0) {
            adv_val = FONT_SIZE / 2; // Fallback value if metrics couldn't be obtained
        }
        appCtx->glyph_adv_cache[c] = (adv_val > 0) ? adv_val : FONT_SIZE / 2;

        for (int col_idx = COL_TEXT; col_idx <= COL_INCORRECT; col_idx++) { // Cache for primary text colors
            SDL_Surface *surf = TTF_RenderGlyph_Blended(appCtx->font, (Uint16)c, appCtx->palette[col_idx]);
            if (!surf) continue;
            appCtx->glyph_w_cache[col_idx][c] = surf->w;
            appCtx->glyph_h_cache[col_idx][c] = surf->h;
            appCtx->glyph_tex_cache[col_idx][c] = SDL_CreateTextureFromSurface(appCtx->ren, surf);
            if (!appCtx->glyph_tex_cache[col_idx][c] && appCtx->log_file_handle) {
                 fprintf(appCtx->log_file_handle, "Warning: Failed to create texture for glyph %c (ASCII %d) color %d\n", c, c, col_idx);
            }
            SDL_FreeSurface(surf);
        }
    }
    // Space and tab width
    appCtx->space_advance_width = appCtx->glyph_adv_cache[' '];
    if (appCtx->space_advance_width <= 0) appCtx->space_advance_width = FONT_SIZE / 3; // Fallback
    appCtx->tab_width_pixels = (appCtx->space_advance_width > 0) ? (TAB_SIZE_IN_SPACES * appCtx->space_advance_width) : (TAB_SIZE_IN_SPACES * (FONT_SIZE / 3));

    // State initialization
    appCtx->typing_started = false;
    appCtx->start_time_ms = 0;
    appCtx->time_at_pause_ms = 0;
    appCtx->is_paused = false;
    appCtx->l_modifier_held = false;
    appCtx->r_modifier_held = false;
    appCtx->total_keystrokes_for_accuracy = 0;
    appCtx->total_errors_committed_for_accuracy = 0;
    appCtx->first_visible_abs_line_num = 0;
    appCtx->predictive_scroll_triggered_this_input_idx = false;
    appCtx->y_offset_due_to_prediction_for_current_idx = 0;


    if(appCtx->log_file_handle) {
        fprintf(appCtx->log_file_handle, "Application context initialized successfully.\n");
        fflush(appCtx->log_file_handle);
    }
    return true;
}

void CleanupApp(AppContext *appCtx) {
    if (!appCtx) return;

    if(appCtx->log_file_handle) fprintf(appCtx->log_file_handle, "Cleaning up application context...\n");

    // Free cached glyph textures
    for (int c = 32; c < 127; c++) {
        for (int col = COL_TEXT; col <= COL_INCORRECT; col++) {
            if (appCtx->glyph_tex_cache[col][c]) {
                SDL_DestroyTexture(appCtx->glyph_tex_cache[col][c]);
                appCtx->glyph_tex_cache[col][c] = NULL;
            }
        }
    }

    if (appCtx->font) { TTF_CloseFont(appCtx->font); appCtx->font = NULL; }
    if (appCtx->ren) { SDL_DestroyRenderer(appCtx->ren); appCtx->ren = NULL; }
    if (appCtx->win) { SDL_DestroyWindow(appCtx->win); appCtx->win = NULL; }

    TTF_Quit();
    SDL_Quit();

    if (appCtx->log_file_handle) {
        fprintf(appCtx->log_file_handle, "Application context cleaned up.\nApplication finished.\n");
        fflush(appCtx->log_file_handle);
        fclose(appCtx->log_file_handle);
        appCtx->log_file_handle = NULL;
    }
}