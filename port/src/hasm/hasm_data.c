/*
 * Data that soul-re keeps in the hand-written assembly (HASM_1.s .sdata).
 */
#include "common.h"
#include "Game/HASM.h"

long depthQPTable;
long depthQBlendStart;
long depthQFogStart;
long depthQFogFar;
long depthQBackColor;

volatile int gameTimer;
long modelFadeValue;
long lowerSplitPush = -100;
short forceDrawInFront;
long draw_belowSplit = 1;

/* These point into the scratchpad (0x1F800028..0x1F80006C on PSX). */
SVECTOR *face_v0;
SVECTOR *face_v1;
SVECTOR *face_v2;
SVECTOR *face_v01;
SVECTOR *face_v12;
SVECTOR *face_v20;
struct UVTYPE *face_uv0;
struct UVTYPE *face_uv1;
struct UVTYPE *face_uv2;
struct UVTYPE *face_uv01;
struct UVTYPE *face_uv12;
struct UVTYPE *face_uv20;

#define SCRATCH(offset) ((void *)(_scratchData + (offset)))

void port_hasm_init(void)
{
    face_v0   = SCRATCH(0x28);
    face_v1   = SCRATCH(0x30);
    face_v2   = SCRATCH(0x38);
    face_v01  = SCRATCH(0x40);
    face_v12  = SCRATCH(0x48);
    face_v20  = SCRATCH(0x50);
    face_uv0  = SCRATCH(0x58);
    face_uv1  = SCRATCH(0x5C);
    face_uv2  = SCRATCH(0x60);
    face_uv01 = SCRATCH(0x64);
    face_uv12 = SCRATCH(0x68);
    face_uv20 = SCRATCH(0x6C);
}
