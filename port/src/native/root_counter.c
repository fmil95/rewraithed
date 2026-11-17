/*
 * Root counter 0 and its interrupt, the game's clock.
 *
 * The game sets counter 0 (spec 0xF2000000) to count the 33.8688 MHz system
 * clock up to 0xFFFF, with an interrupt each time it wraps (about every
 * 1.94 ms) whose handler, TimerTick, increments gameTimer. Timestamps are
 * (counter | gameTimer << 16), and TIMER_TimeDiff turns two of them into the
 * frame time that drives animation and physics. PsyCross neither advances
 * the counter nor delivers the interrupt.
 *
 * Here the count comes from the performance counter. A read first delivers
 * the wraps that have happened since the last one (gameTimer++ per wrap) and
 * then returns the low 16 bits, so counter and gameTimer always agree, as
 * they do on hardware. Reads happen on the game thread only.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define PSX_SYSTEM_CLOCK 33868800ull

extern volatile int gameTimer;  /* port/src/hasm/hasm_data.c */

static LARGE_INTEGER rc_freq;
static LARGE_INTEGER rc_start;
static unsigned long long rc_wraps;
static int rc_running;

static unsigned long long rc_ticks(void)
{
    LARGE_INTEGER now;
    unsigned long long elapsed;

    QueryPerformanceCounter(&now);
    elapsed = (unsigned long long)(now.QuadPart - rc_start.QuadPart);

    /* elapsed * clock / freq, without overflowing */
    return (elapsed / rc_freq.QuadPart) * PSX_SYSTEM_CLOCK +
        ((elapsed % rc_freq.QuadPart) * PSX_SYSTEM_CLOCK) / rc_freq.QuadPart;
}

void port_rcnt0_start(void)
{
    QueryPerformanceFrequency(&rc_freq);
    QueryPerformanceCounter(&rc_start);
    rc_wraps = 0;
    rc_running = 1;
}

void port_rcnt0_stop(void)
{
    rc_running = 0;
}

unsigned int port_rcnt0_read(void)
{
    unsigned long long ticks;

    if (!rc_running)
    {
        return 0;
    }

    ticks = rc_ticks();

    while (rc_wraps < (ticks >> 16))
    {
        rc_wraps++;
        gameTimer++;
    }

    return (unsigned int)(ticks & 0xFFFF);
}
