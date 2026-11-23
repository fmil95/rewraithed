/*
 * The PC version's movies in place of the PlayStation ones ("Use PC Movies",
 * port/shared/pc_disc.c finds them). The PC movies are ARMovie/Escape files
 * (.rpl): 320 x 240 pictures in Eidos' Escape 130 video codec, 15 a second,
 * with 44.1 kHz stereo 4-bit IMA ADPCM sound. The PlayStation's are MDEC
 * streams of the same size, whose 8 x 8 blocks show when enlarged.
 *
 * CINE_Play (main_hooks.c) asks here first: when the PC version has the
 * movie, it is played here, with the same skipping (a new press of a
 * button in the mask), and the PlayStation one is not.
 *
 * The Escape 130 and ADPCM decoding follow FFmpeg's (libavcodec/
 * escape130.c, adpcm.c; the RPL layout from libavformat/rpl.c), LGPL 2.1 or
 * later: the tables below are theirs.
 */
#include "common.h"
#include "Game/CINEMA/CINEPSX.h"

#include <SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "settings.h"
#include "pc_disc.h"

void port_present_rgb_frame(const unsigned char *rgb, int w, int h);   /* native/rgb24_present.c */
extern void PsyX_UpdateInput(void);

/* --- the PlayStation movie each PC one stands for --------------------------- */

static int pc_movie_name(const char *strfile, char *out, size_t size)
{
    char base[32];
    size_t n = 0;
    const char *p = strfile;

    while ((*p == '\\') || (*p == '/'))
    {
        p++;
    }

    while (*p && (*p != '.') && (*p != ';') && (n < sizeof(base) - 1))
    {
        base[n++] = (char)tolower((unsigned char)*p++);
    }

    base[n] = 0;

    if (n == 0)
    {
        return 0;
    }

    /* the intro is KAININT on the PlayStation, intro on PC */
    snprintf(out, size, "%s.rpl", (strcmp(base, "kainint") == 0) ? "intro" : base);

    return 1;
}

/* --- RPL ------------------------------------------------------------------- */

typedef struct
{
    long offset;
    long videoSize;
    long audioSize;
} RplChunk;

typedef struct
{
    const unsigned char *data;
    long size;
    int width, height;
    double fps;
    int audioRate, audioChannels, audioBits;
    int chunkCount;
    RplChunk *chunks;
} Rpl;

/* the text line at *pos, advancing past it */
static int rpl_line(const Rpl *r, long *pos, char *out, size_t size)
{
    size_t n = 0;

    if (*pos >= r->size)
    {
        return 0;
    }

    while ((*pos < r->size) && (r->data[*pos] != '\n'))
    {
        if ((n < size - 1) && (r->data[*pos] != '\r'))
        {
            out[n++] = (char)r->data[*pos];
        }

        (*pos)++;
    }

    (*pos)++;
    out[n] = 0;

    return 1;
}

static int rpl_open(Rpl *r, const unsigned char *data, long size)
{
    char line[256];
    long pos = 0;
    long values[21];
    double fps = 0;
    long catalog;
    int i;

    memset(r, 0, sizeof(*r));
    r->data = data;
    r->size = size;

    /* 21 lines of header, each starting with its value */
    for (i = 0; i < 21; i++)
    {
        if (!rpl_line(r, &pos, line, sizeof(line)))
        {
            return 0;
        }

        values[i] = atol(line);

        if (i == 0 && strncmp(line, "ARMovie", 7) != 0)
        {
            return 0;
        }

        if (i == 8)
        {
            fps = atof(line);
        }
    }

    if (values[4] != 130)
    {
        printf("[rewraithed] PC movie: video format %ld, only Escape 130 is played\n", values[4]);
        return 0;
    }

    r->width = (int)values[5];
    r->height = (int)values[6];
    r->fps = (fps > 1.0) ? fps : 15.0;
    r->audioRate = (values[9] == 101) ? (int)values[10] : 0;
    r->audioChannels = (int)values[11];
    r->audioBits = (int)values[12];
    r->chunkCount = (int)values[14] + 1;    /* the number of the last one */
    catalog = values[17];

    if ((r->width <= 0) || (r->height <= 0) || (r->width & 1) || (r->height & 1) ||
        (r->chunkCount <= 0) || (r->chunkCount > 100000) || (catalog <= 0) || (catalog >= size))
    {
        return 0;
    }

    if ((r->audioBits != 4) || (r->audioChannels < 1) || (r->audioChannels > 2))
    {
        r->audioRate = 0;   /* sound of a kind not played here: silent */
    }

    r->chunks = (RplChunk *)calloc(r->chunkCount, sizeof(RplChunk));

    if (r->chunks == NULL)
    {
        return 0;
    }

    /* the chunk catalog: "offset,video size;audio size" per chunk */
    pos = catalog;

    for (i = 0; i < r->chunkCount; i++)
    {
        long offset;
        long video;
        long audio;

        if (!rpl_line(r, &pos, line, sizeof(line)) || (sscanf(line, "%ld , %ld ; %ld", &offset, &video, &audio) != 3) ||
            (offset < 0) || (video < 0) || (audio < 0) || (offset + video + audio > size))
        {
            r->chunkCount = i;
            break;
        }

        r->chunks[i].offset = offset;
        r->chunks[i].videoSize = video;
        r->chunks[i].audioSize = audio;
    }

    return r->chunkCount > 0;
}

