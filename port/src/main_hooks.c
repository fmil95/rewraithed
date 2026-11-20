/*
 * PC replacements for functions in soul-re's PSX/MAIN.c
 * (renamed there to <name>_psx through port/overrides.txt).
 */
#include <SDL.h>

#include "common.h"
#include "settings.h"

extern int devstation;
extern long DoMainMenu;
extern InterfaceItem InterfaceItems[6];

/* port/src/pc_main.c: value after a command-line option, or NULL */
const char *port_arg(const char *name);
int port_has_flag(const char *name);

void ProcessArgs_psx(char *baseAreaName, GameTracker *gameTracker);
void SAVE_Init_psx(GameTracker *gt);
void CINE_Play_psx(char *strfile, unsigned short mask, int buffers);
void show_screen_psx(char *name);

/* -skiplogos, or the launcher's Skip intros (to the menu or to the game) */
static int port_skip_logos(void)
{
    return port_has_flag("-skiplogos") || (port_settings.skipIntros != SKIP_NONE);
}

/* the still screen after the logo movies (Blood Omen by Silicon Knights,
 * Soul Reaver by Crystal Dynamics): MAIN.c's last InterfaceItem */
static int port_is_legal_screen(const char *name)
{
    return SDL_strcasecmp(name, "\\kain2\\game\\psx\\mainmenu\\legal.tim") == 0;
}

/* The PSX version writes to the extra RAM of a DTL-H dev kit to detect it. */
void CheckForDevStation(void)
{
    devstation = 0;
}

/* The disc's kain2.arg is read as usual; then the PC command line can
 * override the start area and skip the main menu:
 *   -area <name>   start in that area, e.g. city5 (default from the disc)
 *   -skipmenu      start the game right after the logos
 *   -skiplogos     leave out the publisher and developer logo movies and
 *                  the legal screen after them (see CINE_Play below)
 *   -debugmenu     unlock the debug menu (Square + Circle in game)
 *   -material      start in the material realm instead of the spectral one
 *   -skipcutscene  no cutscenes: under1's opening is left out (below), every
 *                  other in-engine scene is fast-forwarded (cutscene_hooks.c)
 *                  and the movies level scripts play are skipped (CINE_Play)
 *   -swim, -abilities <mask>: see port/src/raziel_hooks.c
 *   -vramlog       log object texture placement: see port/src/vram_hooks.c */
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

    /* the boot sequence holds the legal screen for its timeout (165 frames)
     * whether or not show_screen below draws it */
    if (port_skip_logos())
    {
        int i;

        for (i = 0; i < 6; i++)
        {
            if (port_is_legal_screen(InterfaceItems[i].name))
            {
                InterfaceItems[i].timeout = 0;
            }
        }
    }

    /* the disc's kain2.arg has -INSPECTRAL, so areas start in the spectral
     * realm, where most enemies are absent */
    if (port_has_flag("-material"))
    {
        gameTracker->gameData.asmData.MorphType = 0;
    }

    /* The debug menu's "Skip First Puppet Show": under1's opening script tests
     * this bit (ATTR_SKIP_PUPPET_SHOW in EVENT.c) and leaves its cutscene out.
     * Nothing clears it. The other scenes don't test it: cutscene_hooks.c. */
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
 * InterfaceItems) end at once. -skipcutscene: so do the movies level scripts
 * play (CINE_PlayIngame's \CHRONO<n>.STR). Every other movie plays. */
void CINE_Play(char *strfile, unsigned short mask, int buffers)
{
    if (port_skip_logos() &&
        ((SDL_strcasecmp(strfile, "\\PUBLOGO.STR;1") == 0) || (SDL_strcasecmp(strfile, "\\CRYLOGO.STR;1") == 0)))
    {
        return;
    }

    if (port_has_flag("-skipcutscene") && (SDL_strncasecmp(strfile, "\\CHRONO", 7) == 0))
    {
        return;
    }

    CINE_Play_psx(strfile, mask, buffers);
}

/* -skiplogos: and no legal screen either (show_screen draws only that one) */
void show_screen(char *name)
{
    if (port_skip_logos() && port_is_legal_screen(name))
    {
        return;
    }

    show_screen_psx(name);
}
