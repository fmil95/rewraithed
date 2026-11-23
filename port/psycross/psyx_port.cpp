/*
 * The port's additions to PsyCross's OpenGL renderer, called from hooks that
 * port/fixups.cmake puts into src/render/PsyX_render.cpp:
 *
 *  - the view rectangle: the 4:3 part of the window when pillarboxing,
 *    else the whole window (PsyCross stretches the PSX picture over it);
 *  - reading the frame back into VRAM from that rectangle, through a
 *    single-sampled copy when the window is multisampled (a multisampled
 *    framebuffer cannot be blitted with scaling);
 *  - the texture filter, which can be overridden per draw (2D pictures
 *    follow the "Scaling" setting, 3D follows "Filtering").
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include <SDL.h>

#include "psx/types.h"
#include "psx/libgte.h"
#include "psx/libgpu.h"

#include "PsyX/common/glad.h"
#include "PsyX/PsyX_globals.h"
#include "PsyX/PsyX_public.h"
#include "PsyX/PsyX_render.h"

extern SDL_Window *g_window;   /* PsyX_render.cpp */
extern "C" DISPENV activeDispEnv;   /* PsyX_GPU.cpp */
void GR_CopyRGBAFramebufferToVRAM(unsigned int *src, int x, int y, int w, int h, int update_vram, int flip_y);

