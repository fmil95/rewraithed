/*
 * Software SPU: the 24 ADPCM voices of the PlayStation sound chip, mixed to
 * 44.1 kHz stereo on an SDL audio device, plus the CD input that carries XA
 * ADPCM audio (the game's voice-overs, see VOICEXA.c).
 *
 * Covered: 512 KB sound RAM with DMA-style transfers, ADPCM blocks with loop
 * flags, pitch, fixed voice volumes, the ADSR envelope, key on/off and the
 * key status libspu reports, master and CD volume, and the reverb unit with
 * libspu's presets. Not covered: noise, pitch modulation, volume sweeps.
 *
 * The game calls in from its main thread and from the sequencer interrupt
 * (port/src/irq.c); the audio callback runs on SDL's thread. Every entry
 * point holds the audio device lock while it touches SPU state.
 *
 * Debug: REWRAITHED_WAV=<file> records the output as a WAV file;
 * REWRAITHED_SPUTRACE=1 prints SPU state every two seconds.
 */
#include "common.h"

#include <SDL.h>

#define SPU_RAM_SIZE   0x80000
#define SPU_RAM_MASK   (SPU_RAM_SIZE - 1)
#define SPU_VOICES     24
#define OUT_RATE       44100
#define OUT_SAMPLES    512

enum { ENV_OFF, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };

typedef struct
{
    unsigned int start;
    unsigned int repeat;
    unsigned int addr;          /* block being played */
    unsigned short pitch;
    short volL;
    short volR;
    unsigned short adsr1;
    unsigned short adsr2;

    int keyOn;
    int phase;
    int level;                  /* envelope, 0..0x7FFF */
    int envCounter;

    unsigned int counter;       /* position in the block, 20.12 fixed point */
    short block[28];
    short last;                 /* sample before block[0] */
    int hist1;
    int hist2;
} Voice;

static unsigned char spu_ram[SPU_RAM_SIZE];
static Voice voices[SPU_VOICES];

static SDL_AudioDeviceID spu_device;

static short spu_mvolL, spu_mvolR;
static short spu_cdvolL, spu_cdvolR;
static int spu_cdmix;
static int spu_cdreverb;
static int spu_reverbOn;
static unsigned int spu_reverbVoices;
static int spu_reverbMode;
static short spu_reverbDepthL, spu_reverbDepthR;

static unsigned int spu_transferAddr;
static SpuTransferCallbackProc spu_transferCallback;
static volatile int spu_transferPending;
static volatile Uint64 spu_transferDue;   /* when the running DMA completes */
static unsigned int spu_keyons;
static unsigned int spu_written;
extern unsigned int port_rc1_ticks;

static const int adpcm_pos[5] = { 0, 60, 115, 98, 122 };
static const int adpcm_neg[5] = { 0, 0, -52, -55, -60 };

static void spu_lock(void)
{
    if (spu_device != 0)
    {
        SDL_LockAudioDevice(spu_device);
    }
}

static void spu_unlock(void)
{
    if (spu_device != 0)
    {
        SDL_UnlockAudioDevice(spu_device);
    }
}

static int clamp16(int v)
{
    return (v < -32768) ? -32768 : ((v > 32767) ? 32767 : v);
}

/* --- voices -------------------------------------------------------------- */

static void voice_decode_block(Voice *v)
{
    const unsigned char *b = &spu_ram[v->addr & (SPU_RAM_MASK & ~15)];
    int shift = b[0] & 0xF;
    int filter = (b[0] >> 4) & 0x7;
    int i;

    if (shift > 12)
    {
        shift = 9;
    }

    if (filter > 4)
    {
        filter = 0;
    }

    /* loop start: this block is where a loop end jumps back to */
    if (b[1] & 0x4)
    {
        v->repeat = v->addr;
    }

    for (i = 0; i < 28; i++)
    {
        int nibble = (b[2 + (i >> 1)] >> ((i & 1) * 4)) & 0xF;
        int s = (short)(nibble << 12) >> shift;

        s += (v->hist1 * adpcm_pos[filter] + v->hist2 * adpcm_neg[filter] + 32) >> 6;
        s = clamp16(s);

        v->hist2 = v->hist1;
        v->hist1 = s;
        v->block[i] = (short)s;
    }
}

/* the block just played is done: follow its loop flags */
static void voice_next_block(Voice *v)
{
    unsigned char flags = spu_ram[(v->addr & SPU_RAM_MASK) + 1];

    v->last = v->block[27];

    if (flags & 0x1)
    {
        v->addr = v->repeat;

        if (!(flags & 0x2))
        {
            /* end without repeat: the voice goes silent */
            v->phase = ENV_RELEASE;
            v->level = 0;
        }
    }
    else
    {
        v->addr = (v->addr + 16) & SPU_RAM_MASK;
    }

    voice_decode_block(v);
}

static void voice_key_on(Voice *v)
{
    v->addr = v->start & ~15u;
    v->repeat = v->addr;
    v->keyOn = 1;
    v->phase = ENV_ATTACK;
    v->level = 0;
    v->envCounter = 0;
    v->counter = 0;
    v->last = 0;
    v->hist1 = 0;
    v->hist2 = 0;

    voice_decode_block(v);
}

static void voice_key_off(Voice *v)
{
    v->keyOn = 0;

    if (v->phase != ENV_OFF)
    {
        v->phase = ENV_RELEASE;
        v->envCounter = 0;
    }
}

