/*
 * PSX-style memory layout of the PC build (see port/src/native/psx_memory_win32.c).
 */
#ifndef REWRAITHED_MEMORY_MAP_H
#define REWRAITHED_MEMORY_MAP_H

#include <stddef.h>
#include <stdint.h>

#define PORT_PSX_SCRATCHPAD 0x1F800000u
#define PORT_PSX_RAM        0x80000000u
#define PORT_PSX_RAM_SIZE   0x00400000u     /* 4 MB */

/* The game heap (MEMPACK), from overlayAddress to the end of "RAM". */
#define PORT_HEAP_START     0x80100000u
#define PORT_HEAP_END       (PORT_PSX_RAM + PORT_PSX_RAM_SIZE)

/* Right above it, data the port loads itself and hands to the game (the
 * Dreamcast models): PSX-style addresses too, for the same sign tests.
 * Allocated with port_extra_alloc, never freed. */
#define PORT_EXTRA_RAM      (PORT_PSX_RAM + PORT_PSX_RAM_SIZE)
#define PORT_EXTRA_RAM_SIZE 0x00200000u     /* 2 MB */

void *port_extra_alloc(size_t size);

/* Ordering table links hold 24-bit addresses. Links below the size of the
 * PSX RAM view point into it (the PSX ORs 0x80000000 back in, see nextPrim);
 * anything else is the executable's own static data, which the fixed-base
 * image keeps below 16 MB. */
#define PORT_OT_PTR(link) \
    ((uintptr_t)(((uint32_t)(link) < PORT_PSX_RAM_SIZE) ? ((uint32_t)(link) | PORT_PSX_RAM) : (uint32_t)(link)))

#endif
