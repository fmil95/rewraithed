/*
 * libpress: STR movie frame decoding, for the cinemax overlay.
 *
 * On the PSX, DecDCTvlc turns a frame's compressed bitstream into run-length
 * codes on the CPU, DecDCTin hands those to the MDEC chip, and DecDCTout
 * fetches the decoded image by DMA in pieces, calling the DecDCToutCallback
 * when each piece is done. The movie player asks for one 16-pixel-wide
 * column of macroblocks per DecDCTout and uploads it from the callback.
 *
 * Here DecDCTin decodes the whole frame at once (dequantization, IDCT,
 * YCbCr to 24-bit RGB, as the MDEC does), and DecDCTout copies out the next
 * piece and runs the callback chain.
 *
 * The bitstream is STR version 2 (what the disc's movies use): per block a
 * 10-bit DC value, then MPEG-1 style AC codes with a 6+10-bit escape.
 */
#include <math.h>

#include "common.h"

/* --- run-length codes (DecDCTvlc) ------------------------------------------ */

typedef struct
{
    const char *bits;   /* code, without the sign bit */
    unsigned char run;
    unsigned char level;
} VlcCode;

/* AC coefficient codes (MPEG-1 table B.14); each is followed by a sign bit */
static const VlcCode vlc_codes[] = {
    { "11", 0, 1 },
    { "011", 1, 1 },
    { "0100", 0, 2 }, { "0101", 2, 1 },
    { "00101", 0, 3 }, { "00111", 3, 1 }, { "00110", 4, 1 },
    { "000110", 1, 2 }, { "000111", 5, 1 }, { "000101", 6, 1 }, { "000100", 7, 1 },
    { "0000110", 0, 4 }, { "0000100", 2, 2 }, { "0000111", 8, 1 }, { "0000101", 9, 1 },
    { "00100110", 0, 5 }, { "00100001", 0, 6 }, { "00100101", 1, 3 }, { "00100100", 3, 2 },
    { "00100111", 10, 1 }, { "00100011", 11, 1 }, { "00100010", 12, 1 }, { "00100000", 13, 1 },
    { "0000001010", 0, 7 }, { "0000001100", 1, 4 }, { "0000001011", 2, 3 }, { "0000001111", 4, 2 },
    { "0000001001", 5, 2 }, { "0000001110", 14, 1 }, { "0000001101", 15, 1 }, { "0000001000", 16, 1 },
    { "000000011101", 0, 8 }, { "000000011000", 0, 9 }, { "000000010011", 0, 10 }, { "000000010000", 0, 11 },
    { "000000011011", 1, 5 }, { "000000010100", 2, 4 }, { "000000011100", 3, 3 }, { "000000010010", 4, 3 },
    { "000000011110", 6, 2 }, { "000000010101", 7, 2 }, { "000000010001", 8, 2 }, { "000000011111", 17, 1 },
    { "000000011010", 18, 1 }, { "000000011001", 19, 1 }, { "000000010111", 20, 1 }, { "000000010110", 21, 1 },
    { "0000000011010", 0, 12 }, { "0000000011001", 0, 13 }, { "0000000011000", 0, 14 }, { "0000000010111", 0, 15 },
    { "0000000010110", 1, 6 }, { "0000000010101", 1, 7 }, { "0000000010100", 2, 5 }, { "0000000010011", 3, 4 },
    { "0000000010010", 5, 3 }, { "0000000010001", 9, 2 }, { "0000000010000", 10, 2 }, { "0000000011111", 22, 1 },
    { "0000000011110", 23, 1 }, { "0000000011101", 24, 1 }, { "0000000011100", 25, 1 }, { "0000000011011", 26, 1 },
    { "00000000011111", 0, 16 }, { "00000000011110", 0, 17 }, { "00000000011101", 0, 18 }, { "00000000011100", 0, 19 },
    { "00000000011011", 0, 20 }, { "00000000011010", 0, 21 }, { "00000000011001", 0, 22 }, { "00000000011000", 0, 23 },
    { "00000000010111", 0, 24 }, { "00000000010110", 0, 25 }, { "00000000010101", 0, 26 }, { "00000000010100", 0, 27 },
    { "00000000010011", 0, 28 }, { "00000000010010", 0, 29 }, { "00000000010001", 0, 30 }, { "00000000010000", 0, 31 },
    { "000000000011000", 0, 32 }, { "000000000010111", 0, 33 }, { "000000000010110", 0, 34 }, { "000000000010101", 0, 35 },
    { "000000000010100", 0, 36 }, { "000000000010011", 0, 37 }, { "000000000010010", 0, 38 }, { "000000000010001", 0, 39 },
    { "000000000010000", 0, 40 }, { "000000000011111", 1, 8 }, { "000000000011110", 1, 9 }, { "000000000011101", 1, 10 },
    { "000000000011100", 1, 11 }, { "000000000011011", 1, 12 }, { "000000000011010", 1, 13 }, { "000000000011001", 1, 14 },
    { "0000000000010011", 1, 15 }, { "0000000000010010", 1, 16 }, { "0000000000010001", 1, 17 }, { "0000000000010000", 1, 18 },
    { "0000000000010100", 6, 3 }, { "0000000000011010", 11, 2 }, { "0000000000011001", 12, 2 }, { "0000000000011000", 13, 2 },
    { "0000000000010111", 14, 2 }, { "0000000000010110", 15, 2 }, { "0000000000010101", 16, 2 }, { "0000000000011111", 27, 1 },
    { "0000000000011110", 28, 1 }, { "0000000000011101", 29, 1 }, { "0000000000011100", 30, 1 }, { "0000000000011011", 31, 1 },
};

