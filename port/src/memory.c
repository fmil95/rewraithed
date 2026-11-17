/*
 * The game heap.
 *
 * On the PSX the heap (MEMPACK) starts right after the executable
 * (overlayAddress) and runs to the top of main RAM. Here it occupies the
 * PSX-style RAM view mapped at 0x80000000 (port/src/native/psx_memory_win32.c),
 * so everything the game loads gets PSX-style addresses.
 */
#include "common.h"
#include "port/memory_map.h"

void *overlayAddress = (void *)(uintptr_t)PORT_HEAP_START;
long port_heap_size = PORT_HEAP_END - PORT_HEAP_START;
