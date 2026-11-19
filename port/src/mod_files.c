/*
 * Loose files that replace files in BIGFILE.DAT (wrappers for LOAD3D.c,
 * renamed there to <name>_psx through port/overrides.txt).
 *
 * Every file under <exe folder>\mods is keyed by LOAD_HashName of its game
 * path (mods\kain2\area\under\bin\under1.drm -> \kain2\area\under\bin\under1.drm).
 * A lookup of that hash returns a bigfile entry with the file's own size and
 * checksum and a position past the end of the disc; the emulated CD drive
 * (port/src/cd_drive.c) serves those sectors from the file. The game's
 * loader, checksum test and relocation run unchanged, and the disc image is
 * never modified.
 */
#include "common.h"
#include "Game/LOAD3D.h"
#include "port/cd_drive.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_MOD_FILES 256
#define SECTOR_DATA   2048

typedef void (*port_mod_file_cb)(const char *game_path, const char *disk_path, void *ctx);
void port_scan_mods_folder(port_mod_file_cb cb, void *ctx);  /* port/src/native */

void LOAD_InitCdLoader_psx(char *bigFileName, char *voiceFileName);
BigFileEntry *LOAD_GetBigFileEntryByHash_psx(long hash);

typedef struct ModFile
{
    char path[128];         /* game path, for messages */
    unsigned char *data;    /* padded to whole sectors */
    long length;            /* file length rounded up to 4 bytes */
    int first_lba;          /* virtual sectors [first_lba, first_lba + sectors) */
    int sectors;
    int reported;
    int is_dir;             /* a virtual bigfile directory, not a file */
    BigFileEntry entry;
} ModFile;

static ModFile mod_files[MAX_MOD_FILES];
static int mod_count;
static int mod_next_lba;
static long bigfile_base_lba;

/* New units, from units.txt in the mods folder: lines "<new unit> = <unit>",
 * the second being an existing unit whose bigfile directory the new one
 * copies (its object and sound files). */
#define MAX_NEW_UNITS 32
static char new_unit_name[MAX_NEW_UNITS][16];
static char new_unit_source[MAX_NEW_UNITS][16];
static int new_unit_count;

/* The bigfile's directory table, with entries for the new units added */
static unsigned char *patched_header;
static int patched_header_sectors;

static void read_units_txt(const char *disk_path)
{
    FILE *f = fopen(disk_path, "r");
    char line[256];

    if (f == NULL)
    {
        return;
    }

    while (fgets(line, sizeof(line), f) != NULL)
    {
        char a[64], b[64];

        if ((line[0] == '#') || (sscanf(line, " %63[^= \t] = %63s", a, b) != 2))
        {
            continue;
        }

        if (new_unit_count < MAX_NEW_UNITS)
        {
            snprintf(new_unit_name[new_unit_count], 16, "%s", a);
            snprintf(new_unit_source[new_unit_count], 16, "%s", b);
            new_unit_count++;
        }
    }

    fclose(f);
}

static void add_mod_file(const char *game_path, const char *disk_path, void *ctx)
{
    ModFile *m;
    FILE *f;
    long size;
    unsigned long sum;
    long i;

    (void)ctx;

    if (_stricmp(game_path, "\\units.txt") == 0)
    {
        read_units_txt(disk_path);
        return;
    }

    if (mod_count >= MAX_MOD_FILES)
    {
        fprintf(stderr, "[rewraithed] mods: more than %d files, ignoring %s\n", MAX_MOD_FILES, disk_path);
        return;
    }

    f = fopen(disk_path, "rb");

    if (f == NULL)
    {
        fprintf(stderr, "[rewraithed] mods: cannot open %s\n", disk_path);
        return;
    }

    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);

    m = &mod_files[mod_count];
    memset(m, 0, sizeof(*m));
    snprintf(m->path, sizeof(m->path), "%s", game_path);

    /* LOAD_UpdateCheckSum adds whole words, so the length must be too */
    m->length = (size + 3) & ~3;
    m->sectors = (m->length + SECTOR_DATA - 1) / SECTOR_DATA;
    m->data = calloc((size_t)(m->sectors > 0 ? m->sectors : 1), SECTOR_DATA);

    if ((m->data == NULL) || (fread(m->data, 1, (size_t)size, f) != (size_t)size))
    {
        fprintf(stderr, "[rewraithed] mods: cannot read %s\n", disk_path);
        fclose(f);
        free(m->data);
        return;
    }

    fclose(f);

    for (sum = 0, i = 0; i < m->length; i += 4)
    {
        sum += (unsigned long)m->data[i] | ((unsigned long)m->data[i + 1] << 8) |
               ((unsigned long)m->data[i + 2] << 16) | ((unsigned long)m->data[i + 3] << 24);
    }

    m->first_lba = mod_next_lba;
    mod_next_lba += m->sectors;

    m->entry.fileHash = LOAD_HashName(m->path);
    m->entry.fileLen = m->length;
    m->entry.checkSumFull = (long)sum;
    /* filePos is relative to BIGFILE.DAT, set once its position is known */

    printf("[rewraithed] mods: %s replaces %s (%ld bytes)\n", disk_path, m->path, size);

    mod_count++;
}

