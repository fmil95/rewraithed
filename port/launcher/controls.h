/*
 * The launcher's Controls window: shows the keyboard binding of each PSX
 * button and lets the user press a new key for it.
 */
#ifndef REWRAITHED_LAUNCHER_CONTROLS_H
#define REWRAITHED_LAUNCHER_CONTROLS_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "settings.h"

/* Modal; returns 1 and updates s->keys on OK, 0 on Cancel. */
int controls_dialog(HWND owner, PortSettings *s, HFONT font, int dpi);

#endif
