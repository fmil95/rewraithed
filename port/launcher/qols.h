/*
 * The launcher's QoLs window: quality-of-life options the original game
 * lacks (port/src/qol.c).
 */
#ifndef REWRAITHED_LAUNCHER_QOLS_H
#define REWRAITHED_LAUNCHER_QOLS_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "settings.h"

/* Modal; returns 1 and updates s's QoL options on OK, 0 on Cancel. */
int qols_dialog(HWND owner, PortSettings *s, HFONT font, int dpi);

#endif
