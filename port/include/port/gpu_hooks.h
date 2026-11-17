/*
 * Routes the game's frame submission through port/src/gpu_hooks.c.
 */
#ifndef REWRAITHED_GPU_HOOKS_H
#define REWRAITHED_GPU_HOOKS_H

#define DrawOTag         port_DrawOTag
#define DrawSyncCallback port_DrawSyncCallback
#define VSync            port_VSync
#define VSyncCallback    port_VSyncCallback
#define ResetCallback    port_ResetCallback

int port_ResetCallback(void);

/* GPU transfers and immediate draws also raise the draw-sync interrupt. */
#define LoadImage        port_LoadImage
#define StoreImage       port_StoreImage
#define MoveImage        port_MoveImage
#define ClearImage       port_ClearImage
#define DrawPrim         port_DrawPrim
#define PutDrawEnv       port_PutDrawEnv

DRAWENV *port_PutDrawEnv(DRAWENV *env);

int port_LoadImage(RECT16 *rect, u_long *p);
int port_StoreImage(RECT16 *rect, u_long *p);
int port_MoveImage(RECT16 *rect, int x, int y);
int port_ClearImage(RECT16 *rect, u_char r, u_char g, u_char b);
void port_DrawPrim(void *p);

void port_DrawOTag(u_long *p);
int port_DrawSyncCallback(void (*func)(void));
int port_VSync(int mode);
int port_VSyncCallback(void (*func)(void));

/* FPS limit setting (port/src/gpu_hooks.c), used by GAMELOOP.c fixups:
 * at 30 the game paces itself as on the PSX; otherwise its minimum time
 * step and two-vblank wait are lifted and port_frame_limit paces it. */
int port_fps_locked(void);
unsigned int port_min_frame_ms(unsigned int lockRate);
void port_frame_limit(void);

#endif