/* --- Escape 130 ------------------------------------------------------------- */

static const unsigned char offset_table[] = { 2, 4, 10, 20 };

static const signed char sign_table[64][4] = {
    {  0,  0,  0,  0 }, { -1,  1,  0,  0 }, {  1, -1,  0,  0 }, { -1,  0,  1,  0 },
    { -1,  1,  1,  0 }, {  0, -1,  1,  0 }, {  1, -1,  1,  0 }, { -1, -1,  1,  0 },
    {  1,  0, -1,  0 }, {  0,  1, -1,  0 }, {  1,  1, -1,  0 }, { -1,  1, -1,  0 },
    {  1, -1, -1,  0 }, { -1,  0,  0,  1 }, { -1,  1,  0,  1 }, {  0, -1,  0,  1 },
    {  0,  0,  0,  0 }, {  1, -1,  0,  1 }, { -1, -1,  0,  1 }, { -1,  0,  1,  1 },
    { -1,  1,  1,  1 }, {  0, -1,  1,  1 }, {  1, -1,  1,  1 }, { -1, -1,  1,  1 },
    {  0,  0, -1,  1 }, {  1,  0, -1,  1 }, { -1,  0, -1,  1 }, {  0,  1, -1,  1 },
    {  1,  1, -1,  1 }, { -1,  1, -1,  1 }, {  0, -1, -1,  1 }, {  1, -1, -1,  1 },
    {  0,  0,  0,  0 }, { -1, -1, -1,  1 }, {  1,  0,  0, -1 }, {  0,  1,  0, -1 },
    {  1,  1,  0, -1 }, { -1,  1,  0, -1 }, {  1, -1,  0, -1 }, {  0,  0,  1, -1 },
    {  1,  0,  1, -1 }, { -1,  0,  1, -1 }, {  0,  1,  1, -1 }, {  1,  1,  1, -1 },
    { -1,  1,  1, -1 }, {  0, -1,  1, -1 }, {  1, -1,  1, -1 }, { -1, -1,  1, -1 },
    {  0,  0,  0,  0 }, {  1,  0, -1, -1 }, {  0,  1, -1, -1 }, {  1,  1, -1, -1 },
    { -1,  1, -1, -1 }, {  1, -1, -1, -1 }
};

static const signed char luma_adjust[] = { -4, -3, -2, -1, 1, 2, 3, 4 };

static const signed char chroma_adjust[2][8] = {
    { 1, 1, 0, -1, -1, -1,  0,  1 },
    { 0, 1, 1,  1,  0, -1, -1, -1 }
};

static const unsigned char chroma_vals[] = {
     20,  28,  36,  44,  52,  60,  68,  76,
     84,  92, 100, 106, 112, 116, 120, 124,
    128, 132, 136, 140, 144, 150, 156, 164,
    172, 180, 188, 196, 204, 212, 220, 228
};