enum { VLC_INVALID = 0, VLC_CODE, VLC_EOB, VLC_ESCAPE };

typedef struct
{
    unsigned char kind;
    unsigned char len;     /* bits, without the sign bit */
    unsigned char run;
    unsigned char level;
} VlcEntry;

/* indexed by the next 16 bits of the stream */
static VlcEntry vlc_table[1 << 16];
static int vlc_ready;

static void vlc_fill(const char *bits, VlcEntry entry)
{
    int len = (int)strlen(bits);
    unsigned int prefix = 0;
    unsigned int i;
    int b;

    for (b = 0; b < len; b++)
    {
        prefix = (prefix << 1) | (bits[b] == '1');
    }

    entry.len = (unsigned char)len;

    for (i = 0; i < (1u << (16 - len)); i++)
    {
        vlc_table[(prefix << (16 - len)) | i] = entry;
    }
}

static void vlc_init(void)
{
    VlcEntry e;
    size_t i;

    memset(vlc_table, 0, sizeof(vlc_table));

    for (i = 0; i < sizeof(vlc_codes) / sizeof(vlc_codes[0]); i++)
    {
        e.kind = VLC_CODE;
        e.run = vlc_codes[i].run;
        e.level = vlc_codes[i].level;
        vlc_fill(vlc_codes[i].bits, e);
    }

    e.kind = VLC_EOB;
    e.run = e.level = 0;
    vlc_fill("10", e);

    e.kind = VLC_ESCAPE;
    vlc_fill("000001", e);

    vlc_ready = 1;
}

/* bits are read most significant first from little-endian 16-bit words */
typedef struct
{
    const unsigned short *words;
    unsigned int cache;    /* left-aligned */
    int count;             /* valid bits in cache */
} BitReader;

static void bits_fill(BitReader *br)
{
    while (br->count <= 16)
    {
        br->cache |= (unsigned int)*br->words++ << (16 - br->count);
        br->count += 16;
    }
}

static unsigned int bits_peek16(BitReader *br)
{
    bits_fill(br);
    return br->cache >> 16;
}

static unsigned int bits_get(BitReader *br, int n)
{
    unsigned int v;

    bits_fill(br);
    v = br->cache >> (32 - n);
    br->cache <<= n;
    br->count -= n;

    return v;
}

static int sign_extend10(unsigned int v)
{
    return (int)(v << 22) >> 22;
}

/* bs: 8-byte frame header (code count / 2, 0x3800, qscale, version) and
 * the bitstream; buf: 0x38000000 | word count, then 16-bit codes */
