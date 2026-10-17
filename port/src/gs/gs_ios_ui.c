/*
 * iOS: the screen the install (plat_ios_install.c) shows while the game's data is unpacked from the user's disc
 * image: a black window with four lines of text and a progress bar. It is an SDL window and renderer of its own,
 * taken down (GsIosUi_Done) before the game opens its own (gs_draw.c) — the same shape the old port had, and the
 * reason the unpacking runs from game_entry (plat_mem.c) and not from a constructor: iOS kills a launch that takes
 * this long.
 *
 * This file is on the hard-float road (port/src/gs, like the rest of the host code: SDL_RenderDebugText and
 * SDL_FRect are floats), while the installer's is soft-float (port/src): only integers and strings cross between
 * them. There is no window (SDL refuses, a platform that is not iOS): every call returns and the log is the report.
 */
#ifdef BT3_IOS
#include <SDL3/SDL.h>
#include <stdio.h>

static SDL_Window *sWin;
static SDL_Renderer *sRen;

static int ui_open(void) {
    if (sRen != NULL) {
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "bt3: the install's window: SDL could not start: %s\n", SDL_GetError());
        return 0;
    }
    if (!SDL_CreateWindowAndRenderer("Dragon Rage", 0, 0, SDL_WINDOW_FULLSCREEN, &sWin, &sRen)) {
        fprintf(stderr, "bt3: the install's window could not be made: %s\n", SDL_GetError());
        sWin = NULL;
        sRen = NULL;
        return 0;
    }
    SDL_DisableScreenSaver();
    return 1;
}

static void ui_close(void) {
    if (sRen != NULL) {
        SDL_DestroyRenderer(sRen);
        sRen = NULL;
    }
    if (sWin != NULL) {
        SDL_DestroyWindow(sWin);
        sWin = NULL;
    }
}

/* The window's events, dropped: nothing here is interactive (the game has not started; the fingers that land on
   this screen must not reach the pad, and gs_input.c's PortInput_Touch returns while there is no game window). */
static void ui_pump(void) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
    }
}

/* a, b, c, d: the lines (NULL or "" for none). permille < 0: no bar. red: in red (something cannot be done). */
int GsIosUi_Show(const char *a, const char *b, const char *c, const char *d, int permille, int red) {
    const char *line[4];
    int w = 0, h = 0, i;
    float x = 24.0f, y = 26.0f;

    line[0] = a;
    line[1] = b;
    line[2] = c;
    line[3] = d;
    if (!ui_open()) {
        return 0;
    }
    SDL_GetWindowSize(sWin, &w, &h);
    (void)h;
    SDL_SetRenderDrawColor(sRen, 0, 0, 0, 255);
    SDL_RenderClear(sRen);
    SDL_SetRenderScale(sRen, 2.0f, 2.0f);
    if (red != 0) {
        SDL_SetRenderDrawColor(sRen, 255, 120, 120, 255);
    } else {
        SDL_SetRenderDrawColor(sRen, 240, 240, 240, 255);
    }
    for (i = 0; i < 4; i++) {
        if (line[i] != NULL && line[i][0] != '\0') {
            SDL_RenderDebugText(sRen, x, y + (float)i * 22.0f, line[i]);
        }
    }
    if (permille >= 0) { /* the bar: its frame, and inside it how far the unpacking is */
        SDL_FRect frame, filled;
        float barW = (float)w / 2.0f - 2.0f * x;

        if (barW < 40.0f) {
            barW = 40.0f;
        }
        if (permille > 1000) {
            permille = 1000;
        }
        frame.x = x;
        frame.y = y + 4.0f * 22.0f + 14.0f;
        frame.w = barW;
        frame.h = 24.0f;
        filled = frame;
        filled.x += 2.0f;
        filled.y += 2.0f;
        filled.h -= 4.0f;
        filled.w = (barW - 4.0f) * (float)permille / 1000.0f;
        SDL_RenderFillRect(sRen, &filled);
        SDL_RenderRect(sRen, &frame);
    }
    SDL_RenderPresent(sRen);
    ui_pump();
    return 1;
}

/* "Ready": a moment more (the lines are read), the window goes away, and the game opens its own. */
void GsIosUi_Done(void) {
    GsIosUi_Show("Listo", "Datos instalados: ya puedes borrar la ISO", "de la carpeta para recuperar espacio.", NULL, 1000, 0);
    SDL_Delay(1500);
    ui_close();
}

/* The message stays on the screen until the app is closed (it has to be read, and the log can wait). With no
   window (SDL would not start), it returns at once and the caller fails on its own: the log is the report. */
void GsIosUi_Stuck(void) {
    if (!ui_open()) {
        return;
    }
    for (;;) {
        ui_pump();
        SDL_Delay(100);
    }
}
#endif /* BT3_IOS */
