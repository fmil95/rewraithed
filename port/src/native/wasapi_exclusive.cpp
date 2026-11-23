/*
 * Sound output in WASAPI exclusive mode (port/src/spu.c uses it while the
 * game is fullscreen and active): the game takes the audio device for
 * itself, so no other program's sounds (chat notifications, system sounds)
 * play over it. Windows frees the device when the game stops using it,
 * exits or crashes.
 *
 * The game mixes 16-bit stereo at 44.1 kHz (fill). The device's exclusive
 * format is the first it accepts of 44.1 or 48 kHz in 16-bit, 24-bit (in
 * 32) or float; 48 kHz is resampled linearly. Event driven: each period the
 * whole buffer is filled on a thread of its own.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <ks.h>
#include <ksmedia.h>
#include <stdio.h>
#include <string.h>

typedef void (*ExclFill)(short *out, int frames);

static IMMDevice *excl_device;
static IAudioClient *excl_client;
static IAudioRenderClient *excl_render;
static HANDLE excl_event;
static HANDLE excl_thread;
static volatile LONG excl_stop;
static volatile LONG excl_dead;
static ExclFill excl_fill;
static int excl_srcRate;

static WAVEFORMATEXTENSIBLE excl_fmt;
static UINT32 excl_frames;          /* buffer (= period) in frames */
static int excl_kind;               /* 0 s16, 1 s24 in 32, 2 float */

/* resampling: source frames are pulled from fill as needed */
static short excl_src[4096 * 2];
static int excl_srcCount;
static int excl_srcPos;
static unsigned int excl_frac;      /* 16.16 between src[pos] and src[pos + 1] */
static unsigned int excl_step;      /* 16.16 source frames per output frame */

static void make_format(WAVEFORMATEXTENSIBLE *f, int rate, int kind)
{
    memset(f, 0, sizeof(*f));
    f->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    f->Format.nChannels = 2;
    f->Format.nSamplesPerSec = rate;
    f->Format.wBitsPerSample = (kind == 0) ? 16 : 32;
    f->Format.nBlockAlign = (WORD)(f->Format.nChannels * f->Format.wBitsPerSample / 8);
    f->Format.nAvgBytesPerSec = rate * f->Format.nBlockAlign;
    f->Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    f->Samples.wValidBitsPerSample = (kind == 0) ? 16 : ((kind == 1) ? 24 : 32);
    f->dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    f->SubFormat = (kind == 2) ? KSDATAFORMAT_SUBTYPE_IEEE_FLOAT : KSDATAFORMAT_SUBTYPE_PCM;
}

/* the next source frame pair for output: left and right of frame pos and pos + 1 */
static void need_source(int count)
{
    if (excl_srcPos + count <= excl_srcCount)
    {
        return;
    }

    /* keep what is left, then fill the rest */
    {
        int keep = excl_srcCount - excl_srcPos;
        int want = 1024;

        memmove(excl_src, excl_src + excl_srcPos * 2, keep * 2 * sizeof(short));
        excl_fill(excl_src + keep * 2, want);
        excl_srcCount = keep + want;
        excl_srcPos = 0;
    }
}

static void render(BYTE *data, UINT32 frames)
{
    UINT32 i;

    for (i = 0; i < frames; i++)
    {
        int l;
        int r;

        need_source(2);

        {
            const short *a = &excl_src[excl_srcPos * 2];
            int f = (int)(excl_frac >> 4);      /* 12 bits */

            l = a[0] + (((a[2] - a[0]) * f) >> 12);
            r = a[1] + (((a[3] - a[1]) * f) >> 12);
        }

        excl_frac += excl_step;
        excl_srcPos += (int)(excl_frac >> 16);
        excl_frac &= 0xFFFF;

        if (excl_kind == 0)
        {
            ((short *)data)[i * 2] = (short)l;
            ((short *)data)[i * 2 + 1] = (short)r;
        }
        else if (excl_kind == 1)
        {
            ((int *)data)[i * 2] = l << 16;
            ((int *)data)[i * 2 + 1] = r << 16;
        }
        else
        {
            ((float *)data)[i * 2] = l / 32768.0f;
            ((float *)data)[i * 2 + 1] = r / 32768.0f;
        }
    }
}

static int fill_buffer(void)
{
    BYTE *data;

    if (FAILED(excl_render->GetBuffer(excl_frames, &data)))
    {
        return 0;
    }

    render(data, excl_frames);

    return SUCCEEDED(excl_render->ReleaseBuffer(excl_frames, 0));
}

