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
#include <SDL.h>

#include "PsyX/common/glad.h"
#include "PsyX/PsyX_globals.h"
#include "PsyX/PsyX_public.h"

extern SDL_Window *g_window;   /* PsyX_render.cpp */

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

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glGetIntegerv(GL_SAMPLES, &samples);

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

int PsyX_PortTextureFilter(void)
{
    return (g_port_force_filter >= 0) ? g_port_force_filter : g_cfg_bilinearFiltering;
}

}
