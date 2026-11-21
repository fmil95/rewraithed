/*
 * The GPU's 24-bit display mode, used by the movie player: the displayed
 * VRAM area holds packed 8-bit R, G, B bytes (three per pixel, two per
 * 16-bit VRAM word) instead of 15-bit pixels. PsyCross only scans out
 * 15-bit pictures, so this shows such an area itself: it unpacks the bytes
 * into a texture and draws it over the whole window.
 *
 * Runs on the main thread between frames; the GL state PsyCross relies on
 * is saved and restored around it.
 */
#include <stdio.h>
#include <stdlib.h>

#include <SDL.h>

#include "PsyX/common/glad.h"
#include "PsyX/PsyX_globals.h"
#include "settings.h"

extern void GR_ReadVRAM(unsigned short *dst, int x, int y, int dst_w, int dst_h);
extern void GR_SwapWindow(void);

static GLuint rgb_program;
static GLuint rgb_vao;
static GLuint rgb_vbo;
static GLuint rgb_texture;
static int rgb_ready;

static const char *vertex_src =
    "#version 140\n"
    "in vec2 a_pos;\n"
    "out vec2 v_uv;\n"
    "void main()\n"
    "{\n"
    "    v_uv = vec2((a_pos.x + 1.0) * 0.5, (1.0 - a_pos.y) * 0.5);\n"
    "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

static const char *fragment_src =
    "#version 140\n"
    "in vec2 v_uv;\n"
    "out vec4 o_color;\n"
    "uniform sampler2D u_tex;\n"
    "void main()\n"
    "{\n"
    "    o_color = vec4(texture(u_tex, v_uv).rgb, 1.0);\n"
    "}\n";

static GLuint compile(GLenum type, const char *src)
{
    GLuint shader = glCreateShader(type);
    GLint ok = 0;

    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);

    if (!ok)
    {
        char log[512];

        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "[rewraithed] rgb24 shader: %s\n", log);
    }

    return shader;
}

static int rgb_init(void)
{
    static const float quad[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
    GLuint vs = compile(GL_VERTEX_SHADER, vertex_src);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fragment_src);
    GLint ok = 0;

    rgb_program = glCreateProgram();
    glAttachShader(rgb_program, vs);
    glAttachShader(rgb_program, fs);
    glBindAttribLocation(rgb_program, 0, "a_pos");
    glLinkProgram(rgb_program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    glGetProgramiv(rgb_program, GL_LINK_STATUS, &ok);

    if (!ok)
    {
        fprintf(stderr, "[rewraithed] rgb24 shader program does not link\n");
        return 0;
    }

    glGenVertexArrays(1, &rgb_vao);
    glGenBuffers(1, &rgb_vbo);
    glBindVertexArray(rgb_vao);
    glBindBuffer(GL_ARRAY_BUFFER, rgb_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glBindVertexArray(0);

    glGenTextures(1, &rgb_texture);
    glBindTexture(GL_TEXTURE_2D, rgb_texture);
    /* movies follow the "Scaling" setting */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, port_settings.scaleLinear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, port_settings.scaleLinear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    return 1;
}

/* Shows the 24-bit picture of width w pixels and height h whose packed
 * bytes start at VRAM (x, y); w == 0 shows black. Also presents. */
void port_present_rgb24(int x, int y, int w, int h)
{
    GLint program, vao, texture, array_buffer, active_texture, viewport[4];
    GLint draw_fbo, read_fbo, unpack_alignment;
    GLfloat clear_color[4];
    GLboolean blend, depth, scissor, stencil, cull;
    int halfwords = (w * 3 + 1) / 2;

    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active_texture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array_buffer);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_fbo);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_fbo);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack_alignment);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clear_color);

    /* after saving: creating the shader, VAO and texture changes bindings */
    if (!rgb_ready)
    {
        rgb_ready = rgb_init() ? 1 : -1;
    }
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

    /* the window, whatever PsyCross had bound */
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glViewport(0, 0, g_windowWidth, g_windowHeight);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    if ((rgb_ready > 0) && (w > 0) && (h > 0))
    {
        unsigned short *packed = malloc((size_t)halfwords * h * sizeof(unsigned short));
        int vw;
        int vh;

        GR_ReadVRAM(packed, x, y, halfwords, h);

        glBindTexture(GL_TEXTURE_2D, rgb_texture);
        /* each frame: F4 changes "Scaling" while playing (video_hotkeys.c) */
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, port_settings.scaleLinear ? GL_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, port_settings.scaleLinear ? GL_LINEAR : GL_NEAREST);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, halfwords * 2 / 3);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, packed);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        free(packed);

        /* 4:3, centred, or the whole window without pillarboxing */
        vw = g_windowWidth;
        vh = port_settings.pillarbox ? (g_windowWidth * 3 / 4) : g_windowHeight;

        if (vh > g_windowHeight)
        {
            vh = g_windowHeight;
            vw = g_windowHeight * 4 / 3;
        }

        glViewport((g_windowWidth - vw) / 2, (g_windowHeight - vh) / 2, vw, vh);

        glUseProgram(rgb_program);
        glUniform1i(glGetUniformLocation(rgb_program, "u_tex"), 0);
        glBindVertexArray(rgb_vao);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    glUseProgram(program);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, array_buffer);
    glBindTexture(GL_TEXTURE_2D, texture);
    glActiveTexture(active_texture);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw_fbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, read_fbo);
    glPixelStorei(GL_UNPACK_ALIGNMENT, unpack_alignment);
    glClearColor(clear_color[0], clear_color[1], clear_color[2], clear_color[3]);

    if (blend) glEnable(GL_BLEND);
    if (depth) glEnable(GL_DEPTH_TEST);
    if (scissor) glEnable(GL_SCISSOR_TEST);
    if (stencil) glEnable(GL_STENCIL_TEST);
    if (cull) glEnable(GL_CULL_FACE);
}

void port_swap_window(void)
{
    GR_SwapWindow();
}
