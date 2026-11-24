/*
 * The maps of the prototypes up to May 1999 in the July build's level format
 * (proto_convert.h).
 *
 * A map is a relocatable memory image (.drm): a table of the body offsets
 * that hold pointers, then the body, a Level at offset 0. Where a prototype's
 * structs differ from the July build's (as their own symbol files describe
 * them), each struct or array of structs is rewritten in place field by field
 * (by copy segments: old offset, new offset, length) and every pointer to or
 * inside it follows:
 *
 *   Level        grew by up to 0x18 bytes (fog distances per realm, the VRAM
 *                rectangle, SFX markers, Raziel's light groups);
 *   Terrain      lost the SBSP tree (three pointers) for a padding word;
 *   TextureFT3   lost its colour word (16 -> 12 bytes, up to February):
 *                the faces' texture offsets are scaled;
 *   Intro        lost a link pointer (0x50 -> 0x4C bytes, up to Feb 4);
 *   VM objects   (January) had another layout and are left out;
 *   signals      were numbered 0 to 110 (the July build has 27 of them,
 *                renumbered): the others are left out of each signal list.
 *
 * Some January maps were made with a Level shorter than that build's own;
 * the Level ends where the first data after it begins.
 */
#include <stdlib.h>
#include <string.h>

#include "proto_convert.h"

typedef struct { short from, to, len; } Seg;
typedef struct { long size; int count; Seg segs[6]; } Layout;

#define LEVEL_SIZE      0x188
#define TERRAIN_SIZE    0x54
#define TEXTURE_SIZE    0xC
#define INTRO_SIZE      0x4C
#define FACE_SIZE       12

static const Layout level_jan = { 0x170, 5, { { 0x0, 0x0, 0x48 }, { 0x48, 0x50, 0x4 }, { 0x50, 0x58, 0x44 }, { 0x9C, 0xA4, 0x44 }, { 0xE0, 0xF0, 0x90 } } };
static const Layout level_feb16 = { 0x174, 5, { { 0x0, 0x0, 0x4C }, { 0x4C, 0x50, 0x4 }, { 0x54, 0x58, 0x44 }, { 0xA0, 0xA4, 0x44 }, { 0xE4, 0xF0, 0x90 } } };
static const Layout level_apr = { 0x178, 2, { { 0x0, 0x0, 0xE8 }, { 0xE8, 0xF0, 0x90 } } };
static const Layout terrain_jan = { 0x5C, 3, { { 0x8, 0x8, 0x24 }, { 0x30, 0x30, 0xC }, { 0x44, 0x3C, 0x18 } } };
static const Layout terrain_apr = { 0x5C, 3, { { 0x0, 0x0, 0x2C }, { 0x30, 0x30, 0xC }, { 0x44, 0x3C, 0x18 } } };
static const Layout texture_early = { 0x10, 1, { { 0x0, 0x0, 0xC } } };
static const Layout intro_early = { 0x50, 2, { { 0x0, 0x0, 0x18 }, { 0x1C, 0x18, 0x34 } } };

/* each signal's data length in words, by its number */
#define OLD_SIGNALS 111
static const signed char signal_lengths_jan[OLD_SIGNALS] = {
    1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 4, 1, 2, 1, 1, 1, 2, 2, 1, 1, 1, 0, 0, 0,
    0, 1, 1, 0, 0, 1, 1, 1, 1, 1, 2, 2, 1, 1, 1, 2, 2, 1, 2, 1, 1, 1, 0, 0, 1, 0, 0, 2, 2, 1, 2, 3, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 6, 3, 2, 1, 1, 1, 1, 0, 1, 2, 1, 2, 2, 1, 1, 1, 2, 0, 0, 2, 1, 4, 2, 2, 3, 0, 1, 1, 1, 0, 1, 1, 1,
};
static const signed char signal_lengths_apr[OLD_SIGNALS] = {
    1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 4, 1, 2, 1, 1, 1, 2, 2, 1, 1, 1, 0, 0, 0,
    0, 1, 1, 0, 0, 1, 1, 1, 1, 1, 2, 2, 1, 1, 1, 2, 2, 1, 2, 1, 1, 1, 0, 0, 1, 0, 0, 2, 2, 1, 1, 1, 0, 0, 1, 1, 1,
    1, 1, 1, 1, 6, 3, 2, 1, 1, 1, 1, 0, 1, 2, 1, 2, 2, 1, 1, 1, 2, 0, 0, 2, 1, 4, 2, 2, 3, 0, 1, 1, 1, 0, 1, 1, 1,
};

