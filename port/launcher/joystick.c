/*
 * Joystick window. The game reads a game controller (Xbox, PlayStation and
 * most others SDL knows) as the PlayStation pad: each action gets one of
 * the controller's buttons, chosen from a list; the left stick moves as
 * the d-pad does. Buttons are stored by SDL's names, which the game looks
 * up (port/src/pc_main.c). A button already used by another action is
 * swapped with it.
 */
#include <stdio.h>
#include <string.h>

#include "joystick.h"

#include <commctrl.h>

/* the actions of the PSX buttons, in the order of BIND_* */
static const wchar_t *const action_labels[BIND_COUNT] = {
    L"Up", L"Down", L"Left", L"Right",
    L"Jump", L"Feed", L"Attack", L"Telekinesis",
    L"Crouch", L"Look Right", L"Stealth", L"Look Left",
    L"Glyph Menu", L"Pause Menu",
};

/* port_joy_button_names, as shown */
static const wchar_t *const button_labels[JOY_BUTTON_COUNT] = {
    L"None", L"A / Cross", L"B / Circle", L"X / Square", L"Y / Triangle",
    L"LB / L1", L"RB / R1", L"LT / L2", L"RT / R2",
    L"Left Stick Press", L"Right Stick Press", L"Back / Select", L"Start",
    L"D-Pad Up", L"D-Pad Down", L"D-Pad Left", L"D-Pad Right",
};

enum { ID_USE_YES = 400, ID_USE_NO, ID_BUTTON0 = 420, ID_OK = 460, ID_CANCEL, ID_DEFAULTS };

typedef struct
{
    PortSettings work;
    HWND combos[BIND_COUNT];
    HWND yes;
    HWND no;
    int done;               /* 1 OK, -1 cancel */
} JoystickState;

static JoystickState *js;

static int button_index(const char *name)
{
    int b;

    for (b = 0; b < JOY_BUTTON_COUNT; b++)
    {
        if (_stricmp(name, port_joy_button_names[b]) == 0)
        {
            return b;
        }
    }

    return 0;
}

