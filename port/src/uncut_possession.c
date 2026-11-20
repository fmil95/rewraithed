/*
 * -uncut: Raziel's possession ability, cut from the game.
 *
 * The February 1999 prototype has the monster half of it: an idle monster
 * hands commands shaped like Raziel's controls (move in a direction, attack,
 * fire a projectile, stop) to MON_PossessedMessageHandler, which turns,
 * walks, runs, attacks and shoots with the monster's own speeds and attacks.
 * Raziel's player structure had a "possessed" instance, but nothing in that
 * build ever set it: the part that hands Raziel's controls to a monster was
 * never written. Here it is, in the spirit of the rest:
 *
 *  - aiming (L2 + R2, or the mouse's aim button), Feed possesses the monster
 *    nearest the middle of the view;
 *  - the monster becomes "controlled", as level scripts make monsters
 *    (message 0x4000E: its AI stands aside), and the camera follows it;
 *  - each frame the pad becomes the February messages, posted to it, and
 *    Raziel's own controls are cleared (but for Start and Select);
 *  - Feed again, or the monster's death, gives Raziel back his body (and
 *    so does turning possession off on the debug menu's Cut Content page);
 *  - Attack by a switch (a valve, a lever) works it, as Raziel does;
 *  - a cutscene (Raziel puppeted by a level script) gives Raziel back his
 *    body before it starts, and none can be possessed until it ends. One
 *    the possessed monster set off (the city9 valve's) was written for
 *    Raziel doing it and leaves him where he would have stood; he is put
 *    back where he was.
 *
 * MON_PossessedMessageHandler is the February function (decompiled by
 * AesirHod, 95% matching), adapted to the July structures; the hook in
 * MON_IdleMessageHandler (fixup) is the February one, taken by the possessed
 * monster only.
 */
#include "common.h"
#include "Game/INSTANCE.h"
#include "Game/CAMERA.h"
#include "Game/MATH3D.h"
#include "Game/MONSTER/MONLIB.h"
#include "Game/PHYSOBS.h"
#include "Game/MONSTER/MISSILE.h"
#include "Game/STREAM.h"

#include <stdio.h>
#include <stdlib.h>

extern GameTracker gameTrackerX;
extern MainTracker mainTrackerX;

int port_has_flag(const char *name);  /* pc_main.c */
void port_cut_content_frame(void);    /* uncut_content.c */
int port_cut_possession_on(void);
int ratan2(int y, int x);
int rsin(int a);
int rcos(int a);

/* game commands (GAMEPAD's translation of the pad) */
#define CMD_DIRECTIONS  0x000F
#define CMD_TELEKINESIS 0x0010  /* Triangle */
#define CMD_ATTACK      0x0020  /* Square */
#define CMD_FEED        0x0040  /* Circle */
#define CMD_STEALTH     0x0200  /* R1 */
#define CMD_AIM         0x0C00  /* L2 + R2 */
#define CMD_KEEP        0xC000  /* Start, Select: the pause and glyph menus */

#define MAGIC_MONSTER   0xACE00065

/* how far, and how far off the middle of the view, a monster can be */
#define POSSESS_RANGE   8000
#define POSSESS_ANGLE   640

/* a switch is worked from where Raziel would stand (its own engage
 * distance and height band, PhysObSwitchProperties), loosened for bodies
 * bigger than his; and in front of the monster */
#define SWITCH_EXTRA_XY 400
#define SWITCH_EXTRA_Z  256
#define SWITCH_ANGLE    1024

/* switch classes (PhysObSwitchProperties) */
#define SWITCH_HOLD     6   /* a lever held down while Attack is */
#define SWITCH_SIDED    9   /* worked from either side */

static int uncut = -1;
static Instance *possessed;
static Instance *heldSwitch;
static int inCutscene;

/* where Raziel stood when a cutscene took over during possession, and for
 * how many more frames he is put back there once it ends */
static Position razielStood;
static Rotation razielFacing;
static int putBack;
static int putBackFrames;
static long possessedID;
static int wasMoving;