int DecDCTvlc(u_long *bs, u_long *buf)
{
    const unsigned short *hdr = (const unsigned short *)bs;
    unsigned short *out = (unsigned short *)((unsigned int *)buf + 1);
    int total = hdr[0] * 2;
    int qscale = hdr[2] & 0x3F;
    int produced = 0;
    BitReader br;

    if (!vlc_ready)
    {
        vlc_init();
    }

    if (hdr[3] != 2)
    {
        static int warned;

        if (!warned)
        {
            fprintf(stderr, "[rewraithed] movie: STR version %d is not supported\n", hdr[3]);
            warned = 1;
        }
    }

    br.words = hdr + 4;
    br.cache = 0;
    br.count = 0;

    while (produced < total)
    {
        /* DC */
        out[produced++] = (unsigned short)((qscale << 10) | (bits_get(&br, 10) & 0x3FF));

        /* AC until end of block */
        while (produced < total)
        {
            VlcEntry e = vlc_table[bits_peek16(&br)];

            if (e.kind == VLC_EOB)
            {
                bits_get(&br, 2);
                out[produced++] = 0xFE00;
                break;
            }
            else if (e.kind == VLC_ESCAPE)
            {
                int run;
                int level;

                bits_get(&br, 6);
                run = bits_get(&br, 6);
                level = sign_extend10(bits_get(&br, 10));
                out[produced++] = (unsigned short)((run << 10) | (level & 0x3FF));
            }
            else if (e.kind == VLC_CODE)
            {
                int level = e.level;

                bits_get(&br, e.len);

                if (bits_get(&br, 1))
                {
                    level = -level;
                }

                out[produced++] = (unsigned short)((e.run << 10) | (level & 0x3FF));
            }
            else
            {
                /* corrupt stream: end the frame */
                while (produced < total)
                {
                    out[produced++] = 0xFE00;
                }
                break;
            }
        }
    }

    *(unsigned int *)buf = 0x38000000u | (unsigned int)hdr[0];

    return 0;
}

/* --- the MDEC (DecDCTin / DecDCTout) ------------------------------------------ */