extern "C" {

/* Borderless fullscreen at the desktop's own resolution. */
void PsyX_PortSetBorderless(void)
{
    if (g_window == NULL)
    {
        return;
    }

    SDL_SetWindowFullscreen(g_window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_GetWindowSize(g_window, &g_windowWidth, &g_windowHeight);
}

/* Fullscreen at the display mode closest to the chosen size and refresh rate
 * (the launcher's Resolution list), from a window made windowed: the mode is
 * set before the window goes fullscreen, so the display changes mode once.
 * When that mode is not the desktop's, the display goes dark for a moment
 * while it resyncs; the game waits it out on a black picture, so that the
 * first logo is not shown to a dark screen. */
void PsyX_PortSetFullscreenMode(int width, int height, int refresh)
{
    SDL_DisplayMode want;
    SDL_DisplayMode got;
    SDL_DisplayMode desktop;
    int display;

    if (g_window == NULL)
    {
        return;
    }

    display = SDL_GetWindowDisplayIndex(g_window);

    memset(&want, 0, sizeof(want));
    want.w = width;
    want.h = height;
    want.refresh_rate = refresh;

    if ((display >= 0) && (SDL_GetClosestDisplayMode(display, &want, &got) != NULL))
    {
        SDL_SetWindowDisplayMode(g_window, &got);
    }
    else
    {
        printf("[rewraithed] no fullscreen mode near %dx%d @ %d Hz\n", width, height, refresh);
        got = want;
    }

    SDL_SetWindowFullscreen(g_window, SDL_WINDOW_FULLSCREEN);
    SDL_GetWindowSize(g_window, &g_windowWidth, &g_windowHeight);

    printf("[rewraithed] fullscreen mode %dx%d @ %d Hz\n", got.w, got.h, got.refresh_rate);

    if ((display >= 0) && (SDL_GetDesktopDisplayMode(display, &desktop) == 0) &&
        ((desktop.w != got.w) || (desktop.h != got.h) || ((got.refresh_rate != 0) && (desktop.refresh_rate != got.refresh_rate))))
    {
        Uint32 until = SDL_GetTicks() + 2000;

        while (SDL_GetTicks() < until)
        {
            SDL_Event event;

            while (SDL_PollEvent(&event))
            {
            }

            glViewport(0, 0, g_windowWidth, g_windowHeight);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            SDL_GL_SwapWindow(g_window);
            SDL_Delay(10);
        }
    }
}

/* The window's SDL flags (minimized, input focus), 0 without a window. */
unsigned int PsyX_PortWindowFlags(void)
{
    return (g_window != NULL) ? (unsigned int)SDL_GetWindowFlags(g_window) : 0;
}

/* Refresh rate of the window's display, 0 if unknown. */
int PsyX_PortRefreshRate(void)
{
    SDL_DisplayMode mode;

    if ((g_window == NULL) || (SDL_GetWindowDisplayMode(g_window, &mode) != 0))
    {
        return 0;
    }

    return mode.refresh_rate;
}

/* Window title, e.g. to show the frame rate. */
void PsyX_PortSetTitle(const char *title)
{
    if (g_window != NULL)
    {
        SDL_SetWindowTitle(g_window, title);
    }
}

int g_cfg_dithering = 1;        /* GPU dithering of shaded polygons */
int g_cfg_pillarbox = 1;        /* keep the PSX 4:3 picture */
int g_port_force_filter = -1;   /* >= 0: overrides g_cfg_bilinearFiltering */

int g_portViewX, g_portViewY, g_portViewW, g_portViewH;

static void port_compute_view(void)
{
    int w = g_windowWidth;
    int h = g_windowHeight;

    g_portViewX = 0;
    g_portViewY = 0;
    g_portViewW = w;
    g_portViewH = h;

    if (!g_cfg_pillarbox || (w <= 0) || (h <= 0))
    {
        return;
    }

    if (w * 3 > h * 4)
    {
        /* wider than 4:3: bars left and right */
        g_portViewW = h * 4 / 3;
        g_portViewX = (w - g_portViewW) / 2;
    }
    else if (w * 3 < h * 4)
    {
        /* taller: bars above and below */
        g_portViewH = w * 3 / 4;
        g_portViewY = (h - g_portViewH) / 2;
    }
}

/* Replaces PsyCross's full-window viewport. Bars are cleared to black. */
void PsyX_PortSetView(void)
{
    port_compute_view();

    if ((g_portViewW != g_windowWidth) || (g_portViewH != g_windowHeight))
    {
        GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
        GLint box[4];
        GLfloat clear[4];

        glGetIntegerv(GL_SCISSOR_BOX, box);
        glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);

        glEnable(GL_SCISSOR_TEST);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

        if (g_portViewX > 0)
        {
            glScissor(0, 0, g_portViewX, g_windowHeight);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(g_portViewX + g_portViewW, 0, g_windowWidth - g_portViewX - g_portViewW, g_windowHeight);
            glClear(GL_COLOR_BUFFER_BIT);
        }

        if (g_portViewY > 0)
        {
            glScissor(0, 0, g_windowWidth, g_portViewY);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(0, g_portViewY + g_portViewH, g_windowWidth, g_windowHeight - g_portViewY - g_portViewH);
            glClear(GL_COLOR_BUFFER_BIT);
        }

        glClearColor(clear[0], clear[1], clear[2], clear[3]);
        glScissor(box[0], box[1], box[2], box[3]);

        if (!scissor)
        {
            glDisable(GL_SCISSOR_TEST);
        }
    }

    glViewport(g_portViewX, g_portViewY, g_portViewW, g_portViewH);
}

/* Replaces PsyCross's clear (the game's background fill), which covered the
 * whole window and so painted the pillarbox bars in the area's fog colour.
 * In the window only the view rectangle is cleared. */
void PsyX_PortClear(void)
{
    GLint fbo = 0;

    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);

    if ((fbo == 0) && ((g_portViewW != g_windowWidth) || (g_portViewH != g_windowHeight)))
    {
        GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
        GLint box[4];

        glGetIntegerv(GL_SCISSOR_BOX, box);
        glEnable(GL_SCISSOR_TEST);
        glScissor(g_portViewX, g_portViewY, g_portViewW, g_portViewH);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glScissor(box[0], box[1], box[2], box[3]);

        if (!scissor)
        {
            glDisable(GL_SCISSOR_TEST);
        }

        return;
    }

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

/* A scissor rectangle given in 0..1 of the PSX screen (top-down). */
void PsyX_PortScissor(float x, float y, float w, float h)
{
    const float flipOffset = (float)g_portViewH - h * (float)g_portViewH;

    glScissor((GLint)(g_portViewX + x * g_portViewW), (GLint)(g_portViewY + flipOffset - y * g_portViewH),
        (GLsizei)(w * g_portViewW), (GLsizei)(h * g_portViewH));
}

/* Copies the view rectangle of the window into the bound draw framebuffer
 * at (x, y, w, h), flipped (the readback into VRAM). */
void PsyX_PortBlitFromWindow(int x, int y, int w, int h)
{
    static GLuint resolveFbo;
    static GLuint resolveTex;
    static int resolveW, resolveH;
    GLint drawFbo = 0;
    GLint samples = 0;

    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);

    /* GL_SAMPLES describes the draw framebuffer: ask about the window */
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glGetIntegerv(GL_SAMPLES, &samples);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFbo);

    if (samples <= 1)
    {
        glBlitFramebuffer(g_portViewX, g_portViewY, g_portViewX + g_portViewW, g_portViewY + g_portViewH,
            x, y + h, x + w, y, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        return;
    }

    /* resolve the multisampled window at its own size first */
    if ((resolveFbo == 0) || (resolveW != g_windowWidth) || (resolveH != g_windowHeight))
    {
        if (resolveFbo == 0)
        {
            glGenFramebuffers(1, &resolveFbo);
            glGenTextures(1, &resolveTex);
        }

        glBindTexture(GL_TEXTURE_2D, resolveTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, g_windowWidth, g_windowHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glBindTexture(GL_TEXTURE_2D, 0);

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFbo);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolveTex, 0);

        resolveW = g_windowWidth;
        resolveH = g_windowHeight;
    }

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFbo);
    glBlitFramebuffer(0, 0, g_windowWidth, g_windowHeight, 0, 0, g_windowWidth, g_windowHeight,
        GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, resolveFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFbo);
    glBlitFramebuffer(g_portViewX, g_portViewY, g_portViewX + g_portViewW, g_portViewY + g_portViewH,
        x, y + h, x + w, y, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

/* Screenshot key (F12): taken from the next frame, just before it is shown
 * (after the swap the window's contents are undefined). */
void port_save_screenshot(const unsigned char *rgba, int w, int h);   /* port/src/native/screenshot.c */

static int screenshotPending;

void PsyX_PortRequestScreenshot(void)
{
    screenshotPending = 1;
}

/* --- the last frame shown ---------------------------------------------------
 * On PSX the screen is a part of VRAM, so the picture being shown can be
 * copied, used as a texture and drawn over. PsyCross shows frames from the
 * window, whose contents are gone after the swap, and never puts them into
 * its VRAM copy. Each frame shown is kept here (view-sized, single-sampled)
 * so that it can be:
 *  - copied into VRAM (PsyX_PortStoreShownFrame), and
 *  - put back into the window before drawing (PsyX_PortRestoreShownFrame)
 *    when the game keeps drawing over the shown picture.
 * Used by the spectral death effect (UNDRWRLD.c). */
int g_port_keep_frame;  /* set by port/src/gpu_hooks.c for frames the game drew */

static GLuint shownFbo;
static GLuint shownTex;
static int shownW, shownH;
static int shownValid;

static void keep_shown_frame(void)
{
    GLint readFbo = 0;
    GLint drawFbo = 0;
    int w = g_portViewW;
    int h = g_portViewH;

    if ((w <= 0) || (h <= 0))
    {
        return;
    }

    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);

    if (shownFbo == 0)
    {
        glGenFramebuffers(1, &shownFbo);
        glGenTextures(1, &shownTex);
    }

    if ((shownW != w) || (shownH != h))
    {
        glBindTexture(GL_TEXTURE_2D, shownTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        /* drawn back at its own size (draw_shown_frame) */
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, shownFbo);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, shownTex, 0);
        shownW = w;
        shownH = h;
    }

    /* resolved if multisampled; flipped, so the texture's first row is the
     * top of the picture */
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, shownFbo);
    PsyX_PortBlitFromWindow(0, 0, w, h);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFbo);
    shownValid = 1;
}

