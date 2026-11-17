/*
 * PC wrappers for functions in soul-re's LOAD3D.c
 * (renamed there to <name>_psx through port/overrides.txt).
 */
#include "common.h"
#include "Game/LOAD3D.h"
#include "Game/STRMLOAD.h"
#include "settings.h"

#include <SDL.h>

void LOAD_ProcessReadQueue_psx(void);

/* The loader polls this while waiting for data; it is also where the
 * emulated CD drive gets to deliver its sectors. */
void LOAD_ProcessReadQueue(void)
{
    port_cd_pump();

    LOAD_ProcessReadQueue_psx();
}

/* The game loads the units next to the one Raziel enters in the
 * background, a few sectors per frame. With "Preload Chunks" they are all
 * loaded right away instead (at most 5 s), as at the start of a level. */
void port_preload_wait(void)
{
    Uint32 start;

    if (!port_settings.preloadChunks)
    {
        return;
    }

    start = SDL_GetTicks();

    while ((STREAM_PollLoadQueue() != 0) && ((SDL_GetTicks() - start) < 5000))
    {
    }
}
