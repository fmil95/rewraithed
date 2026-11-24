/*
 * Converting the maps of the prototypes up to May 1999 to the July build's
 * level format (port/shared/proto_convert.c).
 */
#ifndef REWRAITHED_PROTO_CONVERT_H
#define REWRAITHED_PROTO_CONVERT_H

/* 1 if the maps of the prototype of that date (YYYY-MM-DD) can be converted */
int proto_convert_known(const char *date);

/* 1 if objects of that prototype load as they are (its Object layout is the
 * July one); the older ones' are left out of their maps instead */
int proto_convert_objects_load(const char *date);

/* Whether the July build has an object, for proto_convert_level. */
typedef int (*ProtoHasObject)(void *ctx, const char *name);

/* A map's .drm (with its .crm, for where its textures go) in the July
 * format: malloc'd, NULL if it can't be converted. Intros of objects that
 * `hasObject` says the July build lacks are left out when the prototype's
 * objects don't load (hasObject may be NULL: none are left out). */
unsigned char *proto_convert_level(const char *date, const unsigned char *drm, long size,
    const unsigned char *crm, long crmSize, ProtoHasObject hasObject, void *ctx, long *outSize);

#endif