static void show_all(void)
{
    int i;

    for (i = 0; i < BIND_COUNT; i++)
    {
        SendMessageW(js->combos[i], CB_SETCURSEL, button_index(js->work.joy[i]), 0);
        EnableWindow(js->combos[i], js->work.joystick);
    }

    SendMessageW(js->yes, BM_SETCHECK, js->work.joystick ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(js->no, BM_SETCHECK, js->work.joystick ? BST_UNCHECKED : BST_CHECKED, 0);
}

static LRESULT CALLBACK joystick_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_COMMAND:
    {
        int id = LOWORD(wp);

        if ((id == ID_USE_YES) || (id == ID_USE_NO))
        {
            js->work.joystick = (id == ID_USE_YES);
            show_all();
        }
        else if ((id >= ID_BUTTON0) && (id < ID_BUTTON0 + BIND_COUNT) && (HIWORD(wp) == CBN_SELCHANGE))
        {
            int i = id - ID_BUTTON0;
            int b = (int)SendMessageW(js->combos[i], CB_GETCURSEL, 0, 0);
            int j;

            /* swap with an action already on that button */
            for (j = 0; j < BIND_COUNT; j++)
            {
                if ((j != i) && (b != 0) && (button_index(js->work.joy[j]) == b))
                {
                    snprintf(js->work.joy[j], sizeof(js->work.joy[j]), "%s", js->work.joy[i]);
                }
            }

            snprintf(js->work.joy[i], sizeof(js->work.joy[i]), "%s", port_joy_button_names[b]);
            show_all();
        }
        else if (id == ID_DEFAULTS)
        {
            PortSettings d;

            port_settings_defaults(&d);
            memcpy(js->work.joy, d.joy, sizeof(d.joy));
            js->work.joystick = d.joystick;
            show_all();
        }
        else if (id == ID_OK)
        {
            js->done = 1;
        }
        else if (id == ID_CANCEL)
        {
            js->done = -1;
        }

        return 0;
    }
    case WM_CLOSE:
        js->done = -1;
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

int joystick_dialog(HWND owner, PortSettings *s, HFONT font, int dpi)
{
    static int registered;
    JoystickState state;
    HINSTANCE inst = GetModuleHandleW(NULL);
    HWND hwnd;
    HWND c;
    RECT r;
    MSG msg;
    int i;

#define S(v) MulDiv((v), dpi, 96)
#define CHILD(cls, text, style, x, y, w, h, id) \
    CreateWindowW(cls, text, WS_CHILD | WS_VISIBLE | (style), S(x), S(y), S(w), S(h), hwnd, (HMENU)(INT_PTR)(id), inst, NULL)
#define FONT(w) SendMessageW((w), WM_SETFONT, (WPARAM)font, TRUE)

    if (!registered)
    {
        WNDCLASSW wc;

        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = joystick_proc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"RewraithedJoystick";
        RegisterClassW(&wc);
        registered = 1;
    }

    memset(&state, 0, sizeof(state));
    state.work = *s;
    js = &state;

    r.left = 0;
    r.top = 0;
    r.right = S(584);
    r.bottom = S(382);
    AdjustWindowRect(&r, WS_CAPTION | WS_SYSMENU, FALSE);

    {
        RECT o;

        GetWindowRect(owner, &o);

        hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"RewraithedJoystick", L"Joystick",
            WS_CAPTION | WS_SYSMENU | WS_POPUP, o.left + S(30), o.top + S(20), r.right - r.left, r.bottom - r.top,
            owner, NULL, inst, NULL);
    }

    c = CHILD(L"BUTTON", L"Joystick", BS_GROUPBOX, 12, 8, 560, 322, 0);
    FONT(c);

    c = CHILD(L"STATIC", L"Use Joystick", SS_CENTERIMAGE, 24, 28, 100, 24, 0);
    FONT(c);
    state.yes = CHILD(L"BUTTON", L"Yes", BS_CHECKBOX | WS_TABSTOP, 128, 28, 56, 24, ID_USE_YES);
    state.no = CHILD(L"BUTTON", L"No", BS_CHECKBOX | WS_TABSTOP, 192, 28, 56, 24, ID_USE_NO);
    FONT(state.yes);
    FONT(state.no);

    c = CHILD(L"STATIC", L"Choose the controller button for each action. The left stick also moves.", 0, 24, 58, 536, 20, 0);
    FONT(c);

    /* two columns of seven actions */
    for (i = 0; i < BIND_COUNT; i++)
    {
        int x = 24 + (i / 7) * 276;
        int yy = 84 + (i % 7) * 32;
        HWND label = CHILD(L"STATIC", action_labels[i], SS_CENTERIMAGE, x, yy, 80, 26, 0);
        int b;

        state.combos[i] = CHILD(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, x + 84, yy, 172, 320,
            ID_BUTTON0 + i);

        for (b = 0; b < JOY_BUTTON_COUNT; b++)
        {
            SendMessageW(state.combos[i], CB_ADDSTRING, 0, (LPARAM)button_labels[b]);
        }

        FONT(label);
        FONT(state.combos[i]);
    }

    c = CHILD(L"STATIC", L"Plug the controller in before starting the game.",
        0, 24, 304, 536, 18, 0);
    FONT(c);

    show_all();

    c = CHILD(L"BUTTON", L"Defaults", WS_TABSTOP | BS_PUSHBUTTON, 12, 340, 84, 30, ID_DEFAULTS);
    FONT(c);
    c = CHILD(L"BUTTON", L"OK", WS_TABSTOP | BS_DEFPUSHBUTTON, 396, 340, 84, 30, ID_OK);
    FONT(c);
    c = CHILD(L"BUTTON", L"Cancel", WS_TABSTOP | BS_PUSHBUTTON, 488, 340, 84, 30, ID_CANCEL);
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
        s->joystick = state.work.joystick;
        memcpy(s->joy, state.work.joy, sizeof(s->joy));
        return 1;
    }

    return 0;
}
