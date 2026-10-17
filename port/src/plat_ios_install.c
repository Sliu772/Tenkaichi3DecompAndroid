/*
 * iOS: the game's data, unpacked from the user's disc image (plat_ios.c calls this before anything else; a device
 * has no Python and no setup program). The same work, and the same result, as port/tools/extract_disc.py and the
 * setup's native.cpp:
 *
 *   the image   ISO 9660: the volume descriptor at sector 16, directory records from the root
 *   the checks   SHA-1 of BIN/DBZP.BIN and of SLUS_216.78 laid out as a flat image from 0x100000 (the values of
 *                config/DBZP.yaml and config/SLUS_216.78.yaml)
 *   the data     gamedata/disc/<loose files>, and every DATA/*.AFS archive split into gamedata/<name>/00000.bin
 *
 * The disc image is a DVD (4.7 GB) and the data another 4.5 GB: the first start takes minutes and needs the room.
 * A file that was already unpacked is not touched again (gamedata/.installed marks the end).
 */
#ifdef BT3_IOS
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#define ROM_BASE 0x100000u
#define ROM_END 0x2FF180u
#define ROM_SHA1 "caee6c2269bba89fc51eea9e7beac3adbec9dc52"
#define DBZP_SHA1 "4f910969e05d9b25c83af7642b60da9949a4348b"
#define MAX_ENTRIES 512
#define MAX_DIRS 128 /* a directory is one run of sectors: the tree of a rebuilt image can name one twice */

typedef struct {
    char path[256]; /* upper case, as the game asks for the files */
    uint64_t offset, size;
} Entry;

typedef struct {
    FILE *fp;
    Entry list[MAX_ENTRIES];
    int n;
} Iso;

static uint8_t sCopyBuf[1 << 18];
static uint64_t sTotal, sDone, sIsoSize; /* sIsoSize: the image's own size; a record past it is not data */
static uint64_t sSeen[MAX_DIRS];         /* the directories already walked, by their first sector */
static int sFiles, sPercent = -1, sNSeen, sBogus;
static char sLine3[96]; /* the file being written, for the screen */

/* The install's screen: gs_ios_ui.c, the hard-float road, where SDL_RenderDebugText and SDL_FRect are at home (this
   file is the port's soft-float road: only integers and strings cross). A zero from GsIosUi_Show means there was no
   window: the log is the only report then. */
int GsIosUi_Show(const char *a, const char *b, const char *c, const char *d, int permille, int red);

/* The reason an install stopped, left on the screen (the caller keeps it there; the log has the English). */
static void ui_bad(const char *a, const char *b, const char *c) {
    GsIosUi_Show("No se pudo instalar", a, b, c, -1, 1);
}

static int read_at(Iso *iso, uint64_t offset, void *buf, size_t n) {
    if (fseeko(iso->fp, (off_t)offset, SEEK_SET) != 0) {
        return 0;
    }
    return fread(buf, 1, n, iso->fp) == n;
}

static void progress(void) {
    static char line4[16];
    int percent = sTotal != 0 ? (int)(sDone * 100 / sTotal) : 0;

    if (percent != sPercent) {
        sPercent = percent;
        fprintf(stderr, "bt3: unpacking: %d%%, %d files\n", percent, sFiles);
        snprintf(line4, sizeof(line4), "%d%%", percent);
        GsIosUi_Show("Instalando los datos del juego", "No bloquees ni cierres la app.", sLine3, line4, percent * 10, 0);
    }
}