/* A multisampled window can't be blitted into: the kept frame is drawn
 * into it as a textured quad instead, at full resolution. (The VRAM way,
 * at PSX resolution, smeared it more with each frame the effect drew over
 * it.) Returns 0 if the shader can't be made. */
static int draw_shown_frame(void)
{
    static GLuint program;
    static GLuint vao;
    static GLuint vbo;
    static int ready;
    GLint oldProgram, oldVao, oldTexture, oldActive, oldBuffer, viewport[4];
    GLboolean blend, depth, scissor, stencil, cull;

    if (ready == 0)
    {
        static const char *vs_src =
            "#version 140\n"
            "in vec2 a_pos;\n"
            "out vec2 v_uv;\n"
            "void main()\n"
            "{\n"
            "    v_uv = vec2((a_pos.x + 1.0) * 0.5, (1.0 - a_pos.y) * 0.5);\n"
            "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
            "}\n";
        static const char *fs_src =
            "#version 140\n"
            "in vec2 v_uv;\n"
            "out vec4 o_color;\n"
            "uniform sampler2D u_tex;\n"
            "void main()\n"
            "{\n"
            "    o_color = vec4(texture(u_tex, v_uv).rgb, 1.0);\n"
            "}\n";
        static const float quad[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
        GLuint vs = glCreateShader(GL_VERTEX_SHADER);
        GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
        GLint ok = 0;
        GLint vao0 = 0;
        GLint buf0 = 0;

        glShaderSource(vs, 1, &vs_src, NULL);
        glCompileShader(vs);
        glShaderSource(fs, 1, &fs_src, NULL);
        glCompileShader(fs);

        program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glBindAttribLocation(program, 0, "a_pos");
        glLinkProgram(program);
        glDeleteShader(vs);
        glDeleteShader(fs);
        glGetProgramiv(program, GL_LINK_STATUS, &ok);

        if (!ok)
        {
            ready = -1;
            return 0;
        }

        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao0);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &buf0);
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
        glBindVertexArray(vao0);
        glBindBuffer(GL_ARRAY_BUFFER, buf0);
        ready = 1;
    }

    if (ready < 0)
    {
        return 0;
    }

    glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVao);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &oldActive);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldTexture);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &oldBuffer);
    glGetIntegerv(GL_VIEWPORT, viewport);
    blend = glIsEnabled(GL_BLEND);
    depth = glIsEnabled(GL_DEPTH_TEST);
    scissor = glIsEnabled(GL_SCISSOR_TEST);
    stencil = glIsEnabled(GL_STENCIL_TEST);
    cull = glIsEnabled(GL_CULL_FACE);

    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);

    glViewport(g_portViewX, g_portViewY, g_portViewW, g_portViewH);
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "u_tex"), 0);
    glBindTexture(GL_TEXTURE_2D, shownTex);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glUseProgram(oldProgram);
    glBindVertexArray(oldVao);
    glBindBuffer(GL_ARRAY_BUFFER, oldBuffer);
    glBindTexture(GL_TEXTURE_2D, oldTexture);
    glActiveTexture(oldActive);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);

    if (blend) glEnable(GL_BLEND);
    if (depth) glEnable(GL_DEPTH_TEST);
    if (scissor) glEnable(GL_SCISSOR_TEST);
    if (stencil) glEnable(GL_STENCIL_TEST);
    if (cull) glEnable(GL_CULL_FACE);

    return 1;
}