/* One envelope step per output sample: the level moves by `step` every
 * `cycles` samples, both derived from the rate's shift and step fields. */
static void voice_envelope(Voice *v)
{
    int shift;
    int stepValue;
    int exponential;
    int decreasing;
    int cycles;
    int step;
    int rate;

    switch (v->phase)
    {
    case ENV_ATTACK:
        rate = (v->adsr1 >> 8) & 0x7F;
        shift = rate >> 2;
        stepValue = 7 - (rate & 3);
        exponential = v->adsr1 >> 15;
        decreasing = 0;
        break;
    case ENV_DECAY:
        shift = (v->adsr1 >> 4) & 0xF;
        stepValue = -8;
        exponential = 1;
        decreasing = 1;
        break;
    case ENV_SUSTAIN:
        rate = (v->adsr2 >> 6) & 0x7F;
        shift = rate >> 2;
        decreasing = (v->adsr2 >> 14) & 1;
        stepValue = decreasing ? (-8 + (rate & 3)) : (7 - (rate & 3));
        exponential = v->adsr2 >> 15;
        break;
    case ENV_RELEASE:
        shift = v->adsr2 & 0x1F;
        stepValue = -8;
        exponential = (v->adsr2 >> 5) & 1;
        decreasing = 1;
        break;
    default:
        return;
    }

    cycles = 1 << ((shift > 11) ? (shift - 11) : 0);
    step = stepValue << ((shift < 11) ? (11 - shift) : 0);

    if (exponential && !decreasing && (v->level > 0x6000))
    {
        cycles <<= 2;
    }

    if (exponential && decreasing)
    {
        step = (step * v->level) >> 15;
    }

    if (++v->envCounter >= cycles)
    {
        v->envCounter = 0;
        v->level += step;

        if (v->level > 0x7FFF)
        {
            v->level = 0x7FFF;
        }

        if (v->level < 0)
        {
            v->level = 0;
        }
    }

    switch (v->phase)
    {
    case ENV_ATTACK:
        if (v->level >= 0x7FFF)
        {
            v->phase = ENV_DECAY;
            v->envCounter = 0;
        }
        break;
    case ENV_DECAY:
        if (v->level <= (((v->adsr1 & 0xF) + 1) * 0x800))
        {
            v->phase = ENV_SUSTAIN;
            v->envCounter = 0;
        }
        break;
    case ENV_RELEASE:
        if (v->level <= 0)
        {
            v->phase = ENV_OFF;
        }
        break;
    }
}

/* 15-bit signed fixed volume; sweep mode (bit 15) plays at full volume */
static int voice_volume(short reg)
{
    if (reg & 0x8000)
    {
        return 0x3FFF;
    }

    return ((short)(reg << 1)) >> 1;
}

static void voice_render(Voice *v, int *mix, int *rmix, int frames)
{
    unsigned int step = (v->pitch > 0x4000) ? 0x4000 : v->pitch;
    int volL = voice_volume(v->volL);
    int volR = voice_volume(v->volR);
    int i;

    for (i = 0; i < frames; i++)
    {
        int index;
        int frac;
        int prev;
        int cur;
        int s;

        if (v->phase == ENV_OFF)
        {
            return;
        }

        index = v->counter >> 12;
        frac = v->counter & 0xFFF;
        prev = (index == 0) ? v->last : v->block[index - 1];
        cur = v->block[index];
        s = prev + (((cur - prev) * frac) >> 12);

        s = (s * v->level) >> 15;

        {
            int l = (s * volL) >> 14;
            int r = (s * volR) >> 14;

            mix[i * 2 + 0] += l;
            mix[i * 2 + 1] += r;

            if (rmix != NULL)
            {
                rmix[i * 2 + 0] += l;
                rmix[i * 2 + 1] += r;
            }
        }

        voice_envelope(v);

        v->counter += step;

        while ((v->counter >> 12) >= 28)
        {
            v->counter -= 28 << 12;
            voice_next_block(v);
        }
    }
}

/* --- XA (CD input) --------------------------------------------------------
 * The drive streams real-time sectors (CdlReadS); the audio sectors that
 * pass the file/channel filter go through its ADPCM decoder into the SPU.
 * The decoder reads the image itself, following the drive position
 * (port_cd_position) so audio stays in step with the disc. */

#define XA_RING        16384
#define XA_SCAN_LIMIT  64

static FILE *xa_image;
static long xa_image_sectors;

static int xa_reqGeneration;
static int xa_reqActive;
static int xa_reqLba;
static int xa_reqFilter;
static int xa_reqFile;
static int xa_reqChan;
static int xa_muted;

static int xa_generation;
static int xa_active;
static int xa_lba;
static int xa_rate;
static int xa_hist[2][2];
static short xa_ring[XA_RING][2];
static unsigned int xa_read;    /* frames, monotonic */
static unsigned int xa_write;
static unsigned int xa_frac;    /* 16.16 between xa_read and xa_read + 1 */

void port_xa_set_image(const char *path)
{
    xa_image = fopen(path, "rb");

    if (xa_image != NULL)
    {
        _fseeki64(xa_image, 0, SEEK_END);
        xa_image_sectors = (long)(_ftelli64(xa_image) / 2352);
    }
}

