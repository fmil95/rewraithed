/*
 * Interrupts: root counter 1 events and critical sections.
 *
 * The sound sequencer (AADLIB.c) installs its update function as the event
 * of root counter 1, counting horizontal blanks, so it runs at a fixed rate
 * (262 hblanks = 60 Hz in the game's default mode) whatever the frame rate.
 * PsyCross implements neither events nor counter interrupts, so a thread
 * here plays the interrupt: it calls the event function on schedule and
 * delivers the SPU's transfer completion.
 *
 * EnterCriticalSection/ExitCriticalSection, which disable interrupts on the
 * PSX, take and release a lock this thread holds while it runs a handler.
 * As on the PSX they do not nest, and inside a handler they do nothing.
 *
 * Root counter 0 (the game clock) is emulated on read, see
 * port/src/native/root_counter.c; its event needs no thread.
 */
#include "common.h"

#include <SDL.h>

#undef GetRCnt
#undef SetRCnt
#undef StartRCnt
#undef StopRCnt
#undef OpenEvent
#undef EnableEvent
#undef DisableEvent
#undef CloseEvent

/* PsyCross's versions, for the counters not emulated here */
extern int GetRCnt(int spec);
extern int SetRCnt(int spec, unsigned short target, int mode);
extern int StartRCnt(int spec);
extern int StopRCnt(int spec);

#define HSYNC_RATE 15734.26
#define EVENT_HANDLE(counter) (0xF1000000u | (counter))

unsigned int port_rcnt0_read(void);
void port_rcnt0_start(void);
void port_rcnt0_stop(void);

static SDL_mutex *irq_lock;
static volatile SDL_threadID irq_owner;
static volatile int irq_held;

/* per thread: inside an interrupt handler (depth), and whether entering it
 * took the lock */
static __declspec(thread) int irq_handler_depth;
static __declspec(thread) int irq_handler_locked;

static long (*rc1_func)();
static volatile int rc1_enabled;
static volatile int rc1_running;
static volatile unsigned short rc1_target = 262;
static volatile Uint64 rc1_restart;

static SDL_Thread *irq_thread;
unsigned int port_rc1_ticks;

static int irq_owned_here(void)
{
    return irq_held && (irq_owner == SDL_ThreadID());
}

/* Interrupt context: the timer thread here, and the emulated CD interrupt
 * (port/src/cd_drive.c) on the game thread. Handlers exclude each other and
 * critical sections, and critical sections inside them do nothing. */
void port_irq_handler_begin(void)
{
    if (irq_handler_depth++ > 0)
    {
        return;
    }

    irq_handler_locked = 0;

    if ((irq_lock != NULL) && !irq_owned_here())
    {
        SDL_LockMutex(irq_lock);
        irq_owner = SDL_ThreadID();
        irq_held = 1;
        irq_handler_locked = 1;
    }
}

void port_irq_handler_end(void)
{
    if (--irq_handler_depth > 0)
    {
        return;
    }

    if (irq_handler_locked)
    {
        irq_handler_locked = 0;
        irq_held = 0;
        SDL_UnlockMutex(irq_lock);
    }
}

int EnterCriticalSection(void)
{
    if ((irq_lock == NULL) || (irq_handler_depth > 0) || irq_owned_here())
    {
        return 1;
    }

    SDL_LockMutex(irq_lock);
    irq_owner = SDL_ThreadID();
    irq_held = 1;

    return 1;
}

void ExitCriticalSection(void)
{
    if ((irq_lock == NULL) || (irq_handler_depth > 0))
    {
        return;
    }

    if (irq_owned_here())
    {
        irq_held = 0;
        SDL_UnlockMutex(irq_lock);
    }
}

int port_is_paused(void);  /* focus_pause.c */

static int SDLCALL irq_main(void *unused)
{
    Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 next = 0;
    Uint64 seen_restart = 0;

    (void)unused;

    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH);

    for (;;)
    {
        Uint64 now = SDL_GetPerformanceCounter();
        Uint64 period = (Uint64)((double)freq * rc1_target / HSYNC_RATE);

        /* the window is hidden (focus_pause.c): the game is paused */
        if (port_is_paused())
        {
            next = now + period;
            SDL_Delay(5);
            continue;
        }

        if (seen_restart != rc1_restart)
        {
            seen_restart = rc1_restart;
            next = now + period;
        }

        port_irq_handler_begin();

        port_spu_poll_irq(0);

        if (rc1_running && rc1_enabled && (rc1_func != NULL) && (now >= next))
        {
            /* catch up a little after a stall, but not with a burst */
            if (now - next > period * 4)
            {
                next = now;
            }

            rc1_func();
            port_rc1_ticks++;
            next += period;
        }

        port_irq_handler_end();

        SDL_Delay(1);
    }

    return 0;
}

static void irq_start_thread(void)
{
    if (irq_thread == NULL)
    {
        irq_thread = SDL_CreateThread(irq_main, "rewraithed-irq", NULL);
    }
}

/* PsyQ initialises the interrupt system before the game's main runs */
void port_irq_init(void)
{
    irq_lock = SDL_CreateMutex();
    irq_start_thread();
}

/* --- root counters --------------------------------------------------------- */

#define RCNT_INDEX(spec) ((spec) & 0xFFFF)

int port_GetRCnt(int spec)
{
    if (RCNT_INDEX(spec) == 0)
    {
        return port_rcnt0_read();
    }

    return GetRCnt(spec);
}

int port_SetRCnt(int spec, unsigned short target, int mode)
{
    if (RCNT_INDEX(spec) == 1)
    {
        rc1_target = (target != 0) ? target : 1;
        rc1_restart++;
        return 1;
    }

    return SetRCnt(spec, target, mode);
}

int port_StartRCnt(int spec)
{
    switch (RCNT_INDEX(spec))
    {
    case 0:
        port_rcnt0_start();
        break;
    case 1:
        rc1_restart++;
        rc1_running = 1;
        return 1;
    }

    return StartRCnt(spec);
}

int port_StopRCnt(int spec)
{
    switch (RCNT_INDEX(spec))
    {
    case 0:
        port_rcnt0_stop();
        break;
    case 1:
        rc1_running = 0;
        return 1;
    }

    return StopRCnt(spec);
}

/* --- events ---------------------------------------------------------------
 * Only the root counter interrupts (class 0xF200000n) are used. */

int port_OpenEvent(unsigned int desc, int spec, int mode, long (*func)())
{
    (void)spec;
    (void)mode;

    if (desc == 0xF2000001)
    {
        rc1_func = func;
        rc1_enabled = 0;
    }

    return EVENT_HANDLE(desc & 0xF);
}

int port_EnableEvent(unsigned int event)
{
    if (event == EVENT_HANDLE(1))
    {
        rc1_enabled = 1;
    }

    return 1;
}

int port_DisableEvent(unsigned int event)
{
    if (event == EVENT_HANDLE(1))
    {
        rc1_enabled = 0;
    }

    return 1;
}

int port_CloseEvent(unsigned int event)
{
    if (event == EVENT_HANDLE(1))
    {
        rc1_enabled = 0;
        rc1_func = NULL;
    }

    return 1;
}