static int uncut_enabled(void)
{
    if (uncut < 0)
    {
        uncut = port_has_flag("-uncut");

        if (uncut)
        {
            printf("[rewraithed] -uncut: possession (aim, then Feed)\n");
        }
    }

    return uncut;
}

/* --- the February monster side --------------------------------------------- */

void MON_PossessedMessageHandler(Instance *instance, Message *message)
{
    /* MON_ChooseAttack reads the target's flags when an attack's height
     * does not match; a controlled monster has none: "height does not
     * matter" (mirFlags 8) */
    static MonsterIR noTarget;
    MonsterVars *mv;
    long id;
    int state;
    short target;
    int nochange;
    short speed;

    mv = (MonsterVars *)instance->extraData;
    id = message->ID;
    state = (int)instance->currentMainState;

    if (id == 0)
    {
        if ((state == MONSTER_STATE_WANDER) || (state == MONSTER_STATE_PURSUE))
        {
            MON_SwitchState(instance, MONSTER_STATE_IDLE);
        }

        return;
    }

    if (mv->mode == 0x80000)
    {
        return;
    }

    /* (February: 0x2000000 and 0x2000008 started MONSTER_STATE_DO, a state
     * the July build no longer has) */

    if (id < 0)
    {
        switch ((unsigned long)id)
        {
        case 0x80000000:
            noTarget.mirFlags = 0x8;
            mv->attackType = MON_ChooseAttack(instance, &noTarget);

            if (mv->attackType != NULL)
            {
                MON_SwitchState(instance, MONSTER_STATE_ATTACK);
            }

            break;
        case 0x80000020:
            if ((signed char)mv->subAttr->combatAttributes->missileAttack != -1)
            {
                MON_SwitchState(instance, MONSTER_STATE_PROJECTILE);
            }

            break;
        }
    }

    if (!(id & 0x10000000))
    {
        return;
    }

    target = (short)message->Data;
    nochange = 0;

    if (target != instance->rotation.z)
    {
        int diff = (target - instance->rotation.z) & 0xFFF;

        if (diff >= 0x801)
        {
            diff -= 0x1000;
        }

        speed = 0;

        switch (state)
        {
        case MONSTER_STATE_WANDER:
            speed = mv->subAttr->speedWalkTurn;
            break;
        case MONSTER_STATE_PURSUE:
            speed = mv->subAttr->speedRunTurn;
            break;
        case MONSTER_STATE_IDLE:
            speed = mv->subAttr->speedPivotTurn;

            /* a sharp turn pivots in place first */
            if (abs(diff) >= 0x201)
            {
                nochange = 1;
            }

            break;
        }

        /* (the February step was per frame at 30 FPS) */
        AngleMoveToward(&instance->rotation.z, target, (short)((speed * gameTrackerX.timeMult) / 4096));
    }

    if (nochange)
    {
        return;
    }

    if (id & 0x1000)
    {
        /* walking: from running or standing */
        if ((state == MONSTER_STATE_PURSUE) || (state == MONSTER_STATE_IDLE))
        {
            MON_SwitchState(instance, MONSTER_STATE_WANDER);
        }

        return;
    }

    /* running: from standing or walking */
    if ((state == MONSTER_STATE_IDLE) || (state == MONSTER_STATE_WANDER))
    {
        MON_SwitchState(instance, MONSTER_STATE_PURSUE);
    }
}

/* MON_Attack (fixup): the possessed monster swings where it faces, not at
 * its enemy (Raziel, standing there) */
int port_uncut_is_possessed(Instance *instance)
{
    return (instance != NULL) && (instance == possessed);
}

/* MON_Projectile (fixup): the possessed monster fires straight ahead, at
 * chest height, not at its enemy */
void port_uncut_fire_missile(Instance *instance, MonsterMissile *missileDef)
{
    static Position ahead;
    int yaw = instance->rotation.z & 0xFFF;

    ahead.x = instance->position.x + (short)((rsin(yaw) * 4000) >> 12);
    ahead.y = instance->position.y - (short)((rcos(yaw) * 4000) >> 12);
    ahead.z = instance->position.z + 300;

    MISSILE_Fire(instance, missileDef, &ahead, THROW_TYPE_POSITION);
}

