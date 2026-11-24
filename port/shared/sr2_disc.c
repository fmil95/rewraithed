/*
 * Soul Reaver 2's copy of the first game's intro (sr2_disc.h): SR1INTRO.PSS in
 * the sr2 folder, or \PSS\SR1INTRO.PSS on an image of the PlayStation 2 disc
 * there (ISO 9660, 2048-byte sectors; read in place, nothing is unpacked).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sr2_disc.h"

#define SECTOR      2048
#define MOVIE_NAME  "SR1INTRO.PSS"

void sr2_folder(char *out, size_t size)
{
    char exe[MAX_PATH];
    char *slash;
    DWORD n = GetModuleFileNameA(NULL, exe, sizeof(exe));

    if ((n == 0) || (n >= sizeof(exe)))
    {
        snprintf(out, size, "sr2");
        return;
    }

    slash = strrchr(exe, '\\');

    if (slash != NULL)
    {
        *slash = '\0';
    }

    snprintf(out, size, "%s\\sr2", exe);
}

static unsigned long le32(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static int read_at(FILE *f, long long pos, void *out, size_t n)
{
    return (_fseeki64(f, pos, SEEK_SET) == 0) && (fread(out, 1, n, f) == n);
}

/* `name` in the directory at `lba` (`size` bytes): its extent and length */
static int find_entry(FILE *f, unsigned long lba, unsigned long size, const char *name, unsigned long *outLba,
    unsigned long *outSize)
{
    unsigned char *dir;
    unsigned long pos = 0;
    size_t want = strlen(name);

    if ((size == 0) || (size > 1024 * 1024) || ((dir = (unsigned char *)malloc(size)) == NULL))
    {
        return 0;
    }

    if (!read_at(f, (long long)lba * SECTOR, dir, size))
    {
        free(dir);
        return 0;
    }

    while (pos < size)
    {
        unsigned n = dir[pos];

        if (n == 0)
        {
            pos = (pos / SECTOR + 1) * SECTOR;
            continue;
        }

        if ((pos + n <= size) && (n > 33))
        {
            unsigned nameLength = dir[pos + 32];
            const char *entry = (const char *)&dir[pos + 33];

            /* "NAME;1" or a folder's "NAME" */
            if ((nameLength >= want) && (_strnicmp(entry, name, want) == 0) &&
                ((nameLength == want) || (entry[want] == ';')))
            {
                *outLba = le32(&dir[pos + 2]);
                *outSize = le32(&dir[pos + 10]);
                free(dir);
                return 1;
            }
        }

        pos += n;
    }

    free(dir);
    return 0;
}

/* \PSS\SR1INTRO.PSS in a disc image */
static int find_in_image(const char *image, long long *offset, long long *length)
{
    unsigned char pvd[SECTOR];
    unsigned long lba, size;
    FILE *f = fopen(image, "rb");
    int found = 0;

    if (f == NULL)
    {
        return 0;
    }

    if (read_at(f, 16LL * SECTOR, pvd, sizeof(pvd)) && (memcmp(pvd + 1, "CD001", 5) == 0) &&
        find_entry(f, le32(pvd + 156 + 2), le32(pvd + 156 + 10), "PSS", &lba, &size) &&
        find_entry(f, lba, size, MOVIE_NAME, &lba, &size))
    {
        *offset = (long long)lba * SECTOR;
        *length = size;
        found = 1;
    }

    fclose(f);

    return found;
}

int sr2_intro_locate(char *path, size_t pathSize, long long *offset, long long *length, char *problem, size_t problemSize)
{
    char folder[MAX_PATH];
    char candidate[MAX_PATH];
    char query[MAX_PATH];
    WIN32_FIND_DATAA found;
    HANDLE h;
    long long at = 0, size = 0;
    int ok = 0;

    sr2_folder(folder, sizeof(folder));

    if (GetFileAttributesA(folder) == INVALID_FILE_ATTRIBUTES)
    {
        if ((problem != NULL) && (problemSize > 0))
        {
            snprintf(problem, problemSize, "There is no sr2 folder next to the game.");
        }

        return 0;
    }

    /* the movie alone */
    snprintf(candidate, sizeof(candidate), "%s\\%s", folder, MOVIE_NAME);
    h = FindFirstFileA(candidate, &found);

    if (h != INVALID_HANDLE_VALUE)
    {
        FindClose(h);
        size = ((long long)found.nFileSizeHigh << 32) | found.nFileSizeLow;
        ok = 1;
    }

    /* else a disc image that has it */
    snprintf(query, sizeof(query), "%s\\*.iso", folder);
    h = ok ? INVALID_HANDLE_VALUE : FindFirstFileA(query, &found);

    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            snprintf(candidate, sizeof(candidate), "%s\\%s", folder, found.cFileName);
            ok = find_in_image(candidate, &at, &size);
        } while (!ok && FindNextFileA(h, &found));

        FindClose(h);
    }

    if (!ok)
    {
        if ((problem != NULL) && (problemSize > 0))
        {
            snprintf(problem, problemSize,
                "The sr2 folder has neither an image of the PlayStation 2 disc of Soul Reaver 2 (.iso) nor its %s.",
                MOVIE_NAME);
        }

        return 0;
    }

    if ((path != NULL) && (pathSize > 0))
    {
        snprintf(path, pathSize, "%s", candidate);
    }

    if (offset != NULL)
    {
        *offset = at;
    }

    if (length != NULL)
    {
        *length = size;
    }

    return 1;
}
