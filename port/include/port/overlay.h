#ifndef REWRAITHED_OVERLAY_H
#define REWRAITHED_OVERLAY_H

#include <stddef.h>

/* One statically linked overlay (see port/overlays.txt). */
typedef struct PortOverlay
{
    const char *name;   /* object name, e.g. "skinner" */
    const void *table;  /* its C function table */
    size_t size;
} PortOverlay;

/* Generated: build/overlay_registry.c, terminated by a NULL name. */
extern const PortOverlay port_overlays[];

/* Name of the module whose .drm was loaded last (set by the load hooks,
 * consumed by RELMOD_InitModulePointers). */
void port_overlay_set_loading(const char *name);

#endif
