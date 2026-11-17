/*
 * The designated disc folder: "disc" next to rewraithed.exe.
 *
 * Whatever the prototype's files are called, the first .cue sheet found
 * there is used, else the first raw image (.bin / .img).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

/* the folder rewraithed.exe is in, without a trailing separator */
void port_exe_folder(char *out, size_t size)
{
    char exe[MAX_PATH];
    char *slash;
    DWORD n = GetModuleFileNameA(NULL, exe, sizeof(exe));

    if ((n == 0) || (n >= sizeof(exe)))
    {
        snprintf(out, size, ".");
        return;
    }

    slash = strrchr(exe, '\\');

    if (slash != NULL)
    {
        *slash = '\0';
    }

    snprintf(out, size, "%s", exe);
}

/* "<exe folder>\disc" */
void port_disc_folder(char *out, size_t size)
{
    char exe[MAX_PATH];

    port_exe_folder(exe, sizeof(exe));
    snprintf(out, size, "%s\\disc", exe);
}

static int find_first(const char *folder, const char *pattern, char *out, size_t size)
{
    char query[MAX_PATH];
    WIN32_FIND_DATAA found;
    HANDLE h;

    snprintf(query, sizeof(query), "%s\\%s", folder, pattern);

    h = FindFirstFileA(query, &found);

    if (h == INVALID_HANDLE_VALUE)
    {
        return 0;
    }

    do
    {
        if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            snprintf(out, size, "%s\\%s", folder, found.cFileName);
            FindClose(h);
            return 1;
        }
    } while (FindNextFileA(h, &found));

    FindClose(h);

    return 0;
}

/* Path of the disc in the designated folder, or 0 if there is none. */
int port_find_disc(char *out, size_t size)
{
    char folder[MAX_PATH];

    port_disc_folder(folder, sizeof(folder));

    return find_first(folder, "*.cue", out, size) ||
        find_first(folder, "*.bin", out, size) ||
        find_first(folder, "*.img", out, size);
}
