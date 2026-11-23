/*
 * Frame submission and GPU/vblank timing.
 *
 * On PSX, DrawOTag starts the GPU on the ordering table and returns at once.
 * When the GPU finishes, the draw-sync interrupt runs the DrawSyncCallback
 * (the game's DrawCallback marks the frame ready), and the vblank interrupt
 * runs the VSyncCallback (the game's VblTick flips the display). The game
 * relies on that ordering, e.g. installing its draw-sync callback only after
 * calling DrawOTag.
 *
 * PsyCross renders the ordering table synchronously and leaves presenting the
 * frame (PsyX_EndScene) to the caller. So here DrawOTag renders and presents,
 * and its completion is delivered on the next vblank of the port's own vblank
 * thread (port/src/native/vblank_thread.c), just before the game's handler.
 */
#include <math.h>

#include <SDL.h>

#include "common.h"
#include "settings.h"
#include "PsyX/PsyX_public.h"
#include "Game/GAMELOOP.h"

#undef DrawOTag
#undef DrawSyncCallback
#undef VSync
#undef VSyncCallback
#undef ResetCallback
#undef LoadImage
#undef StoreImage
#undef MoveImage
#undef ClearImage
#undef DrawPrim
#undef PutDrawEnv

/* --- CPU writes into the draw area -------------------------------------------
 * On PSX the draw area is VRAM, so an image the CPU copies there (title and
 * menu backgrounds, loading screens, all via LoadImage) is simply part of the
 * picture, under whatever is drawn afterwards. PsyCross draws polygons
 * straight into the window and only copies the finished frame back into
 * VRAM, so such writes never reach the screen. Here a transfer that lands in
 * the current draw area is also drawn right away, in order with the other
 * drawing, as 16-bit textured quads over the written rectangle.
 *
 * Some screens (legal screens, loading) are shown by writing straight into
 * the displayed page and then only waiting on VSync, with no frame drawn;
 * those writes are presented from VSync (see port_present_display_writes). */

static DRAWENV port_draw_env;
static int port_have_draw_env;
static int port_display_dirty;
static int port_drawing_over_shown;

void PsyX_PortStoreShownFrame(int x, int y, int w, int h);   /* port/psycross/psyx_port.cpp */
int PsyX_PortRestoreShownFrame(void);
extern int g_port_keep_frame;

extern DRAWENV activeDrawEnv;   /* PsyCross libgpu: current GPU state */
extern DISPENV activeDispEnv;
void port_upload_vram_rect(int x, int y, int w, int h);  /* native/vram_upload.c */
extern int g_port_force_filter;                          /* port/psycross: texture filter override */
void port_debug_frame(void);                             /* native/debug_capture.c */

static int port_rect_overlap(const RECT16 *a, int x, int y, int w, int h)
{
    return (x < a->x + a->w) && (a->x < x + w) && (y < a->y + a->h) && (a->y < y + h);
}

/* Draws VRAM rectangle (x, y, w, h), clipped to env's draw area, at its
 * place in that area. */
