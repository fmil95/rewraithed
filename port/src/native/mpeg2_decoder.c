/*
 * MPEG-2 video through Windows' own decoder (Media Foundation's MPEG-2
 * decoder transform), for sr2_movie.c: an elementary stream goes in, R, G, B
 * pictures come out in display order.
 */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <mftransform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    IMFTransform *mft;
    IMFSample *out;         /* ours, when the decoder doesn't give its own */
    int providesSamples;
    long outSize;
    int width, height;      /* the pictures */
    int planeHeight;        /* the decoder's rows of Y before the chroma */
    int comStarted;
    int mfStarted;
} Mpeg2;

static int pick_output(Mpeg2 *m)
{
    MFT_OUTPUT_STREAM_INFO info;
    DWORD i;

    for (i = 0;; i++)
    {
        IMFMediaType *type = NULL;
        GUID subtype;
        UINT64 size = 0;

        if (FAILED(IMFTransform_GetOutputAvailableType(m->mft, 0, i, &type)))
        {
            return 0;
        }

        if (SUCCEEDED(IMFMediaType_GetGUID(type, &MF_MT_SUBTYPE, &subtype)) && IsEqualGUID(&subtype, &MFVideoFormat_NV12) &&
            SUCCEEDED(IMFTransform_SetOutputType(m->mft, 0, type, 0)))
        {
            if (SUCCEEDED(IMFMediaType_GetUINT64(type, &MF_MT_FRAME_SIZE, &size)))
            {
                m->planeHeight = (int)(size & 0xFFFFFFFF);
            }

            IMFMediaType_Release(type);
            break;
        }

        IMFMediaType_Release(type);
    }

    if (m->planeHeight < m->height)
    {
        m->planeHeight = m->height;
    }

    memset(&info, 0, sizeof(info));
    IMFTransform_GetOutputStreamInfo(m->mft, 0, &info);
    m->providesSamples = (info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0;

    if (m->out != NULL)
    {
        IMFSample_Release(m->out);
        m->out = NULL;
    }

    if (!m->providesSamples)
    {
        IMFMediaBuffer *buffer = NULL;

        m->outSize = (long)((info.cbSize > 0) ? info.cbSize : (DWORD)(m->width * m->planeHeight * 3 / 2 + 4096));

        if (FAILED(MFCreateSample(&m->out)) || FAILED(MFCreateMemoryBuffer((DWORD)m->outSize, &buffer)))
        {
            return 0;
        }

        IMFSample_AddBuffer(m->out, buffer);
        IMFMediaBuffer_Release(buffer);
    }

    return 1;
}

void port_mpeg2_close(void *decoder);

void *port_mpeg2_open(int width, int height, char *error, size_t errorSize)
{
    Mpeg2 *m = (Mpeg2 *)calloc(1, sizeof(Mpeg2));
    MFT_REGISTER_TYPE_INFO in = { MFMediaType_Video, MFVideoFormat_MPEG2 };
    IMFActivate **activates = NULL;
    UINT32 count = 0;
    IMFMediaType *type = NULL;
    UINT32 i;
    HRESULT hr;

    if (m == NULL)
    {
        snprintf(error, errorSize, "no memory");
        return NULL;
    }

    m->width = width;
    m->height = height;
    m->comStarted = SUCCEEDED(CoInitializeEx(NULL, COINIT_MULTITHREADED));

    m->mfStarted = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));

    if (!m->mfStarted)
    {
        snprintf(error, errorSize, "Media Foundation is not available");
        port_mpeg2_close(m);
        return NULL;
    }

    hr = MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER, MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER,
        &in, NULL, &activates, &count);

    if (SUCCEEDED(hr) && (count > 0))
    {
        hr = IMFActivate_ActivateObject(activates[0], &IID_IMFTransform, (void **)&m->mft);
    }

    for (i = 0; i < count; i++)
    {
        IMFActivate_Release(activates[i]);
    }

    CoTaskMemFree(activates);

    if ((count == 0) || FAILED(hr) || (m->mft == NULL))
    {
        snprintf(error, errorSize, "Windows has no MPEG-2 video decoder");
        port_mpeg2_close(m);
        return NULL;
    }

    if (FAILED(MFCreateMediaType(&type)))
    {
        snprintf(error, errorSize, "no memory");
        port_mpeg2_close(m);
        return NULL;
    }

    IMFMediaType_SetGUID(type, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    IMFMediaType_SetGUID(type, &MF_MT_SUBTYPE, &MFVideoFormat_MPEG2);
    IMFMediaType_SetUINT64(type, &MF_MT_FRAME_SIZE, ((UINT64)width << 32) | (UINT64)height);
    IMFMediaType_SetUINT64(type, &MF_MT_FRAME_RATE, ((UINT64)30000 << 32) | 1001);
    hr = IMFTransform_SetInputType(m->mft, 0, type, 0);
    IMFMediaType_Release(type);

    if (FAILED(hr) || !pick_output(m))
    {
        snprintf(error, errorSize, "the MPEG-2 decoder refuses this video (0x%08lx)", (unsigned long)hr);
        port_mpeg2_close(m);
        return NULL;
    }

    IMFTransform_ProcessMessage(m->mft, MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    IMFTransform_ProcessMessage(m->mft, MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);

    return m;
}

/* some of the stream; 0 if the decoder wants its pictures taken first */
int port_mpeg2_feed(void *decoder, const unsigned char *data, size_t size)
{
    Mpeg2 *m = (Mpeg2 *)decoder;
    IMFMediaBuffer *buffer = NULL;
    IMFSample *sample = NULL;
    BYTE *p = NULL;
    HRESULT hr;

    if (FAILED(MFCreateMemoryBuffer((DWORD)size, &buffer)))
    {
        return 1;
    }

    IMFMediaBuffer_Lock(buffer, &p, NULL, NULL);
    memcpy(p, data, size);
    IMFMediaBuffer_Unlock(buffer);
    IMFMediaBuffer_SetCurrentLength(buffer, (DWORD)size);
    MFCreateSample(&sample);
    IMFSample_AddBuffer(sample, buffer);
    hr = IMFTransform_ProcessInput(m->mft, 0, sample, 0);
    IMFSample_Release(sample);
    IMFMediaBuffer_Release(buffer);

    return hr != MF_E_NOTACCEPTING;
}

/* no more of the stream: the pictures held back come out */
void port_mpeg2_drain(void *decoder)
{
    Mpeg2 *m = (Mpeg2 *)decoder;

    IMFTransform_ProcessMessage(m->mft, MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
    IMFTransform_ProcessMessage(m->mft, MFT_MESSAGE_COMMAND_DRAIN, 0);
}

static int clamp(int v)
{
    return (v < 0) ? 0 : ((v > 255) ? 255 : v);
}

/* BT.601 studio range: the MPEG-2 video's own */
static void nv12_to_rgb(const BYTE *y, const BYTE *uv, LONG pitch, int w, int h, unsigned char *rgb)
{
    int row, col;

    for (row = 0; row < h; row++)
    {
        const BYTE *yr = y + (size_t)row * pitch;
        const BYTE *uvr = uv + (size_t)(row / 2) * pitch;
        unsigned char *o = rgb + (size_t)row * w * 3;

        for (col = 0; col < w; col++)
        {
            int c = 298 * (yr[col] - 16);
            int d = uvr[col & ~1] - 128;
            int e = uvr[(col & ~1) + 1] - 128;

            o[0] = (unsigned char)clamp((c + 409 * e + 128) >> 8);
            o[1] = (unsigned char)clamp((c - 100 * d - 208 * e + 128) >> 8);
            o[2] = (unsigned char)clamp((c + 516 * d + 128) >> 8);
            o += 3;
        }
    }
}

/* the next picture as R, G, B bytes (width x height): 1, or 0 if it needs
 * more of the stream (or, drained, has no more) */
int port_mpeg2_picture(void *decoder, unsigned char *rgb)
{
    Mpeg2 *m = (Mpeg2 *)decoder;

    for (;;)
    {
        MFT_OUTPUT_DATA_BUFFER out;
        DWORD status = 0;
        HRESULT hr;
        IMFMediaBuffer *buffer = NULL;
        IMF2DBuffer *buffer2d = NULL;
        BYTE *scan = NULL;
        LONG pitch = 0;

        memset(&out, 0, sizeof(out));
        out.pSample = m->providesSamples ? NULL : m->out;

        if (!m->providesSamples)
        {
            IMFSample_RemoveAllBuffers(m->out);

            if (FAILED(MFCreateMemoryBuffer((DWORD)m->outSize, &buffer)))
            {
                return 0;
            }

            IMFSample_AddBuffer(m->out, buffer);
            IMFMediaBuffer_Release(buffer);
            buffer = NULL;
        }

        hr = IMFTransform_ProcessOutput(m->mft, 0, 1, &out, &status);

        if (out.pEvents != NULL)
        {
            IMFCollection_Release(out.pEvents);
        }

        if (hr == MF_E_TRANSFORM_STREAM_CHANGE)
        {
            if (!pick_output(m))
            {
                return 0;
            }

            continue;
        }

        if (FAILED(hr))
        {
            return 0;
        }

        if (SUCCEEDED(IMFSample_ConvertToContiguousBuffer(out.pSample, &buffer)))
        {
            if (SUCCEEDED(IMFMediaBuffer_QueryInterface(buffer, &IID_IMF2DBuffer, (void **)&buffer2d)) &&
                SUCCEEDED(IMF2DBuffer_Lock2D(buffer2d, &scan, &pitch)))
            {
                nv12_to_rgb(scan, scan + (size_t)pitch * m->planeHeight, pitch, m->width, m->height, rgb);
                IMF2DBuffer_Unlock2D(buffer2d);
            }
            else if (SUCCEEDED(IMFMediaBuffer_Lock(buffer, &scan, NULL, NULL)))
            {
                nv12_to_rgb(scan, scan + (size_t)m->width * m->planeHeight, m->width, m->width, m->height, rgb);
                IMFMediaBuffer_Unlock(buffer);
            }

            if (buffer2d != NULL)
            {
                IMF2DBuffer_Release(buffer2d);
            }

            IMFMediaBuffer_Release(buffer);
        }

        if (m->providesSamples)
        {
            IMFSample_Release(out.pSample);
        }

        return 1;
    }
}

void port_mpeg2_close(void *decoder)
{
    Mpeg2 *m = (Mpeg2 *)decoder;

    if (m == NULL)
    {
        return;
    }

    if (m->out != NULL)
    {
        IMFSample_Release(m->out);
    }

    if (m->mft != NULL)
    {
        IMFTransform_Release(m->mft);
    }

    if (m->mfStarted)
    {
        MFShutdown();
    }

    if (m->comStarted)
    {
        CoUninitialize();
    }

    free(m);
}
