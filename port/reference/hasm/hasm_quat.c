/*
 * C versions of the hand-written maths routines in HASM_2.s:
 * MATH3D_racos_S, G2Quat_ToMatrix_S, G2Quat_FromMatrix_S, G2Quat_FromEuler_S.
 *
 * Written from the MIPS, one register at a time, with the GTE work going
 * through the PsyCross software GTE (MTC2/MFC2/doCOP2), so that results,
 * rounding and saturation follow the original. Local names mirror the MIPS
 * registers to keep the correspondence checkable.
 */
#include "common.h"
#include "Game/HASM.h"
#include "Game/MATH3D.h"

extern short ecostable[];

/* GPF with sf: MAC = IR0 * IR (>> 12 when sf = 1), IR = saturated MAC. */
#define GTE_GPF(sf) doCOP2(0x0190003D | ((sf) << 19))

/* R3000 DIV: no trap on division by zero (LO = -1 or +1). */
static int mips_div(int num, int den)
{
    if (den == 0)
    {
        return (num >= 0) ? -1 : 1;
    }

    return num / den;
}

#define IR(n)   ((int)(short)MFC2(8 + (n)))
#define MAC(n)  ((int)MFC2(24 + (n)))

/* Binary search of ecostable for the angle whose cosine is |a|. */
short MATH3D_racos_S(long a)
{
    int v0 = 0x200;
    int t2 = 0x100;
    int t3 = (a >= 0) ? a : -a;
    int t4;
    int t5;

    do
    {
        t5 = ecostable[v0 + 0x400] - t3;
        t4 = t2;

        if (t5 == 0)
        {
            break;
        }

        t2 >>= 1;

        if (t5 < 0)
        {
            t4 = -t4;
        }

        v0 += t4;
    } while (t2 != 0);

    if (a < 0)
    {
        v0 = 0x800 - v0;
    }

    return (short)v0;
}

void G2Quat_ToMatrix_S(G2Quat *quat, G2Matrix *matrix)
{
    short *m = (short *)matrix;
    int x = quat->x;
    int y = quat->y;
    int z = quat->z;
    int w = quat->w;
    int t0, t1, t2, t3, t4, t5, t6, t7, t8;

    MTC2(x << 1, 8);
    MTC2(x, 9);
    MTC2(w, 10);
    MTC2(y, 11);
    GTE_GPF(1);
    t0 = MAC(1);
    t1 = MAC(2);
    t2 = MAC(3);

    MTC2(y << 1, 8);
    MTC2(y, 9);
    MTC2(w, 10);
    MTC2(z, 11);
    GTE_GPF(1);
    t3 = MAC(1);
    t4 = MAC(2);
    t5 = MAC(3);

    MTC2(z << 1, 8);
    MTC2(z, 9);
    MTC2(w, 10);
    MTC2(x, 11);
    GTE_GPF(1);
    t6 = MAC(1);
    t7 = MAC(2);
    t8 = MAC(3);

    m[7] = (short)(t5 + t1);
    m[8] = (short)(0x1000 - (t0 + t3));
    m[0] = (short)(0x1000 - (t3 + t6));
    m[1] = (short)(t2 - t7);
    m[2] = (short)(t8 + t4);
    m[3] = (short)(t2 + t7);
    m[4] = (short)(0x1000 - (t0 + t6));
    m[5] = (short)(t5 - t1);
    m[6] = (short)(t8 - t4);
}

void G2Quat_FromMatrix_S(G2Quat *quat, G2Matrix *matrix)
{
    short *m = (short *)matrix;
    short *q = (short *)quat;
    int t0 = m[0];
    int t1 = m[4];
    int t2 = m[8];
    int trace = t0 + t1 + t2;
    int v0;
    int lo;

    if (trace > 0)
    {
        v0 = MATH3D_FastSqrt(trace + 0x1000);
        lo = mips_div(0x800000, v0);

        q[3] = (short)(v0 >> 1);

        MTC2(m[7] - m[5], 9);
        MTC2(m[2] - m[6], 10);
        MTC2(m[3] - m[1], 11);
        MTC2(lo, 8);
        GTE_GPF(1);

        q[0] = (short)IR(1);
        q[1] = (short)IR(2);
        q[2] = (short)IR(3);
    }
    else
    {
        /* i, j, k as element indices (the MIPS keeps byte offsets) */
        int i = 0, j = 1, k = 2;
        int t3 = t0;

        if (t0 < t1)
        {
            i = 1, j = 2, k = 0;
            t3 = t1;
        }

        if (t3 < t2)
        {
            i = 2, j = 0, k = 1;
            t3 = t2;
        }

        v0 = MATH3D_FastSqrt(m[i * 4] - (m[j * 4] + m[k * 4]) + 0x1000);
        lo = mips_div(0x800000, v0);

        q[i] = (short)(v0 >> 1);

        MTC2(lo, 8);
        MTC2(m[k * 3 + j] - m[j * 3 + k], 9);
        MTC2(m[j * 3 + i] + m[i * 3 + j], 10);
        MTC2(m[k * 3 + i] + m[i * 3 + k], 11);
        GTE_GPF(1);

        q[3] = (short)IR(1);
        q[j] = (short)IR(2);
        q[k] = (short)IR(3);
    }
}

