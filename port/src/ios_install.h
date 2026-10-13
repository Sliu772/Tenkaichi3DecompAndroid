/*
 * iOS: installs the game's data from the user's own disc image, on the device (no PC needed).
 *
 * The user copies the .iso into the app's folder (Files app > On My iPhone > BT3). On the next start, if
 * gamedata/disc/SLUS_216.78 and gamedata/disc/BIN/DBZP.BIN are not there yet, ios_prepare_data() (called by
 * plat_ios.c before the game starts) finds the .iso in Documents, reads its ISO 9660 directory tree and copies
 *
 *     SLUS_216.78, everything under BIN/ and DATA/   ->   gamedata/disc/<same path, upper case>
 *
 * The AFS archives are copied whole, as the Android installer does (android/.../GameDataInstaller.kt): the engine
 * reads their entries through the archive's table (plat_fcache.c). Progress is drawn with an SDL renderer
 * (SDL_RenderDebugText) that is destroyed before the game opens its own window. Errors stay on the screen.
 *
 * Whether the image is the unmodified USA release is decided later by the engine itself (the hash check in
 * plat_mem.c, "not the unmodified USA release"); here only the files the game needs have to be on the disc.
 *
 * Host code (hard float, 8-byte pointers). Everything is static: plat_ios.c includes this file. A test can define
 * BT3_INSTALL_TEST and provide its own SDL_* functions.
 */
#ifndef BT3_IOS_INSTALL_H
#define BT3_IOS_INSTALL_H

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

