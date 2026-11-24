/*
 * Mods anyone can add without code: a folder in the mods folder next to the
 * game, with a mod.ini, laid out like the mods folder itself:
 *
 *   mods\Better Underworld\mod.ini
 *   mods\Better Underworld\kain2\area\under\bin\under1.drm
 *
 * mod.ini (all optional but name):
 *
 *   name=Better Underworld
 *   author=Someone
 *   description=What it changes, shown when the pointer rests on it.
 *   requires=sr2\*.iso       files it needs from the player, next to the game
 *   missing=What to do when they aren't there.
 *
 * The launcher's Mods window lists each one after the built-in mods
 * (mod_list.h), off until turned on; [Mods] in rewraithed.ini keeps the ones
 * turned on, by folder name. The game loads those, in the window's order
 * (folder names in alphabetical order), each before the next: the first of
 * two mods replacing the same file wins. Loose files in the mods folder
 * itself (outside such folders) are always loaded, after them.
 */
#ifndef REWRAITHED_MOD_FOLDERS_H
#define REWRAITHED_MOD_FOLDERS_H

#include <stddef.h>

#define MAX_FOLDER_MODS     32
#define FOLDER_MOD_NAME     64

typedef struct
{
    char folder[FOLDER_MOD_NAME];   /* its folder's name: the key in [Mods] */
    char name[96];
    char author[64];
    char description[512];
    char requires[260];
    char missing[512];
} FolderMod;

/* "<folder of the running exe>\mods" */
void mod_folders_root(char *out, size_t size);

/* The mod folders (those with a mod.ini), sorted by folder name. */
int mod_folders_list(FolderMod *out, int max);

/* 1 if what the mod requires is there; else 0 and what to do in `problem`. */
int mod_folders_available(const FolderMod *mod, char *problem, size_t problemSize);

#endif
