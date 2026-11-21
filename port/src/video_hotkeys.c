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
 *  F11  Dreamcast models: on, off (with the models imported)
 *
 * Each press moves to the next choice, for this session only: the launcher's
 * settings are not changed. PsyCross keeps F3 (texture filtering), F10 (VRAM
 * dump, Debug builds) and F12 (screenshot).
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
void port_dc_set_enabled(int on);           /* dc_models.c */

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
        printf("[rewraithed] Scaling: %s\n", port_settings.scaleLinear ? "Bilinear" : "Nearest");
        break;

    case SDL_SCANCODE_F5:
        port_settings.vsync = !port_settings.vsync;
        g_cfg_swapInterval = port_settings.vsync;
        port_setup_vsync_pacing(port_settings.vsync);
        printf("[rewraithed] VSync: %s\n", on_off(port_settings.vsync));
        break;

    case SDL_SCANCODE_F6:
        port_settings.fpsLimit = (port_settings.fpsLimit == 30) ? 60 : 30;
        port_setup_vsync_pacing(port_settings.vsync);
        printf("[rewraithed] FPS limit: %d\n", port_settings.fpsLimit);
        break;

    case SDL_SCANCODE_F7:
        port_settings.pgxp = !port_settings.pgxp;
        g_port_pgxp = port_settings.pgxp;
        printf("[rewraithed] PGXP: %s\n", on_off(port_settings.pgxp));
        break;

    case SDL_SCANCODE_F8:
        port_settings.pillarbox = !port_settings.pillarbox;
        g_cfg_pillarbox = port_settings.pillarbox;
        printf("[rewraithed] Pillarboxing: %s\n", on_off(port_settings.pillarbox));
        break;

    case SDL_SCANCODE_F9:
        port_settings.dithering = !port_settings.dithering;
        g_cfg_dithering = port_settings.dithering;
        PsyX_PortRebuildShaders();
        printf("[rewraithed] Dithering: %s\n", on_off(port_settings.dithering));
        break;

    case SDL_SCANCODE_F11:
        port_dc_set_enabled(!port_settings.dcModels);
        printf("[rewraithed] Dreamcast models: %s\n", on_off(port_settings.dcModels));
        break;
    }
}
