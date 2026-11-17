/*
 * Screenshots (F12): PNG files in a "screenshots" folder next to
 * rewraithed.exe, named by date and time. The picture comes from
 * port/psycross/psyx_port.cpp just before the frame is shown.
 *
 * The PNG is written without compression (stored deflate blocks), which
 * needs no library and every viewer reads.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void port_exe_folder(char *out, size_t size);   /* disc_folder_win32.c */

static unsigned long crc_table[256];

static void crc_init(void)
{
    unsigned long c;
    int n, k;

    for (n = 0; n < 256; n++)
    {
        c = (unsigned long)n;

        for (k = 0; k < 8; k++)
        {
            c = (c & 1) ? (0xEDB88320UL ^ (c >> 1)) : (c >> 1);
        }

        crc_table[n] = c;
    }
}

static unsigned long crc_update(unsigned long crc, const unsigned char *p, size_t n)
{
    while (n--)
    {
        crc = crc_table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    }

    return crc;
}

static void put32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static void write_chunk(FILE *f, const char *type, const unsigned char *data, unsigned long len)
{
    unsigned char head[8];
    unsigned char tail[4];
    unsigned long crc;

    put32(head, len);
    memcpy(head + 4, type, 4);
    fwrite(head, 1, 8, f);

    if (len > 0)
    {
        fwrite(data, 1, len, f);
    }

    crc = crc_update(0xFFFFFFFFUL, (const unsigned char *)type, 4);
    crc = crc_update(crc, data, len);
    put32(tail, crc ^ 0xFFFFFFFFUL);
    fwrite(tail, 1, 4, f);
}

/* rgba: w * h pixels, top row first */
static int write_png(const char *path, const unsigned char *rgba, int w, int h)
{
    size_t raw_len = (size_t)(w * 3 + 1) * h;
    size_t blocks = (raw_len + 65534) / 65535;
    size_t z_len = 2 + raw_len + blocks * 5 + 4;
    unsigned char *raw = malloc(raw_len);
    unsigned char *z = malloc(z_len);
    unsigned char ihdr[13];
    unsigned long a = 1, b = 0;
    size_t i, pos, zp;
    int x, y;
    FILE *f;

    if ((raw == NULL) || (z == NULL))
    {
        free(raw);
        free(z);
        return 0;
    }

    /* filter type 0 per row, then RGB */
    for (y = 0, pos = 0; y < h; y++)
    {
        const unsigned char *src = rgba + (size_t)y * w * 4;

        raw[pos++] = 0;

        for (x = 0; x < w; x++)
        {
            raw[pos++] = src[x * 4 + 0];
            raw[pos++] = src[x * 4 + 1];
            raw[pos++] = src[x * 4 + 2];
        }
    }

    for (i = 0; i < raw_len; i++)
    {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }

    zp = 0;
    z[zp++] = 0x78;
    z[zp++] = 0x01;

    for (pos = 0; pos < raw_len;)
    {
        size_t n = raw_len - pos;

        if (n > 65535)
        {
            n = 65535;
        }

        z[zp++] = (pos + n >= raw_len) ? 1 : 0;
        z[zp++] = (unsigned char)(n & 0xFF);
        z[zp++] = (unsigned char)(n >> 8);
        z[zp++] = (unsigned char)(~n & 0xFF);
        z[zp++] = (unsigned char)((~n >> 8) & 0xFF);
        memcpy(z + zp, raw + pos, n);
        zp += n;
        pos += n;
    }

    put32(z + zp, (b << 16) | a);
    zp += 4;

    f = fopen(path, "wb");

    if (f == NULL)
    {
        free(raw);
        free(z);
        return 0;
    }

    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);

    put32(ihdr, (unsigned long)w);
    put32(ihdr + 4, (unsigned long)h);
    ihdr[8] = 8;    /* bits per channel */
    ihdr[9] = 2;    /* RGB */
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;

    write_chunk(f, "IHDR", ihdr, 13);
    write_chunk(f, "IDAT", z, (unsigned long)zp);
    write_chunk(f, "IEND", NULL, 0);

    fclose(f);
    free(raw);
    free(z);
    return 1;
}

void port_save_screenshot(const unsigned char *rgba, int w, int h)
{
    static int crc_ready;
    char folder[MAX_PATH];
    char path[MAX_PATH + 64];
    char stamp[32];
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    int n;

    if (!crc_ready)
    {
        crc_init();
        crc_ready = 1;
    }

    port_exe_folder(folder, sizeof(folder));
    strncat(folder, "\\screenshots", sizeof(folder) - strlen(folder) - 1);
    CreateDirectoryA(folder, NULL);

    strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H-%M-%S", t);
    snprintf(path, sizeof(path), "%s\\rewraithed_%s.png", folder, stamp);

    /* several in the same second */
    for (n = 2; (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) && (n < 1000); n++)
    {
        snprintf(path, sizeof(path), "%s\\rewraithed_%s_%d.png", folder, stamp, n);
    }

    if (write_png(path, rgba, w, h))
    {
        printf("[rewraithed] screenshot saved: %s\n", path);
    }
    else
    {
        printf("[rewraithed] screenshot could not be saved: %s\n", path);
    }
}