static DWORD WINAPI excl_proc(LPVOID param)
{
    (void)param;

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    while (!excl_stop)
    {
        if (WaitForSingleObject(excl_event, 500) != WAIT_OBJECT_0)
        {
            /* no event in half a second: the device is gone */
            if (!excl_stop)
            {
                InterlockedExchange(&excl_dead, 1);
            }

            break;
        }

        if (excl_stop)
        {
            break;
        }

        if (!fill_buffer())
        {
            InterlockedExchange(&excl_dead, 1);
            break;
        }
    }

    return 0;
}

static void release_all(void)
{
    if (excl_render != NULL) { excl_render->Release(); excl_render = NULL; }
    if (excl_client != NULL) { excl_client->Release(); excl_client = NULL; }
    if (excl_device != NULL) { excl_device->Release(); excl_device = NULL; }
    if (excl_event != NULL) { CloseHandle(excl_event); excl_event = NULL; }
}

static int init_client(void)
{
    static const int rates[2] = { 44100, 48000 };
    REFERENCE_TIME defPeriod = 0;
    REFERENCE_TIME minPeriod = 0;
    HRESULT hr;
    int found = 0;
    int r;
    int k;

    if (FAILED(excl_device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void **)&excl_client)))
    {
        return 0;
    }

    for (r = 0; (r < 2) && !found; r++)
    {
        for (k = 0; (k < 3) && !found; k++)
        {
            make_format(&excl_fmt, rates[r], k);

            if (excl_client->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &excl_fmt.Format, NULL) == S_OK)
            {
                excl_kind = k;
                found = 1;
            }
        }
    }

    if (!found)
    {
        printf("[rewraithed] exclusive audio: no format the device takes\n");
        return 0;
    }

    excl_client->GetDevicePeriod(&defPeriod, &minPeriod);

    hr = excl_client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, defPeriod, defPeriod,
        &excl_fmt.Format, NULL);

    if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED)
    {
        UINT32 frames = 0;

        excl_client->GetBufferSize(&frames);
        excl_client->Release();
        excl_client = NULL;

        defPeriod = (REFERENCE_TIME)(10000.0 * 1000.0 * frames / excl_fmt.Format.nSamplesPerSec + 0.5);

        if (FAILED(excl_device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void **)&excl_client)))
        {
            return 0;
        }

        hr = excl_client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, defPeriod, defPeriod,
            &excl_fmt.Format, NULL);
    }

    if (FAILED(hr))
    {
        printf("[rewraithed] exclusive audio: device refused (0x%08lx)\n", (unsigned long)hr);
        return 0;
    }

    return 1;
}

extern "C" int port_excl_start(ExclFill fill, int srcRate)
{
    IMMDeviceEnumerator *enumerator = NULL;
    HRESULT hr;

    if (excl_thread != NULL)
    {
        return 1;
    }

    excl_fill = fill;
    excl_srcRate = srcRate;
    excl_stop = 0;
    excl_dead = 0;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    (void)hr;

    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
            (void **)&enumerator)))
    {
        return 0;
    }

    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &excl_device);
    enumerator->Release();

    if (FAILED(hr) || !init_client())
    {
        release_all();
        return 0;
    }

    excl_event = CreateEventW(NULL, FALSE, FALSE, NULL);

    if ((excl_event == NULL) || FAILED(excl_client->SetEventHandle(excl_event)) ||
        FAILED(excl_client->GetBufferSize(&excl_frames)) ||
        FAILED(excl_client->GetService(__uuidof(IAudioRenderClient), (void **)&excl_render)))
    {
        release_all();
        return 0;
    }

    excl_srcCount = 0;
    excl_srcPos = 0;
    excl_frac = 0;
    excl_step = (unsigned int)(((unsigned long long)srcRate << 16) / excl_fmt.Format.nSamplesPerSec);

    /* a first buffer before starting */
    if (!fill_buffer() || FAILED(excl_client->Start()))
    {
        release_all();
        return 0;
    }

    excl_thread = CreateThread(NULL, 0, excl_proc, NULL, 0, NULL);

    if (excl_thread == NULL)
    {
        excl_client->Stop();
        release_all();
        return 0;
    }

    printf("[rewraithed] exclusive audio: %lu Hz, %s, %u frames\n", (unsigned long)excl_fmt.Format.nSamplesPerSec,
        (excl_kind == 0) ? "16-bit" : ((excl_kind == 1) ? "24-bit" : "float"), excl_frames);

    return 1;
}

extern "C" void port_excl_stop(void)
{
    if (excl_thread == NULL)
    {
        return;
    }

    InterlockedExchange(&excl_stop, 1);
    SetEvent(excl_event);
    WaitForSingleObject(excl_thread, INFINITE);
    CloseHandle(excl_thread);
    excl_thread = NULL;

    excl_client->Stop();
    release_all();
}

/* 1 if the output stopped on its own (the device went away) */
extern "C" int port_excl_dead(void)
{
    return excl_dead != 0;
}
