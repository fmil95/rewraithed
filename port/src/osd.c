/*
 * A short note in the upper left corner of the screen, written in the
 * game's own font: what a function key (video_hotkeys.c, PsyCross's F3 and
 * F12) or Alt+Enter just changed. It shows for two seconds, added to the
 * game's text as that is drawn (FONT_Flush, fixup), so it is seen wherever
 * the game draws text: playing, its menus and the pause menu; not over
 * movies or loading screens.
 */
#include "common.h"
#include "Game/FONT.h"

#include <stdarg.h>
#include <stdio.h>

#include <SDL.h>

#define OSD_SHOW_MS 2000

static char osd_text[64];
static Uint32 osd_since;

void port_osd_note(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(osd_text, sizeof(osd_text), fmt, ap);
    va_end(ap);

    printf("[rewraithed] %s\n", osd_text);
    osd_since = SDL_GetTicks();
}

/* the start of FONT_Flush: the note joins the text buffered this frame */
void port_osd_draw(void)
{
    long color;

    if ((osd_text[0] == '\0') || ((SDL_GetTicks() - osd_since) > OSD_SHOW_MS))
    {
        osd_text[0] = '\0';
        return;
    }

    color = fontTracker.color_global;
    FONT_SetColorIndex(0);
    FONT_SetCursor(10, 12);
    FONT_Print("%s", osd_text);
    fontTracker.color_global = color;
}
