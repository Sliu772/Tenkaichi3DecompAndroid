/*
 * iOS: what the game needs before its main, and what has no place in the memory layout (port/src/plat_mem.c).
 *
 *   - constructor priority 101 (before plat_mem.c's Port_MapMemory, which reads the game's data): the working folder
 *     becomes the app's Documents folder (the one the Files app shows: the user's .iso and, after the first start,
 *     gamedata/) and stdout/stderr go to Documents/bt3_log.txt (a device has no console: the log is the report).
 *   - constructor priority 102: if gamedata/ is not there yet, it is unpacked from an .iso the user copied into
 *     Documents (the log tells the progress; the screen stays black for a while).
 */
#ifdef BT3_IOS
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

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

__attribute__((constructor(102))) static void Port_IosData(void) {
    struct stat st;
    char path[1200], name[256];
    FILE *iso;

    if (sDocuments[0] == '\0') {
        return;
    }
    snprintf(path, sizeof(path), "%s/gamedata/disc/SLUS_216.78", sDocuments);
    if (stat(path, &st) == 0) {
        return; /* the game's data is unpacked */
    }
    /* The user's disc image, copied with the Files app: the first .iso in Documents. */
    iso = NULL;
    {
        DIR *d = opendir(sDocuments);
        struct dirent *e;
        while (d != NULL && (e = readdir(d)) != NULL) {
            size_t n = strlen(e->d_name);
            if (n > 4 && strcasecmp(e->d_name + n - 4, ".iso") == 0) {
                snprintf(path, sizeof(path), "%s/%s", sDocuments, e->d_name);
                iso = fopen(path, "rb");
                snprintf(name, sizeof(name), "%s", e->d_name);
                break;
            }
        }
        if (d != NULL) {
            closedir(d);
        }
    }
    if (iso == NULL) {
        fprintf(stderr, "bt3: no gamedata yet. Copy the game's disc image (.iso) into this app's folder (Files: \"On My iPhone\" > Tenkaichi3Decomp) and start again.\n");
        return;
    }
    fprintf(stderr, "bt3: unpacking the game's data from %s (once; this takes a while)\n", name);
    fclose(iso);
    /* (the ISO 9660 walk itself is written next: the disc's files, under gamedata/) */
    fprintf(stderr, "bt3: (the unpacking is not in this build yet)\n");
}
#endif
