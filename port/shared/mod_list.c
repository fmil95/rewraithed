/*
 * The mods (mod_list.h): one entry each, in the order of the Mods window.
 */
#include <stdio.h>

#include "mod_list.h"
#include "sr2_disc.h"

static int sr2_intro_available(char *problem, size_t problemSize)
{
    char folder[260];
    char why[300];

    if (sr2_intro_locate(NULL, 0, NULL, NULL, why, sizeof(why)))
    {
        return 1;
    }

    sr2_folder(folder, sizeof(folder));
    snprintf(problem, problemSize,
        "This mod plays the intro movie from your own copy of Soul Reaver 2 for PlayStation 2, which has it "
        "among its extras.\n\n"
        "Place your image of its disc (the .iso file) in this folder:\n\n%s\n\nthen choose Yes again.\n\n(%s)",
        folder, why);

    return 0;
}

const PortMod port_mods[MOD_COUNT] = {
    { "SR2Intro", "Use SR2 Movie for the Intro",
      "Plays the intro movie of a new game from Soul Reaver 2's extras, sharper (640 x 480 MPEG-2) than the "
      "PlayStation's and the PC version's. Needs your own image of the PlayStation 2 disc of Soul Reaver 2 in "
      "the sr2 folder next to the game.",
      sr2_intro_available },
};