void port_xa_play(int lba, int useFilter, int file, int chan)
{
    spu_lock();
    xa_reqActive = 1;
    xa_reqLba = lba;
    xa_reqFilter = useFilter;
    xa_reqFile = file;
    xa_reqChan = chan;
    xa_reqGeneration++;
    spu_unlock();
}

void port_xa_stop(void)
{
    spu_lock();

    if (xa_reqActive)
    {
        xa_reqActive = 0;
        xa_reqGeneration++;
    }

    spu_unlock();
}

void port_xa_mute(int mute)
{
    xa_muted = mute;
}

static void xa_put(int l, int r)
{
    xa_ring[xa_write % XA_RING][0] = (short)l;
    xa_ring[xa_write % XA_RING][1] = (short)r;
    xa_write++;
}

static int xa_sample(int ch, int nibble, int header)
{
    int shift = header & 0xF;
    int filter = (header >> 4) & 0x3;
    int s;

    if (shift > 12)
    {
        shift = 9;
    }

    s = (short)(nibble << 12) >> shift;
    s += (xa_hist[ch][0] * adpcm_pos[filter] + xa_hist[ch][1] * adpcm_neg[filter] + 32) >> 6;
    s = clamp16(s);

    xa_hist[ch][1] = xa_hist[ch][0];
    xa_hist[ch][0] = s;

    return s;
}

/* 4-bit XA: 18 sound groups of 128 bytes, 8 sound units of 28 samples each;
 * in stereo the units alternate left and right. */
static void xa_decode_sector(const unsigned char *sector)
{
    unsigned char coding = sector[19];
    int stereo = (coding & 0x3) == 1;
    const unsigned char *data = sector + 24;
    int group;

    if (((coding >> 4) & 0x3) != 0)
    {
        return;  /* 8-bit XA: not used by the game */
    }

    xa_rate = (coding & 0xC) ? 18900 : 37800;

    for (group = 0; group < 18; group++)
    {
        const unsigned char *g = data + group * 128;
        int block;
        int i;

        for (block = 0; block < 4; block++)
        {
            int h0 = g[4 + block * 2];
            int h1 = g[4 + block * 2 + 1];

            if (stereo)
            {
                for (i = 0; i < 28; i++)
                {
                    int b = g[16 + i * 4 + block];

                    xa_put(xa_sample(0, b & 0xF, h0), xa_sample(1, b >> 4, h1));
                }
            }
            else
            {
                for (i = 0; i < 28; i++)
                {
                    int s = xa_sample(0, g[16 + i * 4 + block] & 0xF, h0);

                    xa_put(s, s);
                }

                for (i = 0; i < 28; i++)
                {
                    int s = xa_sample(0, g[16 + i * 4 + block] >> 4, h1);

                    xa_put(s, s);
                }
            }
        }
    }
}

/* Decodes until at least `frames` frames are buffered, but never past the
 * drive: like the hardware, an audio sector plays when the drive reaches
 * it (plus a few sectors of lead for timing jitter). Reading ahead on
 * demand instead would skip the gaps of the selected channel, running into
 * the next voice clip, or past a movie's end into the next file. */
#define XA_LEAD 4

static void xa_fill(unsigned int frames)
{
    unsigned char sector[2352];
    int scanned = 0;

    while (xa_active && ((xa_write - xa_read) < frames) && ((xa_write - xa_read) < XA_RING - 4032) &&
        (scanned < XA_SCAN_LIMIT) && (xa_lba < port_cd_position() + XA_LEAD))
    {
        if ((xa_image == NULL) || (xa_lba < 0) || (xa_lba >= xa_image_sectors))
        {
            xa_active = 0;
            break;
        }

        _fseeki64(xa_image, (long long)xa_lba * 2352, SEEK_SET);

        if (fread(sector, 1, sizeof(sector), xa_image) != sizeof(sector))
        {
            xa_active = 0;
            break;
        }

        xa_lba++;
        scanned++;

        /* subheader: file, channel, submode (bit 2 = audio), coding */
        if (!(sector[18] & 0x4))
        {
            continue;
        }

        if (xa_reqFilter && ((sector[16] != xa_reqFile) || (sector[17] != xa_reqChan)))
        {
            continue;
        }

        xa_decode_sector(sector);
    }
}

static void xa_render(int *mix, int *rmix, int frames)
{
    unsigned int step;
    int volL;
    int volR;
    int i;

    if (xa_generation != xa_reqGeneration)
    {
        xa_generation = xa_reqGeneration;
        xa_active = xa_reqActive;
        xa_lba = xa_reqLba;
        xa_read = xa_write = 0;
        xa_frac = 0;
        xa_rate = 37800;
        memset(xa_hist, 0, sizeof(xa_hist));
    }

    if (!xa_active && (xa_write - xa_read) < 2)
    {
        return;
    }

    step = (unsigned int)(((unsigned long long)xa_rate << 16) / OUT_RATE);

    xa_fill(((frames * step) >> 16) + 2);

    volL = spu_cdmix && !xa_muted ? spu_cdvolL : 0;
    volR = spu_cdmix && !xa_muted ? spu_cdvolR : 0;

    for (i = 0; i < frames; i++)
    {
        short *a;
        short *b;
        int l;
        int r;

        if ((xa_write - xa_read) < 2)
        {
            break;
        }

        a = xa_ring[xa_read % XA_RING];
        b = xa_ring[(xa_read + 1) % XA_RING];
        l = a[0] + (((b[0] - a[0]) * (int)(xa_frac >> 4)) >> 12);
        r = a[1] + (((b[1] - a[1]) * (int)(xa_frac >> 4)) >> 12);

        l = (l * volL) >> 15;
        r = (r * volR) >> 15;

        mix[i * 2 + 0] += l;
        mix[i * 2 + 1] += r;

        if (spu_cdreverb)
        {
            rmix[i * 2 + 0] += l;
            rmix[i * 2 + 1] += r;
        }

        xa_frac += step;
        xa_read += xa_frac >> 16;
        xa_frac &= 0xFFFF;
    }
}

