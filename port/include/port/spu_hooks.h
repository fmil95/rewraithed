/*
 * Routes the game's libspu calls to the port's software SPU
 * (port/src/spu.c). PsyCross's SPU plays each voice through OpenAL without
 * envelopes, loop points or CD input; the sequenced music needs all three.
 */
#ifndef REWRAITHED_SPU_HOOKS_H
#define REWRAITHED_SPU_HOOKS_H

#define SpuInit                  port_SpuInit
#define SpuQuit                  port_SpuQuit
#define SpuSetKey                port_SpuSetKey
#define SpuGetAllKeysStatus      port_SpuGetAllKeysStatus
#define SpuSetReverb             port_SpuSetReverb
#define SpuSetReverbVoice        port_SpuSetReverbVoice
#define SpuSetReverbModeType     port_SpuSetReverbModeType
#define SpuSetReverbModeDepth    port_SpuSetReverbModeDepth
#define SpuClearReverbWorkArea   port_SpuClearReverbWorkArea
#define SpuSetCommonAttr         port_SpuSetCommonAttr
#define SpuGetCommonAttr         port_SpuGetCommonAttr
#define SpuSetCommonMasterVolume port_SpuSetCommonMasterVolume
#define SpuSetCommonCDMix        port_SpuSetCommonCDMix
#define SpuSetTransferCallback   port_SpuSetTransferCallback
#define SpuSetTransferStartAddr  port_SpuSetTransferStartAddr
#define SpuIsTransferCompleted   port_SpuIsTransferCompleted
#define SpuWrite                 port_SpuWrite
#define SpuRead                  port_SpuRead
#define SpuSetVoiceVolume        port_SpuSetVoiceVolume
#define SpuSetVoicePitch         port_SpuSetVoicePitch
#define SpuGetVoicePitch         port_SpuGetVoicePitch
#define SpuSetVoiceStartAddr     port_SpuSetVoiceStartAddr
#define SpuSetVoiceADSRAttr      port_SpuSetVoiceADSRAttr

#include <libspu.h>

/* Called in interrupt context (port/src/irq.c, port/src/cd_drive.c):
 * delivers the completion callback of the last SpuWrite once it is due, or
 * right away with `force`. */
void port_spu_poll_irq(int force);

/* XA audio from the CD drive (port/src/cd_drive.c) into the SPU's CD input. */
void port_xa_set_image(const char *path);
void port_xa_play(int lba, int useFilter, int file, int chan);
void port_xa_stop(void);
void port_xa_mute(int mute);

#endif
