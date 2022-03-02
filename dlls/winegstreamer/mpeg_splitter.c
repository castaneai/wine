/* WMA Decoder DMO / MF Transform
 *
 * Copyright 2022 Rémi Bernon for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "gst_private.h"
#include "gst_guids.h"

#include "amvideo.h"

#include "wine/debug.h"
#include "wine/heap.h"

WINE_DEFAULT_DEBUG_CHANNEL(quartz);

// output pin
struct mpeg_splitter_source
{
    bool is_video;
    struct strmbase_source pin;
    IQualityControl IQualityControl_iface;
    SourceSeeking seek;
};

#pragma region IMediaSeeking

static HRESULT WINAPI ChangeCurrent(IMediaSeeking *iface)
{
    return S_OK;
}

static HRESULT WINAPI ChangeStop(IMediaSeeking *iface)
{
    return S_OK;
}

static HRESULT WINAPI ChangeRate(IMediaSeeking *iface)
{
    return S_OK;
}

static inline struct mpeg_splitter_source *impl_from_IMediaSeeking(IMediaSeeking *iface)
{
    return CONTAINING_RECORD(iface, struct mpeg_splitter_source, seek.IMediaSeeking_iface);
}

static HRESULT WINAPI Seeking_QueryInterface(IMediaSeeking *iface, REFIID riid, void **ppv)
{
    struct mpeg_splitter_source *This = impl_from_IMediaSeeking(iface);
    return IPin_QueryInterface(&This->pin.pin.IPin_iface, riid, ppv);
}

static ULONG WINAPI Seeking_AddRef(IMediaSeeking *iface)
{
    struct mpeg_splitter_source *This = impl_from_IMediaSeeking(iface);
    return IPin_AddRef(&This->pin.pin.IPin_iface);
}

static ULONG WINAPI Seeking_Release(IMediaSeeking *iface)
{
    struct mpeg_splitter_source *This = impl_from_IMediaSeeking(iface);
    return IPin_Release(&This->pin.pin.IPin_iface);
}

static HRESULT WINAPI Seeking_SetPositions(IMediaSeeking *iface, LONGLONG *current, DWORD current_flags, LONGLONG *stop, DWORD stop_flags)
{
    return S_OK;
}

static const IMediaSeekingVtbl Seeking_Vtbl =
{
    Seeking_QueryInterface,
    Seeking_AddRef,
    Seeking_Release,
    SourceSeekingImpl_GetCapabilities,
    SourceSeekingImpl_CheckCapabilities,
    SourceSeekingImpl_IsFormatSupported,
    SourceSeekingImpl_QueryPreferredFormat,
    SourceSeekingImpl_GetTimeFormat,
    SourceSeekingImpl_IsUsingTimeFormat,
    SourceSeekingImpl_SetTimeFormat,
    SourceSeekingImpl_GetDuration,
    SourceSeekingImpl_GetStopPosition,
    SourceSeekingImpl_GetCurrentPosition,
    SourceSeekingImpl_ConvertTimeFormat,
    Seeking_SetPositions,
    SourceSeekingImpl_GetPositions,
    SourceSeekingImpl_GetAvailable,
    SourceSeekingImpl_SetRate,
    SourceSeekingImpl_GetRate,
    SourceSeekingImpl_GetPreroll
};

#pragma endregion

struct mpeg_splitter
{
    struct strmbase_filter filter;
    IAMStreamSelect IAMStreamSelect_iface;

    struct strmbase_sink sink;
    bool streaming, sink_connected;

    struct mpeg_splitter_source **sources;
    unsigned int source_count;
};

#pragma region IAMStreamSelect

static struct mpeg_splitter *impl_from_IAMStreamSelect(IAMStreamSelect *iface)
{
    return CONTAINING_RECORD(iface, struct mpeg_splitter, IAMStreamSelect_iface);
}

static HRESULT WINAPI stream_select_QueryInterface(IAMStreamSelect *iface, REFIID iid, void **out)
{
    struct mpeg_splitter *filter = impl_from_IAMStreamSelect(iface);
    return IUnknown_QueryInterface(filter->filter.outer_unk, iid, out);
}

static ULONG WINAPI stream_select_AddRef(IAMStreamSelect *iface)
{
    struct mpeg_splitter *filter = impl_from_IAMStreamSelect(iface);
    return IUnknown_AddRef(filter->filter.outer_unk);
}

static ULONG WINAPI stream_select_Release(IAMStreamSelect *iface)
{
    struct mpeg_splitter *filter = impl_from_IAMStreamSelect(iface);
    return IUnknown_Release(filter->filter.outer_unk);
}

static HRESULT WINAPI stream_select_Count(IAMStreamSelect *iface, DWORD *count)
{
    FIXME("iface %p, count %p, stub!\n", iface, count);
    return E_NOTIMPL;
}

static HRESULT WINAPI stream_select_Info(IAMStreamSelect *iface, LONG index,
        AM_MEDIA_TYPE **mt, DWORD *flags, LCID *lcid, DWORD *group, WCHAR **name, IUnknown **object, IUnknown **unknown)
{
    FIXME("iface %p, index %ld, mt %p, flags %p, lcid %p, group %p, name %p, object %p, unknown %p, stub!\n",
    iface, index, mt, flags, lcid, group, name, object, unknown);
    return E_NOTIMPL;
}

static HRESULT WINAPI stream_select_Enable(IAMStreamSelect *iface, LONG index, DWORD flags)
{
    FIXME("iface %p, index %ld, flags %#lx, stub!\n", iface, index, flags);
    return E_NOTIMPL;
}

static const IAMStreamSelectVtbl stream_select_vtbl =
{
    stream_select_QueryInterface,
    stream_select_AddRef,
    stream_select_Release,
    stream_select_Count,
    stream_select_Info,
    stream_select_Enable,
};

#pragma endregion

#pragma region filter

static inline struct mpeg_splitter *impl_from_strmbase_filter(struct strmbase_filter *iface)
{
    return CONTAINING_RECORD(iface, struct mpeg_splitter, filter);
}

static HRESULT mpeg_splitter_query_interface(struct strmbase_filter *iface, REFIID iid, void **out)
{
    struct mpeg_splitter *filter = impl_from_strmbase_filter(iface);

    if (IsEqualGUID(iid, &IID_IAMStreamSelect))
    {
        *out = &filter->IAMStreamSelect_iface;
        IUnknown_AddRef((IUnknown *)*out);
        return S_OK;
    }

    return E_NOINTERFACE;
}

static struct strmbase_pin *mpeg_splitter_get_pin(struct strmbase_filter *iface, unsigned int index)
{
    struct mpeg_splitter *filter = impl_from_strmbase_filter(iface);

    if (index == 0)
        return &filter->sink.pin;
    else if (index <= filter->source_count)
        return &filter->sources[index - 1]->pin.pin;

    return NULL;
}

static void mpeg_splitter_destroy(struct strmbase_filter *iface)
{
    struct mpeg_splitter *filter = impl_from_strmbase_filter(iface);
    HRESULT hr;

    /* Don't need to clean up output pins, disconnecting input pin will do that */
    if (filter->sink.pin.peer)
    {
        hr = IPin_Disconnect(filter->sink.pin.peer);
        assert(hr == S_OK);
        hr = IPin_Disconnect(&filter->sink.pin.IPin_iface);
        assert(hr == S_OK);
    }

    strmbase_sink_cleanup(&filter->sink);
    strmbase_filter_cleanup(&filter->filter);
    free(filter);
}

