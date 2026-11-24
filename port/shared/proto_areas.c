/*
 * Earlier areas: unpacking the prototypes and extracting their maps
 * (proto_areas.h).
 *
 * A prototype's BIGFILE.DAT keys its files by a hash of their paths, so the
 * maps are found by name: every unit name of the known areas, with numbers
 * from 0 to 399, and the areas named by the maps' portals ("<unit>,<n>").
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "proto_areas.h"
#include "proto_disc.h"
#include "proto_convert.h"

#define MAX_PREFIXES    256
#define MAX_UNITS       1024
#define MAX_UNIT_NUMBER 400
#define MAX_OBJECTS     1024
#define INDEX_NAME      "index.txt"
#define INDEX_VERSION   4

/* the areas of the prototypes and the July build */
static const char *const known_areas[] = {
    "add", "adda", "aluka", "bonus", "boss", "cathy", "chapel", "chrono", "city", "cityout", "clfpil",
    "clfsubb", "cliff", "conect", "conecta", "conectb", "conectc", "conectd", "conecte", "conectg", "demo",
    "fill", "filtuna", "filtunb", "fire", "forge", "htorm", "htotun", "huba", "hubb", "hunt", "intvaly",
    "lair", "litein", "litetun", "mrlock", "nighta", "nightb", "oracle", "out", "pillar", "pillars",
    "pisaira", "pistair", "piston", "pistunl", "push", "skinnr", "soundg", "stone", "sunrm", "suntnl",
    "suntnlb", "test", "tomb", "tompil", "tower", "train", "under", "undrct",
};

static const char *const unit_extensions[] = { "drm", "crm", "snf", "smf" };

/* an object's files, by its name */
static const char *const object_paths[] = {
    "\\kain2\\object\\%s\\%s.drm", "\\kain2\\object\\%s\\%s.crm",
    "\\kain2\\sfx\\object\\%s\\%s.snf", "\\kain2\\sfx\\object\\%s\\%s.smf",
};

static const char *const month_names[] = {
    "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec",
};

static long le32(const unsigned char *p)
{
    return (long)((unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24));
}

void proto_areas_folder(const char *date, char *out, size_t size)
{
    char proto[MAX_PATH];

    proto_disc_folder(proto, sizeof(proto));
    snprintf(out, size, "%s\\%s\\areas", proto, date);
}

static void say(ProtoAreasProgress progress, void *ctx, const char *format, ...)
{
    char text[512];
    va_list args;

    if (progress == NULL)
    {
        return;
    }

    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);

    progress(ctx, text);
}

static void append(char *report, size_t size, const char *format, ...)
{
    size_t used = strlen(report);
    va_list args;

    if (used + 1 >= size)
    {
        return;
    }

    va_start(args, format);
    vsnprintf(report + used, size - used, format, args);
    va_end(args);
}

/* --- archives --- */

/* The date in an archive's name, as YYYY-MM-DD: "...-1999-01-23.7z" or
 * "... (Jun 18, 1999 prototype).7z". 0 if it has none. */
static int archive_date(const char *name, char *out, size_t size)
{
    const char *p;
    int m;

    for (p = name; *p != '\0'; p++)
    {
        if (isdigit((unsigned char)p[0]) && isdigit((unsigned char)p[1]) && isdigit((unsigned char)p[2]) &&
            isdigit((unsigned char)p[3]) && (p[4] == '-') && isdigit((unsigned char)p[5]) &&
            isdigit((unsigned char)p[6]) && (p[7] == '-') && isdigit((unsigned char)p[8]) && isdigit((unsigned char)p[9]))
        {
            snprintf(out, size, "%.10s", p);
            return 1;
        }
    }

    for (p = name; *p != '\0'; p++)
    {
        for (m = 0; m < 12; m++)
        {
            int day;
            int year;

            if ((_strnicmp(p, month_names[m], 3) == 0) && ((p == name) || !isalpha((unsigned char)p[-1])))
            {
                const char *q = p + 3;

                while (isalpha((unsigned char)*q) || (*q == '.'))
                {
                    q++;
                }

                if ((sscanf(q, " %d , %d", &day, &year) == 2) && (day >= 1) && (day <= 31) && (year >= 1990) && (year < 2100))
                {
                    snprintf(out, size, "%04d-%02d-%02d", year, m + 1, day);
                    return 1;
                }
            }
        }
    }

    return 0;
}

