/*
 * The game stops while its window is hidden: minimized, or, in fullscreen
 * and borderless (where the window fills the screen), no longer the active
 * window (Alt+Tab). Sound stops with it and the game picks up where it was
 * when the window is back. A window in windowed mode keeps running while it
 * can be seen, even when another window is active.
 *
 * Checked once per frame on the main thread (port_frame_limit). While
 * paused, the audio device is paused and the threads that stand in for the
 * PSX interrupts (the vertical blank, port/src/native/vblank_thread.c, and
 * the sequencer's root counter, port/src/irq.c) skip their handlers, so the
 * music does not run on, and the CD and vblank counters do not count the
 * time away.
 */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>

#include "settings.h"

extern unsigned int PsyX_PortWindowFlags(void);  /* port/psycross/psyx_port.cpp */
void port_spu_pause(int pause);                   /* spu.c */

static volatile int paused;

int port_is_paused(void)
{
    return paused;
}

static int window_hidden(void)
{
    static int never = -1;
    unsigned int flags;

    /* REWRAITHED_NOPAUSE: keep running (automated runs) */
    if (never < 0)
    {
        never = (getenv("REWRAITHED_NOPAUSE") != NULL);
    }

    if (never)
    {
        return 0;
    }

    flags = PsyX_PortWindowFlags();

    if (flags == 0)
    {
        return 0;
    }

    if (flags & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN))
    {
        return 1;
    }

    return (port_settings.display != DISPLAY_WINDOWED) && !(flags & SDL_WINDOW_INPUT_FOCUS);
}

void port_focus_check(void)
{
    if (!window_hidden())
    {
        return;
    }

    printf("[rewraithed] window hidden: paused\n");

    paused = 1;
    port_spu_pause(1);

    while (window_hidden())
    {
        SDL_Event event;

        /* the window's own events (focus back, restore); closing it quits */
        while (SDL_PollEvent(&event))
        {
            if ((event.type == SDL_QUIT) || ((event.type == SDL_WINDOWEVENT) && (event.window.event == SDL_WINDOWEVENT_CLOSE)))
            {
                exit(0);
            }
        }

        SDL_Delay(30);
    }

    port_spu_pause(0);
    paused = 0;

    printf("[rewraithed] window back: resumed\n");
}
