/*
 * Controls window. Each PSX button has a button showing its key; clicking
 * one waits for the next key press (Escape cancels). A key already used by
 * another button is swapped with it, so no two buttons share a key. Keys
 * are stored by their SDL scancode names, which the game looks up.
 */
#include <stdio.h>
#include <string.h>

#include "controls.h"

static const wchar_t *const button_labels[BIND_COUNT] = {
    L"D-Pad Up", L"D-Pad Down", L"D-Pad Left", L"D-Pad Right",
    L"Cross", L"Circle", L"Square", L"Triangle",
    L"L1", L"L2", L"R1", L"R2",
    L"Select", L"Start",
};

enum { ID_KEY0 = 200, ID_OK = 300, ID_CANCEL, ID_DEFAULTS };

typedef struct
{
    PortSettings work;
    HWND keyButtons[BIND_COUNT];
    int capture;            /* index waiting for a key, or -1 */
    int done;               /* 1 OK, -1 cancel */
} ControlsState;

static ControlsState *cs;

/* SDL scancode name for a Win32 key message, or NULL if not bindable */
static const char *sdl_key_name(WPARAM vk, LPARAM lp, char *buf, size_t size)
{
    int scan = (int)((lp >> 16) & 0xFF);
    int ext = (int)((lp >> 24) & 1);

    if ((vk >= 'A') && (vk <= 'Z'))
    {
        snprintf(buf, size, "%c", (char)vk);
        return buf;
    }

    if ((vk >= '0') && (vk <= '9'))
    {
        snprintf(buf, size, "%c", (char)vk);
        return buf;
    }

    if ((vk >= VK_F1) && (vk <= VK_F12))
    {
        snprintf(buf, size, "F%d", (int)(vk - VK_F1 + 1));
        return buf;
    }

    if ((vk >= VK_NUMPAD0) && (vk <= VK_NUMPAD9))
    {
        snprintf(buf, size, "Keypad %d", (int)(vk - VK_NUMPAD0));
        return buf;
    }

    switch (vk)
    {
    case VK_SHIFT:      return (scan == 0x36) ? "Right Shift" : "Left Shift";
    case VK_CONTROL:    return ext ? "Right Ctrl" : "Left Ctrl";
    case VK_MENU:       return ext ? "Right Alt" : "Left Alt";
    case VK_RETURN:     return ext ? "Keypad Enter" : "Return";
    case VK_SPACE:      return "Space";
    case VK_BACK:       return "Backspace";
    case VK_TAB:        return "Tab";
    case VK_CAPITAL:    return "CapsLock";
    case VK_UP:         return "Up";
    case VK_DOWN:       return "Down";
    case VK_LEFT:       return "Left";
    case VK_RIGHT:      return "Right";
    case VK_INSERT:     return "Insert";
    case VK_DELETE:     return "Delete";
    case VK_HOME:       return "Home";
    case VK_END:        return "End";
    case VK_PRIOR:      return "PageUp";
    case VK_NEXT:       return "PageDown";
    case VK_MULTIPLY:   return "Keypad *";
    case VK_ADD:        return "Keypad +";
    case VK_SUBTRACT:   return "Keypad -";
    case VK_DECIMAL:    return "Keypad .";
    case VK_DIVIDE:     return "Keypad /";
    case VK_OEM_1:      return ";";
    case VK_OEM_PLUS:   return "=";
    case VK_OEM_COMMA:  return ",";
    case VK_OEM_MINUS:  return "-";
    case VK_OEM_PERIOD: return ".";
    case VK_OEM_2:      return "/";
    case VK_OEM_3:      return "`";
    case VK_OEM_4:      return "[";
    case VK_OEM_5:      return "\\";
    case VK_OEM_6:      return "]";
    case VK_OEM_7:      return "'";
    }

    return NULL;
}

static void show_key(int i)
{
    wchar_t text[64];

    if (cs->capture == i)
    {
        wcscpy(text, L"Press a key...");
    }
    else
    {
        MultiByteToWideChar(CP_UTF8, 0, cs->work.keys[i], -1, text, 64);
    }

    SetWindowTextW(cs->keyButtons[i], text);
}

static void assign_key(int i, const char *name)
{
    int j;

    /* swap with a button already using that key */
    for (j = 0; j < BIND_COUNT; j++)
    {
        if ((j != i) && (_stricmp(cs->work.keys[j], name) == 0))
        {
            strcpy(cs->work.keys[j], cs->work.keys[i]);
            show_key(j);
        }
    }

    snprintf(cs->work.keys[i], sizeof(cs->work.keys[i]), "%s", name);
}

