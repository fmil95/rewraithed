/*
 * -skipcutscene: no cutscenes.
 * (Original definitions renamed to <name>_psx through port/overrides.txt.)
 *
 * The game has no way to skip an in-engine cutscene: a level's event script
 * takes Raziel's control away (gameFlags 0x90: SIGNAL_HandleStopPlayerControl
 * and the script commands that do the same), puts the camera on a cinematic
 * path (mode 5), and gives control back when the scene is over. Only under1's
 * opening checks a skip switch (ATTR_SKIP_PUPPET_SHOW, set in main_hooks.c).
 *
 * So the other scenes are fast-forwarded: while Raziel has no control and the
 * camera is cinematic, every frame runs FF_FRAMES more game updates without
 * drawing them, with voices off (a script waits for each voice line to start
 * and to finish only while voices are on), until control comes back. The
 * scene plays out exactly as scripted, just too fast to see, so everything it
 * opens, moves or spawns ends up where it would have.
 */
#include <SDL.h>

#include "common.h"
#include "Game/CAMERA.h"
#include "Game/VOICEXA.h"

int port_has_flag(const char *name);

void GAMELOOP_Process_psx(GameTracker *gameTracker);
void GAMELOOP_DisplayFrame_psx(GameTracker *gameTracker);
void putVoiceCommand(XAVoiceTracker *vt, unsigned char voiceCmd, unsigned char nextVoiceStatus, int voiceCmdParam);

extern GameTracker gameTrackerX;
extern XAVoiceTracker voiceTracker;

#define FF_FRAMES 16                /* game updates per drawn frame: about 8 s of scene per second */
#define FF_LIMIT (30 * 60 * 5)      /* give up after 5 minutes of game time in one scene */

static int ff_drawing_off;          /* inside the extra updates: GAMELOOP_DisplayFrame draws nothing */
static int ff_active;               /* in a fast-forwarded scene */
static long ff_frames;
static unsigned long ff_voice_flag; /* debugFlags 0x80000 (voices on) before the scene */

static int skip_cutscenes(void)
{
    static int enabled = -1;

    if (enabled < 0)
    {
        enabled = port_has_flag("-skipcutscene");
    }

    return enabled;
}

static int in_cutscene(void)
{
    return ((gameTrackerX.gameFlags & 0x10) != 0) && (theCamera.mode == 5) &&
           (gameTrackerX.gameMode != 6) && (gameTrackerX.levelDone == 0);
}

static void ff_start(void)
{
    ff_active = 1;
    ff_frames = 0;
    ff_voice_flag = gameTrackerX.debugFlags & 0x80000;
    gameTrackerX.debugFlags &= ~0x80000;

    if (voiceTracker.voiceStatus != 0)
    {
        putVoiceCommand(&voiceTracker, 1, 0, 0);     /* voiceCmdStop: the line that just started */
    }

    printf("[rewraithed] cutscene: fast-forwarding\n");
}

static void ff_end(void)
{
    ff_active = 0;
    gameTrackerX.debugFlags |= ff_voice_flag;

    printf("[rewraithed] cutscene: skipped (%ld frames)\n", ff_frames);
}

void GAMELOOP_DisplayFrame(GameTracker *gameTracker)
{
    if (ff_drawing_off)
    {
        return;
    }

    GAMELOOP_DisplayFrame_psx(gameTracker);
}

void GAMELOOP_Process(GameTracker *gameTracker)
{
    int i;

    GAMELOOP_Process_psx(gameTracker);

    if (!skip_cutscenes())
    {
        return;
    }

    if (!in_cutscene() || (ff_frames >= FF_LIMIT))
    {
        if (ff_active && !in_cutscene())
        {
            ff_end();
        }

        return;
    }

    if (!ff_active)
    {
        ff_start();
    }

    ff_drawing_off = 1;

    for (i = 0; (i < FF_FRAMES) && in_cutscene(); i++)
    {
        GAMELOOP_Process_psx(gameTracker);
        ff_frames++;
    }

    ff_drawing_off = 0;

    if (!in_cutscene())
    {
        ff_end();
    }
}
