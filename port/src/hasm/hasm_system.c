/*
 * C versions of the small system routines in HASM_1.s / HASM_2.s.
 */
#include "common.h"
#include "Game/HASM.h"

/* Zeroes .sbss/.bss on PSX; the PC loader already does that. */
void ClearBss(void)
{
}

/* Vertical-blank tick. */
long TimerTick(void)
{
    gameTimer++;
    return 0;
}

/* Identity function; being opaque to the compiler, it forces callers to
 * re-read values that interrupt handlers change. */
int CheckVolatile(void *value)
{
    return (int)(intptr_t)value;
}

/* $gp handling for interrupt callbacks: nothing to do on PC. */
void SetGp(unsigned long gp)
{
    (void)gp;
}

unsigned long port_GetGp(void)
{
    return 0;
}
