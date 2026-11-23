/*
 * The PC version's movies (Soul Reaver for Windows, 1999): the .rpl files in
 * the root of its CD. They are looked for in the "movies" folder next to the
 * game, either as files (copied off the CD, or an install that has them) or
 * inside an image of the CD there (.iso, or a .bin of 2048 or 2352-byte
 * sectors), whose root directory is read as ISO 9660.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pc_disc.h"

#define USER_DATA   2048
#define RAW_SECTOR  2352

/* the movie whose presence tells the PC disc */
#define PC_CHECK_MOVIE "intro.rpl"

void pc_disc_folder(char *out, size_t size)
{
    char exe[MAX_PATH];
    char *slash;
    DWORD n = GetModuleFileNameA(NULL, exe, sizeof(exe));

    if ((n == 0) || (n >= sizeof(exe)))
    {
        snprintf(out, size, "movies");
        return;
    }

    slash = strrchr(exe, '\\');

    if (slash != NULL)
    {
        *slash = '\0';
    }

    snprintf(out, size, "%s\\movies", exe);
}

static unsigned char *read_whole(const char *path, long *size)
{
    FILE *f = fopen(path, "rb");
    unsigned char *data;
    long n;

    if (f == NULL)
    {
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);

    data = (n > 0) ? (unsigned char *)malloc(n) : NULL;

    if ((data != NULL) && (fread(data, 1, n, f) != (size_t)n))
    {
        free(data);
        data = NULL;
    }

    fclose(f);
    *size = n;

    return data;
}

/* --- CD images ------------------------------------------------------------ */

typedef struct
{
    FILE *f;
    int sectorSize;
    int dataOffset;
} Image;

static int image_open(Image *im, const char *path)
{
    unsigned char sync[16];
    long size;

    im->f = fopen(path, "rb");

    if (im->f == NULL)
    {
        return 0;
    }

    fseek(im->f, 0, SEEK_END);
    size = ftell(im->f);
    fseek(im->f, 0, SEEK_SET);

    if ((fread(sync, 1, 16, im->f) == 16) && (sync[0] == 0) && (sync[1] == 0xFF) && (sync[11] == 0))
    {
        im->sectorSize = RAW_SECTOR;
        im->dataOffset = (sync[15] == 2) ? 24 : 16;
    }
    else
    {
        im->sectorSize = USER_DATA;
        im->dataOffset = 0;
    }

    (void)size;
    return 1;
}

/* length bytes from sector lba onwards, across sector boundaries */
static int image_read(Image *im, long lba, long length, unsigned char *out)
{
    while (length > 0)
    {
        long n = (length > USER_DATA) ? USER_DATA : length;

        if (fseek(im->f, lba * (long)im->sectorSize + im->dataOffset, SEEK_SET) != 0)
        {
            return 0;
        }

        if (fread(out, 1, n, im->f) != (size_t)n)
        {
            return 0;
        }

        out += n;
        length -= n;
        lba++;
    }

    return 1;
}

static long le32(const unsigned char *p)
{
    return (long)p[0] | ((long)p[1] << 8) | ((long)p[2] << 16) | ((long)p[3] << 24);
}

/* the root directory entry named name (case ignored, ";1" ignored) */
static int image_find(Image *im, const char *name, long *lba, long *length)
{
    unsigned char pvd[USER_DATA];
    unsigned char *dir;
    long dirLba;
    long dirLen;
    long pos;
    size_t nameLen = strlen(name);

    if (!image_read(im, 16, USER_DATA, pvd) || (pvd[0] != 1) || (memcmp(pvd + 1, "CD001", 5) != 0))
    {
        return 0;
    }

    dirLba = le32(pvd + 156 + 2);
    dirLen = le32(pvd + 156 + 10);

    if ((dirLen <= 0) || (dirLen > 1024 * 1024))
    {
        return 0;
    }

    dir = (unsigned char *)malloc(dirLen);

    if ((dir == NULL) || !image_read(im, dirLba, dirLen, dir))
    {
        free(dir);
        return 0;
    }

    for (pos = 0; pos < dirLen;)
    {
        int recLen = dir[pos];
        int idLen;
        const char *id;
        size_t i;

        if (recLen == 0)
        {
            /* records do not cross sectors: on to the next one */
            pos = (pos / USER_DATA + 1) * USER_DATA;
            continue;
        }

        idLen = dir[pos + 32];
        id = (const char *)&dir[pos + 33];

        if ((size_t)idLen >= nameLen)
        {
            for (i = 0; i < nameLen; i++)
            {
                if (tolower((unsigned char)id[i]) != tolower((unsigned char)name[i]))
                {
                    break;
                }
            }

            if ((i == nameLen) && (((size_t)idLen == nameLen) || (id[nameLen] == ';')))
            {
                *lba = le32(&dir[pos + 2]);
                *length = le32(&dir[pos + 10]);
                free(dir);
                return 1;
            }
        }

        pos += recLen;
    }

    free(dir);
    return 0;
}