/* --- reverb ----------------------------------------------------------------
 * The SPU's reverb unit: delay lines in a work area at the end of sound RAM,
 * run at 22.05 kHz on the summed output of the voices that have reverb on.
 * The algorithm and register meanings follow the hardware; the presets are
 * libspu's own tables, so SpuSetReverbModeType sets up exactly what the PSX
 * library would. */

enum
{
    dAPF1, dAPF2, vIIR, vCOMB1, vCOMB2, vCOMB3, vCOMB4, vWALL, vAPF1, vAPF2,
    mLSAME, mRSAME, mLCOMB1, mRCOMB1, mLCOMB2, mRCOMB2, dLSAME, dRSAME,
    mLDIFF, mRDIFF, mLCOMB3, mRCOMB3, mLCOMB4, mRCOMB4, dLDIFF, dRDIFF,
    mLAPF1, mRAPF1, mLAPF2, mRAPF2, vLIN, vRIN, REV_REGS
};

/* work area start per mode, in 8-byte units */
static const unsigned short rev_start[10] = {
    0xFFFE, 0xFB28, 0xFC18, 0xF6F8, 0xF204, 0xEA44, 0xE128, 0xCFF8, 0xCFF8, 0xF880
};

static const unsigned short rev_presets[10][REV_REGS] = {
    /* off */
    { 0 },
    /* room */
    { 0x007D, 0x005B, 0x6D80, 0x54B8, 0xBED0, 0x0000, 0x0000, 0xBA80, 0x5800, 0x5300, 0x04D6, 0x0333, 0x03F0, 0x0227, 0x0374, 0x01EF,
      0x0334, 0x01B5, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x01B4, 0x0136, 0x00B8, 0x005C, 0x8000, 0x8000 },
    /* studio small */
    { 0x0033, 0x0025, 0x70F0, 0x4FA8, 0xBCE0, 0x4410, 0xC0F0, 0x9C00, 0x5280, 0x4EC0, 0x03E4, 0x031B, 0x03A4, 0x02AF, 0x0372, 0x0266,
      0x031C, 0x025D, 0x025C, 0x018E, 0x022F, 0x0135, 0x01D2, 0x00B7, 0x018F, 0x00B5, 0x00B4, 0x0080, 0x004C, 0x0026, 0x8000, 0x8000 },
    /* studio medium */
    { 0x00B1, 0x007F, 0x70F0, 0x4FA8, 0xBCE0, 0x4510, 0xBEF0, 0xB4C0, 0x5280, 0x4EC0, 0x0904, 0x076B, 0x0824, 0x065F, 0x07A2, 0x0616,
      0x076C, 0x05ED, 0x05EC, 0x042E, 0x050F, 0x0305, 0x0462, 0x02B7, 0x042F, 0x0265, 0x0264, 0x01B2, 0x0100, 0x0080, 0x8000, 0x8000 },
    /* studio large */
    { 0x00E3, 0x00A9, 0x6F60, 0x4FA8, 0xBCE0, 0x4510, 0xBEF0, 0xA680, 0x5680, 0x52C0, 0x0DFB, 0x0B58, 0x0D09, 0x0A3C, 0x0BD9, 0x0973,
      0x0B59, 0x08DA, 0x08D9, 0x05E9, 0x07EC, 0x04B0, 0x06EF, 0x03D2, 0x05EA, 0x031D, 0x031C, 0x0238, 0x0154, 0x00AA, 0x8000, 0x8000 },
    /* hall */
    { 0x01A5, 0x0139, 0x6000, 0x5000, 0x4C00, 0xB800, 0xBC00, 0xC000, 0x6000, 0x5C00, 0x15BA, 0x11BB, 0x14C2, 0x10BD, 0x11BC, 0x0DC1,
      0x11C0, 0x0DC3, 0x0DC0, 0x09C1, 0x0BC4, 0x07C1, 0x0A00, 0x06CD, 0x09C2, 0x05C1, 0x05C0, 0x041A, 0x0274, 0x013A, 0x8000, 0x8000 },
    /* space */
    { 0x033D, 0x0231, 0x7E00, 0x5000, 0xB400, 0xB000, 0x4C00, 0xB000, 0x6000, 0x5400, 0x1ED6, 0x1A31, 0x1D14, 0x183B, 0x1BC2, 0x16B2,
      0x1A32, 0x15EF, 0x15EE, 0x1055, 0x1334, 0x0F2D, 0x11F6, 0x0C5D, 0x1056, 0x0AE1, 0x0AE0, 0x07A2, 0x0464, 0x0232, 0x8000, 0x8000 },
    /* echo */
    { 0x0001, 0x0001, 0x7FFF, 0x7FFF, 0x0000, 0x0000, 0x0000, 0x8100, 0x0000, 0x0000, 0x1FFF, 0x0FFF, 0x1005, 0x0005, 0x0000, 0x0000,
      0x1005, 0x0005, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x1004, 0x1002, 0x0004, 0x0002, 0x8000, 0x8000 },
    /* delay */
    { 0x0001, 0x0001, 0x7FFF, 0x7FFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x1FFF, 0x0FFF, 0x1005, 0x0005, 0x0000, 0x0000,
      0x1005, 0x0005, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x1004, 0x1002, 0x0004, 0x0002, 0x8000, 0x8000 },
    /* pipe */
    { 0x0017, 0x0013, 0x70F0, 0x4FA8, 0xBCE0, 0x4510, 0xBEF0, 0x8500, 0x5F80, 0x54C0, 0x0371, 0x02AF, 0x02E5, 0x01DF, 0x02B0, 0x01D7,
      0x0358, 0x026A, 0x01D6, 0x011E, 0x012D, 0x00B1, 0x011F, 0x0059, 0x01A0, 0x00E3, 0x0058, 0x0040, 0x0028, 0x0014, 0x8000, 0x8000 },
};

