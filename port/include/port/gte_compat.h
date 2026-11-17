/*
 * GTE macros that soul-re uses but PsyCross does not provide.
 *
 * Each one mirrors the register-level behaviour of the MIPS sequence in
 * soul-re's Libs/gte_macros.h / Libs/GTE.h, expressed with the PsyCross
 * software GTE primitives (MTC2/CTC2/MFC2/doCOP2).
 */
#ifndef REWRAITHED_GTE_COMPAT_H
#define REWRAITHED_GTE_COMPAT_H

#define PORT_U32(p, off) (*(unsigned int *)((char *)(p) + (off)))
#define PORT_U16(p, off) (*(unsigned short *)((char *)(p) + (off)))
#define PORT_S16(p, off) (*(short *)((char *)(p) + (off)))
#define PORT_U8(p, off)  (*(unsigned char *)((char *)(p) + (off)))

/* --- PsyCross macros whose operands differ from PsyQ ----------------------
 * PsyQ (and soul-re's gte_macros.h) load these from register values, or
 * with zero-extending byte loads (lbu); PsyCross dereferences the operand or
 * sign-extends through a plain char. */

#undef gte_lddp
#define gte_lddp(r0) MTC2((unsigned int)(r0), 8)

#undef gte_ldsxy3
#define gte_ldsxy3(r0, r1, r2)               \
    {                                        \
        MTC2((unsigned int)(r0), 12);        \
        MTC2((unsigned int)(r2), 14);        \
        MTC2((unsigned int)(r1), 13);        \
    }

#undef gte_ldbv
#define gte_ldbv(r0)                         \
    {                                        \
        MTC2(PORT_U8(r0, 0), 9);             \
        MTC2(PORT_U8(r0, 1), 10);            \
    }

/* PsyQ: lhu $13,4; lhu $12,0; sll; or; mtc2 $12,$0; lwc2 $1,8. PsyCross
 * took the CPU register $12 for the GTE register and wrote SXY0, so V0
 * kept a stale vector (shadows landed at random places, for one). */
#undef gte_ldlv0
#define gte_ldlv0(r0)                                                          \
    {                                                                          \
        MTC2(PORT_U16(r0, 0) | ((unsigned int)PORT_U16(r0, 4) << 16), 0);     \
        MTC2(PORT_U32(r0, 8), 1);                                              \
    }

#undef gte_ldcv
#define gte_ldcv(r0)                         \
    {                                        \
        MTC2(PORT_U8(r0, 0), 9);             \
        MTC2(PORT_U8(r0, 1), 10);            \
        MTC2(PORT_U8(r0, 2), 11);            \
    }

/* --- loads ------------------------------------------------------------- */

/* lwc2 $6 */
#define gte_SetRGBcd(r0) MTC2(PORT_U32(r0, 0), 6)

/* ctc2 $27 / $28 */
#define gte_lddqa(r0) CTC2((unsigned int)(r0), 27)
#define gte_lddqb(r0) CTC2((unsigned int)(r0), 28)

/* lhu x3 -> mtc2 $25..$27 */
#define gte_ldlvnlsv(r0)                 \
    {                                    \
        MTC2(PORT_U16(r0, 0), 25);       \
        MTC2(PORT_U16(r0, 2), 26);       \
        MTC2(PORT_U16(r0, 4), 27);       \
    }

/* lw x2 -> ctc2 $0, $1 (first rotation matrix row) */
#define gte_ldsvrtrow0(r0)               \
    {                                    \
        CTC2(PORT_U32(r0, 0), 0);        \
        CTC2(PORT_U32(r0, 4), 1);        \
    }

/* lhu x3; or $12,$13 -> ctc2 $0, $1 (literal port of the MIPS sequence) */
#define gte_ldv0sv(r0)                                     \
    {                                                      \
        CTC2(PORT_U16(r0, 0) | PORT_U16(r0, 2), 0);        \
        CTC2(PORT_U16(r0, 4), 1);                          \
    }

/* lw x2 -> ctc2 $3, $4 */
#define gte_ldv2_ext(r0)                 \
    {                                    \
        CTC2(PORT_U32(r0, 0), 3);        \
        CTC2(PORT_U32(r0, 4), 4);        \
    }

/* lwc2 $0..$5 from 6 consecutive words */
#define gte_ldv3c(r0)                    \
    {                                    \
        MTC2(PORT_U32(r0, 0), 0);        \
        MTC2(PORT_U32(r0, 4), 1);        \
        MTC2(PORT_U32(r0, 8), 2);        \
        MTC2(PORT_U32(r0, 12), 3);       \
        MTC2(PORT_U32(r0, 16), 4);       \
        MTC2(PORT_U32(r0, 20), 5);       \
    }

/* --- stores ------------------------------------------------------------ */

/* mfc2 $25..$27 -> sh x3 */
#define gte_stlvnlsv(r0)                         \
    {                                            \
        PORT_S16(r0, 0) = (short)MFC2(25);       \
        PORT_S16(r0, 2) = (short)MFC2(26);       \
        PORT_S16(r0, 4) = (short)MFC2(27);       \
    }

/* swc2 $25 */
#define gte_stlvnl0(r0) (PORT_U32(r0, 0) = MFC2(25))

/* swc2 $12 / $13 to two addresses */
#define gte_stsxy01(r0, r1)                      \
    {                                            \
        PORT_U32(r0, 0) = MFC2(12);              \
        PORT_U32(r1, 0) = MFC2(13);              \
    }

/* swc2 vertex register pairs */
#define gte_stv0(r0) { PORT_U32(r0, 0) = MFC2(0); PORT_U32(r0, 4) = MFC2(1); }
#define gte_stv1(r0) { PORT_U32(r0, 0) = MFC2(2); PORT_U32(r0, 4) = MFC2(3); }
#define gte_stv2(r0) { PORT_U32(r0, 0) = MFC2(4); PORT_U32(r0, 4) = MFC2(5); }

/* --- commands ------------------------------------------------------------
 * The "n" variants only add pipeline nops on hardware; the software GTE
 * finishes each command immediately. */

#define gte_nrtps()   gte_rtps()
#define gte_nrtpt()   gte_rtpt()
#define gte_navsz3()  gte_avsz3()
#define gte_ndpcs()   gte_dpcs()
#define gte_nnccs()   gte_nccs()
#define gte_nncds()   gte_ncds()

#define gte_ngpf(sf)  doCOP2(0x0190003D | ((sf) << 19))
#define gte_ngpl(sf)  doCOP2(0x01A0003E | ((sf) << 19))
#define gte_ngpf12()  gte_ngpf(1)
#define gte_ngpl12()  gte_ngpl(1)

#define gte_nsqr(sf)  doCOP2(0x00A00428 | ((sf) << 19))
#define gte_nsqr0()   gte_nsqr(0)

#define gte_nmvmva(sf, mx, v, cv, lm) gte_mvmva(sf, mx, v, cv, lm)
#define gte_nrtvx()   gte_mvmva(1, 0, 0, 0, 0)
#define gte_nrtv0()   gte_mvmva(1, 0, 0, 3, 0)
#define gte_nrtv0tr() gte_mvmva(1, 0, 0, 0, 0)
#define gte_nrtir12() gte_mvmva(1, 0, 3, 3, 0)

#endif
