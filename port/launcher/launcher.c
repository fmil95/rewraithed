/*
 * Rewraithed launcher: edits rewraithed.ini (shared with the game, see
 * port/shared/settings.c) and starts rewraithed.exe from the same folder.
 *
 * Left panel: video options. Right panel: game options and the Controls
 * window. Yes/No options are pairs of check squares of which one is set.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>

#include "settings.h"
#include "areas.h"
#include "controls.h"

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

enum
{
    ID_DISPLAY = 100, ID_RESOLUTION, ID_SCALING, ID_RENDERER,
    ID_VSYNC_YES, ID_VSYNC_NO, ID_PILLAR_YES, ID_PILLAR_NO,
    ID_FILTERING, ID_AA, ID_AREA,
    ID_SKIP, ID_DITHER, ID_PRELOAD_YES, ID_PRELOAD_NO, ID_DEBUG_YES, ID_DEBUG_NO,
    ID_LOG_YES, ID_LOG_NO, ID_FPS, ID_CONTROLS,
    ID_PLAY, ID_SAVE, ID_EXIT,
};

typedef struct { int w, h; } Resolution;

/* 4:3, 16:9, 16:10 and 5:4 sizes from 320x240 up to 1920x1080 */
static const Resolution resolutions[] = {
    { 320, 240 }, { 400, 300 }, { 480, 360 }, { 512, 384 }, { 640, 360 }, { 640, 400 }, { 640, 480 },
    { 720, 480 }, { 800, 450 }, { 800, 480 }, { 800, 500 }, { 800, 600 }, { 854, 480 }, { 960, 540 },
    { 960, 720 }, { 1024, 576 }, { 1024, 600 }, { 1024, 640 }, { 1024, 768 }, { 1152, 648 }, { 1152, 720 },
    { 1152, 864 }, { 1280, 720 }, { 1280, 768 }, { 1280, 800 }, { 1280, 960 }, { 1280, 1024 }, { 1360, 768 },
    { 1366, 768 }, { 1400, 1050 }, { 1440, 900 }, { 1440, 1080 }, { 1600, 900 }, { 1680, 1050 }, { 1920, 1080 },
};

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static const int aa_values[] = { 0, 2, 4, 8 };
static const int fps_values[] = { 0, 30, 60, 120, 240 };

static PortSettings settings;
static char ini_path[MAX_PATH];
static HFONT ui_font;
static int dpi = 96;
static HWND main_window;

#define S(v) MulDiv((v), dpi, 96)

static HWND control(HWND parent, const wchar_t *cls, const wchar_t *text, DWORD style, int x, int y, int w, int h, int id)
{
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h),
        parent, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);

    SendMessageW(c, WM_SETFONT, (WPARAM)ui_font, TRUE);

    return c;
}

static void label(HWND parent, const wchar_t *text, int x, int y, int w)
{
    control(parent, L"STATIC", text, SS_CENTERIMAGE, x, y, w, 24, 0);
}

static HWND combo(HWND parent, int x, int y, int w, int id)
{
    return control(parent, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, x, y, w, 320, id);
}

static void combo_add(int id, const wchar_t *text)
{
    SendDlgItemMessageW(main_window, id, CB_ADDSTRING, 0, (LPARAM)text);
}

static void combo_set(int id, int index)
{
    SendDlgItemMessageW(main_window, id, CB_SETCURSEL, index, 0);
}

static int combo_get(int id)
{
    return (int)SendDlgItemMessageW(main_window, id, CB_GETCURSEL, 0, 0);
}

/* a Yes/No pair of check squares */
static void yes_no(HWND parent, int x, int y, int idYes)
{
    control(parent, L"BUTTON", L"Yes", BS_CHECKBOX | WS_TABSTOP, x, y, 56, 24, idYes);
    control(parent, L"BUTTON", L"No", BS_CHECKBOX | WS_TABSTOP, x + 64, y, 56, 24, idYes + 1);
}