static unsigned short rev_regs[REV_REGS];
static unsigned int rev_base = 0x7FFF0;     /* work area start, bytes */
static unsigned int rev_addr = 0x7FFF0;     /* current position, bytes */
static int rev_half;                        /* first or second 44.1 kHz sample of a step */
static int rev_inL, rev_inR;
static int rev_outL, rev_outR;

/* work area access: offsets in bytes from the current position, wrapping
 * inside the work area */
static unsigned int rev_where(int offset)
{
    int size = SPU_RAM_SIZE - (int)rev_base;
    int rel = ((int)(rev_addr - rev_base) + offset) % size;

    if (rel < 0)
    {
        rel += size;
    }

    return (rev_base + rel) & ~1u;
}

static int rev_read(int offset)
{
    unsigned int a = rev_where(offset);

    return (short)(spu_ram[a] | (spu_ram[a + 1] << 8));
}

static void rev_write(int offset, int value)
{
    unsigned int a = rev_where(offset);

    value = clamp16(value);
    spu_ram[a] = (unsigned char)value;
    spu_ram[a + 1] = (unsigned char)(value >> 8);
}

#define RV(r)     ((int)(short)rev_regs[r])     /* volume / coefficient */
#define RA(r)     ((int)rev_regs[r] * 8)        /* address, bytes */
#define MUL(a, b) (((a) * (b)) >> 15)

/* one 22.05 kHz step; sets rev_outL/R */
static void rev_step(int inL, int inR)
{
    int lin = MUL(RV(vLIN), inL);
    int rin = MUL(RV(vRIN), inR);
    int prev;
    int lout;
    int rout;
    int t;

    /* same side reflection */
    prev = rev_read(RA(mLSAME) - 2);
    rev_write(RA(mLSAME), MUL(lin + MUL(rev_read(RA(dLSAME)), RV(vWALL)) - prev, RV(vIIR)) + prev);
    prev = rev_read(RA(mRSAME) - 2);
    rev_write(RA(mRSAME), MUL(rin + MUL(rev_read(RA(dRSAME)), RV(vWALL)) - prev, RV(vIIR)) + prev);

    /* different side reflection */
    prev = rev_read(RA(mLDIFF) - 2);
    rev_write(RA(mLDIFF), MUL(lin + MUL(rev_read(RA(dRDIFF)), RV(vWALL)) - prev, RV(vIIR)) + prev);
    prev = rev_read(RA(mRDIFF) - 2);
    rev_write(RA(mRDIFF), MUL(rin + MUL(rev_read(RA(dLDIFF)), RV(vWALL)) - prev, RV(vIIR)) + prev);

    /* early echo */
    lout = MUL(RV(vCOMB1), rev_read(RA(mLCOMB1))) + MUL(RV(vCOMB2), rev_read(RA(mLCOMB2))) +
        MUL(RV(vCOMB3), rev_read(RA(mLCOMB3))) + MUL(RV(vCOMB4), rev_read(RA(mLCOMB4)));
    rout = MUL(RV(vCOMB1), rev_read(RA(mRCOMB1))) + MUL(RV(vCOMB2), rev_read(RA(mRCOMB2))) +
        MUL(RV(vCOMB3), rev_read(RA(mRCOMB3))) + MUL(RV(vCOMB4), rev_read(RA(mRCOMB4)));

    /* late reverb: all-pass filters 1 and 2 */
    t = rev_read(RA(mLAPF1) - RA(dAPF1));
    lout = clamp16(lout - MUL(RV(vAPF1), t));
    rev_write(RA(mLAPF1), lout);
    lout = MUL(lout, RV(vAPF1)) + t;

    t = rev_read(RA(mRAPF1) - RA(dAPF1));
    rout = clamp16(rout - MUL(RV(vAPF1), t));
    rev_write(RA(mRAPF1), rout);
    rout = MUL(rout, RV(vAPF1)) + t;

    t = rev_read(RA(mLAPF2) - RA(dAPF2));
    lout = clamp16(lout - MUL(RV(vAPF2), t));
    rev_write(RA(mLAPF2), lout);
    lout = MUL(lout, RV(vAPF2)) + t;

    t = rev_read(RA(mRAPF2) - RA(dAPF2));
    rout = clamp16(rout - MUL(RV(vAPF2), t));
    rev_write(RA(mRAPF2), rout);
    rout = MUL(rout, RV(vAPF2)) + t;

    rev_outL = MUL(clamp16(lout), spu_reverbDepthL);
    rev_outR = MUL(clamp16(rout), spu_reverbDepthR);

    rev_addr += 2;

    if (rev_addr >= SPU_RAM_SIZE)
    {
        rev_addr = rev_base;
    }
}

