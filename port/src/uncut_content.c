/*
 * -uncut: the debug menu's "Cut Content" page, and the Soul Reaver's
 * elemental forms from the April 14, 1999 prototype.
 *
 * The page (at the bottom of the debug menu, -uncut only) turns possession
 * (uncut_possession.c) and the Spirit Glyph (uncut_spirit.c) on and off,
 * and picks one of five reavers: Sunlight, Water, Stone, Spirit, Sound (the
 * Fire Reaver is in the game already).
 *
 * July still carries all eight reavers' numbers (1 Spectral, 2 Material,
 * 3 Sunlight, 4 Water, 5 Stone, 6 Fire, 7 Spirit, 8 Sound), their colours in
 * the reaver's tune data and the debug flags that imbue them, but only the
 * Spectral, Material and Fire reavers do anything. April's reavers each hit
 * with their element (SoulReaverCollide, CollideReaverProjectile, fixups),
 * and April's MON_DefaultMessageHandler gives the element its effect:
 *
 *  - Sunlight: kills a stunned monster, else a hard hit;
 *  - Water:    the same, on monsters that water hurts; nothing on others;
 *  - Stone:    turns a monster to stone; a stunned or stone one dies;
 *  - Spirit:   kills, and Raziel takes the monster's soul (0x1000016);
 *  - Sound:    kills monsters (whatAmI 2), nothing on humans.
 *
 * July's reaver colours for Sunlight, Water, Stone, Spirit and Sound are
 * placeholders (a dark red Spirit Reaver); April's glow colours are put in
 * their place, with blade colours (and arm lightning) to match.
 * April's projectile models for the Water and Stone reavers are not on the
 * July disc: their thrown blasts are a plain force field.
 *
 * Picking a reaver gives Raziel the Soul Reaver ability if he lacks it (the
 * debug menu's Special Abilities, Soul Reaver), and the reaver imbued with
 * it; picking none leaves him the ability and the plain Material/Spectral
 * blade. The picked reaver is the
 * material realm's only: in the spectral realm Raziel has the plain
 * Spectral blade, and back in the material realm the picked reaver again.
 * The Fire Reaver still goes out under water, as in the game.
 */
#include "common.h"
#include "Game/INSTANCE.h"
#include "Game/DEBUG.h"
#include "Game/MONSTER/MONLIB.h"

#include <stdio.h>

extern GameTracker gameTrackerX;

int port_has_flag(const char *name);  /* pc_main.c */

#define REAVER_FIRST    3   /* Sunlight */
#define REAVER_LAST     8   /* Sound */
#define REAVER_BIT(n)   (1UL << ((n) + 9))  /* debugRazielFlags1/2 */
#define ABILITY_REAVER  0x8                 /* Raziel holds the Soul Reaver */
#define ABILITY_SOUL_REAVER 0x9             /* the debug menu's Soul Reaver ability */

#define CUT_POSSESSION  0x1
#define CUT_SPIRIT      0x2

static long cutToggles = CUT_POSSESSION | CUT_SPIRIT;
static long cutReavers;         /* 1 << reaver number, one at most */
static long shownReavers;       /* what the page showed last frame */
static int pendingReaver;       /* imbue this for a few more frames */
static int pendingFrames;
static int wasMaterial = -1;    /* the realm last frame */

DebugMenuLine port_cutContentMenu[] = {
    {DEBUG_LINE_TYPE_MENU, 0, 0, "MAIN MENU...", (long *)standardMenu, 0},
    {DEBUG_LINE_TYPE_BIT, 0, 0, "Possession", &cutToggles, CUT_POSSESSION},
    {DEBUG_LINE_TYPE_BIT, 0, 0, "Spirit Glyph", &cutToggles, CUT_SPIRIT},
    {DEBUG_LINE_TYPE_BIT, 0, 0, "Sunlight Reaver", &cutReavers, 1 << 3},
    {DEBUG_LINE_TYPE_BIT, 0, 0, "Water Reaver", &cutReavers, 1 << 4},
    {DEBUG_LINE_TYPE_BIT, 0, 0, "Stone Reaver", &cutReavers, 1 << 5},
    {DEBUG_LINE_TYPE_BIT, 0, 0, "Spirit Reaver", &cutReavers, 1 << 7},
    {DEBUG_LINE_TYPE_BIT, 0, 0, "Sound Reaver", &cutReavers, 1 << 8},
    {DEBUG_LINE_TYPE_ENDLIST, 0, 0, "", &gameTrackerX.debugFlags, 0},
};

