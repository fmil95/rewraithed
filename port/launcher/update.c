/*
 * Update button. The latest release on GitHub (fmil95/rewraithed) is read
 * from its API; when its version is newer than this launcher's, its zip
 * (rewraithedv<version>_win32.zip, or the _debug-win32 one for a debug
 * build) is downloaded, unpacked with Windows' own tar.exe, and its files
 * replace those next to the launcher. Files in use (the launcher itself,
 * the game if running) can't be overwritten but can be renamed, so each
 * replaced file is first renamed to <name>.old, which the next start of the
 * launcher deletes (update_cleanup). Then the new launcher is started.
 *
 * At start, update_check_async looks in the background, so the button can
 * show that an update is out; nothing is downloaded without asking.
 */
#include "update.h"

#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define RELEASES_HOST   L"api.github.com"
#define RELEASES_PATH   L"/repos/fmil95/rewraithed/releases/latest"
#define USER_AGENT      L"Rewraithed-Launcher/" REWRAITHED_VERSION

#ifdef _DEBUG
#define ZIP_SUFFIX      "_debug-win32.zip"
#define DEBUG_BUILD     1
#else
#define ZIP_SUFFIX      "_win32.zip"
#define DEBUG_BUILD     0
#endif

typedef struct
{
    int newer;                  /* 1 newer, 0 not, -1 the check failed */
    wchar_t version[32];        /* of the latest release */
    wchar_t zipUrl[512];
} Latest;

static Latest found;            /* the background check's */
static volatile LONG foundReady;

/* --- HTTP ----------------------------------------------------------------- */