/* adds the reverb output to mix; rmix is the reverb input */
static void reverb_render(int *mix, const int *rmix, int frames)
{
    int i;

    if (!spu_reverbOn || (rev_base >= SPU_RAM_SIZE - 16))
    {
        return;
    }

    for (i = 0; i < frames; i++)
    {
        rev_inL += rmix[i * 2 + 0];
        rev_inR += rmix[i * 2 + 1];

        if (rev_half)
        {
            rev_step(clamp16(rev_inL >> 1), clamp16(rev_inR >> 1));
            rev_inL = rev_inR = 0;
        }

        rev_half ^= 1;

        mix[i * 2 + 0] += rev_outL;
        mix[i * 2 + 1] += rev_outR;
    }
}

#undef RV
#undef RA
#undef MUL

/* --- output -------------------------------------------------------------- */

static FILE *spu_wav;
static unsigned int spu_wav_bytes;

static void wav_header(void)
{
    unsigned int v[11];

    v[0] = 0x46464952;            /* RIFF */
    v[1] = 36 + spu_wav_bytes;
    v[2] = 0x45564157;            /* WAVE */
    v[3] = 0x20746D66;            /* fmt  */
    v[4] = 16;
    v[5] = 1 | (2 << 16);         /* PCM, stereo */
    v[6] = OUT_RATE;
    v[7] = OUT_RATE * 4;
    v[8] = 4 | (16 << 16);
    v[9] = 0x61746164;            /* data */
    v[10] = spu_wav_bytes;

    fseek(spu_wav, 0, SEEK_SET);
    fwrite(v, sizeof(v), 1, spu_wav);
    fseek(spu_wav, 0, SEEK_END);
}

static void wav_write(const short *samples, int bytes)
{
    fwrite(samples, 1, bytes, spu_wav);
    spu_wav_bytes += bytes;

    if ((spu_wav_bytes & 0xFFFF) < (unsigned int)bytes)
    {
        wav_header();
        fflush(spu_wav);
    }
}

static void SDLCALL spu_callback(void *user, Uint8 *stream, int len)
{
    static int mix[OUT_SAMPLES * 4 * 2];
    static int rmix[OUT_SAMPLES * 4 * 2];
    short *out = (short *)stream;
    short *start = out;
    int frames = len / 4;
    int i;

    (void)user;

    while (frames > 0)
    {
        int n = (frames > OUT_SAMPLES * 4) ? OUT_SAMPLES * 4 : frames;
        int mvolL = voice_volume(spu_mvolL);
        int mvolR = voice_volume(spu_mvolR);

        memset(mix, 0, n * 2 * sizeof(int));
        memset(rmix, 0, n * 2 * sizeof(int));

        for (i = 0; i < SPU_VOICES; i++)
        {
            voice_render(&voices[i], mix, (spu_reverbVoices & (1u << i)) ? rmix : NULL, n);
        }

        xa_render(mix, rmix, n);

        reverb_render(mix, rmix, n);

        /* the main volume applies to everything: voices, CD and reverb */
        for (i = 0; i < n; i++)
        {
            mix[i * 2 + 0] = (mix[i * 2 + 0] * mvolL) >> 14;
            mix[i * 2 + 1] = (mix[i * 2 + 1] * mvolR) >> 14;
        }

        for (i = 0; i < n * 2; i++)
        {
            out[i] = (short)clamp16(mix[i]);
        }

        out += n * 2;
        frames -= n;
    }

    if (spu_wav != NULL)
    {
        wav_write(start, len);
    }

    if (getenv("REWRAITHED_SPUTRACE") != NULL)
    {
        static unsigned int calls;

        if ((++calls % 172) == 0)
        {
            int active = 0;

            for (i = 0; i < SPU_VOICES; i++)
            {
                active += voices[i].phase != ENV_OFF;
            }

            printf("[spu] written %u ticks %u keyons %u active %d mvol %d,%d cd %d vol %d,%d xa %d lba %d buffered %u reverb %d mode %d depth %d,%d\n", spu_written, port_rc1_ticks, spu_keyons, active,
                spu_mvolL, spu_mvolR, spu_cdmix, spu_cdvolL, spu_cdvolR, xa_active, xa_lba, xa_write - xa_read, spu_reverbOn, spu_reverbMode, spu_reverbDepthL, spu_reverbDepthR);
        }
    }
}

static void spu_open_device(void)
{
    SDL_AudioSpec want;
    SDL_AudioSpec have;

    if (spu_device != 0)
    {
        return;
    }

    if (!SDL_WasInit(SDL_INIT_AUDIO) && (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0))
    {
        fprintf(stderr, "[rewraithed] spu: no audio (%s)\n", SDL_GetError());
        return;
    }

    SDL_zero(want);
    want.freq = OUT_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = OUT_SAMPLES;
    want.callback = spu_callback;

    spu_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);

    if (spu_device == 0)
    {
        fprintf(stderr, "[rewraithed] spu: cannot open audio device (%s)\n", SDL_GetError());
        return;
    }

    if (getenv("REWRAITHED_WAV") != NULL)
    {
        spu_wav = fopen(getenv("REWRAITHED_WAV"), "wb");

        if (spu_wav != NULL)
        {
            wav_header();
        }
    }

    SDL_PauseAudioDevice(spu_device, 0);
}

