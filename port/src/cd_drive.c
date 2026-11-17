/*
 * CD drive emulation for the game's interrupt-driven loader (LOAD3D.c).
 *
 * The loader starts a stream with CdControl(CdlReadN), then expects the
 * sync callback once the seek completes and a ready callback (CdlDataReady)
 * per sector, from which it pulls the sector header and payload with
 * CdGetSector. PsyCross does not emulate that path, so the game's CD calls
 * are routed here (see port/cd_drive.h, included by the prefix).
 *
 * Callbacks are delivered from the game thread in port_cd_pump(), which runs
 * whenever the game services its read queue; this replaces the CD interrupt
 * without introducing a second thread into the game code. They run as
 * interrupt handlers (port/src/irq.c), so they exclude the sound interrupt.
 *
 * Sectors come much faster than the drive's 150 per second. Code that pairs
 * a sector with an SPU transfer (the sound loader double-buffers sectors and
 * finishes each one from the transfer's completion callback) relies on the
 * transfer ending before the next sector, so a pending SPU transfer is
 * completed before each sector is delivered.
 */
#include <SDL.h>

#include "common.h"
#include "port/cd_drive.h"

#define RAW_SECTOR_SIZE 2352
#define PREGAP_SECTORS  150
#define SECTORS_PER_PUMP 64

static FILE *cd_image;
static long cd_image_sectors;

static CdlCB cd_ready_cb;
static CdlCB cd_sync_cb;

static unsigned char cd_mode;
static CdlLOC cd_setloc;
static int cd_lba;           /* next sector to read, PsyQ CdPosToInt numbering */
static int cd_reading;
static int cd_complete_pending;
static int cd_in_pump;

/* XA streaming (CdlReadS): audio sectors go to the drive's ADPCM decoder, not
 * to the ready callback. The position advances in real time at double speed
 * (150 sectors per second), timed by vblanks; the game polls it with
 * CdlGetlocL to know when a voice clip has ended. The audio itself is
 * decoded by the SPU's CD input (port/src/spu.c). */
static int cd_streaming;
static int cd_stream_start_lba;
static int cd_stream_start_vbl;
static CdlFILTER cd_filter;

static int cd_current_lba(void)
{
    if (cd_streaming)
    {
        return cd_stream_start_lba + ((VSync(-1) - cd_stream_start_vbl) * 5) / 2;
    }

    return cd_lba;
}

/* The drive position, for the SPU's ADPCM decoder (another thread): it only
 * decodes the audio sectors the drive has reached. */
int port_cd_position(void)
{
    return cd_current_lba();
}

static unsigned char cd_sector[RAW_SECTOR_SIZE];
static int cd_sector_pos;    /* read cursor into cd_sector for CdGetSector */

static unsigned char cd_result[8];

void port_cd_set_image(const char *path)
{
    cd_image = fopen(path, "rb");

    if (cd_image == NULL)
    {
        fprintf(stderr, "[rewraithed] cd: cannot open image '%s'\n", path);
        return;
    }

    _fseeki64(cd_image, 0, SEEK_END);
    cd_image_sectors = (long)(_ftelli64(cd_image) / RAW_SECTOR_SIZE);

    port_xa_set_image(path);
}

static int loc_to_lba(const CdlLOC *loc)
{
    int m = (loc->minute >> 4) * 10 + (loc->minute & 0xF);
    int s = (loc->second >> 4) * 10 + (loc->second & 0xF);
    int f = (loc->sector >> 4) * 10 + (loc->sector & 0xF);

    return ((m * 60) + s) * 75 + f - PREGAP_SECTORS;
}

static void read_raw_sector(int lba)
{
    if ((cd_image == NULL) || (lba < 0) || (lba >= cd_image_sectors))
    {
        memset(cd_sector, 0, sizeof(cd_sector));
        return;
    }

    _fseeki64(cd_image, (long long)lba * RAW_SECTOR_SIZE, SEEK_SET);
    fread(cd_sector, 1, RAW_SECTOR_SIZE, cd_image);
}

/* --- callbacks -------------------------------------------------------- */

CdlCB port_CdReadyCallback(CdlCB func)
{
    CdlCB old = cd_ready_cb;

    cd_ready_cb = func;

    return old;
}