/* Puts the last frame shown back into the window's view. Returns 0 when
 * that cannot be done by a blit (a multisampled window cannot be blitted
 * into); the caller then draws it from VRAM instead. */
int PsyX_PortRestoreShownFrame(void)
{
    GLint readFbo = 0;
    GLint drawFbo = 0;
    GLint samples = 0;
    GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);

    if (!shownValid || (shownW != g_portViewW) || (shownH != g_portViewH))
    {
        return 1;
    }

    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glGetIntegerv(GL_SAMPLES, &samples);

    if (samples > 1)
    {
        /* into the window (still bound for drawing) */
        int drawn = draw_shown_frame();

        glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFbo);
        return drawn;
    }
    glDisable(GL_SCISSOR_TEST);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, shownFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, shownW, shownH, g_portViewX, g_portViewY + shownH, g_portViewX + shownW, g_portViewY,
        GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFbo);

    if (scissor)
    {
        glEnable(GL_SCISSOR_TEST);
    }

    return 1;
}

static void store_fbo_to_vram(GLuint srcFbo, int srcW, int srcH, int x, int y, int w, int h);

/* Writes the last frame shown into VRAM at (x, y, w, h), scaled to it. */
void PsyX_PortStoreShownFrame(int x, int y, int w, int h)
{
    if (!shownValid)
    {
        return;
    }

    store_fbo_to_vram(shownFbo, shownW, shownH, x, y, w, h);
}