/* --- libspu -------------------------------------------------------------- */

/* the window is hidden (focus_pause.c): no sound until it is back */
void port_spu_pause(int pause)
{
    if (spu_device != 0)
    {
        SDL_PauseAudioDevice(spu_device, pause ? 1 : 0);
    }
}

void port_SpuInit(void)
{
    spu_open_device();

    spu_lock();
    memset(voices, 0, sizeof(voices));
    spu_mvolL = spu_mvolR = 0;
    spu_cdvolL = spu_cdvolR = 0;
    spu_cdmix = 0;
    spu_transferCallback = NULL;
    spu_transferPending = 0;
    spu_unlock();
}

void port_SpuQuit(void)
{
    int i;

    spu_lock();

    for (i = 0; i < SPU_VOICES; i++)
    {
        voices[i].phase = ENV_OFF;
        voices[i].keyOn = 0;
    }

    spu_unlock();
}

void port_SpuSetKey(int on_off, unsigned int voice_bit)
{
    int i;

    spu_lock();

    for (i = 0; i < SPU_VOICES; i++)
    {
        if (voice_bit & (1u << i))
        {
            if (on_off)
            {
                voice_key_on(&voices[i]);
                spu_keyons++;
            }
            else
            {
                voice_key_off(&voices[i]);
            }
        }
    }

    spu_unlock();
}

void port_SpuGetAllKeysStatus(char *status)
{
    int i;

    spu_lock();

    for (i = 0; i < SPU_VOICES; i++)
    {
        int env = voices[i].phase != ENV_OFF;

        if (voices[i].keyOn)
        {
            status[i] = env ? SPU_ON : SPU_ON_ENV_OFF;
        }
        else
        {
            status[i] = env ? SPU_OFF_ENV_ON : SPU_OFF;
        }
    }

    spu_unlock();
}

int port_SpuSetReverb(int on_off)
{
    spu_reverbOn = on_off;

    return on_off;
}

unsigned int port_SpuSetReverbVoice(int on_off, unsigned int voice_bit)
{
    if (on_off)
    {
        spu_reverbVoices |= voice_bit & 0xFFFFFF;
    }
    else
    {
        spu_reverbVoices &= ~voice_bit;
    }

    return spu_reverbVoices;
}

/* Like libspu: loads the preset and work area for the mode and resets the
 * depth to 0; SPU_REV_MODE_CLEAR_WA (0x100) also clears the work area. */
int port_SpuSetReverbModeType(int mode)
{
    int type = mode & 0xFF;

    if (type >= 10)
    {
        return -1;
    }

    spu_lock();

    spu_reverbMode = type;
    memcpy(rev_regs, rev_presets[type], sizeof(rev_regs));
    rev_base = (unsigned int)rev_start[type] * 8;
    rev_addr = rev_base;
    rev_outL = rev_outR = 0;
    spu_reverbDepthL = spu_reverbDepthR = 0;

    if (mode & SPU_REV_MODE_CLEAR_WA)
    {
        memset(&spu_ram[rev_base], 0, SPU_RAM_SIZE - rev_base);
    }

    spu_unlock();

    return 0;
}

void port_SpuSetReverbModeDepth(short depth_left, short depth_right)
{
    spu_reverbDepthL = depth_left;
    spu_reverbDepthR = depth_right;
}

int port_SpuClearReverbWorkArea(int mode)
{
    unsigned int base;

    mode &= 0xFF;

    if (mode >= 10)
    {
        return -1;
    }

    base = (unsigned int)rev_start[mode] * 8;

    spu_lock();
    memset(&spu_ram[base], 0, SPU_RAM_SIZE - base);
    spu_unlock();

    return 0;
}

void port_SpuSetCommonAttr(SpuCommonAttr *attr)
{
    unsigned int mask = attr->mask;

    spu_lock();

    if ((mask == 0) || (mask & SPU_COMMON_MVOLL)) spu_mvolL = attr->mvol.left;
    if ((mask == 0) || (mask & SPU_COMMON_MVOLR)) spu_mvolR = attr->mvol.right;
    if ((mask == 0) || (mask & SPU_COMMON_CDVOLL)) spu_cdvolL = attr->cd.volume.left;
    if ((mask == 0) || (mask & SPU_COMMON_CDVOLR)) spu_cdvolR = attr->cd.volume.right;
    if ((mask == 0) || (mask & SPU_COMMON_CDREV)) spu_cdreverb = attr->cd.reverb;
    if ((mask == 0) || (mask & SPU_COMMON_CDMIX)) spu_cdmix = attr->cd.mix;

    spu_unlock();
}

void port_SpuGetCommonAttr(SpuCommonAttr *attr)
{
    memset(attr, 0, sizeof(*attr));

    attr->mvol.left = spu_mvolL;
    attr->mvol.right = spu_mvolR;
    attr->mvolx.left = spu_mvolL;
    attr->mvolx.right = spu_mvolR;
    attr->cd.volume.left = spu_cdvolL;
    attr->cd.volume.right = spu_cdvolR;
    attr->cd.reverb = spu_cdreverb;
    attr->cd.mix = spu_cdmix;
}

