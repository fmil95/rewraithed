/*
 * The Dreamcast version's movies in place of the PlayStation ones (Game
 * Movies: DC). They are Sofdec files (.sfd) in the root of the Dreamcast
 * disc (the GDI dump the Dreamcast models use, port/shared/dc_disc.c):
 * MPEG program streams of 320 x 240 MPEG-1 video at 29.97 frames a second
 * (twice the PlayStation's and the PC's 15) and about twice their bit rate,
 * with 44.1 kHz stereo CRI ADX sound as MPEG audio stream 0xC0.
 *
 * The file is read from the disc as it plays. pl_mpeg (MIT, fetched by
 * cmake/Dependencies.cmake) splits the stream and decodes the video; the
 * ADX sound is decoded here, as ffmpeg's libavcodec/adx.c does.
 *
 * CINE_Play (main_hooks.c) asks here first; a movie the disc lacks plays
 * from the PlayStation disc. Skipping is the cinema player's: a new press
 * of a button in the mask.
 */
#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"

#include "common.h"
#include "Game/CINEMA/CINEPSX.h"

#include <SDL.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "settings.h"
#include "dc_disc.h"

void port_present_rgb_frame(const unsigned char *rgb, int w, int h);   /* native/rgb24_present.c */
extern void PsyX_UpdateInput(void);
extern void port_spu_exclusive(int want);                              /* spu.c */

#define READ_CHUNK      (64 * 1024)
#define AUDIO_AHEAD_MS  300

/* --- the movie each PlayStation one stands for ------------------------------ */

/* "\KAININT.STR;1" -> "INTRO.SFD", "\CHRONO3.STR;1" -> "CHRONO3.SFD" */
static int dc_movie_name(const char *strfile, char *out, size_t size)
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
        base[n++] = (char)toupper((unsigned char)*p++);
    }

    base[n] = 0;

    if (n == 0)
    {
        return 0;
    }

    snprintf(out, size, "%s.SFD", (strcmp(base, "KAININT") == 0) ? "INTRO" : base);

    return 1;
}

/* --- ADX ------------------------------------------------------------------------ */

typedef struct
{
    unsigned char *data;        /* the ADX file, as its packets arrive */
    long length;
    long capacity;
    long pos;                   /* next block to decode */
    int haveHeader;
    int channels;
    int rate;
    int coeff[2];
    int hist[2][2];
    int ended;
} Adx;

static void adx_append(Adx *a, const unsigned char *bytes, long n)
{
    if (a->length + n > a->capacity)
    {
        long cap = (a->capacity == 0) ? (256 * 1024) : a->capacity;
        unsigned char *grown;

        while (cap < a->length + n)
        {
            cap *= 2;
        }

        grown = (unsigned char *)realloc(a->data, cap);

        if (grown == NULL)
        {
            return;
        }

        a->data = grown;
        a->capacity = cap;
    }

    memcpy(a->data + a->length, bytes, n);
    a->length += n;
}

/* the header, once enough of it is in: data offset, channels, rate, filter */
static int adx_header(Adx *a)
{
    const unsigned char *h = a->data;
    int offset;
    int cutoff;
    double x;
    double y;
    double c;

    if (a->haveHeader)
    {
        return 1;
    }

    if ((a->length < 20) || (h[0] != 0x80) || (h[1] != 0x00))
    {
        return 0;
    }

    offset = (h[2] << 8) | h[3];

    if (a->length < offset + 4)
    {
        return 0;
    }

    a->channels = h[7];
    a->rate = (int)(((unsigned long)h[8] << 24) | ((unsigned long)h[9] << 16) | ((unsigned long)h[10] << 8) | h[11]);
    cutoff = (h[16] << 8) | h[17];

    if ((h[4] != 3) || (h[5] != 18) || (h[6] != 4) || (a->channels < 1) || (a->channels > 2) || (a->rate <= 0))
    {
        a->ended = 1;
        return 0;
    }

    /* the prediction filter from the high-pass frequency (ffmpeg's
     * ff_adx_calculate_coeffs, 12 fraction bits) */
    x = sqrt(2.0) - cos(2.0 * 3.14159265358979323846 * cutoff / a->rate);
    y = sqrt(2.0) - 1.0;
    c = (x - sqrt((x + y) * (x - y))) / y;
    a->coeff[0] = (int)floor(c * 2.0 * 4096 + 0.5);
    a->coeff[1] = (int)floor(-(c * c) * 4096 + 0.5);

    a->haveHeader = 1;
    a->pos = offset + 4;

    return 1;
}

static short clamp16(int v)
{
    return (short)((v < -32768) ? -32768 : ((v > 32767) ? 32767 : v));
}

/* the whole frames (18 bytes a channel, 32 samples) that are in, as
 * interleaved stereo; returns the stereo frames written */
