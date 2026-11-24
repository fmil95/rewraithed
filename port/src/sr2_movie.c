/*
 * "Use SR2 Movie for the Intro" (Mods): the intro movie of a new game
 * (KAININT.STR) from Soul Reaver 2's extras on PlayStation 2,
 * \PSS\SR1INTRO.PSS (port/shared/sr2_disc.c finds it, on the disc's image or
 * on its own).
 *
 * A PSS is an MPEG-2 program stream: packs of packets, the video (stream
 * 0xE0) MPEG-2 of 640 x 480 at 29.97 pictures a second, the sound in private
 * stream 1 (0xBD): after a 4-byte sub-header, an "SShd" header (format, rate,
 * channels, interleave) and an "SSbd" one, then 16-bit little-endian PCM in
 * blocks of `interleave` bytes per channel, left then right. The video goes
 * to Windows' MPEG-2 decoder (native/mpeg2_decoder.c), the sound to SDL; each
 * picture is shown at its time from the start. Skipping is the cinema
 * player's: a new press of a button in the mask. CINE_Play (main_hooks.c)
 * asks here first.
 */
#include "common.h"
#include "Game/CINEMA/CINEPSX.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "settings.h"
#include "sr2_disc.h"

void port_present_rgb_frame(const unsigned char *rgb, int w, int h);   /* native/rgb24_present.c */
extern void PsyX_UpdateInput(void);
void port_debug_frame(void);                                           /* native/debug_capture.c */
extern void port_spu_exclusive(int want);                              /* spu.c */

/* native/mpeg2_decoder.c */
void *port_mpeg2_open(int width, int height, char *error, size_t errorSize);
int port_mpeg2_feed(void *decoder, const unsigned char *data, size_t size);
void port_mpeg2_drain(void *decoder);
int port_mpeg2_picture(void *decoder, unsigned char *rgb);
void port_mpeg2_close(void *decoder);

#define WIDTH       640
#define HEIGHT      480
#define FPS         (30000.0 / 1001.0)
#define PATH_SIZE   260
#define READ_SIZE   (512 * 1024)
#define MAX_PACKET  (6 + 65535)

typedef struct
{
    FILE *f;
    long long left;             /* bytes of the movie not yet read */
    unsigned char *buf;
    size_t size, pos;
    int ended;
} Reader;

typedef struct
{
    SDL_AudioDeviceID dev;
    int rate, channels, interleave;
    int sawHeader;
    unsigned char *pending;     /* sound bytes not yet a whole left/right pair of blocks */
    size_t pendingSize;
    short *mixed;
} Sound;

/* at least `want` bytes from pos, unless the movie ends first */
static size_t reader_have(Reader *r, size_t want)
{
    if (r->size - r->pos < want)
    {
        size_t keep = r->size - r->pos;
        size_t room;
        size_t got;

        memmove(r->buf, r->buf + r->pos, keep);
        r->size = keep;
        r->pos = 0;
        room = READ_SIZE + MAX_PACKET - keep;

        if ((long long)room > r->left)
        {
            room = (size_t)r->left;
        }

        got = (room > 0) ? fread(r->buf + keep, 1, room, r->f) : 0;
        r->size += got;
        r->left -= got;
    }

    return r->size - r->pos;
}