typedef struct
{
    int width, height;
    unsigned char *oldY, *newY;     /* 6-bit luma, per pixel */
    unsigned char *oldU, *newU;     /* 5-bit chroma, per 2 x 2 block */
    unsigned char *oldV, *newV;
    unsigned char *yAvg;            /* per block */
} Escape130;

/* little-endian bit reader */
typedef struct
{
    const unsigned char *data;
    long bits;
    long pos;
} Bits;

static unsigned get_bits(Bits *b, int n)
{
    unsigned v = 0;
    int i;

    for (i = 0; i < n; i++, b->pos++)
    {
        if (b->pos < b->bits)
        {
            v |= ((b->data[b->pos >> 3] >> (b->pos & 7)) & 1u) << i;
        }
    }

    return v;
}

static int skip_count(Bits *b)
{
    unsigned v;

    if (b->bits - b->pos < 1 + 3)
    {
        return -1;
    }

    if (get_bits(b, 1))
    {
        return 0;
    }

    v = get_bits(b, 3);

    if (v)
    {
        return (int)v;
    }

    v = get_bits(b, 8);

    if (v)
    {
        return (int)v + 7;
    }

    v = get_bits(b, 15);

    if (v)
    {
        return (int)v + 262;
    }

    return -1;
}

static int esc_init(Escape130 *e, int width, int height)
{
    size_t luma = (size_t)width * height;
    size_t blocks = luma / 4;

    e->width = width;
    e->height = height;
    e->oldY = (unsigned char *)calloc(luma, 1);
    e->newY = (unsigned char *)calloc(luma, 1);
    e->oldU = (unsigned char *)malloc(blocks);
    e->newU = (unsigned char *)malloc(blocks);
    e->oldV = (unsigned char *)malloc(blocks);
    e->newV = (unsigned char *)malloc(blocks);
    e->yAvg = (unsigned char *)calloc(blocks, 1);

    if (!e->oldY || !e->newY || !e->oldU || !e->newU || !e->oldV || !e->newV || !e->yAvg)
    {
        return 0;
    }

    memset(e->oldU, 0x10, blocks);
    memset(e->oldV, 0x10, blocks);

    return 1;
}

static void esc_free(Escape130 *e)
{
    free(e->oldY); free(e->newY);
    free(e->oldU); free(e->newU);
    free(e->oldV); free(e->newV);
    free(e->yAvg);
}