static int adx_decode(Adx *a, short *out, int maxFrames)
{
    int written = 0;
    long frameBytes;

    if (a->ended || !adx_header(a))
    {
        return 0;
    }

    frameBytes = 18L * a->channels;

    while ((a->pos + frameBytes <= a->length) && (written + 32 <= maxFrames))
    {
        int ch;

        for (ch = 0; ch < a->channels; ch++)
        {
            const unsigned char *b = a->data + a->pos + 18 * ch;
            int scale = (b[0] << 8) | b[1];
            int s1 = a->hist[ch][0];
            int s2 = a->hist[ch][1];
            int i;

            if (scale & 0x8000)
            {
                /* the end marker */
                a->ended = 1;
                return written;
            }

            for (i = 0; i < 32; i++)
            {
                int nibble = (i & 1) ? (b[2 + i / 2] & 15) : (b[2 + i / 2] >> 4);
                int d = (nibble & 8) ? (nibble - 16) : nibble;
                int s0 = d * scale + ((a->coeff[0] * s1 + a->coeff[1] * s2) >> 12);
                short s = clamp16(s0);

                s2 = s1;
                s1 = s;

                out[(written + i) * 2 + ch] = s;

                if (a->channels == 1)
                {
                    out[(written + i) * 2 + 1] = s;
                }
            }

            a->hist[ch][0] = s1;
            a->hist[ch][1] = s2;
        }

        a->pos += frameBytes;
        written += 32;
    }

    /* drop what is decoded once a lot has built up */
    if (a->pos > 4 * 1024 * 1024)
    {
        memmove(a->data, a->data + a->pos, a->length - a->pos);
        a->length -= a->pos;
        a->pos = 0;
    }

    return written;
}

/* --- the stream ------------------------------------------------------------------- */

typedef struct
{
    DcDisc *disc;
    long lba;
    long size;
    long read;                  /* bytes of the file given to the demuxer */
    plm_buffer_t *fileBuffer;
    plm_demux_t *demux;
    plm_buffer_t *videoBuffer;
    plm_video_t *video;
    Adx adx;
    unsigned char *chunk;
    long soundFrames;           /* stereo frames queued */
} Sfd;

/* the demuxer wants more of the file */
static void load_file(plm_buffer_t *buffer, void *user)
{
    Sfd *s = (Sfd *)user;
    long n = s->size - s->read;

    if (n <= 0)
    {
        plm_buffer_signal_end(buffer);
        return;
    }

    if (n > READ_CHUNK)
    {
        n = READ_CHUNK;
    }

    if (!dc_disc_read_file(s->disc, s->lba, s->read, n, s->chunk))
    {
        plm_buffer_signal_end(buffer);
        return;
    }

    s->read += n;
    plm_buffer_write(buffer, s->chunk, (size_t)n);
}

/* the video decoder wants more: packets until a video one (the sound ones
 * met on the way are kept for the ADX decoder) */
static void load_video(plm_buffer_t *buffer, void *user)
{
    Sfd *s = (Sfd *)user;

    for (;;)
    {
        plm_packet_t *packet = plm_demux_decode(s->demux);

        if (packet == NULL)
        {
            plm_buffer_signal_end(buffer);
            return;
        }

        if (packet->type == PLM_DEMUX_PACKET_VIDEO_1)
        {
            plm_buffer_write(buffer, packet->data, packet->length);
            return;
        }

        if (packet->type == PLM_DEMUX_PACKET_AUDIO_1)
        {
            adx_append(&s->adx, packet->data, (long)packet->length);
        }
    }
}

static void sfd_close(Sfd *s)
{
    if (s->video != NULL) plm_video_destroy(s->video);
    if (s->demux != NULL) plm_demux_destroy(s->demux);
    free(s->adx.data);
    free(s->chunk);

    if (s->disc != NULL)
    {
        dc_disc_close(s->disc);
    }
}

static int sfd_open(Sfd *s, const char *name)
{
    char gdi[260];

    memset(s, 0, sizeof(*s));

    if (!dc_disc_locate(gdi, sizeof(gdi), NULL, 0))
    {
        return 0;
    }

    s->disc = dc_disc_open(gdi);

    if ((s->disc == NULL) || !dc_disc_find_file(s->disc, name, &s->lba, &s->size) || (s->size <= 0))
    {
        sfd_close(s);
        return 0;
    }

    s->chunk = (unsigned char *)malloc(READ_CHUNK);
    s->fileBuffer = plm_buffer_create_with_capacity(256 * 1024);
    s->videoBuffer = plm_buffer_create_with_capacity(256 * 1024);

    if ((s->chunk == NULL) || (s->fileBuffer == NULL) || (s->videoBuffer == NULL))
    {
        if (s->fileBuffer != NULL) plm_buffer_destroy(s->fileBuffer);
        if (s->videoBuffer != NULL) plm_buffer_destroy(s->videoBuffer);
        sfd_close(s);
        return 0;
    }

    plm_buffer_set_load_callback(s->fileBuffer, load_file, s);
    plm_buffer_set_load_callback(s->videoBuffer, load_video, s);

    /* both destroy their buffers with themselves */
    s->demux = plm_demux_create(s->fileBuffer, 1);
    s->video = plm_video_create_with_buffer(s->videoBuffer, 1);

    if ((s->demux == NULL) || (s->video == NULL) || !plm_video_has_header(s->video))
    {
        sfd_close(s);
        return 0;
    }

    return 1;
}