static void port_draw_vram_rect(const DRAWENV *env, int x, int y, int w, int h)
{
    /* One tile + one quad per cell; at most 3 x 3 cells. Static, so the
     * 24-bit packet links can address it (port/memory_map.h). */
    static struct
    {
        TILE black;
        POLY_FT4 quad;
    } cells[9];
    static u_long terminator;

    const RECT16 *c = &env->clip;
    int x1, y1, col;
    int n = 0;
    unsigned short tpage;
    unsigned char dtd, dfe;

    x1 = MIN(x + w, c->x + c->w);
    y1 = MIN(y + h, c->y + c->h);
    x = MAX(x, c->x);
    y = MAX(y, c->y);

    if ((x >= x1) || (y >= y1))
    {
        return;
    }

    /* the quads sample the GPU copy of VRAM: bring that rectangle up to date */
    PsyX_BeginScene();
    port_upload_vram_rect(x, y, x1 - x, y1 - y);

    /* a textured polygon also sets the GPU's current texture page, which the
     * game's sprites rely on: put it back afterwards */
    tpage = activeDrawEnv.tpage;
    dtd = activeDrawEnv.dtd;
    dfe = activeDrawEnv.dfe;

    /* columns that stay inside one 256-texel-wide 16-bit texture page, and
     * rows inside one 256-line page */
    for (col = x; col < x1; col = (col & ~0xFF) + 256)
    {
        int cw = MIN(x1, (col & ~0xFF) + 256) - col;
        int row;

        for (row = y; (row < y1) && (n < 9); row = (row & ~0xFF) + 256)
        {
            int rh = MIN(y1, (row & ~0xFF) + 256) - row;
            int sx = col - env->ofs[0];
            int sy = row - env->ofs[1];
            TILE *black = &cells[n].black;
            POLY_FT4 *quad = &cells[n].quad;

            /* texel 0x0000 is transparent: lay down the black first */
            setTile(black);
            setRGB0(black, 0, 0, 0);
            setXY0(black, sx, sy);
            setWH(black, cw, rh);

            setPolyFT4(quad);
            setShadeTex(quad, 1);
            setXYWH(quad, sx, sy, cw, rh);
            setUVWH(quad, col & 0xFF, row & 0xFF, cw - 1, rh - 1);
            quad->tpage = getTPage(2, 0, col & ~0xFF, row & ~0xFF);
            quad->clut = 0;

            if (n > 0)
            {
                setaddr(&cells[n - 1].quad, black);
            }

            setaddr(black, quad);
            n++;
        }
    }

    /* Submitted as a packet list, not with DrawPrim: PsyCross keeps DrawPrim
     * output on top of everything drawn later (stencil), whereas this is
     * background the frame is drawn over. */
    if (n > 0)
    {
        setlen(&terminator, 0);
        termPrim(&terminator);
        setaddr(&cells[n - 1].quad, &terminator);

        /* full-screen pictures follow the "Scaling" setting */
        g_port_force_filter = port_settings.scaleLinear;
        DrawOTag((u_long *)&cells[0].black);
        g_port_force_filter = -1;
    }

    activeDrawEnv.tpage = tpage;
    activeDrawEnv.dtd = dtd;
    activeDrawEnv.dfe = dfe;
}

/* Called after every CPU transfer into VRAM. */
static void port_show_vram_write(int x, int y, int w, int h)
{
    /* movie frames are shown by port_movie_present */
    if (port_movie_active())
    {
        return;
    }

    if (port_have_draw_env)
    {
        port_draw_vram_rect(&port_draw_env, x, y, w, h);
    }

    if (port_rect_overlap(&activeDispEnv.disp, x, y, w, h))
    {
        port_display_dirty = 1;
    }
}

/* A transfer reached the displayed page but no frame has been drawn since:
 * present that page as it is in VRAM. */
static void port_present_display_writes(void)
{
    DRAWENV env;

    if (!port_display_dirty)
    {
        return;
    }

    port_display_dirty = 0;

    memset(&env, 0, sizeof(env));
    env.clip = activeDispEnv.disp;
    env.ofs[0] = activeDispEnv.disp.x;
    env.ofs[1] = activeDispEnv.disp.y;
    env.dtd = 1;
    env.dfe = 1;    /* drawing to the displayed area (0 = off-screen in PsyCross) */

    PutDrawEnv(&env);
    port_draw_vram_rect(&env, env.clip.x, env.clip.y, env.clip.w, env.clip.h);
    port_debug_frame();
    PsyX_EndScene();

    if (port_have_draw_env)
    {
        PutDrawEnv(&port_draw_env);
    }
}

/* On PSX, setting a draw environment with isbg set fills its clip area with
 * (r0, g0, b0); PsyCross leaves that out. */
