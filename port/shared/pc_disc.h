/*
 * The PC version's movies (pc_disc.c): .rpl files in the "movies" folder
 * next to the game, or in an image of the PC CD there.
 */
#ifndef REWRAITHED_PC_DISC_H
#define REWRAITHED_PC_DISC_H

#include <stddef.h>

/* the folder they are looked for in */
void pc_disc_folder(char *out, size_t size);

/* 1 if the movies can be found; otherwise 0 and why, in problem */
int pc_disc_locate(char *problem, size_t problemSize);

/* the whole of a movie file, e.g. "intro.rpl" (malloc'd), or NULL */
unsigned char *pc_disc_read_movie(const char *name, long *size);

#endif
