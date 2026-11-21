/*
 * The Dreamcast disc (a GDI dump: a .gdi and its track files) in the
 * "dreamcast" folder next to the executables, for the Dreamcast models
 * (port/src/dc_models.c). The launcher only checks that it is there.
 */
#ifndef REWRAITHED_DC_DISC_H
#define REWRAITHED_DC_DISC_H

#include <stddef.h>

typedef struct DcDisc DcDisc;

/* "<folder of the running exe>\dreamcast" */
void dc_disc_folder(char *out, size_t size);

/* The .gdi in the dreamcast folder, with all its track files there: 1 and
 * its path, or 0 and what is missing (both buffers may be NULL). */
int dc_disc_locate(char *gdi, size_t gdiSize, char *problem, size_t problemSize);

/* NULL if the disc cannot be read, or has no BIGFILE.DAT or TEXTURES.VQ */
DcDisc *dc_disc_open(const char *gdi);
void dc_disc_close(DcDisc *disc);

/* a file of the disc's BIGFILE.DAT by its hash: malloc'd, NULL if absent */
unsigned char *dc_disc_read_bigfile(DcDisc *disc, unsigned long hash, long *size);

/* bytes of TEXTURES.VQ; 0 if past its end or unreadable */
int dc_disc_read_textures(DcDisc *disc, long offset, long length, unsigned char *out);

#endif
