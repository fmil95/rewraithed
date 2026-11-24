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

int port_earlier_areas_on(void);   /* port/src/mod_files.c */

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
 * "Add health and energy rechargers in Underworld": with the option on, a
 * healthr and an eaggotr are placed in under1, the disc's own (nothing is
 * needed in the mods folder): their objects are the same ones pillars1
 * uses, loaded when wanted (STREAM_GetObjectTracker), and their intros are
 * the port's (port_qol_place_chargers), brought in as a level's own are.
 * Like pillars1's own pair, they stay hidden
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

/* The two rechargers, as placed in under1 (its own coordinates): beside the
 * hint at (-2807, 729, 100), turned a quarter. The unique IDs are far above
 * the game's (those of a level, and 8192 up for the instances it makes), so
 * no other instance has them; above 8191, a saved one is kept with its own
 * position and flags (SAVE_SaveableInstance) and brought back from there. */
static const struct
{
    const char *name;
    long uniqueID;
    short x, y, z;
} port_chargers[] = {
    { "eaggotr", 32512, -2800, -200, 0 },
    { "healthr", 32513, -2800, -1000, 0 },
};

#define PORT_CHARGERS ((int)(sizeof(port_chargers) / sizeof(port_chargers[0])))

static Intro port_charger_intros[PORT_CHARGERS];
static long port_chargers_unit = -1;    /* under1's unit the intros were made for */
static Level *port_chargers_level;

/* under1, if it is loaded */
static StreamUnit *port_find_under1(void)
{
    int i;

    for (i = 0; i < 16; i++)
    {
        StreamUnit *unit = &StreamTracker.StreamList[i];

        if ((unit->used == 2) && (unit->level != NULL) && (unit->level->worldName != NULL) &&
            (_stricmp(unit->level->worldName, "under1") == 0))
        {
            return unit;
        }
    }

    return NULL;
}

/* where under1's own coordinates are in the world: its hint intro (UniqueID
 * 60, at (-2807, 729, 100) in under1) moved with it; 0 if it is not there */
static int port_under1_offset(Level *level, Position *offset)
{
    long i;

    for (i = 0; i < level->numIntros; i++)
    {
        Intro *intro = &level->introList[i];

        if ((intro->UniqueID == 60) && (_strnicmp(intro->name, "hint", 4) == 0))
        {
            offset->x = intro->position.x + 2807;
            offset->y = intro->position.y - 729;
            offset->z = intro->position.z - 100;
            return 1;
        }
    }

    return 0;
}

/* each frame with the option on: the rechargers are brought into under1
 * while it is loaded, once their objects have loaded */
static void port_qol_place_chargers(void)
{
    StreamUnit *unit = port_find_under1();
    long i;
    int c;

    if (unit == NULL)
    {
        port_chargers_unit = -1;
        port_chargers_level = NULL;
        return;
    }

    /* a level that places its own (a modded under1) is left as it is */
    for (i = 0; i < unit->level->numIntros; i++)
    {
        if (port_is_charger(unit->level->introList[i].name) && (unit->level->introList[i].UniqueID < 8192))
        {
            return;
        }
    }

    if ((port_chargers_unit != unit->StreamUnitID) || (port_chargers_level != unit->level))
    {
        Position offset;

        if (!port_under1_offset(unit->level, &offset))
        {
            return;
        }

        memset(port_charger_intros, 0, sizeof(port_charger_intros));

        for (c = 0; c < PORT_CHARGERS; c++)
        {
            Intro *intro = &port_charger_intros[c];

            snprintf(intro->name, sizeof(intro->name), "%s", port_chargers[c].name);
            intro->UniqueID = port_chargers[c].uniqueID;
            intro->rotation.z = 1024;
            intro->position.x = port_chargers[c].x + offset.x;
            intro->position.y = port_chargers[c].y + offset.y;
            intro->position.z = port_chargers[c].z + offset.z;
            intro->maxRad = 302;
            intro->maxRadSq = 302 * 302;
        }

        port_chargers_unit = unit->StreamUnitID;
        port_chargers_level = unit->level;
    }

    for (c = 0; c < PORT_CHARGERS; c++)
    {
        Intro *intro = &port_charger_intros[c];
        ObjectTracker *tracker;

        if (INSTANCE_Introduced(intro, (short)unit->StreamUnitID))
        {
            continue;
        }

        /* starts loading the object if it is not loaded */
        tracker = STREAM_GetObjectTracker(intro->name);

        if ((tracker != NULL) && (tracker->objectStatus == OBJECTTRACKER_USED) &&
            (INSTANCE_IntroduceInstance(intro, (short)unit->StreamUnitID) != NULL))
        {
            printf("[rewraithed] QoLs: %s placed in under1 at (%d, %d, %d)\n", intro->name,
                intro->position.x, intro->position.y, intro->position.z);
        }
    }
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
    if (!port_settings.underChargers || port_earlier_areas_on() || (intro == NULL) || !port_is_charger(intro->name) || !port_in_under1(streamUnitID))
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

    if (!port_settings.underChargers || port_earlier_areas_on() || (gameTrackerX.instanceList == NULL))
    {
        return;
    }

    port_qol_place_chargers();

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

        printf("[rewraithed] QoLs: Underworld %s %s, at (%d, %d, %d)\n", instance->object->name, shown ? "shown" : "hidden",
            instance->position.x, instance->position.y, instance->position.z);

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
