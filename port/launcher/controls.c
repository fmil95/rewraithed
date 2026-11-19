/*
 * Keyboard and Mouse window. Each action has two key buttons, its key and an
 * alternate one; clicking one waits for the next key press (clicking it again
 * cancels, Backspace clears it, as Backspace is the debug menu's key). A key already
 * used elsewhere is swapped with it, so no two buttons share a key. Keys are
 * stored by their SDL scancode names, which the game looks up. Below, a
 * mouse button for each mouse action.
 */
#include <stdio.h>
#include <string.h>

#include "controls.h"

#include <commctrl.h>

/* the actions of the PSX buttons, in the order of BIND_* */
static const wchar_t *const action_labels[BIND_COUNT] = {
    L"Up", L"Down", L"Left", L"Right",
    L"Jump", L"Feed", L"Attack", L"Telekinesis",
    L"Crouch", L"Look Right", L"Stealth", L"Look Left",
    L"Glyph Menu", L"Pause Menu",
};

static const wchar_t *const mouse_labels[MOUSE_COUNT] = { L"Attack", L"Telekinesis", L"Aim" };

static const wchar_t *const mouse_button_labels[MOUSE_BUTTON_COUNT] = {
    L"None", L"Left Mouse", L"Middle Mouse", L"Right Mouse", L"Mouse 4", L"Mouse 5",
};

#define SLOTS (BIND_COUNT * 2)  /* keys, then alternate keys */

enum { ID_KEY0 = 200, ID_MOUSE0 = 260, ID_OK = 300, ID_CANCEL, ID_DEFAULTS };

typedef struct
{
    PortSettings work;
    HWND keyButtons[SLOTS];
    HWND mouseCombos[MOUSE_COUNT];
    int capture;            /* slot waiting for a key, or -1 */
    int done;               /* 1 OK, -1 cancel */
} ControlsState;

static ControlsState *cs;

static char *slot_key(int slot)
{
    return (slot < BIND_COUNT) ? cs->work.keys[slot] : cs->work.keys2[slot - BIND_COUNT];
}

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

    if ((vk >= VK_F1) && (vk <= VK_F11))
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
    case VK_TAB:        return "Tab";
    case VK_ESCAPE:     return "Escape";
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

/* how a key name is shown: "Return" is the Enter key */
static void key_text(const char *name, wchar_t *text, int size)
{
    if (name[0] == '\0')
    {
        wcscpy(text, L"-");
    }
    else if (_stricmp(name, "Return") == 0)
    {
        wcscpy(text, L"Enter");
    }
    else if (_stricmp(name, "Keypad Enter") == 0)
    {
        wcscpy(text, L"Keypad Enter");
    }
    else
    {
        MultiByteToWideChar(CP_UTF8, 0, name, -1, text, size);
    }
}

static void show_key(int slot)
{
    wchar_t text[64];

    if (cs->capture == slot)
    {
        wcscpy(text, L"Press a key...");
    }
    else
    {
        key_text(slot_key(slot), text, 64);
    }

    SetWindowTextW(cs->keyButtons[slot], text);
}

static void assign_key(int slot, const char *name)
{
    int j;

    /* swap with a button already using that key */
    for (j = 0; j < SLOTS; j++)
    {
        if ((j != slot) && (name[0] != '\0') && (_stricmp(slot_key(j), name) == 0))
        {
            strcpy(slot_key(j), slot_key(slot));
            show_key(j);
        }
    }

    snprintf(slot_key(slot), 24, "%s", name);
}

static void show_mouse(int i)
{
    SendMessageW(cs->mouseCombos[i], CB_SETCURSEL, cs->work.mouse[i], 0);
}

static void show_all(void)
{
    int i;

    for (i = 0; i < SLOTS; i++)
    {
        show_key(i);
    }

    for (i = 0; i < MOUSE_COUNT; i++)
    {
        show_mouse(i);
    }
}