static int run_and_wait(char *commandLine)
{
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD code = 1;

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);

    if (!CreateProcessA(NULL, commandLine, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
    {
        return 0;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return code == 0;
}

/* Windows' own tar (libarchive) reads .7z and .zip; 7-Zip if it's installed,
 * for what tar can't. Only the disc images and their .cue files are needed. */
static int unpack(const char *archive, const char *folder)
{
    char command[3 * MAX_PATH];
    char system[MAX_PATH];
    char sevenZip[MAX_PATH];
    DWORD size = sizeof(sevenZip);

    GetSystemDirectoryA(system, sizeof(system));
    snprintf(command, sizeof(command),
        "\"%s\\tar.exe\" -x -f \"%s\" -C \"%s\" --exclude *.sub --exclude *.img.bin --exclude *.lic",
        system, archive, folder);

    if (run_and_wait(command))
    {
        return 1;
    }

    if (RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\7-Zip", "Path", RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, NULL,
            sevenZip, &size) == ERROR_SUCCESS)
    {
        snprintf(command, sizeof(command), "\"%s\\7z.exe\" x \"%s\" -o\"%s\" -y -x!*.sub -x!*.img.bin -x!*.lic",
            sevenZip, archive, folder);

        return run_and_wait(command);
    }

    return 0;
}

static void unpack_archives(ProtoAreasProgress progress, void *ctx, char *report, size_t reportSize)
{
    static const char *const patterns[] = { "*.7z", "*.zip" };
    char proto[MAX_PATH];
    int p;

    proto_disc_folder(proto, sizeof(proto));

    for (p = 0; p < 2; p++)
    {
        char pattern[MAX_PATH];
        WIN32_FIND_DATAA found;
        HANDLE h;

        snprintf(pattern, sizeof(pattern), "%s\\%s", proto, patterns[p]);
        h = FindFirstFileA(pattern, &found);

        if (h == INVALID_HANDLE_VALUE)
        {
            continue;
        }

        do
        {
            char date[PROTO_DATE_SIZE];
            char folder[MAX_PATH];
            char archive[MAX_PATH];
            char image[MAX_PATH];

            if (!archive_date(found.cFileName, date, sizeof(date)))
            {
                append(report, reportSize, "%s: no date in its name, left alone.\n", found.cFileName);
                continue;
            }

            snprintf(folder, sizeof(folder), "%s\\%s", proto, date);
            snprintf(archive, sizeof(archive), "%s\\%s", proto, found.cFileName);

            if (proto_disc_image_in(folder, image, sizeof(image)))
            {
                continue;
            }

            say(progress, ctx, "Unpacking %s...", found.cFileName);
            CreateDirectoryA(folder, NULL);

            if (!unpack(archive, folder) || !proto_disc_image_in(folder, image, sizeof(image)))
            {
                append(report, reportSize, "%s: could not be unpacked.\n", found.cFileName);
            }
        } while (FindNextFileA(h, &found));

        FindClose(h);
    }
}

/* --- maps --- */

typedef struct
{
    char prefixes[MAX_PREFIXES][PROTO_UNIT_SIZE];
    int prefixCount;
    char units[MAX_UNITS][PROTO_UNIT_SIZE];
    int unitCount;
    char objects[MAX_OBJECTS][PROTO_UNIT_SIZE];
    int objectCount;
} UnitSearch;

static void add_prefix(UnitSearch *s, const char *prefix, size_t length)
{
    int i;

    if ((length == 0) || (length >= PROTO_UNIT_SIZE - 4) || (s->prefixCount >= MAX_PREFIXES))
    {
        return;
    }

    for (i = 0; i < s->prefixCount; i++)
    {
        if ((strlen(s->prefixes[i]) == length) && (_strnicmp(s->prefixes[i], prefix, length) == 0))
        {
            return;
        }
    }

    memcpy(s->prefixes[s->prefixCount], prefix, length);
    s->prefixes[s->prefixCount][length] = '\0';
    _strlwr(s->prefixes[s->prefixCount]);
    s->prefixCount++;
}

/* the areas a map's portals lead to: "<letters><digits>,<digits>\0" */
static void add_portal_areas(UnitSearch *s, const unsigned char *data, long size)
{
    long i;

    for (i = 1; i + 2 < size; i++)
    {
        long end;
        long start;
        long letters;

        if ((data[i] != ',') || !isdigit(data[i - 1]) || !isdigit(data[i + 1]))
        {
            continue;
        }

        for (end = i + 1; (end < size) && isdigit(data[end]); end++)
        {
        }

        if ((end >= size) || (data[end] != '\0'))
        {
            continue;
        }

        for (start = i - 1; (start > 0) && isdigit(data[start - 1]); start--)
        {
        }

        for (letters = start; (letters > 0) && islower(data[letters - 1]); letters--)
        {
        }

        if (start - letters >= 2)
        {
            add_prefix(s, (const char *)data + letters, (size_t)(start - letters));
        }
    }
}

/* \kain2\area\<area>\bin\<unit>.<ext>, the area being the unit's name up to its first digit */
static void unit_path(const char *unit, const char *ext, char *out, size_t size)
{
    size_t n = strcspn(unit, "0123456789");

    snprintf(out, size, "\\kain2\\area\\%.*s\\bin\\%s.%s", (int)n, unit, unit, ext);
}

/* The game reads a level whose Terrain has the July layout: its word at 0x2C
 * is padding. The builds up to May have a pointer there (and a different
 * layout after it). */
static int loads_as_is(const unsigned char *drm, long size)
{
    long numRelocs;
    long dataStart;
    long terrain;
    long i;

    if (size < 4)
    {
        return 0;
    }

    numRelocs = le32(drm);
    dataStart = ((numRelocs + 512) / 512) * 512 * 4;

    if ((numRelocs <= 0) || (4 + numRelocs * 4 > size) || (dataStart + 4 > size))
    {
        return 0;
    }

    terrain = le32(drm + dataStart);

    for (i = 0; i < numRelocs; i++)
    {
        if (le32(drm + 4 + i * 4) == terrain + 0x2C)
        {
            return 0;
        }
    }

    return 1;
}

/* whether a level has an intro for Raziel (the game needs one in the map it
 * starts in) */
static int level_startable(const unsigned char *drm, long size)
{
    long numRelocs = le32(drm);
    long dataStart = ((numRelocs + 512) / 512) * 512 * 4;
    const unsigned char *body = drm + dataStart;
    long bodySize = size - dataStart;
    long intros, numIntros, i;

    if (bodySize < 0x80)
    {
        return 0;
    }

    intros = le32(body + 0x7C);
    numIntros = le32(body + 0x78);

    for (i = 0; (intros > 0) && (i < numIntros) && (intros + (i + 1) * 0x4C <= bodySize); i++)
    {
        if (_strnicmp((const char *)body + intros + i * 0x4C, "raziel", 7) == 0)
        {
            return 1;
        }
    }

    return 0;
}

/* the objects a level that loads as it is names: Level.objectNameList (0x8C),
 * 16-byte names up to a 0xFF */
static void add_object(UnitSearch *s, const unsigned char *at)
{
    char name[PROTO_UNIT_SIZE];
    int i;

    memcpy(name, at, 15);
    name[15] = '\0';

    for (i = 0; name[i] != '\0'; i++)
    {
        if (!isalnum((unsigned char)name[i]) && (name[i] != '_'))
        {
            return;
        }
    }

    if (i == 0)
    {
        return;
    }

    for (i = 0; i < s->objectCount; i++)
    {
        if (_stricmp(s->objects[i], name) == 0)
        {
            return;
        }
    }

    if (s->objectCount < MAX_OBJECTS)
    {
        snprintf(s->objects[s->objectCount++], PROTO_UNIT_SIZE, "%s", name);
    }
}

/* ... and its intros (Level.introList, 0x7C, of Level.numIntros, 0x78,
 * 0x4C bytes each, the name first) */
static void add_level_objects(UnitSearch *s, const unsigned char *drm, long size)
{
    long numRelocs = le32(drm);
    long dataStart = ((numRelocs + 512) / 512) * 512 * 4;
    const unsigned char *body = drm + dataStart;
    long bodySize = size - dataStart;
    long list;
    long intros;
    long numIntros;
    long i;

    if (bodySize < 0x90)
    {
        return;
    }

    intros = le32(body + 0x7C);
    numIntros = le32(body + 0x78);

    for (i = 0; (intros > 0) && (i < numIntros) && (intros + (i + 1) * 0x4C <= bodySize); i++)
    {
        add_object(s, body + intros + i * 0x4C);
    }

    if (((list = le32(body + 0x8C)) <= 0) || (list >= bodySize))
    {
        return;
    }

    for (; (list + 16 <= bodySize) && (body[list] != 0xFF); list += 16)
    {
        char name[PROTO_UNIT_SIZE];
        int i;

        memcpy(name, body + list, 15);
        name[15] = '\0';

        for (i = 0; name[i] != '\0'; i++)
        {
            if (!isalnum((unsigned char)name[i]) && (name[i] != '_'))
            {
                break;
            }
        }

        if ((i == 0) || (name[i] != '\0'))
        {
            continue;
        }

        for (i = 0; i < s->objectCount; i++)
        {
            if (_stricmp(s->objects[i], name) == 0)
            {
                break;
            }
        }

        if ((i == s->objectCount) && (s->objectCount < MAX_OBJECTS))
        {
            snprintf(s->objects[s->objectCount++], PROTO_UNIT_SIZE, "%s", name);
        }
    }
}

static int make_folders(const char *path)
{
    char partial[MAX_PATH];
    const char *p;

    for (p = strchr(path + 3, '\\'); p != NULL; p = strchr(p + 1, '\\'))
    {
        snprintf(partial, sizeof(partial), "%.*s", (int)(p - path), path);
        CreateDirectoryA(partial, NULL);
    }

    return 1;
}

static unsigned long checksum(const unsigned char *data, long size)
{
    unsigned long sum = 0;
    long i;

    for (i = 0; i + 4 <= size; i += 4)
    {
        sum += (unsigned long)le32(data + i);
    }

    if (i < size)
    {
        unsigned char last[4] = { 0, 0, 0, 0 };

        memcpy(last, data + i, (size_t)(size - i));
        sum += (unsigned long)le32(last);
    }

    return sum;
}

static int compare_units(const void *a, const void *b)
{
    const char *x = (const char *)a;
    const char *y = (const char *)b;
    size_t nx = strcspn(x, "0123456789");
    size_t ny = strcspn(y, "0123456789");
    int c = strncmp(x, y, (nx < ny) ? nx : ny);

    if (c != 0)
    {
        return c;
    }

    if (nx != ny)
    {
        return (nx < ny) ? -1 : 1;
    }

    return atoi(x + nx) - atoi(y + ny);
}

/* data as a file of the areas folder, and its index line */
static int write_file(const char *folder, const char *path, const char *kind, FILE *index,
    const unsigned char *data, long size)
{
    char disk[MAX_PATH];
    FILE *f;
    int ok = 0;

    snprintf(disk, sizeof(disk), "%s%s", folder, path);
    make_folders(disk);
    f = fopen(disk, "wb");

    if ((f != NULL) && (fwrite(data, 1, (size_t)size, f) == (size_t)size))
    {
        fprintf(index, "%s %s %ld %lu\n", kind, path, size, checksum(data, size));
        ok = 1;
    }

    if (f != NULL)
    {
        fclose(f);
    }

    return ok;
}

/* the July build's objects, from the game's own disc (for leaving out of
 * converted maps the objects it lacks) */
static int july_has_object(void *ctx, const char *name)
{
    char path[MAX_PATH];

    snprintf(path, sizeof(path), "\\kain2\\object\\%s\\%s.drm", name, name);

    return proto_bigfile_has((ProtoBigFile *)ctx, path);
}

static ProtoBigFile *open_july_disc(void)
{
    char folder[MAX_PATH];
    char image[MAX_PATH];
    char *slash;

    proto_disc_folder(folder, sizeof(folder));
    slash = strrchr(folder, '\\');

    if (slash == NULL)
    {
        return NULL;
    }

    strcpy(slash + 1, "disc");

    return proto_disc_image_in(folder, image, sizeof(image)) ? proto_bigfile_open(image) : NULL;
}

/* one file of the prototype's bigfile into the areas folder, and its index line */
static int copy_file(ProtoBigFile *bf, const char *folder, const char *path, const char *kind, FILE *index,
    unsigned char **keep, long *keepSize)
{
    char disk[MAX_PATH];
    unsigned char *data;
    long size = 0;
    FILE *f;
    int ok = 0;

    data = proto_bigfile_read(bf, path, &size);

    if (data == NULL)
    {
        return 0;
    }

    snprintf(disk, sizeof(disk), "%s%s", folder, path);
    make_folders(disk);
    f = fopen(disk, "wb");

    if ((f != NULL) && (fwrite(data, 1, (size_t)size, f) == (size_t)size))
    {
        fprintf(index, "%s %s %ld %lu\n", kind, path, size, checksum(data, size));
        ok = 1;
    }

    if (f != NULL)
    {
        fclose(f);
    }

    if (keep != NULL)
    {
        *keep = data;
        *keepSize = size;
    }
    else
    {
        free(data);
    }

    return ok;
}

/* the maps of one prototype's disc image into proto\<date>\areas */
static int extract_maps(const char *date, const char *image, ProtoAreasProgress progress, void *ctx,
    char *report, size_t reportSize)
{
    char folder[MAX_PATH];
    char indexPath[MAX_PATH];
    char tempPath[MAX_PATH];
    UnitSearch *s = (UnitSearch *)calloc(1, sizeof(UnitSearch));
    ProtoBigFile *bf = proto_bigfile_open(image);
    FILE *index;
    ProtoBigFile *july = NULL;
    int format = 1;
    int convert;
    int left = 0;
    int done;
    int i;

    if ((s == NULL) || (bf == NULL))
    {
        append(report, reportSize, "%s: its disc image could not be read.\n", date);
        proto_bigfile_close(bf);
        free(s);
        return 0;
    }

    say(progress, ctx, "Finding the maps of the %s prototype...", date);

    for (i = 0; i < (int)(sizeof(known_areas) / sizeof(known_areas[0])); i++)
    {
        add_prefix(s, known_areas[i], strlen(known_areas[i]));
    }

    /* the list grows as the portals name more areas */
    for (done = 0; done < s->prefixCount; done++)
    {
        int n;

        for (n = 0; n < MAX_UNIT_NUMBER; n++)
        {
            char unit[PROTO_UNIT_SIZE];
            char path[MAX_PATH];
            unsigned char *drm;
            long size = 0;

            snprintf(unit, sizeof(unit), "%s%d", s->prefixes[done], n);
            unit_path(unit, "drm", path, sizeof(path));

            if (!proto_bigfile_has(bf, path) || (s->unitCount >= MAX_UNITS))
            {
                continue;
            }

            drm = proto_bigfile_read(bf, path, &size);

            if (drm == NULL)
            {
                continue;
            }

            add_portal_areas(s, drm, size);
            free(drm);
            snprintf(s->units[s->unitCount++], PROTO_UNIT_SIZE, "%s", unit);
        }
    }

    qsort(s->units, s->unitCount, PROTO_UNIT_SIZE, compare_units);

    proto_areas_folder(date, folder, sizeof(folder));
    snprintf(indexPath, sizeof(indexPath), "%s\\%s", folder, INDEX_NAME);
    snprintf(tempPath, sizeof(tempPath), "%s\\%s.part", folder, INDEX_NAME);
    make_folders(tempPath);

    index = fopen(tempPath, "w");

    if (index == NULL)
    {
        append(report, reportSize, "%s: cannot write to %s.\n", date, folder);
        proto_bigfile_close(bf);
        free(s);
        return 0;
    }

    convert = proto_convert_known(date);

    if (convert)
    {
        july = open_july_disc();
    }
    else if (proto_bigfile_early(bf))
    {
        format = 0;
    }

    say(progress, ctx, "Extracting the %d maps of the %s prototype%s...", s->unitCount, date,
        convert ? " (and converting them to the game's level format)" : "");

    for (i = 0; i < s->unitCount; i++)
    {
        char path[MAX_PATH];
        unsigned char *level;
        long size = 0;
        int e;

        unit_path(s->units[i], "drm", path, sizeof(path));
        level = proto_bigfile_read(bf, path, &size);

        if (level == NULL)
        {
            continue;
        }

        if (convert)
        {
            char crmPath[MAX_PATH];
            long crmSize = 0;
            long fresh = 0;
            unsigned char *crm;
            unsigned char *converted;

            unit_path(s->units[i], "crm", crmPath, sizeof(crmPath));
            crm = proto_bigfile_read(bf, crmPath, &crmSize);
            converted = proto_convert_level(date, level, size, crm, crmSize, (july != NULL) ? july_has_object : NULL,
                july, &fresh);
            free(crm);
            free(level);

            /* a file that isn't a map, or one the converter can't follow */
            if (converted == NULL)
            {
                left++;
                continue;
            }

            level = converted;
            size = fresh;
        }
        else if (!loads_as_is(level, size))
        {
            format = 0;
        }

        fprintf(index, "unit %s %d\n", s->units[i], format ? level_startable(level, size) : 0);
        write_file(folder, path, "file", index, level, size);

        if (format)
        {
            add_level_objects(s, level, size);
        }

        free(level);

        for (e = 1; e < 4; e++)
        {
            unit_path(s->units[i], unit_extensions[e], path, sizeof(path));
            copy_file(bf, folder, path, "file", index, NULL, NULL);
        }
    }

    proto_bigfile_close(july);

    /* the objects the maps name, for those the July build lacks (the
     * prototypes' up to February have another layout and are left out) */
    for (i = 0; (i < s->objectCount) && proto_convert_objects_load(date); i++)
    {
        int e;

        for (e = 0; e < 4; e++)
        {
            char path[MAX_PATH];

            snprintf(path, sizeof(path), object_paths[e], s->objects[i], s->objects[i]);
            copy_file(bf, folder, path, "object", index, NULL, NULL);
        }
    }

    fprintf(index, "version %d\n", INDEX_VERSION);
    fprintf(index, "format %d\n", format);
    fclose(index);
    MoveFileExA(tempPath, indexPath, MOVEFILE_REPLACE_EXISTING);

    if (!format)
    {
        append(report, reportSize, "%s: %d maps (an older level format the game can't load).\n", date, s->unitCount);
    }
    else if (convert)
    {
        append(report, reportSize, "%s: %d maps, converted to the game's level format%s.\n", date, s->unitCount - left,
            left ? " (some files that weren't maps left out)" : "");
    }
    else
    {
        append(report, reportSize, "%s: %d maps.\n", date, s->unitCount);
    }

    proto_bigfile_close(bf);
    free(s);

    return 1;
}

int proto_areas_extract(ProtoAreasProgress progress, void *ctx, char *report, size_t reportSize)
{
    char proto[MAX_PATH];
    char pattern[MAX_PATH];
    WIN32_FIND_DATAA found;
    HANDLE h;

    if ((report != NULL) && (reportSize > 0))
    {
        report[0] = '\0';
    }

    unpack_archives(progress, ctx, report, reportSize);

    proto_disc_folder(proto, sizeof(proto));
    snprintf(pattern, sizeof(pattern), "%s\\*", proto);
    h = FindFirstFileA(pattern, &found);

    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            char folder[MAX_PATH];
            char image[MAX_PATH];
            char date[PROTO_DATE_SIZE];

            if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (found.cFileName[0] == '.') ||
                !archive_date(found.cFileName, date, sizeof(date)) || (strcmp(date, found.cFileName) != 0))
            {
                continue;
            }

            if (proto_areas_read(date, NULL, NULL, NULL) >= 0)
            {
                char path[MAX_PATH];
                char line[128];
                int version = 0;
                FILE *f;

                proto_areas_folder(date, path, sizeof(path));
                strncat(path, "\\" INDEX_NAME, sizeof(path) - strlen(path) - 1);
                f = fopen(path, "r");

                while ((f != NULL) && (fgets(line, sizeof(line), f) != NULL))
                {
                    sscanf(line, "version %d", &version);
                }

                if (f != NULL)
                {
                    fclose(f);
                }

                if (version >= INDEX_VERSION)
                {
                    continue;
                }
            }

            snprintf(folder, sizeof(folder), "%s\\%s", proto, found.cFileName);

            if (proto_disc_image_in(folder, image, sizeof(image)))
            {
                extract_maps(date, image, progress, ctx, report, reportSize);
            }
        } while (FindNextFileA(h, &found));

        FindClose(h);
    }

    return proto_areas_dates(NULL, 0);
}

