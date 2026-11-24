/*
 * Earlier areas windows (earlier.h).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "earlier.h"
#include "proto_areas.h"
#include "proto_disc.h"

#include <commctrl.h>

#define MAX_DATES   32
#define MAX_UNITS   1024
#define WM_APP_DONE (WM_APP + 20)

enum { ID_DATE = 600, ID_MAPS, ID_NOTE, ID_LOAD, ID_CANCEL };

void earlier_date_text(const char *date, wchar_t *out, int size)
{
    static const wchar_t *const months[] = {
        L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun", L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec",
    };
    int y, m, d;

    if ((sscanf(date, "%d-%d-%d", &y, &m, &d) == 3) && (m >= 1) && (m <= 12))
    {
        _snwprintf(out, size, L"%s %d, %d", months[m - 1], d, y);
    }
    else
    {
        _snwprintf(out, size, L"%hs", date);
    }

    out[size - 1] = L'\0';
}

#define S(v) MulDiv((v), dpi, 96)
#define CHILD(cls, text, style, x, y, w, h, id) \
    CreateWindowW(cls, text, WS_CHILD | WS_VISIBLE | (style), S(x), S(y), S(w), S(h), hwnd, (HMENU)(INT_PTR)(id), inst, NULL)
#define FONT(w) SendMessageW((w), WM_SETFONT, (WPARAM)font, TRUE)

/* a popup window of the given client size near the owner */
static HWND popup(HWND owner, const wchar_t *cls, WNDPROC proc, const wchar_t *title, int w, int h, int dpi)
{
    HINSTANCE inst = GetModuleHandleW(NULL);
    WNDCLASSW wc;
    RECT r;
    RECT o;

    if (!GetClassInfoW(inst, cls, &wc))
    {
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = proc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = cls;
        RegisterClassW(&wc);
    }

    r.left = 0;
    r.top = 0;
    r.right = S(w);
    r.bottom = S(h);
    AdjustWindowRect(&r, WS_CAPTION | WS_SYSMENU, FALSE);
    GetWindowRect(owner, &o);

    return CreateWindowExW(WS_EX_DLGMODALFRAME, cls, title, WS_CAPTION | WS_SYSMENU | WS_POPUP,
        o.left + S(30), o.top + S(20), r.right - r.left, r.bottom - r.top, owner, NULL, inst, NULL);
}

