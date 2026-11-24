/*
 * Mods: optional changes to the game, each on or off, listed in the
 * launcher's Mods window and saved under [Mods] in rewraithed.ini.
 *
 * Adding one:
 *   1. a MOD_ value below (before MOD_COUNT; the order is the window's);
 *   2. its entry in port_mods (port/shared/mod_list.c): the ini key, the name
 *      and explanation the window shows, and, when it needs files of the
 *      player's own (another game's disc, say), a check that they are there,
 *      whose message the window shows when they aren't;
 *   3. the game side reads port_settings.mods[MOD_...].
 * The settings file and the window need nothing else.
 */
#ifndef REWRAITHED_MOD_LIST_H
#define REWRAITHED_MOD_LIST_H

#include <stddef.h>

enum
{
    MOD_SR2_INTRO,      /* Soul Reaver 2's copy of the intro movie (port/src/sr2_movie.c) */
    MOD_COUNT
};

typedef struct
{
    const char *key;            /* in [Mods] of rewraithed.ini */
    const char *name;           /* UTF-8 */
    const char *description;    /* UTF-8, the tooltip */
    /* NULL, or 1 when the files it needs are there; else 0 and what to do
     * (UTF-8) in `problem` */
    int (*available)(char *problem, size_t problemSize);
} PortMod;

extern const PortMod port_mods[MOD_COUNT];

#endif