typedef struct { float x, y, w, h; } Ios_FRect; /* SDL_FRect */
extern bool SDL_SetHint(const char *name, const char *value);
extern bool SDL_Init(uint32_t flags);
extern bool SDL_CreateWindowAndRenderer(const char *title, int w, int h, uint64_t flags, void **window, void **renderer);
extern bool SDL_GetWindowSize(void *window, int *w, int *h);
extern bool SDL_SetRenderDrawColor(void *renderer, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
extern bool SDL_RenderClear(void *renderer);
extern bool SDL_SetRenderScale(void *renderer, float x, float y);
extern bool SDL_RenderDebugText(void *renderer, float x, float y, const char *str);
extern bool SDL_RenderFillRect(void *renderer, const Ios_FRect *rect);
extern bool SDL_RenderRect(void *renderer, const Ios_FRect *rect);
extern bool SDL_RenderPresent(void *renderer);
extern void SDL_DestroyRenderer(void *renderer);
extern void SDL_DestroyWindow(void *window);
extern bool SDL_PollEvent(void *event);
extern bool SDL_DisableScreenSaver(void);
extern void SDL_Delay(uint32_t ms);

#define INST_SECTOR 2048u
#define INST_CHUNK (8u << 20)
#define INST_SPARE (256ull << 20) /* free space left over after the copy */

/* ------------------------------------------------------------------------------------------------------------- */
/* The screen: a few lines of text and a progress bar. Without a window everything goes to the log only.          */

static void *sInstWin, *sInstRen;
static int sInstW, sInstH;

static int inst_ui_open(void) {
    if (sInstRen != NULL) {
        return 1;
    }
    SDL_SetHint("SDL_RENDER_DRIVER", "metal"); /* (reset in inst_ui_close: the game does not use SDL_Renderer) */
    if (!SDL_Init(0x20u /* SDL_INIT_VIDEO */)) {
        return 0;
    }
    if (!SDL_CreateWindowAndRenderer("BT3", 0, 0, 1u /* SDL_WINDOW_FULLSCREEN */, &sInstWin, &sInstRen)) {
        sInstWin = sInstRen = NULL;
        return 0;
    }
    SDL_GetWindowSize(sInstWin, &sInstW, &sInstH);
    SDL_DisableScreenSaver(); /* a copy of several GB must not be interrupted by the screen locking */
    return 1;
}

static void inst_ui_close(void) {
    if (sInstRen != NULL) {
        SDL_DestroyRenderer(sInstRen);
    }
    if (sInstWin != NULL) {
        SDL_DestroyWindow(sInstWin);
    }
    sInstRen = sInstWin = NULL;
    SDL_SetHint("SDL_RENDER_DRIVER", NULL);
}

/* Keeps UIKit's run loop alive and lets the user quit. */
static void inst_ui_events(void) {
    uint64_t ev[32];

    while (SDL_PollEvent(ev)) {
        if (*(uint32_t *)ev == 0x100u /* SDL_EVENT_QUIT */) {
            exit(0);
        }
    }
}

/* frac < 0: no bar. err != 0: the text is red. Lines should be at most about 44 characters (8x8 font at scale 2). */
static void inst_show(const char *a, const char *b, const char *c, const char *d, float frac, int err) {
    const char *line[4];
    float scale = 2.0f, w, x = 24.0f, y = 24.0f;
    int i;

    line[0] = a;
    line[1] = b;
    line[2] = c;
    line[3] = d;
    for (i = 0; i < 4; i++) {
        if (line[i] != NULL) {
            fprintf(stderr, "bt3 install: %s\n", line[i]);
        }
    }
    if (!inst_ui_open()) {
        return;
    }
    SDL_SetRenderDrawColor(sInstRen, 0, 0, 0, 255);
    SDL_RenderClear(sInstRen);
    SDL_SetRenderScale(sInstRen, scale, scale);
    if (err) {
        SDL_SetRenderDrawColor(sInstRen, 255, 110, 110, 255);
    } else {
        SDL_SetRenderDrawColor(sInstRen, 255, 255, 255, 255);
    }
    for (i = 0; i < 4; i++) {
        if (line[i] != NULL) {
            SDL_RenderDebugText(sInstRen, x, y + 14.0f * (float)i, line[i]);
        }
    }
    if (frac >= 0.0f) {
        Ios_FRect box, fill;
        w = (float)sInstW / scale - 2.0f * x;
        if (w < 40.0f) {
            w = 40.0f;
        }
        box.x = x;
        box.y = y + 14.0f * 4.0f + 8.0f;
        box.w = w;
        box.h = 10.0f;
        fill = box;
        fill.w = w * (frac > 1.0f ? 1.0f : frac);
        SDL_SetRenderDrawColor(sInstRen, 255, 150, 0, 255);
        SDL_RenderFillRect(sInstRen, &fill);
        SDL_SetRenderDrawColor(sInstRen, 255, 255, 255, 255);
        SDL_RenderRect(sInstRen, &box);
    }
    SDL_RenderPresent(sInstRen);
    inst_ui_events();
}

/* An error stays on the screen until the user closes the app (a test just exits). */
static void inst_stuck(void) {
#ifdef BT3_INSTALL_TEST
    exit(3);
#else
    for (;;) {
        inst_ui_events();
        SDL_Delay(100);
    }
#endif
}

/* ------------------------------------------------------------------------------------------------------------- */
/* ISO 9660: the primary volume descriptor at sector 16, then the directory records from the root.                */

typedef struct {
    char path[200]; /* as on the disc without ";1", e.g. "DATA/PZS3US1.AFS" */
    uint64_t off, size;
} InstFile;

typedef struct {
    int fd;
    InstFile *f;
    int n, cap;
} InstIso;

static uint32_t inst_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int inst_pread(int fd, uint64_t off, void *buf, size_t len) {
    uint8_t *p = (uint8_t *)buf;

    while (len > 0) {
        ssize_t n = pread(fd, p, len, (off_t)off);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return -1; /* the image ends early (incomplete file?) */
        }
        p += n;
        off += (uint64_t)n;
        len -= (size_t)n;
    }
    return 0;
}

/* What the game needs from the disc, plus the root's serial file (SLES_xxx.xx ...) to say which release it is. */
static int inst_wanted(const char *path) {
    char u[200];
    size_t i, n = strlen(path);

    if (n >= sizeof(u)) {
        return 0;
    }
    for (i = 0; i <= n; i++) {
        u[i] = (char)toupper((unsigned char)path[i]);
    }
    if (strcmp(u, "SLUS_216.78") == 0 || strncmp(u, "BIN/", 4) == 0 || strncmp(u, "DATA/", 5) == 0) {
        return 1;
    }
    return n == 11 && u[0] == 'S' && u[4] == '_' && u[8] == '.' && strchr(u, '/') == NULL;
}

