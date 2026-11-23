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
#include "dc_disc.h"
#include "proto_disc.h"
#include "pc_disc.h"
#include "areas.h"
#include "controls.h"
#include "joystick.h"

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

enum
{
    ID_DISPLAY = 100, ID_RESOLUTION, ID_SCALING, ID_RENDERER,
    ID_VSYNC_YES, ID_VSYNC_NO, ID_PILLAR_YES, ID_PILLAR_NO, ID_PGXP_YES, ID_PGXP_NO,
    ID_FILTERING, ID_AA, ID_AREA,
    ID_SKIP, ID_DITHER, ID_PRELOAD_YES, ID_PRELOAD_NO, ID_DEBUG_YES, ID_DEBUG_NO,
    ID_LOG_YES, ID_LOG_NO, ID_DISCORD_YES, ID_DISCORD_NO, ID_DC_YES, ID_DC_NO, ID_PROTO_YES, ID_PROTO_NO, ID_PCMOV_YES, ID_PCMOV_NO, ID_FPS, ID_CONTROLS, ID_JOYSTICK,
    ID_PARAMS, ID_PLAY, ID_SAVE,
};

typedef struct { int w, h; } Resolution;

/* common 4:3, 16:9, 16:10 and 5:4 sizes, largest first */
static const Resolution resolutions[] = {
    { 1920, 1080 }, { 1680, 1050 }, { 1600, 1024 }, { 1600, 900 }, { 1440, 1080 }, { 1440, 900 },
    { 1366, 768 }, { 1360, 768 }, { 1280, 1024 }, { 1280, 960 }, { 1280, 800 }, { 1280, 768 },
    { 1280, 720 }, { 1176, 664 }, { 1152, 864 }, { 1024, 768 }, { 960, 720 }, { 800, 600 },
    { 720, 576 }, { 720, 480 }, { 640, 480 }, { 320, 240 },
};

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* The Resolution list: each size above, once per refresh rate the monitor
 * offers at it (fullscreen sets that rate), or once without a rate when the
 * monitor has no mode of that size (a window can still have it). */
#define MAX_LIST_ENTRIES 256

typedef struct { int w, h, refresh; } ResolutionEntry;

static ResolutionEntry resolution_list[MAX_LIST_ENTRIES];
static int resolution_count;

static void build_resolution_list(void)
{
    static int rates[MAX_LIST_ENTRIES];
    int i;

    resolution_count = 0;

    for (i = 0; i < COUNT(resolutions); i++)
    {
        DEVMODEW mode;
        DWORD m;
        int count = 0;
        int a;
        int b;

        memset(&mode, 0, sizeof(mode));
        mode.dmSize = sizeof(mode);

        for (m = 0; EnumDisplaySettingsW(NULL, m, &mode); m++)
        {
            int rate = (int)mode.dmDisplayFrequency;
            int known = 0;

            if (((int)mode.dmPelsWidth != resolutions[i].w) || ((int)mode.dmPelsHeight != resolutions[i].h) || (rate <= 1))
            {
                continue;
            }

            for (a = 0; a < count; a++)
            {
                known |= (rates[a] == rate);
            }

            if (!known && (count < MAX_LIST_ENTRIES))
            {
                rates[count++] = rate;
            }
        }

        /* highest rate first */
        for (a = 0; a < count; a++)
        {
            for (b = a + 1; b < count; b++)
            {
                if (rates[b] > rates[a])
                {
                    int t = rates[a];

                    rates[a] = rates[b];
                    rates[b] = t;
                }
            }
        }

        if (count == 0)
        {
            rates[count++] = 0;
        }

        for (a = 0; (a < count) && (resolution_count < MAX_LIST_ENTRIES); a++)
        {
            resolution_list[resolution_count].w = resolutions[i].w;
            resolution_list[resolution_count].h = resolutions[i].h;
            resolution_list[resolution_count].refresh = rates[a];
            resolution_count++;
        }
    }
}

static void resolution_text(wchar_t *text, int size, int w, int h, int refresh)
{
    if (refresh > 0)
    {
        swprintf(text, size, L"%d x %d @ %d Hz", w, h, refresh);
    }
    else
    {
        swprintf(text, size, L"%d x %d", w, h);
    }
}

