/*
 * PsyQ library functions the game uses that PsyCross does not provide.
 */
#include "common.h"

/* --- libgte ------------------------------------------------------------- */

/* Fallbacks for when soul-re's disassembly of the PsyQ library is not
 * available (see cmake/Hasm.cmake); otherwise the original routines are
 * recompiled and used. */
#if !PORT_GAME_LIBGTE

MATRIX *TransposeMatrix(MATRIX *m0, MATRIX *m1)
{
    MATRIX t;
    int i, j;

    for (i = 0; i < 3; i++)
    {
        for (j = 0; j < 3; j++)
        {
            t.m[i][j] = m0->m[j][i];
        }
    }

    for (i = 0; i < 3; i++)
    {
        for (j = 0; j < 3; j++)
        {
            m1->m[i][j] = t.m[i][j];
        }
    }

    return m1;
}

/* Same GTE sequence as the PsyQ gte_LoadAverageShort12 macro. */
void LoadAverageShort12(SVECTOR *v0, SVECTOR *v1, long p0, long p1, SVECTOR *v2)
{
    gte_lddp(p0);
    gte_ldsv(v0);
    gte_gpf12();
    gte_lddp(p1);
    gte_ldsv(v1);
    gte_gpl12();
    gte_stsv(v2);
}

/* Same GTE sequence as the PsyQ gte_LoadAverageCol macro. */
void LoadAverageCol(u_char *v0, u_char *v1, long p0, long p1, u_char *v2)
{
    gte_lddp(p0);
    gte_ldcv(v0);
    gte_gpf12();
    gte_lddp(p1);
    gte_ldcv(v1);
    gte_gpl12();
    gte_stcv(v2);
}

#endif /* !PORT_GAME_LIBGTE */

/* RotMatrixZYX comes from the game's own assembly (cmake/Hasm.cmake). */

/* --- libgpu ------------------------------------------------------------- */

void SetDrawStp(DR_STP *p, int pbw)
{
    setlen(p, 2);
    p->code[0] = 0xE6000000 | (pbw != 0);
    p->code[1] = 0;
}

/* --- libcd -------------------------------------------------------------- */

int StCdIntrFlag;

int CdReset(int mode)
{
    (void)mode;
    return 1;
}

/* Disc check (CdlGetID): always pass. */
int checkagain(void)
{
    return 0;
}