/* --- textures read from the buffer being drawn ------------------------------
 * On PSX the frame is drawn into VRAM, so a polygon can be textured with
 * what is already drawn: the game draws a see-through object (the urn
 * Raziel aims to throw) as the picture behind it, then adds the object's
 * texture. PsyCross draws all of a frame's polygons into the window at the
 * end, and its VRAM never holds the frame. So on the first polygon of a
 * frame textured from the draw buffer (PsyX_GPU.cpp, AddSplit), what is
 * collected so far is drawn, and the window is copied into the draw
 * buffer's place in VRAM (PsyX_PortStoreWindowToVRAM) for the rest. */
static int feedbackDone;    /* this frame */

extern "C" int port_feedback_allowed(int pageY);     /* port/src/gpu_hooks.c */

int PsyX_PortReadsDrawBuffer(int tpage, const RECT16 *clip)
{
    int px = (tpage & 0xF) * 64;
    int py = ((tpage >> 4) & 1) * 256;

    if (feedbackDone || (tpage & 0x8000) || (((tpage >> 7) & 3) != 2) || !port_feedback_allowed(py))
    {
        return 0;
    }

    /* the 256 x 256 texture page overlaps the area being drawn */
    if ((px >= clip->x + clip->w) || (px + 256 <= clip->x) || (py >= clip->y + clip->h) || (py + 256 <= clip->y))
    {
        return 0;
    }

    feedbackDone = 1;
    return 1;
}

void PsyX_PortStoreWindowToVRAM(int x, int y, int w, int h)
{
    static GLuint fbo;
    static GLuint tex;
    static int texW, texH;
    GLint readFbo = 0;
    GLint drawFbo = 0;
    int vw = g_portViewW;
    int vh = g_portViewH;

    if ((vw <= 0) || (vh <= 0))
    {
        return;
    }

    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);

    if (fbo == 0)
    {
        glGenFramebuffers(1, &fbo);
        glGenTextures(1, &tex);
    }

    if ((texW != vw) || (texH != vh))
    {
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, vw, vh, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        texW = vw;
        texH = vh;
    }

    /* resolved if multisampled; flipped, first row at the top */
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
    PsyX_PortBlitFromWindow(0, 0, vw, vh);

    store_fbo_to_vram(fbo, vw, vh, x, y, w, h);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFbo);
}

/* a new frame: the next one may need it again (PsyX_PortAfterSwap) */
static void feedback_new_frame(void)
{
    feedbackDone = 0;
}

/* srcW x srcH of srcFbo (first row the top of the picture) into VRAM at
 * (x, y, w, h), scaled to it */
