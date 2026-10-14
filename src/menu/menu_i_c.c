#include "common.h"
#include "menu/menu_i.h"
#include "sys/pad.h"

extern void Port_DiagBlock(const char *what, const void *p, u32 bytes); /* bring-up (iOS) */
extern void Port_DiagNote(const char *what, long a, long b);

Logo *gLogo = NULL; /* 0x3B590C */

/*
 * Logo / FirstRun, 0x35D948..0x35E0F8: what the overlay does the first time it runs after boot
 * (Progress_Main calls FirstRun_Main when gProgress->flags has MPROG_FIRST_RUN): start loading partition 2 in
 * the background, wait for a controller, check the memory card, show the boot pictures, wait for the partition
 * behind the loading screen if it is not in yet, and play the opening movie.
 */

/* Allocates the logo work and relocates picture `section` of the unpacked pack. */
void Logo_Init(u32 *pack, s32 section, s32 frames) {
    MTexRes *res = NULL;
    u32 *e;

    gLogo = Heap_Alloc(0x24, 0x20, 0, 2);
    memset(gLogo, 0, 0x24);
    res = (MTexRes *)MPACK_AT(pack, section);
    Port_DiagBlock("logo res before relocation", res, 0x80);
    Res_RelocateOffsets(&res, res, res);
    e = (u32 *)MTEX(res, 0); /* entry 0 as words: pixOfs, clutOfs, pixSize, clutSize (offsets in words from the section) */
    Port_DiagNote("logo: section, entries", section, (long)((u32 *)res)[0]);
    Port_DiagBlock("logo res after relocation", res, 0x80);
    Port_DiagBlock("logo entry 0", e, 0x40);
    Port_DiagBlock("logo pixels 0", (u8 *)res + e[0] * 4, (u32)e[2]);
    Port_DiagBlock("logo clut 0", (u8 *)res + e[1] * 4, (u32)e[3]);
    gLogo->pic = res;
    gLogo->frames = frames;
}

/* Frees the logo work. */
void Logo_Term(void) {
    if (gLogo != NULL) {
        Heap_Free(gLogo);
        gLogo = NULL;
    }
}

#define LOGO_FADE_OUT(black, n) \
    if (black) { \
        ColorFade_StartOut(0, 0, 0, n); \
    } else { \
        ColorFade_StartOut(0xFF, 0xFF, 0xFF, n); \
    }

/* bring-up (iOS): is the first picture of the logo still in memory? (the blocks arrive at the GS as zeros) */
static void Logo_Probe(const char *when) {
    u32 *e;

    if (gLogo == NULL) {
        return;
    }
    e = (u32 *)MTEX((MTexRes *)gLogo->pic, 0);
    Port_DiagBlock(when, (u8 *)gLogo->pic + e[0] * 4, (u32)e[2]);
}

/* bring-up (iOS): says when the number of non-zero words of the first picture changes (and the iteration it happens in) */
static void Logo_Watch(s32 it, u32 *last) {
    u32 *e, *w, k, n, nz = 0;

    if (gLogo == NULL) {
        return;
    }
    e = (u32 *)MTEX((MTexRes *)gLogo->pic, 0);
    w = (u32 *)((u8 *)gLogo->pic + e[0] * 4);
    n = (u32)e[2] / 4;
    for (k = 0; k < n; k++) {
        if (w[k] != 0) {
            nz++;
        }
    }
    if (nz != *last) {
        Port_DiagNote("logo pixels: iteration, words not zero", it, (long)nz);
        *last = nz;
    }
}

/*
 * Shows one full-screen picture: optional fade out of what is on screen first, fade in (from black or white),
 * `frames` + 2 frames, fade out (to black or white). The background partition load is polled every frame.
 */
