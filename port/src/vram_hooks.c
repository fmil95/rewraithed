/*
 * -vramlog (or Enable Logging in the launcher): log how object textures are
 * placed in video memory.
 * (Original definitions renamed to <name>_psx through port/overrides.txt.)
 *
 * Each object type gets one block in the object area (x 512..1023,
 * y 256..511). When a block does not fit, the game compacts the area and
 * tries again; if it still does not fit, the object is used without a block
 * and its textures look scrambled. The log goes to vram.log next to the
 * executable (its own file: the game log can be overwritten by other writers):
 *
 *   [vram] <unit> get  <object> 44x256 -> 576,256    (or "-> FAILED")
 *   [vram] <unit> free <object> 576,256 44x256
 *   [vram] <unit> rearrange: <n> blocks, <columns> of 512 columns used
 *
 * After each failure and each compaction the used blocks are listed. The
 * editor's vrambudget.py simulates the same allocator; this log is what it
 * is calibrated against.
 */
#include <SDL.h>

#include "common.h"
#include "Game/VRAM.h"
#include "Game/STREAM.h"
#include "settings.h"

int port_has_flag(const char *name);

long VRAM_GetObjectVramSpace_psx(VramSize *vramSize, ObjectTracker *objectTracker);
void VRAM_RearrangeVramsLayer_psx(long whichLayer);
void VRAM_ClearVramBlock_psx(BlockVramEntry *block);

extern BlockVramEntry *usedVramBlocks;
extern GameTracker gameTrackerX;

static int in_rearrange;

static FILE *vram_log;

static int vram_log_enabled(void)
{
    static int enabled = -1;
    char path[1024];
    char *base;

    if (enabled < 0)
    {
        enabled = 0;

        /* -vramlog, or the launcher's Enable Logging */
        if (port_has_flag("-vramlog") || port_settings.logging)
        {
            base = SDL_GetBasePath();
            SDL_snprintf(path, sizeof(path), "%svram.log", (base != NULL) ? base : "");
            SDL_free(base);

            vram_log = fopen(path, "w");
            enabled = (vram_log != NULL);
        }
    }

    return enabled;
}

static const char *current_unit(void)
{
    if ((gameTrackerX.level != NULL) && (gameTrackerX.level->worldName != NULL))
    {
        return gameTrackerX.level->worldName;
    }

    return "?";
}

static const char *block_name(BlockVramEntry *b)
{
    switch (b->type)
    {
    case 2:
        return (b->udata.streamObject != NULL) ? b->udata.streamObject->name : "(object)";
    case 3:
        return "(font)";
    case 4:
        return "(button)";
    default:
        return "(other)";
    }
}

static void list_object_area(void)
{
    BlockVramEntry *b;
    long area;
    int n;

    area = 0;
    n = 0;

    for (b = usedVramBlocks; b != NULL; b = b->next)
    {
        if (b->y >= 256)
        {
            fprintf(vram_log, "[vram]     %-8s %d,%d %dx%d\n", block_name(b), b->x, b->y, b->w, b->h);
            area += (long)b->w * b->h;
            n++;
        }
    }

    fprintf(vram_log, "[vram]     %d blocks, %ld of 512 columns used\n", n, area / 256);
    fflush(vram_log);
}

long VRAM_GetObjectVramSpace(VramSize *vramSize, ObjectTracker *objectTracker)
{
    long result;
    BlockVramEntry *b;

    result = VRAM_GetObjectVramSpace_psx(vramSize, objectTracker);

    if (vram_log_enabled())
    {
        b = (BlockVramEntry *)objectTracker->vramBlock;

        if ((result != 0) && (b != NULL))
        {
            fprintf(vram_log, "[vram] %s get  %s %dx%d -> %d,%d\n", current_unit(), objectTracker->name,
                   vramSize->w, vramSize->h, b->x, b->y);
            fflush(vram_log);
        }
        else
        {
            fprintf(vram_log, "[vram] %s get  %s %dx%d -> FAILED, its textures will be wrong\n", current_unit(),
                   objectTracker->name, vramSize->w, vramSize->h);
            list_object_area();
        }
    }

    return result;
}

void VRAM_RearrangeVramsLayer(long whichLayer)
{
    in_rearrange++;
    VRAM_RearrangeVramsLayer_psx(whichLayer);
    in_rearrange--;

    if (vram_log_enabled() && (whichLayer != 0))
    {
        fprintf(vram_log, "[vram] %s rearrange (object area compacted)\n", current_unit());
        list_object_area();
    }
}

void VRAM_ClearVramBlock(BlockVramEntry *block)
{
    if (vram_log_enabled() && !in_rearrange && (block != NULL) && (block->y >= 256))
    {
        fprintf(vram_log, "[vram] %s free %s %d,%d %dx%d\n", current_unit(), block_name(block),
               block->x, block->y, block->w, block->h);
        fflush(vram_log);
    }

    VRAM_ClearVramBlock_psx(block);
}
