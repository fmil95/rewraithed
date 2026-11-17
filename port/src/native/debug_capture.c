/*
 * Development aids for automated runs, driven by environment variables:
 *
 * REWRAITHED_SHOT_FRAMES  comma-separated frame numbers (e.g. "300,900");
 *                         each is saved as shot_<frame>.bmp in
 *                         REWRAITHED_SHOT_DIR (default: current directory),
 *                         read back before the frame is presented.
 * REWRAITHED_KEYS         scripted key presses, "frame:Key:duration,...",
 *                         e.g. "400:Return:10,700:C:5" (SDL key names; see
 *                         PsyCross's keyboard mapping: Return = Start,
 *                         C = Cross, Up/Down/Left/Right = d-pad).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "PsyX/common/glad.h"
#include "PsyX/PsyX_globals.h"

extern void GR_ReadVRAM(unsigned short *dst, int x, int y, int dst_w, int dst_h);

/* Whole 1024x512 VRAM as 15-bit colour (indexed textures show as noise,
 * which still shows where texture pages and CLUTs sit). */
static void save_vram(const char *path)
{
    unsigned short *raw = malloc(1024 * 512 * 2);
    unsigned char *rgb = malloc(1024 * 512 * 4);
    SDL_Surface *surface;
    int i;

    GR_ReadVRAM(raw, 0, 0, 1024, 512);

    for (i = 0; i < 1024 * 512; i++)
    {
        unsigned short c = raw[i];

        rgb[i * 4 + 0] = (unsigned char)(((c >> 10) & 31) << 3);
        rgb[i * 4 + 1] = (unsigned char)(((c >> 5) & 31) << 3);
        rgb[i * 4 + 2] = (unsigned char)((c & 31) << 3);
        rgb[i * 4 + 3] = 255;
    }

    surface = SDL_CreateRGBSurfaceFrom(rgb, 1024, 512, 32, 1024 * 4, 0x000000FF, 0x0000FF00, 0x00FF0000, 0);
    SDL_SaveBMP(surface, path);
    SDL_FreeSurface(surface);

    free(rgb);
    free(raw);
}

/* REWRAITHED_SHOT_TPAGE="px,py,cx,cy": decode the 4-bit texture page at VRAM
 * (px,py) through the CLUT at (cx,cy) into a 256x256 image. */
static void save_tpage4(const char *path, const char *spec)
{
    int px, py, cx, cy, x, y;
    unsigned short *page = malloc(64 * 256 * 2);
    unsigned short clut[16];
    unsigned char *rgb = malloc(256 * 256 * 4);
    SDL_Surface *surface;

    if (sscanf(spec, "%d,%d,%d,%d", &px, &py, &cx, &cy) != 4)
    {
        free(page);
        free(rgb);
        return;
    }

    GR_ReadVRAM(page, px, py, 64, 256);
    GR_ReadVRAM(clut, cx, cy, 16, 1);

    for (y = 0; y < 256; y++)
    {
        for (x = 0; x < 256; x++)
        {
            unsigned short c = clut[(page[y * 64 + (x >> 2)] >> ((x & 3) * 4)) & 15];
            unsigned char *p = &rgb[(y * 256 + x) * 4];

            p[0] = (unsigned char)((c & 31) << 3);
            p[1] = (unsigned char)(((c >> 5) & 31) << 3);
            p[2] = (unsigned char)(((c >> 10) & 31) << 3);
            p[3] = 255;
        }
    }

    surface = SDL_CreateRGBSurfaceFrom(rgb, 256, 256, 32, 256 * 4, 0x000000FF, 0x0000FF00, 0x00FF0000, 0);
    SDL_SaveBMP(surface, path);
    SDL_FreeSurface(surface);

    free(rgb);
    free(page);
}