CdlCB port_CdSyncCallback(CdlCB func)
{
    CdlCB old = cd_sync_cb;

    cd_sync_cb = func;

    return old;
}

/* --- commands --------------------------------------------------------- */

static int cd_command(unsigned char com, unsigned char *param, unsigned char *result)
{
    cd_result[0] = 0x02; /* status: motor on */

    switch (com)
    {
    case CdlSetloc:
        if (param != NULL)
        {
            memcpy(&cd_setloc, param, sizeof(cd_setloc));
        }
        break;
    case CdlSetmode:
        if (param != NULL)
        {
            cd_mode = param[0];
        }
        break;
    case CdlSetfilter:
        if (param != NULL)
        {
            memcpy(&cd_filter, param, sizeof(cd_filter));
        }
        break;
    case CdlMute:
        port_xa_mute(1);
        break;
    case CdlDemute:
        port_xa_mute(0);
        break;
    case CdlReadN:
        if (param != NULL)
        {
            memcpy(&cd_setloc, param, sizeof(cd_setloc));
        }
        cd_lba = loc_to_lba(&cd_setloc);
        cd_reading = 1;
        cd_streaming = 0;
        port_xa_stop();
        break;
    case CdlReadS:
        if (param != NULL)
        {
            memcpy(&cd_setloc, param, sizeof(cd_setloc));
        }
        cd_lba = loc_to_lba(&cd_setloc);
        cd_reading = 0;
        cd_streaming = 1;
        cd_stream_start_lba = cd_lba;
        cd_stream_start_vbl = VSync(-1);
        if (cd_mode & CdlModeRT)
        {
            port_xa_play(cd_lba, (cd_mode & CdlModeSF) != 0, cd_filter.file, cd_filter.chan);
        }
        break;
    case CdlSeekL:
    case CdlSeekP:
        cd_lba = loc_to_lba(&cd_setloc);
        cd_reading = 0;
        cd_streaming = 0;
        port_xa_stop();
        break;
    case CdlPause:
    case CdlStop:
        cd_lba = cd_current_lba();
        cd_reading = 0;
        cd_streaming = 0;
        port_xa_stop();
        break;
    case CdlGetlocL:
        /* raw header of the current sector: mm ss ff mode file chan submode coding */
        if (result != NULL)
        {
            read_raw_sector(cd_current_lba());
            memcpy(result, cd_sector + 12, 8);
        }
        break;
    case CdlGetlocP:
        /* track, index, position in the track, absolute position (BCD) */
        if (result != NULL)
        {
            CdlLOC pos;

            CdIntToPos(cd_current_lba(), &pos);

            result[0] = 0x01;
            result[1] = 0x01;
            result[2] = pos.minute;
            result[3] = pos.second;
            result[4] = pos.sector;
            result[5] = pos.minute;
            result[6] = pos.second;
            result[7] = pos.sector;
        }
        break;
    default:
        break;
    }

    if ((result != NULL) && (com != CdlGetlocL) && (com != CdlGetlocP))
    {
        memcpy(result, cd_result, sizeof(cd_result));
    }

    cd_complete_pending = 1;

    return 1;
}

int port_CdControl(unsigned char com, unsigned char *param, unsigned char *result)
{
    return cd_command(com, param, result);
}

int port_CdControlF(unsigned char com, unsigned char *param)
{
    return cd_command(com, param, NULL);
}

int port_CdControlB(unsigned char com, unsigned char *param, unsigned char *result)
{
    int ret = cd_command(com, param, result);

    port_cd_pump_complete();

    return ret;
}

int port_CdSync(int mode, unsigned char *result)
{
    (void)mode;

    port_cd_pump_complete();

    if (result != NULL)
    {
        memcpy(result, cd_result, sizeof(cd_result));
    }

    return CdlComplete;
}

/* Copies the next `words` 32-bit words of the current sector. In
 * CdlModeSize1 the sector starts at the header (12 bytes before the data),
 * otherwise at the 2048-byte payload. */
int port_CdGetSector(void *madr, int words)
{
    int bytes = words * 4;

    if (cd_sector_pos + bytes > RAW_SECTOR_SIZE)
    {
        bytes = RAW_SECTOR_SIZE - cd_sector_pos;
    }

    memcpy(madr, cd_sector + cd_sector_pos, bytes);
    cd_sector_pos += bytes;

    return 1;
}