/* old number -> July number (-1: the July build has no such signal) */
static signed char signal_to_july[OLD_SIGNALS];

static void init_signal_map(void)
{
    static const signed char pairs[][2] = {
        { 12, 0 }, { 13, 1 }, { 14, 2 }, { 15, 3 }, { 16, 4 }, { 17, 5 }, { 18, 6 }, { 19, 7 }, { 20, 8 },
        { 21, 9 }, { 22, 10 }, { 43, 11 }, { 44, 12 }, { 48, 13 }, { 54, 14 }, { 60, 15 }, { 62, 16 },
        { 63, 17 }, { 78, 18 }, { 80, 19 }, { 81, 20 }, { 93, 21 }, { 98, 22 }, { 106, 23 }, { 107, 24 },
        { 108, 25 }, { 109, 26 },
    };
    int i;

    memset(signal_to_july, -1, sizeof(signal_to_july));

    for (i = 0; i < (int)(sizeof(pairs) / sizeof(pairs[0])); i++)
    {
        signal_to_july[(int)pairs[i][0]] = pairs[i][1];
    }
}

typedef struct
{
    const char *date;
    const Layout *level;        /* NULL: as the July one */
    const Layout *terrain;
    const Layout *texture;      /* NULL: as the July one */
    const Layout *intro;        /* NULL: as the July one */
    long numIntrosAt;           /* Level.numIntros in the old layout */
    int dropVM;
    int early;                  /* no realm fogs or VRAM rectangle in the Level */
    const signed char *signalLengths;
    int objectsLoad;
} Build;

static const Build builds[] = {
    { "1999-01-23", &level_jan, &terrain_jan, &texture_early, &intro_early, 0x70, 1, 1, signal_lengths_jan, 0 },
    { "1999-02-04", &level_jan, &terrain_jan, &texture_early, &intro_early, 0x70, 0, 1, signal_lengths_jan, 0 },
    { "1999-02-16", &level_feb16, &terrain_jan, &texture_early, NULL, 0x74, 0, 1, signal_lengths_jan, 0 },
    { "1999-04-14", &level_apr, &terrain_apr, NULL, NULL, 0x78, 0, 0, signal_lengths_apr, 1 },
    { "1999-05-12", NULL, &terrain_apr, NULL, NULL, 0x78, 0, 0, signal_lengths_apr, 1 },
};

static const Build *find_build(const char *date)
{
    int i;

    for (i = 0; i < (int)(sizeof(builds) / sizeof(builds[0])); i++)
    {
        if (strcmp(builds[i].date, date) == 0)
        {
            return &builds[i];
        }
    }

    return NULL;
}

int proto_convert_known(const char *date)
{
    return find_build(date) != NULL;
}

int proto_convert_objects_load(const char *date)
{
    const Build *b = find_build(date);

    return (b == NULL) || b->objectsLoad;
}

/* --- the memory image --- */

typedef struct
{
    unsigned char *body;
    long size;
    long *relocs;       /* sorted body offsets of the pointers */
    long count;
    int bad;            /* something didn't fit: the map can't be converted */
} Drm;

static long le32(const unsigned char *p)
{
    return (long)((unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24));
}

static long g32(Drm *d, long off)
{
    if ((off < 0) || (off + 4 > d->size))
    {
        d->bad = 1;
        return 0;
    }

    return le32(d->body + off);
}

