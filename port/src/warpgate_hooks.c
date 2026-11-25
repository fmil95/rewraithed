/*
 * Warp gates: the gate frame's inner edge no longer cracks while choosing a
 * destination.
 *
 * Between destinations the gate shows its "cloud cover" (the swirl,
 * WARPGATE_UnHideCloudCoverInUnit), faded in and out through its fadeValue.
 * The model renderer fades a model by depth-cueing its colours toward the fog
 * colour (modelFadeValue into the GTE's IR0), so a part-faded swirl is a
 * darker, bluer one, still opaque. The swirl is a few large polygons lying
 * over the frame's inner edge, a bevel of small terrain faces; sorted by depth
 * they come out sometimes in front of the bevel, sometimes behind it, as the
 * camera moves by a hair. At full strength the swirl's edge is as green as the
 * bevel and nothing shows; part-faded, it shows as dark bands and gaps along
 * the frame that come and go.
 *
 * So each frame the swirl is drawn a little further from the camera
 * (WARP_COVER_PUSH, along the line from the camera to it): far enough to sort
 * behind the bevel every time, near enough to stay in front of the view of the
 * destination, which the gate draws 128 units behind its opening
 * (GetFogColor's s_zval). It looks the same, a hair smaller.
 */
#include "common.h"
#include "Game/CAMERA.h"
#include "Game/OBTABLE.h"

#include <math.h>

extern GameTracker gameTrackerX;

#define WARP_COVER_PUSH     112
#define MAX_COVERS          8
#define OBJECT_WARP_FACE    23      /* objectAccess: the warp gates' cloud cover */

static Instance *covers[MAX_COVERS];
static long saved[MAX_COVERS][3];
static int coverCount;

/* before the frame is drawn */
void port_warpgate_push_covers(void)
{
    Object *warpFace = (Object *)objectAccess[OBJECT_WARP_FACE].object;
    Instance *instance;

    coverCount = 0;

    if ((warpFace == NULL) || (gameTrackerX.instanceList == NULL))
    {
        return;
    }

    for (instance = gameTrackerX.instanceList->first; (instance != NULL) && (coverCount < MAX_COVERS);
         instance = instance->next)
    {
        double dx, dy, dz, d;
        int k;

        if ((instance->object != warpFace) || (instance->matrix == NULL) || (instance->flags & 0x800))
        {
            continue;
        }

        dx = (double)(instance->matrix[0].t[0] - theCamera.core.position.x);
        dy = (double)(instance->matrix[0].t[1] - theCamera.core.position.y);
        dz = (double)(instance->matrix[0].t[2] - theCamera.core.position.z);
        d = sqrt((dx * dx) + (dy * dy) + (dz * dz));

        if (d < 1.0)
        {
            continue;
        }

        covers[coverCount] = instance;

        for (k = 0; k < 3; k++)
        {
            saved[coverCount][k] = instance->matrix[0].t[k];
        }

        instance->matrix[0].t[0] += (long)(dx * WARP_COVER_PUSH / d);
        instance->matrix[0].t[1] += (long)(dy * WARP_COVER_PUSH / d);
        instance->matrix[0].t[2] += (long)(dz * WARP_COVER_PUSH / d);
        coverCount++;
    }
}

/* after it: where the game keeps them */
void port_warpgate_restore_covers(void)
{
    int i, k;

    for (i = 0; i < coverCount; i++)
    {
        for (k = 0; k < 3; k++)
        {
            covers[i]->matrix[0].t[k] = saved[i][k];
        }
    }

    coverCount = 0;
}