static int cd_pumps;
static int cd_sectors_delivered;

void port_cd_debug_print(void)
{
    printf("[cd] reading %d streaming %d lba %d pending %d pumps %d sectors %d ready %p sync %p\n",
        cd_reading, cd_streaming, cd_current_lba(), cd_complete_pending, cd_pumps, cd_sectors_delivered,
        (void *)cd_ready_cb, (void *)cd_sync_cb);
}

/* --- delivery ------------------------------------------------------------ */

void port_cd_pump_complete(void)
{
    if (cd_complete_pending)
    {
        cd_complete_pending = 0;

        if (cd_sync_cb != NULL)
        {
            port_irq_handler_begin();
            cd_sync_cb(CdlComplete, cd_result);
            port_irq_handler_end();
        }
    }
}

void port_cd_pump(void)
{
    int i;

    if (cd_in_pump)
    {
        return;
    }

    cd_in_pump = 1;
    cd_pumps++;

    port_cd_pump_complete();

    for (i = 0; (i < SECTORS_PER_PUMP) && cd_reading; i++)
    {
        int lba = cd_lba;

        /* the image starts at 00:02:00, i.e. CdPosToInt sector 0 */
        read_raw_sector(lba);

        cd_lba++;
        cd_sectors_delivered++;
        cd_sector_pos = (cd_mode & CdlModeSize1) ? 12 : 24;

        if (cd_ready_cb != NULL)
        {
            port_irq_handler_begin();
            port_spu_poll_irq(1);
            cd_ready_cb(CdlDataReady, cd_result);
            port_irq_handler_end();
        }

        /* the callback may have restarted the read: let the seek complete */
        port_cd_pump_complete();
    }

    cd_in_pump = 0;
}

/* --- streaming: CdRead2 and the St* library (movies) -----------------------
 * A movie is a stream of real-time sectors: video sectors (a 32-byte StHEADER
 * and 2016 bytes of a frame's bitstream each) interleaved with XA audio. On
 * the PSX the drive delivers them at 150 sectors per second, libcd collects
 * a frame's sectors in a ring buffer and StGetNext hands out complete
 * frames, which is what paces the player. Here the sectors are read from the
 * image as the stream position (the same clock as CdlGetlocP) passes them,
 * and the audio goes to the SPU's CD input (port/src/spu.c). */

#define ST_SLOTS        4
#define ST_MAX_SECTORS  64
#define ST_PAYLOAD      2016

enum { ST_FREE, ST_ASSEMBLING, ST_READY, ST_OUT };

typedef struct
{
    int state;
    unsigned int frame;
    int sectors;
    int got;
    unsigned char header[32];
    unsigned char data[ST_MAX_SECTORS * ST_PAYLOAD];
} StFrame;

static StFrame st_frames[ST_SLOTS];
static int st_ring_set;
static int st_masked;
static int st_lba;              /* next stream sector to look at */

static void st_reset(void)
{
    int i;

    for (i = 0; i < ST_SLOTS; i++)
    {
        st_frames[i].state = ST_FREE;
    }
}

static StFrame *st_slot_for(unsigned int frame, int sectors)
{
    StFrame *free_slot = NULL;
    int i;

    for (i = 0; i < ST_SLOTS; i++)
    {
        if ((st_frames[i].state == ST_ASSEMBLING) && (st_frames[i].frame == frame))
        {
            return &st_frames[i];
        }

        if ((st_frames[i].state == ST_FREE) && (free_slot == NULL))
        {
            free_slot = &st_frames[i];
        }
    }

    if ((free_slot == NULL) || (sectors <= 0) || (sectors > ST_MAX_SECTORS))
    {
        return NULL;   /* ring full: the frame is dropped, as on hardware */
    }

    free_slot->state = ST_ASSEMBLING;
    free_slot->frame = frame;
    free_slot->sectors = sectors;
    free_slot->got = 0;

    return free_slot;
}

