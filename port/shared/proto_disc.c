/*
 * Reading files out of a prototype's CD image (one MODE2/2352 or 2048-byte
 * data track) and its BIGFILE.DAT, as the editor's leveldump/disc.py does:
 *
 *   long numSubDirs; { short id; short numFiles; long offset; } [numSubDirs]
 *   at each offset: long numFiles; { long hash, len, pos, checksum; } [...]
 *
 * Positions and offsets are bytes from the start of BIGFILE.DAT.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "proto_disc.h"

#define USER_DATA   2048
#define RAW_SECTOR  2352

/* the icon sheet whose presence tells the April disc */
#define PROTO_CHECK_FILE "\\kain2\\object\\glphicon\\glphicon.crm"

typedef struct
{
    FILE *f;
    int sectorSize;
    int dataOffset;
} CdImage;

void proto_disc_folder(char *out, size_t size)
{
    char exe[MAX_PATH];
    char *slash;
    DWORD n = GetModuleFileNameA(NULL, exe, sizeof(exe));

    if ((n == 0) || (n >= sizeof(exe)))
    {
        snprintf(out, size, "proto");
        return;
    }

    slash = strrchr(exe, '\\');

    if (slash != NULL)
    {
        *slash = '\0';
    }

    snprintf(out, size, "%s\\proto", exe);
}

/* LOAD_HashName; the early (January to February) builds' version counts the
 * backslashes too */
static long hash_path(const char *path, int skipSlash)
{
    static const char *const extensions[] = { "drm", "crm", "tim", "smp", "snd", "smf", "snf" };
    long strl = (long)strlen(path) - 1;
    long total = 0;
    long xorValue = 0;
    long length = 0;
    long ext = 0;
    const char *dot = strchr(path, '.');

    if (dot != NULL)
    {
        int i;

        for (i = 0; i < 7; i++)
        {
            if (_stricmp(dot + 1, extensions[i]) == 0)
            {
                ext = i;
                strl -= 4;
                break;
            }
        }
    }

    while (strl >= 0)
    {
        int c = (unsigned char)path[strl--];

        if (skipSlash && (c == '\\'))
        {
            continue;
        }

        c = (signed char)(toupper(c) - 0x1A);
        total += c;
        xorValue ^= c * length;
        length++;
    }

    return (long)(((unsigned long)length << 27) | ((unsigned long)total << 15) | ((unsigned long)xorValue << 3) | (unsigned long)ext);
}

long proto_hash_name(const char *path)
{
    return hash_path(path, 1);
}

static int cd_open(CdImage *cd, const char *path)
{
    static const unsigned char sync[12] = { 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0 };
    unsigned char head[16];

    cd->f = fopen(path, "rb");

    if (cd->f == NULL)
    {
        return 0;
    }

    if (fread(head, 1, 16, cd->f) != 16)
    {
        fclose(cd->f);
        return 0;
    }

    if (memcmp(head, sync, 12) == 0)
    {
        cd->sectorSize = RAW_SECTOR;
        /* MODE2 Form 1: sync, header, subheader; MODE1: sync, header */
        cd->dataOffset = (head[15] == 2) ? 24 : 16;
    }
    else
    {
        cd->sectorSize = USER_DATA;
        cd->dataOffset = 0;
    }

    return 1;
}

static int cd_read(CdImage *cd, long lba, long offset, long length, unsigned char *out)
{
    lba += offset / USER_DATA;
    offset %= USER_DATA;

    while (length > 0)
    {
        long n = USER_DATA - offset;

        if (n > length)
        {
            n = length;
        }

        if ((_fseeki64(cd->f, (long long)lba * cd->sectorSize + cd->dataOffset + offset, SEEK_SET) != 0) ||
            (fread(out, 1, n, cd->f) != (size_t)n))
        {
            return 0;
        }

        out += n;
        length -= n;
        offset = 0;
        lba++;
    }

    return 1;
}

static long le32(const unsigned char *p)
{
    return (long)((unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24));
}