static LRESULT CALLBACK controls_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_COMMAND:
    {
        int id = LOWORD(wp);

        if ((id >= ID_KEY0) && (id < ID_KEY0 + SLOTS))
        {
            int old = cs->capture;

            /* clicked again: cancel */
            cs->capture = (old == id - ID_KEY0) ? -1 : id - ID_KEY0;

            if (old >= 0)
            {
                show_key(old);
            }

            if (cs->capture >= 0)
            {
                show_key(cs->capture);
            }

            SetFocus(hwnd);
        }
        else if ((id >= ID_MOUSE0) && (id < ID_MOUSE0 + MOUSE_COUNT) && (HIWORD(wp) == CBN_SELCHANGE))
        {
            int i = id - ID_MOUSE0;
            int b = (int)SendMessageW(cs->mouseCombos[i], CB_GETCURSEL, 0, 0);
            int j;

            /* swap with an action already on that button */
            for (j = 0; j < MOUSE_COUNT; j++)
            {
                if ((j != i) && (b != MOUSE_NONE) && (cs->work.mouse[j] == b))
                {
                    cs->work.mouse[j] = cs->work.mouse[i];
                    show_mouse(j);
                }
            }

            cs->work.mouse[i] = b;
        }
        else if (id == ID_DEFAULTS)
        {
            PortSettings d;

            port_settings_defaults(&d);
            memcpy(cs->work.keys, d.keys, sizeof(d.keys));
            memcpy(cs->work.keys2, d.keys2, sizeof(d.keys2));
            memcpy(cs->work.mouse, d.mouse, sizeof(d.mouse));
            cs->capture = -1;
            show_all();
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
    int y;

#define S(v) MulDiv((v), dpi, 96)
#define CHILD(cls, text, style, x, y, w, h, id) \
    CreateWindowW(cls, text, WS_CHILD | WS_VISIBLE | (style), S(x), S(y), S(w), S(h), hwnd, (HMENU)(INT_PTR)(id), inst, NULL)

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
    r.right = S(584);
    r.bottom = S(490);
    AdjustWindowRect(&r, WS_CAPTION | WS_SYSMENU, FALSE);

    {
        RECT o;

        GetWindowRect(owner, &o);

        hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"RewraithedControls", L"Keyboard and Mouse",
            WS_CAPTION | WS_SYSMENU | WS_POPUP, o.left + S(30), o.top + S(20), r.right - r.left, r.bottom - r.top,
            owner, NULL, inst, NULL);
    }

    /* --- keyboard: two columns of seven actions, key and alternate key --- */
    {
        HWND c = CHILD(L"BUTTON", L"Keyboard", BS_GROUPBOX, 12, 8, 560, 322, 0);

        SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);

        c = CHILD(L"STATIC", L"Click a key, then press the key to use for it. Click it again to cancel; Backspace clears it.",
            0, 24, 28, 536, 20, 0);
        SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
    }

    for (i = 0; i < 2; i++)
    {
        int x = 24 + i * 276;
        HWND c;

        c = CHILD(L"STATIC", L"Key", SS_CENTER, x + 84, 52, 84, 18, 0);
        SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
        c = CHILD(L"STATIC", L"Alternate", SS_CENTER, x + 172, 52, 84, 18, 0);
        SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
    }

    for (i = 0; i < BIND_COUNT; i++)
    {
        int x = 24 + (i / 7) * 276;
        int yy = 72 + (i % 7) * 32;
        HWND label = CHILD(L"STATIC", action_labels[i], SS_CENTERIMAGE, x, yy, 80, 26, 0);

        state.keyButtons[i] = CHILD(L"BUTTON", L"", WS_TABSTOP | BS_PUSHBUTTON, x + 84, yy, 84, 26, ID_KEY0 + i);
        state.keyButtons[BIND_COUNT + i] = CHILD(L"BUTTON", L"", WS_TABSTOP | BS_PUSHBUTTON, x + 172, yy, 84, 26,
            ID_KEY0 + BIND_COUNT + i);

        SendMessageW(label, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(state.keyButtons[i], WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(state.keyButtons[BIND_COUNT + i], WM_SETFONT, (WPARAM)font, TRUE);
    }

    {
        HWND c = CHILD(L"STATIC", L"In the menus, Enter also selects. Backspace opens the debug menu, when it is enabled.", 0, 24, 302, 536, 18, 0);

        SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
    }

    /* --- mouse --- */
    {
        HWND c = CHILD(L"BUTTON", L"Mouse", BS_GROUPBOX, 12, 338, 560, 96, 0);

        SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
    }

    for (i = 0; i < MOUSE_COUNT; i++)
    {
        int x = 24 + (i % 2) * 276;
        int yy = 362 + (i / 2) * 34;
        HWND label = CHILD(L"STATIC", mouse_labels[i], SS_CENTERIMAGE, x, yy, 80, 26, 0);
        int b;

        state.mouseCombos[i] = CHILD(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, x + 84, yy, 172, 200,
            ID_MOUSE0 + i);

        for (b = 0; b < MOUSE_BUTTON_COUNT; b++)
        {
            SendMessageW(state.mouseCombos[i], CB_ADDSTRING, 0, (LPARAM)mouse_button_labels[b]);
        }

        SendMessageW(label, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(state.mouseCombos[i], WM_SETFONT, (WPARAM)font, TRUE);
    }

    show_all();

    /* --- buttons --- */
    y = 448;

    {
        HWND b;

        b = CHILD(L"BUTTON", L"Defaults", WS_TABSTOP | BS_PUSHBUTTON, 12, y, 84, 30, ID_DEFAULTS);
        SendMessageW(b, WM_SETFONT, (WPARAM)font, TRUE);

        b = CHILD(L"BUTTON", L"OK", WS_TABSTOP | BS_DEFPUSHBUTTON, 396, y, 84, 30, ID_OK);
        SendMessageW(b, WM_SETFONT, (WPARAM)font, TRUE);

        b = CHILD(L"BUTTON", L"Cancel", WS_TABSTOP | BS_PUSHBUTTON, 488, y, 84, 30, ID_CANCEL);
        SendMessageW(b, WM_SETFONT, (WPARAM)font, TRUE);
    }

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

        /* a key for the button waiting for one */
        if ((state.capture >= 0) && ((msg.message == WM_KEYDOWN) || (msg.message == WM_SYSKEYDOWN)))
        {
            char buf[32];
            const char *name;
            int slot = state.capture;

            if (msg.wParam == VK_BACK)
            {
                state.capture = -1;
                slot_key(slot)[0] = '\0';
                show_key(slot);
                continue;
            }

            name = sdl_key_name(msg.wParam, msg.lParam, buf, sizeof(buf));

            if (name != NULL)
            {
                state.capture = -1;
                assign_key(slot, name);
                show_key(slot);
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
        memcpy(s->keys2, state.work.keys2, sizeof(s->keys2));
        memcpy(s->mouse, state.work.mouse, sizeof(s->mouse));
        return 1;
    }

    return 0;
}