static int walk(Iso *iso, uint32_t lba, uint32_t size, const char *dir, int depth) {
    uint8_t *d;
    size_t pos, i;

    for (i = 0; i < (size_t)sNSeen; i++) {
        if (sSeen[i] == lba) {
            return 1; /* one directory is one run of sectors: an image that names it twice is not walked twice */
        }
    }
    if (sNSeen < MAX_DIRS) {
        sSeen[sNSeen++] = lba;
    }
    d = malloc(size == 0 ? 1 : size);
    if (d == NULL || !read_at(iso, (uint64_t)lba * 2048, d, size)) {
        free(d);
        fprintf(stderr, "bt3: the disc image could not be read (its directory tree). It may be incomplete.\n");
        ui_bad("La imagen de disco esta danada o incompleta.", "Vuelve a copiarla al iPhone.", NULL);
        return 0;
    }
    for (pos = 0; pos + 34 <= size;) {
        uint32_t ext, bytes, flags, len;
        char name[256], full[512];
        size_t nameLen;

        if (d[pos] == 0) { /* records do not cross sectors: the rest of this one is empty */
            pos = (pos / 2048 + 1) * 2048;
            continue;
        }
        len = d[pos];
        if (len < 34 || pos + len > size) { /* the fixed fields alone are 33 bytes: anything shorter is not a record */
            break;
        }
        memcpy(&ext, &d[pos + 2], 4);
        memcpy(&bytes, &d[pos + 10], 4);
        flags = d[pos + 25];
        nameLen = d[pos + 32];
        if (nameLen > sizeof(name) - 1) {
            nameLen = sizeof(name) - 1;
        }
        memcpy(name, &d[pos + 33], nameLen);
        name[nameLen] = '\0';
        pos += len;
        if (nameLen == 1 && (name[0] == 0 || name[0] == 1)) {
            continue; /* this directory and its parent: a name of one byte is not text */
        }
        for (i = 0; name[i] != '\0'; i++) { /* ";1": the file's version */
            if (name[i] == ';') {
                name[i] = '\0';
                break;
            }
        }
        nameLen = strlen(name);
        if (nameLen > 0 && name[nameLen - 1] == '.') {
            name[nameLen - 1] = '\0';
        }
        for (i = 0; name[i] != '\0'; i++) {
            name[i] = name[i] >= 'a' && name[i] <= 'z' ? (char)(name[i] - 'a' + 'A') : name[i];
        }
        if (flags & 4) { /* an "associated" file: it belongs to the record before it, and the game has no use of it */
            continue;
        }
        if (flags & 2) {
            if (depth < 8 && (uint64_t)ext * 2048 + bytes <= sIsoSize) {
                snprintf(full, sizeof(full), "%s%s/", dir, name);
                if (!walk(iso, ext, bytes, full, depth + 1)) {
                    free(d);
                    return 0;
                }
            } else if ((uint64_t)ext * 2048 + bytes > sIsoSize) {
                if (sBogus < 3) { /* the tree names sectors the image does not hold */
                    fprintf(stderr, "bt3: a directory of the image names sectors past its end (left out): %s%s/\n", dir, name);
                }
                sBogus++;
            }
        } else {
            snprintf(full, sizeof(full), "%s%s", dir, name);
            if (strcmp(full, "SLUS_216.78") == 0 || strncmp(full, "BIN/", 4) == 0 || strncmp(full, "DATA/", 5) == 0) {
                if ((uint64_t)ext * 2048 + bytes > sIsoSize) {
                    if (sBogus < 3) { /* not data: a rebuilt (or damaged) tree can name these */
                        fprintf(stderr, "bt3: a file of the image names sectors past its end (left out): %s (%u MB at sector %u)\n",
                                full, (unsigned)(bytes >> 20), (unsigned)ext);
                    }
                    sBogus++;
                } else {
                    if (iso->n >= MAX_ENTRIES) {
                        fprintf(stderr, "bt3: the disc image has more files than expected.\n");
                        free(d);
                        return 0;
                    }
                    if (iso->n > 0 && strcmp(iso->list[iso->n - 1].path, full) == 0) {
                        iso->list[iso->n - 1].size += bytes; /* a file in several extents: they follow each other */
                    } else {
                        snprintf(iso->list[iso->n].path, sizeof(iso->list[iso->n].path), "%s", full);
                        iso->list[iso->n].offset = (uint64_t)ext * 2048;
                        iso->list[iso->n].size = bytes;
                        iso->n++;
                    }
                }
            }
        }
    }
    free(d);
    return 1;
}