DRAWENV *port_PutDrawEnv(DRAWENV *env)
{
    PutDrawEnv(env);

    port_draw_env = *env;
    port_have_draw_env = 1;

    if (env->isbg)
    {
        ClearImage(&env->clip, env->r0, env->g0, env->b0);
    }

    return env;
}

static void (*volatile port_drawsync_cb)(void);
static void (*volatile port_vsync_cb)(void);
static volatile int port_draw_done_pending;

void port_start_vblank_thread(void (*handler)(void));

/* Runs on the vblank thread, like an interrupt handler. */
static void port_vblank(void)
{
    void (*vsync)(void);

    if (port_draw_done_pending)
    {
        void (*cb)(void) = port_drawsync_cb;

        port_draw_done_pending = 0;

        if (cb != NULL)
        {
            cb();
        }
    }

    vsync = port_vsync_cb;

    if (vsync != NULL)
    {
        vsync();
    }
}

void port_gpu_init(void)
{
    port_start_vblank_thread(port_vblank);
}

int port_DrawSyncCallback(void (*func)(void))
{
    void (*old)(void) = port_drawsync_cb;

    port_drawsync_cb = func;

    return (int)(intptr_t)old;
}

int port_VSyncCallback(void (*func)(void))
{
    void (*old)(void) = port_vsync_cb;

    port_vsync_cb = func;

    return (int)(intptr_t)old;
}

/* PsyQ ResetCallback clears the game's interrupt callbacks. */
int port_ResetCallback(void)
{
    port_vsync_cb = NULL;
    port_drawsync_cb = NULL;

    return 0;
}

/* The PSX reads the pads (and services the window) during vblank; PsyCross
 * leaves the polling to the caller, which must be the main thread. */
int port_VSync(int mode)
{
    if (mode >= 0)
    {
        if (port_movie_active())
        {
            port_movie_present();
        }
        else
        {
            port_present_display_writes();
            PsyX_UpdateInput();
        }
    }

    return VSync(mode);
}

/* --- movies -------------------------------------------------------------------
 * The movie player decodes frames into VRAM and flips between them from its
 * vblank handler, in the GPU's 24-bit display mode, and draws nothing. While
 * it streams (port_movie_active), the displayed area is presented from here
 * once per vblank, from the main thread: by the stream library while the
 * player waits for frames, and by VSync. */

void port_present_rgb24(int x, int y, int w, int h);   /* native/rgb24_present.c */
void port_swap_window(void);

void port_movie_present(void)
{
    static int last_vbl = -1;
    int vbl = VSync(-1);

    if (vbl == last_vbl)
    {
        return;
    }

    last_vbl = vbl;

    if (activeDispEnv.isrgb24)
    {
        port_present_rgb24(activeDispEnv.disp.x, activeDispEnv.disp.y, activeDispEnv.disp.w, activeDispEnv.disp.h);
    }
    else
    {
        port_present_rgb24(0, 0, 0, 0);
    }

    port_debug_frame();
    port_swap_window();
    PsyX_UpdateInput();
}

int port_LoadImage(RECT16 *rect, u_long *p)
{
    int ret = LoadImage(rect, p);

    port_show_vram_write(rect->x, rect->y, rect->w, rect->h);
    port_draw_done_pending = 1;

    return ret;
}

int port_StoreImage(RECT16 *rect, u_long *p)
{
    int ret = StoreImage(rect, p);

    port_draw_done_pending = 1;

    return ret;
}

int port_MoveImage(RECT16 *rect, int x, int y)
{
    int ret = MoveImage(rect, x, y);

    port_show_vram_write(x, y, rect->w, rect->h);
    port_draw_done_pending = 1;

    return ret;
}

int port_ClearImage(RECT16 *rect, u_char r, u_char g, u_char b)
{
    int ret = ClearImage(rect, r, g, b);

    port_show_vram_write(rect->x, rect->y, rect->w, rect->h);
    port_draw_done_pending = 1;

    return ret;
}

