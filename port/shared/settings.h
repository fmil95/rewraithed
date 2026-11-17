/*
 * Settings shared by the game and the launcher: rewraithed.ini, next to the
 * executables. The launcher edits it; the game reads it at startup (its
 * command-line options still override).
 */
#ifndef REWRAITHED_SETTINGS_H
#define REWRAITHED_SETTINGS_H

#include <stddef.h>

enum { DISPLAY_WINDOWED, DISPLAY_FULLSCREEN, DISPLAY_BORDERLESS };
enum { SKIP_NONE, SKIP_TO_MENU, SKIP_TO_GAME };

/* PSX pad buttons, in the order of the keyboard bindings */
enum
{
    BIND_UP, BIND_DOWN, BIND_LEFT, BIND_RIGHT,
    BIND_CROSS, BIND_CIRCLE, BIND_SQUARE, BIND_TRIANGLE,
    BIND_L1, BIND_L2, BIND_R1, BIND_R2,
    BIND_SELECT, BIND_START,
    BIND_COUNT
};

typedef struct
{
    /* display */
    int display;            /* DISPLAY_* */
    int width;
    int height;
    int scaleLinear;        /* movies and 2D screens: 0 nearest, 1 bilinear */
    int vsync;
    int pillarbox;          /* keep 4:3 with bars */
    int filtering;          /* textures: 0 off, 1 bilinear */
    int antialiasing;       /* MSAA samples: 0, 2, 4, 8 */

    /* game */
    char area[16];          /* start area, empty for the disc's */
    int skipIntros;         /* SKIP_* */
    int dithering;
    int preloadChunks;      /* load every neighbouring stream unit at once */
    int debugMenu;          /* Square + Circle in game */
    int logging;            /* rewraithed.log */
    int fpsLimit;           /* 0 = unlimited */

    /* keyboard: SDL scancode names */
    char keys[BIND_COUNT][24];
} PortSettings;

extern const char *const port_bind_names[BIND_COUNT];

/* the game's settings (port/src/pc_main.c) */
extern PortSettings port_settings;

void port_settings_defaults(PortSettings *s);

/* both return 0 on failure (missing file: defaults are kept) */
int port_settings_load(PortSettings *s, const char *path);
int port_settings_save(const PortSettings *s, const char *path);

/* "<folder of the running exe>\rewraithed.ini" */
void port_settings_path(char *out, size_t size);

#endif
