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

/* The April disc in the proto folder or one of its folders: 1 and its image's path, or 0 and what
 * is wrong (both buffers may be NULL). It is the April disc when its
 * BIGFILE.DAT has the Spirit Glyph's icon sheet. */
int proto_disc_locate(char *image, size_t imageSize, char *problem, size_t problemSize);

/* A file of an image's BIGFILE.DAT by its game path: malloc'd, NULL if
 * absent or unreadable. */
unsigned char *proto_disc_read(const char *image, const char *gamePath, long *size);

/* The first disc image in `folder` that has a BIGFILE.DAT: 1 and its path. */
int proto_disc_image_in(const char *folder, char *image, size_t imageSize);

/* A prototype's BIGFILE.DAT, kept open for many reads. The builds up to
 * February 1999 have a flat file table and hash paths with their backslashes;
 * the later ones have one table per stream unit, as the July build. */
typedef struct ProtoBigFile ProtoBigFile;

ProtoBigFile *proto_bigfile_open(const char *image);
void proto_bigfile_close(ProtoBigFile *bf);
int proto_bigfile_early(const ProtoBigFile *bf);
int proto_bigfile_has(const ProtoBigFile *bf, const char *gamePath);
unsigned char *proto_bigfile_read(ProtoBigFile *bf, const char *gamePath, long *size);

#endif
