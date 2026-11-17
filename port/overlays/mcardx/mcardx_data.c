/*
 * Data the mcardx overlay takes from its copy of PsyQ's memory card library
 * (the overlay's LIBS segment), which port/src/memcard.c replaces.
 */
#include "Overlays/mcardx/mcardx.h"

/* empty save title (libmcrd rodata) */
unsigned char D_880000F0[] = " ";

/* Save slot titles, indexed by the slot letter in the save file name: the
 * overlay reads 16-byte rows from a base 'a' rows before its own "soul
 * reaver a/b/c" table (D_8800577C). */
char D_8800516C['a' + 3][16] = {
    ['a'] = "soul reaver a",
    ['b'] = "soul reaver b",
    ['c'] = "soul reaver c",
};
