/*
 * PC wrappers for functions in soul-re's RAZIEL/RAZIEL.c
 * (renamed there to <name>_psx through port/overrides.txt).
 *
 * Test abilities from the command line:
 *   -swim               swimming (with the abilities before it: the debug
 *                       menu's "SWIM", 0x1F)
 *   -abilities <mask>   any ability bits (hex or decimal), e.g. 0x3F for all
 *                       special abilities, 0x01FC0000 for all glyphs; see the
 *                       debug menu's ability lists in DEBUG.c
 */
#include <stdlib.h>

#include "common.h"
#include "Game/DEBUG.h"

const char *port_arg(const char *name);
int port_has_flag(const char *name);

void mdRazielProcess_psx(Instance *playerInstance, GameTracker *gameTracker, long *controlCommand);

static unsigned long port_abilities;
static int port_abilities_read;

/* Raziel.Abilities is copied from debugRazielFlags1 at the end of every
 * mdRazielProcess, and debugRazielFlags1 is reset from Raziel.Abilities when
 * Raziel is set up or changes Reaver, so the extra bits go in every frame. */
void mdRazielProcess(Instance *playerInstance, GameTracker *gameTracker, long *controlCommand)
{
    if (!port_abilities_read)
    {
        const char *mask = port_arg("-abilities");

        port_abilities_read = 1;

        if (port_has_flag("-swim"))
        {
            port_abilities |= 0x1F;
        }

        if (mask != NULL)
        {
            port_abilities |= strtoul(mask, NULL, 0);
        }

        if (port_abilities != 0)
        {
            printf("[rewraithed] extra Raziel abilities 0x%lx\n", port_abilities);
        }
    }

    debugRazielFlags1 |= port_abilities;

    mdRazielProcess_psx(playerInstance, gameTracker, controlCommand);
}