/* takes in the sectors the drive has passed since the last call */
static void st_scan(void)
{
    static unsigned char sector[RAW_SECTOR_SIZE];
    int now = cd_current_lba();

    while (cd_streaming && (st_lba < now))
    {
        const unsigned char *h = sector + 24;
        unsigned int frame;
        int index;
        int sectors;
        StFrame *slot;

        if ((cd_image == NULL) || (st_lba < 0) || (st_lba >= cd_image_sectors))
        {
            st_lba++;
            continue;
        }

        _fseeki64(cd_image, (long long)st_lba * RAW_SECTOR_SIZE, SEEK_SET);
        fread(sector, 1, RAW_SECTOR_SIZE, cd_image);
        st_lba++;

        /* audio sectors are the drive's ADPCM decoder's business */
        if ((sector[18] & 0x04) || (h[0] != 0x60) || (h[1] != 0x01))
        {
            continue;
        }

        index = h[4] | (h[5] << 8);
        sectors = h[6] | (h[7] << 8);
        frame = h[8] | (h[9] << 8) | (h[10] << 16) | ((unsigned int)h[11] << 24);

        slot = st_slot_for(frame, sectors);

        if ((slot == NULL) || (index >= slot->sectors))
        {
            continue;
        }

        if (index == 0)
        {
            memcpy(slot->header, h, sizeof(slot->header));
        }

        memcpy(slot->data + index * ST_PAYLOAD, h + 32, ST_PAYLOAD);

        if (++slot->got >= slot->sectors)
        {
            slot->state = ST_READY;
        }
    }
}

/* Starts streaming from the CdlSetloc position (CdlModeStream etc.). */
int port_CdRead2(int mode)
{
    cd_mode = (unsigned char)mode;
    cd_lba = loc_to_lba(&cd_setloc);
    cd_reading = 0;
    cd_streaming = 1;
    cd_stream_start_lba = cd_lba;
    cd_stream_start_vbl = VSync(-1);
    st_lba = cd_lba;

    if (mode & CdlModeRT)
    {
        port_xa_play(cd_lba, (mode & CdlModeSF) != 0, cd_filter.file, cd_filter.chan);
    }

    return 1;
}

void port_StSetRing(u_int *ring_addr, u_int ring_size)
{
    (void)ring_addr;
    (void)ring_size;

    st_reset();
    st_ring_set = 1;
    st_masked = 0;
}

void port_StClearRing(void)
{
    st_reset();
}

void port_StUnSetRing(void)
{
    st_reset();
    st_ring_set = 0;
}

void port_StSetStream(u_int mode, u_int start_frame, u_int end_frame, void (*func1)(), void (*func2)())
{
    (void)mode;
    (void)start_frame;
    (void)end_frame;
    (void)func1;
    (void)func2;

    st_masked = 0;
}

void port_StSetMask(u_int mask, u_int start, u_int end)
{
    (void)start;
    (void)end;

    st_masked = (int)mask;
}

void port_StCdInterrupt(void)
{
}

int port_StGetBackloc(CdlLOC *loc)
{
    CdIntToPos(st_lba, loc);

    return 1;
}

u_int port_StFreeRing(u_int *base)
{
    int i;

    for (i = 0; i < ST_SLOTS; i++)
    {
        if ((void *)st_frames[i].data == (void *)base)
        {
            st_frames[i].state = ST_FREE;
            return 0;
        }
    }

    return 1;
}

/* 0 and the next complete frame, or 1 if none is due. Waits (showing the
 * picture and reading the pads, as the PSX does meanwhile) for up to 200 ms
 * for the next frame, so the player's busy loops keep real time. */
u_int port_StGetNext(u_int **addr, u_int **header)
{
    Uint32 start = SDL_GetTicks();

    for (;;)
    {
        StFrame *best = NULL;
        int i;

        st_scan();

        for (i = 0; i < ST_SLOTS; i++)
        {
            if ((st_frames[i].state == ST_READY) && ((best == NULL) || (st_frames[i].frame < best->frame)))
            {
                best = &st_frames[i];
            }
        }

        if ((best != NULL) && !st_masked)
        {
            best->state = ST_OUT;
            *addr = (u_int *)best->data;
            *header = (u_int *)best->header;
            return 0;
        }

        if (!st_ring_set || !cd_streaming || st_masked || ((SDL_GetTicks() - start) > 200))
        {
            return 1;
        }

        port_movie_present();
        SDL_Delay(1);
    }
}

int port_movie_active(void)
{
    return st_ring_set;
}