/* `bytes` bytes of BIGFILE.DAT from byte `offset`, straight from the disc. */
static int read_bigfile(long offset, unsigned char *out, long bytes)
{
    unsigned char sector[SECTOR_DATA];
    long done = 0;

    while (done < bytes)
    {
        long pos = offset + done;
        long skip = pos % SECTOR_DATA;
        long n = SECTOR_DATA - skip;

        if (n > bytes - done)
        {
            n = bytes - done;
        }

        if (!port_cd_read_data(bigfile_base_lba + pos / SECTOR_DATA, sector))
        {
            return 0;
        }

        memcpy(out + done, sector + skip, n);
        done += n;
    }

    return 1;
}

static long get32(const unsigned char *p)
{
    return (long)((unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) |
                  ((unsigned long)p[3] << 24));
}

static void put32(unsigned char *p, long v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

/* A new unit (in units.txt) needs a bigfile directory: LOAD_ChangeDirectoryByID
 * stops the game with "Could not read directory hash" for an unknown unit.
 * Each new unit gets a virtual directory listing the files of the unit it
 * copies (still read from the disc, so their names needn't be known) plus
 * every mods file, and the directory table (read once by LOAD_InitCdLoader)
 * gets an entry for it. */
static void build_unit_directories(void)
{
    unsigned char head[8];
    long num_dirs, root_offset, i, k;

    if (new_unit_count == 0 || !read_bigfile(0, head, 8))
    {
        return;
    }

    num_dirs = get32(head);

    {
        unsigned char first[12];

        if (!read_bigfile(4, first, 8))
        {
            return;
        }

        root_offset = get32(first + 4);       /* subDirList[0].subDirOffset */
    }

    patched_header_sectors = (int)((root_offset + SECTOR_DATA - 1) / SECTOR_DATA);
    patched_header = calloc((size_t)patched_header_sectors, SECTOR_DATA);

    if ((patched_header == NULL) || !read_bigfile(0, patched_header, root_offset))
    {
        free(patched_header);
        patched_header = NULL;
        return;
    }

    for (k = 0; k < new_unit_count; k++)
    {
        short id = (short)LOAD_HashUnit(new_unit_name[k]);
        short src = (short)LOAD_HashUnit(new_unit_source[k]);
        long src_offset = -1, src_files = 0, n, total;
        unsigned char *dir;
        ModFile *m;

        for (i = 0; i < num_dirs; i++)
        {
            const unsigned char *e = patched_header + 4 + i * 8;
            short eid = (short)(e[0] | (e[1] << 8));

            if (eid == id)
            {
                printf("[rewraithed] mods: unit %s already has a bigfile directory\n", new_unit_name[k]);
                src_offset = -2;
                break;
            }

            if (eid == src)
            {
                src_files = (short)(e[2] | (e[3] << 8));
                src_offset = get32(e + 4);
            }
        }

        if (src_offset == -2)
        {
            continue;
        }

        if (src_offset < 0)
        {
            fprintf(stderr, "[rewraithed] mods: units.txt: no bigfile directory for unit %s\n", new_unit_source[k]);
            continue;
        }

        if ((4 + (num_dirs + 1) * 8 > root_offset) || (mod_count >= MAX_MOD_FILES))
        {
            fprintf(stderr, "[rewraithed] mods: no room for another directory (%s)\n", new_unit_name[k]);
            break;
        }

        n = 0;
        for (i = 0; i < mod_count; i++)
        {
            n += !mod_files[i].is_dir;
        }

        total = 4 + (src_files + n) * 16;
        m = &mod_files[mod_count];
        memset(m, 0, sizeof(*m));
        m->sectors = (int)((total + SECTOR_DATA - 1) / SECTOR_DATA);
        dir = calloc((size_t)m->sectors, SECTOR_DATA);

        if ((dir == NULL) || !read_bigfile(src_offset, dir, 4 + src_files * 16))
        {
            free(dir);
            continue;
        }

        /* the source directory's files, then the mods files */
        put32(dir, src_files + n);
        for (i = 0, n = 0; i < mod_count; i++)
        {
            unsigned char *e;

            if (mod_files[i].is_dir)
            {
                continue;
            }

            e = dir + 4 + (src_files + n) * 16;
            put32(e, mod_files[i].entry.fileHash);
            put32(e + 4, mod_files[i].entry.fileLen);
            put32(e + 8, mod_files[i].entry.filePos);
            put32(e + 12, mod_files[i].entry.checkSumFull);
            n++;
        }

        snprintf(m->path, sizeof(m->path), "(directory of %s)", new_unit_name[k]);
        m->data = dir;
        m->length = total;
        m->is_dir = 1;
        m->first_lba = mod_next_lba;
        mod_next_lba += m->sectors;
        mod_count++;

        {
            unsigned char *e = patched_header + 4 + num_dirs * 8;

            e[0] = (unsigned char)id;
            e[1] = (unsigned char)(id >> 8);
            e[2] = (unsigned char)(src_files + n);
            e[3] = (unsigned char)((src_files + n) >> 8);
            put32(e + 4, (m->first_lba - bigfile_base_lba) * SECTOR_DATA);
        }

        num_dirs++;
        put32(patched_header, num_dirs);

        printf("[rewraithed] mods: new unit %s (directory of %s, %ld files)\n",
            new_unit_name[k], new_unit_source[k], src_files + n);
    }
}

/* For the CD drive: puts the patched directory table into the bigfile's first sectors. */
void port_mod_patch_sector(int lba, unsigned char *data)
{
    if ((patched_header != NULL) && (lba >= bigfile_base_lba) && (lba < bigfile_base_lba + patched_header_sectors))
    {
        long part = lba - bigfile_base_lba;
        long bytes = SECTOR_DATA;

        memcpy(data, patched_header + part * SECTOR_DATA, bytes);
    }
}

/* Finds BIGFILE.DAT the same way the loader does, then the mods. */
void LOAD_InitCdLoader(char *bigFileName, char *voiceFileName)
{
    CdlFILE fp;
    int i;

    if (CdSearchFile(&fp, bigFileName) != NULL)
    {
        bigfile_base_lba = CdPosToInt(&fp.pos);

        /* virtual sectors start after the last sector of the disc */
        mod_next_lba = port_cd_image_sectors() + 1000;

        port_scan_mods_folder(add_mod_file, NULL);

        for (i = 0; i < mod_count; i++)
        {
            mod_files[i].entry.filePos = (mod_files[i].first_lba - bigfile_base_lba) * SECTOR_DATA;
        }

        build_unit_directories();

        if (mod_next_lba >= (100 * 60 * 75) - 150)
        {
            fprintf(stderr, "[rewraithed] mods: too much data for the CD address range\n");
            mod_count = 0;
            free(patched_header);
            patched_header = NULL;
        }
    }

    LOAD_InitCdLoader_psx(bigFileName, voiceFileName);
}

BigFileEntry *LOAD_GetBigFileEntryByHash(long hash)
{
    int i;

    for (i = 0; i < mod_count; i++)
    {
        if (!mod_files[i].is_dir && mod_files[i].entry.fileHash == hash)
        {
            if (!mod_files[i].reported)
            {
                mod_files[i].reported = 1;
                printf("[rewraithed] mods: loading %s from the mods folder\n", mod_files[i].path);
            }

            return &mod_files[i].entry;
        }
    }

    return LOAD_GetBigFileEntryByHash_psx(hash);
}

/* For the CD drive: the 2048 data bytes of a virtual sector, or NULL. */
const unsigned char *port_mod_sector(int lba)
{
    int i;

    for (i = 0; i < mod_count; i++)
    {
        ModFile *m = &mod_files[i];

        if ((lba >= m->first_lba) && (lba < m->first_lba + m->sectors))
        {
            return m->data + (size_t)(lba - m->first_lba) * SECTOR_DATA;
        }
    }

    return NULL;
}
