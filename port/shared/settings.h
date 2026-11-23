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

/* mouse actions, and the PSX buttons they press */
enum { MOUSE_ATTACK, MOUSE_TELEKINESIS, MOUSE_AIM, MOUSE_COUNT };

/* mouse buttons, as SDL numbers them; 0 none */
enum { MOUSE_NONE, MOUSE_LEFT, MOUSE_MIDDLE, MOUSE_RIGHT, MOUSE_X1, MOUSE_X2, MOUSE_BUTTON_COUNT };

typedef struct
{
    /* display */
    int display;            /* DISPLAY_* */
    int width;
    int height;
    int refresh;            /* fullscreen refresh rate in Hz; 0: the display's own */
    int scaleLinear;        /* movies and 2D screens: 0 nearest, 1 bilinear */
    int vsync;
    int pillarbox;          /* keep 4:3 with bars */
    int filtering;          /* textures: 0 off, 1 bilinear */
    int pgxp;               /* PGXP: sub-pixel vertex precision */
    int antialiasing;       /* MSAA samples: 0, 2, 4, 8 */

    /* game */
    char area[16];          /* start area, empty for the disc's */
    int skipIntros;         /* SKIP_* */
    int dithering;
    int preloadChunks;      /* load every neighbouring stream unit at once */
    int debugMenu;          /* Square + Circle in game */
    int logging;            /* rewraithed.log, and VRAM dumps (F12, crash) */
    int fpsLimit;           /* 0 = unlimited */
    int discordPresence;    /* Discord Rich Presence */
    int dcModels;           /* Raziel's and Kain's Dreamcast models, from the
                               Dreamcast disc in the "dreamcast" folder */
    int protoAssets;        /* assets from the April 1999 prototype's disc
                               in the "proto" folder (the Spirit Glyph icon) */
    int pcMovies;           /* the PC version's movies, from the movies
                               folder (pc_disc.c) */
    char parameters[256];   /* extra command-line options (-uncut, ...) */

    /* keyboard: SDL scancode names; each button has a second, alternate key
     * ("" for none) */
    char keys[BIND_COUNT][24];
    char keys2[BIND_COUNT][24];

    /* mouse: MOUSE_* button for each MOUSE_* action */
    int mouse[MOUSE_COUNT];

    /* joystick (an SDL game controller): on or off, and the controller
     * button for each PSX button, by SDL's names ("a", "lefttrigger",
     * "dpup", ...; "none" for none). The left stick also moves. */
    int joystick;
    char joy[BIND_COUNT][24];
} PortSettings;

extern const char *const port_bind_names[BIND_COUNT];
extern const char *const port_mouse_action_names[MOUSE_COUNT];
extern const char *const port_mouse_button_names[MOUSE_BUTTON_COUNT];

/* the controller buttons a PSX button can be given, by SDL's names */
#define JOY_BUTTON_COUNT 17
extern const char *const port_joy_button_names[JOY_BUTTON_COUNT];

/* the game's settings (port/src/pc_main.c) */
extern PortSettings port_settings;

void port_settings_defaults(PortSettings *s);

/* both return 0 on failure (missing file: defaults are kept) */
int port_settings_load(PortSettings *s, const char *path);
int port_settings_save(const PortSettings *s, const char *path);

/* "<folder of the running exe>\rewraithed.ini" */
void port_settings_path(char *out, size_t size);

#endif
