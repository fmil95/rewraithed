/*
 * Reading the Dreamcast disc from a GDI dump.
 *
 * A .gdi lists the disc's tracks: number, first sector (LBA), type (4 for
 * data), sector size (2352 raw or 2048), file name. The game's files are in
 * the ISO 9660 file system of the high-density area, whose volume
 * descriptor is at sector 45000 + 16. BIGFILE.DAT has the PlayStation
 * layout (a count, then hash, size, offset and checksum per file);
 * TEXTURES.VQ is read by offset. (tools/dc_import.py does the same.)
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dc_disc.h"

#define SECTOR          2048
#define HD_AREA         45000
#define MAX_TRACKS      16

typedef struct
{
    long lba;
    long count;     /* sectors */
    int size;       /* bytes per sector in the file */
    char path[MAX_PATH];
    FILE *file;
} DcTrack;

struct DcDisc
{
    DcTrack tracks[MAX_TRACKS];
    int numTracks;
    long bigLba;
    long bigSize;
    long texLba;
    long texSize;
    long bigCount;
    unsigned char *bigDirectory;    /* bigCount entries of 16 bytes */
};

void dc_disc_folder(char *out, size_t size)
{
    char exe[MAX_PATH];
    char *slash;
    DWORD n = GetModuleFileNameA(NULL, exe, sizeof(exe));

    if ((n == 0) || (n >= sizeof(exe)))
    {
        snprintf(out, size, "dreamcast");
        return;
    }

    slash = strrchr(exe, '\\');

    if (slash != NULL)
    {
        *slash = '\0';
    }

    snprintf(out, size, "%s\\dreamcast", exe);
}

static void set_problem(char *problem, size_t size, const char *fmt, const char *arg)
{
    if ((problem != NULL) && (size > 0))
    {
        snprintf(problem, size, fmt, arg);
    }
}

/* the tracks of a .gdi: number of them, or -1 if the file cannot be read */
static int parse_gdi(const char *gdi, DcTrack *tracks, int max, int dataOnly)
{
    char folder[MAX_PATH];
    char line[512];
    char *slash;
    FILE *f = fopen(gdi, "r");
    int count = 0;
    int first = 1;

    if (f == NULL)
    {
        return -1;
    }

    snprintf(folder, sizeof(folder), "%s", gdi);
    slash = strrchr(folder, '\\');

    if (slash != NULL)
    {
        slash[1] = '\0';
    }
    else
    {
        folder[0] = '\0';
    }

    while ((fgets(line, sizeof(line), f) != NULL) && (count < max))
    {
        char name[MAX_PATH];
        long number;
        long lba;
        int type;
        int size;
        char *p;

        if (first)
        {
            /* the number of tracks */
            first = 0;
            continue;
        }

        if (sscanf(line, "%ld %ld %d %d", &number, &lba, &type, &size) != 4)
        {
            continue;
        }

        /* the name: fifth field, quoted if it has spaces */
        p = line;

        for (number = 0; number < 4; number++)
        {
            while ((*p == ' ') || (*p == '\t')) p++;
            while ((*p != ' ') && (*p != '\t') && (*p != '\0')) p++;
        }

        while ((*p == ' ') || (*p == '\t')) p++;

        if (*p == '"')
        {
            char *end = strchr(p + 1, '"');

            if (end == NULL)
            {
                continue;
            }

            snprintf(name, sizeof(name), "%.*s", (int)(end - p - 1), p + 1);
        }
        else
        {
            char *end = p;

            while ((*end != ' ') && (*end != '\t') && (*end != '\r') && (*end != '\n') && (*end != '\0')) end++;

            snprintf(name, sizeof(name), "%.*s", (int)(end - p), p);
        }

        if (dataOnly && (type != 4))
        {
            continue;
        }

        tracks[count].lba = lba;
        tracks[count].size = size;
        tracks[count].count = 0;
        tracks[count].file = NULL;
        snprintf(tracks[count].path, sizeof(tracks[count].path), "%s%s", folder, name);
        count++;
    }

    fclose(f);

    return count;
}

