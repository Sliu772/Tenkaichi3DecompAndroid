/*
 * iOS: what the game needs before its main, and what has no place in the memory layout (port/src/plat_mem.c).
 *
 *   - constructor priority 101 (before anything else): the working folder becomes the app's Documents folder (the
 *     one the Files app shows: the user's .iso and, after the first start, gamedata/) and stdout/stderr go to
 *     Documents/bt3_log.txt (a device has no console: the log is the report).
 *   - Port_IosData (called by plat_mem.c's game_entry once SDL's UIKit entry is up): the working folder is set to
 *     Documents again — SDL's own delegate moves it to the app bundle — and, while gamedata/.installed is not
 *     there, the user's disc image is unpacked into it (plat_ios_install.c: minutes, with its own window and
 *     progress). This cannot run in a constructor: those run before the app is up, and iOS kills a launch that
 *     takes that long.
 */
#ifdef BT3_IOS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int Port_IosInstall(const char *iso, const char *documents);          /* plat_ios_install.c */
int Port_IosFindIso(const char *documents, char *out, size_t size);   /* plat_ios_install.c */
int GsIosUi_Show(const char *a, const char *b, const char *c, const char *d, int permille, int red); /* gs_ios_ui.c */
void GsIosUi_Stuck(void); /* gs_ios_ui.c: the message stays on the screen until the app is closed */
void GsIosUi_Done(void);  /* gs_ios_ui.c: "ready", a moment, and the window goes away */

static char sDocuments[1024]; /* the app's Documents folder ("$HOME/Documents") */

/* The game's own folder, for anything the user sees or drops in: the disc image, saves, the log. */
const char *Port_Documents(void) {
    return sDocuments;
}

__attribute__((constructor(101))) static void Port_IosStart(void) {
    const char *home = getenv("HOME");
    char path[1200];
    FILE *log;

    if (home != NULL && strlen(home) + sizeof("/Documents") <= sizeof(sDocuments)) {
        snprintf(sDocuments, sizeof(sDocuments), "%s/Documents", home);
        mkdir(sDocuments, 0755);
        chdir(sDocuments);
    }
    snprintf(path, sizeof(path), "%s/bt3_log.txt", sDocuments[0] != '\0' ? sDocuments : ".");
    log = fopen(path, "a");
    if (log != NULL) { /* every report of the program, from here on, is in the file the Files app shows */
        dup2(fileno(log), 1);
        dup2(fileno(log), 2);
        fclose(log);
        setvbuf(stdout, NULL, _IOLBF, 0);
        setvbuf(stderr, NULL, _IOLBF, 0);
    }
    printf("\nbt3: start (documents: %s)\n", sDocuments);
    fflush(stdout);
}

void Port_IosData(void) {
    char path[1200], marker[1240];
    struct stat st;

    if (sDocuments[0] == '\0') {
        return;
    }
    if (chdir(sDocuments) != 0) {
        /* SDL's delegate moved the working folder to the bundle: everything the port opens by its relative name
           ("gamedata/...") has to be under Documents, where the user's files are. */
        fprintf(stderr, "bt3: cannot enter %s\n", sDocuments);
    }
    snprintf(marker, sizeof(marker), "%s/gamedata/.installed", sDocuments);
    if (stat(marker, &st) == 0) {
        return; /* the game's data is unpacked (the marker is written by the end of the install, and by the PC's) */
    }
    if (!Port_IosFindIso(sDocuments, path, sizeof(path))) {
        fprintf(stderr, "bt3: no gamedata yet and no disc image in %s\n", sDocuments);
        GsIosUi_Show("Falta la ISO de Budokai Tenkaichi 3", "Copia tu .iso (region USA) a Archivos >",
                     "En mi iPhone > Dragon Rage, y abre la app otra vez.", NULL, -1, 1);
        GsIosUi_Stuck(); /* the user has to read it: the screen stays */
        return;
    }
    fprintf(stderr, "bt3: the game's data will be unpacked from %s (once; a few minutes)\n", path);
    if (Port_IosInstall(path, sDocuments) == 0) {
        GsIosUi_Done();
    } else {
        fprintf(stderr, "bt3: the game's data could not be unpacked (see above)\n");
        GsIosUi_Stuck(); /* plat_ios_install.c left its reason on the screen */
    }
}
#endif