static int inst_add(InstIso *iso, const char *path, uint64_t off, uint64_t size) {
    if (iso->n > 0 && strcmp(iso->f[iso->n - 1].path, path) == 0) {
        iso->f[iso->n - 1].size += size; /* a file in several extents */
        return 0;
    }
    if (iso->n == iso->cap) {
        int cap = iso->cap != 0 ? iso->cap * 2 : 256;
        InstFile *g = (InstFile *)realloc(iso->f, (size_t)cap * sizeof(InstFile));
        if (g == NULL) {
            return -1;
        }
        iso->f = g;
        iso->cap = cap;
    }
    snprintf(iso->f[iso->n].path, sizeof(iso->f[iso->n].path), "%s", path);
    iso->f[iso->n].off = off;
    iso->f[iso->n].size = size;
    iso->n++;
    return 0;
}

static int inst_walk(InstIso *iso, uint64_t lba, uint32_t size, const char *dir, int depth) {
    uint8_t *d;
    uint32_t pos = 0;

    if (depth > 8 || size == 0 || size > (16u << 20)) {
        return 0;
    }
    d = (uint8_t *)malloc(size);
    if (d == NULL) {
        return -1;
    }
    if (inst_pread(iso->fd, lba * INST_SECTOR, d, size) != 0) {
        free(d);
        return -1;
    }
    while (pos + 34 <= size) {
        uint32_t len = d[pos], extent, bytes, nameLen;
        int flags;
        char name[128], path[200];

        if (len == 0) { /* records do not cross sectors: the rest of this one is padding */
            pos = (pos / INST_SECTOR + 1) * INST_SECTOR;
            continue;
        }
        if (len < 34 || pos + len > size) {
            break;
        }
        extent = inst_le32(d + pos + 2);
        bytes = inst_le32(d + pos + 10);
        flags = d[pos + 25];
        nameLen = d[pos + 32];
        if (nameLen > len - 33) {
            nameLen = len - 33;
        }
        if (nameLen >= sizeof(name)) {
            nameLen = sizeof(name) - 1;
        }
        memcpy(name, d + pos + 33, nameLen);
        name[nameLen] = '\0';
        pos += len;
        if (nameLen == 1 && (name[0] == '\0' || name[0] == '\1')) { /* "." and ".." */
            continue;
        }
        {
            char *semi = strchr(name, ';');
            if (semi != NULL) {
                *semi = '\0';
            }
        }
        nameLen = (uint32_t)strlen(name);
        if (nameLen > 0 && name[nameLen - 1] == '.' && !(flags & 2)) {
            name[nameLen - 1] = '\0';
        }
        if (dir[0] != '\0') {
            snprintf(path, sizeof(path), "%s/%s", dir, name);
        } else {
            snprintf(path, sizeof(path), "%s", name);
        }
        if (flags & 2) {
            if (inst_walk(iso, extent, bytes, path, depth + 1) != 0) {
                free(d);
                return -1;
            }
        } else if (inst_wanted(path)) {
            if (inst_add(iso, path, (uint64_t)extent * INST_SECTOR, bytes) != 0) {
                free(d);
                return -1;
            }
        }
    }
    free(d);
    return 0;
}

/* What gets copied: the program, BIN/ and DATA/ (not the serial file that only tells the release). */
static int inst_is_game_file(const InstFile *f) {
    return strcasecmp(f->path, "SLUS_216.78") == 0 || strncasecmp(f->path, "BIN/", 4) == 0 ||
           strncasecmp(f->path, "DATA/", 5) == 0;
}