/* ---------------------------------------------------------------------------------------------------------- SHA-1 */
typedef struct {
    uint32_t h[5];
    uint8_t block[64];
    uint64_t total;
    size_t fill;
} Sha1;

static uint32_t rol32(uint32_t v, int n) {
    return (v << n) | (v >> (32 - n));
}

static void sha1_block(Sha1 *s) {
    uint32_t w[80], a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4];
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = (uint32_t)s->block[i * 4] << 24 | (uint32_t)s->block[i * 4 + 1] << 16 | (uint32_t)s->block[i * 4 + 2] << 8 | s->block[i * 4 + 3];
    }
    for (i = 16; i < 80; i++) {
        w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    for (i = 0; i < 80; i++) {
        uint32_t f = i < 20 ? (b & c) | (~b & d) : i < 40 ? b ^ c ^ d : i < 60 ? (b & c) | (b & d) | (c & d) : b ^ c ^ d;
        uint32_t k = i < 20 ? 0x5A827999 : i < 40 ? 0x6ED9EBA1 : i < 60 ? 0x8F1BBCDC : 0xCA62C1D6;
        uint32_t t = rol32(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rol32(b, 30); b = a; a = t;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e;
}

static void sha1_init(Sha1 *s) {
    s->h[0] = 0x67452301;
    s->h[1] = 0xEFCDAB89;
    s->h[2] = 0x98BADCFE;
    s->h[3] = 0x10325476;
    s->h[4] = 0xC3D2E1F0;
    s->total = 0;
    s->fill = 0;
}

static void sha1_add(Sha1 *s, const uint8_t *p, size_t n) {
    s->total += n;
    while (n > 0) {
        size_t take = 64 - s->fill < n ? 64 - s->fill : n;
        memcpy(s->block + s->fill, p, take);
        s->fill += take;
        p += take;
        n -= take;
        if (s->fill == 64) {
            sha1_block(s);
            s->fill = 0;
        }
    }
}

static void sha1_hex(Sha1 *s, char out[41]) {
    uint64_t bits = s->total * 8;
    uint8_t pad = 0x80, zero = 0, len[8];
    int i;

    sha1_add(s, &pad, 1);
    while (s->fill != 56) {
        sha1_add(s, &zero, 1);
    }
    for (i = 0; i < 8; i++) {
        len[i] = (uint8_t)(bits >> (56 - i * 8));
    }
    sha1_add(s, len, 8);
    for (i = 0; i < 5; i++) {
        snprintf(out + i * 8, 9, "%08x", s->h[i]);
    }
}

static Entry *find(Iso *iso, const char *path) {
    int i;
    for (i = 0; i < iso->n; i++) {
        if (strcmp(iso->list[i].path, path) == 0) {
            return &iso->list[i];
        }
    }
    return NULL;
}

/* The programs on the disc have to be the ones this port was made from: the flat image of SLUS_216.78 from 0x100000
   (its loaded sections) and BIN/DBZP.BIN under SHA-1. */
static int check_disc(Iso *iso) {
    Entry *slus = find(iso, "SLUS_216.78"), *dbzp = find(iso, "BIN/DBZP.BIN");
    uint8_t *elf, *rom, chunk[65536];
    Sha1 sha;
    char hex[41];
    uint64_t off;

    if (slus == NULL || dbzp == NULL) {
        fprintf(stderr, "bt3: this is not a disc image of the USA release of Budokai Tenkaichi 3 (no SLUS_216.78 on it).\n"
                        "bt3: other regions and the Wii version have different programs and are not supported.\n");
        ui_bad("Esta ISO no es la version USA de BT3.", "El port necesita SLUS-21678 sin modificar.", NULL);
        return 0;
    }
    if (slus->size < 0x34 || slus->size > (32u << 20)) {
        fprintf(stderr, "bt3: the program on the disc has an unexpected size: the image may be damaged.\n");
        ui_bad("La imagen de disco esta danada o incompleta.", "Vuelve a copiarla al iPhone.", NULL);
        return 0;
    }
    elf = malloc((size_t)slus->size);
    if (elf == NULL || !read_at(iso, slus->offset, elf, (size_t)slus->size)) {
        free(elf);
        fprintf(stderr, "bt3: the disc image could not be read. It may be incomplete.\n");
        ui_bad("La imagen de disco esta danada o incompleta.", "Vuelve a copiarla al iPhone.", NULL);
        return 0;
    }
    rom = calloc(1, ROM_END - ROM_BASE);
    if (rom == NULL) {
        free(elf);
        return 0;
    }
    if (memcmp(elf, "\177ELF", 4) == 0) {
        uint32_t shoff;
        uint16_t shentsize, shnum, i;
        memcpy(&shoff, elf + 0x20, 4);
        memcpy(&shentsize, elf + 0x2E, 2);
        memcpy(&shnum, elf + 0x30, 2);
        for (i = 0; i < shnum && (uint64_t)shoff + (uint64_t)(i + 1) * shentsize <= slus->size; i++) {
            uint32_t sh[6]; /* name, type, flags, address, offset, size */
            memcpy(sh, elf + shoff + (uint64_t)i * shentsize, sizeof(sh));
            if ((sh[2] & 2) && sh[1] != 8 && sh[5] != 0 && sh[3] >= ROM_BASE && sh[3] + sh[5] <= ROM_END && (uint64_t)sh[4] + sh[5] <= slus->size) {
                memcpy(rom + (sh[3] - ROM_BASE), elf + sh[4], sh[5]);
            }
        }
    }
    sha1_init(&sha);
    sha1_add(&sha, rom, ROM_END - ROM_BASE);
    sha1_hex(&sha, hex);
    free(elf);
    free(rom);
    if (strcmp(hex, ROM_SHA1) != 0) {
        fprintf(stderr, "bt3: the program on this disc is not the expected one (%s): the port needs the unmodified USA\n"
                        "bt3: release (SLUS-21678); a patched or damaged image will not work.\n", hex);
        ui_bad("La ISO no es la version USA original.", "El port no acepta imagenes modificadas.", NULL);
        return 0;
    }
    sha1_init(&sha);
    off = 0;
    while (off < dbzp->size) {
        size_t n = (size_t)(dbzp->size - off < (uint64_t)sizeof(chunk) ? dbzp->size - off : (uint64_t)sizeof(chunk));
        if (!read_at(iso, dbzp->offset + off, chunk, n)) {
            fprintf(stderr, "bt3: the disc image could not be read. It may be incomplete.\n");
            ui_bad("La imagen de disco esta danada o incompleta.", "Vuelve a copiarla al iPhone.", NULL);
            return 0;
        }
        sha1_add(&sha, chunk, n);
        off += n;
    }
    sha1_hex(&sha, hex);
    if (strcmp(hex, DBZP_SHA1) != 0) {
        fprintf(stderr, "bt3: the menu program on this disc is not the expected one (%s): the port needs the unmodified\n"
                        "bt3: USA release (SLUS-21678); a patched or damaged image will not work.\n", hex);
        ui_bad("La ISO no es la version USA original.", "El port no acepta imagenes modificadas.", NULL);
        return 0;
    }
    return 1;
}

/* -------------------------------------------------------------------------------------------- the unpacking itself */
static int mkdirs(const char *path) {
    char buf[1100];
    size_t i, n;

    snprintf(buf, sizeof(buf), "%s", path);
    n = strlen(buf);
    while (n > 1 && buf[n - 1] == '/') {
        buf[--n] = '\0';
    }
    for (i = 1; buf[i] != '\0'; i++) {
        if (buf[i] == '/') {
            buf[i] = '\0';
            if (mkdir(buf, 0755) != 0 && errno != EEXIST) {
                return 0;
            }
            buf[i] = '/';
        }
    }
    if (mkdir(buf, 0755) != 0 && errno != EEXIST) {
        return 0;
    }
    return 1;
}

static int copy_out(Iso *iso, uint64_t offset, uint64_t size, const char *path) {
    const char *base = strrchr(path, '/');
    FILE *out;

    base = base != NULL ? base + 1 : path;
    snprintf(sLine3, sizeof(sLine3), "%s", base);
    out = fopen(path, "wb");
    if (out == NULL) {
        fprintf(stderr, "bt3: could not write %s (is the device out of room?)\n", path);
        return 0;
    }
    while (size > 0) {
        size_t n = size < (uint64_t)sizeof(sCopyBuf) ? (size_t)size : sizeof(sCopyBuf);
        if (!read_at(iso, offset, sCopyBuf, n) || fwrite(sCopyBuf, 1, n, out) != n) {
            fclose(out);
            fprintf(stderr, "bt3: could not write %s (is the device out of room?)\n", path);
            return 0;
        }
        offset += n;
        size -= n;
    }
    if (fclose(out) != 0) {
        fprintf(stderr, "bt3: could not write %s (is the device out of room?)\n", path);
        return 0;
    }
    return 1;
}

/* An AFS archive: "AFS\0", the number of entries, then (offset, size) of each; one loose file per entry, as the
   game asks for them (<name>/00042.bin). */
static int unpack_afs(Iso *iso, const Entry *e, const char *dir) {
    uint8_t head[8];
    uint32_t count, i, *table;
    uint64_t inArchive = 0;
    char path[1100];

    if (!read_at(iso, e->offset, head, 8) || memcmp(head, "AFS\0", 4) != 0) {
        fprintf(stderr, "bt3: an archive on the disc (%s) is damaged.\n", e->path);
        ui_bad("Un archivo de datos de la ISO esta danado.", e->path, NULL);
        return 0;
    }
    memcpy(&count, head + 4, 4);
    if ((uint64_t)count * 8 + 8 > e->size || count > 100000) {
        fprintf(stderr, "bt3: an archive on the disc (%s) is damaged.\n", e->path);
        ui_bad("Un archivo de datos de la ISO esta danado.", e->path, NULL);
        return 0;
    }
    table = malloc((size_t)count * 8);
    if (table == NULL || !read_at(iso, e->offset + 8, table, (size_t)count * 8)) {
        free(table);
        fprintf(stderr, "bt3: the disc image could not be read (is it complete?)\n");
        ui_bad("La ISO no se pudo leer.", "Comprueba que la copia esta completa.", NULL);
        return 0;
    }
    for (i = 0; i < count; i++) {
        if ((uint64_t)table[i * 2] + table[i * 2 + 1] > e->size) {
            fprintf(stderr, "bt3: an archive on the disc (%s) is damaged.\n", e->path);
            ui_bad("Un archivo de datos de la ISO esta danado.", e->path, NULL);
            free(table);
            return 0;
        }
        snprintf(path, sizeof(path), "%s%05u.bin", dir, i);
        if (!copy_out(iso, e->offset + table[i * 2], table[i * 2 + 1], path)) {
            free(table);
            ui_bad("Error al escribir (sin espacio?).", path, NULL);
            return 0;
        }
        sFiles++;
        inArchive += table[i * 2 + 1];
        sDone += table[i * 2 + 1];
        progress();
    }
    free(table);
    sDone += e->size > inArchive ? e->size - inArchive : 0; /* the archive's table and padding */
    progress();
    return 1;
}

/* The user's disc image in Documents: a *.iso first; failing that, any file large enough whose volume descriptor at
   sector 16 says ISO 9660 (a copy that lost its name still works). The full path is returned. */
int Port_IosFindIso(const char *documents, char *out, size_t size) {
    DIR *d;
    struct dirent *e;
    struct stat st;
    char full[1200], byContent[1200];
    int found = 0;

    byContent[0] = '\0';
    d = opendir(documents);
    if (d == NULL) {
        fprintf(stderr, "bt3: cannot read %s: %s\n", documents, strerror(errno));
        return 0;
    }
    while ((e = readdir(d)) != NULL) {
        size_t n = strlen(e->d_name);
        if (e->d_name[0] == '.' || n > 200) {
            continue;
        }
        snprintf(full, sizeof(full), "%s/%s", documents, e->d_name);
        if (stat(full, &st) != 0 || !S_ISREG(st.st_mode)) {
            continue;
        }
        fprintf(stderr, "bt3: Documents: \"%s\" (%llu MB)\n", e->d_name, (unsigned long long)(st.st_size >> 20));
        if (!found && n > 4 && strcasecmp(e->d_name + n - 4, ".iso") == 0) {
            snprintf(out, size, "%s", full);
            found = 1;
        } else if (byContent[0] == '\0' && st.st_size > (256ll << 20)) {
            Iso probe;
            uint8_t pvd[2048];
            memset(&probe, 0, sizeof(probe));
            probe.fp = fopen(full, "rb");
            if (probe.fp != NULL) {
                if (read_at(&probe, 16 * 2048, pvd, sizeof(pvd)) && pvd[0] == 1 && memcmp(pvd + 1, "CD001", 5) == 0) {
                    snprintf(byContent, sizeof(byContent), "%s", full);
                }
                fclose(probe.fp);
            }
        }
    }
    closedir(d);
    if (!found && byContent[0] != '\0') {
        snprintf(out, size, "%s", byContent);
        found = 1;
    }
    return found;
}

int Port_IosInstall(const char *isoPath, const char *documents) {
    Iso iso;
    uint8_t pvd[2048];
    char dir[1100], marker[1200];
    uint32_t lba, size;
    FILE *m;
    int i;

    memset(&iso, 0, sizeof(iso));
    sTotal = sDone = sIsoSize = 0; /* the report of this install, not of a previous one */
    sFiles = 0;
    sPercent = -1;
    sNSeen = sBogus = 0;
    snprintf(dir, sizeof(dir), "%s/gamedata/", documents);
    snprintf(marker, sizeof(marker), "%s.installed", dir);
    m = fopen(marker, "rb");
    if (m != NULL) {
        fclose(m);
        return 0; /* already unpacked */
    }
    iso.fp = fopen(isoPath, "rb");
    if (iso.fp == NULL) {
        fprintf(stderr, "bt3: the disc image could not be opened: %s\n", isoPath);
        ui_bad("No se pudo abrir la ISO.", isoPath, NULL);
        return 1;
    }
    {   /* the image's own size: what its records may name (sIsoSize, used by walk) */
        off_t fsz;
        fseeko(iso.fp, 0, SEEK_END);
        fsz = ftello(iso.fp);
        if (fsz <= 0) {
            fprintf(stderr, "bt3: the disc image's size could not be read: %s\n", isoPath);
            ui_bad("No se pudo leer la ISO.", "Vuelve a copiarla al iPhone.", NULL);
            fclose(iso.fp);
            return 1;
        }
        sIsoSize = (uint64_t)fsz;
    }
    fprintf(stderr, "bt3: the disc image is %llu MB\n", (unsigned long long)(sIsoSize >> 20));
    if (!read_at(&iso, 16 * 2048, pvd, sizeof(pvd)) || pvd[0] != 1 || memcmp(pvd + 1, "CD001", 5) != 0) {
        fprintf(stderr, "bt3: this file is not a disc image (.iso). Compressed images (.chd, .cso, .gz)\n"
                        "bt3: have to be converted to .iso first.\n");
        ui_bad("Esto no es una imagen de disco .iso.", "Si era .bin/.cue o comprimida, conviertela a .iso.", NULL);
        return 1;
    }
    memcpy(&lba, pvd + 158, 4);
    memcpy(&size, pvd + 166, 4);
    if (!walk(&iso, lba, size, "", 0) || !check_disc(&iso)) {
        return 1;
    }
    for (i = 0; i < iso.n; i++) {
        sTotal += iso.list[i].size;
    }
    {   /* what the tree holds, for the log: the only report when the check below stops the install */
        int k, j, top[5];

        for (k = 0; k < 5; k++) {
            top[k] = -1;
            for (j = 0; j < iso.n; j++) {
                int used = 0;
                for (i = 0; i < k; i++) {
                    if (top[i] == j) {
                        used = 1;
                        break;
                    }
                }
                if (!used && (top[k] < 0 || iso.list[j].size > iso.list[top[k]].size)) {
                    top[k] = j;
                }
            }
        }
        if (sBogus != 0) {
            fprintf(stderr, "bt3: %d records of the disc image name sectors past its end (left out)\n", sBogus);
        }
        for (k = 0; k < 5 && top[k] >= 0; k++) {
            fprintf(stderr, "bt3: largest: %s (%llu MB)\n", iso.list[top[k]].path,
                    (unsigned long long)(iso.list[top[k]].size >> 20));
        }
    }
    fprintf(stderr, "bt3: unpacking the game's data (%llu MB, %d files on the disc) into gamedata/ ...\n",
            (unsigned long long)(sTotal >> 20), iso.n);
    {   /* room for the data, plus a margin: an interrupted write is worse than an early no */
        struct statvfs fs;
        if (statvfs(documents, &fs) == 0 && fs.f_frsize != 0) {
            uint64_t freeBytes = (uint64_t)fs.f_bavail * (uint64_t)fs.f_frsize;
            if (freeBytes < sTotal + (256ull << 20)) {
                char need[96];
                snprintf(need, sizeof(need), "Hacen falta %llu MB y hay %llu MB.",
                         (unsigned long long)((sTotal + (256ull << 20)) >> 20), (unsigned long long)(freeBytes >> 20));
                fprintf(stderr, "bt3: %s\n", need);
                ui_bad(need, "Libera espacio y abre la app otra vez.", NULL);
                return 1;
            }
        }
    }
    if (!mkdirs(dir)) {
        fprintf(stderr, "bt3: could not make %s\n", dir);
        ui_bad("No se pudieron crear las carpetas.", "Comprueba el espacio libre.", NULL);
        return 1;
    }
    for (i = 0; i < iso.n; i++) {
        Entry *e = &iso.list[i];
        size_t len = strlen(e->path);
        int afs = strncmp(e->path, "DATA/", 5) == 0 && len > 4 && strcmp(e->path + len - 4, ".AFS") == 0;

        if (!afs) { /* a loose file: gamedata/disc/..., in the upper case the game asks for */
            char path[1100], parent[1100], *slash;
            snprintf(path, sizeof(path), "%sdisc/%s", dir, e->path);
            snprintf(parent, sizeof(parent), "%s", path);
            slash = strrchr(parent, '/');
            if (slash != NULL) {
                *slash = '\0';
                if (!mkdirs(parent)) {
                    ui_bad("No se pudieron crear las carpetas.", "Comprueba el espacio libre.", NULL);
                    return 1;
                }
            }
            if (!copy_out(&iso, e->offset, e->size, path)) {
                ui_bad("Error al escribir (sin espacio?).", sLine3, NULL);
                return 1;
            }
            sFiles++;
            sDone += e->size;
            progress();
        } else { /* an archive: gamedata/<name>/00000.bin ... */
            char stem[256], adir[1100];
            size_t j;
            snprintf(stem, sizeof(stem), "%s", e->path + 5);
            stem[len - 9] = '\0';
            for (j = 0; stem[j] != '\0'; j++) {
                stem[j] = stem[j] >= 'A' && stem[j] <= 'Z' ? (char)(stem[j] - 'A' + 'a') : stem[j];
            }
            snprintf(adir, sizeof(adir), "%s%s/", dir, stem);
            if (!mkdirs(adir)) {
                ui_bad("No se pudieron crear las carpetas.", "Comprueba el espacio libre.", NULL);
                return 1;
            }
            if (!unpack_afs(&iso, e, adir)) {
                return 1; /* the screen carries the reason */
            }
        }
    }
    m = fopen(marker, "wb");
    if (m != NULL) {
        fclose(m);
    }
    fclose(iso.fp);
    fprintf(stderr, "bt3: unpacked %d files into gamedata/\n", sFiles);
    return 0;
}
#endif /* BT3_IOS */