/* BIGFILE.DAT in the root directory */
static int cd_find_bigfile(CdImage *cd, long *lba, long *size)
{
    unsigned char pvd[USER_DATA];
    unsigned char *dir;
    long rootLba;
    long rootSize;
    long pos = 0;

    if (!cd_read(cd, 16, 0, USER_DATA, pvd) || (memcmp(pvd + 1, "CD001", 5) != 0))
    {
        return 0;
    }

    rootLba = le32(pvd + 156 + 2);
    rootSize = le32(pvd + 156 + 10);

    if ((rootSize <= 0) || (rootSize > 1024 * 1024) || ((dir = (unsigned char *)malloc(rootSize)) == NULL))
    {
        return 0;
    }

    if (!cd_read(cd, rootLba, 0, rootSize, dir))
    {
        free(dir);
        return 0;
    }

    while (pos < rootSize)
    {
        int n = dir[pos];

        if (n == 0)
        {
            pos = (pos / USER_DATA + 1) * USER_DATA;
            continue;
        }

        if ((pos + n <= rootSize) && (dir[pos + 32] >= 11) && (_strnicmp((char *)&dir[pos + 33], "BIGFILE.DAT", 11) == 0))
        {
            *lba = le32(&dir[pos + 2]);
            *size = le32(&dir[pos + 10]);
            free(dir);
            return 1;
        }

        pos += n;
    }

    free(dir);
    return 0;
}

struct ProtoBigFile
{
    CdImage cd;
    long lba;
    long size;
    int early;          /* the flat table of the January to February builds */
    long count;
    long *entries;      /* hash, length, position: sorted by hash */
};

static int compare_entries(const void *a, const void *b)
{
    long x = *(const long *)a;
    long y = *(const long *)b;

    return (x < y) ? -1 : (x > y);
}

/* by hash, then by the order read */
static int compare_pairs(const void *a, const void *b)
{
    const long *x = (const long *)a;
    const long *y = (const long *)b;

    if (x[0] != y[0])
    {
        return (x[0] < y[0]) ? -1 : 1;
    }

    return (x[1] < y[1]) ? -1 : (x[1] > y[1]);
}

/* the files of every directory, the root (0) first: the first of a hash wins */
static int read_directories(ProtoBigFile *bf, long numDirs)
{
    unsigned char word[4];
    unsigned char *subdirs = (unsigned char *)malloc(numDirs * 8);
    long d;

    if ((subdirs == NULL) || !cd_read(&bf->cd, bf->lba, 4, numDirs * 8, subdirs))
    {
        free(subdirs);
        return 0;
    }

    for (d = 0; d < numDirs; d++)
    {
        long dirOffset = le32(subdirs + d * 8 + 4);
        long numFiles;
        unsigned char *files;
        long *grown;
        long i;

        if (!cd_read(&bf->cd, bf->lba, dirOffset, 4, word))
        {
            continue;
        }

        numFiles = le32(word);

        if ((numFiles <= 0) || (numFiles > 65536) || ((files = (unsigned char *)malloc(numFiles * 16)) == NULL))
        {
            continue;
        }

        grown = (long *)realloc(bf->entries, (bf->count + numFiles) * 3 * sizeof(long));

        if ((grown != NULL) && cd_read(&bf->cd, bf->lba, dirOffset + 4, numFiles * 16, files))
        {
            bf->entries = grown;

            for (i = 0; i < numFiles; i++)
            {
                long *e = bf->entries + bf->count * 3;

                e[0] = le32(files + i * 16);
                e[1] = le32(files + i * 16 + 4);
                e[2] = le32(files + i * 16 + 8);
                bf->count++;
            }
        }
        else if (grown != NULL)
        {
            bf->entries = grown;
        }

        free(files);
    }

    free(subdirs);

    return bf->count > 0;
}

/* the early builds: long numFiles; { hash, length, ?, position, checksum, ? } [numFiles] */
static int read_flat_table(ProtoBigFile *bf, long numFiles)
{
    unsigned char *files = (unsigned char *)malloc(numFiles * 24);
    long i;

    bf->entries = (long *)malloc(numFiles * 3 * sizeof(long));

    if ((files == NULL) || (bf->entries == NULL) || !cd_read(&bf->cd, bf->lba, 4, numFiles * 24, files))
    {
        free(files);
        return 0;
    }

    for (i = 0; i < numFiles; i++)
    {
        bf->entries[i * 3] = le32(files + i * 24);
        bf->entries[i * 3 + 1] = le32(files + i * 24 + 4);
        bf->entries[i * 3 + 2] = le32(files + i * 24 + 12);
    }

    bf->count = numFiles;
    free(files);

    return 1;
}

