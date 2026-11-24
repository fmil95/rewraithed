/*
 * Mods window (mods_window.h): a scrolling list of the mods, the built-in ones
 * (port_mods, mod_list.h) then the mod folders in the mods folder
 * (mod_folders.h), each with a check to turn it on; the one chosen in the list
 * is described beside it (author, what it does, what it needs and whether
 * that is there, where it is). A mod that needs files of the player's own is
 * only turned on once they are there, else its message says where they go.
 * Open Mods Folder shows the folder new mods go in.
 */
#include <stdio.h>
#include <string.h>

#include "mods_window.h"

#include <commctrl.h>
#include <shellapi.h>

#define MAX_ROWS        (MOD_COUNT + MAX_FOLDER_MODS)
#define ID_LIST         700
#define ID_TITLE        701
#define ID_AUTHOR       702
#define ID_DESCRIPTION  703
#define ID_NEEDS        704
#define ID_WHERE        705
#define ID_OK           800
#define ID_CANCEL       801
#define ID_OPEN_FOLDER  802
#define WM_APP_UNCHECK  (WM_APP + 30)

typedef struct
{
    FolderMod folders[MAX_FOLDER_MODS];
    int folderCount;
    int on[MAX_ROWS];               /* the built-in mods, then the folders */
    HWND list;
    HWND title, author, description, needs, where;
    int filling;                    /* setting the checks: not the player's clicks */
    int done;                       /* 1 OK, -1 cancel */
} ModsState;

static ModsState *ms;

static int row_count(void)
{
    return MOD_COUNT + ms->folderCount;
}

static void utf8_to_wide(const char *in, wchar_t *out, int size)
{
    if (MultiByteToWideChar(CP_UTF8, 0, in, -1, out, size) == 0)
    {
        out[0] = L'\0';
    }
}

static void set_text(HWND w, const char *utf8)
{
    wchar_t text[1200];

    utf8_to_wide(utf8, text, 1200);
    SetWindowTextW(w, text);
}

/* 1 if the row's mod has what it needs; else 0 and the message */
static int row_available(int row, char *problem, size_t size)
{
    problem[0] = '\0';

    if (row < MOD_COUNT)
    {
        return (port_mods[row].available == NULL) || port_mods[row].available(problem, size);
    }

    return mod_folders_available(&ms->folders[row - MOD_COUNT], problem, size);
}

/* the chosen mod, beside the list */
static void describe(int row)
{
    char text[1400];
    char problem[1200];
    int ok;

    if ((row < 0) || (row >= row_count()))
    {
        SetWindowTextW(ms->title, L"");
        SetWindowTextW(ms->author, L"");
        SetWindowTextW(ms->description, L"Choose a mod in the list to read about it.");
        SetWindowTextW(ms->needs, L"");
        SetWindowTextW(ms->where, L"");
        return;
    }

    ok = row_available(row, problem, sizeof(problem));

    if (row < MOD_COUNT)
    {
        set_text(ms->title, port_mods[row].name);
        SetWindowTextW(ms->author, L"By Rewraithed");
        set_text(ms->description, port_mods[row].description);
        SetWindowTextW(ms->where, L"Built into the game.");

        if (port_mods[row].available == NULL)
        {
            SetWindowTextW(ms->needs, L"Needs nothing more.");
        }
        else
        {
            SetWindowTextW(ms->needs, ok ? L"Needs files of your own: found." :
                L"Needs files of your own: not found yet. Turn it on to see where they go.");
        }
    }
    else
    {
        const FolderMod *m = &ms->folders[row - MOD_COUNT];

        set_text(ms->title, m->name);

        if (m->author[0] != '\0')
        {
            snprintf(text, sizeof(text), "By %s", m->author);
            set_text(ms->author, text);
        }
        else
        {
            SetWindowTextW(ms->author, L"");
        }

        set_text(ms->description, (m->description[0] != '\0') ? m->description : "(No description in its mod.ini.)");
        snprintf(text, sizeof(text), "In the mods folder: %s", m->folder);
        set_text(ms->where, text);

        if (m->requires[0] == '\0')
        {
            SetWindowTextW(ms->needs, L"Needs nothing more.");
        }
        else
        {
            snprintf(text, sizeof(text), "Needs %s next to the game: %s", m->requires, ok ? "found." : "not found yet.");
            set_text(ms->needs, text);
        }
    }
}

static void set_check(int row, int on)
{
    ms->filling = 1;
    ListView_SetCheckState(ms->list, row, on);
    ms->filling = 0;
}