static void p32(Drm *d, long off, long v)
{
    if ((off < 0) || (off + 4 > d->size))
    {
        d->bad = 1;
        return;
    }

    d->body[off] = (unsigned char)v;
    d->body[off + 1] = (unsigned char)(v >> 8);
    d->body[off + 2] = (unsigned char)(v >> 16);
    d->body[off + 3] = (unsigned char)(v >> 24);
}

static unsigned g16(Drm *d, long off)
{
    if ((off < 0) || (off + 2 > d->size))
    {
        d->bad = 1;
        return 0;
    }

    return (unsigned)(d->body[off] | (d->body[off + 1] << 8));
}

static void p16(Drm *d, long off, unsigned v)
{
    if ((off < 0) || (off + 2 > d->size))
    {
        d->bad = 1;
        return;
    }

    d->body[off] = (unsigned char)v;
    d->body[off + 1] = (unsigned char)(v >> 8);
}

static int compare_longs(const void *a, const void *b)
{
    long x = *(const long *)a;
    long y = *(const long *)b;

    return (x < y) ? -1 : (x > y);
}

static void sort_relocs(Drm *d)
{
    qsort(d->relocs, d->count, sizeof(long), compare_longs);
}

static int is_reloc(Drm *d, long off)
{
    return bsearch(&off, d->relocs, d->count, sizeof(long), compare_longs) != NULL;
}

static int drm_load(Drm *d, const unsigned char *data, long size)
{
    long num;
    long header;
    long i;

    memset(d, 0, sizeof(*d));

    if (size < 4)
    {
        return 0;
    }

    num = le32(data);
    header = ((num + 512) / 512) * 512 * 4;

    if ((num <= 0) || (header > size) || (4 + num * 4 > size))
    {
        return 0;
    }

    d->size = size - header;
    d->body = (unsigned char *)malloc(d->size + 4);
    d->relocs = (long *)malloc(num * sizeof(long));

    if ((d->body == NULL) || (d->relocs == NULL))
    {
        return 0;
    }

    memcpy(d->body, data + header, d->size);

    for (i = 0; i < num; i++)
    {
        long r = le32(data + 4 + i * 4);

        if ((r < 0) || (r + 4 > d->size) || (r & 3))
        {
            return 0;
        }

        d->relocs[i] = r;
    }

    d->count = num;
    sort_relocs(d);

    return 1;
}

static void drm_free(Drm *d)
{
    free(d->body);
    free(d->relocs);
}

static unsigned char *drm_save(Drm *d, long *outSize)
{
    long words = ((d->count + 512) / 512) * 512;
    long total = words * 4 + d->size;
    unsigned char *out = (unsigned char *)calloc(1, total);
    long i;

    if (out == NULL)
    {
        return NULL;
    }

    out[0] = (unsigned char)d->count;
    out[1] = (unsigned char)(d->count >> 8);
    out[2] = (unsigned char)(d->count >> 16);
    out[3] = (unsigned char)(d->count >> 24);

    for (i = 0; i < d->count; i++)
    {
        unsigned char *p = out + 4 + i * 4;

        p[0] = (unsigned char)d->relocs[i];
        p[1] = (unsigned char)(d->relocs[i] >> 8);
        p[2] = (unsigned char)(d->relocs[i] >> 16);
        p[3] = (unsigned char)(d->relocs[i] >> 24);
    }

    memcpy(out + words * 4, d->body, d->size);
    *outSize = total;

    return out;
}

/* `n` zero bytes at `at`; pointers to `at` or after follow */
static void insert(Drm *d, long at, long n)
{
    unsigned char *grown;
    long i;

    if ((at < 0) || (at > d->size) || ((grown = (unsigned char *)realloc(d->body, d->size + n + 4)) == NULL))
    {
        d->bad = 1;
        return;
    }

    d->body = grown;

    for (i = 0; i < d->count; i++)
    {
        long v = g32(d, d->relocs[i]);

        if (v >= at)
        {
            p32(d, d->relocs[i], v + n);
        }
    }

    memmove(d->body + at + n, d->body + at, d->size - at);
    memset(d->body + at, 0, n);
    d->size += n;

    for (i = 0; i < d->count; i++)
    {
        if (d->relocs[i] >= at)
        {
            d->relocs[i] += n;
        }
    }
}