int port_cut_possession_on(void)
{
    return (cutToggles & CUT_POSSESSION) != 0;
}

int port_cut_spirit_on(void)
{
    return (cutToggles & CUT_SPIRIT) != 0;
}

/* the reaver Raziel holds, from the debug flags the game keeps in step */
static int held_reaver(void)
{
    unsigned long mask = (debugRazielFlags2 >> 10) & 0xFF;
    int n = 0;

    if (!(debugRazielFlags1 & ABILITY_REAVER))
    {
        return 0;
    }

    while (mask != 0)
    {
        mask >>= 1;
        n++;
    }

    return n;
}

static int in_material(void)
{
    return gameTrackerX.gameData.asmData.MorphType == 0;
}

static void pick(int reaver)
{
    if (reaver != 0)
    {
        if ((debugRazielFlags1 & ABILITY_SOUL_REAVER) != ABILITY_SOUL_REAVER)
        {
            /* the Soul Reaver ability (Raziel menu, Special Abilities), for
             * good; ProcessSpecialAbilities births and picks up the reaver */
            debugRazielFlags1 |= ABILITY_SOUL_REAVER;
        }

        if (in_material())
        {
            /* the pick-up imbues it Material: imbue it again after that */
            pendingReaver = reaver;
            pendingFrames = 4;
        }
        else
        {
            /* the plain Spectral blade until Raziel is back in the material
             * realm */
            debugRazielFlags2 = 0;
        }

        printf("[rewraithed] -uncut: reaver %d (%s realm)\n", reaver, in_material() ? "material" : "spectral");
    }
    else
    {
        /* the Soul Reaver stays: back to the realm's plain blade */
        debugRazielFlags2 = 0;
        pendingFrames = 0;

        printf("[rewraithed] -uncut: plain reaver\n");
    }
}

static void add_debug_menu_line(void)
{
    int i;

    for (i = 0; standardMenu[i].type != DEBUG_LINE_TYPE_ENDLIST; i++)
    {
        if (standardMenu[i].var_address == (long *)port_cutContentMenu)
        {
            return;
        }
    }

    /* the fixup's spare line after the list's end (DEBUG.c) */
    if (standardMenu[i + 1].var_address == (long *)port_cutContentMenu)
    {
        DebugMenuLine end = standardMenu[i];

        standardMenu[i] = standardMenu[i + 1];
        standardMenu[i].type = DEBUG_LINE_TYPE_MENU;
        standardMenu[i + 1] = end;
    }
}

/* once a frame with -uncut (port_uncut_frame) */
void port_cut_content_frame(void)
{
    add_debug_menu_line();

    if (gameTrackerX.playerInstance == NULL)
    {
        return;
    }

    if (cutReavers != shownReavers)
    {
        long added = cutReavers & ~shownReavers;
        int n;

        /* one at a time: the one just turned on wins */
        if (added != 0)
        {
            cutReavers = added & -added;
        }

        for (n = 0; (n <= REAVER_LAST) && !(cutReavers & (1 << n)); n++)
        {
        }

        pick((n <= REAVER_LAST) ? n : 0);

        shownReavers = cutReavers;
        return;
    }

    if (pendingFrames > 0)
    {
        debugRazielFlags2 = REAVER_BIT(pendingReaver);

        /* counted in play only: Raziel does not pick it up in the menu */
        if (gameTrackerX.gameMode == 0)
        {
            pendingFrames--;
        }

        return;
    }

    {
        int material = in_material();
        int held = held_reaver();

        if (!material)
        {
            /* the spectral realm has the plain Spectral blade only */
            if ((held >= REAVER_FIRST) && (held <= REAVER_LAST))
            {
                debugRazielFlags2 = REAVER_BIT(1);
            }
        }
        else if ((wasMaterial == 0) && (cutReavers != 0) && (held != 0))
        {
            /* back in the material realm: the picked reaver again (once:
             * the game's own rules, as the Fire Reaver going out under
             * water, hold until the next shift) */
            int n;

            for (n = REAVER_FIRST; !(cutReavers & (1 << n)); n++)
            {
            }

            pendingReaver = n;
            pendingFrames = 4;
        }

        wasMaterial = material;
    }
}

