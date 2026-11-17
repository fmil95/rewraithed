/*
 * Inline MIPS assembly macros from soul-re's game headers, as C on the
 * software GTE.
 *
 * port/include/port/prefix.h turns __asm__ into nothing, which drops these
 * macros without a compile error. The prefix includes this header in every
 * game file: it first includes the soul-re headers that define the macros,
 * whose include guards keep them from redefining the macros later, and then
 * replaces them.
 */
#ifndef REWRAITHED_HASM_INLINE_H
#define REWRAITHED_HASM_INLINE_H

#include "port/gte_compat.h"

#include "Game/G2/ANIMG2.h"
#include "Game/MATH3D.h"

/* Game/G2/ANIMG2.h: segMatrix = (GTE rotation matrix) * segMatrix, one
 * column at a time. Each column is loaded into IR1-IR3 and multiplied with
 * "mvmva 1, 0, 3, 3, 0" (.word 0x4A49E012), and the result read back from
 * IR1-IR3. G2Matrix starts with short rotScale[3][3]. */
#undef hasm_segmatrixop
#define hasm_segmatrixop(r0) port_hasm_segmatrixop((void *)(r0))

static __inline void port_hasm_segmatrixop(void *matrix)
{
    int col;

    for (col = 0; col < 3; col++)
    {
        MTC2(PORT_U16(matrix, (0 * 3 + col) * 2), 9);
        MTC2(PORT_U16(matrix, (1 * 3 + col) * 2), 10);
        MTC2(PORT_U16(matrix, (2 * 3 + col) * 2), 11);

        gte_nrtir12();

        PORT_S16(matrix, (0 * 3 + col) * 2) = (short)MFC2(9);
        PORT_S16(matrix, (1 * 3 + col) * 2) = (short)MFC2(10);
        PORT_S16(matrix, (2 * 3 + col) * 2) = (short)MFC2(11);
    }
}

/* Game/MATH3D.h: squared length through the GTE. hasm_sqrlen1 loads
 * IR1-IR3, the caller runs gte_sqr0(), and hasm_sqrlen2 reads MAC1-MAC3 back
 * into its (nominally input) operands: r0 = MAC1 + MAC2 + MAC3, r1 = MAC2,
 * r2 = MAC3. */
#undef hasm_sqrlen1
#define hasm_sqrlen1(r0, r1, r2)                 \
    {                                            \
        MTC2((unsigned int)(r0), 9);             \
        MTC2((unsigned int)(r1), 10);            \
        MTC2((unsigned int)(r2), 11);            \
    }

#undef hasm_sqrlen2
#define hasm_sqrlen2(r0, r1, r2)                 \
    {                                            \
        (r1) = MFC2(26);                         \
        (r2) = MFC2(27);                         \
        (r0) = MFC2(25) + (r1) + (r2);           \
    }

#endif