void port_DrawPrim(void *p)
{
    DrawPrim(p);

    port_draw_done_pending = 1;
}

/* Debug: REWRAITHED_OTCHECK=1 logs packets whose first command byte is not a
 * drawing command PsyCross understands, before PsyCross parses them. */
static void port_check_ot(u_long *p)
{
    static int frames;
    static int hist[256][32];
    unsigned int *pkt = (unsigned int *)p;
    int guard = 0;

    while ((pkt != NULL) && (guard++ < 200000))
    {
        unsigned int tag = pkt[0];
        int len = tag >> 24;

        if (len > 0)
        {
            hist[pkt[1] >> 24][len & 31]++;

            /* textured triangles on the right of the screen: tpage/clut */
            if (((pkt[1] >> 24) & 0xFC) == 0x34 && len == 9 && (short)pkt[2] > 380 && (frames % 60) == 59)
            {
                static unsigned int seen[64];
                unsigned int key = (pkt[6] >> 16) | (pkt[3] & 0xFFFF0000);
                int k;

                for (k = 0; k < 64 && seen[k] != 0 && seen[k] != key; k++)
                {
                }

                if (k < 64 && seen[k] == 0)
                {
                    unsigned int tp = pkt[6] >> 16;
                    unsigned int clut = pkt[3] >> 16;

                    seen[k] = key;
                    printf("[ot] GT3 tpage %04x (x %d y %d depth %d) clut %04x (x %d y %d) uv0 %04x\n", tp,
                        (tp & 0xF) * 64, ((tp >> 4) & 1) * 256 + ((tp >> 11) & 1) * 512, (tp >> 7) & 3,
                        clut, (clut & 0x3F) * 16, clut >> 6, pkt[3] & 0xFFFF);
                }
            }
        }

        if ((tag & 0xFFFFFF) == 0xFFFFFF)
        {
            break;
        }

        pkt = (unsigned int *)PORT_OT_PTR(tag & 0xFFFFFF);
    }

    if ((++frames % 60) == 0)
    {
        int c, l;

        printf("[ot] packets by first command / tag length:\n");

        for (c = 0; c < 256; c++)
        {
            for (l = 0; l < 32; l++)
            {
                if (hist[c][l] != 0)
                {
                    printf("[ot]   cmd %02x len %2d: %d\n", c, l, hist[c][l]);
                    hist[c][l] = 0;
                }
            }
        }
    }
}

/* Debug: REWRAITHED_SEGDUMP=<frame> prints the player's segment matrices at
 * that frame, with two consistency checks per segment:
 *   len   row lengths of the rotation (4096 = orthonormal at 1.0 scale)
 *   joint |position - (parent rotation * pivot + parent position)|, which is
 *         0 when the segment sits where its parent's joint says it should. */