static void unptr(Drm *d, long at)
{
    long i;

    p32(d, at, 0);

    for (i = 0; i < d->count; i++)
    {
        if (d->relocs[i] == at)
        {
            d->relocs[i] = d->relocs[--d->count];
            sort_relocs(d);
            return;
        }
    }
}

/* where an old offset inside the converted block went; a dropped field's goes
 * to its struct's start, and *dropped says so */
typedef struct
{
    long base, osz, nsz;
    const Layout *layout;
} Mapper;

static long map_offset(const Mapper *m, long v, int *dropped)
{
    long i = (v - m->base) / m->osz;
    long inner = (v - m->base) - i * m->osz;
    int s;

    for (s = 0; s < m->layout->count; s++)
    {
        const Seg *g = &m->layout->segs[s];

        if ((inner >= g->from) && (inner < g->from + g->len))
        {
            *dropped = 0;
            return m->base + i * m->nsz + g->to + (inner - g->from);
        }
    }

    *dropped = 1;
    return m->base + i * m->nsz;
}

/* `count` structs at `base` from `layout` to `nsz` bytes each */
static void remap(Drm *d, long base, long count, const Layout *layout, long nsz)
{
    Mapper m;
    long osz = layout->size;
    long end;
    long span;
    unsigned char *old;
    unsigned char *fresh;
    long i, k;
    int s, dropped;

    if ((count <= 0) || d->bad)
    {
        return;
    }

    if (nsz > osz)
    {
        insert(d, base + count * osz, count * (nsz - osz));
    }

    end = base + count * osz;
    span = (count * osz > count * nsz) ? count * osz : count * nsz;

    if ((base < 0) || (base + span > d->size) || d->bad)
    {
        d->bad = 1;
        return;
    }

    m.base = base;
    m.osz = osz;
    m.nsz = nsz;
    m.layout = layout;

    old = (unsigned char *)malloc(count * osz);
    fresh = (unsigned char *)calloc(1, span);

    if ((old == NULL) || (fresh == NULL))
    {
        free(old);
        free(fresh);
        d->bad = 1;
        return;
    }

    memcpy(old, d->body + base, count * osz);

    for (i = 0; i < count; i++)
    {
        for (s = 0; s < layout->count; s++)
        {
            const Seg *g = &layout->segs[s];

            memcpy(fresh + i * nsz + g->to, old + i * osz + g->from, g->len);
        }
    }

    /* pointers inside move with their field, or go with a dropped one */
    for (i = 0, k = 0; i < d->count; i++)
    {
        long r = d->relocs[i];

        if ((r >= base) && (r < end))
        {
            r = map_offset(&m, r, &dropped);

            if (dropped)
            {
                continue;
            }
        }

        d->relocs[k++] = r;
    }

    d->count = k;
    memcpy(d->body + base, fresh, span);

    /* pointers into the block */
    for (i = 0; i < d->count; i++)
    {
        long v = g32(d, d->relocs[i]);

        if ((v >= base) && (v < end))
        {
            p32(d, d->relocs[i], map_offset(&m, v, &dropped));
        }
    }

    sort_relocs(d);
    free(old);
    free(fresh);
}

/* --- signals --- */

typedef struct
{
    long old;
    long length;
    long fresh;         /* -1: left out */
} Piece;

static long map_signal(const Piece *pieces, long count, long v, long newEnd, int *dropped)
{
    long i;

    *dropped = 0;

    for (i = 0; i < count; i++)
    {
        if ((v >= pieces[i].old) && (v < pieces[i].old + pieces[i].length))
        {
            long j;

            if (pieces[i].fresh >= 0)
            {
                return pieces[i].fresh + (v - pieces[i].old);
            }

            *dropped = 1;

            for (j = i + 1; j < count; j++)
            {
                if (pieces[j].fresh >= 0)
                {
                    return pieces[j].fresh;
                }
            }

            return newEnd;
        }
    }

    return v;
}

