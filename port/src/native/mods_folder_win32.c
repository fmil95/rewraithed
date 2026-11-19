/*
 * The mods folder: "mods" next to rewraithed.exe. Its tree mirrors the
 * game's file names, e.g. mods\kain2\area\under\bin\under1.drm stands in for
 * \kain2\area\under\bin\under1.drm in BIGFILE.DAT (port/src/mod_files.c).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

void port_exe_folder(char *out, size_t size);

typedef void (*port_mod_file_cb)(const char *game_path, const char *disk_path, void *ctx);

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

        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
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

    scan(folder, "", cb, ctx);
}