/* --- playing ----------------------------------------------------------------------- */

static void queue_sound(SDL_AudioDeviceID dev, Sfd *s, short *pcm, int pcmFrames)
{
    int n;

    if (dev == 0)
    {
        return;
    }

    while ((SDL_GetQueuedAudioSize(dev) < (Uint32)(s->adx.rate * 4 * AUDIO_AHEAD_MS / 1000)) &&
        ((n = adx_decode(&s->adx, pcm, pcmFrames)) > 0))
    {
        SDL_QueueAudio(dev, pcm, (Uint32)n * 4);
        s->soundFrames += n;
    }
}

int port_dc_movie_play(const char *strfile, unsigned short mask)
{
    char name[40];
    Sfd s;
    int w;
    int h;
    double fps;
    unsigned char *rgb;
    short *pcm;
    int pcmFrames = 32 * 256;
    SDL_AudioDeviceID dev = 0;
    unsigned short lastPad0 = 0xFFFF;
    unsigned short lastPad1 = 0xFFFF;
    Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 start;
    int f;

    if ((port_settings.gameMovies != MOVIES_DC) || !dc_movie_name(strfile, name, sizeof(name)))
    {
        return 0;
    }

    if (!sfd_open(&s, name))
    {
        return 0;
    }

    w = plm_video_get_width(s.video);
    h = plm_video_get_height(s.video);
    fps = plm_video_get_framerate(s.video);

    if ((w <= 0) || (h <= 0) || (fps <= 0))
    {
        printf("[rewraithed] DC movie %s: not playable, the PlayStation one plays\n", name);
        sfd_close(&s);
        return 0;
    }

    printf("[rewraithed] DC movie %s: %d x %d at %.3f a second\n", name, w, h, fps);

    rgb = (unsigned char *)calloc((size_t)w * h * 3, 1);
    pcm = (short *)malloc((size_t)pcmFrames * 2 * sizeof(short));

    /* the first frame brings in the sound up to it, and the ADX header */
    {
        plm_frame_t *first = plm_video_decode(s.video);

        if (first != NULL)
        {
            plm_frame_to_rgb(first, rgb, w * 3);
        }

        if (adx_header(&s.adx))
        {
            SDL_AudioSpec want;
            SDL_AudioSpec have;

            memset(&want, 0, sizeof(want));
            want.freq = s.adx.rate;
            want.format = AUDIO_S16SYS;
            want.channels = 2;
            want.samples = 1024;

            /* the game's exclusive output (spu.c) holds the device */
            port_spu_exclusive(0);

            SDL_InitSubSystem(SDL_INIT_AUDIO);
            dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        }

        if (first == NULL)
        {
            f = -1;
        }
        else
        {
            f = 0;
        }
    }

    if ((dev != 0) && (pcm != NULL))
    {
        queue_sound(dev, &s, pcm, pcmFrames);
        SDL_PauseAudioDevice(dev, 0);
    }

    start = SDL_GetPerformanceCounter();

    while ((f >= 0) && (rgb != NULL) && (pcm != NULL))
    {
        Uint64 due = start + (Uint64)((double)f * (double)freq / fps);
        unsigned short pad0;
        unsigned short pad1;
        unsigned short pressed;
        plm_frame_t *frame;

        queue_sound(dev, &s, pcm, pcmFrames);

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

        port_present_rgb_frame(rgb, w, h);

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

        /* the next picture */
        frame = plm_video_decode(s.video);

        if (frame == NULL)
        {
            break;
        }

        plm_frame_to_rgb(frame, rgb, w * 3);
        f++;
    }

    printf("[rewraithed] DC movie %s: %d pictures shown, %.1f s of sound\n", name, f + 1,
        (s.adx.rate > 0) ? (double)s.soundFrames / s.adx.rate : 0.0);

    /* the sound still queued plays out, unless skipped */
    if (dev != 0)
    {
        SDL_CloseAudioDevice(dev);
    }

    /* black after it, as the PlayStation player leaves */
    port_present_rgb_frame(NULL, 0, 0);

    free(pcm);
    free(rgb);
    sfd_close(&s);

    return 1;
}