static int clip(int v, int lo, int hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

/* one frame, then into rgb (width x height x 3) */
static void esc_decode(Escape130 *e, const unsigned char *data, long size, unsigned char *rgb)
{
    int bw = e->width / 2;
    int total = bw * (e->height / 2);
    unsigned y[4] = { 0, 0, 0, 0 };
    unsigned cb = 0x10;
    unsigned cr = 0x10;
    int yavg = 0;
    int skip = -1;
    Bits bits;
    int b;
    unsigned char *t;

    if (size <= 16)
    {
        return;
    }

    /* the first 16 bytes are a header with nothing needed in it */
    bits.data = data;
    bits.bits = size * 8;
    bits.pos = 16 * 8;

    for (b = 0; b < total; b++)
    {
        int bx = b % bw;
        int by = b / bw;
        size_t o = (size_t)by * 2 * e->width + bx * 2;
        int i;

        if (skip == -1)
        {
            skip = skip_count(&bits);

            if (skip == -1)
            {
                /* ends early: the rest stays as it was */
                skip = total;
            }
        }

        if (skip)
        {
            y[0] = e->oldY[o];
            y[1] = e->oldY[o + 1];
            y[2] = e->oldY[o + e->width];
            y[3] = e->oldY[o + e->width + 1];
            yavg = e->yAvg[b];
            cb = e->oldU[b];
            cr = e->oldV[b];
        }
        else
        {
            if (get_bits(&bits, 1))
            {
                unsigned sign = get_bits(&bits, 6);
                unsigned diff = get_bits(&bits, 2);

                yavg = 2 * (int)get_bits(&bits, 5);

                for (i = 0; i < 4; i++)
                {
                    y[i] = (unsigned)clip(yavg + offset_table[diff] * sign_table[sign][i], 0, 63);
                }
            }
            else if (get_bits(&bits, 1))
            {
                if (get_bits(&bits, 1))
                {
                    yavg = (int)get_bits(&bits, 6);
                }
                else
                {
                    yavg = (yavg + luma_adjust[get_bits(&bits, 3)]) & 63;
                }

                for (i = 0; i < 4; i++)
                {
                    y[i] = (unsigned)yavg;
                }
            }

            if (get_bits(&bits, 1))
            {
                if (get_bits(&bits, 1))
                {
                    cb = get_bits(&bits, 5);
                    cr = get_bits(&bits, 5);
                }
                else
                {
                    unsigned a = get_bits(&bits, 3);

                    cb = (cb + chroma_adjust[0][a]) & 31;
                    cr = (cr + chroma_adjust[1][a]) & 31;
                }
            }
        }

        e->yAvg[b] = (unsigned char)yavg;
        e->newY[o] = (unsigned char)y[0];
        e->newY[o + 1] = (unsigned char)y[1];
        e->newY[o + e->width] = (unsigned char)y[2];
        e->newY[o + e->width + 1] = (unsigned char)y[3];
        e->newU[b] = (unsigned char)cb;
        e->newV[b] = (unsigned char)cr;

        skip--;
    }

    /* YUV (BT.601) to RGB */
    for (b = 0; b < total; b++)
    {
        int bx = b % bw;
        int by = b / bw;
        int u = chroma_vals[e->newU[b]] - 128;
        int v = chroma_vals[e->newV[b]] - 128;
        int k;

        for (k = 0; k < 4; k++)
        {
            int px = bx * 2 + (k & 1);
            int py = by * 2 + (k >> 1);
            int luma = e->newY[(size_t)py * e->width + px] << 2;
            unsigned char *p = rgb + ((size_t)py * e->width + px) * 3;

            p[0] = (unsigned char)clip(luma + ((91881 * v) >> 16), 0, 255);
            p[1] = (unsigned char)clip(luma - ((22554 * u + 46802 * v) >> 16), 0, 255);
            p[2] = (unsigned char)clip(luma + ((116130 * u) >> 16), 0, 255);
        }
    }

    t = e->oldY; e->oldY = e->newY; e->newY = t;
    t = e->oldU; e->oldU = e->newU; e->newU = t;
    t = e->oldV; e->oldV = e->newV; e->newV = t;
}

/* --- IMA ADPCM (Acorn/Escape) ---------------------------------------------- */

static const int ima_index[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

static const int ima_step[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107,
    118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
    6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

typedef struct
{
    int predictor;
    int index;
} Ima;

static short ima_nibble(Ima *c, int nibble)
{
    int step = ima_step[c->index];
    int diff = ((nibble & 7) * step) >> 2;

    c->index = clip(c->index + ima_index[nibble], 0, 88);
    c->predictor = clip((nibble & 8) ? (c->predictor - diff) : (c->predictor + diff), -32768, 32767);

    return (short)c->predictor;
}

/* bytes of one chunk into 16-bit stereo; returns the samples written */
static int ima_decode(Ima state[2], int channels, const unsigned char *in, long size, short *out)
{
    long i;
    int n = 0;

    for (i = 0; i < size; i++)
    {
        short a = ima_nibble(&state[0], in[i] >> 4);
        short b = ima_nibble(&state[(channels == 2) ? 1 : 0], in[i] & 15);

        if (channels == 2)
        {
            out[n++] = a;
            out[n++] = b;
        }
        else
        {
            out[n++] = a;
            out[n++] = a;
            out[n++] = b;
            out[n++] = b;
        }
    }

    return n;
}

/* --- playing ----------------------------------------------------------------- */

/* chunks of sound queued ahead of the picture */
#define AUDIO_LEAD 3

static void queue_audio(SDL_AudioDeviceID dev, const Rpl *r, Ima state[2], int chunk, short *buffer)
{
    const RplChunk *c;
    int n;

    if ((dev == 0) || (chunk >= r->chunkCount))
    {
        return;
    }

    c = &r->chunks[chunk];

    if (c->audioSize <= 0)
    {
        return;
    }

    n = ima_decode(state, r->audioChannels, r->data + c->offset + c->videoSize, c->audioSize, buffer);
    SDL_QueueAudio(dev, buffer, (Uint32)n * sizeof(short));
}

/* Plays the PC version of a movie, if there is one: 1 when played. */
int port_pc_movie_play(const char *strfile, unsigned short mask)
{
    char name[40];
    unsigned char *file;
    long size;
    Rpl rpl;
    Escape130 esc;
    unsigned char *rgb;
    short *pcm;
    SDL_AudioDeviceID dev = 0;
    Ima state[2] = { { 0, 0 }, { 0, 0 } };
    unsigned short lastPad0 = 0xFFFF;
    unsigned short lastPad1 = 0xFFFF;
    Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 start;
    long maxAudio = 0;
    int f;

    if (!port_settings.pcMovies || !pc_movie_name(strfile, name, sizeof(name)))
    {
        return 0;
    }

    file = pc_disc_read_movie(name, &size);

    if (file == NULL)
    {
        return 0;
    }

    if (!rpl_open(&rpl, file, size) || !esc_init(&esc, rpl.width, rpl.height))
    {
        printf("[rewraithed] PC movie %s: not playable, the PlayStation one plays\n", name);
        free(rpl.chunks);
        esc_free(&esc);
        free(file);
        return 0;
    }

    printf("[rewraithed] PC movie %s: %d x %d, %d frames at %.3f a second\n", name, rpl.width, rpl.height,
        rpl.chunkCount, rpl.fps);

    for (f = 0; f < rpl.chunkCount; f++)
    {
        if (rpl.chunks[f].audioSize > maxAudio)
        {
            maxAudio = rpl.chunks[f].audioSize;
        }
    }

    rgb = (unsigned char *)calloc((size_t)rpl.width * rpl.height * 3, 1);
    pcm = (short *)malloc((size_t)(maxAudio + 1) * 4 * sizeof(short));

    if (rpl.audioRate > 0)
    {
        SDL_AudioSpec want;
        SDL_AudioSpec have;

        memset(&want, 0, sizeof(want));
        want.freq = rpl.audioRate;
        want.format = AUDIO_S16SYS;
        want.channels = 2;
        want.samples = 1024;

        /* the game's exclusive output (spu.c) holds the device: shared
         * output for the movie; the next frame takes it back */
        {
            extern void port_spu_exclusive(int want);

            port_spu_exclusive(0);
        }

        SDL_InitSubSystem(SDL_INIT_AUDIO);
        dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);

        if (dev != 0)
        {
            for (f = 0; f < AUDIO_LEAD; f++)
            {
                queue_audio(dev, &rpl, state, f, pcm);
            }

            SDL_PauseAudioDevice(dev, 0);
        }
    }

    start = SDL_GetPerformanceCounter();

    for (f = 0; (f < rpl.chunkCount) && (rgb != NULL) && (pcm != NULL); f++)
    {
        Uint64 due = start + (Uint64)((double)f * (double)freq / rpl.fps);
        unsigned short pad0;
        unsigned short pad1;
        unsigned short pressed;

        esc_decode(&esc, rpl.data + rpl.chunks[f].offset, rpl.chunks[f].videoSize, rgb);

        queue_audio(dev, &rpl, state, f + AUDIO_LEAD, pcm);

        /* this frame's time */
        for (;;)
        {
            Uint64 now = SDL_GetPerformanceCounter();
            Uint64 left;

            if (now >= due)
            {
                break;
            }

            left = ((due - now) * 1000) / freq;
            SDL_Delay((left > 2) ? (Uint32)(left - 1) : 0);
        }

        port_present_rgb_frame(rgb, rpl.width, rpl.height);

        /* the cinema player's skip: a new press of a button in the mask
         * (the pads read active-low) */
        PsyX_UpdateInput();
        pad0 = CINE_Pad(0);
        pad1 = CINE_Pad(1);
        pressed = (unsigned short)((lastPad0 & ~pad0) | (lastPad1 & ~pad1));
        lastPad0 = pad0;
        lastPad1 = pad1;

        if (pressed & mask)
        {
            break;
        }
    }

    if (dev != 0)
    {
        SDL_CloseAudioDevice(dev);
    }

    /* black after it, as the PlayStation player leaves */
    port_present_rgb_frame(NULL, 0, 0);

    free(pcm);
    free(rgb);
    esc_free(&esc);
    free(rpl.chunks);
    free(file);

    return 1;
}