static void yes_no_set(int idYes, int value)
{
    CheckDlgButton(main_window, idYes, value ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(main_window, idYes + 1, value ? BST_UNCHECKED : BST_CHECKED);
}

static int yes_no_get(int idYes)
{
    return IsDlgButtonChecked(main_window, idYes) == BST_CHECKED;
}

static void build_ui(HWND hwnd)
{
    int y;
    int i;

    /* --- left: video --- */
    control(hwnd, L"BUTTON", L"Video", BS_GROUPBOX, 12, 8, 300, 362, 0);

    y = 32;
    label(hwnd, L"Display", 24, y, 96);
    combo(hwnd, 124, y, 176, ID_DISPLAY);
    y += 36;
    label(hwnd, L"Resolution", 24, y, 96);
    combo(hwnd, 124, y, 176, ID_RESOLUTION);
    y += 36;
    label(hwnd, L"Scaling", 24, y, 96);
    combo(hwnd, 124, y, 176, ID_SCALING);
    y += 36;
    label(hwnd, L"Renderer", 24, y, 96);
    combo(hwnd, 124, y, 176, ID_RENDERER);
    y += 36;
    label(hwnd, L"VSync", 24, y, 96);
    yes_no(hwnd, 124, y, ID_VSYNC_YES);
    y += 36;
    label(hwnd, L"Pillarboxing", 24, y, 96);
    yes_no(hwnd, 124, y, ID_PILLAR_YES);
    y += 36;
    label(hwnd, L"Filtering", 24, y, 96);
    combo(hwnd, 124, y, 176, ID_FILTERING);
    y += 36;
    label(hwnd, L"Antialiasing", 24, y, 96);
    combo(hwnd, 124, y, 176, ID_AA);
    y += 36;
    label(hwnd, L"Area", 24, y, 96);
    combo(hwnd, 124, y, 176, ID_AREA);

    /* --- right: game --- */
    control(hwnd, L"BUTTON", L"Game", BS_GROUPBOX, 324, 8, 316, 362, 0);

    y = 32;
    label(hwnd, L"Skip Intros", 336, y, 120);
    combo(hwnd, 460, y, 168, ID_SKIP);
    y += 36;
    label(hwnd, L"Dithering", 336, y, 120);
    combo(hwnd, 460, y, 168, ID_DITHER);
    y += 36;
    label(hwnd, L"Preload Chunks", 336, y, 120);
    yes_no(hwnd, 460, y, ID_PRELOAD_YES);
    y += 36;
    label(hwnd, L"Enable Debug Menu", 336, y, 120);
    yes_no(hwnd, 460, y, ID_DEBUG_YES);
    y += 26;
    control(hwnd, L"STATIC", L"In game, press Square and Circle at the same time (X and V on the keyboard by default).",
        0, 336, y, 292, 32, 0);
    y += 40;
    label(hwnd, L"Enable Logging", 336, y, 120);
    yes_no(hwnd, 460, y, ID_LOG_YES);
    y += 36;
    label(hwnd, L"FPS Limit", 336, y, 120);
    combo(hwnd, 460, y, 168, ID_FPS);
    y += 36;
    label(hwnd, L"Controls", 336, y, 120);
    control(hwnd, L"BUTTON", L"Keyboard...", BS_PUSHBUTTON | WS_TABSTOP, 460, y, 168, 26, ID_CONTROLS);

    /* --- bottom --- */
    control(hwnd, L"BUTTON", L"Play", BS_DEFPUSHBUTTON | WS_TABSTOP, 372, 382, 84, 30, ID_PLAY);
    control(hwnd, L"BUTTON", L"Save", BS_PUSHBUTTON | WS_TABSTOP, 464, 382, 84, 30, ID_SAVE);
    control(hwnd, L"BUTTON", L"Exit", BS_PUSHBUTTON | WS_TABSTOP, 556, 382, 84, 30, ID_EXIT);

    /* --- lists --- */
    combo_add(ID_DISPLAY, L"Windowed");
    combo_add(ID_DISPLAY, L"Fullscreen");
    combo_add(ID_DISPLAY, L"Borderless");

    for (i = COUNT(resolutions) - 1; i >= 0; i--)
    {
        wchar_t text[32];

        swprintf(text, 32, L"%d x %d", resolutions[i].w, resolutions[i].h);
        combo_add(ID_RESOLUTION, text);
    }

    combo_add(ID_SCALING, L"Nearest");
    combo_add(ID_SCALING, L"Bilinear");

    combo_add(ID_RENDERER, L"OpenGL 3.3");

    combo_add(ID_FILTERING, L"Off");
    combo_add(ID_FILTERING, L"Bilinear");

    combo_add(ID_AA, L"Off");
    combo_add(ID_AA, L"2x");
    combo_add(ID_AA, L"4x");
    combo_add(ID_AA, L"8x");

    combo_add(ID_AREA, L"Default (from the disc)");

    for (i = 0; i < COUNT(launcher_areas); i++)
    {
        wchar_t text[64];

        MultiByteToWideChar(CP_UTF8, 0, launcher_areas[i].label, -1, text, 64);
        combo_add(ID_AREA, text);
    }

    combo_add(ID_SKIP, L"Don't Skip");
    combo_add(ID_SKIP, L"Skip to Menu");
    combo_add(ID_SKIP, L"Skip to Game");

    combo_add(ID_DITHER, L"On");
    combo_add(ID_DITHER, L"Off");

    combo_add(ID_FPS, L"0 (unlimited)");
    combo_add(ID_FPS, L"30");
    combo_add(ID_FPS, L"60");
    combo_add(ID_FPS, L"120");
    combo_add(ID_FPS, L"240");
}

static void show_settings(void)
{
    int i;
    int sel;

    combo_set(ID_DISPLAY, settings.display);

    sel = -1;

    for (i = 0; i < COUNT(resolutions); i++)
    {
        if ((resolutions[i].w == settings.width) && (resolutions[i].h == settings.height))
        {
            sel = COUNT(resolutions) - 1 - i;
        }
    }

    if (sel < 0)
    {
        /* a size from the file that is not in the list */
        wchar_t text[32];

        swprintf(text, 32, L"%d x %d", settings.width, settings.height);
        sel = (int)SendDlgItemMessageW(main_window, ID_RESOLUTION, CB_ADDSTRING, 0, (LPARAM)text);
    }

    combo_set(ID_RESOLUTION, sel);
    combo_set(ID_SCALING, settings.scaleLinear ? 1 : 0);
    combo_set(ID_RENDERER, 0);
    yes_no_set(ID_VSYNC_YES, settings.vsync);
    yes_no_set(ID_PILLAR_YES, settings.pillarbox);
    combo_set(ID_FILTERING, settings.filtering ? 1 : 0);

    for (i = 0; i < COUNT(aa_values); i++)
    {
        if (aa_values[i] == settings.antialiasing)
        {
            combo_set(ID_AA, i);
        }
    }

    sel = 0;

    for (i = 0; i < COUNT(launcher_areas); i++)
    {
        if (_stricmp(launcher_areas[i].unit, settings.area) == 0)
        {
            sel = i + 1;
            break;
        }
    }

    combo_set(ID_AREA, sel);
    combo_set(ID_SKIP, settings.skipIntros);
    combo_set(ID_DITHER, settings.dithering ? 0 : 1);
    yes_no_set(ID_PRELOAD_YES, settings.preloadChunks);
    yes_no_set(ID_DEBUG_YES, settings.debugMenu);
    yes_no_set(ID_LOG_YES, settings.logging);

    sel = 1;

    for (i = 0; i < COUNT(fps_values); i++)
    {
        if (fps_values[i] == settings.fpsLimit)
        {
            sel = i;
        }
    }

    combo_set(ID_FPS, sel);
}

static void read_settings(void)
{
    int sel;
    wchar_t text[32];

    settings.display = combo_get(ID_DISPLAY);

    sel = combo_get(ID_RESOLUTION);
    SendDlgItemMessageW(main_window, ID_RESOLUTION, CB_GETLBTEXT, sel, (LPARAM)text);
    swscanf(text, L"%d x %d", &settings.width, &settings.height);

    settings.scaleLinear = combo_get(ID_SCALING) == 1;
    settings.vsync = yes_no_get(ID_VSYNC_YES);
    settings.pillarbox = yes_no_get(ID_PILLAR_YES);
    settings.filtering = combo_get(ID_FILTERING) == 1;
    settings.antialiasing = aa_values[combo_get(ID_AA)];

    sel = combo_get(ID_AREA);
    snprintf(settings.area, sizeof(settings.area), "%s", (sel > 0) ? launcher_areas[sel - 1].unit : "");

    settings.skipIntros = combo_get(ID_SKIP);
    settings.dithering = combo_get(ID_DITHER) == 0;
    settings.preloadChunks = yes_no_get(ID_PRELOAD_YES);
    settings.debugMenu = yes_no_get(ID_DEBUG_YES);
    settings.logging = yes_no_get(ID_LOG_YES);
    settings.fpsLimit = fps_values[combo_get(ID_FPS)];
}

static int save(void)
{
    read_settings();

    if (!port_settings_save(&settings, ini_path))
    {
        MessageBoxW(main_window, L"Could not write rewraithed.ini next to the launcher.", L"Rewraithed", MB_ICONERROR);
        return 0;
    }

    return 1;
}

static int play(void)
{
    wchar_t exe[MAX_PATH];
    wchar_t dir[MAX_PATH];
    wchar_t *slash;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;

    if (!save())
    {
        return 0;
    }

    GetModuleFileNameW(NULL, dir, MAX_PATH);
    slash = wcsrchr(dir, L'\\');

    if (slash != NULL)
    {
        *slash = L'\0';
    }

    swprintf(exe, MAX_PATH, L"%s\\rewraithed.exe", dir);

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);

    /* no console window: with logging on, output goes to rewraithed.log */
    if (!CreateProcessW(exe, NULL, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, dir, &si, &pi))
    {
        MessageBoxW(main_window, L"Could not start rewraithed.exe. It must be in the same folder as the launcher.",
            L"Rewraithed", MB_ICONERROR);
        return 0;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return 1;
}

static LRESULT CALLBACK main_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_COMMAND:
    {
        int id = LOWORD(wp);

        switch (id)
        {
        case ID_VSYNC_YES: case ID_VSYNC_NO:
        case ID_PILLAR_YES: case ID_PILLAR_NO:
        case ID_PRELOAD_YES: case ID_PRELOAD_NO:
        case ID_DEBUG_YES: case ID_DEBUG_NO:
        case ID_LOG_YES: case ID_LOG_NO:
        {
            /* Yes ids are even offsets from ID_VSYNC_YES */
            int yesId = ((id - ID_VSYNC_YES) % 2 == 0) ? id : id - 1;

            if ((id >= ID_PRELOAD_YES) && (id <= ID_LOG_NO))
            {
                yesId = ((id - ID_PRELOAD_YES) % 2 == 0) ? id : id - 1;
            }

            yes_no_set(yesId, id == yesId);
            return 0;
        }
        case ID_CONTROLS:
            controls_dialog(hwnd, &settings, ui_font, dpi);
            return 0;
        case ID_PLAY:
            if (play())
            {
                DestroyWindow(hwnd);
            }
            return 0;
        case ID_SAVE:
            save();
            return 0;
        case ID_EXIT:
            DestroyWindow(hwnd);
            return 0;
        }

        break;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show)
{
    WNDCLASSW wc;
    NONCLIENTMETRICSW ncm;
    RECT r;
    MSG msg;
    HDC screen;

    (void)prev;
    (void)cmd;

    SetProcessDPIAware();
    InitCommonControls();

    screen = GetDC(NULL);
    dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(NULL, screen);

    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    ui_font = CreateFontIndirectW(&ncm.lfMessageFont);

    port_settings_defaults(&settings);
    port_settings_path(ini_path, sizeof(ini_path));
    port_settings_load(&settings, ini_path);

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = main_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"RewraithedLauncher";
    RegisterClassW(&wc);

    r.left = 0;
    r.top = 0;
    r.right = S(652);
    r.bottom = S(424);
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);

    main_window = CreateWindowW(L"RewraithedLauncher", L"Rewraithed Launcher",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT,
        r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);

    build_ui(main_window);
    show_settings();

    ShowWindow(main_window, show);
    UpdateWindow(main_window);

    while (GetMessageW(&msg, NULL, 0, 0))
    {
        if (!IsDialogMessageW(main_window, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    return 0;
}