ProtoBigFile *proto_bigfile_open(const char *image)
{
    ProtoBigFile *bf = (ProtoBigFile *)calloc(1, sizeof(ProtoBigFile));
    unsigned char head[12];
    long num;
    long firstOffset;
    int ok;

    if ((bf == NULL) || !cd_open(&bf->cd, image))
    {
        free(bf);
        return NULL;
    }

    if (!cd_find_bigfile(&bf->cd, &bf->lba, &bf->size) || !cd_read(&bf->cd, bf->lba, 0, 12, head))
    {
        proto_bigfile_close(bf);
        return NULL;
    }

    num = le32(head);
    /* the first directory's files follow the directory table (rounded up to a
     * sector); in the flat table that word is a file's length */
    firstOffset = le32(head + 8);

    if ((num <= 0) || (num > 65536))
    {
        proto_bigfile_close(bf);
        return NULL;
    }

    if ((firstOffset >= 4 + num * 8) && (firstOffset <= 4 + num * 8 + USER_DATA))
    {
        ok = read_directories(bf, num);
    }
    else
    {
        bf->early = 1;
        ok = read_flat_table(bf, num);
    }

    if (!ok)
    {
        proto_bigfile_close(bf);
        return NULL;
    }

    /* stable for equal hashes: the first one read stays first */
    {
        long i;
        long *keyed = (long *)malloc(bf->count * 4 * sizeof(long));

        if (keyed != NULL)
        {
            for (i = 0; i < bf->count; i++)
            {
                keyed[i * 4] = bf->entries[i * 3];
                keyed[i * 4 + 1] = i;
                keyed[i * 4 + 2] = bf->entries[i * 3 + 1];
                keyed[i * 4 + 3] = bf->entries[i * 3 + 2];
            }

            qsort(keyed, bf->count, 4 * sizeof(long), compare_pairs);

            for (i = 0; i < bf->count; i++)
            {
                bf->entries[i * 3] = keyed[i * 4];
                bf->entries[i * 3 + 1] = keyed[i * 4 + 2];
                bf->entries[i * 3 + 2] = keyed[i * 4 + 3];
            }

            free(keyed);
        }
    }

    return bf;
}

void proto_bigfile_close(ProtoBigFile *bf)
{
    if (bf != NULL)
    {
        if (bf->cd.f != NULL)
        {
            fclose(bf->cd.f);
        }

        free(bf->entries);
        free(bf);
    }
}

int proto_bigfile_early(const ProtoBigFile *bf)
{
    return bf->early;
}

int proto_bigfile_has(const ProtoBigFile *bf, const char *gamePath)
{
    long hash = hash_path(gamePath, !bf->early);
    const long *e = (const long *)bsearch(&hash, bf->entries, bf->count, 3 * sizeof(long), compare_entries);

    return (e != NULL) && (e[1] > 0);
}

unsigned char *proto_bigfile_read(ProtoBigFile *bf, const char *gamePath, long *size)
{
    long hash = hash_path(gamePath, !bf->early);
    const long *e = (const long *)bsearch(&hash, bf->entries, bf->count, 3 * sizeof(long), compare_entries);
    unsigned char *result;

    /* bsearch may land on any of equal hashes: go to the first */
    while ((e != NULL) && (e > bf->entries) && (e[-3] == hash))
    {
        e -= 3;
    }

    if ((e == NULL) || (e[1] <= 0) || (e[2] < 0) || (e[2] + e[1] > bf->size) ||
        ((result = (unsigned char *)malloc(e[1])) == NULL))
    {
        return NULL;
    }

    if (!cd_read(&bf->cd, bf->lba, e[2], e[1], result))
    {
        free(result);
        return NULL;
    }

    *size = e[1];

    return result;
}

unsigned char *proto_disc_read(const char *image, const char *gamePath, long *size)
{
    ProtoBigFile *bf = proto_bigfile_open(image);
    unsigned char *result;

    if (bf == NULL)
    {
        return NULL;
    }

    result = proto_bigfile_read(bf, gamePath, size);
    proto_bigfile_close(bf);

    return result;
}

/* the image a .cue names (its first FILE), next to the .cue */
static int cue_image(const char *cue, char *out, size_t size)
{
    char line[512];
    FILE *f = fopen(cue, "r");
    int found = 0;

    if (f == NULL)
    {
        return 0;
    }

    while (!found && (fgets(line, sizeof(line), f) != NULL))
    {
        char *a = strstr(line, "FILE");
        char *b;

        if ((a == NULL) || ((a = strchr(a, '"')) == NULL) || ((b = strchr(a + 1, '"')) == NULL))
        {
            continue;
        }

        {
            char folder[MAX_PATH];
            char *slash;

            snprintf(folder, sizeof(folder), "%s", cue);
            slash = strrchr(folder, '\\');

            if (slash != NULL)
            {
                slash[1] = '\0';
            }
            else
            {
                folder[0] = '\0';
            }

            snprintf(out, size, "%s%.*s", folder, (int)(b - a - 1), a + 1);
            found = 1;
        }
    }

    fclose(f);

    return found;
}

/* The glyph object tells April from July (both have an icon sheet of the
 * same size): April's glyphs cost no mana, its tune data's costs are all
 * zero; July's are 1, 2, 4, 6, 8, 10. The tune data is the object's data
 * pointer (offset 0x1C, relative after relocation) past the relocation
 * table; the costs start 4 bytes in. */
