/*
 * The launcher's Joystick window: the joystick on or off, and the
 * controller button of each action.
 */
#ifndef REWRAITHED_LAUNCHER_JOYSTICK_H
#define REWRAITHED_LAUNCHER_JOYSTICK_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "settings.h"

/* Modal; returns 1 and updates s->joystick and s->joy on OK, 0 on Cancel. */
int joystick_dialog(HWND owner, PortSettings *s, HFONT font, int dpi);

#endif
