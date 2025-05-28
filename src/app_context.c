#include "app_context.h"
#include "config.h" // For FONT_SIZE, UI_FONT_SIZE, PROJECT_NAME_STR, COMPANY_NAME_STR, ENABLE_GAME_LOGS
#include <SDL2/SDL_filesystem.h> // For SDL_GetPrefPath
#include <string.h> // For memset
#include <math.h>   // For roundf

bool InitializeApp(AppContext *appCtx, const char* title) {
    if (!appCtx) return false;
    memset(appCtx, 0, sizeof(AppContext)); // Initialize the entire structure with zeros
    appCtx->scale_x_factor = 1.0f; // Default scale factors
    appCtx->scale_y_factor = 1.0f;
    appCtx->font = NULL;        // Initialize font pointers
    appCtx->ui_font = NULL;

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
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best"); // or "linear", "nearest" for text quality

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

    // Calculate and store scaling factors
    int physW_val, physH_val;
    SDL_GetRendererOutputSize(appCtx->ren, &physW_val, &physH_val);
    appCtx->scale_x_factor = (WINDOW_W > 0) ? (float)physW_val / WINDOW_W : 1.0f;
    appCtx->scale_y_factor = (WINDOW_H > 0) ? (float)physH_val / WINDOW_H : 1.0f;
    if (appCtx->scale_x_factor < 0.1f) appCtx->scale_x_factor = 1.0f; // Sanity check for scale factors
    if (appCtx->scale_y_factor < 0.1f) appCtx->scale_y_factor = 1.0f;
    SDL_RenderSetScale(appCtx->ren, appCtx->scale_x_factor, appCtx->scale_y_factor);

    if (appCtx->log_file_handle) {
        fprintf(appCtx->log_file_handle, "Renderer scale factors: scale_x=%.2f, scale_y=%.2f (physW=%d, physH=%d, logicalW=%d, logicalH=%d)\n",
                appCtx->scale_x_factor, appCtx->scale_y_factor, physW_val, physH_val, WINDOW_W, WINDOW_H);
    }

    const char* font_paths[] = {
        "Arial Unicode.ttf", "arial.ttf", // Relative paths first
#ifdef _WIN32 // Windows-specific paths
        "C:/Windows/Fonts/arialuni.ttf", "C:/Windows/Fonts/arial.ttf", "C:/Windows/Fonts/verdana.ttf",
        "C:/Windows/Fonts/tahoma.ttf", "C:/Windows/Fonts/times.ttf", "C:/Windows/Fonts/consola.ttf",
#endif
        // macOS common paths
        "/System/Library/Fonts/Arial Unicode.ttf", "/System/Library/Fonts/Arial.ttf", "/Library/Fonts/Arial Unicode.ttf",
        // Linux common paths
        "/usr/share/fonts/truetype/msttcorefonts/Arial.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        NULL // Sentinel
    };

    // Load main text font
    if(appCtx->log_file_handle) fprintf(appCtx->log_file_handle, "Attempting to load main font (FONT_SIZE=%d)...\n", FONT_SIZE);
    printf("Attempting to load main font (FONT_SIZE=%d)...\n", FONT_SIZE);
    for (int i = 0; font_paths[i] != NULL; ++i) {
        if(appCtx->log_file_handle) fprintf(appCtx->log_file_handle, "Trying main font path: %s\n", font_paths[i]);
        // We don't printf this for UI font to avoid duplicate "Trying font path" if paths are reused

        #if SDL_TTF_VERSION_ATLEAST(2,0,12)
            // FONT_SIZE is treated as a logical point size.
            // TTF_OpenFontDPI will render it sharply for the target DPI.
            unsigned int target_hdpi = (unsigned int)(72 * appCtx->scale_x_factor);
            unsigned int target_vdpi = (unsigned int)(72 * appCtx->scale_y_factor);
            if (target_hdpi == 0) target_hdpi = 72;
            if (target_vdpi == 0) target_vdpi = 72;

            appCtx->font = TTF_OpenFontDPI(font_paths[i], FONT_SIZE, target_hdpi, target_vdpi);
            if (appCtx->font && appCtx->log_file_handle) {
                fprintf(appCtx->log_file_handle, "Loaded main font with TTF_OpenFontDPI (ptsize=%d, target_hdpi=%u, target_vdpi=%u)\n", FONT_SIZE, target_hdpi, target_vdpi);
            }
        #else
            appCtx->font = TTF_OpenFont(font_paths[i], FONT_SIZE);
            if (appCtx->font && appCtx->log_file_handle) {
                fputs("Loaded main font with TTF_OpenFont (TTF_OpenFontDPI not available or version too old).\n", appCtx->log_file_handle);
            }
        #endif

        if (appCtx->font) {
            if(appCtx->log_file_handle) fprintf(appCtx->log_file_handle, "Successfully loaded main font from: %s\n", font_paths[i]);
            printf("Successfully loaded main font from: %s\n", font_paths[i]);
            break;
        } else {
            if(appCtx->log_file_handle) fprintf(appCtx->log_file_handle, "Failed to load main font from %s: %s\n", font_paths[i], TTF_GetError());
            // printf for main font failure is useful
             printf("Failed to load main font from %s: %s\n", font_paths[i], TTF_GetError());
        }
    }

    if (!appCtx->font) { // Critical if main font fails
        fprintf(stderr, "CRITICAL: Failed to load main font after trying all paths: %s\n", TTF_GetError());
        if(appCtx->log_file_handle) fprintf(appCtx->log_file_handle, "CRITICAL: Failed to load main font. Last error: %s\n", TTF_GetError());
        SDL_DestroyRenderer(appCtx->ren); SDL_DestroyWindow(appCtx->win);
        TTF_Quit(); SDL_Quit(); if(appCtx->log_file_handle) {fclose(appCtx->log_file_handle); appCtx->log_file_handle = NULL;}
        return false;
    }

    // Load UI font (timer, stats)
    if(appCtx->log_file_handle) fprintf(appCtx->log_file_handle, "Attempting to load UI font (UI_FONT_SIZE=%d)...\n", UI_FONT_SIZE);
    printf("Attempting to load UI font (UI_FONT_SIZE=%d)...\n", UI_FONT_SIZE);
    bool ui_font_loaded = false;
    for (int i = 0; font_paths[i] != NULL; ++i) {
        // No separate printf/log for "Trying UI font path" if it's the same as main font paths to avoid noise
        #if SDL_TTF_VERSION_ATLEAST(2,0,12)
            // UI_FONT_SIZE is treated as a logical point size.
            unsigned int target_hdpi = (unsigned int)(72 * appCtx->scale_x_factor);
            unsigned int target_vdpi = (unsigned int)(72 * appCtx->scale_y_factor);
            if (target_hdpi == 0) target_hdpi = 72;
            if (target_vdpi == 0) target_vdpi = 72;

            appCtx->ui_font = TTF_OpenFontDPI(font_paths[i], UI_FONT_SIZE, target_hdpi, target_vdpi);
            if (appCtx->ui_font && appCtx->log_file_handle) {
                fprintf(appCtx->log_file_handle, "Loaded UI font with TTF_OpenFontDPI (ptsize=%d, target_hdpi=%u, target_vdpi=%u)\n", UI_FONT_SIZE, target_hdpi, target_vdpi);
            }
        #else
            appCtx->ui_font = TTF_OpenFont(font_paths[i], UI_FONT_SIZE);
            if (appCtx->ui_font && appCtx->log_file_handle) {
                fputs("Loaded UI font with TTF_OpenFont.\n", appCtx->log_file_handle);
            }
        #endif
        if (appCtx->ui_font) {
            ui_font_loaded = true;
            if(appCtx->log_file_handle) fprintf(appCtx->log_file_handle, "Successfully loaded UI font from: %s\n", font_paths[i]);
            printf("Successfully loaded UI font from: %s\n", font_paths[i]);
            break;
        } else {
             if(appCtx->log_file_handle) fprintf(appCtx->log_file_handle, "Failed to load UI font from %s: %s\n", font_paths[i], TTF_GetError());
             // No printf here for UI font failure to reduce console noise if path is shared
        }
    }

    if (!ui_font_loaded) {
        fprintf(stderr, "WARNING: Failed to load UI font: %s. Using main font as fallback for UI elements.\n", TTF_GetError());
        if(appCtx->log_file_handle) fprintf(appCtx->log_file_handle, "WARNING: Failed to load UI font. Using main font as fallback. Last error: %s\n", TTF_GetError());
        appCtx->ui_font = appCtx->font; // Fallback to main font
        appCtx->ui_line_h = appCtx->line_h; // Use main font's line height
    } else {
        // Calculate logical line height for UI font
        int ui_scaled_line_h_px = TTF_FontLineSkip(appCtx->ui_font);
        if (ui_scaled_line_h_px <= 0) ui_scaled_line_h_px = TTF_FontHeight(appCtx->ui_font);
        if (ui_scaled_line_h_px <= 0) ui_scaled_line_h_px = (int)((UI_FONT_SIZE + 4) * appCtx->scale_y_factor); // Estimate scaled pixels
        if (ui_scaled_line_h_px <= 0 && appCtx->scale_y_factor > 0.01f) ui_scaled_line_h_px = (int)( (UI_FONT_SIZE < 10 ? 14 : UI_FONT_SIZE + 2) * appCtx->scale_y_factor); // Min scaled height for UI
        else if (ui_scaled_line_h_px <= 0) ui_scaled_line_h_px = (UI_FONT_SIZE < 10 ? 14 : UI_FONT_SIZE + 2);

        appCtx->ui_line_h = (appCtx->scale_y_factor > 0.01f) ? (int)roundf((float)ui_scaled_line_h_px / appCtx->scale_y_factor) : (UI_FONT_SIZE + 4);
        if (appCtx->ui_line_h <= 0) appCtx->ui_line_h = UI_FONT_SIZE + 4; // Ensure positive logical line height
    }
     if(appCtx->log_file_handle && appCtx->ui_font) { // Log only if ui_font is valid
        fprintf(appCtx->log_file_handle, "UI Font line height: logical ui_line_h=%d (based on UI_FONT_SIZE=%d, using font: %s)\n",
                appCtx->ui_line_h, UI_FONT_SIZE, (appCtx->ui_font == appCtx->font ? "main_font_fallback" : "ui_font_specific"));
    }


    // Setup palette
    appCtx->palette[COL_BG]        = (SDL_Color){50,52,55,255};
    appCtx->palette[COL_TEXT]      = (SDL_Color){100,102,105,255};
    appCtx->palette[COL_CORRECT]   = (SDL_Color){201,200,190,255};
    appCtx->palette[COL_INCORRECT] = (SDL_Color){200,0,0,255};
    appCtx->palette[COL_CURSOR]    = (SDL_Color){255,200,0,255};

    // Calculate logical line height for main font
    int scaled_line_h_px = TTF_FontLineSkip(appCtx->font);
    if (scaled_line_h_px <= 0) scaled_line_h_px = TTF_FontHeight(appCtx->font);
    if (scaled_line_h_px <= 0) scaled_line_h_px = (int)((FONT_SIZE + 4) * appCtx->scale_y_factor);
    if (scaled_line_h_px <= 0 && appCtx->scale_y_factor > 0.01f) scaled_line_h_px = (int)(18 * appCtx->scale_y_factor); // Min scaled height
    else if (scaled_line_h_px <= 0) scaled_line_h_px = 18;

    appCtx->line_h = (appCtx->scale_y_factor > 0.01f) ? (int)roundf((float)scaled_line_h_px / appCtx->scale_y_factor) : (FONT_SIZE + 4);
    if (appCtx->line_h <= 0) appCtx->line_h = FONT_SIZE + 4; // Ensure positive logical line height

    if (appCtx->log_file_handle) {
        fprintf(appCtx->log_file_handle, "Main Font line height: scaled_px=%d, scale_y_factor=%.2f -> logical line_h=%d\n",
                scaled_line_h_px, appCtx->scale_y_factor, appCtx->line_h);
    }

    // Cache ASCII glyphs (for main font only)
    for (int c = 32; c < 127; c++) {
        int scaled_adv_px;
        if (TTF_GlyphMetrics(appCtx->font, (Uint16)c, NULL, NULL, NULL, NULL, &scaled_adv_px) != 0) {
            scaled_adv_px = (int)((FONT_SIZE / 2.0f) * appCtx->scale_x_factor);
             if (scaled_adv_px <= 0) scaled_adv_px = (int)(7 * appCtx->scale_x_factor); // min scaled
        }
        if (scaled_adv_px <= 0 && appCtx->scale_x_factor > 0.01f) scaled_adv_px = (int)(1 * appCtx->scale_x_factor); // min if font returns 0
        else if (scaled_adv_px <=0) scaled_adv_px = FONT_SIZE/2;

        appCtx->glyph_adv_cache[c] = (appCtx->scale_x_factor > 0.01f) ? (int)roundf((float)scaled_adv_px / appCtx->scale_x_factor) : (FONT_SIZE / 2);
        if (appCtx->glyph_adv_cache[c] <= 0 && scaled_adv_px > 0) appCtx->glyph_adv_cache[c] = 1;
        else if (appCtx->glyph_adv_cache[c] <= 0) appCtx->glyph_adv_cache[c] = FONT_SIZE / 2 > 1 ? FONT_SIZE / 2 : 1;

        for (int col_idx = COL_TEXT; col_idx <= COL_INCORRECT; col_idx++) {
            SDL_Surface *surf = TTF_RenderGlyph_Blended(appCtx->font, (Uint16)c, appCtx->palette[col_idx]);
            if (!surf) continue;

            // Log metrics for 'H' glyph from main font for comparison / debug
            if (c == 'H' && col_idx == COL_TEXT && appCtx->log_file_handle) {
                fprintf(appCtx->log_file_handle, "DEBUG: Main Font Glyph 'H' metrics: surf->w=%d, surf->h=%d (target_vdpi=%u, scale_y_factor=%.2f, FONT_SIZE_logical=%d)\n",
                        surf->w, surf->h,
                        (unsigned int)(72 * appCtx->scale_y_factor), // This DPI was used for main font
                        appCtx->scale_y_factor,
                        FONT_SIZE);
            }

            appCtx->glyph_w_cache[col_idx][c] = (appCtx->scale_x_factor > 0.01f && surf->w > 0) ? (int)roundf((float)surf->w / appCtx->scale_x_factor) : surf->w;
            appCtx->glyph_h_cache[col_idx][c] = (appCtx->scale_y_factor > 0.01f && surf->h > 0) ? (int)roundf((float)surf->h / appCtx->scale_y_factor) : surf->h;
            if (surf->w > 0 && appCtx->glyph_w_cache[col_idx][c] <= 0) appCtx->glyph_w_cache[col_idx][c] = 1;
            if (surf->h > 0 && appCtx->glyph_h_cache[col_idx][c] <= 0) appCtx->glyph_h_cache[col_idx][c] = 1;

            appCtx->glyph_tex_cache[col_idx][c] = SDL_CreateTextureFromSurface(appCtx->ren, surf);
            if (!appCtx->glyph_tex_cache[col_idx][c] && appCtx->log_file_handle) {
                 fprintf(appCtx->log_file_handle, "Warning: Failed to create texture for glyph %c (ASCII %d) color %d. SDL Error: %s\n", c, c, col_idx, SDL_GetError());
            }
            SDL_FreeSurface(surf);
        }
    }
    appCtx->space_advance_width = appCtx->glyph_adv_cache[' '];
    if (appCtx->space_advance_width <= 0) appCtx->space_advance_width = (int)(FONT_SIZE / 3.0f);
    if (appCtx->space_advance_width <= 0) appCtx->space_advance_width = 1;


    appCtx->tab_width_pixels = (appCtx->space_advance_width > 0) ? (TAB_SIZE_IN_SPACES * appCtx->space_advance_width) : (int)(TAB_SIZE_IN_SPACES * (FONT_SIZE / 3.0f));
    if (appCtx->tab_width_pixels <= 0) appCtx->tab_width_pixels = TAB_SIZE_IN_SPACES;

    // State initialization
    appCtx->typing_started = false;
    appCtx->start_time_ms = 0;
    appCtx->time_at_pause_ms = 0;
    appCtx->is_paused = false;
    // appCtx->l_modifier_held = false; // REMOVED - these were the cause of the error
    // appCtx->r_modifier_held = false; // REMOVED - these were the cause of the error
    // New modifier flags (l_alt_modifier_held, r_alt_modifier_held, l_cmd_modifier_held, r_cmd_modifier_held)
    // are already initialized to false by the memset at the beginning of this function.

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

    // Free cached glyph textures (for main font)
    for (int c = 32; c < 127; c++) {
        for (int col = COL_TEXT; col <= COL_INCORRECT; col++) {
            if (appCtx->glyph_tex_cache[col][c]) {
                SDL_DestroyTexture(appCtx->glyph_tex_cache[col][c]);
                appCtx->glyph_tex_cache[col][c] = NULL;
            }
        }
    }

    // Close fonts
    if (appCtx->ui_font && appCtx->ui_font != appCtx->font) { // Close UI font only if it's a separate loaded font
        TTF_CloseFont(appCtx->ui_font);
    }
    appCtx->ui_font = NULL; // Set to NULL regardless, to avoid double free if it was a fallback

    if (appCtx->font) {
        TTF_CloseFont(appCtx->font);
        appCtx->font = NULL;
    }

    // Cleanup SDL subsystems
    if (appCtx->ren) { SDL_DestroyRenderer(appCtx->ren); appCtx->ren = NULL; }
    if (appCtx->win) { SDL_DestroyWindow(appCtx->win); appCtx->win = NULL; }

    TTF_Quit();
    SDL_Quit();

    // Close log file
    if (appCtx->log_file_handle) {
        fprintf(appCtx->log_file_handle, "Application context cleaned up.\nApplication finished.\n");
        fflush(appCtx->log_file_handle);
        fclose(appCtx->log_file_handle);
        appCtx->log_file_handle = NULL;
    }
}
