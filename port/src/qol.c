/*
 * Quality-of-life options (the launcher's QoLs window).
 *
 * "Disable respawning enemies in block puzzles": the Zephonim (wallcr)
 * of the block puzzle rooms cathy13, cathy59 and cathy60 come back a few
 * minutes after being killed (their intro data holds command 9, with 130
 * to 200 seconds, MON_ProcessIntro), which makes pushing the blocks around
 * a chore. With the option on, their intros get flag 0x400 as they are
 * brought in, as the editor's levelpatch.py --no-regen writes into a level:
 * MON_KillMonster then marks them dead for good and the regenerator drops
 * them (MONAPI.c). Called at the start of MON_ProcessIntro (fixup).
 */
#include "common.h"
#include "Game/STREAM.h"

#include <stdio.h>
#include <string.h>

#include "settings.h"

static const char *const port_puzzle_units[] = { "cathy13", "cathy59", "cathy60" };

static int port_is_puzzle_unit(long streamUnitID)
{
    Level *level = STREAM_GetLevelWithID(streamUnitID);
    int i;

    if ((level == NULL) || (level->worldName == NULL))
    {
        return 0;
    }

    for (i = 0; i < (int)(sizeof(port_puzzle_units) / sizeof(port_puzzle_units[0])); i++)
    {
        if (_stricmp(level->worldName, port_puzzle_units[i]) == 0)
        {
            return 1;
        }
    }

    return 0;
}

void port_qol_monster_intro(Instance *instance)
{
    if (!port_settings.noPuzzleRespawn || (instance == NULL) || (instance->intro == NULL))
    {
        return;
    }

    if ((_strnicmp(instance->introName, "wallcr", 6) == 0) && port_is_puzzle_unit(instance->birthStreamUnitID))
    {
        instance->intro->flags |= 0x400;
    }
}

/*
 * "Add health and energy rechargers in Underworld": with the option on, the
 * modded under1 in the mods folder is used (mod_files.c), which places a
 * healthr and an eaggotr there. Like pillars1's own pair, they stay hidden
 * until Kain's scene in pillars1 shows them: that scene (event -6) shows
 * pillars1's pair and adds 1 to variable 2 of pillars1's event 3, which
 * keeps them shown from then on. The Underworld's pair follows that variable,
 * read from pillars1 when it is loaded, else from its saved events.
 */
#include "Game/SAVEINFO.h"
#include "Game/GAMELOOP.h"

#define PILLARS1_AREA       31442   /* pillars1's streamUnitID, its events' areaID */
#define PILLARS1_EVENT      3
#define PILLARS1_EVENT_VAR  2

static int port_is_charger(const char *name)
{
    return (_strnicmp(name, "healthr", 7) == 0) || (_strnicmp(name, "eaggotr", 7) == 0);
}

static int port_in_under1(long streamUnitID)
{
    Level *level = STREAM_GetLevelWithID(streamUnitID);

    return (level != NULL) && (level->worldName != NULL) && (_stricmp(level->worldName, "under1") == 0);
}

/* Kain's scene in pillars1 has shown its rechargers */
static int port_chargers_shown(void)
{
    Level *level = STREAM_GetLevelWithID(PILLARS1_AREA);
    SavedBasic *saved;
    long i;

    if ((level != NULL) && (level->PuzzleInstances != NULL))
    {
        for (i = 0; i < level->PuzzleInstances->numPuzzles; i++)
        {
            Event *event = level->PuzzleInstances->eventInstances[i];

            if (event->eventNumber == PILLARS1_EVENT)
            {
                return event->eventVariables[PILLARS1_EVENT_VAR] >= 1;
            }
        }
    }

    saved = SAVE_GetSavedEvent(PILLARS1_AREA, PILLARS1_EVENT);

    if (saved == NULL)
    {
        return 0;
    }

    if (saved->savedID == SAVED_ID_EVENT)
    {
        return ((SavedEvent *)saved)->eventVariables[PILLARS1_EVENT_VAR] >= 1;
    }

    return ((SavedEventSmallVars *)saved)->eventVariables[PILLARS1_EVENT_VAR] >= 1;
}

/* the start of INSTANCE_IntroduceInstance (fixup): the Underworld's
 * rechargers come in hidden (intro flag 0x80) until they are to be shown */
void port_qol_introduce(Intro *intro, short streamUnitID)
{
    if (!port_settings.underChargers || (intro == NULL) || !port_is_charger(intro->name) || !port_in_under1(streamUnitID))
    {
        return;
    }

    if (port_chargers_shown())
    {
        intro->flags &= ~0x80;
    }
    else
    {
        intro->flags |= 0x80;
    }
}

/* the rechargers already set, with the state they were given: each is set
 * once, and again only when the state changes, so that whatever they do
 * themselves afterwards is left alone */
#define MAX_CHARGERS 8

static struct
{
    Instance *instance;
    long uniqueID;
    int shown;
} port_chargers_set[MAX_CHARGERS];

static int port_charger_needs_set(Instance *instance, int shown)
{
    int i;
    int free_slot = -1;

    for (i = 0; i < MAX_CHARGERS; i++)
    {
        if ((port_chargers_set[i].instance == instance) && (port_chargers_set[i].uniqueID == instance->introUniqueID))
        {
            if (port_chargers_set[i].shown == shown)
            {
                return 0;
            }

            port_chargers_set[i].shown = shown;
            return 1;
        }

        if ((free_slot < 0) && (port_chargers_set[i].instance == NULL))
        {
            free_slot = i;
        }
    }

    if (free_slot < 0)
    {
        free_slot = (int)(instance->introUniqueID % MAX_CHARGERS);
    }

    port_chargers_set[free_slot].instance = instance;
    port_chargers_set[free_slot].uniqueID = instance->introUniqueID;
    port_chargers_set[free_slot].shown = shown;

    return 1;
}

/* each frame of the game loop (fixup): the Underworld's rechargers shown or
 * hidden as the event script's attributes 10 and 11 do, should a saved
 * state disagree with their intro or the state change while they are there */
void port_qol_frame(void)
{
    Instance *instance;
    int shown = 0;
    int checked = 0;

    if (!port_settings.underChargers || (gameTrackerX.instanceList == NULL))
    {
        return;
    }

    for (instance = gameTrackerX.instanceList->first; instance != NULL; instance = instance->next)
    {
        int hidden;

        if ((instance->object == NULL) || (instance->object->name == NULL) || !port_is_charger(instance->object->name) ||
            !port_in_under1(instance->birthStreamUnitID))
        {
            continue;
        }

        if (!checked)
        {
            shown = port_chargers_shown();
            checked = 1;
        }

        if (!port_charger_needs_set(instance, shown))
        {
            continue;
        }

        hidden = (instance->flags & 0x800) != 0;

        printf("[rewraithed] QoLs: Underworld %s %s\n", instance->object->name, shown ? "shown" : "hidden");

        if (shown && hidden)
        {
            instance->flags &= ~(0x800 | 0x40000);
            instance->flags2 &= ~0x20000000;

            if (instance->object->oflags2 & 0x80000)
            {
                instance->flags2 &= ~0x10000000;
            }
        }
        else if (!shown && !hidden)
        {
            instance->flags |= 0x800 | 0x40000;
            instance->flags2 |= 0x20000000;

            if (instance->object->oflags2 & 0x80000)
            {
                instance->flags2 |= 0x10000000;
            }
        }
    }
}
