// main.c
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WINDOW_W     800
#define WINDOW_H     200
#define FONT_SIZE    22
#define MAX_TEXT_LEN 10000
#ifndef TEXT_FILE_PATH
#define TEXT_FILE_PATH "text.txt"
#endif

enum { COL_BG, COL_TEXT, COL_CORRECT, COL_INCORRECT, COL_CURSOR, N_COLORS };

int main(int argc, char **argv) {
    // 1) Load text
    const char *path = (argc > 1 ? argv[1] : TEXT_FILE_PATH);
    FILE *f = fopen(path, "r");
    if (!f) { perror("Failed to open text file"); return 1; }
    char *text = malloc(MAX_TEXT_LEN);
    size_t text_len = fread(text, 1, MAX_TEXT_LEN - 1, f);
    fclose(f);
    while (text_len > 0 && (text[text_len - 1] == '\n' || text[text_len - 1] == '\r')) text_len--;
    text[text_len] = '\0';

    // 2) Init SDL + TTF
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        return 1;
    }
    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init error: %s\n", TTF_GetError());
        SDL_Quit();
        return 1;
    }
    TTF_SetFontHinting(NULL, TTF_HINTING_LIGHT);

    // 3) Window & renderer with High-DPI support
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");
    SDL_Window *win = SDL_CreateWindow(
        "TypingApp",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_W, WINDOW_H,
        SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI
    );
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    // Adjust for DPI
    int physW, physH;
    SDL_GetRendererOutputSize(ren, &physW, &physH);
    float scale_x = (float)physW / WINDOW_W;
    float scale_y = (float)physH / WINDOW_H;
    SDL_RenderSetScale(ren, scale_x, scale_y);

    // 4) Load font with DPI
#if SDL_TTF_VERSION_ATLEAST(2,20,0)
    TTF_Font *font = TTF_OpenFontDPI(
        "/System/Library/Fonts/Arial.ttf",
        FONT_SIZE,
        (int)(72 * scale_x),
        (int)(72 * scale_y)
    );
#else
    TTF_Font *font = TTF_OpenFont("/System/Library/Fonts/Arial.ttf", FONT_SIZE);
#endif
    if (!font)
        font = TTF_OpenFont("/Library/Fonts/Arial Unicode.ttf", FONT_SIZE);

    // 5) Palette & glyph cache + metrics
    SDL_Color palette[N_COLORS] = {
        {50, 52, 55, 255},   // background
        {100,102,105,255},   // to-type
        {201,200,190,255},   // correct
        {200,  0,  0,255},   // incorrect
        {255,200,  0,255},   // cursor
    };

    int ascent = TTF_FontAscent(font);
    int descent = TTF_FontDescent(font);
    int line_h = ascent - descent;

    SDL_Texture *glyph_tex[N_COLORS][128] = {{NULL}};
    int glyph_adv[128], glyph_w[N_COLORS][128], glyph_h[N_COLORS][128];
    for (int c = 32; c < 127; c++) {
        int minx, maxx, miny, maxy, adv;
        TTF_GlyphMetrics(font, c, &minx, &maxx, &miny, &maxy, &adv);
        glyph_adv[c] = adv;
        for (int col = COL_TEXT; col <= COL_INCORRECT; col++) {
            SDL_Surface *surf = TTF_RenderGlyph_Blended(font, c, palette[col]);
            glyph_w[col][c] = surf->w;
            glyph_h[col][c] = surf->h;
            glyph_tex[col][c] = SDL_CreateTextureFromSurface(ren, surf);
            SDL_FreeSurface(surf);
        }
    }

    // 6) Pre-render background text
    SDL_Surface *surf_bg = TTF_RenderText_Blended_Wrapped(
        font, text, palette[COL_TEXT], WINDOW_W - 20
    );
    SDL_Texture *tex_bg = SDL_CreateTextureFromSurface(ren, surf_bg);
    int bgW, bgH;
    SDL_QueryTexture(tex_bg, NULL, NULL, &bgW, &bgH);
    SDL_FreeSurface(surf_bg);

    // 7) Input state
    char *input = calloc(text_len + 1, 1);
    size_t idx = 0;
    Uint32 start = SDL_GetTicks();
    SDL_StartTextInput();
    bool show_cursor = true;
    Uint32 last = SDL_GetTicks();

    while (1) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) goto cleanup;
            if (ev.type == SDL_KEYDOWN) {
                if (ev.key.keysym.sym == SDLK_ESCAPE) goto cleanup;
                if (ev.key.keysym.sym == SDLK_BACKSPACE && idx > 0)
                    input[--idx] = '\0';
            }
            if (ev.type == SDL_TEXTINPUT && idx < text_len)
                input[idx++] = ev.text.text[0];
        }
        // cursor blink
        if (SDL_GetTicks() - last > 500) {
            show_cursor = !show_cursor;
            last = SDL_GetTicks();
        }
        // clear
        SDL_SetRenderDrawColor(
            ren,
            palette[COL_BG].r,
            palette[COL_BG].g,
            palette[COL_BG].b,
            palette[COL_BG].a
        );
        SDL_RenderClear(ren);

        // timer
        Uint32 elapsed = (SDL_GetTicks() - start) / 1000;
        int m = elapsed / 60, s = elapsed % 60;
        char tb[16];
        snprintf(tb, sizeof(tb), "%02d:%02d", m, s);
        SDL_Surface *ts = TTF_RenderText_Blended(font, tb, palette[COL_CURSOR]);
        SDL_Texture *tt = SDL_CreateTextureFromSurface(ren, ts);
        int tw, th;
        TTF_SizeText(font, tb, &tw, &th);
        SDL_Rect rtimer = { 10, 10, tw, th };
        SDL_RenderCopy(ren, tt, NULL, &rtimer);
        SDL_FreeSurface(ts);
        SDL_DestroyTexture(tt);

        // draw background text
        SDL_Rect rbg = {10, 10 + th + 10, bgW, bgH};
        SDL_RenderCopy(ren, tex_bg, NULL, &rbg);

        // overlay typed
        int x = rbg.x;
        int y0 = rbg.y;
        for (size_t i = 0; i < idx; i++) {
            unsigned char ch = text[i];
            int col = (input[i] == text[i] ? COL_CORRECT : COL_INCORRECT);
            SDL_Rect dst = {
                x,
                y0 + (line_h - glyph_h[col][ch]) / 2,
                glyph_w[col][ch],
                glyph_h[col][ch]
            };
            SDL_RenderCopy(ren, glyph_tex[col][ch], NULL, &dst);
            x += glyph_adv[ch];
        }
        // cursor
        if (show_cursor && idx < text_len) {
            SDL_Rect cur = { x, y0, 2, line_h };
            SDL_SetRenderDrawColor(
                ren,
                palette[COL_CURSOR].r,
                palette[COL_CURSOR].g,
                palette[COL_CURSOR].b,
                palette[COL_CURSOR].a
            );
            SDL_RenderFillRect(ren, &cur);
        }
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

cleanup:
    SDL_StopTextInput();
    double total = (SDL_GetTicks() - start) / 1000.0;
    double wpm = ((double)idx / 5) / (total / 60.0);
    printf("Typed: %zu chars, WPM: %.2f\n", idx, wpm);
    for (int c = 32; c < 127; c++)
        for (int col = COL_TEXT; col <= COL_INCORRECT; col++)
            SDL_DestroyTexture(glyph_tex[col][c]);
    SDL_DestroyTexture(tex_bg);
    TTF_CloseFont(font);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    TTF_Quit();
    SDL_Quit();
    free(text);
    free(input);
    return 0;
}