static void port_dump_segments(void)
{
    static int frame;
    const char *env = getenv("REWRAITHED_SEGDUMP");
    Instance *inst = gameTrackerX.playerInstance;
    Model *model;
    int i;

    if ((env == NULL) || (++frame != atoi(env)) || (inst == NULL) || (inst->matrix == NULL))
    {
        return;
    }

    model = inst->object->modelList[inst->currentModel];

    printf("[seg] player model %d: %ld segments, instance pos %d,%d,%d\n", inst->currentModel,
        model->numSegments, inst->position.x, inst->position.y, inst->position.z);

    for (i = 0; i < model->numSegments; i++)
    {
        Segment *s = &model->segmentList[i];
        MATRIX *m = &inst->matrix[i];
        int r;
        double len[3];
        double err = -1;

        for (r = 0; r < 3; r++)
        {
            len[r] = sqrt((double)m->m[r][0] * m->m[r][0] + (double)m->m[r][1] * m->m[r][1] + (double)m->m[r][2] * m->m[r][2]);
        }

        double angle = -1;

        if ((s->parent >= 0) && (s->parent < model->numSegments))
        {
            MATRIX *p = &inst->matrix[s->parent];
            double e[3];
            double tr = 0;
            int c;

            for (r = 0; r < 3; r++)
            {
                e[r] = p->t[r] + ((double)p->m[r][0] * s->px + (double)p->m[r][1] * s->py + (double)p->m[r][2] * s->pz) / 4096.0 - m->t[r];
            }

            err = sqrt(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);

            /* rotation relative to the parent: trace of parent^T * child */
            for (c = 0; c < 3; c++)
            {
                for (r = 0; r < 3; r++)
                {
                    tr += (double)p->m[r][c] * m->m[r][c];
                }
            }

            tr /= 4096.0 * 4096.0;
            angle = acos(MAX(-1.0, MIN(1.0, (tr - 1.0) / 2.0))) * 180.0 / 3.14159265;
        }

        printf("[seg] %2d parent %2d pivot %5d,%5d,%5d verts %4d-%4d len %4.0f %4.0f %4.0f joint %4.0f rel %5.1f deg t %6ld,%6ld,%6ld\n",
            i, s->parent, s->px, s->py, s->pz, s->firstVertex, s->lastVertex,
            len[0], len[1], len[2], err, angle, (long)m->t[0], (long)m->t[1], (long)m->t[2]);
    }
}

/* Debug: REWRAITHED_STATETRACE=1 prints a line of game state every second. */
static void port_trace_state(void)
{
    static int n;
    Instance *player = gameTrackerX.playerInstance;
    int every = atoi(getenv("REWRAITHED_STATETRACE"));

    /* REWRAITHED_STATETRACE=<frames between lines>; 1 means every 60 */
    if (every <= 1)
    {
        every = 60;
    }

    if ((++n % every) != 0)
    {
        return;
    }

    printf("[state] t %u frame %d mode %ld flags %08lx dbg %08lx player %d,%d,%d unit %ld\n",
        SDL_GetTicks(), n, (long)gameTrackerX.gameMode, (unsigned long)gameTrackerX.gameFlags, (unsigned long)gameTrackerX.debugFlags,
        player ? player->position.x : 0, player ? player->position.y : 0, player ? player->position.z : 0,
        player ? (long)player->currentStreamUnitID : -1);

    {
        void port_cd_debug_print(void);

        port_cd_debug_print();
    }
}

void port_DrawOTag(u_long *p)
{
    if (getenv("REWRAITHED_STATETRACE"))
    {
        port_trace_state();
    }

    port_dump_segments();

    /* The PSX mirrors main RAM at 0x00000000 (KUSEG); the game sometimes
     * passes a 24-bit link value straight in as the table pointer. */
    if ((uintptr_t)p < PORT_PSX_RAM_SIZE)
    {
        p = (u_long *)PORT_OT_PTR((uintptr_t)p);
    }

    if (getenv("REWRAITHED_OTCHECK"))
    {
        port_check_ot(p);
    }

    /* a frame is being drawn: it shows those writes itself */
    port_display_dirty = 0;

    /* PsyCross's PutDispEnv keeps the display a frame behind on purpose, so
     * that normally the page drawn is where it puts the window; this effect
     * draws into the page being shown, so line the display up with it (the
     * game's next PutDispEnv sets it again) */
    if (port_drawing_over_shown && port_have_draw_env)
    {
        activeDispEnv.disp.x = port_draw_env.clip.x;
        activeDispEnv.disp.y = port_draw_env.clip.y;
    }

    if (port_drawing_over_shown && !PsyX_PortRestoreShownFrame() && port_have_draw_env)
    {
        /* multisampled window: through VRAM, at PSX resolution, into the
         * area being drawn (the effect draws into the displayed page) */
        RECT16 *d = &port_draw_env.clip;

        PsyX_PortStoreShownFrame(d->x, d->y, d->w, d->h);
        port_draw_vram_rect(&port_draw_env, d->x, d->y, d->w, d->h);
    }

    DrawOTag(p);

    port_debug_frame();

    g_port_keep_frame = 1;
    PsyX_EndScene();
    g_port_keep_frame = 0;

    /* the in-game loop presents frames without calling VSync */
    PsyX_UpdateInput();

    port_draw_done_pending = 1;
}

