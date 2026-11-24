/*
 * The launcher's earlier areas: Extract Earlier Areas unpacks the prototypes
 * in the proto folder and extracts their maps; Load Earlier Areas picks a
 * prototype and one of its maps to start in (port/shared/proto_areas.h).
 */
#ifndef REWRAITHED_LAUNCHER_EARLIER_H
#define REWRAITHED_LAUNCHER_EARLIER_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "settings.h"

/* Modal while it works, then tells what it did. */
void earlier_extract_dialog(HWND owner, HFONT font, int dpi);

/* Modal; returns 1 and sets s->earlierAreas and s->area on Load, 0 on Cancel. */
int earlier_load_dialog(HWND owner, PortSettings *s, HFONT font, int dpi);

/* "Jun 18, 1999" for "1999-06-18" */
void earlier_date_text(const char *date, wchar_t *out, int size);

#endif
