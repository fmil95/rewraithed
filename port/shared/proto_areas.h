/*
 * Earlier areas: the maps (stream units) of the prototypes of the game.
 *
 * The launcher's "Extract Earlier Areas" unpacks each prototype's archive
 * (.7z or .zip, named with its date) in the proto folder next to the game into
 * a folder named by that date (proto\1999-06-18), then copies every map of its
 * disc's BIGFILE.DAT to proto\<date>\areas\kain2\area\<area>\bin\<unit>.drm
 * (and .crm, .snf, .smf), and the objects those maps name, with an index of
 * them. The game then serves those
 * files in place of the disc's (port/src/mod_files.c, EarlierAreas in
 * rewraithed.ini).
 */
#ifndef REWRAITHED_PROTO_AREAS_H
#define REWRAITHED_PROTO_AREAS_H

#include <stddef.h>

#define PROTO_DATE_SIZE 16
#define PROTO_UNIT_SIZE 16

/* "<proto folder>\<date>\areas" */
void proto_areas_folder(const char *date, char *out, size_t size);

/* Unpacks the archives and extracts the maps of every prototype whose maps
 * aren't extracted yet. `progress` gets a line for each step. Returns how many
 * prototypes have their maps afterwards; `report` says what happened to each. */
typedef void (*ProtoAreasProgress)(void *ctx, const char *text);
int proto_areas_extract(ProtoAreasProgress progress, void *ctx, char *report, size_t reportSize);

/* The prototypes whose maps are extracted, oldest first: their dates. */
int proto_areas_dates(char (*dates)[PROTO_DATE_SIZE], int max);

/* A prototype's index: each map's unit name (`startable`: Raziel has a place
 * to start in it), and each file with its game path
 * (\kain2\area\...), length and checksum. `object` marks the files of the
 * objects the maps name (\kain2\object\..., \kain2\sfx\object\...), for
 * those the July build lacks. Returns 1 when the game can load its maps as
 * they are, 0 when their level format is older than the game's, -1 when there
 * is no index. Either callback may be NULL. */
typedef void (*ProtoAreasUnit)(void *ctx, const char *unit, int startable);
typedef void (*ProtoAreasFile)(void *ctx, const char *gamePath, long length, unsigned long checksum, int object);
int proto_areas_read(const char *date, ProtoAreasUnit unitCb, ProtoAreasFile fileCb, void *ctx);

#endif