#define LEVEL_SIGNAL_START  0xD0
#define LEVEL_SIGNAL_END    0xD4
#define TERRAIN_SIGNALS     0x50

/* Each MultiSignal: long numSignals; short signalNum, flags; the signals
 * (a word with the number, then its data); an end signal. */
static void convert_signals(Drm *d, const signed char *lengths)
{
    long start, end, m, outLen = 0, newEnd;
    long pieceCount = 0, pieceCap;
    Piece *pieces;
    unsigned char *out;
    long t, tsig = -1;
    long i, k;
    int dropped;

    if (!is_reloc(d, LEVEL_SIGNAL_START) || !is_reloc(d, LEVEL_SIGNAL_END))
    {
        return;
    }

    start = g32(d, LEVEL_SIGNAL_START);
    end = g32(d, LEVEL_SIGNAL_END);

    if ((end <= start) || (end > d->size))
    {
        return;
    }

    pieceCap = (end - start) / 4 + 4;
    pieces = (Piece *)malloc(pieceCap * sizeof(Piece));
    out = (unsigned char *)calloc(1, end - start);

    if ((pieces == NULL) || (out == NULL))
    {
        free(pieces);
        free(out);
        d->bad = 1;
        return;
    }

#define PIECE(o, l, f) do { if (pieceCount < pieceCap) { pieces[pieceCount].old = (o); pieces[pieceCount].length = (l); pieces[pieceCount].fresh = (f); pieceCount++; } } while (0)

    for (m = start; (m < end) && !d->bad;)
    {
        long n = g32(d, m);
        long header = outLen;
        long s = m + 8;
        long kept = 0;

        if ((n < 0) || (n > 1024) || (m + 8 > end))
        {
            d->bad = 1;
            break;
        }

        memcpy(out + outLen, d->body + m, 8);
        PIECE(m, 8, start + outLen);
        outLen += 8;

        for (i = 0; i < n; i++)
        {
            long raw = g32(d, s);
            long id = raw & 0x7FFFFFFF;
            long size;

            if ((id >= OLD_SIGNALS) || (s + (size = (lengths[id] + 1) * 4) > end))
            {
                d->bad = 1;
                break;
            }

            if (signal_to_july[id] >= 0)
            {
                long fresh = (raw & (long)0x80000000) | signal_to_july[id];

                PIECE(s, size, start + outLen);
                out[outLen] = (unsigned char)fresh;
                out[outLen + 1] = (unsigned char)(fresh >> 8);
                out[outLen + 2] = (unsigned char)(fresh >> 16);
                out[outLen + 3] = (unsigned char)(fresh >> 24);
                memcpy(out + outLen + 4, d->body + s + 4, size - 4);
                outLen += size;
                kept++;
            }
            else
            {
                PIECE(s, size, -1);
            }

            s += size;
        }

        if (d->bad || (s + 4 > end))
        {
            d->bad = 1;
            break;
        }

        out[header] = (unsigned char)kept;
        out[header + 1] = (unsigned char)(kept >> 8);
        out[header + 2] = 0;
        out[header + 3] = 0;

        PIECE(s, 4, start + outLen);
        out[outLen] = 15;
        outLen += 4;
        m = s + 4;
    }

#undef PIECE

    if (d->bad || (m != end))
    {
        d->bad = 1;
        free(pieces);
        free(out);
        return;
    }

    newEnd = start + outLen;

    /* face signal offsets are relative to Terrain.signals */
    t = g32(d, 0);

    if (is_reloc(d, t + TERRAIN_SIGNALS))
    {
        tsig = g32(d, t + TERRAIN_SIGNALS);
    }

    if ((tsig >= start) && (tsig < end))
    {
        long numFaces = g32(d, t + 0x14);
        long faces = g32(d, t + 0x20);
        long base = map_signal(pieces, pieceCount, tsig, newEnd, &dropped);

        for (i = 0; (i < numFaces) && !d->bad; i++)
        {
            long o = faces + i * FACE_SIZE;
            unsigned to = g16(d, o + 10);

            if ((to != 0xFFFF) && (o + 6 < d->size) && (d->body[o + 6] & 0xC0))
            {
                p16(d, o + 10, (unsigned)(map_signal(pieces, pieceCount, tsig + to, newEnd, &dropped) - base));
            }
        }
    }

    for (i = 0, k = 0; i < d->count; i++)
    {
        long r = d->relocs[i];

        if ((r >= start) && (r < end))
        {
            r = map_signal(pieces, pieceCount, r, newEnd, &dropped);

            if (dropped)
            {
                continue;
            }
        }

        d->relocs[k++] = r;
    }

    d->count = k;
    memcpy(d->body + start, out, end - start);

    for (i = 0; i < d->count; i++)
    {
        long v = g32(d, d->relocs[i]);

        if ((v >= start) && (v < end))
        {
            p32(d, d->relocs[i], map_signal(pieces, pieceCount, v, newEnd, &dropped));
        }
    }

    sort_relocs(d);
    p32(d, LEVEL_SIGNAL_END, newEnd);

    free(pieces);
    free(out);
}