static int compare_dates(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

int proto_areas_dates(char (*dates)[PROTO_DATE_SIZE], int max)
{
    char proto[MAX_PATH];
    char pattern[MAX_PATH];
    WIN32_FIND_DATAA found;
    HANDLE h;
    int count = 0;

    proto_disc_folder(proto, sizeof(proto));
    snprintf(pattern, sizeof(pattern), "%s\\*", proto);
    h = FindFirstFileA(pattern, &found);

    if (h == INVALID_HANDLE_VALUE)
    {
        return 0;
    }

    do
    {
        char index[MAX_PATH];

        if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (found.cFileName[0] == '.') ||
            (strlen(found.cFileName) >= PROTO_DATE_SIZE))
        {
            continue;
        }

        snprintf(index, sizeof(index), "%s\\%s\\areas\\%s", proto, found.cFileName, INDEX_NAME);

        if (GetFileAttributesA(index) == INVALID_FILE_ATTRIBUTES)
        {
            continue;
        }

        if ((dates != NULL) && (count < max))
        {
            snprintf(dates[count], PROTO_DATE_SIZE, "%s", found.cFileName);
        }

        count++;
    } while (FindNextFileA(h, &found));

    FindClose(h);

    if (dates != NULL)
    {
        qsort(dates, (count < max) ? count : max, PROTO_DATE_SIZE, compare_dates);
    }

    return count;
}