static void store_fbo_to_vram(GLuint srcFbo, int srcW, int srcH, int x, int y, int w, int h)
{
    static GLuint fbo;
    static GLuint tex;
    static int texW, texH;
    GLint readFbo = 0;
    GLint drawFbo = 0;
    GLint packAlign = 4;
    unsigned int *pixels;

    if ((x < 0) || (y < 0) || (w <= 0) || (h <= 0) || (x + w > 1024) || (y + h > 512))
    {
        return;
    }

    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);

    if (fbo == 0)
    {
        glGenFramebuffers(1, &fbo);
        glGenTextures(1, &tex);
    }

    if ((texW != w) || (texH != h))
    {
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        texW = w;
        texH = h;
    }

    /* scaled down; rows stay top first, as in VRAM */
    glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
    glBlitFramebuffer(0, 0, srcW, srcH, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_LINEAR);

    pixels = (unsigned int *)malloc((size_t)w * h * 4);

    if (pixels != NULL)
    {
        glGetIntegerv(GL_PACK_ALIGNMENT, &packAlign);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glPixelStorei(GL_PACK_ALIGNMENT, packAlign);

        GR_CopyRGBAFramebufferToVRAM(pixels, x, y, w, h, 1, 0);
        free(pixels);
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFbo);
}

/* --- PGXP -------------------------------------------------------------------
 * The GTE rounds each projected vertex down to whole PSX pixels, which makes
 * geometry wobble and snap, more visibly the higher the resolution. With
 * PGXP on, the GTE (fixup in PsyX_GTE.cpp) also stores the exact position,
 * keyed by the integer screen X/Y it wrote. A polygon whose vertices carry
 * those X/Y gets the exact positions back when it is drawn (fixups in
 * PsyX_GPU.cpp): the whole pixels in x/y, the fraction in 1/128 pixel in the
 * vertex's two spare bytes, added in the vertex shader. A key that another
 * vertex shares is off by under a pixel at worst, and a polygon needs two
 * vertices found, so 2D polygons that happen to sit on a projected vertex
 * stay as they are. */
int g_port_pgxp;    /* set by port/src/pc_main.c from the PGXP setting */

typedef struct
{
    unsigned int key;
    unsigned int frame;
    float x, y;
} PortPgxpEntry;

static PortPgxpEntry pgxp_cache[1 << 16];
static unsigned int pgxp_frame = 1;

/* REWRAITHED_PACELOG: how long each picture stayed up (the time between one
 * swap returning and the next), every 300 pictures, in display refreshes
 * of the window's display */
void PsyX_PortAfterSwap(void)
{
    static int enabled = -1;
    static Uint64 last;
    static int counts[6];
    static int total;
    Uint64 now;
    double ms;
    double refresh = 1000.0 / 60.0;
    SDL_DisplayMode mode;

    feedback_new_frame();

    if (enabled < 0)
    {
        enabled = (getenv("REWRAITHED_PACELOG") != NULL);
    }

    if (!enabled)
    {
        return;
    }

    now = SDL_GetPerformanceCounter();

    if (last != 0)
    {
        int bucket;

        if ((SDL_GetWindowDisplayMode(g_window, &mode) == 0) && (mode.refresh_rate > 0))
        {
            refresh = 1000.0 / mode.refresh_rate;
        }

        ms = (double)(now - last) * 1000.0 / (double)SDL_GetPerformanceFrequency();
        bucket = (int)(ms / refresh + 0.5);

        if (bucket > 5)
        {
            bucket = 5;
        }

        counts[bucket]++;

        if (++total == 300)
        {
            printf("[rewraithed] pace (refreshes per picture): 0:%d 1:%d 2:%d 3:%d 4:%d 5+:%d\n",
                counts[0], counts[1], counts[2], counts[3], counts[4], counts[5]);
            memset(counts, 0, sizeof(counts));
            total = 0;
        }
    }

    last = now;
}

