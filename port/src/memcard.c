/*
 * Memory cards for the mcardx overlay (save/load menus).
 *
 * The overlay carries PsyQ's libmcrd; this is that library's interface on
 * PC, over raw 128 KB card images (the .mcd format other PSX emulators use,
 * so saves can be moved between them):
 *   -memcard1 <file>   card in port 1 (default memcard1.mcd next to the
 *                      exe; created and formatted on first use)
 *   -memcard2 <file>   card in port 2 (default memcard2.mcd next to the
 *                      exe; only present if the file exists)
 *
 * The asynchronous calls (MemCardExist, Accept, ReadFile, WriteFile) do
 * their work at once and leave the result for MemCardSync, as a request
 * that completed before the first poll. Writes go to the image file
 * immediately.
 *
 * Card layout: 16 blocks of 8 KB. Block 0 holds the header frame ("MC"),
 * 15 directory frames (one per data block) and the broken-sector list;
 * blocks 1-15 hold file data, a file being a chain of blocks.
 */
#include "common.h"

#include <libmcrd.h>

#define CARD_SIZE     0x20000
#define BLOCK_SIZE    0x2000
#define FRAME_SIZE    128
#define CARD_BLOCKS   15

#define DIR_FIRST     0x51     /* in use: first, middle, last block */
#define DIR_MIDDLE    0x52
#define DIR_LAST      0x53
#define DIR_FREE      0xA0

const char *port_arg(const char *name);   /* port/src/pc_main.c */
void port_exe_folder(char *out, size_t size);  /* port/src/native */

typedef struct
{
    char path[1024];
    int present;
    int newCard;          /* reported by MemCardExist until accepted */
    unsigned char data[CARD_SIZE];
} Card;

static Card cards[2];
static int card_started;

static int sync_pending;  /* a finished request waiting for MemCardSync */
static long sync_cmd;
static long sync_result;

/* --- card images ------------------------------------------------------- */

static unsigned char *dir_frame(Card *card, int block)
{
    return &card->data[block * FRAME_SIZE];   /* block 1..15 -> frames 1..15 */
}

static void frame_checksum(unsigned char *frame)
{
    unsigned char x = 0;
    int i;

    for (i = 0; i < FRAME_SIZE - 1; i++)
    {
        x ^= frame[i];
    }

    frame[FRAME_SIZE - 1] = x;
}

static void card_format(Card *card)
{
    int i;

    memset(card->data, 0, BLOCK_SIZE);

    card->data[0] = 'M';
    card->data[1] = 'C';
    frame_checksum(&card->data[0]);

    for (i = 1; i <= CARD_BLOCKS; i++)
    {
        unsigned char *f = dir_frame(card, i);

        f[0] = DIR_FREE;
        f[8] = 0xFF;
        f[9] = 0xFF;
        frame_checksum(f);
    }

    /* broken sector list: none */
    for (i = 16; i < 36; i++)
    {
        unsigned char *f = &card->data[i * FRAME_SIZE];

        memset(f, 0, FRAME_SIZE);
        f[0] = f[1] = f[2] = f[3] = 0xFF;
        f[8] = f[9] = 0xFF;
        frame_checksum(f);
    }

    /* the last frame of block 0 repeats the header */
    memcpy(&card->data[63 * FRAME_SIZE], &card->data[0], FRAME_SIZE);
}

static int card_formatted(Card *card)
{
    return (card->data[0] == 'M') && (card->data[1] == 'C');
}

static void card_save(Card *card)
{
    FILE *f = fopen(card->path, "wb");

    if (f == NULL)
    {
        fprintf(stderr, "[rewraithed] memcard: cannot write '%s'\n", card->path);
        return;
    }

    fwrite(card->data, 1, CARD_SIZE, f);
    fclose(f);
}

static void card_load(int port)
{
    Card *card = &cards[port];
    const char *arg = port_arg((port == 0) ? "-memcard1" : "-memcard2");
    FILE *f;

    if (arg != NULL)
    {
        snprintf(card->path, sizeof(card->path), "%s", arg);
    }
    else
    {
        char folder[1024];

        port_exe_folder(folder, sizeof(folder));
        snprintf(card->path, sizeof(card->path), "%s/memcard%d.mcd", folder, port + 1);
    }
    card->present = 0;

    f = fopen(card->path, "rb");

    if (f != NULL)
    {
        card->present = fread(card->data, 1, CARD_SIZE, f) == CARD_SIZE;
        fclose(f);

        if (!card->present)
        {
            fprintf(stderr, "[rewraithed] memcard: '%s' is not a 128 KB card image\n", card->path);
        }
    }
    else if (port == 0)
    {
        /* a fresh, formatted card in port 1 */
        card_format(card);
        card_save(card);
        card->present = 1;
        printf("[rewraithed] memcard: created '%s'\n", card->path);
    }

    card->newCard = card->present;
}