static const unsigned char zigzag[64] = {
     0,  1,  8, 16,  9,  2,  3, 10, 17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

/* libpress's default quantization matrix, natural order (luma and chroma) */
static const unsigned char quant[64] = {
     2, 16, 19, 22, 26, 27, 29, 34,
    16, 16, 22, 24, 27, 29, 34, 37,
    19, 22, 26, 27, 29, 34, 34, 38,
    22, 22, 26, 27, 29, 34, 37, 40,
    22, 26, 27, 29, 32, 35, 40, 48,
    26, 27, 29, 32, 35, 40, 48, 58,
    26, 27, 29, 34, 38, 46, 56, 69,
    27, 29, 35, 38, 46, 56, 69, 83,
};

static float idct_cos[8][8];   /* [x][u] = C(u)/2 * cos((2x+1)u pi/16) */
static int idct_ready;

#define MB_BYTES (16 * 16 * 3)
#define MAX_MBS  (40 * 30)         /* up to 640x480 */

static unsigned char mdec_out[MAX_MBS * MB_BYTES];
static int mdec_out_size;
static int mdec_out_pos;

static void (*mdec_callback)(void);
static int mdec_cb_pending;
static int mdec_in_callback;

static void idct_init(void)
{
    int x;
    int u;

    for (x = 0; x < 8; x++)
    {
        for (u = 0; u < 8; u++)
        {
            double c = (u == 0) ? sqrt(0.5) : 1.0;

            idct_cos[x][u] = (float)(c * 0.5 * cos((2 * x + 1) * u * 3.14159265358979 / 16.0));
        }
    }

    idct_ready = 1;
}

static void idct(const int *in, float *out)
{
    float tmp[64];
    int x;
    int y;
    int u;
    int v;

    /* rows: tmp[y][x] = sum_u in[y][u] * cos[x][u] */
    for (y = 0; y < 8; y++)
    {
        for (x = 0; x < 8; x++)
        {
            float s = 0;

            for (u = 0; u < 8; u++)
            {
                s += in[y * 8 + u] * idct_cos[x][u];
            }

            tmp[y * 8 + x] = s;
        }
    }

    for (x = 0; x < 8; x++)
    {
        for (y = 0; y < 8; y++)
        {
            float s = 0;

            for (v = 0; v < 8; v++)
            {
                s += tmp[v * 8 + x] * idct_cos[y][v];
            }

            out[y * 8 + x] = s;
        }
    }
}

/* one block of run-length codes; returns the codes left, or -1 at the end */
static int rl_block(const unsigned short **src, const unsigned short *end, float *out)
{
    int coef[64];
    const unsigned short *p = *src;
    unsigned short n;
    int q;
    int k;
    int val;

    while ((p < end) && (*p == 0xFE00))
    {
        p++;
    }

    if (p >= end)
    {
        *src = p;
        return -1;
    }

    memset(coef, 0, sizeof(coef));

    n = *p++;
    q = (n >> 10) & 0x3F;
    k = 0;
    val = sign_extend10(n) * quant[0];

    for (;;)
    {
        if (q == 0)
        {
            val = sign_extend10(n) * 2;
        }

        if (val < -0x400)
        {
            val = -0x400;
        }

        if (val > 0x3FF)
        {
            val = 0x3FF;
        }

        coef[(q == 0) ? k : zigzag[k]] = val;

        if (p >= end)
        {
            break;
        }

        n = *p++;
        k += ((n >> 10) & 0x3F) + 1;

        if (k > 63)
        {
            break;
        }

        val = (sign_extend10(n) * quant[zigzag[k]] * q + 4) / 8;
    }

    *src = p;
    idct(coef, out);

    return 0;
}

static int clamp_byte(float v)
{
    int i = (int)floorf(v + 0.5f);

    if (i < -128)
    {
        i = -128;
    }

    if (i > 127)
    {
        i = 127;
    }

    return i + 128;
}

/* buf: as written by DecDCTvlc; mode: output format (24-bit RGB here) */
void DecDCTin(u_long *buf, int mode)
{
    unsigned int header = *(unsigned int *)buf;
    const unsigned short *src = (const unsigned short *)((unsigned int *)buf + 1);
    const unsigned short *end = src + (header & 0xFFFF) * 2;
    int mb = 0;

    (void)mode;

    if (!idct_ready)
    {
        idct_init();
    }

    while (mb < MAX_MBS)
    {
        float blk[6][64];
        unsigned char *out = &mdec_out[mb * MB_BYTES];
        int i;
        int x;
        int y;

        /* Cr, Cb, then the four luma blocks */
        for (i = 0; i < 6; i++)
        {
            if (rl_block(&src, end, blk[i]) < 0)
            {
                break;
            }
        }

        if (i < 6)
        {
            break;
        }

        for (y = 0; y < 16; y++)
        {
            for (x = 0; x < 16; x++)
            {
                const float *ly = blk[2 + ((y >> 3) << 1) + (x >> 3)];
                float yy = ly[(y & 7) * 8 + (x & 7)];
                float cr = blk[0][(y >> 1) * 8 + (x >> 1)];
                float cb = blk[1][(y >> 1) * 8 + (x >> 1)];

                out[0] = (unsigned char)clamp_byte(yy + 1.402f * cr);
                out[1] = (unsigned char)clamp_byte(yy - 0.3437f * cb - 0.7143f * cr);
                out[2] = (unsigned char)clamp_byte(yy + 1.772f * cb);
                out += 3;
            }
        }

        mb++;
    }

    mdec_out_size = mb * MB_BYTES;
    mdec_out_pos = 0;
}

/* Copies the next `size` words of the decoded image; when that "transfer"
 * is done, the callback runs. The callback usually asks for the next piece,
 * which is queued here instead of nesting. */
void DecDCTout(u_long *buf, int size)
{
    int bytes = size * 4;
    int avail = mdec_out_size - mdec_out_pos;

    if (bytes > avail)
    {
        memset((char *)buf + MAX(avail, 0), 0, bytes - MAX(avail, 0));
        bytes = MAX(avail, 0);
    }

    memcpy(buf, &mdec_out[mdec_out_pos], bytes);
    mdec_out_pos += bytes;

    mdec_cb_pending++;

    if (mdec_in_callback)
    {
        return;
    }

    mdec_in_callback = 1;

    while (mdec_cb_pending > 0)
    {
        void (*cb)(void) = mdec_callback;

        mdec_cb_pending--;

        if (cb != NULL)
        {
            cb();
        }
    }

    mdec_in_callback = 0;
}

void DecDCTReset(int mode)
{
    (void)mode;

    mdec_out_size = 0;
    mdec_out_pos = 0;
    mdec_cb_pending = 0;
}

int DecDCToutCallback(void (*func)())
{
    void (*old)(void) = mdec_callback;

    mdec_callback = (void (*)(void))func;

    return (int)(intptr_t)old;
}