/* April's reaction to a reaver of an element (MON_DefaultMessageHandler,
 * 0x100001F/0x1000021, power = the element). Returns 0 for July's own. */
int port_uncut_reaver_hit(Instance *instance, evMonsterHitData *data, int *fatal, int *damage)
{
    MonsterVars *mv = (MonsterVars *)instance->extraData;
    MonsterAttributes *ma = (MonsterAttributes *)instance->data;
    int stunned = (mv->mvFlags & 0x10) != 0;

    switch (data->power)
    {
    case 0x10:  /* water */
        if (mv->subAttr->waterVuln == 0)
        {
            return 1;
        }
        /* fall through */
    case 0x40:  /* sunlight */
        if (stunned)
        {
            *fatal = 1;
        }
        else
        {
            *damage = 12288;
        }

        break;
    case 0x400: /* stone */
        if (stunned || (instance->currentMainState == MONSTER_STATE_PETRIFIED))
        {
            *fatal = 1;
        }
        else if (instance->petrifyValue == 0)
        {
            MON_SwitchState(instance, MONSTER_STATE_PETRIFIED);

            instance->petrifyValue = 4096;
        }

        break;
    case 0x800: /* spirit: the soul goes to Raziel */
        *fatal = 1;

        INSTANCE_Post(gameTrackerX.playerInstance, 0x1000016, mv->soulJuice);

        mv->soulJuice = 0;
        break;
    case 0x200: /* sound */
        if (ma->whatAmI & 0x2)
        {
            *fatal = 1;
        }

        break;
    default:
        return 0;
    }

    /* the death's knockback reads the hit */
    if (*fatal)
    {
        mv->messageData = data;
    }

    return 1;
}

/* Colours for the reavers July left as placeholders (REAVER.c reads them
 * from the reaver's tune data; 0xBBGGRR): April's glow, and in place of
 * April's blade colours (the same blue for every reaver) a pale core and a
 * glow of the reaver's own colour, as July's Fire Reaver has. The blade
 * colours are also the lightning around Raziel's arm. */
void port_uncut_reaver_colors(Instance *reaver)
{
    static const unsigned long april[REAVER_LAST + 1][3] = {
        /*      glow      blade     blade glow */
        [3] = {0x089691, 0xE0F0F0, 0x10F0F0},  /* Sunlight: yellow */
        [4] = {0x721F18, 0xF0D0C0, 0xF04030},  /* Water: blue */
        [5] = {0x808080, 0xE0E0E0, 0xA0A0A0},  /* Stone: grey */
        [7] = {0x2D852F, 0xD0F0D0, 0x40F040},  /* Spirit: green */
        [8] = {0x721050, 0xF0C0E0, 0xF020A0},  /* Sound: purple */
    };
    ReaverTuneData *tune;
    int n;

    if (!port_has_flag("-uncut") || (reaver == NULL) || (reaver->data == NULL))
    {
        return;
    }

    tune = (ReaverTuneData *)reaver->data;

    for (n = REAVER_FIRST; n <= REAVER_LAST; n++)
    {
        if (n == 6)
        {
            continue;  /* the Fire Reaver is July's */
        }

        (&tune->spectralGlowColor)[n - 1] = april[n][0];
        (&tune->spectralInnerColor)[n - 1] = april[n][1];
        (&tune->spectralInnerGlowColor)[n - 1] = april[n][2];
    }
}