static unsigned le32(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

/* left/right blocks into interleaved samples, queued */
static void sound_data(Sound *s, const unsigned char *data, size_t size)
{
    size_t pair;
    size_t at = 0;
    unsigned char *grown;

    if ((s->dev == 0) || (s->interleave <= 0))
    {
        return;
    }

    pair = (size_t)s->interleave * s->channels;
    grown = (unsigned char *)realloc(s->pending, s->pendingSize + size);

    if (grown == NULL)
    {
        return;
    }

    s->pending = grown;
    memcpy(s->pending + s->pendingSize, data, size);
    s->pendingSize += size;

    while (s->pendingSize - at >= pair)
    {
        int n = s->interleave / 2;
        int i;
        const unsigned char *l = s->pending + at;
        const unsigned char *r = (s->channels > 1) ? l + s->interleave : l;

        for (i = 0; i < n; i++)
        {
            s->mixed[i * 2] = (short)(l[i * 2] | (l[i * 2 + 1] << 8));
            s->mixed[i * 2 + 1] = (short)(r[i * 2] | (r[i * 2 + 1] << 8));
        }

        SDL_QueueAudio(s->dev, s->mixed, (Uint32)n * 4);
        at += pair;
    }

    memmove(s->pending, s->pending + at, s->pendingSize - at);
    s->pendingSize -= at;
}

/* private stream 1: the sound, the first packet with its headers */
static void sound_packet(Sound *s, const unsigned char *p, size_t size)
{
    if (size <= 4)
    {
        return;
    }

    p += 4;
    size -= 4;

    if (!s->sawHeader && (size >= 40) && (memcmp(p, "SShd", 4) == 0))
    {
        unsigned headerSize = le32(p + 4);
        unsigned format = le32(p + 8);
        SDL_AudioSpec want;
        SDL_AudioSpec have;

        s->sawHeader = 1;
        s->rate = (int)le32(p + 12);
        s->channels = (int)le32(p + 16);
        s->interleave = (int)le32(p + 20);

        if ((8 + headerSize + 8 > size) || (memcmp(p + 8 + headerSize, "SSbd", 4) != 0) || (format != 1) ||
            (s->channels < 1) || (s->channels > 2) || (s->interleave <= 0) || (s->interleave > 65536) || (s->rate <= 0))
        {
            printf("[rewraithed] SR2 intro: sound in a form this doesn't play (format %u), no sound\n", format);
            return;
        }

        s->mixed = (short *)malloc((size_t)s->interleave * 2);
        memset(&want, 0, sizeof(want));
        want.freq = s->rate;
        want.format = AUDIO_S16SYS;
        want.channels = 2;
        want.samples = 1024;

        SDL_InitSubSystem(SDL_INIT_AUDIO);
        s->dev = (s->mixed != NULL) ? SDL_OpenAudioDevice(NULL, 0, &want, &have, 0) : 0;

        if (s->dev != 0)
        {
            SDL_PauseAudioDevice(s->dev, 0);
        }

        p += 8 + headerSize + 8;
        size -= 8 + headerSize + 8;
    }

    if (s->sawHeader)
    {
        sound_data(s, p, size);
    }
}

/* The next video data of the movie (with the sound met on the way queued):
 * a pointer into the reader's buffer and its size, or NULL at the end. */
static const unsigned char *next_video(Reader *r, Sound *s, size_t *size)
{
    for (;;)
    {
        size_t have = reader_have(r, MAX_PACKET + 16);
        const unsigned char *b = r->buf + r->pos;
        unsigned code;
        size_t length;

        if (have < 4)
        {
            r->ended = 1;
            return NULL;
        }

        if ((b[0] != 0) || (b[1] != 0) || (b[2] != 1))
        {
            r->pos++;
            continue;
        }

        code = b[3];

        if (code == 0xB9)
        {
            r->ended = 1;
            return NULL;
        }

        if (code == 0xBA)
        {
            /* the pack header: MPEG-2's 14 bytes and its stuffing */
            r->pos += (have >= 14) ? (size_t)(14 + (b[13] & 7)) : 4;
            continue;
        }

        if ((code < 0xBB) || (have < 6))
        {
            r->pos += 4;
            continue;
        }

        length = 6 + (((size_t)b[4] << 8) | b[5]);

        if (length > have)
        {
            r->ended = 1;
            return NULL;
        }

        r->pos += length;

        if (((code == 0xBD) || ((code >= 0xE0) && (code <= 0xEF))) && (length >= 9) && (9 + (size_t)b[8] <= length))
        {
            const unsigned char *payload = b + 9 + b[8];
            size_t payloadSize = length - 9 - b[8];

            if (code == 0xBD)
            {
                sound_packet(s, payload, payloadSize);
            }
            else if (code == 0xE0)
            {
                *size = payloadSize;
                return payload;
            }
        }
    }
}

static int is_intro(const char *strfile)
{
    const char *p = strfile;

    while ((*p == '\\') || (*p == '/'))
    {
        p++;
    }

    return SDL_strncasecmp(p, "KAININT.", 8) == 0;
}

/* Plays SR2's copy of the intro, if the mod is on and it is there: 1 when played. */
int port_sr2_movie_play(const char *strfile, unsigned short mask)
{
    char path[PATH_SIZE];
    char problem[400];
    long long offset, length;
    Reader r;
    Sound s;
    void *decoder;
    unsigned char *rgb;
    unsigned short lastPad0, lastPad1;
    Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 start = 0;
    long shown = 0;
    int drained = 0;
    const unsigned char *held = NULL;   /* video data the decoder didn't take yet */
    size_t heldSize = 0;

    if (!port_settings.mods[MOD_SR2_INTRO] || !is_intro(strfile))
    {
        return 0;
    }

    if (!sr2_intro_locate(path, sizeof(path), &offset, &length, problem, sizeof(problem)))
    {
        printf("[rewraithed] SR2 intro: %s The game's own plays.\n", problem);
        return 0;
    }

    memset(&r, 0, sizeof(r));
    memset(&s, 0, sizeof(s));
    r.f = fopen(path, "rb");
    r.left = length;
    r.buf = (unsigned char *)malloc(READ_SIZE + MAX_PACKET + 16);
    rgb = (unsigned char *)malloc((size_t)WIDTH * HEIGHT * 3);
    decoder = port_mpeg2_open(WIDTH, HEIGHT, problem, sizeof(problem));

    if ((r.f == NULL) || (r.buf == NULL) || (rgb == NULL) || (decoder == NULL) || (_fseeki64(r.f, offset, SEEK_SET) != 0))
    {
        printf("[rewraithed] SR2 intro: cannot play %s (%s). The game's own plays.\n", path,
            (decoder == NULL) ? problem : "cannot read it");
        port_mpeg2_close(decoder);

        if (r.f != NULL)
        {
            fclose(r.f);
        }

        free(r.buf);
        free(rgb);
        return 0;
    }

    printf("[rewraithed] SR2 intro: %s (%lld bytes from %lld)\n", path, length, offset);

    /* the game's exclusive output (spu.c) holds the device: the movie's sound
     * plays shared; the next frame takes it back */
    port_spu_exclusive(0);

    /* a button already held (the press that started the game) is not a new press */
    PsyX_UpdateInput();
    lastPad0 = CINE_Pad(0);
    lastPad1 = CINE_Pad(1);

    for (;;)
    {
        unsigned short pad0, pad1, pressed;

        if (!port_mpeg2_picture(decoder, rgb))
        {
            /* more of the stream, or the end of it */
            if (held == NULL)
            {
                held = next_video(&r, &s, &heldSize);
            }

            if (held != NULL)
            {
                if (port_mpeg2_feed(decoder, held, heldSize))
                {
                    held = NULL;
                }

                continue;
            }

            if (!drained)
            {
                port_mpeg2_drain(decoder);
                drained = 1;
                continue;
            }

            break;
        }

        /* this picture's time from the first */
        if (shown == 0)
        {
            start = SDL_GetPerformanceCounter();
        }
        else
        {
            Uint64 due = start + (Uint64)((double)shown * (double)freq / FPS);

            for (;;)
            {
                Uint64 now = SDL_GetPerformanceCounter();
                Uint64 wait;

                if (now >= due)
                {
                    break;
                }

                wait = ((due - now) * 1000) / freq;
                SDL_Delay((wait > 2) ? (Uint32)(wait - 1) : 0);
            }
        }

        port_present_rgb_frame(rgb, WIDTH, HEIGHT);
        shown++;

        /* scripted keys and the watchdog of test runs count the pictures
         * (native/debug_capture.c) */
        port_debug_frame();
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

    printf("[rewraithed] SR2 intro: %ld pictures shown\n", shown);

    if (s.dev != 0)
    {
        SDL_CloseAudioDevice(s.dev);
    }

    port_mpeg2_close(decoder);
    fclose(r.f);
    free(r.buf);
    free(rgb);
    free(s.pending);
    free(s.mixed);

    /* black after it, as the PlayStation player leaves */
    port_present_rgb_frame(NULL, 0, 0);

    return 1;
}
