/*
 * Skipping in-engine cutscenes: hold Cross for 2 seconds during one, or all
 * of them with -skipcutscene or the debug menu's "Skip All Puppet Shows"
 * (debugFlags2 0x800000, which nothing else reads any more).
 * (Original definitions renamed to <name>_psx through port/overrides.txt.)
 *
 * The game has no way to skip an in-engine cutscene: a level's event script
 * takes Raziel's control away (gameFlags 0x10: SIGNAL_HandleStopPlayerControl
 * and the script commands that do the same), puts the camera on a cinematic
 * path (mode 5), and gives control back when the scene is over. Only under1's
 * opening checks a skip switch (ATTR_SKIP_PUPPET_SHOW, set in main_hooks.c).
 *
 * So a skipped scene is played out unseen: from the frame it is skipped, the
 * game updates without drawing, with voices off (a script waits for each
 * voice line to start and to finish only while voices are on), up to
 * SKIP_UPDATES_PER_FRAME times a frame, until control comes back, and always
 * an even number of them (each swaps the draw page); the screen keeps its
 * last picture meanwhile, and the next one shown is gameplay. The
 * scene's script runs exactly as written, so everything it opens, moves or
 * spawns ends up where it would have.
 */
#include <SDL.h>

#include "common.h"
#include "Game/CAMERA.h"
#include "Game/VOICEXA.h"

int port_has_flag(const char *name);
void port_osd_note(const char *fmt, ...);   /* osd.c */
unsigned short CINE_Pad(int pad);           /* CINEPSX.c: the pad's buttons, active-low */

void GAMELOOP_Process_psx(GameTracker *gameTracker);
void GAMELOOP_DisplayFrame_psx(GameTracker *gameTracker);
void port_warpgate_push_covers(void);       /* warpgate_hooks.c */
void port_warpgate_restore_covers(void);
void putVoiceCommand(XAVoiceTracker *vt, unsigned char voiceCmd, unsigned char nextVoiceStatus, int voiceCmdParam);

extern GameTracker gameTrackerX;
extern XAVoiceTracker voiceTracker;

#define SKIP_HOLD_MS            2000        /* how long Cross is held to skip */
#define PAD_CROSS               0x4000
#define SKIP_UPDATES_PER_FRAME  2000        /* about a minute of scene a frame */
#define SKIP_LIMIT              (30 * 60 * 5)   /* give up after 5 minutes of game time in one scene */
#define DEBUG2_SKIP_ALL         0x800000    /* "Skip All Puppet Shows" */

static int skip_drawing_off;        /* while skipping: GAMELOOP_DisplayFrame draws nothing */
static int skip_active;             /* playing out a skipped scene */
static long skip_frames;
static unsigned long skip_voice_flag;   /* debugFlags 0x80000 (voices on) before the scene */
static Uint32 hold_since;           /* when Cross was pressed in this scene, 0 if not held */
static int hold_shown;              /* the seconds left last shown */
static int skip_gave_up;            /* this scene ran past SKIP_LIMIT: it plays as it is */

static int skip_all(void)
{
    static int flag = -1;

    if (flag < 0)
    {
        flag = port_has_flag("-skipcutscene");
    }

    return flag || ((gameTrackerX.debugFlags2 & DEBUG2_SKIP_ALL) != 0);
}

static int in_cutscene(void)
{
    return ((gameTrackerX.gameFlags & 0x10) != 0) && (theCamera.mode == 5) &&
           (gameTrackerX.gameMode != 6) && (gameTrackerX.levelDone == 0);
}

static int cross_held(void)
{
    return !(CINE_Pad(0) & PAD_CROSS) || !(CINE_Pad(1) & PAD_CROSS);
}

/* 1 once Cross has been held long enough in this scene; a note counts down */
static int held_to_skip(void)
{
    Uint32 now = SDL_GetTicks();
    int left;

    if (!cross_held())
    {
        hold_since = 0;
        return 0;
    }

    if (hold_since == 0)
    {
        hold_since = now ? now : 1;
        hold_shown = -1;
    }

    if (now - hold_since >= SKIP_HOLD_MS)
    {
        return 1;
    }

    left = (int)((SKIP_HOLD_MS - (now - hold_since) + 999) / 1000);

    if (left != hold_shown)
    {
        hold_shown = left;
        port_osd_note("Hold to skip the cutscene: %d", left);
    }

    return 0;
}

static void skip_start(void)
{
    skip_active = 1;
    skip_frames = 0;
    skip_voice_flag = gameTrackerX.debugFlags & 0x80000;
    gameTrackerX.debugFlags &= ~0x80000;

    if (voiceTracker.voiceStatus != 0)
    {
        putVoiceCommand(&voiceTracker, 1, 0, 0);     /* voiceCmdStop: the line that is playing */
    }

    printf("[rewraithed] cutscene: skipping\n");
}

static void skip_end(void)
{
    skip_active = 0;
    hold_since = 0;
    gameTrackerX.debugFlags |= skip_voice_flag;

    printf("[rewraithed] cutscene: skipped (%ld updates)\n", skip_frames);
}

void GAMELOOP_DisplayFrame(GameTracker *gameTracker)
{
    if (skip_drawing_off)
    {
        return;
    }

    /* the warp gates' swirls drawn behind their frames' edges (warpgate_hooks.c) */
    port_warpgate_push_covers();
    GAMELOOP_DisplayFrame_psx(gameTracker);
    port_warpgate_restore_covers();
}

void GAMELOOP_Process(GameTracker *gameTracker)
{
    int i;

    if (!skip_active)
    {
        GAMELOOP_Process_psx(gameTracker);

        if (!in_cutscene())
        {
            hold_since = 0;
            skip_gave_up = 0;
            return;
        }

        if (skip_gave_up || (!skip_all() && !held_to_skip()))
        {
            return;
        }

        skip_start();
    }

    /* the rest of the scene, unseen */
    skip_drawing_off = 1;

    for (i = 0; (i < SKIP_UPDATES_PER_FRAME) && in_cutscene() && (skip_frames < SKIP_LIMIT); i++)
    {
        GAMELOOP_Process_psx(gameTracker);
        skip_frames++;
    }

    skip_drawing_off = 0;

    if (!in_cutscene() || (skip_frames >= SKIP_LIMIT))
    {
        /* every update swaps the draw page, its draw list and its primitive
         * pool (ResetPrimPool) as if it was shown: after an odd number of
         * unseen ones the game would draw into the page not on screen, which
         * would keep showing a picture from the scene. One more evens them. */
        if (skip_frames & 1)
        {
            skip_drawing_off = 1;
            GAMELOOP_Process_psx(gameTracker);
            skip_drawing_off = 0;
            skip_frames++;
        }

        skip_end();

        if (skip_frames >= SKIP_LIMIT)
        {
            skip_gave_up = 1;
        }
    }
}