/* each CD image in the folder, until fn returns non-zero */
static int for_each_image(int (*fn)(const char *path, void *ctx), void *ctx)
{
    static const char *const patterns[] = { "*.iso", "*.bin", "*.img" };
    char folder[MAX_PATH];
    int p;

    pc_disc_folder(folder, sizeof(folder));

    for (p = 0; p < 3; p++)
    {
        char query[MAX_PATH];
        WIN32_FIND_DATAA found;
        HANDLE h;

        snprintf(query, sizeof(query), "%s\\%s", folder, patterns[p]);
        h = FindFirstFileA(query, &found);

        if (h == INVALID_HANDLE_VALUE)
        {
            continue;
        }

        do
        {
            char path[MAX_PATH];

            snprintf(path, sizeof(path), "%s\\%s", folder, found.cFileName);

            if (fn(path, ctx))
            {
                FindClose(h);
                return 1;
            }
        } while (FindNextFileA(h, &found));

        FindClose(h);
    }

    return 0;
}

typedef struct
{
    const char *name;
    unsigned char *data;
    long size;
} ImageRead;

static int read_from_image(const char *path, void *ctx)
{
    ImageRead *r = (ImageRead *)ctx;
    Image im;
    long lba;
    long length;

    if (!image_open(&im, path))
    {
        return 0;
    }

    if (image_find(&im, r->name, &lba, &length) && (length > 0))
    {
        r->data = (unsigned char *)malloc(length);

        if ((r->data != NULL) && !image_read(&im, lba, length, r->data))
        {
            free(r->data);
            r->data = NULL;
        }

        r->size = length;
    }

    fclose(im.f);

    return r->data != NULL;
}

unsigned char *pc_disc_read_movie(const char *name, long *size)
{
    char folder[MAX_PATH];
    char path[MAX_PATH];
    unsigned char *data;
    ImageRead r;

    pc_disc_folder(folder, sizeof(folder));

    /* a loose file first */
    snprintf(path, sizeof(path), "%s\\%s", folder, name);
    data = read_whole(path, size);

    if (data != NULL)
    {
        return data;
    }

    r.name = name;
    r.data = NULL;
    r.size = 0;

    if (for_each_image(read_from_image, &r))
    {
        *size = r.size;
        return r.data;
    }

    return NULL;
}

static int image_has_check_movie(const char *path, void *ctx)
{
    Image im;
    long lba;
    long length;
    int found;

    (void)ctx;

    if (!image_open(&im, path))
    {
        return 0;
    }

    found = image_find(&im, PC_CHECK_MOVIE, &lba, &length);
    fclose(im.f);

    return found;
}

int pc_disc_locate(char *problem, size_t problemSize)
{
    char folder[MAX_PATH];
    char path[MAX_PATH];
    DWORD attr;

    pc_disc_folder(folder, sizeof(folder));

    snprintf(path, sizeof(path), "%s\\%s", folder, PC_CHECK_MOVIE);
    attr = GetFileAttributesA(path);

    if ((attr != INVALID_FILE_ATTRIBUTES) && !(attr & FILE_ATTRIBUTE_DIRECTORY))
    {
        return 1;
    }

    if (for_each_image(image_has_check_movie, NULL))
    {
        return 1;
    }

    if ((problem != NULL) && (problemSize > 0))
    {
        snprintf(problem, problemSize, "no %s and no image of the PC disc holding it in %s", PC_CHECK_MOVIE, folder);
    }

    return 0;
}