int proto_areas_read(const char *date, ProtoAreasUnit unitCb, ProtoAreasFile fileCb, void *ctx)
{
    char folder[MAX_PATH];
    char path[MAX_PATH];
    char line[512];
    int format = -1;
    FILE *f;

    proto_areas_folder(date, folder, sizeof(folder));
    snprintf(path, sizeof(path), "%s\\%s", folder, INDEX_NAME);
    f = fopen(path, "r");

    if (f == NULL)
    {
        return -1;
    }

    while (fgets(line, sizeof(line), f) != NULL)
    {
        char a[300];
        long length;
        unsigned long sum;
        int value;

        if (sscanf(line, "unit %15s", a) == 1)
        {
            int startable = 1;

            sscanf(line, "unit %*s %d", &startable);

            if (unitCb != NULL)
            {
                unitCb(ctx, a, startable);
            }
        }
        else if (sscanf(line, "file %299s %ld %lu", a, &length, &sum) == 3)
        {
            if (fileCb != NULL)
            {
                fileCb(ctx, a, length, sum, 0);
            }
        }
        else if (sscanf(line, "object %299s %ld %lu", a, &length, &sum) == 3)
        {
            if (fileCb != NULL)
            {
                fileCb(ctx, a, length, sum, 1);
            }
        }
        else if (sscanf(line, "format %d", &value) == 1)
        {
            format = value ? 1 : 0;
        }
    }

    fclose(f);

    /* an index without its last line was cut short */
    return format;
}
