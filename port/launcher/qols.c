/*
 * QoLs window: Yes/No options that change the game's own behaviour for
 * convenience. Each one is read by the game (port/src/qol.c).
 */
#include <string.h>

#include "qols.h"

#include <commctrl.h>

/* "Make Drowned Abbey jumps more forgiving" is not shown until the game side
 * of it is done (the gate between aluka23 and aluka27) */
#define QOLS_SHOW_ABBEY 0

/* the dialog's rows of options, and the height of each */
#define QOLS_ROWS       (2 + QOLS_SHOW_ABBEY)
#define QOLS_ROW_HEIGHT 32

enum { ID_CHARGERS_YES = 500, ID_CHARGERS_NO, ID_NOREGEN_YES, ID_NOREGEN_NO, ID_ABBEY_YES, ID_ABBEY_NO, ID_OK = 560, ID_CANCEL };

typedef struct
{
    PortSettings work;
    HWND chargersYes;
    HWND chargersNo;
    HWND abbeyYes;
    HWND abbeyNo;
    HWND noRegenYes;
    HWND noRegenNo;
    int done;               /* 1 OK, -1 cancel */
} QolsState;

static QolsState *qs;

static void show_all(void)
{
    SendMessageW(qs->chargersYes, BM_SETCHECK, qs->work.underChargers ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(qs->chargersNo, BM_SETCHECK, qs->work.underChargers ? BST_UNCHECKED : BST_CHECKED, 0);
    if (qs->abbeyYes != NULL)
    {
        SendMessageW(qs->abbeyYes, BM_SETCHECK, qs->work.abbeyJumps ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(qs->abbeyNo, BM_SETCHECK, qs->work.abbeyJumps ? BST_UNCHECKED : BST_CHECKED, 0);
    }
    SendMessageW(qs->noRegenYes, BM_SETCHECK, qs->work.noPuzzleRespawn ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(qs->noRegenNo, BM_SETCHECK, qs->work.noPuzzleRespawn ? BST_UNCHECKED : BST_CHECKED, 0);
}

static LRESULT CALLBACK qols_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_COMMAND:
    {
        int id = LOWORD(wp);

        if ((id == ID_CHARGERS_YES) || (id == ID_CHARGERS_NO))
        {
            qs->work.underChargers = (id == ID_CHARGERS_YES);
            show_all();
        }
        else if ((id == ID_ABBEY_YES) || (id == ID_ABBEY_NO))
        {
            qs->work.abbeyJumps = (id == ID_ABBEY_YES);
            show_all();
        }
        else if ((id == ID_NOREGEN_YES) || (id == ID_NOREGEN_NO))
        {
            qs->work.noPuzzleRespawn = (id == ID_NOREGEN_YES);
            show_all();
        }
        else if (id == ID_OK)
        {
            qs->done = 1;
        }
        else if (id == ID_CANCEL)
        {
            qs->done = -1;
        }

        return 0;
    }
    case WM_CLOSE:
        qs->done = -1;
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

int qols_dialog(HWND owner, PortSettings *s, HFONT font, int dpi)
{
    static int registered;
    QolsState state;
    HINSTANCE inst = GetModuleHandleW(NULL);
    HWND hwnd;
    HWND c;
    HWND tip;
    RECT r;
    MSG msg;

#define S(v) MulDiv((v), dpi, 96)
#define CHILD(cls, text, style, x, y, w, h, id) \
    CreateWindowW(cls, text, WS_CHILD | WS_VISIBLE | (style), S(x), S(y), S(w), S(h), hwnd, (HMENU)(INT_PTR)(id), inst, NULL)
#define FONT(w) SendMessageW((w), WM_SETFONT, (WPARAM)font, TRUE)

    if (!registered)
    {
        WNDCLASSW wc;

        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = qols_proc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"RewraithedQoLs";
        RegisterClassW(&wc);
        registered = 1;
    }

    memset(&state, 0, sizeof(state));
    state.work = *s;
    qs = &state;

    r.left = 0;
    r.top = 0;
    r.right = S(464);
    r.bottom = S(184 - (3 - QOLS_ROWS) * QOLS_ROW_HEIGHT);
    AdjustWindowRect(&r, WS_CAPTION | WS_SYSMENU, FALSE);

    {
        RECT o;

        GetWindowRect(owner, &o);

        hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"RewraithedQoLs", L"QoLs",
            WS_CAPTION | WS_SYSMENU | WS_POPUP, o.left + S(30), o.top + S(20), r.right - r.left, r.bottom - r.top,
            owner, NULL, inst, NULL);
    }

    c = CHILD(L"BUTTON", L"Quality-of-Life Improvements", BS_GROUPBOX, 12, 8, 440, 124 - (3 - QOLS_ROWS) * QOLS_ROW_HEIGHT, 0);
    FONT(c);

    /* each option's explanation, on its label and checks */
    tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwnd, NULL, inst, NULL);
    SendMessageW(tip, TTM_SETMAXTIPWIDTH, 0, S(300));
    SendMessageW(tip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 30000);

    {
        static const struct
        {
            const wchar_t *label;
            const wchar_t *tip;
            int yesId;
        } rows[] = {
            { L"Add health and energy rechargers in Underworld",
              L"Places a health and an energy recharger in the Underworld (under1). They stay hidden until "
              L"Kain's scene in pillars1 shows that area's own.", ID_CHARGERS_YES },
#if QOLS_SHOW_ABBEY
            { L"Make Drowned Abbey jumps more forgiving",
              L"Once the stained glass window in aluka23 is shattered, the gate between aluka23 and aluka27 is gone "
              L"and can be passed through.", ID_ABBEY_YES },
#endif
            { L"Disable respawning enemies in block puzzles",
              L"The Zephonim of the block puzzle rooms stay dead once killed.", ID_NOREGEN_YES },
        };
        int row;

        for (row = 0; row < (int)(sizeof(rows) / sizeof(rows[0])); row++)
        {
            int y = 28 + row * QOLS_ROW_HEIGHT;
            HWND targets[3];
            TOOLINFOW ti;
            int i;

            targets[0] = CHILD(L"STATIC", rows[row].label, SS_CENTERIMAGE | SS_NOTIFY, 24, y, 300, 24, 0);
            targets[1] = CHILD(L"BUTTON", L"Yes", BS_CHECKBOX | WS_TABSTOP, 328, y, 56, 24, rows[row].yesId);
            targets[2] = CHILD(L"BUTTON", L"No", BS_CHECKBOX | WS_TABSTOP, 388, y, 56, 24, rows[row].yesId + 1);

            for (i = 0; i < 3; i++)
            {
                FONT(targets[i]);

                memset(&ti, 0, sizeof(ti));
                ti.cbSize = sizeof(ti);
                ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
                ti.hwnd = hwnd;
                ti.uId = (UINT_PTR)targets[i];
                ti.lpszText = (wchar_t *)rows[row].tip;
                SendMessageW(tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
            }

            if (rows[row].yesId == ID_CHARGERS_YES)
            {
                state.chargersYes = targets[1];
                state.chargersNo = targets[2];
            }
            else if (rows[row].yesId == ID_ABBEY_YES)
            {
                state.abbeyYes = targets[1];
                state.abbeyNo = targets[2];
            }
            else
            {
                state.noRegenYes = targets[1];
                state.noRegenNo = targets[2];
            }
        }
    }

    show_all();

    c = CHILD(L"BUTTON", L"OK", WS_TABSTOP | BS_DEFPUSHBUTTON, 276, 142 - (3 - QOLS_ROWS) * QOLS_ROW_HEIGHT, 84, 30, ID_OK);
    FONT(c);
    c = CHILD(L"BUTTON", L"Cancel", WS_TABSTOP | BS_PUSHBUTTON, 368, 142 - (3 - QOLS_ROWS) * QOLS_ROW_HEIGHT, 84, 30, ID_CANCEL);
    FONT(c);

#undef FONT
#undef CHILD
#undef S

    EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW);

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

    if (state.done == 1)
    {
        s->noPuzzleRespawn = state.work.noPuzzleRespawn;
        s->underChargers = state.work.underChargers;
        s->abbeyJumps = state.work.abbeyJumps;
        return 1;
    }

    return 0;
}