void Logo_Show(u32 *pack, s32 section, s32 frames, s32 fade, s32 skip, s32 inBlack, s32 outBlack, s32 fadeFirst) {
    s32 it = 0;
    u32 last = 0xFFFFFFFFu;

    Logo_Init(pack, section, frames);
    if (fadeFirst) {
        LOGO_FADE_OUT(inBlack, fade);
        do {
            do {
                Gfx_BeginFrame();
                Pad_Update();
                ColorFade_Update();
                File_WaitPartitionTimeout(2);
                ColorFade_Draw();
                Gfx_EndFrame(1);
                Dma_Flush();
                File_Stub264D90();
            } while (ColorFade_IsFadingOut());
        } while (!ColorFade_IsOutDone());
    }
    if (inBlack) {
        ColorFade_StartIn(0, 0, 0, fade);
    } else {
        ColorFade_StartIn(0xFF, 0xFF, 0xFF, fade);
    }
    while (1) {
        if (it < 1) { Port_DiagNote("logo loop, iteration", it, section); Logo_Probe("probe: loop top"); }
        Gfx_BeginFrame();
        Pad_Update();
        ColorFade_Update();
        if (it < 1) { Logo_Probe("probe: before Sprite_DrawPicture"); }
        Sprite_DrawPicture(gLogo->pic, 0, 0, 0x80);
        if (it < 1) { Logo_Probe("probe: after Sprite_DrawPicture"); }
        File_WaitPartitionTimeout(2);
        if (it < 1) { Logo_Probe("probe: after File_WaitPartitionTimeout"); }
        ColorFade_Draw();
        Gfx_EndFrame(1);
        if (it < 1) { Logo_Probe("probe: after Gfx_EndFrame"); }
        Dma_Flush();
        if (it < 1) { Logo_Probe("probe: after Dma_Flush"); }
        Logo_Watch(it, &last);
        it++;
        File_Stub264D90();
        if (ColorFade_IsInDone()) {
            gLogo->flags |= LOGO_FADED_IN;
        }
        if (ColorFade_IsFadingOut()) {
            continue;
        }
        if (ColorFade_IsOutDone()) {
            break;
        }
        if (gLogo->flags & LOGO_FADED_IN) {
            gLogo->timer++;
            if (gLogo->timer >= gLogo->frames + 2) {
                LOGO_FADE_OUT(outBlack, fade);
            }
        }
        switch (skip) {
        case LOGO_SKIP_ANY:
            if (gPad[0].gamePressed & (PADG_START | PADG_CROSS)) {
                LOGO_FADE_OUT(outBlack, fade / 2);
            }
            break;
        case LOGO_SKIP_AFTER_60:
            if (gLogo->timer >= 60) {
                if (gPad[0].gamePressed & (PADG_START | PADG_CROSS)) {
                    LOGO_FADE_OUT(outBlack, fade);
                }
            }
            break;
        }
    }
    sceGsSyncPath(0, 0);
    Logo_Term();
    Dma_ResetBuffers();
}

/* The six boot pictures in order: sections 6, 7, 8, 3, 4, 5 of the unpacked pack, 120 frames each. */
void Logo_ShowAll(u32 *pack) {
    Logo_Show(pack, 6, 0x78, 0x3C, LOGO_SKIP_ANY, 1, 0, 0);
    Logo_Show(pack, 7, 0x78, 0x3C, LOGO_SKIP_ANY, 0, 0, 0);
    Logo_Show(pack, 8, 0x78, 0x3C, LOGO_SKIP_ANY, 0, 0, 0);
    Logo_Show(pack, 3, 0x78, 0x3C, LOGO_SKIP_ANY, 0, 0, 0);
    Logo_Show(pack, 4, 0x78, 0x3C, LOGO_SKIP_ANY, 0, 0, 0);
    Logo_Show(pack, 5, 0x78, 0x3C, LOGO_SKIP_NONE, 0, 1, 0);
}

/* If partition `pt` is not loaded yet, shows the loading screen until it is. */
void FirstRun_WaitPartition(s32 pt) {
    s32 flags = 0;

    if (File_IsPartitionLoaded(pt)) {
        return;
    }
    Dma_ResetBuffers();
    Load_InitScreen();
    ColorFade_StartIn(0, 0, 0, 0xF);
    while (1) {
        Snd_Update();
        Gfx_BeginFrame();
        Pad_Update();
        ColorFade_Update();
        File_WaitPartitionTimeout(pt);
        if (File_IsPartitionLoaded(pt)) {
            if (flags & 1) {
                if (!(flags & 2)) {
                    ColorFade_StartOut(0, 0, 0, 0xF);
                    flags |= 2;
                }
            }
        }
        Load_UpdateScreen();
        Load_DrawScreen();
        ColorFade_Draw();
        Gfx_EndFrame(2);
        Dma_Flush();
        if (ColorFade_IsInDone()) {
            flags |= 1;
        }
        if (ColorFade_IsOutDone()) {
            break;
        }
        Load_ReadInput();
    }
    Dma_ResetBuffers();
}

/* The first-run sequence. Returns 1. */
s32 FirstRun_Main(void) {
    u32 *res;

    File_LoadPartitionNw(2);
    FirstRun_WaitPad(0, 0);
    PadWatch_SetEnabled(0);
    if (gMenuArc0 == NULL) {
        gMenuArc0 = File_LoadSync(gProgress->baseFile + 0x19, NULL, 0);
    }
    BootCard_Run(2);
    res = Sprite_Unpack(MPACK_AT(gMenuArc0, 3), NULL, NULL);
    Logo_ShowAll(res);
    if (res != NULL) {
        Heap_Free(res);
    }
    if (gMenuArc0 != NULL) {
        Heap_Free(gMenuArc0);
        gMenuArc0 = NULL;
    }
    FirstRun_WaitPartition(2);
    Movie_PlayOpening();
    PadWatch_SetEnabled(1);
    return 1;
}

/* Runs empty frames until controller port 0 has a usable controller. Returns 1. */
s32 FirstRun_WaitPad() {
    Pad_Update();
    if (gPad[0].status == PAD_STATUS_NONE) {
        do {
            Gfx_BeginFrame();
            Pad_Update();
            File_WaitPartitionTimeout(2);
            Gfx_EndFrame(1);
            Dma_Flush();
            File_Stub264D90();
        } while (gPad[0].status == PAD_STATUS_NONE);
        Dma_ResetBuffers();
    }
    sceGsSyncPath(0, 0);
    return 1;
}
