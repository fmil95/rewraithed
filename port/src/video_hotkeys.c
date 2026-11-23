/*
 * Video settings on function keys, while playing (PsyCross's own F4-F6,
 * which moved the keyboard to another controller port and toggled a PGXP
 * this build does not have, give way to these; fixup in PsyX_main.cpp):
 *
 *  F4   Scaling: Nearest, Bilinear (movies and full-screen 2D pictures,
 *       such as the legal and loading screens; not the 3D game, F3 is that)
 *  F5   VSync: on, off
 *  F6   FPS limit: 30, 60
 *  F7   PGXP: on, off
 *  F8   Pillarboxing: on, off
 *  F9   Dithering: on, off
 *
 *  Alt+Enter  Display: Fullscreen, Windowed, Borderless
 *
 * F11 (Dreamcast models on and off) is left out for now: switching them in
 * the middle of a scene could crash (pillars1's cutscene) or corrupt
 * textures. The launcher's Use DC Models still applies from the start.
 *
 * Each press moves to the next choice, for this session only: the launcher's
 * settings are not changed. PsyCross keeps F3 (texture filtering), F10 (VRAM
 * dump, Debug builds) and F12 (screenshot). What a key changed is shown in
 * the upper left corner for a moment (osd.c).
 */
#include <stdio.h>

#include <SDL.h>

#include "settings.h"
#include "port/gpu_hooks.h"

extern int g_cfg_swapInterval;              /* PsyCross: VSync */
extern int g_port_pgxp;                     /* port/psycross/psyx_port.cpp */
extern int g_cfg_pillarbox;
extern int g_cfg_dithering;

void PsyX_PortRebuildShaders(void);         /* PsyX_render.cpp (fixup) */
void PsyX_PortSetWindowed(int width, int height);   /* port/psycross/psyx_port.cpp */
void PsyX_PortSetBorderless(void);
void PsyX_PortSetFullscreenMode(int width, int height, int refresh);
void port_osd_note(const char *fmt, ...);   /* osd.c */

static const char *on_off(int on)
{
    return on ? "on" : "off";
}

void PsyX_PortVideoHotkey(int scancode)
{
    switch (scancode)
    {
    case SDL_SCANCODE_F4:
        port_settings.scaleLinear = !port_settings.scaleLinear;
        port_osd_note("Scaling: %s", port_settings.scaleLinear ? "Bilinear" : "Nearest");
        break;

    case SDL_SCANCODE_F5:
        port_settings.vsync = !port_settings.vsync;
        g_cfg_swapInterval = port_settings.vsync;
        port_setup_vsync_pacing(port_settings.vsync);
        port_osd_note("VSync: %s", on_off(port_settings.vsync));
        break;

    case SDL_SCANCODE_F6:
        port_settings.fpsLimit = (port_settings.fpsLimit == 30) ? 60 : 30;
        port_setup_vsync_pacing(port_settings.vsync);
        port_osd_note("FPS limit: %d", port_settings.fpsLimit);
        break;

    case SDL_SCANCODE_F7:
        port_settings.pgxp = !port_settings.pgxp;
        g_port_pgxp = port_settings.pgxp;
        port_osd_note("PGXP: %s", on_off(port_settings.pgxp));
        break;

    case SDL_SCANCODE_F8:
        port_settings.pillarbox = !port_settings.pillarbox;
        g_cfg_pillarbox = port_settings.pillarbox;
        port_osd_note("Pillarboxing: %s", on_off(port_settings.pillarbox));
        break;

    case SDL_SCANCODE_F9:
        port_settings.dithering = !port_settings.dithering;
        g_cfg_dithering = port_settings.dithering;
        PsyX_PortRebuildShaders();
        port_osd_note("Dithering: %s", on_off(port_settings.dithering));
        break;
    }
}

/* Alt+Enter (fixup in PsyX_main.cpp): Fullscreen, then Windowed, then
 * Borderless, then Fullscreen again, at the launcher's Resolution */
void PsyX_PortCycleDisplay(void)
{
    switch (port_settings.display)
    {
    case DISPLAY_FULLSCREEN:
        port_settings.display = DISPLAY_WINDOWED;
        PsyX_PortSetWindowed(port_settings.width, port_settings.height);
        port_osd_note("Display: Windowed");
        break;

    case DISPLAY_WINDOWED:
        port_settings.display = DISPLAY_BORDERLESS;
        PsyX_PortSetBorderless();
        port_osd_note("Display: Borderless");
        break;

    default:
        port_settings.display = DISPLAY_FULLSCREEN;
        PsyX_PortSetFullscreenMode(port_settings.width, port_settings.height, port_settings.refresh);
        port_osd_note("Display: Fullscreen");
        break;
    }

    /* the frame pacing follows the display's refresh rate */
    port_setup_vsync_pacing(port_settings.vsync);
}