/* --- objects the July build lacks (when the prototype's don't load) --- */

#define LEVEL_NUM_INTROS    0x78
#define LEVEL_INTROS        0x7C
#define LEVEL_OBJECT_NAMES  0x8C
#define INTRO_FLAGS         0x2C
#define INTRO_NOT_INTRODUCED 0x4000

static void leave_out_objects(Drm *d, ProtoHasObject hasObject, void *ctx)
{
    long numIntros = g32(d, LEVEL_NUM_INTROS);
    long intros = g32(d, LEVEL_INTROS);
    long i;

    if (is_reloc(d, LEVEL_INTROS))
    {
        for (i = 0; (i < numIntros) && !d->bad; i++)
        {
            long o = intros + i * INTRO_SIZE;
            char name[17];

            if (o + INTRO_SIZE > d->size)
            {
                d->bad = 1;
                break;
            }

            memcpy(name, d->body + o, 16);
            name[16] = '\0';

            if ((name[0] != '\0') && !hasObject(ctx, name))
            {
                p32(d, o + INTRO_FLAGS, g32(d, o + INTRO_FLAGS) | INTRO_NOT_INTRODUCED);
            }
        }
    }

    if (is_reloc(d, LEVEL_OBJECT_NAMES))
    {
        long list = g32(d, LEVEL_OBJECT_NAMES);
        long w = list;

        for (i = list; (i + 16 <= d->size) && (d->body[i] != 0xFF); i += 16)
        {
            char name[17];

            memcpy(name, d->body + i, 16);
            name[16] = '\0';

            if (hasObject(ctx, name))
            {
                memmove(d->body + w, d->body + i, 16);
                w += 16;
            }
        }

        if (w < i)
        {
            memset(d->body + w, 0, i - w);
            d->body[w] = 0xFF;
        }
    }
}

