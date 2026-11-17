/*
 * libgte routines that Soul Reaver links from its own hand-written assembly
 * (HASM_2.s), recompiled by tools/hasm2c.py (cmake/Hasm.cmake).
 *
 * PsyCross implements the same names, not always identically (its
 * ScaleMatrix scales rows; PsyQ's scales columns), so the game's calls are
 * routed to the game's own code.
 */
#ifndef REWRAITHED_LIBGTE_GAME_H
#define REWRAITHED_LIBGTE_GAME_H

MATRIX *port_RotMatrix(SVECTOR *r, MATRIX *m);
MATRIX *port_RotMatrixX(long r, MATRIX *m);
MATRIX *port_RotMatrixY(long r, MATRIX *m);
MATRIX *port_RotMatrixZ(long r, MATRIX *m);
MATRIX *port_ScaleMatrix(MATRIX *m, VECTOR *v);
int port_rsin(int a);
int port_rcos(int a);

#define RotMatrix   port_RotMatrix
#define RotMatrixX  port_RotMatrixX
#define RotMatrixY  port_RotMatrixY
#define RotMatrixZ  port_RotMatrixZ
#define ScaleMatrix port_ScaleMatrix
#define rsin        port_rsin
#define rcos        port_rcos

/* PsyQ libgte routines recompiled from soul-re's disassembly of the
 * executable (cmake/Hasm.cmake), when available. Their prototypes come from
 * the game's own headers, which these macros rename along with the calls. */
#if PORT_GAME_LIBGTE
#define TransposeMatrix    port_TransposeMatrix
#define LoadAverageShort12 port_LoadAverageShort12
#define LoadAverageCol     port_LoadAverageCol
#define MulMatrix0         port_MulMatrix0
#define MulMatrix2         port_MulMatrix2
#define CompMatrix         port_CompMatrix
#define ApplyMatrix        port_ApplyMatrix
#define ApplyMatrixSV      port_ApplyMatrixSV
#define ApplyMatrixLV      port_ApplyMatrixLV
#define TransMatrix        port_TransMatrix
#define RotTrans           port_RotTrans
#define SetRotMatrix       port_SetRotMatrix
#define SetLightMatrix     port_SetLightMatrix
#define SetColorMatrix     port_SetColorMatrix
#define SetTransMatrix     port_SetTransMatrix
#define SetDQA             port_SetDQA
#define SetDQB             port_SetDQB
#define SetBackColor       port_SetBackColor
#define SetFarColor        port_SetFarColor
#define SetGeomOffset      port_SetGeomOffset
#define SetGeomScreen      port_SetGeomScreen
#endif

#endif
