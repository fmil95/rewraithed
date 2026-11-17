/*
 * C versions of the hand-written collision routines in HASM_2.s, plus the
 * game's ApplyMatrix_S. Register-level translations of the MIPS, with the
 * GTE work done by the PsyCross software GTE. Rotation-matrix registers are
 * used as scratch vectors exactly as the original does.
 */
#include "common.h"
#include "Game/HASM.h"

/* MVMVA sf=1, mx=RT, v=V0, cv=none, lm=0: MAC1..3 = RT * V0 >> 12 */
#define GTE_RTV0()  doCOP2(0x0400012 | (1 << 19) | (0 << 17) | (0 << 15) | (3 << 13))
#define GTE_GPF1()  doCOP2(0x0198003D)

#define IR(n)   ((int)(short)MFC2(8 + (n)))
#define MAC(n)  ((int)MFC2(24 + (n)))

#define U32(p, off) (*(unsigned int *)((char *)(p) + (off)))
#define U16(p, off) (*(unsigned short *)((char *)(p) + (off)))
#define S16(p, off) (*(short *)((char *)(p) + (off)))

static int mips_div(int num, int den)
{
    if (den == 0)
    {
        return (num >= 0) ? -1 : 1;
    }

    return num / den;
}

void COLLIDE_NearestPointOnPlane_S(SVector *planePoint, SVector *normal, long z, Position *position)
{
    unsigned int t3 = U32(position, 0);
    int t5 = S16(position, 4);
    unsigned int t0 = U32(normal, 0);
    int t2 = S16(normal, 4);
    int t1 = (int)t0 >> 16;
    int t4;

    CTC2(t3, 0);
    CTC2(t5, 1);
    MTC2(t0, 0);
    MTC2(t2, 1);
    GTE_RTV0();

    t4 = z - MAC(1);

    MTC2(t4, 8);
    MTC2((short)t0, 9);
    MTC2(t1, 10);
    MTC2(t2, 11);
    GTE_GPF1();

    S16(planePoint, 0) = (short)(IR(1) + (short)t3);
    S16(planePoint, 2) = (short)(IR(2) + ((int)t3 >> 16));
    S16(planePoint, 4) = (short)(IR(3) + t5);
}

long COLLIDE_IntersectLineAndPlane_S(SVector *planePoint, Position *oldPos, Position *position, SVector *normal, long z)
{
    unsigned int v0 = U32(oldPos, 0);
    unsigned int t2 = U16(oldPos, 4);
    unsigned int t4 = U32(position, 0);
    unsigned int t6 = U16(position, 4);
    unsigned int t1 = v0 >> 16;
    unsigned int t0 = v0 & 0xFFFF;
    unsigned int t5 = t4 >> 16;
    int mac1;
    int mac2;
    int t7;

    t4 &= 0xFFFF;
    t4 -= t0;
    t5 -= t1;
    t6 -= t2;

    MTC2(U32(normal, 0), 0);
    MTC2(U32(normal, 4), 1);
    CTC2(v0, 0);
    CTC2(t2 | (t4 << 16), 1);
    CTC2((t5 & 0xFFFF) | (t6 << 16), 2);
    GTE_RTV0();

    mac1 = MAC(1);
    mac2 = MAC(2);
    t7 = (mac1 - (int)z) << 12;

    if (mac2 == 0)
    {
        return 0;
    }

    t7 = mips_div(-t7, mac2);

    if ((unsigned int)t7 >= 0x1001)
    {
        return 0;
    }

    MTC2(t4, 9);
    MTC2(t5, 10);
    MTC2(t6, 11);
    MTC2(t7, 8);
    GTE_GPF1();

    S16(planePoint, 0) = (short)(t0 + IR(1));
    S16(planePoint, 2) = (short)(t1 + IR(2));
    S16(planePoint, 4) = (short)(t2 + IR(3));

    return 1;
}

int COLLIDE_NearestPointOnLine_S(SVector *linePoint, SVECTOR *oldPoint, SVECTOR *newPoint, Position *position)
{
    unsigned int t0 = U32(oldPoint, 0);
    unsigned int t1 = U16(oldPoint, 4);
    unsigned int t2 = U32(newPoint, 0);
    unsigned int t3 = U16(newPoint, 4);
    unsigned int t8 = U32(position, 0);
    unsigned int t9 = U16(position, 4);
    unsigned int t4 = t0 >> 16;
    unsigned int t5 = t0 & 0xFFFF;
    unsigned int t6 = t2 >> 16;
    unsigned int t7 = t2 & 0xFFFF;
    unsigned int v0;
    int a1;
    int a3;

    CTC2(t0, 0);
    CTC2((t8 << 16) | t1, 1);
    CTC2((t9 << 16) | (t8 >> 16), 2);

    t7 = t7 - t5;
    t8 = t6 - t4;
    t9 = t3 - t1;
    v0 = (t8 << 16) | (t7 & 0xFFFF);

    CTC2(v0, 3);
    CTC2(t9, 4);
    MTC2(v0, 0);
    MTC2(t9, 1);
    GTE_RTV0();

    a1 = (MAC(1) - MAC(2)) << 12;
    a3 = MAC(3);

    if (a3 != 0)
    {
        v0 = (unsigned int)mips_div(-a1, a3);

        MTC2(t7, 9);
        MTC2(t8, 10);
        MTC2(t9, 11);
    }

    if (v0 >= 0x1001)
    {
        if ((int)v0 > 0)
        {
            U32(linePoint, 0) = t2;
            U16(linePoint, 4) = (unsigned short)t3;
        }
        else
        {
            U32(linePoint, 0) = t0;
            U16(linePoint, 4) = (unsigned short)t1;
        }

        return (int)v0;
    }

    MTC2(v0, 8);
    GTE_GPF1();

    S16(linePoint, 0) = (short)(t5 + IR(1));
    S16(linePoint, 2) = (short)(t4 + IR(2));
    S16(linePoint, 4) = (short)(t1 + IR(3));

    return (int)v0;
}

VECTOR *ApplyMatrix_S(MATRIX *m, SVECTOR *v, VECTOR *r)
{
    CTC2(U32(m, 0), 0);
    CTC2(U32(m, 4), 1);
    CTC2(U32(m, 8), 2);
    CTC2(U32(m, 12), 3);
    CTC2(U16(m, 16), 4);
    MTC2(U32(v, 0), 0);
    MTC2(U32(v, 4), 1);
    GTE_RTV0();

    r->vx = MAC(1);
    r->vy = MAC(2);
    r->vz = MAC(3);

    return r;
}