static HRESULT mpeg_splitter_init_stream(struct strmbase_filter *iface)
{
    struct mpeg_splitter *filter = impl_from_strmbase_filter(iface);
    DWORD stop_flags = AM_SEEKING_NoPositioning;
    const SourceSeeking *seeking;
    unsigned int i;

    if (!filter->sink_connected)
        return S_OK;

    filter->streaming = true;

    /* DirectShow retains the old seek positions, but resets to them every time
     * it transitions from stopped -> paused. */

    seeking = &filter->sources[0]->seek;
    if (seeking->llStop)
        stop_flags = AM_SEEKING_AbsolutePositioning;
    // TODO: seek

    for (i = 0; i < filter->source_count; ++i)
    {
        struct mpeg_splitter_source *pin = filter->sources[i];
        HRESULT hr;

        if (!pin->pin.pin.peer)
            continue;

        if (FAILED(hr = IMemAllocator_Commit(pin->pin.pAllocator)))
            ERR("Failed to commit allocator, hr %#lx.\n", hr);

        // TODO: start streaming
    }

    return S_OK;
}

static HRESULT mpeg_splitter_cleanup_stream(struct strmbase_filter *iface)
{
    struct mpeg_splitter *filter = impl_from_strmbase_filter(iface);
    unsigned int i;

    if (!filter->sink_connected)
        return S_OK;

    filter->streaming = false;

    for (i = 0; i < filter->source_count; ++i)
    {
        struct mpeg_splitter_source *pin = filter->sources[i];

        if (!pin->pin.pin.peer)
            continue;

        IMemAllocator_Decommit(pin->pin.pAllocator);

        // TODO: stop streaming
    }

    return S_OK;
}