static void save_framebuffer(const char *path)
{
    int w = g_windowWidth;
    int h = g_windowHeight;
    unsigned char *pixels = malloc((size_t)w * h * 4);
    SDL_Surface *surface;

    glReadPixels(0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, pixels);

    surface = SDL_CreateRGBSurfaceFrom(pixels, w, h, 32, w * 4, 0x00FF0000, 0x0000FF00, 0x000000FF, 0);
    SDL_SaveBMP(surface, path);
    SDL_FreeSurface(surface);

    free(pixels);
}

void port_watchdog_kick(void);

static int frame_count;
static int shot_frames[64];
static int shot_count = -1;

typedef struct
{
    int frame;
    int duration;
    SDL_Scancode key;
} KeyPress;

static KeyPress key_presses[64];
static int key_count;

static void parse_env(void)
{
    const char *env = getenv("REWRAITHED_SHOT_FRAMES");

    shot_count = 0;

    while ((env != NULL) && (*env != '\0') && (shot_count < 64))
    {
        shot_frames[shot_count++] = atoi(env);
        env = strchr(env, ',');

        if (env != NULL)
        {
            env++;
        }
    }

    env = getenv("REWRAITHED_KEYS");

    while ((env != NULL) && (*env != '\0') && (key_count < 64))
    {
        char name[32];
        int frame;
        int duration;

        if (sscanf(env, "%d:%31[^:]:%d", &frame, name, &duration) == 3)
        {
            key_presses[key_count].frame = frame;
            key_presses[key_count].duration = duration;
            key_presses[key_count].key = SDL_GetScancodeFromName(name);
            key_count++;
        }

        env = strchr(env, ',');

        if (env != NULL)
        {
            env++;
        }
    }
}

/* PsyCross polls SDL's keyboard state array; scripted presses write to it
 * directly (real key events overwrite it again). */
static void apply_keys(void)
{
    Uint8 *state = (Uint8 *)SDL_GetKeyboardState(NULL);
    int i;

    for (i = 0; i < key_count; i++)
    {
        KeyPress *kp = &key_presses[i];

        if (frame_count == kp->frame)
        {
            state[kp->key] = 1;
            printf("[rewraithed] key down %s at frame %d\n", SDL_GetScancodeName(kp->key), frame_count);
        }
        else if (frame_count == kp->frame + kp->duration)
        {
            state[kp->key] = 0;
        }
    }
}

void port_debug_frame(void)
{
    int i;

    if (shot_count < 0)
    {
        parse_env();

        /* REWRAITHED_NOTEX: draw untextured (vertex colours only) */
        g_dbg_texturelessMode = getenv("REWRAITHED_NOTEX") != NULL;
    }

    frame_count++;

    port_watchdog_kick();

    apply_keys();

    if ((shot_count > 0) && ((frame_count % 120) == 0))
    {
        printf("[rewraithed] frame %d at %u ms\n", frame_count, SDL_GetTicks());
    }

    for (i = 0; i < shot_count; i++)
    {
        if (shot_frames[i] == frame_count)
        {
            const char *dir = getenv("REWRAITHED_SHOT_DIR");
            char path[1024];

            SDL_snprintf(path, sizeof(path), "%s%sshot_%d.bmp", dir ? dir : "", dir ? "/" : "", frame_count);
            save_framebuffer(path);

            if (getenv("REWRAITHED_SHOT_VRAM"))
            {
                SDL_snprintf(path, sizeof(path), "%s%svram_%d.bmp", dir ? dir : "", dir ? "/" : "", frame_count);
                save_vram(path);
            }

            if (getenv("REWRAITHED_SHOT_TPAGE"))
            {
                SDL_snprintf(path, sizeof(path), "%s%svram_tpage_%d.bmp", dir ? dir : "", dir ? "/" : "", frame_count);
                save_tpage4(path, getenv("REWRAITHED_SHOT_TPAGE"));
            }

            printf("[rewraithed] captured frame %d\n", frame_count);
        }
    }
}