/* the body of an https URL (malloc'd, 0-terminated), or NULL */
static char *http_get(const wchar_t *url, DWORD *size)
{
    URL_COMPONENTSW uc;
    wchar_t host[256];
    wchar_t path[1024];
    HINTERNET session = NULL;
    HINTERNET connect = NULL;
    HINTERNET request = NULL;
    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    char *body = NULL;
    DWORD length = 0;

    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 1024;

    if (!WinHttpCrackUrl(url, 0, 0, &uc))
    {
        return NULL;
    }

    session = WinHttpOpen(USER_AGENT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    connect = (session != NULL) ? WinHttpConnect(session, host, uc.nPort, 0) : NULL;
    request = (connect != NULL) ? WinHttpOpenRequest(connect, L"GET", path, NULL, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0) : NULL;

    if ((request != NULL) &&
        WinHttpSendRequest(request, L"Accept: application/vnd.github+json\r\n", (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, NULL) &&
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
            &status, &statusSize, WINHTTP_NO_HEADER_INDEX) &&
        (status == 200))
    {
        for (;;)
        {
            DWORD available = 0;
            DWORD read = 0;
            char *grown;

            if (!WinHttpQueryDataAvailable(request, &available) || (available == 0))
            {
                break;
            }

            grown = (char *)realloc(body, length + available + 1);

            if (grown == NULL)
            {
                free(body);
                body = NULL;
                break;
            }

            body = grown;

            if (!WinHttpReadData(request, body + length, available, &read))
            {
                free(body);
                body = NULL;
                break;
            }

            length += read;
            body[length] = '\0';
        }
    }

    if (request != NULL) WinHttpCloseHandle(request);
    if (connect != NULL) WinHttpCloseHandle(connect);
    if (session != NULL) WinHttpCloseHandle(session);

    if ((body != NULL) && (size != NULL))
    {
        *size = length;
    }

    return body;
}

/* --- the latest release ---------------------------------------------------- */

/* "0.99j" against "0.99k", "1.0": runs of digits by value, other characters
 * as they are */
static int version_compare(const wchar_t *a, const wchar_t *b)
{
    while (*a || *b)
    {
        if (iswdigit(*a) && iswdigit(*b))
        {
            long na = wcstol(a, (wchar_t **)&a, 10);
            long nb = wcstol(b, (wchar_t **)&b, 10);

            if (na != nb)
            {
                return (na < nb) ? -1 : 1;
            }
        }
        else
        {
            if (*a != *b)
            {
                return (*a < *b) ? -1 : 1;
            }

            a++;
            b++;
        }
    }

    return 0;
}

/* the string value after "key": in json, from position from (to a wide string) */
static const char *json_string(const char *from, const char *key, wchar_t *out, int outLen)
{
    char pattern[64];
    const char *p;
    const char *end;
    char value[1024];
    size_t n;

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    p = strstr(from, pattern);

    if (p == NULL)
    {
        return NULL;
    }

    p = strchr(p + strlen(pattern), '"');

    if (p == NULL)
    {
        return NULL;
    }

    p++;
    end = strchr(p, '"');

    if (end == NULL)
    {
        return NULL;
    }

    n = (size_t)(end - p);

    if (n >= sizeof(value))
    {
        n = sizeof(value) - 1;
    }

    memcpy(value, p, n);
    value[n] = '\0';
    MultiByteToWideChar(CP_UTF8, 0, value, -1, out, outLen);

    return end + 1;
}

static void check_latest(Latest *latest)
{
    char *json = http_get(L"https://" RELEASES_HOST RELEASES_PATH, NULL);
    wchar_t tag[32];
    const char *p;

    latest->newer = -1;
    latest->version[0] = L'\0';
    latest->zipUrl[0] = L'\0';

    if ((json == NULL) || (json_string(json, "tag_name", tag, 32) == NULL))
    {
        free(json);
        return;
    }

    wcsncpy(latest->version, (tag[0] == L'v') ? tag + 1 : tag, 31);
    latest->version[31] = L'\0';

    /* the zip for this kind of build */
    for (p = json; (p = json_string(p, "browser_download_url", latest->zipUrl, 512)) != NULL;)
    {
        size_t n = wcslen(latest->zipUrl);
        wchar_t suffix[32];

        MultiByteToWideChar(CP_UTF8, 0, ZIP_SUFFIX, -1, suffix, 32);

        /* "_win32.zip" also ends the debug zip's name: that one has "_debug" */
        if ((n >= wcslen(suffix)) && (_wcsicmp(latest->zipUrl + n - wcslen(suffix), suffix) == 0) &&
            (DEBUG_BUILD || (wcsstr(latest->zipUrl, L"_debug") == NULL)))
        {
            break;
        }

        latest->zipUrl[0] = L'\0';
    }

    free(json);

    latest->newer = (latest->zipUrl[0] != L'\0') && (version_compare(latest->version, REWRAITHED_VERSION) > 0);
}

static DWORD WINAPI check_thread(LPVOID param)
{
    check_latest(&found);
    InterlockedExchange(&foundReady, 1);
    PostMessageW((HWND)param, WM_APP_UPDATE, found.newer == 1, 0);

    /* right after an update, the old launcher may still have been running
     * when this one started and cleaned up: again, once it has exited */
    Sleep(3000);
    update_cleanup();

    return 0;
}

void update_check_async(HWND hwnd)
{
    HANDLE t = CreateThread(NULL, 0, check_thread, hwnd, 0, NULL);

    if (t != NULL)
    {
        CloseHandle(t);
    }
}

/* --- installing ------------------------------------------------------------ */

static void exe_folder(wchar_t *out)
{
    wchar_t *slash;

    GetModuleFileNameW(NULL, out, MAX_PATH);
    slash = wcsrchr(out, L'\\');

    if (slash != NULL)
    {
        *slash = L'\0';
    }
}

void update_cleanup(void)
{
    wchar_t dir[MAX_PATH];
    wchar_t query[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;

    exe_folder(dir);
    swprintf(query, MAX_PATH, L"%s\\*.old", dir);
    h = FindFirstFileW(query, &fd);

    if (h == INVALID_HANDLE_VALUE)
    {
        return;
    }

    do
    {
        wchar_t path[MAX_PATH];

        swprintf(path, MAX_PATH, L"%s\\%s", dir, fd.cFileName);
        DeleteFileW(path);
    } while (FindNextFileW(h, &fd));

    FindClose(h);
}

static int run_wait(wchar_t *cmdline)
{
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    DWORD code = 1;

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);

    if (!CreateProcessW(NULL, cmdline, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
    {
        return 0;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return code == 0;
}

/* deletes a folder and what is in it (the unpacked update) */
static void remove_tree(const wchar_t *dir)
{
    wchar_t query[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;

    swprintf(query, MAX_PATH, L"%s\\*", dir);
    h = FindFirstFileW(query, &fd);

    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            wchar_t path[MAX_PATH];

            if ((wcscmp(fd.cFileName, L".") == 0) || (wcscmp(fd.cFileName, L"..") == 0))
            {
                continue;
            }

            swprintf(path, MAX_PATH, L"%s\\%s", dir, fd.cFileName);

            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                remove_tree(path);
            }
            else
            {
                DeleteFileW(path);
            }
        } while (FindNextFileW(h, &fd));

        FindClose(h);
    }

    RemoveDirectoryW(dir);
}

/* the unpacked files of from (and its folders) into to; 0 and a message
 * in error if one could not be put in place */
static int install_tree(const wchar_t *from, const wchar_t *to, wchar_t *error, int errorLen)
{
    wchar_t query[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int ok = 1;

    swprintf(query, MAX_PATH, L"%s\\*", from);
    h = FindFirstFileW(query, &fd);

    if (h == INVALID_HANDLE_VALUE)
    {
        return 1;
    }

    do
    {
        wchar_t src[MAX_PATH];
        wchar_t dst[MAX_PATH];
        wchar_t old[MAX_PATH];

        if ((wcscmp(fd.cFileName, L".") == 0) || (wcscmp(fd.cFileName, L"..") == 0))
        {
            continue;
        }

        swprintf(src, MAX_PATH, L"%s\\%s", from, fd.cFileName);
        swprintf(dst, MAX_PATH, L"%s\\%s", to, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            CreateDirectoryW(dst, NULL);
            ok = install_tree(src, dst, error, errorLen) && ok;
            continue;
        }

        swprintf(old, MAX_PATH, L"%s.old", dst);

        /* a file in use can be renamed, not overwritten */
        if (GetFileAttributesW(dst) != INVALID_FILE_ATTRIBUTES)
        {
            DeleteFileW(old);
            MoveFileExW(dst, old, MOVEFILE_REPLACE_EXISTING);
        }

        if (!MoveFileExW(src, dst, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED))
        {
            /* put the old one back */
            MoveFileExW(old, dst, MOVEFILE_REPLACE_EXISTING);
            swprintf(error, errorLen, L"Could not replace %s.", fd.cFileName);
            ok = 0;
        }
    } while (FindNextFileW(h, &fd));

    FindClose(h);

    return ok;
}

static int download_and_install(HWND owner, const Latest *latest)
{
    wchar_t temp[MAX_PATH];
    wchar_t work[MAX_PATH];
    wchar_t zip[MAX_PATH];
    wchar_t unpacked[MAX_PATH];
    wchar_t dir[MAX_PATH];
    wchar_t cmd[2 * MAX_PATH + 64];
    wchar_t sys[MAX_PATH];
    wchar_t error[256] = L"";
    char *data;
    DWORD size = 0;
    FILE *f;

    data = http_get(latest->zipUrl, &size);

    if ((data == NULL) || (size == 0))
    {
        free(data);
        MessageBoxW(owner, L"Could not download the update.", L"Update", MB_ICONERROR);
        return 0;
    }

    GetTempPathW(MAX_PATH, temp);
    swprintf(work, MAX_PATH, L"%srewraithed-update", temp);
    remove_tree(work);
    CreateDirectoryW(work, NULL);

    swprintf(zip, MAX_PATH, L"%s\\update.zip", work);
    swprintf(unpacked, MAX_PATH, L"%s\\files", work);
    CreateDirectoryW(unpacked, NULL);

    f = _wfopen(zip, L"wb");

    if ((f == NULL) || (fwrite(data, 1, size, f) != size))
    {
        if (f != NULL) fclose(f);
        free(data);
        MessageBoxW(owner, L"Could not save the update.", L"Update", MB_ICONERROR);
        return 0;
    }

    fclose(f);
    free(data);

    /* Windows 10 and later have bsdtar, which reads zips */
    GetSystemDirectoryW(sys, MAX_PATH);
    swprintf(cmd, sizeof(cmd) / sizeof(cmd[0]), L"\"%s\\tar.exe\" -xf \"%s\" -C \"%s\"", sys, zip, unpacked);

    if (!run_wait(cmd))
    {
        remove_tree(work);
        MessageBoxW(owner, L"Could not unpack the update.", L"Update", MB_ICONERROR);
        return 0;
    }

    exe_folder(dir);

    if (!install_tree(unpacked, dir, error, 256))
    {
        wchar_t text[400];

        remove_tree(work);
        swprintf(text, 400, L"%s Close the game if it is running, and try again.", error);
        MessageBoxW(owner, text, L"Update", MB_ICONERROR);
        return 0;
    }

    remove_tree(work);

    /* the new launcher */
    {
        wchar_t exe[MAX_PATH];
        STARTUPINFOW si;
        PROCESS_INFORMATION pi;

        GetModuleFileNameW(NULL, exe, MAX_PATH);
        memset(&si, 0, sizeof(si));
        si.cb = sizeof(si);

        if (CreateProcessW(exe, NULL, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi))
        {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }

    return 1;
}

int update_run(HWND owner)
{
    Latest latest;
    wchar_t text[300];
    HCURSOR cursor = SetCursor(LoadCursor(NULL, IDC_WAIT));

    if (foundReady && (found.newer == 1))
    {
        latest = found;
    }
    else
    {
        check_latest(&latest);
    }

    SetCursor(cursor);

    if (latest.newer < 0)
    {
        MessageBoxW(owner, L"Could not reach GitHub to look for updates.", L"Update", MB_ICONWARNING);
        return 0;
    }

    if (latest.newer == 0)
    {
        swprintf(text, 300, L"Rewraithed %s is the latest version.", REWRAITHED_VERSION);
        MessageBoxW(owner, text, L"Update", MB_ICONINFORMATION);
        return 0;
    }

    swprintf(text, 300, L"Rewraithed %s is out (this is %s). Download and install it now? The launcher restarts when done.",
        latest.version, REWRAITHED_VERSION);

    if (MessageBoxW(owner, text, L"Update", MB_YESNO | MB_ICONQUESTION) != IDYES)
    {
        return 0;
    }

    cursor = SetCursor(LoadCursor(NULL, IDC_WAIT));

    if (!download_and_install(owner, &latest))
    {
        SetCursor(cursor);
        return 0;
    }

    return 1;
}
