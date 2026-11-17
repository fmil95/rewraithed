/*
 * Routes the game's root counter and event calls through port/src/irq.c.
 */
#ifndef REWRAITHED_TIMER_HOOKS_H
#define REWRAITHED_TIMER_HOOKS_H

#define GetRCnt      port_GetRCnt
#define SetRCnt      port_SetRCnt
#define StartRCnt    port_StartRCnt
#define StopRCnt     port_StopRCnt
#define OpenEvent    port_OpenEvent
#define EnableEvent  port_EnableEvent
#define DisableEvent port_DisableEvent
#define CloseEvent   port_CloseEvent

#include <libapi.h>

/* the sound interrupt saves $gp around its handler (port/src/hasm) */
#define GetGp port_GetGp
unsigned long port_GetGp(void);

void port_irq_init(void);

/* Brackets code that plays an interrupt handler (port/src/irq.c). */
void port_irq_handler_begin(void);
void port_irq_handler_end(void);

#endif
