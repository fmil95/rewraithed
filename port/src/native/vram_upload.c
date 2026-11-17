/*
 * Uploads one rectangle of PsyCross's CPU-side VRAM into the GPU VRAM
 * texture currently in use.
 *
 * PsyCross's own GR_UpdateVRAM re-uploads all of VRAM into the other of its
 * two textures and switches to it, which mid-scene would swap the texture
 * under polygons already queued. This only refreshes the written rectangle.
 */
#include <stdlib.h>

#include "PsyX/PsyX_render.h"

void port_upload_vram_rect(int x, int y, int w, int h)
{
    unsigned short *pixels;

    if (w <= 0 || h <= 0)
    {
        return;
    }

    pixels = malloc((size_t)w * h * sizeof(unsigned short));

    GR_ReadVRAM(pixels, x, y, w, h);

    glBindTexture(GL_TEXTURE_2D, g_vramTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, VRAM_FORMAT, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);

    free(pixels);
}
