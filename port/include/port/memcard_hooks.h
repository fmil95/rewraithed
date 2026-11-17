/*
 * Routes the memory card library calls of the mcardx overlay (its own copy
 * of PsyQ's libmcrd, in the overlay's LIBS segment) to port/src/memcard.c.
 */
#ifndef REWRAITHED_MEMCARD_HOOKS_H
#define REWRAITHED_MEMCARD_HOOKS_H

/* first, so its (int-typed) prototypes keep PsyCross's names: only the
 * McErr / McFunc constants are used from it */
#include <libmcrd.h>

#define MemCardStart       port_MemCardStart
#define MemCardStop        port_MemCardStop
#define MemCardExist       port_MemCardExist
#define MemCardAccept      port_MemCardAccept
#define MemCardReadFile    port_MemCardReadFile
#define MemCardWriteFile   port_MemCardWriteFile
#define MemCardCreateFile  port_MemCardCreateFile
#define MemCardDeleteFile  port_MemCardDeleteFile
#define MemCardFormat      port_MemCardFormat
#define MemCardUnformat    port_MemCardUnformat
#define MemCardGetDirentry port_MemCardGetDirentry
#define MemCardSync        port_MemCardSync
#define _card_open         port_card_open
#define _card_close        port_card_close

struct DIRENTRY;

void port_MemCardStart(void);
void port_MemCardStop(void);
long port_MemCardExist(long chan);
long port_MemCardAccept(long chan);
long port_MemCardReadFile(long chan, char *file, unsigned long *adrs, long ofs, long bytes);
long port_MemCardWriteFile(long chan, char *file, unsigned long *adrs, long ofs, long bytes);
long port_MemCardCreateFile(long chan, char *file, long blocks);
long port_MemCardDeleteFile(long chan, char *file);
long port_MemCardFormat(long chan);
long port_MemCardUnformat(long chan);
long port_MemCardGetDirentry(long chan, char *name, struct DIRENTRY *dir, long *files, long ofs, long max);
long port_MemCardSync(long mode, long *cmds, long *result);
void port_card_open(long chan);
void port_card_close(void);

#endif