/* --- drawing over the picture being shown -------------------------------------
 * The spectral death effect (UNDRWRLD.c) copies the displayed page to the
 * other one, then keeps drawing a rotating, shrinking copy of it over the
 * displayed page, without clearing, while the underworld loads. The port
 * writes the last frame shown into VRAM before that copy, and meanwhile
 * starts each frame from the last one shown instead of an empty window
 * (port/psycross/psyx_port.cpp). Called from fixups in UNDRWRLD.c. */
void port_store_shown_frame(int x, int y, int w, int h)
{
    PsyX_PortStoreShownFrame(x, y, w, h);
}

static unsigned long port_draw_over_shown_start;

void port_draw_over_shown(int on)
{
    port_drawing_over_shown = on;

    if (on)
    {
        port_draw_over_shown_start = gameTrackerX.vblCount;
    }
}

/* Loading from the PC is much quicker than from the PSX's CD, and the
 * effect lasts as long as the load: hold it for at least 1.5 s. */
int port_draw_over_shown_hold(void)
{
    return port_drawing_over_shown && ((gameTrackerX.vblCount - port_draw_over_shown_start) < 90);
}

/* --- FPS limit ------------------------------------------------------------- */

/* The engine still waits on things paced at the 60 Hz vblank (draw
 * completion, the vblank counter), so it cannot go past about 60 frames a
 * second; higher limits and "unlimited" run at 60. */
static int port_fps_target(void)
{
    static int warned;
    int fps = port_settings.fpsLimit;

    if ((fps <= 0) || (fps > 60))
    {
        if (!warned)
        {
            printf("[rewraithed] FPS limit %d: running at 60, the most the engine reaches for now\n", fps);
            warned = 1;
        }

        fps = 60;
    }

    return fps;
}

/* VSync at 30 FPS. The game used to wait for its own vertical blanks (the
 * port's 59.94 Hz timer), and the swap then waited again for the display's
 * next refresh; the two drifted, and the pictures stayed up for an uneven
 * mix of refreshes, a little under 30 a second (a stutter that shows in
 * recordings). With VSync the game no longer waits for its timer:
 *  - on a display whose refresh is a multiple of 30 (60, 120 Hz), every swap
 *    waits for refresh / 30 refreshes: each picture stays up exactly 1/30 s;
 *  - on others (75, 144 Hz), port_frame_limit() starts a frame every 1/30 s
 *    and the swap shows it at the next refresh: the most even pattern that
 *    display allows (2, 3, 2, 3 refreshes at 75 Hz), at 30 a second.
 * VSync off keeps the timer. */
static int vsync_mode;  /* 0 the game's timer, 1 the swap interval, 2 port_frame_limit */

int port_vsync_paced(void)
{
    return vsync_mode != 0;
}

void port_setup_vsync_pacing(int vsync)
{
    extern int PsyX_PortRefreshRate(void);
    int rate = PsyX_PortRefreshRate();

    vsync_mode = 0;

    /* again from the start when the FPS limit changes (F6) */
    PsyX_SetSwapInterval(1);

    if (!vsync || !port_fps_locked())
    {
        return;
    }

    if ((rate >= 30) && ((rate % 30) == 0))
    {
        vsync_mode = 1;
        PsyX_SetSwapInterval(rate / 30);
        printf("[rewraithed] VSync paces the frames: one picture every %d refreshes of %d Hz\n", rate / 30, rate);
    }
    else
    {
        vsync_mode = 2;
        printf("[rewraithed] VSync: %d Hz display, frames started every 1/30 s\n", rate);
    }
}

