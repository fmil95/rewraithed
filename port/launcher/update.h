/*
 * The launcher's Update button (update.c): newer releases from GitHub.
 */
#ifndef REWRAITHED_UPDATE_H
#define REWRAITHED_UPDATE_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* posted to the window given to update_check_async: wParam 1 if a newer
 * release is out */
#define WM_APP_UPDATE (WM_APP + 1)

/* leftovers of an update (the files it replaced, *.old) */
void update_cleanup(void);

/* checks in the background; posts WM_APP_UPDATE to hwnd when done */
void update_check_async(HWND hwnd);

/* the Update button: checks (if the background check has not found one),
 * asks, then downloads, installs and restarts the launcher. Returns 1 when
 * the launcher should close for the new one. */
int update_run(HWND owner);

#endif