static Card *card_for(long chan)
{
    int port = (chan >> 4) & 1;

    if ((chan & 0xF) != 0)
    {
        return NULL;   /* multitap slots */
    }

    return cards[port].present ? &cards[port] : NULL;
}

/* --- directory ---------------------------------------------------------- */

static int file_find(Card *card, const char *name)
{
    int i;

    for (i = 1; i <= CARD_BLOCKS; i++)
    {
        unsigned char *f = dir_frame(card, i);

        if ((f[0] == DIR_FIRST) && (strncmp((char *)&f[10], name, 20) == 0))
        {
            return i;
        }
    }

    return 0;
}

static int file_size(Card *card, int first)
{
    unsigned char *f = dir_frame(card, first);

    return f[4] | (f[5] << 8) | (f[6] << 16) | (f[7] << 24);
}

static int block_next(Card *card, int block)
{
    unsigned char *f = dir_frame(card, block);
    int next = f[8] | (f[9] << 8);

    return (next == 0xFFFF) ? 0 : next + 1;
}

/* copies between a file and memory; returns 0 if out of the file */
static int file_io(Card *card, int first, long ofs, unsigned char *buf, long bytes, int write)
{
    int block = first;

    if ((ofs < 0) || (bytes < 0) || (ofs + bytes > file_size(card, first)))
    {
        return 0;
    }

    while (ofs >= BLOCK_SIZE)
    {
        block = block_next(card, block);
        ofs -= BLOCK_SIZE;
    }

    while (bytes > 0)
    {
        long n = BLOCK_SIZE - ofs;
        unsigned char *p;

        if (block == 0)
        {
            return 0;
        }

        if (n > bytes)
        {
            n = bytes;
        }

        p = &card->data[block * BLOCK_SIZE + ofs];

        if (write)
        {
            memcpy(p, buf, n);
        }
        else
        {
            memcpy(buf, p, n);
        }

        buf += n;
        bytes -= n;
        ofs = 0;
        block = block_next(card, block);
    }

    return 1;
}

static int name_match(const char *pattern, const char *name)
{
    for (; *pattern != '\0'; pattern++, name++)
    {
        if (*pattern == '*')
        {
            return 1;
        }

        if ((*name == '\0') || ((*pattern != '?') && (*pattern != *name)))
        {
            return 0;
        }
    }

    return *name == '\0';
}

/* --- requests ------------------------------------------------------------ */

static long request_done(long cmd, long result)
{
    if (getenv("REWRAITHED_MCTRACE") != NULL)
    {
        printf("[memcard] request %ld -> %ld%s\n", cmd, result, sync_pending ? " (refused: busy)" : "");
    }

    if (sync_pending)
    {
        return 0;   /* the previous request has not been synced */
    }

    sync_pending = 1;
    sync_cmd = cmd;
    sync_result = result;

    return 1;
}

static long card_status(long chan)
{
    Card *card = card_for(chan);

    if (card == NULL)
    {
        return McErrCardNotExist;
    }

    return card_formatted(card) ? McErrNone : McErrNotFormat;
}

/* --- libmcrd ------------------------------------------------------------- */

void port_card_open(long chan)
{
    (void)chan;
}

void port_card_close(void)
{
}

void port_MemCardStart(void)
{
    if (!card_started)
    {
        card_load(0);
        card_load(1);
    }

    card_started = 1;
    sync_pending = 0;
}

void port_MemCardStop(void)
{
    card_started = 0;
    sync_pending = 0;
}

long port_MemCardExist(long chan)
{
    Card *card = card_for(chan);

    if (card == NULL)
    {
        return request_done(McFuncExist, McErrCardNotExist);
    }

    return request_done(McFuncExist, card->newCard ? McErrNewCard : McErrNone);
}

long port_MemCardAccept(long chan)
{
    Card *card = card_for(chan);

    if (card != NULL)
    {
        card->newCard = 0;
    }

    return request_done(McFuncAccept, card_status(chan));
}

long port_MemCardReadFile(long chan, char *file, unsigned long *adrs, long ofs, long bytes)
{
    Card *card = card_for(chan);
    long status = card_status(chan);
    int first;

    if (status != McErrNone)
    {
        return request_done(McFuncReadFile, status);
    }

    first = file_find(card, file);

    if ((first == 0) || !file_io(card, first, ofs, (unsigned char *)adrs, bytes, 0))
    {
        return request_done(McFuncReadFile, McErrFileNotExist);
    }

    return request_done(McFuncReadFile, McErrNone);
}