int dc_disc_locate(char *gdi, size_t gdiSize, char *problem, size_t problemSize)
{
    char folder[MAX_PATH];
    char pattern[MAX_PATH];
    char path[MAX_PATH];
    DcTrack tracks[MAX_TRACKS];
    WIN32_FIND_DATAA found;
    HANDLE h;
    int count;
    int i;

    dc_disc_folder(folder, sizeof(folder));
    snprintf(pattern, sizeof(pattern), "%s\\*.gdi", folder);

    h = FindFirstFileA(pattern, &found);

    if (h == INVALID_HANDLE_VALUE)
    {
        set_problem(problem, problemSize, "No .gdi file in %s.", folder);
        return 0;
    }

    snprintf(path, sizeof(path), "%s\\%s", folder, found.cFileName);
    FindClose(h);

    count = parse_gdi(path, tracks, MAX_TRACKS, 0);

    if (count <= 0)
    {
        set_problem(problem, problemSize, "%s lists no tracks.", path);
        return 0;
    }

    for (i = 0; i < count; i++)
    {
        if (GetFileAttributesA(tracks[i].path) == INVALID_FILE_ATTRIBUTES)
        {
            set_problem(problem, problemSize, "The track file %s is missing.", tracks[i].path);
            return 0;
        }
    }

    if ((gdi != NULL) && (gdiSize > 0))
    {
        snprintf(gdi, gdiSize, "%s", path);
    }

    return 1;
}

static int read_sector(DcDisc *disc, long lba, unsigned char *out)
{
    int i;

    for (i = 0; i < disc->numTracks; i++)
    {
        DcTrack *t = &disc->tracks[i];

        if ((lba >= t->lba) && (lba < t->lba + t->count))
        {
            long pos = (lba - t->lba) * (long)t->size + ((t->size == 2352) ? 16 : 0);

            if (t->file == NULL)
            {
                t->file = fopen(t->path, "rb");

                if (t->file == NULL)
                {
                    return 0;
                }
            }

            if ((fseek(t->file, pos, SEEK_SET) != 0) || (fread(out, 1, SECTOR, t->file) != SECTOR))
            {
                return 0;
            }

            return 1;
        }
    }

    return 0;
}

/* length bytes from offset bytes into a file starting at sector lba */
static int read_bytes(DcDisc *disc, long lba, long offset, long length, unsigned char *out)
{
    unsigned char sector[SECTOR];

    lba += offset / SECTOR;
    offset %= SECTOR;

    while (length > 0)
    {
        long n = SECTOR - offset;

        if (n > length)
        {
            n = length;
        }

        if (!read_sector(disc, lba, sector))
        {
            return 0;
        }

        memcpy(out, sector + offset, n);
        out += n;
        length -= n;
        offset = 0;
        lba++;
    }

    return 1;
}

