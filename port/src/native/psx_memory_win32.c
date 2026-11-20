/*
 * PSX memory layout inside the 32-bit process.
 *
 *  0x1F800000  scratchpad (1 KB on PSX). The hand-written routines address it
 *              with absolute constants and keep pointers into it, so it is
 *              mapped at that exact address, and PsyCross's getScratchAddr()
 *              is pointed at it too.
 *
 *  0x80000000  "main RAM" (4 MB here). The game heap lives in it, so data the
 *              game loads (models, textures, primitives, ordering tables) has
 *              PSX-style addresses: hand-written code tells pointers from
 *              other values by their sign (bgtz/bltz), and 24-bit ordering
 *              table links decode back to these addresses (see port_ot_ptr).
 *              Needs /LARGEADDRESSAWARE.
 *
 *  0x80400000  2 MB more, for data the port itself loads and gives the game
 *              (port_extra_alloc), so that it has PSX-style addresses too.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#include "port/memory_map.h"

extern char *_scratchData;  /* PsyCross libetc */

static void *map_fixed(uintptr_t address, size_t size, const char *what)
{
    void *p = VirtualAlloc((void *)address, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

    if (p != (void *)address)
    {
        fprintf(stderr, "[rewraithed] cannot map the PSX %s at 0x%08X (error %lu)\n",
            what, (unsigned int)address, GetLastError());
        exit(1);
    }

    return p;
}

void port_map_psx_memory(void)
{
    _scratchData = (char *)map_fixed(PORT_PSX_SCRATCHPAD, 0x1000, "scratchpad");

    map_fixed(PORT_PSX_RAM, PORT_PSX_RAM_SIZE, "main RAM");
    map_fixed(PORT_EXTRA_RAM, PORT_EXTRA_RAM_SIZE, "port data area");
}

/* bump allocation in the port data area; NULL when it is full */
void *port_extra_alloc(size_t size)
{
    static size_t used;
    void *p;

    size = (size + 15) & ~(size_t)15;

    if (used + size > PORT_EXTRA_RAM_SIZE)
    {
        return NULL;
    }

    p = (void *)(uintptr_t)(PORT_EXTRA_RAM + used);
    used += size;

    return p;
}