void port_SpuSetCommonMasterVolume(short mvol_left, short mvol_right)
{
    spu_mvolL = mvol_left;
    spu_mvolR = mvol_right;
}

void port_SpuSetCommonCDMix(int cd_mix)
{
    spu_cdmix = cd_mix;
}

SpuTransferCallbackProc port_SpuSetTransferCallback(SpuTransferCallbackProc func)
{
    SpuTransferCallbackProc old = spu_transferCallback;

    spu_transferCallback = func;

    return old;
}

unsigned int port_SpuSetTransferStartAddr(unsigned int addr)
{
    spu_transferAddr = addr & SPU_RAM_MASK & ~7u;

    return spu_transferAddr;
}

/* A transfer takes time on the PSX (a few MB/s through the SPU DMA), and
 * the game relies on it: after starting one it records where the next
 * chunk begins, and only then may the completion interrupt run the
 * callback that continues the load. So the copy happens at once, but the
 * transfer reports completion, and its callback comes from the interrupt
 * thread, only after that time. */
static void spu_transfer_started(unsigned int bytes)
{
    Uint64 freq = SDL_GetPerformanceFrequency();

    spu_transferDue = SDL_GetPerformanceCounter() + freq / 1000 + (freq * bytes) / 4000000;
    spu_transferPending = 1;
}

int port_SpuIsTransferCompleted(int flag)
{
    (void)flag;

    return SDL_GetPerformanceCounter() >= spu_transferDue;
}

unsigned int port_SpuWrite(unsigned char *addr, unsigned int size)
{
    unsigned int n = size;

    if (spu_transferAddr + n > SPU_RAM_SIZE)
    {
        n = SPU_RAM_SIZE - spu_transferAddr;
    }

    spu_lock();
    memcpy(&spu_ram[spu_transferAddr], addr, n);
    spu_written += n;
    spu_unlock();

    spu_transfer_started(n);

    return size;
}

unsigned int port_SpuRead(unsigned char *addr, unsigned int size)
{
    unsigned int n = size;

    if (spu_transferAddr + n > SPU_RAM_SIZE)
    {
        n = SPU_RAM_SIZE - spu_transferAddr;
    }

    spu_lock();
    memcpy(addr, &spu_ram[spu_transferAddr], n);
    spu_unlock();

    spu_transfer_started(n);

    return size;
}

/* Called in interrupt context. `force` completes the running transfer and
 * every transfer its completion callback starts in turn: the CD drive does
 * so before delivering its next sector. On the PSX a whole chain of these
 * finishes well within a sector's time, and the sound loader depends on it:
 * each callback writes the next piece of the current sector and leaves the
 * rest for the following callback, so all of it must be consumed before the
 * next sector replaces the data. */
void port_spu_poll_irq(int force)
{
    int guard;

    for (guard = 0; guard < 4096; guard++)
    {
        SpuTransferCallbackProc callback;

        if (!spu_transferPending || (!force && (SDL_GetPerformanceCounter() < spu_transferDue)))
        {
            return;
        }

        callback = spu_transferCallback;

        spu_transferPending = 0;
        spu_transferDue = 0;

        if (callback != NULL)
        {
            callback();
        }

        if (!force)
        {
            return;
        }
    }
}

void port_SpuSetVoiceVolume(int vNum, short volL, short volR)
{
    if ((unsigned int)vNum < SPU_VOICES)
    {
        voices[vNum].volL = volL;
        voices[vNum].volR = volR;
    }
}

void port_SpuSetVoicePitch(int vNum, unsigned short pitch)
{
    if ((unsigned int)vNum < SPU_VOICES)
    {
        voices[vNum].pitch = pitch;
    }
}

void port_SpuGetVoicePitch(int vNum, unsigned short *pitch)
{
    *pitch = ((unsigned int)vNum < SPU_VOICES) ? voices[vNum].pitch : 0;
}

void port_SpuSetVoiceStartAddr(int vNum, unsigned int startAddr)
{
    if ((unsigned int)vNum < SPU_VOICES)
    {
        voices[vNum].start = startAddr & SPU_RAM_MASK;
    }
}

/* libspu's ADSR modes back into the two ADSR registers */
void port_SpuSetVoiceADSRAttr(int vNum, unsigned short AR, unsigned short DR, unsigned short SR, unsigned short RR,
    unsigned short SL, int ARmode, int SRmode, int RRmode)
{
    unsigned short adsr1;
    unsigned short adsr2;

    if ((unsigned int)vNum >= SPU_VOICES)
    {
        return;
    }

    adsr1 = ((ARmode == SPU_VOICE_EXPIncN) || (ARmode == SPU_VOICE_EXPIncR) ? 0x8000 : 0) |
        ((AR & 0x7F) << 8) | ((DR & 0xF) << 4) | (SL & 0xF);

    adsr2 = ((SRmode >= SPU_VOICE_EXPIncN) ? 0x8000 : 0) |
        (((SRmode == SPU_VOICE_LINEARDecN) || (SRmode == SPU_VOICE_LINEARDecR) || (SRmode == SPU_VOICE_EXPDec)) ? 0x4000 : 0) |
        ((SR & 0x7F) << 6) | ((RRmode == SPU_VOICE_EXPDec) ? 0x20 : 0) | (RR & 0x1F);

    spu_lock();
    voices[vNum].adsr1 = adsr1;
    voices[vNum].adsr2 = adsr2;
    spu_unlock();
}