static unsigned long le32(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

/* a file in the root directory of the high-density area */
static int find_root_file(DcDisc *disc, const char *name, long *lba, long *size)
{
    unsigned char pvd[SECTOR];
    unsigned char *dir;
    long rootLba;
    long rootSize;
    long pos = 0;
    size_t len = strlen(name);

    if (!read_sector(disc, HD_AREA + 16, pvd) || (memcmp(pvd + 1, "CD001", 5) != 0))
    {
        return 0;
    }

    rootLba = (long)le32(pvd + 156 + 2);
    rootSize = (long)le32(pvd + 156 + 10);

    if ((rootSize <= 0) || (rootSize > 1024 * 1024))
    {
        return 0;
    }

    dir = (unsigned char *)malloc(rootSize);

    if ((dir == NULL) || !read_bytes(disc, rootLba, 0, rootSize, dir))
    {
        free(dir);
        return 0;
    }

    while (pos < rootSize)
    {
        int n = dir[pos];

        if (n == 0)
        {
            /* records do not cross sectors */
            pos = (pos / SECTOR + 1) * SECTOR;
            continue;
        }

        if ((pos + n <= rootSize) && (dir[pos + 32] >= len) && (_strnicmp((char *)&dir[pos + 33], name, len) == 0) &&
            ((dir[pos + 32] == len) || (dir[pos + 33 + len] == ';')))
        {
            *lba = (long)le32(&dir[pos + 2]);
            *size = (long)le32(&dir[pos + 10]);
            free(dir);
            return 1;
        }

        pos += n;
    }

    free(dir);
    return 0;
}

DcDisc *dc_disc_open(const char *gdi)
{
    DcDisc *disc = (DcDisc *)calloc(1, sizeof(DcDisc));
    unsigned char word[4];
    int i;

    if (disc == NULL)
    {
        return NULL;
    }

    disc->numTracks = parse_gdi(gdi, disc->tracks, MAX_TRACKS, 1);

    if (disc->numTracks <= 0)
    {
        free(disc);
        return NULL;
    }

    for (i = 0; i < disc->numTracks; i++)
    {
        DcTrack *t = &disc->tracks[i];
        WIN32_FILE_ATTRIBUTE_DATA info;

        if ((t->size <= 0) || !GetFileAttributesExA(t->path, GetFileExInfoStandard, &info))
        {
            dc_disc_close(disc);
            return NULL;
        }

        t->count = (long)(((unsigned long long)info.nFileSizeHigh << 32 | info.nFileSizeLow) / (unsigned long long)t->size);
    }

    if (!find_root_file(disc, "BIGFILE.DAT", &disc->bigLba, &disc->bigSize) ||
        !find_root_file(disc, "TEXTURES.VQ", &disc->texLba, &disc->texSize) ||
        !read_bytes(disc, disc->bigLba, 0, 4, word))
    {
        dc_disc_close(disc);
        return NULL;
    }

    disc->bigCount = (long)le32(word);

    if ((disc->bigCount <= 0) || (disc->bigCount > 65536))
    {
        dc_disc_close(disc);
        return NULL;
    }

    disc->bigDirectory = (unsigned char *)malloc(disc->bigCount * 16);

    if ((disc->bigDirectory == NULL) || !read_bytes(disc, disc->bigLba, 4, disc->bigCount * 16, disc->bigDirectory))
    {
        dc_disc_close(disc);
        return NULL;
    }

    return disc;
}

void dc_disc_close(DcDisc *disc)
{
    int i;

    if (disc == NULL)
    {
        return;
    }

    for (i = 0; i < disc->numTracks; i++)
    {
        if (disc->tracks[i].file != NULL)
        {
            fclose(disc->tracks[i].file);
        }
    }

    free(disc->bigDirectory);
    free(disc);
}

unsigned char *dc_disc_read_bigfile(DcDisc *disc, unsigned long hash, long *size)
{
    long i;

    for (i = 0; i < disc->bigCount; i++)
    {
        const unsigned char *e = &disc->bigDirectory[i * 16];

        if (le32(e) == hash)
        {
            long length = (long)le32(e + 4);
            long offset = (long)le32(e + 8);
            unsigned char *data;

            if ((length <= 0) || (offset < 0) || (offset + length > disc->bigSize))
            {
                return NULL;
            }

            data = (unsigned char *)malloc(length);

            if ((data == NULL) || !read_bytes(disc, disc->bigLba, offset, length, data))
            {
                free(data);
                return NULL;
            }

            *size = length;
            return data;
        }
    }

    return NULL;
}

int dc_disc_read_textures(DcDisc *disc, long offset, long length, unsigned char *out)
{
    if ((offset < 0) || (offset + length > disc->texSize))
    {
        return 0;
    }

    return read_bytes(disc, disc->texLba, offset, length, out);
}

int dc_disc_find_file(DcDisc *disc, const char *name, long *lba, long *size)
{
    return find_root_file(disc, name, lba, size);
}

int dc_disc_read_file(DcDisc *disc, long lba, long offset, long length, unsigned char *out)
{
    return read_bytes(disc, lba, offset, length, out);
}