void PsyX_PortBeforeSwap(void)
{
    static GLuint fbo;
    static GLuint tex;
    static int texW, texH;
    GLint readFbo = 0;
    GLint drawFbo = 0;
    GLint packAlign = 4;
    int w = g_portViewW;
    int h = g_portViewH;
    unsigned char *pixels;

    /* only frames the game drew: pictures shown straight from the VRAM copy
     * (port/src/gpu_hooks.c) would be the empty pages PsyCross keeps there */
    if (g_port_keep_frame)
    {
        g_port_keep_frame = 0;
        keep_shown_frame();
    }

    pgxp_frame++;

    if (!screenshotPending)
    {
        return;
    }

    screenshotPending = 0;

    if ((w <= 0) || (h <= 0))
    {
        w = g_windowWidth;
        h = g_windowHeight;
        g_portViewX = 0;
        g_portViewY = 0;
        g_portViewW = w;
        g_portViewH = h;
    }

    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);

    if (fbo == 0)
    {
        glGenFramebuffers(1, &fbo);
        glGenTextures(1, &tex);
    }

    if ((texW != w) || (texH != h))
    {
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glBindTexture(GL_TEXTURE_2D, 0);
        texW = w;
        texH = h;
    }

    /* the view rectangle, resolved if multisampled, top row first */
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    PsyX_PortBlitFromWindow(0, 0, w, h);

    pixels = (unsigned char *)malloc((size_t)w * h * 4);

    if (pixels != NULL)
    {
        glGetIntegerv(GL_PACK_ALIGNMENT, &packAlign);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glPixelStorei(GL_PACK_ALIGNMENT, packAlign);

        port_save_screenshot(pixels, w, h);
        free(pixels);
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFbo);
}

/* Window pixels per PSX pixel for the dither pattern of the next draw:
 * 1 off screen (drawn at PSX resolution), else view height / PSX height. */
void PsyX_PortSetDitherScale(int onScreen)
{
    GLint program = 0;
    GLint loc;
    float scale = 1.0f;

    if (onScreen && (activeDispEnv.disp.h > 0) && (g_portViewH > 0))
    {
        scale = (float)g_portViewH / (float)activeDispEnv.disp.h;
    }

    glGetIntegerv(GL_CURRENT_PROGRAM, &program);

    if (program == 0)
    {
        return;
    }

    loc = glGetUniformLocation((GLuint)program, "ditherScale");

    if (loc >= 0)
    {
        glUniform1f(loc, scale);
    }
}

static unsigned int pgxp_slot(unsigned int key)
{
    return (key * 2654435761u) >> 16;
}

void PsyX_PortPgxpStore(unsigned int sxy, double x, double y)
{
    PortPgxpEntry *e;
    short sx = (short)(sxy & 0xFFFF);
    short sy = (short)(sxy >> 16);

    /* clamped by the GTE: the integer position is not the exact one */
    if ((x < sx - 0.5) || (x >= sx + 2.0) || (y < sy - 0.5) || (y >= sy + 2.0))
    {
        return;
    }

    e = &pgxp_cache[pgxp_slot(sxy)];
    e->key = sxy;
    e->frame = pgxp_frame;
    e->x = (float)x;
    e->y = (float)y;
}

void PsyX_PortPgxpApply(GrVertex *vertex, short **pts, int count)
{
    const PortPgxpEntry *found[4];
    int hits = 0;
    int i;

    if (!g_port_pgxp)
    {
        return;
    }

    for (i = 0; i < count; i++)
    {
        unsigned int key = (unsigned short)pts[i][0] | ((unsigned int)(unsigned short)pts[i][1] << 16);
        const PortPgxpEntry *e = &pgxp_cache[pgxp_slot(key)];

        /* stored this frame or the one before (drawn a frame later) */
        found[i] = ((e->key == key) && (pgxp_frame - e->frame <= 1)) ? e : NULL;
        hits += (found[i] != NULL);
    }


    if (hits < 2)
    {
        return;
    }

    for (i = 0; i < count; i++)
    {
        float dx, dy;
        int ix, iy;

        if (found[i] == NULL)
        {
            continue;
        }

        dx = found[i]->x - pts[i][0];
        dy = found[i]->y - pts[i][1];
        ix = (int)floorf(dx);
        iy = (int)floorf(dy);

        vertex[i].x += ix;
        vertex[i].y += iy;
        vertex[i]._p0 = (char)((dx - ix) * 127.0f);
        vertex[i]._p1 = (char)((dy - iy) * 127.0f);
    }
}