static InstFile *inst_find(InstIso *iso, const char *path) {
    int i;

    for (i = 0; i < iso->n; i++) {
        if (strcasecmp(iso->f[i].path, path) == 0) {
            return &iso->f[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------------------------------------------------- */
/* Files                                                                                                          */

static void inst_mkpath(const char *file) { /* creates the folders above `file` */
    char tmp[1024];
    char *p;

    snprintf(tmp, sizeof(tmp), "%s", file);
    for (p = tmp + 1; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
}

static void inst_rm_rf(const char *path) {
    struct stat st;
    DIR *dp;
    struct dirent *e;
    char sub[1200];

    if (lstat(path, &st) != 0) {
        return;
    }
    if (S_ISDIR(st.st_mode)) {
        dp = opendir(path);
        if (dp != NULL) {
            while ((e = readdir(dp)) != NULL) {
                if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) {
                    snprintf(sub, sizeof(sub), "%s/%s", path, e->d_name);
                    inst_rm_rf(sub);
                }
            }
            closedir(dp);
        }
        rmdir(path);
    } else {
        unlink(path);
    }
}

/* An earlier, interrupted install (or a PC extraction) is replaced; gamedata/mods stays. */
static void inst_clean(void) {
    DIR *dp = opendir("gamedata");
    struct dirent *e;
    char sub[1200];

    if (dp == NULL) {
        return;
    }
    while ((e = readdir(dp)) != NULL) {
        if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0 && strcmp(e->d_name, "mods") != 0) {
            snprintf(sub, sizeof(sub), "gamedata/%s", e->d_name);
            inst_rm_rf(sub);
        }
    }
    closedir(dp);
}

static int inst_exists(const char *path) {
    return access(path, R_OK) == 0;
}

/* The game's data is there: its two programs, and no install that was cut short. */
static int inst_present(void) {
    return inst_exists("gamedata/disc/SLUS_216.78") && inst_exists("gamedata/disc/BIN/DBZP.BIN") &&
           !inst_exists("gamedata/.installing");
}

/* The first *.iso in the current folder (Documents). */
static int inst_find_iso(char *out, size_t size) {
    DIR *dp = opendir(".");
    struct dirent *e;
    int found = 0;

    if (dp == NULL) {
        return 0;
    }
    while (!found && (e = readdir(dp)) != NULL) {
        size_t n = strlen(e->d_name);
        if (n > 4 && strcasecmp(e->d_name + n - 4, ".iso") == 0 && n < size) {
            snprintf(out, size, "%s", e->d_name);
            found = 1;
        }
    }
    closedir(dp);
    return found;
}

/* ------------------------------------------------------------------------------------------------------------- */
/* The install. Returns 0 or shows the error on the screen (and in the log) and returns -1.                      */

static int inst_fail(const char *a, const char *b, const char *c) {
    inst_show("No se pudo instalar", a, b, c, -1.0f, 1);
    return -1;
}

static int inst_run(const char *isoPath) {
    InstIso iso;
    InstFile *slus, *dbzp;
    uint8_t pvd[INST_SECTOR];
    uint8_t *buf = NULL;
    struct statvfs fs;
    uint64_t total = 0, done = 0, freeBytes;
    char l1[256], l2[256], l3[96], out[1100];
    int i, files = 0, lastPercent = -1, rc = -1;
    FILE *marker;

    memset(&iso, 0, sizeof(iso));
    iso.fd = open(isoPath, O_RDONLY);
    if (iso.fd < 0) {
        snprintf(l1, sizeof(l1), "%s", isoPath);
        return inst_fail("No se pudo abrir la ISO:", l1, "");
    }
    inst_show("Leyendo la ISO...", isoPath, "", "", -1.0f, 0);
    if (inst_pread(iso.fd, 16ull * INST_SECTOR, pvd, sizeof(pvd)) != 0 || pvd[0] != 1 || memcmp(pvd + 1, "CD001", 5) != 0) {
        close(iso.fd);
        return inst_fail("Esto no es una ISO de 2048 bytes por sector.", "Si es .bin/.cue, convertela a .iso.", "");
    }
    if (inst_walk(&iso, inst_le32(pvd + 156 + 2), inst_le32(pvd + 156 + 10), "", 0) != 0) {
        close(iso.fd);
        free(iso.f);
        return inst_fail("La ISO esta danada o incompleta.", "Vuelve a copiarla al iPhone.", "");
    }
    slus = inst_find(&iso, "SLUS_216.78");
    dbzp = inst_find(&iso, "BIN/DBZP.BIN");
    if (slus == NULL || dbzp == NULL) {
        const char *serial = "?";
        for (i = 0; i < iso.n; i++) {
            if (strchr(iso.f[i].path, '/') == NULL && iso.f[i].path[4] == '_') {
                serial = iso.f[i].path;
            }
        }
        snprintf(l1, sizeof(l1), "Esta ISO es %s.", serial);
        close(iso.fd);
        free(iso.f);
        return inst_fail(l1, "Hace falta la version USA: SLUS-21678.", "");
    }
    for (i = 0; i < iso.n; i++) {
        if (inst_is_game_file(&iso.f[i])) {
            total += iso.f[i].size;
        }
    }
    freeBytes = statvfs(".", &fs) == 0 ? (uint64_t)fs.f_bavail * (uint64_t)fs.f_frsize : 0;
    if (freeBytes != 0 && freeBytes < total + INST_SPARE) {
        snprintf(l1, sizeof(l1), "Hacen falta %.1f GB libres y hay %.1f GB.", (double)(total + INST_SPARE) / 1e9, (double)freeBytes / 1e9);
        close(iso.fd);
        free(iso.f);
        return inst_fail(l1, "Libera espacio y abre la app otra vez.", "");
    }
    buf = (uint8_t *)malloc(INST_CHUNK);
    if (buf == NULL) {
        close(iso.fd);
        free(iso.f);
        return inst_fail("Sin memoria para copiar.", "", "");
    }

    inst_clean();
    mkdir("gamedata", 0755);
    marker = fopen("gamedata/.installing", "w"); /* removed at the very end: an interrupted copy is redone */
    if (marker != NULL) {
        fputs("installing\n", marker);
        fclose(marker);
    }
    for (i = 0; i < iso.n; i++) {
        InstFile *f = &iso.f[i];
        char upper[200];
        size_t k;
        int fd;
        uint64_t left, pos;

        if (!inst_is_game_file(f)) {
            continue;
        }
        for (k = 0; k <= strlen(f->path); k++) {
            upper[k] = (char)toupper((unsigned char)f->path[k]); /* upper case, as the game asks for them */
        }
        snprintf(out, sizeof(out), "gamedata/disc/%s", upper);
        inst_mkpath(out);
        fd = open(out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            snprintf(l1, sizeof(l1), "%s", upper);
            inst_fail("No se pudo crear:", l1, strerror(errno));
            goto finish;
        }
        pos = f->off;
        left = f->size;
        while (left > 0) {
            size_t n = left < INST_CHUNK ? (size_t)left : (size_t)INST_CHUNK;
            uint8_t *p = buf;
            size_t w = n;
            int percent;

            if (inst_pread(iso.fd, pos, buf, n) != 0) {
                close(fd);
                inst_fail("La ISO termina antes de tiempo.", "Vuelve a copiarla al iPhone.", "");
                goto finish;
            }
            while (w > 0) {
                ssize_t r = write(fd, p, w);
                if (r < 0 && errno == EINTR) {
                    continue;
                }
                if (r <= 0) {
                    snprintf(l1, sizeof(l1), "%s", strerror(errno));
                    close(fd);
                    inst_fail("Error al escribir (sin espacio?):", l1, "");
                    goto finish;
                }
                p += r;
                w -= (size_t)r;
            }
            pos += n;
            left -= n;
            done += n;
            percent = total != 0 ? (int)(done * 100 / total) : 100;
            if (percent != lastPercent) {
                lastPercent = percent;
                snprintf(l2, sizeof(l2), "%s", upper);
                snprintf(l3, sizeof(l3), "%d%%   %.2f / %.2f GB", percent, (double)done / 1e9, (double)total / 1e9);
                inst_show("Instalando los datos del juego", "No bloquees ni cierres la app.", l2, l3,
                          total != 0 ? (float)((double)done / (double)total) : 1.0f, 0);
            }
        }
        close(fd);
        files++;
    }
    unlink("gamedata/.installing");
    marker = fopen("gamedata/.installed", "w");
    if (marker != NULL) {
        fprintf(marker, "files=%d\nbytes=%llu\nlayout=afs\n", files, (unsigned long long)total);
        fclose(marker);
    }
    rc = 0;
finish:
    free(buf);
    free(iso.f);
    close(iso.fd);
    return rc;
}

/* Called by plat_ios.c before the game starts. Returns when the game's data is there; with nothing to install from,
   or with an error, the message stays on the screen. */
static void ios_prepare_data(void) {
    char iso[512];

    if (inst_present()) {
        return;
    }
    if (!inst_find_iso(iso, sizeof(iso))) {
        inst_show("Falta la ISO de Budokai Tenkaichi 3", "Copia tu ISO (version USA, SLUS-21678) en:",
                  "Archivos > En mi iPhone > BT3", "y abre la app otra vez.", -1.0f, 1);
        inst_stuck();
        return;
    }
    if (inst_run(iso) != 0) {
        inst_stuck();
        return;
    }
    inst_show("Listo", "Datos instalados. Ya puedes borrar la ISO", "de la carpeta BT3 para recuperar espacio.", "", 1.0f, 0);
    SDL_Delay(1500);
    inst_ui_close();
}

#endif
