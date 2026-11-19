/*
 * The launcher's Keyboard and Mouse window: the key and alternate key of
 * each action, and the mouse button of each mouse action.
 */
#ifndef REWRAITHED_LAUNCHER_CONTROLS_H
#define REWRAITHED_LAUNCHER_CONTROLS_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "settings.h"

/* Modal; returns 1 and updates s->keys, keys2 and mouse on OK, 0 on Cancel. */
int controls_dialog(HWND owner, PortSettings *s, HFONT font, int dpi);

#endif