static const struct strmbase_filter_ops mpeg_splitter_ops =
{
    .filter_query_interface = mpeg_splitter_query_interface,
    .filter_get_pin = mpeg_splitter_get_pin,
    .filter_destroy = mpeg_splitter_destroy,
    .filter_init_stream = mpeg_splitter_init_stream,
    .filter_cleanup_stream = mpeg_splitter_cleanup_stream,
};

#pragma endregion

#pragma region source (output pin)

static inline struct mpeg_splitter_source *impl_source_from_IPin(IPin *iface)
{
    return CONTAINING_RECORD(iface, struct mpeg_splitter_source, pin.pin.IPin_iface);
}

static HRESULT mpeg_splitter_source_query_interface(struct strmbase_pin *iface, REFIID iid, void **out)
{
    struct mpeg_splitter_source *pin = impl_source_from_IPin(&iface->IPin_iface);

    if (IsEqualGUID(iid, &IID_IMediaSeeking))
        *out = &pin->seek.IMediaSeeking_iface;
    else if (IsEqualGUID(iid, &IID_IQualityControl))
        *out = &pin->IQualityControl_iface;
    else
        return E_NOINTERFACE;

    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static HRESULT mpeg_splitter_source_query_accept(struct strmbase_pin *pin, const AM_MEDIA_TYPE *mt)
{
    // TODO: mock
    return S_OK;
}

static HRESULT mpeg_splitter_source_get_media_type(struct strmbase_pin *iface, unsigned int index, AM_MEDIA_TYPE *mt)
{
    struct mpeg_splitter_source *pin = impl_source_from_IPin(&iface->IPin_iface);

    // TODO: mock
    memset(mt, 0, sizeof(AM_MEDIA_TYPE));
    if (pin->is_video)
    {
        // video
        VIDEOINFO *video_format;
        LONG width = 320, height = 240;
        if (!(video_format = CoTaskMemAlloc(sizeof(*video_format))))
            return E_OUTOFMEMORY;
        memset(video_format, 0, sizeof(*video_format));
        video_format->AvgTimePerFrame = 100;
        video_format->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        video_format->bmiHeader.biWidth = width;
        video_format->bmiHeader.biHeight = height;
        video_format->rcSource.right = width;
        video_format->rcSource.bottom = height;
        video_format->rcTarget = video_format->rcSource;
        video_format->bmiHeader.biPlanes = 1;
        video_format->bmiHeader.biBitCount = 32;
        video_format->bmiHeader.biCompression = BI_RGB;
        video_format->bmiHeader.biSizeImage = width * height * 4;

        mt->majortype = MEDIATYPE_Video;
        mt->subtype = MEDIASUBTYPE_RGB32;
        mt->bFixedSizeSamples = TRUE;
        mt->lSampleSize = 1;
        mt->formattype = FORMAT_VideoInfo;
        mt->cbFormat = sizeof(VIDEOINFOHEADER);
        mt->pbFormat = (BYTE *)video_format;
        return S_OK;
    }
    else
    {
        // audio
        WAVEFORMATEX *audio_format;
        if (!(audio_format = CoTaskMemAlloc(sizeof(*audio_format))))
            return E_OUTOFMEMORY;
        memset(audio_format, 0, sizeof(*audio_format));

        audio_format->wFormatTag = WAVE_FORMAT_PCM,
        audio_format->nChannels = 1,
        audio_format->nSamplesPerSec = 11025,
        audio_format->wBitsPerSample = 16,
        audio_format->nBlockAlign = 2,
        audio_format->nAvgBytesPerSec = 2 * 11025,

        mt->majortype = MEDIATYPE_Audio;
        mt->subtype = WMMEDIASUBTYPE_PCM;
        mt->formattype = FORMAT_WaveFormatEx;
        mt->pUnk = NULL;
        mt->cbFormat = sizeof(WAVEFORMATEX);
        mt->pbFormat = (BYTE *)audio_format;
        return S_OK;
    }

    return VFW_S_NO_MORE_ITEMS;
}

static HRESULT WINAPI mpeg_splitter_source_DecideBufferSize(struct strmbase_source *iface,
        IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *ppropInputRequest)
{
    // TODO: mock
    return S_OK;
}

static void mpeg_splitter_source_disconnect(struct strmbase_source *iface)
{
}

static const struct strmbase_source_ops mpeg_splitter_source_ops =
{
    .base.pin_query_interface = mpeg_splitter_source_query_interface,
    .base.pin_query_accept = mpeg_splitter_source_query_accept,
    .base.pin_get_media_type = mpeg_splitter_source_get_media_type,
    .pfnAttemptConnection = BaseOutputPinImpl_AttemptConnection,
    .pfnDecideAllocator = BaseOutputPinImpl_DecideAllocator,
    .pfnDecideBufferSize = mpeg_splitter_source_DecideBufferSize,
    .source_disconnect = mpeg_splitter_source_disconnect,
};

static struct mpeg_splitter_source *create_output_pin(struct mpeg_splitter *filter,
        const WCHAR *name, bool is_video)
{
    struct mpeg_splitter_source *pin, **new_array;

    if (!(new_array = realloc(filter->sources, (filter->source_count + 1) * sizeof(*filter->sources))))
        return NULL;
    filter->sources = new_array;

    if (!(pin= calloc(1, sizeof(*pin))))
        return NULL;

    pin->is_video = is_video;
    strmbase_source_init(&pin->pin, &filter->filter, name, &mpeg_splitter_source_ops);
    if (strmbase_seeking_init(&pin->seek, &Seeking_Vtbl, ChangeStop, ChangeCurrent, ChangeRate) != S_OK)
        return E_OUTOFMEMORY;

    filter->sources[filter->source_count++] = pin;
    return pin;
}

#pragma endregion

#pragma region sink (input pin)

static inline struct mpeg_splitter *impl_from_strmbase_sink(struct strmbase_sink *iface)
{
    return CONTAINING_RECORD(iface, struct mpeg_splitter, sink);
}

static HRESULT mpeg_splitter_sink_query_interface(struct strmbase_pin *iface, REFIID iid, void **out)
{
    struct mpeg_splitter *filter = impl_from_strmbase_sink(iface);

    if (IsEqualGUID(iid, &IID_IMemInputPin))
        *out = &filter->sink.IMemInputPin_iface;
    else
        return E_NOINTERFACE;

    IUnknown_AddRef((IUnknown * ) * out);
    return S_OK;
}

static HRESULT mpeg_splitter_sink_query_accept(struct strmbase_pin *iface, const AM_MEDIA_TYPE *mt)
{
    if (!IsEqualGUID(&mt->majortype, &MEDIATYPE_Stream))
        return S_FALSE;
    if (IsEqualGUID(&mt->subtype, &MEDIASUBTYPE_MPEG1Audio))
        return S_OK;
    if (IsEqualGUID(&mt->subtype, &MEDIASUBTYPE_MPEG1System))
        return S_OK;
    if (IsEqualGUID(&mt->subtype, &MEDIASUBTYPE_MPEG1Video)
        || IsEqualGUID(&mt->subtype, &MEDIASUBTYPE_MPEG1VideoCD))
        FIXME("Unsupported subtype %s.\n", wine_dbgstr_guid(&mt->subtype));
    return S_FALSE;
}

static HRESULT mpeg_splitter_sink_connect(struct strmbase_sink *iface, IPin *peer, const AM_MEDIA_TYPE *pmt)
{
    struct mpeg_splitter *filter = impl_from_strmbase_sink(iface);
    HRESULT hr = S_OK;
    unsigned int i;

    filter->sink_connected = true;

    create_output_pin(filter, L"Video", TRUE);
    create_output_pin(filter, L"Audio", FALSE);

    for (i = 0; i < filter->source_count; ++i)
    {
        struct mpeg_splitter_source *pin = filter->sources[i];

        // TODO: get duration
        // pin->seek.llDuration = pin->seek.llStop = wg_parser_stream_get_duration(pin->wg_stream);
        pin->seek.llCurrent = 0;
    }

    return hr;
}

static void mpeg_splitter_sink_disconnect(struct strmbase_sink *iface)
{
}

static HRESULT WINAPI mpeg_splitter_sink_Receive(struct strmbase_sink *iface, IMediaSample *pSample)
{
    TRACE("------------ sink_RECEIVE-----\n");
    return S_OK;
}

static const struct strmbase_sink_ops mpeg_splitter_sink_ops =
{
    .base.pin_query_interface = mpeg_splitter_sink_query_interface,
    .base.pin_query_accept = mpeg_splitter_sink_query_accept,
    .sink_connect = mpeg_splitter_sink_connect,
    .sink_disconnect = mpeg_splitter_sink_disconnect,
    .pfnReceive =  mpeg_splitter_sink_Receive,
};

#pragma endregion

HRESULT mpeg_splitter_create(IUnknown *outer, IUnknown **out)
{
    struct mpeg_splitter *splitter;

    TRACE("outer %p, out %p.\n", outer, out);

    if (!(splitter = calloc(1, sizeof(*splitter))))
        return E_OUTOFMEMORY;

    strmbase_filter_init(&splitter->filter, outer, &CLSID_MPEG1Splitter, &mpeg_splitter_ops);
    strmbase_sink_init(&splitter->sink, &splitter->filter, L"Input", &mpeg_splitter_sink_ops, NULL);
    splitter->source_count = 0;

    TRACE("Created MPEG-1 splitter %p.\n", splitter);
    *out = &splitter->filter.IUnknown_inner;
    return S_OK;
}