/* MON_IdleMessageHandler (fixup), which the idle, walking and running
 * states call: 1 when the message was the possessed monster's command */
int port_uncut_monster_message(Instance *instance, Message *message)
{
    if ((instance == NULL) || (instance != possessed))
    {
        return 0;
    }

    if ((message->ID == 0) || (message->ID & 0xF2000000))
    {
        MON_PossessedMessageHandler(instance, message);
        return 1;
    }

    return 0;
}

/* --- the player side ------------------------------------------------------------ */

static int is_monster(Instance *instance)
{
    MonsterAttributes *attributes = (MonsterAttributes *)instance->data;

    return (instance != gameTrackerX.playerInstance) && (attributes != NULL) &&
        (attributes->magicnum == MAGIC_MONSTER) && (instance->extraData != NULL);
}

/* dead, or not in this realm (a sluagh in the material one: hidden, out of
 * the game until the realm changes) */
static int is_dead(Instance *instance)
{
    if ((instance->flags & 0x800) || (instance->flags2 & 0x24000000))
    {
        return 1;
    }

    switch (instance->currentMainState)
    {
    case MONSTER_STATE_IMPALEDEATH:
    case MONSTER_STATE_GENERALDEATH:
    case MONSTER_STATE_DEAD:
    case MONSTER_STATE_TERRAINIMPALEDEATH:
        return 1;
    }

    return 0;
}

/* the possessed instance is still in the game, as the same monster */
static int still_there(Instance *instance)
{
    Instance *i;

    for (i = gameTrackerX.instanceList->first; i != NULL; i = i->next)
    {
        if (i == instance)
        {
            return (i->introUniqueID == possessedID) && is_monster(i) && !is_dead(i);
        }
    }

    return 0;
}

/* the monster nearest the middle of the view, within range */
static Instance *aimed_monster(void)
{
    Instance *raziel = gameTrackerX.playerInstance;
    Position *eye = &theCamera.core.position;
    short view = MATH3D_AngleFromPosToPos(eye, &raziel->position);
    Instance *best = NULL;
    long bestScore = 0x7FFFFFFF;
    Instance *i;

    for (i = gameTrackerX.instanceList->first; i != NULL; i = i->next)
    {
        long distance;
        int off;

        if (!is_monster(i) || is_dead(i))
        {
            continue;
        }

        distance = MATH3D_LengthXYZ(i->position.x - raziel->position.x, i->position.y - raziel->position.y,
            i->position.z - raziel->position.z);
        off = abs(AngleDiff(view, MATH3D_AngleFromPosToPos(eye, &i->position)));

        if ((distance > POSSESS_RANGE) || (off > POSSESS_ANGLE))
        {
            continue;
        }

        if (off * 8 + distance < bestScore)
        {
            bestScore = off * 8 + distance;
            best = i;
        }
    }

    return best;
}

static void possess(Instance *instance)
{
    possessed = instance;
    possessedID = instance->introUniqueID;
    wasMoving = 0;

    /* controlled, as a level script would make it: its AI stands aside */
    INSTANCE_Post(instance, 0x4000E, 1);
    CAMERA_SetInstanceFocus(&theCamera, instance);

    /* Raziel's body is empty: no player control (as in cutscenes) */
    gameTrackerX.playerInstance->flags |= 0x100;

    printf("[rewraithed] -uncut: Raziel possesses %s\n", instance->introName);
}

static void release(int alive)
{
    if (alive)
    {
        INSTANCE_Post(possessed, 0, 0);
        INSTANCE_Post(possessed, 0x4000E, 0);
    }

    if (heldSwitch != NULL)
    {
        INSTANCE_Post(heldSwitch, 0x800020, 0);
        heldSwitch = NULL;
    }

    possessed = NULL;

    if (gameTrackerX.playerInstance != NULL)
    {
        gameTrackerX.playerInstance->flags &= ~0x100;
        CAMERA_SetInstanceFocus(&theCamera, gameTrackerX.playerInstance);
    }

    printf("[rewraithed] -uncut: Raziel is himself again\n");
}

/* --- switches -------------------------------------------------------------------- */

/* how far a target is off the monster's facing, -2048..2047
 * (MON_FacingOffset gives 0..4095) */
