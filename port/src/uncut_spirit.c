/*
 * -uncut: the Spirit Glyph, from the April 14, 1999 prototype.
 *
 * April's glyph menu has eight glyphs; the Spirit Glyph is the one July no
 * longer has. Cast, it calls up a green ring well away from Raziel that
 * closes in on him, and every monster and human it passes over dies on the
 * spot. With -uncut it comes back as the eighth glyph of the menu (after the
 * plane shift glyph), with April's ring and April's effect on monsters
 * (GLYPH.c, MONMSG.c and FX.c fixups):
 *
 *  - the menu turns in eighths of a circle instead of sevenths;
 *  - it is always known; like the other glyphs, it can only be cast in the
 *    material realm and out of water;
 *  - it costs no mana, as all of April's glyphs;
 *  - Raziel casts it with the Stone Glyph's motions (April used one cast
 *    animation for all glyphs; July has one per glyph);
 *  - its icon is the icon sheet's eighth cell. On the July disc that is an
 *    unused swirl; tools/uncut_spirit_icon.py copies April's Spirit Glyph
 *    icon there, into the mods folder.
 *
 * Glyph numbers here are July's (1 Force ... 7 plane shift); the Spirit
 * Glyph is 8, posts 0x80008 to Raziel, and its ring 0x80008 to monsters.
 */
#include "common.h"
#include "Game/INSTANCE.h"
#include "Game/MONSTER/MONLIB.h"
#include "Game/MONSTER/MONTABLE.h"

#include <stdio.h>

extern GameTracker gameTrackerX;

int port_has_flag(const char *name);  /* pc_main.c */

#define SPIRIT_GLYPH    8
#define SPIRIT_ABILITY  (1 << (SPIRIT_GLYPH + 17))
#define SPIRIT_COST     0

static int uncut = -1;

static int spirit_enabled(void)
{
    if (uncut < 0)
    {
        uncut = port_has_flag("-uncut");

        if (uncut)
        {
            printf("[rewraithed] -uncut: the Spirit Glyph (glyph menu, after the plane shift glyph)\n");
        }
    }

    return uncut;
}

/* glyphs in the menu, and the turn from one to the next */
int port_uncut_glyphs(void)
{
    return spirit_enabled() ? SPIRIT_GLYPH : 7;
}

int port_uncut_glyph_step(void)
{
    return spirit_enabled() ? (4096 / SPIRIT_GLYPH) : 585;
}

/* the Spirit Glyph's bit among Raziel's glyphs: known (_GlyphIsGlyphSet)
 * and, before the realm and water leave only the plane shift glyph, usable
 * (queryGlyphStatus) */
long port_uncut_spirit_ability(void)
{
    return spirit_enabled() ? SPIRIT_ABILITY : 0;
}

int port_uncut_spirit_cost(void)
{
    return SPIRIT_COST;
}

/* the ring reached a monster or human: April's MON_DefaultMessageHandler,
 * 0x80005 there. It dies whatever it is vulnerable to. */
void port_uncut_spirit_hit(Instance *instance)
{
    /* the death's knockback reads the last hit (mv->messageData), which a
     * monster never hit has none of: this one is a push away from Raziel */
    static evMonsterHitData hit = {NULL, NULL, 100, 10, 0};

    MonsterVars *mv;
    evFXHitData data;
    void (*damageEffect)(Instance *instance, evFXHitData *data);

    mv = (MonsterVars *)instance->extraData;

    if ((mv == NULL) || (instance->currentMainState == MONSTER_STATE_GENERALDEATH) || (instance->currentMainState == MONSTER_STATE_DEAD))
    {
        return;
    }

    hit.sender = gameTrackerX.playerInstance;

    MON_SetFXHitData(instance, &data, 0x800, 1);

    damageEffect = (void (*)(Instance *, evFXHitData *))MONTABLE_GetDamageEffectFunc(instance);

    if (damageEffect != NULL)
    {
        damageEffect(instance, &data);
    }

    mv->damageType = 0x800;
    mv->messageData = &hit;

    MON_SwitchState(instance, MONSTER_STATE_GENERALDEATH);
}