long port_MemCardWriteFile(long chan, char *file, unsigned long *adrs, long ofs, long bytes)
{
    Card *card = card_for(chan);
    long status = card_status(chan);
    int first;

    if (status != McErrNone)
    {
        return request_done(McFuncWriteFile, status);
    }

    first = file_find(card, file);

    if ((first == 0) || !file_io(card, first, ofs, (unsigned char *)adrs, bytes, 1))
    {
        return request_done(McFuncWriteFile, McErrFileNotExist);
    }

    card_save(card);

    return request_done(McFuncWriteFile, McErrNone);
}

long port_MemCardCreateFile(long chan, char *file, long blocks)
{
    Card *card = card_for(chan);
    long status = card_status(chan);
    int chain[CARD_BLOCKS];
    int found = 0;
    int i;

    if (status != McErrNone)
    {
        return status;
    }

    if (file_find(card, file) != 0)
    {
        return McErrAlreadyExist;
    }

    for (i = 1; (i <= CARD_BLOCKS) && (found < blocks); i++)
    {
        if ((dir_frame(card, i)[0] & 0xF0) == DIR_FREE)
        {
            chain[found++] = i;
        }
    }

    if ((blocks <= 0) || (found < blocks))
    {
        return McErrBlockFull;
    }

    for (i = 0; i < blocks; i++)
    {
        unsigned char *f = dir_frame(card, chain[i]);
        int next = (i + 1 < blocks) ? chain[i + 1] - 1 : 0xFFFF;

        memset(f, 0, FRAME_SIZE);

        f[0] = (i == 0) ? DIR_FIRST : ((i + 1 < blocks) ? DIR_MIDDLE : DIR_LAST);

        if ((blocks == 1) && (i == 0))
        {
            f[0] = DIR_FIRST;
        }

        if (i == 0)
        {
            int size = blocks * BLOCK_SIZE;

            f[4] = (unsigned char)size;
            f[5] = (unsigned char)(size >> 8);
            f[6] = (unsigned char)(size >> 16);
            f[7] = (unsigned char)(size >> 24);
            strncpy((char *)&f[10], file, 20);
        }

        f[8] = (unsigned char)next;
        f[9] = (unsigned char)(next >> 8);
        frame_checksum(f);

        memset(&card->data[chain[i] * BLOCK_SIZE], 0, BLOCK_SIZE);
    }

    card_save(card);

    return McErrNone;
}

long port_MemCardDeleteFile(long chan, char *file)
{
    Card *card = card_for(chan);
    long status = card_status(chan);
    int block;

    if (status != McErrNone)
    {
        return status;
    }

    block = file_find(card, file);

    if (block == 0)
    {
        return McErrFileNotExist;
    }

    while (block != 0)
    {
        unsigned char *f = dir_frame(card, block);
        int next = block_next(card, block);

        /* deleted entries keep their chain, marked free (0xA1-0xA3) */
        f[0] = (unsigned char)(DIR_FREE | (f[0] & 0x0F));
        frame_checksum(f);
        block = next;
    }

    card_save(card);

    return McErrNone;
}

long port_MemCardFormat(long chan)
{
    Card *card = card_for(chan);

    if (card == NULL)
    {
        return McErrCardNotExist;
    }

    card_format(card);
    card_save(card);

    return McErrNone;
}

long port_MemCardUnformat(long chan)
{
    Card *card = card_for(chan);

    if (card == NULL)
    {
        return McErrCardNotExist;
    }

    memset(card->data, 0, BLOCK_SIZE);
    card_save(card);

    return McErrNone;
}

long port_MemCardGetDirentry(long chan, char *name, struct DIRENTRY *dir, long *files, long ofs, long max)
{
    Card *card = card_for(chan);
    long status = card_status(chan);
    long seen = 0;
    long count = 0;
    int i;

    *files = 0;

    if (status != McErrNone)
    {
        return status;
    }

    for (i = 1; (i <= CARD_BLOCKS) && (count < max); i++)
    {
        unsigned char *f = dir_frame(card, i);
        char fileName[21];

        if (f[0] != DIR_FIRST)
        {
            continue;
        }

        memcpy(fileName, &f[10], 20);
        fileName[20] = '\0';

        if (!name_match(name, fileName))
        {
            continue;
        }

        if (seen++ < ofs)
        {
            continue;
        }

        memset(&dir[count], 0, sizeof(dir[count]));
        memcpy(dir[count].name, fileName, sizeof(dir[count].name));
        dir[count].size = file_size(card, i);
        dir[count].head = i;
        count++;
    }

    *files = count;

    return McErrNone;
}

/* mode 0 waits, 1 polls: 1 = done (with the command and its result),
 * 0 = still running, -1 = nothing was requested */
long port_MemCardSync(long mode, long *cmds, long *result)
{
    (void)mode;

    if (!sync_pending)
    {
        return -1;
    }

    sync_pending = 0;
    *cmds = sync_cmd;
    *result = sync_result;

    return 1;
}
