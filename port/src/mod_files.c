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
    BigFileEntry entry;
} ModFile;

static ModFile mod_files[MAX_MOD_FILES];
static int mod_count;
static int mod_next_lba;
static long bigfile_base_lba;

static void add_mod_file(const char *game_path, const char *disk_path, void *ctx)
{
    ModFile *m;
    FILE *f;
    long size;
    unsigned long sum;
    long i;

    (void)ctx;

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

        if (mod_next_lba >= (100 * 60 * 75) - 150)
        {
            fprintf(stderr, "[rewraithed] mods: too much data for the CD address range\n");
            mod_count = 0;
        }
    }

    LOAD_InitCdLoader_psx(bigFileName, voiceFileName);
}

BigFileEntry *LOAD_GetBigFileEntryByHash(long hash)
{
    int i;

    for (i = 0; i < mod_count; i++)
    {
        if (mod_files[i].entry.fileHash == hash)
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
