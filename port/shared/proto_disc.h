/*
 * The April 14, 1999 prototype's disc (a CD image: .bin, .img, or a .cue
 * naming one) in the "proto" folder next to the executables, for assets the
 * July build lacks (the Spirit Glyph's icon, port/src/mod_files.c). The
 * launcher only checks that it is there.
 */
#ifndef REWRAITHED_PROTO_DISC_H
#define REWRAITHED_PROTO_DISC_H

#include <stddef.h>

/* "<folder of the running exe>\proto" */
void proto_disc_folder(char *out, size_t size);

/* LOAD_HashName: the bigfile hash of a path like \kain2\object\x\x.crm */
long proto_hash_name(const char *path);

/* The April disc in the proto folder: 1 and its image's path, or 0 and what
 * is wrong (both buffers may be NULL). It is the April disc when its
 * BIGFILE.DAT has the Spirit Glyph's icon sheet. */
int proto_disc_locate(char *image, size_t imageSize, char *problem, size_t problemSize);

/* A file of an image's BIGFILE.DAT by its game path: malloc'd, NULL if
 * absent or unreadable. */
unsigned char *proto_disc_read(const char *image, const char *gamePath, long *size);

#endif
