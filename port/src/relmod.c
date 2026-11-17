/*
 * PC replacement for soul-re's RELMOD.c (dropped through port/overrides.txt).
 *
 * On PSX an overlay .drm carries MIPS code, and RELMOD patches its addresses
 * after loading; object->relocModule then points at the module's function
 * table. Here the overlay's C is linked into the executable, so loading a
 * module copies the C function table over the MIPS one in the loaded object.
 * relocModule keeps pointing into the object, so it stays valid when MEMPACK
 * moves the object around.
 */
#include <ctype.h>

#include "common.h"
#include "Game/RELMOD.h"
#include "port/overlay.h"

static char port_loading_name[32];

void port_overlay_set_loading(const char *name)
{
    strncpy(port_loading_name, name, sizeof(port_loading_name) - 1);
    port_loading_name[sizeof(port_loading_name) - 1] = '\0';
}

static int name_equal(const char *a, const char *b)
{
    while ((*a != '\0') && (tolower((unsigned char)*a) == tolower((unsigned char)*b)))
    {
        a++;
        b++;
    }

    return tolower((unsigned char)*a) == tolower((unsigned char)*b);
}

void RELMOD_InitModulePointers(intptr_t baseaddr, int *relocs)
{
    const PortOverlay *ovl;

    (void)relocs;

    for (ovl = port_overlays; ovl->name != NULL; ovl++)
    {
        if (name_equal(ovl->name, port_loading_name))
        {
            memcpy((void *)baseaddr, ovl->table, ovl->size);

            printf("[rewraithed] overlay '%s' bound to C\n", ovl->name);

            port_loading_name[0] = '\0';
            return;
        }
    }

    /* Left as loaded: the unrelocated version pointer makes the game reject
     * the module, which disables that overlay cleanly. */
    printf("[rewraithed] overlay '%s' not available on PC yet\n", port_loading_name);

    port_loading_name[0] = '\0';
}

void RELMOD_RelocModulePointers(intptr_t baseaddr, int offset, int *relocs)
{
    (void)baseaddr;
    (void)offset;
    (void)relocs;
}
