/*
 * Mod folders (mod_folders.h).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mod_folders.h"

static void exe_folder(char *out, size_t size)
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

void mod_folders_root(char *out, size_t size)
{
    char exe[MAX_PATH];

    exe_folder(exe, sizeof(exe));
    snprintf(out, size, "%s\\mods", exe);
}

static char *trim(char *s)
{
    char *end;

    while (isspace((unsigned char)*s))
    {
        s++;
    }

    end = s + strlen(s);

    while ((end > s) && isspace((unsigned char)end[-1]))
    {
        *--end = '\0';
    }

    return s;
}

static void read_mod_ini(const char *path, FolderMod *mod)
{
    char line[1024];
    FILE *f = fopen(path, "r");

    if (f == NULL)
    {
        return;
    }

    while (fgets(line, sizeof(line), f) != NULL)
    {
        char *p = trim(line);
        char *eq = strchr(p, '=');
        char *key;
        char *value;

        /* a UTF-8 file's byte-order mark */
        if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
        {
            p += 3;
        }

        if ((eq == NULL) || (*p == ';') || (*p == '#') || (*p == '['))
        {
            continue;
        }

        *eq = '\0';
        key = trim(p);
        value = trim(eq + 1);

        if (_stricmp(key, "name") == 0)                 snprintf(mod->name, sizeof(mod->name), "%s", value);
        else if (_stricmp(key, "author") == 0)          snprintf(mod->author, sizeof(mod->author), "%s", value);
        else if (_stricmp(key, "description") == 0)     snprintf(mod->description, sizeof(mod->description), "%s", value);
        else if (_stricmp(key, "requires") == 0)        snprintf(mod->requires, sizeof(mod->requires), "%s", value);
        else if (_stricmp(key, "missing") == 0)         snprintf(mod->missing, sizeof(mod->missing), "%s", value);
    }

    fclose(f);
}

static int compare_mods(const void *a, const void *b)
{
    return _stricmp(((const FolderMod *)a)->folder, ((const FolderMod *)b)->folder);
}

int mod_folders_list(FolderMod *out, int max)
{
    char root[MAX_PATH];
    char query[MAX_PATH];
    WIN32_FIND_DATAA found;
    HANDLE h;
    int count = 0;

    mod_folders_root(root, sizeof(root));
    snprintf(query, sizeof(query), "%s\\*", root);
    h = FindFirstFileA(query, &found);

    if (h == INVALID_HANDLE_VALUE)
    {
        return 0;
    }

    do
    {
        char ini[MAX_PATH];

        if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (found.cFileName[0] == '.') ||
            (strlen(found.cFileName) >= FOLDER_MOD_NAME) || (count >= max))
        {
            continue;
        }

        snprintf(ini, sizeof(ini), "%s\\%s\\mod.ini", root, found.cFileName);

        if (GetFileAttributesA(ini) == INVALID_FILE_ATTRIBUTES)
        {
            continue;
        }

        memset(&out[count], 0, sizeof(out[count]));
        snprintf(out[count].folder, sizeof(out[count].folder), "%s", found.cFileName);
        read_mod_ini(ini, &out[count]);

        if (out[count].name[0] == '\0')
        {
            snprintf(out[count].name, sizeof(out[count].name), "%s", found.cFileName);
        }

        count++;
    } while (FindNextFileA(h, &found));

    FindClose(h);
    qsort(out, count, sizeof(FolderMod), compare_mods);

    return count;
}

int mod_folders_available(const FolderMod *mod, char *problem, size_t problemSize)
{
    char exe[MAX_PATH];
    char pattern[MAX_PATH];
    WIN32_FIND_DATAA found;
    HANDLE h;

    if (mod->requires[0] == '\0')
    {
        return 1;
    }

    exe_folder(exe, sizeof(exe));
    snprintf(pattern, sizeof(pattern), "%s\\%s", exe, mod->requires);
    h = FindFirstFileA(pattern, &found);

    if (h != INVALID_HANDLE_VALUE)
    {
        FindClose(h);
        return 1;
    }

    if ((problem != NULL) && (problemSize > 0))
    {
        if (mod->missing[0] != '\0')
        {
            snprintf(problem, problemSize, "%s\n\n(It needs %s next to the game: %s)", mod->missing, mod->requires, exe);
        }
        else
        {
            snprintf(problem, problemSize, "This mod needs %s next to the game:\n\n%s", mod->requires, exe);
        }
    }

    return 0;
}
