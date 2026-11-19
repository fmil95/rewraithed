/*
 * Routes the game's low-level libcd calls to port/src/cd_drive.c.
 * CdSearchFile, CdIntToPos, CdPosToInt etc. stay with PsyCross.
 */
#ifndef REWRAITHED_CD_DRIVE_H
#define REWRAITHED_CD_DRIVE_H

#define CdControl       port_CdControl
#define CdControlF      port_CdControlF
#define CdControlB      port_CdControlB
#define CdSync          port_CdSync
#define CdGetSector     port_CdGetSector
#define CdReadyCallback port_CdReadyCallback
#define CdSyncCallback  port_CdSyncCallback
#define CdRead2         port_CdRead2

/* CD streaming library (movies, port/src/cd_drive.c) */
#define StSetRing       port_StSetRing
#define StClearRing     port_StClearRing
#define StUnSetRing     port_StUnSetRing
#define StSetStream     port_StSetStream
#define StFreeRing      port_StFreeRing
#define StGetNext       port_StGetNext
#define StSetMask       port_StSetMask
#define StCdInterrupt   port_StCdInterrupt
#define StGetBackloc    port_StGetBackloc

#include <libcd.h>

void port_cd_set_image(const char *path);

/* Delivers pending command completions and streams sectors to the ready
 * callback. Called from the game thread (see port/src/load_hooks.c). */
void port_cd_pump(void);
void port_cd_pump_complete(void);

/* A movie is streaming (StSetRing .. StUnSetRing): its frames are shown in
 * 24-bit mode by port/src/gpu_hooks.c. */
int port_movie_active(void);

/* "Preload Chunks": finish loading every unit next to the one just
 * entered before going on (port/src/load_hooks.c) */
void port_preload_wait(void);

/* sector the drive is at (streams advance in real time) */
int port_cd_position(void);

/* sectors in the disc image; files from the mods folder are served from
 * virtual sectors behind it (port/src/mod_files.c) */
int port_cd_image_sectors(void);
const unsigned char *port_mod_sector(int lba);
void port_movie_present(void);

#endif
