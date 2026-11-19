/*
 * Keyboard shortcuts that depend on what the game is doing, called from
 * fixups in GAMEPAD.c and GAMELOOP.c:
 *
 *  - in the menus (logos, press start, main menu, pause and debug menus),
 *    Enter also selects, as Cross does (port/psycross/psyx_port.cpp reads
 *    g_port_menu_enter);
 *  - Backspace opens and closes the debug menu, when it is enabled, without
 *    pressing any pad button, so Raziel does not attack or feed;
 *  - while Raziel aims or looks around, moving the mouse turns his view, as
 *    the analog stick does (RAZIEL_TurnHead). The mouse is captured
 *    (relative mode, pointer hidden) until he stops.
 */
#include <SDL.h>

#include "common.h"
#include "settings.h"

extern GameTracker gameTrackerX;
extern MainTracker mainTrackerX;

extern int g_port_menu_enter;   /* port/psycross/psyx_port.cpp */

/* look rotation (4096 a turn) for each pixel of mouse movement, in
 * quarters: 2 is half a unit a pixel */
#define MOUSE_LOOK_QUARTERS 2

/* movement not yet turned into whole units, in quarters */
static int mouse_look_rest_x;
static int mouse_look_rest_z;

/* frames left before the mouse is released: 2 when RAZIEL_TurnHead ran
 * this frame, counted down at the start of each frame */
static int mouse_look_frames;

int port_has_flag(const char *name);

/* GAMEPAD_Process, before the pad is read */
void port_input_frame(void)
{
    g_port_menu_enter = (mainTrackerX.mainState != 1) ||
        (gameTrackerX.gameMode == 4) ||     /* debug menu */
        (gameTrackerX.gameMode == 6);       /* pause menu */

    /* no longer aiming or looking around: give the pointer back */
    if ((mouse_look_frames > 0) && (--mouse_look_frames == 0))
    {
        SDL_SetRelativeMouseMode(SDL_FALSE);
    }
}

/* RAZIEL_TurnHead, each frame of aiming or looking around: the mouse
 * movement since the last frame, added to his look rotation */
void port_mouse_look(short *rotx, short *rotz)
{
    int dx = 0;
    int dy = 0;

    /* the game's window (its only one) must have the focus */
    if (SDL_GetKeyboardFocus() == NULL)
    {
        return;
    }

    if (mouse_look_frames == 0)
    {
        SDL_SetRelativeMouseMode(SDL_TRUE);

        /* drop the movement from before */
        SDL_GetRelativeMouseState(NULL, NULL);
        mouse_look_rest_x = 0;
        mouse_look_rest_z = 0;
    }
    else
    {
        SDL_GetRelativeMouseState(&dx, &dy);
    }

    mouse_look_frames = 2;

    /* as the stick: right turns right, down looks down; what is left of a
     * unit is kept for the next frame, so slow movements still turn */
    mouse_look_rest_z += dx * MOUSE_LOOK_QUARTERS;
    mouse_look_rest_x += dy * MOUSE_LOOK_QUARTERS;

    *rotz -= (short)(mouse_look_rest_z / 4);
    *rotx += (short)(mouse_look_rest_x / 4);

    mouse_look_rest_z %= 4;
    mouse_look_rest_x %= 4;
}

/* GAMELOOP_ChangeMode: 1 once for each press of Backspace */
int port_debug_key_pressed(void)
{
    static int wasDown;
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    int down = (keys != NULL) && keys[SDL_SCANCODE_BACKSPACE];
    int pressed = down && !wasDown;

    wasDown = down;

    return pressed && (port_settings.debugMenu || port_has_flag("-debugmenu"));
}
