/*
 * PC wrappers around the two ways the game loads overlay modules, so that
 * port/src/relmod.c knows which overlay a module belongs to.
 * (Original definitions renamed to <name>_psx through port/overrides.txt.)
 */
#include "common.h"
#include "Game/STREAM.h"
#include "Game/STRMLOAD.h"
#include "port/overlay.h"

void STREAM_LoadObjectReturn_psx(void *loadData, void *data, void *data2);
void LOAD_LoadToAddress_psx(char *fileName, void *loadAddr, long relocateBinary);

/* Streamed objects (monsters, cinemax): data is the ObjectTracker. */
void STREAM_LoadObjectReturn(void *loadData, void *data, void *data2)
{
    port_overlay_set_loading(((ObjectTracker *)data)->name);

    STREAM_LoadObjectReturn_psx(loadData, data, data2);
}

/* Direct loads such as "\kain2\object\mcardx\mcardx.drm". */
void LOAD_LoadToAddress(char *fileName, void *loadAddr, long relocateBinary)
{
    const char *base;
    const char *dot;
    char name[32];
    size_t len;

    base = strrchr(fileName, '\\');
    base = (base != NULL) ? base + 1 : fileName;
    dot = strrchr(base, '.');
    len = (dot != NULL) ? (size_t)(dot - base) : strlen(base);

    if (len >= sizeof(name))
    {
        len = sizeof(name) - 1;
    }

    memcpy(name, base, len);
    name[len] = '\0';

    port_overlay_set_loading(name);

    LOAD_LoadToAddress_psx(fileName, loadAddr, relocateBinary);
}
