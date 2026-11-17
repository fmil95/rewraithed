/*
 * Force-included ahead of every soul-re source file in the PC build.
 *
 * It neutralises the MIPS/GCC-specific parts of the decomp headers without
 * touching them, and pulls in the PsyCross versions of the PsyQ libraries.
 */
#if !defined(REWRAITHED_PREFIX_H) && !defined(PORT_NATIVE)
#define REWRAITHED_PREFIX_H

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

/* GCC syntax used in a few places (section placement, inline MIPS asm,
 * the _break() debug trap). None of it means anything on PC. */
#define __attribute__(x)
#define __asm__(...)
#define asm(...)
#define __volatile__ volatile

/* soul-re's Libs/GTE.h emits raw COP2 opcodes; skip it and use the
 * software GTE from PsyCross through port/gte_compat.h instead. */
#define _GTE_H_

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include <libgte.h>
#include <libgpu.h>
#include <libetc.h>
#include <inline_c.h>
#include <gtemac.h>

#include "port/memory_map.h"

/* Ordering table links are 24-bit; decode them like the PSX does (PsyQ's
 * nextPrim ORs 0x80000000 back in). */
#undef nextPrim
#define nextPrim(p) ((void *)PORT_OT_PTR(((P_TAG *)(p))->addr))

#include "port/gte_compat.h"
#include "port/libgte_game.h"
#include "port/cd_drive.h"
#include "port/gpu_hooks.h"
#include "port/timer_hooks.h"
#include "port/spu_hooks.h"
#include "port/memcard_hooks.h"

/* PsyCross calls the PsyQ RECT "RECT16" so it can coexist with <windows.h>;
 * the game uses the PsyQ name. */
typedef struct _RECT16 RECT;

/* Size of the game heap that starts at overlayAddress (port/src/memory.c). */
extern long port_heap_size;

/* C versions of the game's inline-assembly macros; last, as it includes
 * game headers */
#include "port/hasm_inline.h"

#endif