/* sin/cos of a 12-bit angle from ecostable (half-period table + sign). */
static void eco_sincos(int angle, int *s, int *c)
{
    int idx = angle & 0x7FF;

    *s = ecostable[idx];
    *c = ecostable[idx + 0x400];

    if (angle & 0x800)
    {
        *s = -*s;
        *c = -*c;
    }
}

void G2Quat_FromEuler_S(G2Quat *quat, G2EulerAngles *euler)
{
    char *base = (char *)quat;
    int t0 = euler->x;
    int t2 = euler->y;
    int t4 = euler->z;
    int a1 = euler->order;
    int t1, t3, t5, t6, t7, t8, t9;
    int v1;
    int s0, c0, s1, c1, s2, c2;

    if (a1 & 1)
    {
        int tmp = t0;

        t0 = t4;
        t4 = tmp;
    }

    t0 >>= 1;

    if (a1 & 4)
    {
        t2 = -t2;
    }

    t2 >>= 1;
    t4 >>= 1;

    eco_sincos(t0, &s0, &c0);
    eco_sincos(t2, &s1, &c1);
    eco_sincos(t4, &s2, &c2);

    MTC2(c0, 8);
    MTC2(c2, 9);
    MTC2(s2, 10);
    GTE_GPF(1);
    t6 = IR(1);                 /* c0*c2 */
    t7 = IR(2);                 /* c0*s2 */

    MTC2(s0, 8);
    MTC2(c2, 9);
    MTC2(s2, 10);
    GTE_GPF(1);

    v1 = (a1 & 0x18) >> 2;
    t8 = IR(1);                 /* s0*c2 */
    t9 = IR(2);                 /* s0*s2 */

    if (a1 & 2)
    {
        t0 = t7 + t8;
        t5 = t6 - t9;

        MTC2(c1, 8);
        MTC2(t0, 9);
        MTC2(t5, 10);
        GTE_GPF(1);

        a1 = (a1 & 4) >> 1;
        t1 = t6 + t9;
        t4 = t7 - t8;
        t0 = IR(1);
        t5 = IR(2);

        MTC2(s1, 8);
        MTC2(t1, 9);
        MTC2(t4, 10);
        GTE_GPF(1);

        t1 = IR(1);
        t4 = IR(2);
    }
    else
    {
        long long lo1;
        long long lo2;
        int m1, m2, m3;

        MTC2(c1, 8);
        MTC2(t8, 9);
        MTC2(t9, 10);
        MTC2(t7, 11);
        lo1 = (long long)c1 * t6;
        GTE_GPF(0);

        a1 = (a1 & 4) >> 1;
        t0 = MAC(1);
        t1 = MAC(2);
        t4 = MAC(3);
        t5 = (int)lo1;

        MTC2(s1, 8);
        MTC2(t7, 9);
        MTC2(t6, 10);
        MTC2(t8, 11);
        lo2 = (long long)s1 * t9;
        GTE_GPF(0);

        m1 = MAC(1);
        m2 = MAC(2);
        m3 = MAC(3);

        t0 = (t0 - m1) >> 12;
        t1 = (t1 + m2) >> 12;
        t4 = (t4 - m3) >> 12;
        t5 = (t5 + (int)lo2) >> 12;
    }

    t8 = v1 + a1 + 2;

    if (t8 >= 5)
    {
        t8 -= 6;
    }

    t9 = (v1 - a1) + 4;

    if (t9 >= 5)
    {
        t9 -= 6;
    }

    if (a1 != 0)
    {
        t1 = -t1;
    }

    *(short *)(base + v1) = (short)t0;
    *(short *)(base + t8) = (short)t1;
    *(short *)(base + t9) = (short)t4;
    quat->w = (short)t5;
}