static LRESULT CALLBACK controls_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_COMMAND:
    {
        int id = LOWORD(wp);

        if ((id >= ID_KEY0) && (id < ID_KEY0 + BIND_COUNT))
        {
            int old = cs->capture;

            cs->capture = id - ID_KEY0;

            if (old >= 0)
            {
                show_key(old);
            }

            show_key(cs->capture);
            SetFocus(hwnd);
        }
        else if (id == ID_DEFAULTS)
        {
            PortSettings d;
            int i;

            port_settings_defaults(&d);
            memcpy(cs->work.keys, d.keys, sizeof(d.keys));
            cs->capture = -1;

            for (i = 0; i < BIND_COUNT; i++)
            {
                show_key(i);
            }
        }
        else if (id == ID_OK)
        {
            cs->done = 1;
        }
        else if (id == ID_CANCEL)
        {
            cs->done = -1;
        }

        return 0;
    }
    case WM_CLOSE:
        cs->done = -1;
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

int controls_dialog(HWND owner, PortSettings *s, HFONT font, int dpi)
{
    static int registered;
    ControlsState state;
    HINSTANCE inst = GetModuleHandleW(NULL);
    HWND hwnd;
    RECT r;
    MSG msg;
    int i;

#define S(v) MulDiv((v), dpi, 96)

    if (!registered)
    {
        WNDCLASSW wc;

        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = controls_proc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"RewraithedControls";
        RegisterClassW(&wc);
        registered = 1;
    }

    memset(&state, 0, sizeof(state));
    state.work = *s;
    state.capture = -1;
    cs = &state;

    r.left = 0;
    r.top = 0;
    r.right = S(360);
    r.bottom = S(54 + 7 * 30 + 50);
    AdjustWindowRect(&r, WS_CAPTION | WS_SYSMENU, FALSE);

    {
        RECT o;

        GetWindowRect(owner, &o);

        hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"RewraithedControls", L"Controls",
            WS_CAPTION | WS_SYSMENU | WS_POPUP, o.left + S(40), o.top + S(40), r.right - r.left, r.bottom - r.top,
            owner, NULL, inst, NULL);
    }

    {
        HWND note = CreateWindowW(L"STATIC", L"Click a button, then press the key to use for it (Esc cancels).",
            WS_CHILD | WS_VISIBLE, S(12), S(10), S(336), S(32), hwnd, NULL, inst, NULL);

        SendMessageW(note, WM_SETFONT, (WPARAM)font, TRUE);
    }

    /* two columns of seven */
    for (i = 0; i < BIND_COUNT; i++)
    {
        int col = i / 7;
        int row = i % 7;
        int x = S(12 + col * 174);
        int y = S(46 + row * 30);
        HWND label = CreateWindowW(L"STATIC", button_labels[i], WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
            x, y, S(70), S(24), hwnd, NULL, inst, NULL);

        state.keyButtons[i] = CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            x + S(72), y, S(92), S(24), hwnd, (HMENU)(INT_PTR)(ID_KEY0 + i), inst, NULL);

        SendMessageW(label, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(state.keyButtons[i], WM_SETFONT, (WPARAM)font, TRUE);
        show_key(i);
    }

    {
        int y = S(46 + 7 * 30 + 12);
        HWND b;

        b = CreateWindowW(L"BUTTON", L"Defaults", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            S(12), y, S(80), S(26), hwnd, (HMENU)ID_DEFAULTS, inst, NULL);
        SendMessageW(b, WM_SETFONT, (WPARAM)font, TRUE);

        b = CreateWindowW(L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            S(184), y, S(80), S(26), hwnd, (HMENU)ID_OK, inst, NULL);
        SendMessageW(b, WM_SETFONT, (WPARAM)font, TRUE);

        b = CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            S(270), y, S(80), S(26), hwnd, (HMENU)ID_CANCEL, inst, NULL);
        SendMessageW(b, WM_SETFONT, (WPARAM)font, TRUE);
    }

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

        /* a key for the button waiting for one */
        if ((state.capture >= 0) && ((msg.message == WM_KEYDOWN) || (msg.message == WM_SYSKEYDOWN)))
        {
            char buf[32];
            const char *name;

            if (msg.wParam == VK_ESCAPE)
            {
                int i2 = state.capture;

                state.capture = -1;
                show_key(i2);
                continue;
            }

            name = sdl_key_name(msg.wParam, msg.lParam, buf, sizeof(buf));

            if (name != NULL)
            {
                int i2 = state.capture;

                state.capture = -1;
                assign_key(i2, name);
                show_key(i2);
            }

            continue;
        }

        if ((msg.message == WM_KEYDOWN) && (msg.wParam == VK_ESCAPE))
        {
            state.done = -1;
            continue;
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    EnableWindow(owner, TRUE);
    DestroyWindow(hwnd);
    SetForegroundWindow(owner);

    if (state.done == 1)
    {
        memcpy(s->keys, state.work.keys, sizeof(s->keys));
        return 1;
    }

    return 0;
}