unsigned char *proto_convert_level(const char *date, const unsigned char *drm, long size,
    const unsigned char *crm, long crmSize, ProtoHasObject hasObject, void *ctx, long *outSize)
{
    const Build *b = find_build(date);
    unsigned char *result = NULL;
    Drm d;
    long t;

    if ((b == NULL) || !drm_load(&d, drm, size) || !is_reloc(&d, 0))
    {
        if (b != NULL)
        {
            drm_free(&d);
        }

        return NULL;
    }

    init_signal_map();
    t = g32(&d, 0);

    /* texture lists: the faces' offsets into them first */
    if (b->texture != NULL)
    {
        long start = g32(&d, t + 0x34);
        long end = g32(&d, t + 0x38);
        long numFaces = g32(&d, t + 0x14);
        long faces = g32(&d, t + 0x20);
        long osz = b->texture->size;
        long i;

        if (is_reloc(&d, t + 0x34) && (end >= start))
        {
            long n = (end - start) / osz;

            for (i = 0; (i < numFaces) && !d.bad; i++)
            {
                long o = faces + i * FACE_SIZE;
                unsigned to = g16(&d, o + 10);

                if ((to != 0xFFFF) && (o + 6 < d.size) && !(d.body[o + 6] & 0xC0))
                {
                    p16(&d, o + 10, (unsigned)(to / osz * TEXTURE_SIZE));
                }
            }

            remap(&d, start, n, b->texture, TEXTURE_SIZE);
            p32(&d, t + 0x38, start + n * TEXTURE_SIZE);
        }
    }

    /* intro lists (the Level's and the Terrain's are the same one or two) */
    if (b->intro != NULL)
    {
        long lists[2][2];
        int n = 0;
        int i;

        if (is_reloc(&d, b->numIntrosAt + 4))
        {
            lists[n][0] = g32(&d, b->numIntrosAt + 4);
            lists[n][1] = g32(&d, b->numIntrosAt);
            n++;
        }

        if (is_reloc(&d, t + 0xC) && ((n == 0) || (g32(&d, t + 0xC) != lists[0][0])))
        {
            lists[n][0] = g32(&d, t + 0xC);
            lists[n][1] = g32(&d, t + 0x8);
            n++;
        }

        /* the later one first, so that the first stays where it is */
        if ((n == 2) && (lists[1][0] > lists[0][0]))
        {
            long a = lists[0][0], c = lists[0][1];

            lists[0][0] = lists[1][0];
            lists[0][1] = lists[1][1];
            lists[1][0] = a;
            lists[1][1] = c;
        }

        for (i = 0; i < n; i++)
        {
            if ((lists[i][1] > 0) && (lists[i][1] < 10000))
            {
                remap(&d, lists[i][0], lists[i][1], b->intro, INTRO_SIZE);
            }
        }
    }

    if (b->dropVM)
    {
        p32(&d, 0x8, 0);
        unptr(&d, 0xC);
    }

    t = g32(&d, 0);
    remap(&d, t, 1, b->terrain, TERRAIN_SIZE);

    if (b->level != NULL)
    {
        /* the Level ends where the first data after it begins */
        Layout level = *b->level;
        long first = level.size;
        long i;
        int s, k;

        for (i = 0; i < d.count; i++)
        {
            long v = g32(&d, d.relocs[i]);

            if ((v > 0) && (v < first))
            {
                first = v;
            }
        }

        first &= ~3;

        if (first < level.size)
        {
            for (s = 0, k = 0; s < level.count; s++)
            {
                Seg g = level.segs[s];

                if (g.from < first)
                {
                    if (g.from + g.len > first)
                    {
                        g.len = (short)(first - g.from);
                    }

                    level.segs[k++] = g;
                }
            }

            level.count = k;
            level.size = first;
        }

        remap(&d, 0, 1, &level, LEVEL_SIZE);
    }

    convert_signals(&d, b->signalLengths);

    if (b->early && !d.bad)
    {
        unsigned fogFar = g16(&d, 0x44);
        unsigned fogNear = g16(&d, 0x46);

        p16(&d, 0x48, fogFar);
        p16(&d, 0x4A, fogNear);
        p16(&d, 0x4C, fogFar);
        p16(&d, 0x4E, fogNear);

        /* the VRAM rectangle of the level's textures, from its .crm */
        if ((crm != NULL) && (crmSize >= 36))
        {
            p16(&d, 0x9C, (unsigned)le32(crm + 20));
            p16(&d, 0x9E, (unsigned)le32(crm + 24));
            p16(&d, 0xA0, (unsigned)le32(crm + 28));
            p16(&d, 0xA2, (unsigned)le32(crm + 32));
        }
        else
        {
            p16(&d, 0xA0, 512);
            p16(&d, 0xA2, 256);
        }
    }

    if (!b->objectsLoad && (hasObject != NULL) && !d.bad)
    {
        leave_out_objects(&d, hasObject, ctx);
    }

    if (!d.bad)
    {
        result = drm_save(&d, outSize);
    }

    drm_free(&d);

    return result;
}
