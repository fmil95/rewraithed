/*
 * The mods folder: "mods" next to rewraithed.exe. Its tree mirrors the
 * game's file names, e.g. mods\kain2\area\under\bin\under1.drm stands in for
 * \kain2\area\under\bin\under1.drm in BIGFILE.DAT (port/src/mod_files.c).
 * A folder in it with a mod.ini is a mod of its own (port/shared/
 * mod_folders.h), laid out the same way and only read when turned on.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

void port_exe_folder(char *out, size_t size);

typedef void (*port_mod_file_cb)(const char *game_path, const char *disk_path, void *ctx);

/* the mods folder's own loose files leave out its mod folders */
static int skip_mod_folders;

static void scan(const char *disk_dir, const char *game_dir, port_mod_file_cb cb, void *ctx)
{
    char query[MAX_PATH];
    WIN32_FIND_DATAA found;
    HANDLE h;

    snprintf(query, sizeof(query), "%s\\*", disk_dir);

    h = FindFirstFileA(query, &found);

    if (h == INVALID_HANDLE_VALUE)
    {
        return;
    }

    do
    {
        char disk_path[MAX_PATH];
        char game_path[MAX_PATH];

        if ((strcmp(found.cFileName, ".") == 0) || (strcmp(found.cFileName, "..") == 0))
        {
            continue;
        }

        snprintf(disk_path, sizeof(disk_path), "%s\\%s", disk_dir, found.cFileName);
        snprintf(game_path, sizeof(game_path), "%s\\%s", game_dir, found.cFileName);

        /* a mod folder's description, not a game file */
        if ((game_dir[0] == '\0') && (_stricmp(found.cFileName, "mod.ini") == 0))
        {
            continue;
        }

        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            char ini[MAX_PATH];

            /* a mod folder at the top: read on its own when turned on */
            snprintf(ini, sizeof(ini), "%s\\mod.ini", disk_path);

            if (skip_mod_folders && (game_dir[0] == '\0') && (GetFileAttributesA(ini) != INVALID_FILE_ATTRIBUTES))
            {
                continue;
            }

            scan(disk_path, game_path, cb, ctx);
        }
        else
        {
            cb(game_path, disk_path, ctx);
        }
    } while (FindNextFileA(h, &found));

    FindClose(h);
}

/* Calls `cb` for every file under <exe folder>\mods with its game path
 * ("\kain2\area\...") and its path on disk. */
void port_scan_mods_folder(port_mod_file_cb cb, void *ctx)
{
    char exe[MAX_PATH];
    char folder[MAX_PATH];

    port_exe_folder(exe, sizeof(exe));
    snprintf(folder, sizeof(folder), "%s\\mods", exe);

    skip_mod_folders = 1;
    scan(folder, "", cb, ctx);
    skip_mod_folders = 0;
}

/* The same for one mod folder of it, mods\<name> (mod_folders.h). */
void port_scan_mod_folder(const char *name, port_mod_file_cb cb, void *ctx)
{
    char exe[MAX_PATH];
    char folder[MAX_PATH];

    port_exe_folder(exe, sizeof(exe));
    snprintf(folder, sizeof(folder), "%s\\mods\\%s", exe, name);

    scan(folder, "", cb, ctx);
}

/* The same for another folder laid out like it (a prototype's own mods:
 * proto\<date>\mods, port/src/mod_files.c). */
void port_scan_folder(const char *folder, port_mod_file_cb cb, void *ctx)
{
    scan(folder, "", cb, ctx);
}
