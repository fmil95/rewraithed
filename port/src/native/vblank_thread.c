/*
 * The port's own vertical-blank "interrupt": a thread that calls the given
 * handler at the NTSC field rate. Independent from PsyCross's vsync callback
 * slot, which PsyCross clears internally (e.g. from SpuInit).
 */
#include <SDL.h>

static void (*vblank_handler)(void);
static SDL_Thread *vblank_thread;
static volatile int vblank_running;

static int vblank_main(void *unused)
{
    const Uint64 freq = SDL_GetPerformanceFrequency();
    const Uint64 period = (Uint64)((double)freq / 59.94);
    Uint64 next = SDL_GetPerformanceCounter() + period;

    (void)unused;

    while (vblank_running)
    {
        Uint64 now = SDL_GetPerformanceCounter();

        if (now < next)
        {
            Uint64 msLeft = ((next - now) * 1000) / freq;

            SDL_Delay((msLeft > 1) ? (Uint32)(msLeft - 1) : 0);
            continue;
        }

        next += period;

        /* after a long stall, do not replay the missed fields */
        if (now > next + period * 4)
        {
            next = now + period;
        }

        vblank_handler();
    }

    return 0;
}

void port_start_vblank_thread(void (*handler)(void))
{
    vblank_handler = handler;
    vblank_running = 1;
    vblank_thread = SDL_CreateThread(vblank_main, "rewraithed_vblank", NULL);
}