static int facing_off(Instance *monster, Instance *target)
{
    int off = MON_FacingOffset(monster, target) & 0xFFF;

    return (off >= 2048) ? off - 4096 : off;
}

/* a switch within reach, in front of the possessed monster: as Raziel's
 * senses find one (SENSES.c), a physical object (0x20) able to be switched
 * (0x40) */
static Instance *switch_in_reach(Instance *monster)
{
    Instance *best = NULL;
    long bestDistance = 0x7FFFFFFF;
    Instance *i;

    for (i = gameTrackerX.instanceList->first; i != NULL; i = i->next)
    {
        PhysObSwitchProperties *prop;
        long xy;
        long z;

        if ((i == monster) || (i == gameTrackerX.playerInstance) || (i->flags & 0x800) ||
            !(INSTANCE_Query(i, queryWhatAmI) & 0x20) || !(INSTANCE_Query(i, queryPhysicalAbility) & 0x40))
        {
            continue;
        }

        prop = (PhysObSwitchProperties *)INSTANCE_Query(i, queryPhysicalSwitchProperties);

        if (prop == NULL)
        {
            continue;
        }

        xy = MATH3D_LengthXYZ(i->position.x - monster->position.x, i->position.y - monster->position.y, 0);
        z = i->position.z - monster->position.z;

        if ((xy > prop->engageXYDistance + SWITCH_EXTRA_XY) ||
            (z < (signed char)prop->engageZMinDelta - SWITCH_EXTRA_Z) || (z > (signed char)prop->engageZMaxDelta + SWITCH_EXTRA_Z) ||
            (abs(facing_off(monster, i)) > SWITCH_ANGLE))
        {
            continue;
        }

        if (xy < bestDistance)
        {
            bestDistance = xy;
            best = i;
        }
    }

    return best;
}

/* the switch works as when Raziel pulls it (StateHandlerPullSwitch): it
 * plays its own animation and changes state, which the level's scripts
 * follow. Raziel's own pulling animation is for his body only. */
static int work_switch(Instance *monster)
{
    Instance *sw = switch_in_reach(monster);
    long switchClass;

    if (sw == NULL)
    {
        return 0;
    }

    switchClass = INSTANCE_Query(sw, queryPhysicalClass);

    if (switchClass == SWITCH_SIDED)
    {
        /* which side it is worked from */
        long status = INSTANCE_Query(sw, queryPhysicalStatus);
        short offset = MON_FacingOffset(monster, sw) & 0xFFF;

        if (offset > 2048)
        {
            offset |= 0xF000;
        }

        if (((offset + 1024) < 0) || ((offset + 1024) > 2048))
        {
            status |= 0x1;
        }
        else
        {
            status &= ~0x1;
        }

        INSTANCE_Post(sw, 0x80002A, status);
    }

    INSTANCE_Post(sw, 0x800020, 0);

    if (switchClass == SWITCH_HOLD)
    {
        heldSwitch = sw;
    }

    printf("[rewraithed] -uncut: %s works a switch (class %ld)\n", monster->introName, switchClass);

    return 1;
}

/* the direction pushed, as a world angle (Raziel's own ProcessMovement) */
static int pushed_direction(long *command, short *yaw)
{
    long x = command[3];
    long y = command[4];

    if ((x == 0) && (y == 0))
    {
        if (!(command[0] & CMD_DIRECTIONS))
        {
            return 0;
        }

        x = (command[0] & 0x4) ? -128 : ((command[0] & 0x8) ? 128 : 0);
        y = (command[0] & 0x1) ? -128 : ((command[0] & 0x2) ? 128 : 0);
    }

    *yaw = (short)((((1024 - ratan2((y * 4096) / 96, (x * 4096) / 96)) & 0xFFF) + theCamera.lagZ) & 0xFFF);

    return 1;
}

/* RazielPost (fixup): message 0x4000E, Raziel puppeted for a cutscene
 * (on != 0) or given back to the player */
