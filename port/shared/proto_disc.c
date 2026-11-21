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

long proto_hash_name(const char *path)
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

        if (c == '\\')
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

unsigned char *proto_disc_read(const char *image, const char *gamePath, long *size)
{
    CdImage cd;
    unsigned char word[8];
    unsigned char *subdirs = NULL;
    unsigned char *result = NULL;
    long bigLba;
    long bigSize;
    long numDirs;
    long hash = proto_hash_name(gamePath);
    long d;

    if (!cd_open(&cd, image))
    {
        return NULL;
    }

    if (!cd_find_bigfile(&cd, &bigLba, &bigSize) || !cd_read(&cd, bigLba, 0, 4, word))
    {
        fclose(cd.f);
        return NULL;
    }

    numDirs = le32(word);

    if ((numDirs <= 0) || (numDirs > 4096) || ((subdirs = (unsigned char *)malloc(numDirs * 8)) == NULL) ||
        !cd_read(&cd, bigLba, 4, numDirs * 8, subdirs))
    {
        free(subdirs);
        fclose(cd.f);
        return NULL;
    }

    /* every directory, the root (0) included */
    for (d = 0; (d < numDirs) && (result == NULL); d++)
    {
        long dirOffset = le32(subdirs + d * 8 + 4);
        long numFiles;
        unsigned char *files;
        long i;

        if (!cd_read(&cd, bigLba, dirOffset, 4, word))
        {
            continue;
        }

        numFiles = le32(word);

        if ((numFiles <= 0) || (numFiles > 65536) || ((files = (unsigned char *)malloc(numFiles * 16)) == NULL))
        {
            continue;
        }

        if (cd_read(&cd, bigLba, dirOffset + 4, numFiles * 16, files))
        {
            for (i = 0; i < numFiles; i++)
            {
                if (le32(files + i * 16) == hash)
                {
                    long length = le32(files + i * 16 + 4);
                    long filePos = le32(files + i * 16 + 8);

                    if ((length > 0) && (filePos >= 0) && (filePos + length <= bigSize) &&
                        ((result = (unsigned char *)malloc(length)) != NULL))
                    {
                        if (cd_read(&cd, bigLba, filePos, length, result))
                        {
                            *size = length;
                        }
                        else
                        {
                            free(result);
                            result = NULL;
                        }
                    }

                    break;
                }
            }
        }

        free(files);
    }

    free(subdirs);
    fclose(cd.f);

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

int proto_disc_locate(char *image, size_t imageSize, char *problem, size_t problemSize)
{
    static const char *const patterns[] = { "*.cue", "*.bin", "*.img", "*.iso" };
    char folder[MAX_PATH];
    char candidate[MAX_PATH];
    int sawImage = 0;
    int p;

    proto_disc_folder(folder, sizeof(folder));

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
            unsigned char *sheet;
            long size = 0;

            snprintf(candidate, sizeof(candidate), "%s\\%s", folder, found.cFileName);

            if ((p == 0) && !cue_image(candidate, candidate, sizeof(candidate)))
            {
                continue;
            }

            sawImage = 1;
            sheet = proto_disc_read(candidate, PROTO_CHECK_FILE, &size);

            if (sheet != NULL)
            {
                free(sheet);

                if ((size == 8228) && is_april_glyph_object(candidate))
                {
                    FindClose(h);

                    if ((image != NULL) && (imageSize > 0))
                    {
                        snprintf(image, imageSize, "%s", candidate);
                    }

                    return 1;
                }
            }
        } while (FindNextFileA(h, &found));

        FindClose(h);
    }

    if ((problem != NULL) && (problemSize > 0))
    {
        if (sawImage)
        {
            snprintf(problem, problemSize, "The disc image in %s is not the April 14, 1999 prototype.", folder);
        }
        else
        {
            snprintf(problem, problemSize, "No disc image (.cue, .bin, .img) in %s.", folder);
        }
    }

    return 0;
}
