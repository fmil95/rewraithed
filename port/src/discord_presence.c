/*
 * What Discord Rich Presence shows (the IPC client is in
 * port/src/native/discord_rpc.c): the region and unit Raziel is in and the
 * realm, or the menu.
 *
 * Called from the presence thread, so it only copies plain fields of the
 * game's trackers and follows no pointers: a value caught mid-change is
 * just shown a second late.
 */
#include <SDL.h>

#include "common.h"
#include "settings.h"

extern GameTracker gameTrackerX;
extern MainTracker mainTrackerX;

void port_discord_start(void);   /* native/discord_rpc.c */
int port_has_flag(const char *name);

/* unit name prefix -> region, after the groups of the debug level select
 * (port/launcher/areas.h) */
static const struct { const char *prefix; const char *region; } regions[] = {
    { "aluka", "Aluka" },
    { "nighta", "Ash Village" },
    { "nightb", "Ash Village" },
    { "oracle", "Oracle's Cave" },
    { "pillars", "Pillars" },
    { "tompil", "Pillars" },
    { "cathy", "Silenced Cathedral" },
    { "skinnr", "Skinner" },
    { "out", "Skinner" },
    { "stone", "Stone Glyph" },
    { "fill", "Sunlight Glyph" },
    { "intvaly", "Sunlight Glyph" },
    { "sunrm", "Sunlight Glyph" },
    { "piston", "Sunlight Glyph" },
    { "htorm", "Sunlight Glyph" },
    { "boss", "Tomb of the Seven" },
    { "tomb", "Tomb of the Seven" },
    { "conectc", "Tomb of the Seven" },
    { "add", "Tomb of the Seven" },
    { "tower", "Water Glyph" },
    { "city", "City" },
    { "cliff", "Cliffs" },
    { "under", "Underworld" },
    { "mrlock", "Morlock" },
    { "huba", "Hub A" },
    { "hubb", "Hub B" },
    { "train", "Training" },
    { "fire", "Dark Eden" },
    { "chrono", "Chronoplast" },
};

static const char *region_of(const char *unit)
{
    char prefix[16];
    size_t n = 0;
    size_t i;

    while ((unit[n] != '\0') && ((unit[n] < '0') || (unit[n] > '9')) && (n < sizeof(prefix) - 1))
    {
        prefix[n] = (char)SDL_tolower((unsigned char)unit[n]);
        n++;
    }

    prefix[n] = '\0';

    for (i = 0; i < sizeof(regions) / sizeof(regions[0]); i++)
    {
        if (SDL_strcmp(prefix, regions[i].prefix) == 0)
        {
            return regions[i].region;
        }
    }

    return NULL;
}

void port_discord_snapshot(char *details, size_t detailsSize, char *state, size_t stateSize, int *realm)
{
    char unit[16];
    const char *region;
    size_t i;

    /* the current unit's name, kept to letters and digits */
    for (i = 0; i < sizeof(unit) - 1; i++)
    {
        char c = gameTrackerX.baseAreaName[i];

        if (!(((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9')) || (c == '_')))
        {
            break;
        }

        unit[i] = c;
    }

    unit[i] = '\0';

    *realm = 0;
    state[0] = '\0';

    switch (mainTrackerX.mainState)
    {
    case 1:     /* in game */
        region = region_of(unit);
        *realm = (gameTrackerX.gameData.asmData.MorphType != 0) ? 2 : 1;

        SDL_snprintf(details, detailsSize, "%s", (region != NULL) ? region : "Exploring Nosgoth");
        SDL_snprintf(state, stateSize, "%s - %s Realm%s", unit, (*realm == 2) ? "Spectral" : "Material",
            (gameTrackerX.gameMode == 6) ? " (paused)" : "");
        break;
    case 2:     /* loading an area */
        SDL_snprintf(details, detailsSize, "Loading");
        SDL_snprintf(state, stateSize, "%s", unit);
        break;
    case 8:
    case 9:
        SDL_snprintf(details, detailsSize, "In the main menu");
        break;
    default:    /* logos and intro movies */
        SDL_snprintf(details, detailsSize, "Starting up");
        break;
    }
}

/* port/src/pc_main.c: on unless turned off in the launcher or with
 * -nodiscord */
void port_discord_init(void)
{
    if (port_settings.discordPresence && !port_has_flag("-nodiscord"))
    {
        port_discord_start();
    }
}