/* runs the window's messages until *done is set */
static void modal_loop(HWND owner, HWND hwnd, volatile int *done, int escapeCloses)
{
    MSG msg;

    EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW);

    while (!*done)
    {
        if (!GetMessageW(&msg, NULL, 0, 0))
        {
            /* the launcher is quitting: leave it for its own loop */
            PostQuitMessage((int)msg.wParam);
            *done = -1;
            break;
        }

        if (escapeCloses && (msg.message == WM_KEYDOWN) && (msg.wParam == VK_ESCAPE))
        {
            *done = -1;
            continue;
        }

        if (!IsDialogMessageW(hwnd, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(owner, TRUE);
    DestroyWindow(hwnd);
    SetForegroundWindow(owner);
}

/* --- Extract Earlier Areas --- */

typedef struct
{
    HWND window;
    HWND status;
    volatile int done;
    int count;
    char report[4096];
} ExtractState;

static ExtractState *xs;

static void extract_progress(void *ctx, const char *text)
{
    ExtractState *state = (ExtractState *)ctx;

    SetWindowTextA(state->status, text);
}

static DWORD WINAPI extract_thread(void *param)
{
    ExtractState *state = (ExtractState *)param;

    state->count = proto_areas_extract(extract_progress, state, state->report, sizeof(state->report));
    PostMessageW(state->window, WM_APP_DONE, 0, 0);

    return 0;
}

static LRESULT CALLBACK extract_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_APP_DONE)
    {
        xs->done = 1;
        return 0;
    }

    /* it can't be stopped halfway */
    if (msg == WM_CLOSE)
    {
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

void earlier_extract_dialog(HWND owner, HFONT font, int dpi)
{
    static ExtractState state;
    HINSTANCE inst = GetModuleHandleW(NULL);
    char proto[MAX_PATH];
    wchar_t text[4500];
    HANDLE thread;
    HWND hwnd;
    HWND c;

    memset(&state, 0, sizeof(state));
    xs = &state;

    hwnd = popup(owner, L"RewraithedExtract", extract_proc, L"Extract Earlier Areas", 420, 96, dpi);
    state.window = hwnd;

    c = CHILD(L"STATIC", L"This can take some minutes the first time: each prototype's disc is unpacked, "
        L"then its maps are copied out.", 0, 16, 12, 388, 36, 0);
    FONT(c);
    state.status = CHILD(L"STATIC", L"Looking at the proto folder...", SS_ENDELLIPSIS, 16, 56, 388, 24, 0);
    FONT(state.status);

    thread = CreateThread(NULL, 0, extract_thread, &state, 0, NULL);

    if (thread == NULL)
    {
        DestroyWindow(hwnd);
        return;
    }

    modal_loop(owner, hwnd, &state.done, 0);
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);

    proto_disc_folder(proto, sizeof(proto));

    if (state.count == 0)
    {
        _snwprintf(text, 4500,
            L"No prototype maps were found.\n\nPut the prototypes' archives (.7z or .zip, with the date in their names, "
            L"like \"Legacy of Kain - Soul Reaver (Jun 18, 1999 prototype).7z\"), or their disc images in folders named "
            L"by date (like 1999-06-18), in:\n%hs\n\n%hs",
            proto, state.report);
    }
    else if (state.report[0] == '\0')
    {
        _snwprintf(text, 4500, L"The maps of %d prototypes are ready. Choose one with Load Earlier Areas.", state.count);
    }
    else
    {
        _snwprintf(text, 4500, L"The maps of %d prototypes are ready. Choose one with Load Earlier Areas.\n\n%hs",
            state.count, state.report);
    }

    text[4499] = L'\0';
    MessageBoxW(owner, text, L"Extract Earlier Areas", (state.count == 0) ? MB_ICONINFORMATION : MB_OK);
}

/* --- Load Earlier Areas --- */

typedef struct
{
    char dates[MAX_DATES][PROTO_DATE_SIZE];
    int formats[MAX_DATES];
    int dateCount;
    char units[MAX_UNITS][PROTO_UNIT_SIZE];
    char startable[MAX_UNITS];
    int unitCount;
    HWND dateList;
    HWND mapList;
    HWND note;
    HWND load;
    int date;
    int chosen;             /* the map chosen with Load */
    volatile int done;      /* 1 Load, -1 cancel */
} LoadState;

static LoadState *ls;

static void add_unit(void *ctx, const char *unit, int startable)
{
    LoadState *state = (LoadState *)ctx;

    if (state->unitCount < MAX_UNITS)
    {
        state->startable[state->unitCount] = (char)startable;
        snprintf(state->units[state->unitCount++], PROTO_UNIT_SIZE, "%s", unit);
    }
}

/* Load needs a map Raziel can start in; the note says why not */
static void update_load_button(void)
{
    int sel = (int)SendMessageW(ls->mapList, LB_GETCURSEL, 0, 0);
    int ok = (ls->formats[ls->date] == 1) && (sel >= 0) && (sel < ls->unitCount) && ls->startable[sel];

    if ((ls->formats[ls->date] == 1) && (sel >= 0) && (sel < ls->unitCount) && !ls->startable[sel])
    {
        wchar_t text[160];

        _snwprintf(text, 160, L"%hs has no place for Raziel to start in: walk into it from a map next to it.",
            ls->units[sel]);
        text[159] = L'\0';
        SetWindowTextW(ls->note, text);
    }
    else if (ls->formats[ls->date] == 1)
    {
        wchar_t text[96];

        _snwprintf(text, 96, L"%d maps. Choose one to start in, then Load.", ls->unitCount);
        text[95] = L'\0';
        SetWindowTextW(ls->note, text);
    }

    EnableWindow(ls->load, ok);
}

/* the maps of the chosen prototype; `select` is selected if it's one */
static void show_maps(const char *select)
{
    int i;

    ls->unitCount = 0;
    ls->formats[ls->date] = proto_areas_read(ls->dates[ls->date], add_unit, NULL, ls);

    SendMessageW(ls->mapList, WM_SETREDRAW, FALSE, 0);
    SendMessageW(ls->mapList, LB_RESETCONTENT, 0, 0);

    for (i = 0; i < ls->unitCount; i++)
    {
        wchar_t text[PROTO_UNIT_SIZE];

        MultiByteToWideChar(CP_UTF8, 0, ls->units[i], -1, text, PROTO_UNIT_SIZE);
        SendMessageW(ls->mapList, LB_ADDSTRING, 0, (LPARAM)text);

        if ((select != NULL) && (_stricmp(select, ls->units[i]) == 0))
        {
            SendMessageW(ls->mapList, LB_SETCURSEL, i, 0);
        }
    }

    SendMessageW(ls->mapList, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(ls->mapList, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_UPDATENOW);

    if (ls->formats[ls->date] != 1)
    {
        SetWindowTextW(ls->note, L"This prototype's maps are in an older level format than the game's, "
            L"so the game can't load them. Press Extract Earlier Areas again to convert them.");
    }

    update_load_button();
}

static LRESULT CALLBACK load_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_COMMAND)
    {
        int id = LOWORD(wp);
        int code = HIWORD(wp);

        if ((id == ID_DATE) && (code == CBN_SELCHANGE))
        {
            ls->date = (int)SendMessageW(ls->dateList, CB_GETCURSEL, 0, 0);
            show_maps(NULL);
        }
        else if ((id == ID_MAPS) && (code == LBN_SELCHANGE))
        {
            update_load_button();
        }
        else if (((id == ID_MAPS) && (code == LBN_DBLCLK)) || (id == ID_LOAD))
        {
            if (IsWindowEnabled(ls->load))
            {
                ls->chosen = (int)SendMessageW(ls->mapList, LB_GETCURSEL, 0, 0);
                ls->done = 1;
            }
        }
        else if (id == ID_CANCEL)
        {
            ls->done = -1;
        }

        return 0;
    }

    if (msg == WM_CLOSE)
    {
        ls->done = -1;
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

int earlier_load_dialog(HWND owner, PortSettings *s, HFONT font, int dpi)
{
    HINSTANCE inst = GetModuleHandleW(NULL);
    LoadState *state = (LoadState *)calloc(1, sizeof(LoadState));
    HWND hwnd;
    HWND c;
    int i;
    int result = 0;

    if (state == NULL)
    {
        return 0;
    }

    ls = state;
    state->dateCount = proto_areas_dates(state->dates, MAX_DATES);

    if (state->dateCount > MAX_DATES)
    {
        state->dateCount = MAX_DATES;
    }

    if (state->dateCount == 0)
    {
        MessageBoxW(owner, L"No prototype's maps are extracted yet: put the prototypes' archives in the proto folder "
            L"next to the game, then press Extract Earlier Areas.", L"Load Earlier Areas", MB_ICONINFORMATION);
        free(state);
        return 0;
    }

    hwnd = popup(owner, L"RewraithedEarlier", load_proc, L"Load Earlier Areas", 340, 444, dpi);

    c = CHILD(L"STATIC", L"Prototype", SS_CENTERIMAGE, 16, 14, 70, 24, 0);
    FONT(c);
    state->dateList = CHILD(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 92, 14, 232, 300, ID_DATE);
    FONT(state->dateList);

    c = CHILD(L"STATIC", L"Maps", SS_CENTERIMAGE, 16, 48, 70, 24, 0);
    FONT(c);
    state->mapList = CHILD(L"LISTBOX", L"", LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_MULTICOLUMN | WS_HSCROLL | WS_BORDER | WS_TABSTOP,
        16, 74, 308, 284, ID_MAPS);
    FONT(state->mapList);
    SendMessageW(state->mapList, LB_SETCOLUMNWIDTH, S(76), 0);

    state->note = CHILD(L"STATIC", L"", 0, 16, 364, 308, 34, ID_NOTE);
    FONT(state->note);

    state->load = CHILD(L"BUTTON", L"Load", WS_TABSTOP | BS_DEFPUSHBUTTON, 148, 404, 84, 30, ID_LOAD);
    FONT(state->load);
    c = CHILD(L"BUTTON", L"Cancel", WS_TABSTOP | BS_PUSHBUTTON, 240, 404, 84, 30, ID_CANCEL);
    FONT(c);

    for (i = 0; i < state->dateCount; i++)
    {
        wchar_t text[48];

        earlier_date_text(state->dates[i], text, 32);

        if (proto_areas_read(state->dates[i], NULL, NULL, NULL) != 1)
        {
            wcscat(text, L" (older format)");
        }
        SendMessageW(state->dateList, CB_ADDSTRING, 0, (LPARAM)text);

        if (strcmp(state->dates[i], s->earlierAreas) == 0)
        {
            state->date = i;
        }
    }

    /* the newest, unless one is chosen already */
    if (s->earlierAreas[0] == '\0')
    {
        state->date = state->dateCount - 1;
    }

    SendMessageW(state->dateList, CB_SETCURSEL, state->date, 0);
    show_maps(s->earlierAreas[0] ? s->area : NULL);

    modal_loop(owner, hwnd, &state->done, 1);

    if (state->done == 1)
    {
        int sel = state->chosen;

        if ((sel >= 0) && (sel < state->unitCount) && (state->formats[state->date] == 1) && state->startable[sel])
        {
            snprintf(s->earlierAreas, sizeof(s->earlierAreas), "%s", state->dates[state->date]);
            snprintf(s->area, sizeof(s->area), "%s", state->units[sel]);
            result = 1;
        }
    }

    free(state);
    ls = NULL;

    return result;
}

#undef FONT
#undef CHILD
#undef S
