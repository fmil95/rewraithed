/*
 * The player's copy of Soul Reaver 2 for PlayStation 2, in the "sr2" folder
 * next to the game: among its extras is the first game's intro movie,
 * \PSS\SR1INTRO.PSS (MPEG-2 video of 640 x 480 at 29.97 pictures a second,
 * 48 kHz stereo sound), sharper than the PlayStation's and the PC version's
 * own. The folder may hold the disc's image (.iso) or that file alone.
 */
#ifndef REWRAITHED_SR2_DISC_H
#define REWRAITHED_SR2_DISC_H

#include <stddef.h>

/* "<folder of the running exe>\sr2" */
void sr2_folder(char *out, size_t size);

/* The intro movie: 1 and the file holding it with where in it the movie
 * starts and how long it is (the whole file when it is SR1INTRO.PSS itself),
 * or 0 and what is wrong (any buffer may be NULL). */
int sr2_intro_locate(char *path, size_t pathSize, long long *offset, long long *length, char *problem, size_t problemSize);

#endif