static int is_april_glyph_object(const char *image)
{
    long size = 0;
    unsigned char *drm = proto_disc_read(image, "\\kain2\\object\\glphicon\\glphicon.drm", &size);
    long numRelocs;
    long dataStart;
    long tune;
    int i;
    int april = 0;

    if (drm == NULL)
    {
        return 0;
    }

    numRelocs = (size >= 4) ? le32(drm) : -1;
    dataStart = ((numRelocs + 512) / 512) * 512 * 4;

    if ((numRelocs > 0) && (dataStart + 0x20 <= size))
    {
        tune = le32(drm + dataStart + 0x1C);

        if ((tune > 0) && (dataStart + tune + 12 <= size))
        {
            april = 1;

            for (i = 0; i < 8; i++)
            {
                if (drm[dataStart + tune + 4 + i] != 0)
                {
                    april = 0;
                }
            }
        }
    }

    free(drm);

    return april;
}

/* Calls `check` with each disc image in `folder` (a .cue's image, then .bin,
 * .img and .iso files) until it returns 1; 1 and that image's path then. */
static int each_image(const char *folder, int (*check)(const char *image, void *ctx), void *ctx,
    char *image, size_t imageSize, int *sawImage)
{
    static const char *const patterns[] = { "*.cue", "*.bin", "*.img", "*.iso" };
    char candidate[MAX_PATH];
    int p;

    for (p = 0; p < 4; p++)
    {
        char pattern[MAX_PATH];
        WIN32_FIND_DATAA found;
        HANDLE h;

        snprintf(pattern, sizeof(pattern), "%s\\%s", folder, patterns[p]);
        h = FindFirstFileA(pattern, &found);

        if (h == INVALID_HANDLE_VALUE)
        {
            continue;
        }

        do
        {
            snprintf(candidate, sizeof(candidate), "%s\\%s", folder, found.cFileName);

            if ((p == 0) && !cue_image(candidate, candidate, sizeof(candidate)))
            {
                continue;
            }

            if (sawImage != NULL)
            {
                *sawImage = 1;
            }

            if (check(candidate, ctx))
            {
                FindClose(h);

                if ((image != NULL) && (imageSize > 0))
                {
                    snprintf(image, imageSize, "%s", candidate);
                }

                return 1;
            }
        } while (FindNextFileA(h, &found));

        FindClose(h);
    }

    return 0;
}

static int has_bigfile(const char *image, void *ctx)
{
    ProtoBigFile *bf = proto_bigfile_open(image);

    (void)ctx;
    proto_bigfile_close(bf);

    return bf != NULL;
}

int proto_disc_image_in(const char *folder, char *image, size_t imageSize)
{
    return each_image(folder, has_bigfile, NULL, image, imageSize, NULL);
}

static int is_april_disc(const char *image, void *ctx)
{
    long size = 0;
    unsigned char *sheet = proto_disc_read(image, PROTO_CHECK_FILE, &size);

    (void)ctx;

    if (sheet == NULL)
    {
        return 0;
    }

    free(sheet);

    return (size == 8228) && is_april_glyph_object(image);
}

/* in the proto folder, or one of its folders (Extract Earlier Areas puts each
 * prototype in a folder named by its date) */
int proto_disc_locate(char *image, size_t imageSize, char *problem, size_t problemSize)
{
    char folder[MAX_PATH];
    char pattern[MAX_PATH];
    WIN32_FIND_DATAA found;
    HANDLE h;
    int sawImage = 0;

    proto_disc_folder(folder, sizeof(folder));

    if (each_image(folder, is_april_disc, NULL, image, imageSize, &sawImage))
    {
        return 1;
    }

    snprintf(pattern, sizeof(pattern), "%s\\*", folder);
    h = FindFirstFileA(pattern, &found);

    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            char sub[MAX_PATH];

            if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (found.cFileName[0] == '.'))
            {
                continue;
            }

            snprintf(sub, sizeof(sub), "%s\\%s", folder, found.cFileName);

            if (each_image(sub, is_april_disc, NULL, image, imageSize, &sawImage))
            {
                FindClose(h);
                return 1;
            }
        } while (FindNextFileA(h, &found));

        FindClose(h);
    }

    if ((problem != NULL) && (problemSize > 0))
    {
        if (sawImage)
        {
            snprintf(problem, problemSize, "None of the disc images in %s is the April 14, 1999 prototype.", folder);
        }
        else
        {
            snprintf(problem, problemSize, "No disc image (.cue, .bin, .img) in %s.", folder);
        }
    }

    return 0;
}
