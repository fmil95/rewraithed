/*
 * PC replacements for functions in soul-re's PSX/MAIN.c
 * (renamed there to <name>_psx through port/overrides.txt).
 */
#include <SDL.h>

#include "common.h"
#include "settings.h"

extern int devstation;
extern long DoMainMenu;

/* port/src/pc_main.c: value after a command-line option, or NULL */
const char *port_arg(const char *name);
int port_has_flag(const char *name);

void ProcessArgs_psx(char *baseAreaName, GameTracker *gameTracker);
void SAVE_Init_psx(GameTracker *gt);
void CINE_Play_psx(char *strfile, unsigned short mask, int buffers);

/* The PSX version writes to the extra RAM of a DTL-H dev kit to detect it. */
void CheckForDevStation(void)
{
    devstation = 0;
}

/* The disc's kain2.arg is read as usual; then the PC command line can
 * override the start area and skip the main menu:
 *   -area <name>   start in that area, e.g. city5 (default from the disc)
 *   -skipmenu      start the game right after the logos
 *   -skiplogos     leave out the publisher and developer logo movies
 *                  (see CINE_Play below)
 *   -debugmenu     unlock the debug menu (Square + Circle in game)
 *   -material      start in the material realm instead of the spectral one
 *   -skipcutscene  skip the start area's in-engine cutscene (e.g. under1's)
 *   -swim, -abilities <mask>: see port/src/raziel_hooks.c */
void ProcessArgs(char *baseAreaName, GameTracker *gameTracker)
{
    const char *area;

    ProcessArgs_psx(baseAreaName, gameTracker);

    area = port_arg("-area");

    if ((area == NULL) && (port_settings.area[0] != '\0'))
    {
        area = port_settings.area;
    }

    if (area != NULL)
    {
        strncpy(baseAreaName, area, 15);
        baseAreaName[15] = '\0';
    }

    if (port_has_flag("-skipmenu") || (port_settings.skipIntros == SKIP_TO_GAME))
    {
        DoMainMenu = 0;
    }

    /* the disc's kain2.arg has -INSPECTRAL, so areas start in the spectral
     * realm, where most enemies are absent */
    if (port_has_flag("-material"))
    {
        gameTracker->gameData.asmData.MorphType = 0;
    }

    /* The debug menu's "Skip First Puppet Show": level event scripts test
     * this bit (ATTR_SKIP_PUPPET_SHOW in EVENT.c) and leave out their
     * cutscene. Nothing clears it. */
    if (port_has_flag("-skipcutscene"))
    {
        gameTracker->streamFlags |= 0x800000;
    }

    /* The debug menu opens on Square + Circle only if debugFlags 0x40000
     * is clear, which GAMELOOP_SystemInit sets and nothing clears, or if
     * playerCheatFlags 0x2 is set, which nothing in the game sets. */
    if (port_has_flag("-debugmenu") || port_settings.debugMenu)
    {
        gameTracker->playerCheatFlags |= 0x2;
    }

    printf("[rewraithed] start area '%s'%s\n", baseAreaName, DoMainMenu ? "" : " (no main menu)");
}

/* The game sets up the memory card (and so the pause menu's Save Game)
 * only when it boots through the main menu; -skipmenu keeps it. */
void SAVE_Init(GameTracker *gt)
{
    long doMainMenu = DoMainMenu;

    DoMainMenu = 1;
    SAVE_Init_psx(gt);
    DoMainMenu = doMainMenu;
}

/* -skiplogos: the two logo movies the game plays at boot (MAIN.c's
 * InterfaceItems) end at once; every other movie plays. */
void CINE_Play(char *strfile, unsigned short mask, int buffers)
{
    if ((port_has_flag("-skiplogos") || (port_settings.skipIntros != SKIP_NONE)) &&
        ((SDL_strcasecmp(strfile, "\\PUBLOGO.STR;1") == 0) || (SDL_strcasecmp(strfile, "\\CRYLOGO.STR;1") == 0)))
    {
        return;
    }

    CINE_Play_psx(strfile, mask, buffers);
}