static void open_mods_folder(HWND hwnd)
{
    char folder[MAX_PATH];

    mod_folders_root(folder, sizeof(folder));
    CreateDirectoryA(folder, NULL);
    ShellExecuteA(hwnd, "open", folder, NULL, NULL, SW_SHOWNORMAL);
}

static void on_item_changed(HWND hwnd, const NMLISTVIEW *n)
{
    if ((n->iItem < 0) || (n->iItem >= row_count()) || !(n->uChanged & LVIF_STATE))
    {
        return;
    }

    /* chosen: described */
    if ((n->uNewState & LVIS_SELECTED) && !(n->uOldState & LVIS_SELECTED))
    {
        describe(n->iItem);
    }

    /* its check */
    if (!ms->filling && ((n->uNewState ^ n->uOldState) & LVIS_STATEIMAGEMASK))
    {
        int on = (n->uNewState & LVIS_STATEIMAGEMASK) == INDEXTOSTATEIMAGEMASK(2);
        char problem[1200];

        if (on && !row_available(n->iItem, problem, sizeof(problem)))
        {
            wchar_t text[1200];
            wchar_t title[128];

            utf8_to_wide(problem, text, 1200);
            ListView_GetItemText(ms->list, n->iItem, 0, title, 128);
            MessageBoxW(hwnd, text, title, MB_ICONINFORMATION);

            /* not while the list is still handling the click */
            PostMessageW(hwnd, WM_APP_UNCHECK, (WPARAM)n->iItem, 0);
            on = 0;
        }

        ms->on[n->iItem] = on;
    }
}