static void put_raziel_back(void)
{
    Instance *raziel = gameTrackerX.playerInstance;
    evPositionData where;

    if (raziel == NULL)
    {
        return;
    }

    where.x = razielStood.x;
    where.y = razielStood.y;
    where.z = razielStood.z;

    STREAM_SetInstancePosition(raziel, &where);
    raziel->oldPos = raziel->position;
    raziel->rotation = razielFacing;
}

void port_uncut_cutscene(int on)
{
    inCutscene = on;

    if (on && (possessed != NULL))
    {
        printf("[rewraithed] -uncut: a cutscene starts\n");

        razielStood = gameTrackerX.playerInstance->position;
        razielFacing = gameTrackerX.playerInstance->rotation;
        putBack = 1;

        release(1);
    }
    else if (!on && putBack)
    {
        putBack = 0;
        put_raziel_back();

        /* and again in case the script moves him once it has let go */
        putBackFrames = 2;
    }
}

/* GAMEPAD_Process (fixup), once the pad has been read. Presses are found
 * here from the buttons held, as the game's own "pressed" commands are
 * changed below for Raziel. */
void port_uncut_frame(GameTracker *gameTracker)
{
    static long previousHeld;
    long *command = gameTracker->controlCommand[0];
    long held = command[0];
    long pressed = held & ~previousHeld;
    short yaw;

    previousHeld = held;

    if (!uncut_enabled())
    {
        return;
    }

    port_cut_content_frame();

    if ((mainTrackerX.mainState != 1) || (gameTracker->playerInstance == NULL))
    {
        return;
    }

    if ((putBackFrames > 0) && !inCutscene)
    {
        put_raziel_back();
        putBackFrames--;
    }

    if ((gameTracker->gameMode != 0) || inCutscene)
    {
        return;
    }

    /* turned off on the debug menu's Cut Content page */
    if (!port_cut_possession_on())
    {
        if (possessed != NULL)
        {
            release(1);
        }

        return;
    }

    if (possessed == NULL)
    {
        /* aiming, Feed: possession */
        if (((held & CMD_AIM) == CMD_AIM) && (pressed & CMD_FEED))
        {
            Instance *target = aimed_monster();

            if (target != NULL)
            {
                possess(target);

                /* the press is not Raziel's */
                command[1] &= ~CMD_FEED;
            }
        }

        return;
    }

    if (!still_there(possessed))
    {
        printf("[rewraithed] -uncut: %s is dead or gone\n", possessed->introName);
        release(0);
        return;
    }

    if (pressed & CMD_FEED)
    {
        release(1);
        command[1] &= ~CMD_FEED;
        return;
    }

    if (pushed_direction(command, &yaw))
    {
        MonsterVars *mv = (MonsterVars *)possessed->extraData;
        int walk = (held & CMD_STEALTH) != 0;

        /* walking (controlled) heads for mv->destination: just ahead */
        mv->destination.x = possessed->position.x + (short)((rsin(yaw) * 1500) >> 12);
        mv->destination.y = possessed->position.y - (short)((rcos(yaw) * 1500) >> 12);
        mv->destination.z = possessed->position.z;

        INSTANCE_Post(possessed, 0x10000000 | (walk ? 0x1000 : 0), yaw);
        wasMoving = 1;
    }
    else if (wasMoving)
    {
        INSTANCE_Post(possessed, 0, 0);
        wasMoving = 0;
    }

    /* a held lever goes back when Attack is let go of */
    if ((heldSwitch != NULL) && !(held & CMD_ATTACK))
    {
        INSTANCE_Post(heldSwitch, 0x800020, 0);
        heldSwitch = NULL;
    }

    /* Attack by a switch works it; anywhere else, attacks */
    if ((pressed & CMD_ATTACK) && !work_switch(possessed))
    {
        INSTANCE_Post(possessed, (int)0x80000000, 0);
    }

    if (pressed & CMD_TELEKINESIS)
    {
        INSTANCE_Post(possessed, (int)0x80000020, 0);
    }

    /* Raziel stands still (flag 0x100 above); the camera is left no
     * presses, nor the stick, to act on either. The buttons held stay as
     * they are: the game finds presses by comparing them frame to frame. */
    command[1] &= CMD_KEEP;
    command[2] &= CMD_KEEP;
    command[3] = 0;
    command[4] = 0;
}