/* --- input added to the keyboard's ---------------------------------------
 * Set by port/src/pc_main.c from the settings: a second key for each pad
 * button and the mouse button of each mouse action (SDL numbers, 0 none).
 * In the menus (set each frame by port/src/input_hooks.c), Enter also
 * selects, as Cross. Pad bits are active low, like the pad's. Called from
 * PsyX_Pad_UpdateKeyboardInput. */
int g_port_alt_keys[14];    /* SDL scancodes, in the order of BIND_* */
int g_port_mouse[3];        /* attack, telekinesis, aim */
int g_port_menu_enter = 1;    /* the game starts in the logos */

static const u_short port_bind_bits[14] = {
    0x10, 0x40, 0x80, 0x20,         /* up, down, left, right */
    0x4000, 0x2000, 0x8000, 0x1000, /* cross, circle, square, triangle */
    0x400, 0x100, 0x800, 0x200,     /* L1, L2, R1, R2 */
    0x1, 0x8,                       /* select, start */
};

/* square (attack), triangle (telekinesis), L2 + R2 (aim) */
static const u_short port_mouse_bits[3] = { 0x8000, 0x1000, 0x100 | 0x200 };

u_short PsyX_PortExtraButtons(void)
{
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    u_short ret = 0xFFFF;
    int i;

    for (i = 0; i < 14; i++)
    {
        if ((g_port_alt_keys[i] > 0) && keys[g_port_alt_keys[i]])
        {
            ret &= ~port_bind_bits[i];
        }
    }

    if (g_port_menu_enter && (keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_KP_ENTER]))
    {
        ret &= ~0x4000;     /* Cross */
    }

    /* the mouse, while the game window has the focus */
    if ((g_window != NULL) && (SDL_GetMouseFocus() == g_window))
    {
        Uint32 buttons = SDL_GetMouseState(NULL, NULL);

        for (i = 0; i < 3; i++)
        {
            if ((g_port_mouse[i] > 0) && (buttons & SDL_BUTTON(g_port_mouse[i])))
            {
                ret &= ~port_mouse_bits[i];
            }
        }
    }

    return ret;
}

/* --- textures from outside VRAM ---------------------------------------------
 * RGBA textures the game side registers (port/src/dc_models.c: the
 * Dreamcast models' textures), drawn for primitives whose texture page has
 * bit 15 set, slot in its low 5 bits (fixup in PsyX_GPU.cpp, AddSplit). The
 * GL texture is made on first use, on the thread that draws. */
#define PORT_EXT_TEXTURES 32

static unsigned char *ext_rgba[PORT_EXT_TEXTURES];
static int ext_width[PORT_EXT_TEXTURES];
static int ext_height[PORT_EXT_TEXTURES];
static TextureID ext_texture[PORT_EXT_TEXTURES];

void PsyX_PortSetExtTexture(int slot, const unsigned char *rgba, int width, int height)
{
    size_t size = (size_t)width * height * 4;

    if ((slot < 0) || (slot >= PORT_EXT_TEXTURES) || (ext_rgba[slot] != NULL))
    {
        return;
    }

    ext_rgba[slot] = (unsigned char *)malloc(size);

    if (ext_rgba[slot] != NULL)
    {
        memcpy(ext_rgba[slot], rgba, size);
        ext_width[slot] = width;
        ext_height[slot] = height;
    }
}

/* 0 when the slot has no texture: the primitive is drawn from VRAM */
TextureID PsyX_PortExtTexture(int slot, int *width, int *height)
{
    if ((slot < 0) || (slot >= PORT_EXT_TEXTURES) || (ext_rgba[slot] == NULL))
    {
        return 0;
    }

    if (ext_texture[slot] == 0)
    {
        ext_texture[slot] = GR_CreateRGBATexture(ext_width[slot], ext_height[slot], ext_rgba[slot]);
    }

    *width = ext_width[slot];
    *height = ext_height[slot];

    return ext_texture[slot];
}

int PsyX_PortTextureFilter(void)
{
    return (g_port_force_filter >= 0) ? g_port_force_filter : g_cfg_bilinearFiltering;
}

}