static LRESULT CALLBACK mods_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_NOTIFY:
    {
        const NMHDR *h = (const NMHDR *)lp;

        if ((h->idFrom == ID_LIST) && (h->code == LVN_ITEMCHANGED))
        {
            on_item_changed(hwnd, (const NMLISTVIEW *)lp);
        }

        return 0;
    }
    case WM_APP_UNCHECK:
        set_check((int)wp, 0);
        ms->on[(int)wp] = 0;
        return 0;
    case WM_COMMAND:
    {
        int id = LOWORD(wp);

        if (id == ID_OPEN_FOLDER)
        {
            open_mods_folder(hwnd);
        }
        else if (id == ID_OK)
        {
            ms->done = 1;
        }
        else if (id == ID_CANCEL)
        {
            ms->done = -1;
        }

        return 0;
    }
    case WM_CTLCOLORSTATIC:
        /* the details on the window's own colour */
        SetBkMode((HDC)wp, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    case WM_CLOSE:
        ms->done = -1;
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

int mods_dialog(HWND owner, PortSettings *s, HFONT font, int dpi)
{
    static int registered;
    static ModsState state;
    HINSTANCE inst = GetModuleHandleW(NULL);
    INITCOMMONCONTROLSEX icc;
    HFONT bold = NULL;
    LOGFONTW lf;
    LVCOLUMNW col;
    HWND hwnd;
    HWND c;
    RECT r;
    MSG msg;
    int i, k;

#define S(v) MulDiv((v), dpi, 96)
#define CHILD(cls, text, style, x, y, w, h, id) \
    CreateWindowW(cls, text, WS_CHILD | WS_VISIBLE | (style), S(x), S(y), S(w), S(h), hwnd, (HMENU)(INT_PTR)(id), inst, NULL)
#define FONT(w) SendMessageW((w), WM_SETFONT, (WPARAM)font, TRUE)

    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icc);

    if (!registered)
    {
        WNDCLASSW wc;

        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = mods_proc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"RewraithedMods";
        RegisterClassW(&wc);
        registered = 1;
    }

    memset(&state, 0, sizeof(state));
    ms = &state;
    state.folderCount = mod_folders_list(state.folders, MAX_FOLDER_MODS);

    for (i = 0; i < MOD_COUNT; i++)
    {
        state.on[i] = s->mods[i];
    }

    for (i = 0; i < state.folderCount; i++)
    {
        for (k = 0; k < s->folderModCount; k++)
        {
            if (_stricmp(s->folderMods[k], state.folders[i].folder) == 0)
            {
                state.on[MOD_COUNT + i] = 1;
            }
        }
    }

    r.left = 0;
    r.top = 0;
    r.right = S(640);
    r.bottom = S(404);
    AdjustWindowRect(&r, WS_CAPTION | WS_SYSMENU, FALSE);

    {
        RECT o;

        GetWindowRect(owner, &o);

        hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"RewraithedMods", L"Mods",
            WS_CAPTION | WS_SYSMENU | WS_POPUP, o.left + S(20), o.top + S(20), r.right - r.left, r.bottom - r.top,
            owner, NULL, inst, NULL);
    }

    if ((font != NULL) && GetObjectW(font, sizeof(lf), &lf))
    {
        lf.lfWeight = FW_BOLD;
        lf.lfHeight = lf.lfHeight * 5 / 4;
        bold = CreateFontIndirectW(&lf);
    }

    /* --- the list --- */
    c = CHILD(L"STATIC", L"Turn a mod on with its check; choose one to read about it.", 0, 12, 10, 616, 18, 0);
    FONT(c);

    state.list = CHILD(WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER |
        WS_BORDER | WS_TABSTOP | WS_VSCROLL, 12, 32, 300, 314, ID_LIST);
    FONT(state.list);
    ListView_SetExtendedListViewStyle(state.list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

    memset(&col, 0, sizeof(col));
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.pszText = L"Mod";
    col.cx = S(186);
    ListView_InsertColumn(state.list, 0, &col);
    col.pszText = L"Kind";
    col.cx = S(90);
    ListView_InsertColumn(state.list, 1, &col);

    state.filling = 1;

    for (i = 0; i < row_count(); i++)
    {
        wchar_t name[128];
        LVITEMW item;

        utf8_to_wide((i < MOD_COUNT) ? port_mods[i].name : state.folders[i - MOD_COUNT].name, name, 128);
        memset(&item, 0, sizeof(item));
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.pszText = name;
        ListView_InsertItem(state.list, &item);
        ListView_SetItemText(state.list, i, 1, (i < MOD_COUNT) ? L"Built in" : L"Mods folder");
        ListView_SetCheckState(state.list, i, state.on[i]);
    }

    state.filling = 0;

    /* --- the chosen mod --- */
    c = CHILD(L"BUTTON", L"About", BS_GROUPBOX, 324, 26, 304, 320, 0);
    FONT(c);
    state.title = CHILD(L"STATIC", L"", SS_ENDELLIPSIS, 336, 48, 280, 24, ID_TITLE);
    SendMessageW(state.title, WM_SETFONT, (WPARAM)((bold != NULL) ? bold : font), TRUE);
    state.author = CHILD(L"STATIC", L"", SS_ENDELLIPSIS, 336, 74, 280, 20, ID_AUTHOR);
    FONT(state.author);
    state.description = CHILD(L"STATIC", L"", 0, 336, 102, 280, 150, ID_DESCRIPTION);
    FONT(state.description);
    state.needs = CHILD(L"STATIC", L"", 0, 336, 258, 280, 48, ID_NEEDS);
    FONT(state.needs);
    state.where = CHILD(L"STATIC", L"", SS_PATHELLIPSIS, 336, 312, 280, 20, ID_WHERE);
    FONT(state.where);

    /* --- buttons --- */
    c = CHILD(L"BUTTON", L"Open Mods Folder", WS_TABSTOP | BS_PUSHBUTTON, 12, 362, 140, 30, ID_OPEN_FOLDER);
    FONT(c);
    c = CHILD(L"BUTTON", L"OK", WS_TABSTOP | BS_DEFPUSHBUTTON, 452, 362, 84, 30, ID_OK);
    FONT(c);
    c = CHILD(L"BUTTON", L"Cancel", WS_TABSTOP | BS_PUSHBUTTON, 544, 362, 84, 30, ID_CANCEL);
    FONT(c);

#undef FONT
#undef CHILD
#undef S

    /* the first mod, described */
    ListView_SetItemState(state.list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    describe(0);

    EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    SetFocus(state.list);

    while (!state.done)
    {
        if (!GetMessageW(&msg, NULL, 0, 0))
        {
            /* the launcher is quitting: leave it for its own loop */
            PostQuitMessage((int)msg.wParam);
            state.done = -1;
            break;
        }

        if ((msg.message == WM_KEYDOWN) && (msg.wParam == VK_ESCAPE))
        {
            state.done = -1;
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

    if (bold != NULL)
    {
        DeleteObject(bold);
    }

    if (state.done == 1)
    {
        for (i = 0; i < MOD_COUNT; i++)
        {
            s->mods[i] = state.on[i];
        }

        /* the folders turned on that are there (a removed one is forgotten) */
        s->folderModCount = 0;

        for (i = 0; i < state.folderCount; i++)
        {
            if (state.on[MOD_COUNT + i])
            {
                snprintf(s->folderMods[s->folderModCount++], FOLDER_MOD_NAME, "%s", state.folders[i].folder);
            }
        }

        return 1;
    }

    return 0;
}