int port_fps_locked(void)
{
    return port_fps_target() == 30;
}

/* Above 30 FPS, code written for the PSX's 30 FPS that steps once per frame
 * runs too fast. port_frame_limit() counts, once per frame, the 30-per-
 * second ticks due by the FPS target (not the game clock, so that they keep
 * going while the game is paused):
 *  - port_hud_ticks(): the ticks due this frame (the spiral HUD);
 *  - port_frame30(): a frame counter at 30 per second (texture animations,
 *    which the game takes from gameTrackerX.frameCount).
 * At 30 FPS they are 1 and the game's own frameCount. */
static int port_ticks30 = 1;
static unsigned long port_count30;

static void port_count_ticks30(void)
{
    static int accumulator;

    if (port_fps_locked() || (port_fps_target() <= 0))
    {
        port_ticks30 = 1;
        return;
    }

    accumulator += (30 * 4096) / port_fps_target();
    port_ticks30 = accumulator / 4096;
    accumulator -= port_ticks30 * 4096;
    port_count30 += port_ticks30;
}

/* 30-per-second ticks of game time (gameTrackerX.timeMult, 4096 per PSX
 * frame) due this frame, for game code that steps once per frame (the
 * plane shift's morph, the levels' vertex animations); each caller keeps
 * its own accumulator. Always 1 at 30 FPS, at most 4 after a long frame. */
int port_game_ticks(long *accumulator)
{
    extern GameTracker gameTrackerX;
    int ticks;

    if (port_fps_locked() || (port_fps_target() <= 0))
    {
        return 1;
    }

    *accumulator += gameTrackerX.timeMult;
    ticks = *accumulator / 4096;
    *accumulator -= ticks * 4096;

    return (ticks > 4) ? 4 : ticks;
}

/* a per-frame step (look and aim with the stick or keys, RAZIEL_TurnHead)
 * scaled to game time, so it turns as fast at 60 FPS as at 30; what is
 * left below a whole unit is kept in rest for the next frame */
int port_game_step(int value, long *rest)
{
    extern GameTracker gameTrackerX;
    int step;

    if (port_fps_locked() || (port_fps_target() <= 0))
    {
        return value;
    }

    *rest += (long)value * (long)gameTrackerX.timeMult;
    step = (int)(*rest / 4096);
    *rest -= (long)step * 4096;

    return step;
}

/* the Soul Reaver's lightning (FX_SoulReaverWinding) is drawn with this
 * seed, renewed on each 30-per-second tick of the blade (_SoulReaverAnimate),
 * so its flicker keeps the PlayStation's pace at 60 FPS */
unsigned int port_reaver_seed;

int port_hud_ticks(void)
{
    return port_ticks30;
}

unsigned long port_frame30(void)
{
    extern GameTracker gameTrackerX;

    return (port_fps_locked() || (port_fps_target() <= 0)) ? gameTrackerX.frameCount : port_count30;
}

unsigned int port_min_frame_ms(unsigned int lockRate)
{
    return port_fps_locked() ? lockRate : 1;
}

void port_frame_limit(void)
{
    static Uint64 next;
    Uint64 freq;
    Uint64 period;
    Uint64 now;

    {
        extern void port_focus_check(void);  /* focus_pause.c */

        port_focus_check();
    }

    port_count_ticks30();

    if ((port_fps_locked() && (vsync_mode != 2)) || (port_fps_target() <= 0))
    {
        return;
    }

    freq = SDL_GetPerformanceFrequency();
    period = freq / (Uint64)port_fps_target();
    now = SDL_GetPerformanceCounter();

    if ((next == 0) || (now > next + period * 4))
    {
        next = now;
    }

    while (now < next)
    {
        Uint64 ms = ((next - now) * 1000) / freq;

        if (ms > 2)
        {
            SDL_Delay((Uint32)(ms - 1));
        }

        now = SDL_GetPerformanceCounter();
    }

    next += period;
}