/* the desktop's refresh rate, 0 if unknown */
static int desktop_refresh(void)
{
    DEVMODEW mode;

    memset(&mode, 0, sizeof(mode));
    mode.dmSize = sizeof(mode);

    return EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &mode) ? (int)mode.dmDisplayFrequency : 0;
}

/* the Display list, in its order */
static const int display_order[] = { DISPLAY_FULLSCREEN, DISPLAY_WINDOWED, DISPLAY_BORDERLESS };

static const int aa_values[] = { 0, 2, 4, 8 };
static const int fps_values[] = { 30, 60 };

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

static HWND label(HWND parent, const wchar_t *text, int x, int y, int w)
{
    return control(parent, L"STATIC", text, SS_CENTERIMAGE | SS_NOTIFY, x, y, w, 24, 0);
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

/* a hover tooltip for one control */
static void tooltip(HWND parent, HWND target, const wchar_t *text)
{
    static HWND tip;
    TOOLINFOW ti;

    if (tip == NULL)
    {
        tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, parent, NULL, GetModuleHandleW(NULL), NULL);
        SendMessageW(tip, TTM_SETMAXTIPWIDTH, 0, S(300));
        SendMessageW(tip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 30000);
    }

    memset(&ti, 0, sizeof(ti));
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = parent;
    ti.uId = (UINT_PTR)target;
    ti.lpszText = (wchar_t *)text;
    SendMessageW(tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
}

/* the same tooltip on an option's label and its control(s) (0: none) */
static void option_tip(HWND parent, HWND lbl, int id1, int id2, const wchar_t *text)
{
    tooltip(parent, lbl, text);

    if (id1 != 0)
    {
        tooltip(parent, GetDlgItem(parent, id1), text);
    }

    if (id2 != 0)
    {
        tooltip(parent, GetDlgItem(parent, id2), text);
    }
}

/* a label and a list, or a label and a Yes/No pair, on one row with a tooltip */
static void combo_row(HWND parent, const wchar_t *text, int x, int y, int lw, int cx, int cw, int id, const wchar_t *tip)
{
    HWND lbl = label(parent, text, x, y, lw);

    combo(parent, cx, y, cw, id);
    option_tip(parent, lbl, id, 0, tip);
}

static HWND yes_no_row(HWND parent, const wchar_t *text, int x, int y, int lw, int cx, int idYes, const wchar_t *tip)
{
    HWND lbl = label(parent, text, x, y, lw);

    yes_no(parent, cx, y, idYes);
    option_tip(parent, lbl, idYes, idYes + 1, tip);

    return lbl;
}

/* the width, in 96-dpi units, the Area list needs for its longest name */
static int area_list_width(HWND parent)
{
    HDC dc = GetDC(parent);
    HFONT old = (HFONT)SelectObject(dc, ui_font);
    int widest = 0;
    int i;

    for (i = -1; i < COUNT(launcher_areas); i++)
    {
        wchar_t text[64];
        SIZE size;

        if (i < 0)
        {
            wcscpy(text, L"Default (from the disc)");
        }
        else
        {
            MultiByteToWideChar(CP_UTF8, 0, launcher_areas[i].label, -1, text, 64);
        }

        GetTextExtentPoint32W(dc, text, (int)wcslen(text), &size);

        if (size.cx > widest)
        {
            widest = size.cx;
        }
    }

    SelectObject(dc, old);
    ReleaseDC(parent, dc);

    /* the text, the arrow button and the borders */
    return MulDiv(widest + GetSystemMetrics(SM_CXVSCROLL), 96, dpi) + 14;
}

static void build_ui(HWND hwnd)
{
    HWND lbl;
    int y;
    int i;

    /* top right: the version */
    control(hwnd, L"STATIC", L"Version " REWRAITHED_VERSION, SS_RIGHT | WS_DISABLED, 440, 4, 200, 16, 0);

    /* --- left: video --- */
    control(hwnd, L"BUTTON", L"Video", BS_GROUPBOX, 12, 18, 300, 398, 0);

    y = 42;
    combo_row(hwnd, L"Display", 24, y, 96, 124, 176, ID_DISPLAY,
        L"Fullscreen takes over the screen at the chosen resolution. Windowed opens a window of that size. "
        L"Borderless covers the screen with a window, which is quicker to switch away from.");
    y += 36;
    combo_row(hwnd, L"Resolution", 24, y, 96, 124, 176, ID_RESOLUTION,
        L"The size the game is drawn at, in pixels: the screen mode in fullscreen, or the size of the window. "
        L"Each size the monitor offers is listed once per refresh rate it has at that size; the rate is used in fullscreen. "
        L"Windowed and borderless keep the desktop's own rate.");
    y += 36;
    combo_row(hwnd, L"Scaling", 24, y, 96, 124, 176, ID_SCALING,
        L"How movies and 2D screens are enlarged to the window. Nearest keeps their pixels sharp, Bilinear smooths them.");
    y += 36;
    combo_row(hwnd, L"Renderer", 24, y, 96, 124, 176, ID_RENDERER,
        L"The graphics API the game draws with. OpenGL 3.3 is the only one for now.");
    y += 36;
    yes_no_row(hwnd, L"VSync", 24, y, 96, 124, ID_VSYNC_YES,
        L"Waits for the monitor to finish each refresh before showing a new frame, which stops tearing.");
    y += 36;
    yes_no_row(hwnd, L"Pillarboxing", 24, y, 96, 124, ID_PILLAR_YES,
        L"Keeps the game's original 4:3 shape on wide screens, with black bars at the sides. "
        L"With No, the picture is stretched to fill the window.");
    y += 36;
    combo_row(hwnd, L"Filtering", 24, y, 96, 124, 176, ID_FILTERING,
        L"How 3D textures are drawn. Off keeps the sharp pixels of the PlayStation, Bilinear smooths them.");
    y += 36;
    yes_no_row(hwnd, L"Use PGXP", 24, y, 96, 124, ID_PGXP_YES,
        L"Places 3D geometry with sub-pixel precision instead of snapping it to whole PlayStation pixels, "
        L"which reduces the wobbling of polygons as they move.");
    y += 36;
    combo_row(hwnd, L"Antialiasing", 24, y, 96, 124, 176, ID_AA,
        L"Smooths the jagged edges of polygons. Higher values look smoother but need a faster graphics card.");

    /* --- right: game --- */
    control(hwnd, L"BUTTON", L"Game", BS_GROUPBOX, 324, 18, 316, 398, 0);

    y = 42;
    combo_row(hwnd, L"Skip Intros", 336, y, 120, 460, 168, ID_SKIP,
        L"Skip to Menu leaves out the logos and goes to the main menu. "
        L"Skip to Game also leaves out the menu and starts the game right away.");
    y += 36;
    combo_row(hwnd, L"Dithering", 336, y, 120, 460, 168, ID_DITHER,
        L"The PlayStation's dot pattern that hides color banding. Off gives smoother colors, On looks like the original.");
    y += 36;
    yes_no_row(hwnd, L"Enable Debug Menu", 336, y, 120, 460, ID_DEBUG_YES,
        L"Opens the game's debug menu. In game, press Backspace, or Attack and Feed at the same time.");
    y += 36;
    yes_no_row(hwnd, L"Enable Logging", 336, y, 120, 460, ID_LOG_YES,
        L"Writes what the game does to rewraithed.log next to the game, including the details of a crash, "
        L"and how textures are placed in video memory to vram.log. Useful when reporting problems.");
    y += 36;
    yes_no_row(hwnd, L"Discord Rich Presence", 336, y, 120, 460, ID_DISCORD_YES,
        L"Shows on your Discord profile that you are playing Rewraithed, with the area you are in and the realm. "
        L"Needs the Discord app running on this PC.");
    y += 36;
    combo_row(hwnd, L"FPS Limit", 336, y, 120, 460, 168, ID_FPS,
        L"How many frames a second the game runs at. 30 is the original rate of the game, 60 is smoother.");
    y += 36;
    yes_no_row(hwnd, L"Use PC Movies", 336, y, 120, 460, ID_PCMOV_YES,
        L"Plays the movies of the 1999 PC version instead of the PlayStation ones. "
        L"Needs the .rpl files from your PC disc, or an image of that disc, in the movies folder next to the game.");
    y += 36;
    yes_no_row(hwnd, L"Use DC Models", 336, y, 120, 460, ID_DC_YES,
        L"Draws Raziel and Kain with their more detailed models from the Dreamcast version. "
        L"Needs your own dump of the Dreamcast disc (the .gdi file and its track files) "
        L"in the dreamcast folder next to the game.");
    y += 36;
    yes_no_row(hwnd, L"Use Proto Assets", 336, y, 120, 460, ID_PROTO_YES,
        L"Uses assets from the April 14, 1999 prototype that the July build lacks: the Spirit Glyph's own icon "
        L"(with -uncut). Needs your own image of that prototype's disc in the proto folder next to the game.");

    /* last row of the box, just above the buttons */
    y = 372;
    lbl = label(hwnd, L"Controls", 336, y, 60);
    control(hwnd, L"BUTTON", L"Keyboard and Mouse", BS_PUSHBUTTON | WS_TABSTOP, 404, y, 128, 26, ID_CONTROLS);
    control(hwnd, L"BUTTON", L"Joystick", BS_PUSHBUTTON | WS_TABSTOP, 540, y, 88, 26, ID_JOYSTICK);
    tooltip(hwnd, GetDlgItem(hwnd, ID_JOYSTICK), L"Turn the joystick (a game controller) on or off, and choose its button for each action. The left stick also moves.");
    option_tip(hwnd, lbl, ID_CONTROLS, 0, L"Change the keys for each action, with an alternate key for each (the arrow keys also move), and the mouse buttons for attacking, telekinesis and aiming.");

    /* --- bottom: the Area list as wide as its longest name, then Parameters
     * up to the buttons --- */
    {
        int areaWidth = area_list_width(hwnd);
        int paramsX = 64 + areaWidth + 12;

        combo_row(hwnd, L"Area", 24, 431, 36, 64, areaWidth, ID_AREA,
            L"Starts the game straight in the chosen area. Default starts where the disc does.");

        lbl = label(hwnd, L"Parameters", paramsX, 431, 64);
        control(hwnd, L"EDIT", L"", ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, paramsX + 66, 432, 496 - (paramsX + 66), 22, ID_PARAMS);
        SendDlgItemMessageW(hwnd, ID_PARAMS, EM_SETLIMITTEXT, sizeof(settings.parameters) - 1, 0);
        option_tip(hwnd, lbl, ID_PARAMS, 0,
            L"Extra options for the game, as on its command line, separated by spaces. For example: "
            L"-uncut restores content cut from the game, -material starts in the material realm, "
            L"-skipcutscene skips the cutscenes.");
    }

    /* no Exit button: the window's close button does that */
    control(hwnd, L"BUTTON", L"Play", BS_DEFPUSHBUTTON | WS_TABSTOP, 504, 428, 64, 30, ID_PLAY);
    control(hwnd, L"BUTTON", L"Save", BS_PUSHBUTTON | WS_TABSTOP, 576, 428, 64, 30, ID_SAVE);

    /* --- lists --- */
    for (i = 0; i < COUNT(display_order); i++)
    {
        static const wchar_t *const names[] = { L"Windowed", L"Fullscreen", L"Borderless" };

        combo_add(ID_DISPLAY, names[display_order[i]]);
    }

    build_resolution_list();

    for (i = 0; i < resolution_count; i++)
    {
        wchar_t text[48];

        resolution_text(text, 48, resolution_list[i].w, resolution_list[i].h, resolution_list[i].refresh);
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

    combo_add(ID_DITHER, L"Off");
    combo_add(ID_DITHER, L"On");

    combo_add(ID_FPS, L"30");
    combo_add(ID_FPS, L"60");
}

static void show_settings(void)
{
    int i;
    int sel;

    for (i = 0; i < COUNT(display_order); i++)
    {
        if (display_order[i] == settings.display)
        {
            combo_set(ID_DISPLAY, i);
        }
    }


    sel = -1;

    {
        /* the saved size and rate; with no rate saved (older settings), the
         * size at the desktop's rate, else at its first (highest) rate */
        int wanted = (settings.refresh > 0) ? settings.refresh : desktop_refresh();
        int first = -1;

        for (i = 0; i < resolution_count; i++)
        {
            if ((resolution_list[i].w != settings.width) || (resolution_list[i].h != settings.height))
            {
                continue;
            }

            if (first < 0)
            {
                first = i;
            }

            if ((resolution_list[i].refresh == wanted) || (resolution_list[i].refresh == 0))
            {
                sel = i;
                break;
            }
        }

        if ((sel < 0) && (settings.refresh == 0))
        {
            sel = first;
        }
    }

    if (sel < 0)
    {
        /* a size or rate from the file that is not in the list */
        wchar_t text[48];

        resolution_text(text, 48, settings.width, settings.height, settings.refresh);
        sel = (int)SendDlgItemMessageW(main_window, ID_RESOLUTION, CB_ADDSTRING, 0, (LPARAM)text);
    }

    combo_set(ID_RESOLUTION, sel);
    combo_set(ID_SCALING, settings.scaleLinear ? 1 : 0);
    combo_set(ID_RENDERER, 0);
    yes_no_set(ID_VSYNC_YES, settings.vsync);
    yes_no_set(ID_PILLAR_YES, settings.pillarbox);
    combo_set(ID_FILTERING, settings.filtering ? 1 : 0);
    yes_no_set(ID_PGXP_YES, settings.pgxp);

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
    combo_set(ID_DITHER, settings.dithering ? 1 : 0);
    yes_no_set(ID_DEBUG_YES, settings.debugMenu);
    yes_no_set(ID_LOG_YES, settings.logging);
    yes_no_set(ID_DISCORD_YES, settings.discordPresence);

    /* only on while the Dreamcast disc is in the dreamcast folder */
    yes_no_set(ID_DC_YES, settings.dcModels && dc_disc_locate(NULL, 0, NULL, 0));

    /* only on while the April disc is in the proto folder */
    yes_no_set(ID_PROTO_YES, settings.protoAssets && proto_disc_locate(NULL, 0, NULL, 0));
    yes_no_set(ID_PCMOV_YES, settings.pcMovies && pc_disc_locate(NULL, 0));

    {
        wchar_t params[256];

        MultiByteToWideChar(CP_UTF8, 0, settings.parameters, -1, params, 256);
        SetDlgItemTextW(main_window, ID_PARAMS, params);
    }

    /* 30 or 60: higher limits and unlimited, from older versions, run at 60 */
    sel = ((settings.fpsLimit > 30) || (settings.fpsLimit <= 0)) ? 1 : 0;

    combo_set(ID_FPS, sel);
}

static void read_settings(void)
{
    int sel;
    wchar_t text[48];

    settings.display = display_order[combo_get(ID_DISPLAY)];

    sel = combo_get(ID_RESOLUTION);
    SendDlgItemMessageW(main_window, ID_RESOLUTION, CB_GETLBTEXT, sel, (LPARAM)text);
    settings.refresh = 0;

    if (swscanf(text, L"%d x %d @ %d", &settings.width, &settings.height, &settings.refresh) < 3)
    {
        settings.refresh = 0;
    }

    settings.scaleLinear = combo_get(ID_SCALING) == 1;
    settings.vsync = yes_no_get(ID_VSYNC_YES);
    settings.pillarbox = yes_no_get(ID_PILLAR_YES);
    settings.filtering = combo_get(ID_FILTERING) == 1;
    settings.pgxp = yes_no_get(ID_PGXP_YES);
    settings.antialiasing = aa_values[combo_get(ID_AA)];

    sel = combo_get(ID_AREA);
    snprintf(settings.area, sizeof(settings.area), "%s", (sel > 0) ? launcher_areas[sel - 1].unit : "");

    settings.skipIntros = combo_get(ID_SKIP);
    settings.dithering = combo_get(ID_DITHER) == 1;
    settings.preloadChunks = 0;
    settings.debugMenu = yes_no_get(ID_DEBUG_YES);
    settings.logging = yes_no_get(ID_LOG_YES);
    settings.discordPresence = yes_no_get(ID_DISCORD_YES);
    settings.dcModels = yes_no_get(ID_DC_YES);
    settings.protoAssets = yes_no_get(ID_PROTO_YES);
    settings.pcMovies = yes_no_get(ID_PCMOV_YES);
    settings.fpsLimit = fps_values[combo_get(ID_FPS)];

    {
        wchar_t params[256];

        GetDlgItemTextW(main_window, ID_PARAMS, params, 256);
        WideCharToMultiByte(CP_UTF8, 0, params, -1, settings.parameters, sizeof(settings.parameters), NULL, NULL);
    }
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
        case ID_DC_YES:
        {
            char problem[700];
            char folder[MAX_PATH];
            wchar_t text[1200];

            if (!dc_disc_locate(NULL, 0, problem, sizeof(problem)))
            {
                dc_disc_folder(folder, sizeof(folder));
                CreateDirectoryA(folder, NULL);

                swprintf(text, 1200,
                    L"The Dreamcast models are read from your own copy of the Dreamcast version of Soul Reaver.\n\n"
                    L"Place your dump of the disc, the .gdi file with all its track files, in this folder:\n\n%hs\n\n"
                    L"then choose Yes again.\n\n(%hs)", folder, problem);
                MessageBoxW(hwnd, text, L"Use DC Models", MB_ICONINFORMATION);
                yes_no_set(ID_DC_YES, 0);
                return 0;
            }

            yes_no_set(ID_DC_YES, 1);
            return 0;
        }
        case ID_DC_NO:
            yes_no_set(ID_DC_YES, 0);
            return 0;
        case ID_PROTO_YES:
        {
            char problem[700];
            char folder[MAX_PATH];
            wchar_t text[1200];
            HCURSOR old = SetCursor(LoadCursor(NULL, IDC_WAIT));
            int found = proto_disc_locate(NULL, 0, problem, sizeof(problem));

            SetCursor(old);

            if (!found)
            {
                proto_disc_folder(folder, sizeof(folder));
                CreateDirectoryA(folder, NULL);

                swprintf(text, 1200,
                    L"The prototype assets are read from your own copy of the April 14, 1999 prototype of Soul Reaver.\n\n"
                    L"Place your image of its disc (the .cue and .bin, or the .bin alone) in this folder:\n\n%hs\n\n"
                    L"then choose Yes again.\n\n(%hs)", folder, problem);
                MessageBoxW(hwnd, text, L"Use Proto Assets", MB_ICONINFORMATION);
                yes_no_set(ID_PROTO_YES, 0);
                return 0;
            }

            yes_no_set(ID_PROTO_YES, 1);
            return 0;
        }
        case ID_PROTO_NO:
            yes_no_set(ID_PROTO_YES, 0);
            return 0;
        case ID_PCMOV_YES:
        {
            char problem[700];
            char folder[MAX_PATH];
            wchar_t text[1200];
            HCURSOR old = SetCursor(LoadCursor(NULL, IDC_WAIT));
            int found = pc_disc_locate(problem, sizeof(problem));

            SetCursor(old);

            if (!found)
            {
                pc_disc_folder(folder, sizeof(folder));
                CreateDirectoryA(folder, NULL);

                swprintf(text, 1200,
                    L"The PC movies are read from your own copy of the 1999 PC version of Soul Reaver.\n\n"
                    L"Place the .rpl files from the root of its disc, or an image of the disc (.iso), in this folder:\n\n%hs\n\n"
                    L"then choose Yes again.\n\n(%hs)", folder, problem);
                MessageBoxW(hwnd, text, L"Use PC Movies", MB_ICONINFORMATION);
                yes_no_set(ID_PCMOV_YES, 0);
                return 0;
            }

            yes_no_set(ID_PCMOV_YES, 1);
            return 0;
        }
        case ID_PCMOV_NO:
            yes_no_set(ID_PCMOV_YES, 0);
            return 0;
        case ID_VSYNC_YES: case ID_VSYNC_NO:
        case ID_PILLAR_YES: case ID_PILLAR_NO:
        case ID_PGXP_YES: case ID_PGXP_NO:
        case ID_DEBUG_YES: case ID_DEBUG_NO:
        case ID_LOG_YES: case ID_LOG_NO:
        case ID_DISCORD_YES: case ID_DISCORD_NO:
        {
            /* Yes ids are even offsets from ID_VSYNC_YES */
            int yesId = ((id - ID_VSYNC_YES) % 2 == 0) ? id : id - 1;

            if ((id >= ID_PRELOAD_YES) && (id <= ID_DISCORD_NO))
            {
                yesId = ((id - ID_PRELOAD_YES) % 2 == 0) ? id : id - 1;
            }

            yes_no_set(yesId, id == yesId);
            return 0;
        }
        case ID_CONTROLS:
            controls_dialog(hwnd, &settings, ui_font, dpi);
            return 0;
        case ID_JOYSTICK:
            joystick_dialog(hwnd, &settings, ui_font, dpi);
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
    r.bottom = S(470);
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
