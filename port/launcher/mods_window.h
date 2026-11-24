/*
 * The launcher's Mods window: a list of the built-in mods (port_mods,
 * port/shared/mod_list.h) and the mod folders (port/shared/mod_folders.h),
 * each with a check, and the chosen one described beside it.
 */
#ifndef REWRAITHED_LAUNCHER_MODS_WINDOW_H
#define REWRAITHED_LAUNCHER_MODS_WINDOW_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "settings.h"

/* Modal; returns 1 and updates s->mods on OK, 0 on Cancel. */
int mods_dialog(HWND owner, PortSettings *s, HFONT font, int dpi);

#endif
