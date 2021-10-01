/*
 * MPEG-I Decoder filters
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

WINE_DEFAULT_DEBUG_CHANNEL(quartz);

struct empty_decoder
{
    struct strmbase_filter filter;
    struct strmbase_source source;
    struct strmbase_sink sink;
    AM_MEDIA_TYPE mt;
    IQualityControl IQualityControl_iface;
    SourceSeeking seek;
};

#pragma region IQualityControl

static inline struct empty_decoder *impl_from_IQualityControl(IQualityControl *iface)
{
    return CONTAINING_RECORD(iface, struct empty_decoder, IQualityControl_iface);
}

static HRESULT WINAPI QualityControl_QueryInterface(IQualityControl *iface, REFIID riid, void **ppv)
{
    struct empty_decoder *filter = impl_from_IQualityControl(iface);
    return IPin_QueryInterface(&filter->source.pin.IPin_iface, riid, ppv);
}

static ULONG WINAPI QualityControl_AddRef(IQualityControl *iface)
{
    struct empty_decoder *filter = impl_from_IQualityControl(iface);
    return IPin_AddRef(&filter->source.pin.IPin_iface);
}

static ULONG WINAPI QualityControl_Release(IQualityControl *iface)
{
    struct empty_decoder *filter = impl_from_IQualityControl(iface);
    return IPin_Release(&filter->source.pin.IPin_iface);
}

static HRESULT WINAPI QualityControl_Notify(IQualityControl *iface, IBaseFilter *sender, Quality q)
{
    return S_OK;
}

static HRESULT WINAPI QualityControl_SetSink(IQualityControl *iface, IQualityControl *tonotify)
{
    return S_OK;
}

static const IQualityControlVtbl OutPin_QualityControl_Vtbl = {
    QualityControl_QueryInterface,
    QualityControl_AddRef,
    QualityControl_Release,
    QualityControl_Notify,
    QualityControl_SetSink
};

#pragma endregion

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

static inline struct empty_decoder *impl_from_IMediaSeeking(IMediaSeeking *iface)
{
    return CONTAINING_RECORD(iface, struct empty_decoder, seek.IMediaSeeking_iface);
}

static HRESULT WINAPI Seeking_QueryInterface(IMediaSeeking *iface, REFIID riid, void **ppv)
{
    struct empty_decoder *This = impl_from_IMediaSeeking(iface);
    return IPin_QueryInterface(&This->source.pin.IPin_iface, riid, ppv);
}

static ULONG WINAPI Seeking_AddRef(IMediaSeeking *iface)
{
    struct empty_decoder *This = impl_from_IMediaSeeking(iface);
    return IPin_AddRef(&This->source.pin.IPin_iface);
}

static ULONG WINAPI Seeking_Release(IMediaSeeking *iface)
{
    struct empty_decoder *This = impl_from_IMediaSeeking(iface);
    return IPin_Release(&This->source.pin.IPin_iface);
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

static struct empty_decoder *impl_from_strmbase_filter(struct strmbase_filter *iface)
{
    return CONTAINING_RECORD(iface, struct empty_decoder, filter);
}

static struct strmbase_pin *empty_decoder_get_pin(struct strmbase_filter *iface, unsigned int index)
{
    struct empty_decoder *filter = impl_from_strmbase_filter(iface);

    if (index == 0)
        return &filter->sink.pin;
    else if (index == 1)
        return &filter->source.pin;
    return NULL;
}

static void empty_decoder_destroy(struct strmbase_filter *iface)
{
    struct empty_decoder *filter = impl_from_strmbase_filter(iface);

    if (filter->sink.pin.peer)
        IPin_Disconnect(filter->sink.pin.peer);
    IPin_Disconnect(&filter->sink.pin.IPin_iface);

    if (filter->source.pin.peer)
        IPin_Disconnect(filter->source.pin.peer);
    IPin_Disconnect(&filter->source.pin.IPin_iface);

    strmbase_sink_cleanup(&filter->sink);
    strmbase_source_cleanup(&filter->source);
    strmbase_filter_cleanup(&filter->filter);
    free(filter);
}

static HRESULT empty_decoder_init_stream(struct strmbase_filter *iface)
{
    struct empty_decoder *filter = impl_from_strmbase_filter(iface);

    return S_OK;
}

static HRESULT empty_decoder_cleanup_stream(struct strmbase_filter *iface)
{
    struct empty_decoder *filter = impl_from_strmbase_filter(iface);

    return S_OK;
}

static const struct strmbase_filter_ops empty_decoder_filter_ops =
{
        .filter_get_pin = empty_decoder_get_pin,
        .filter_destroy = empty_decoder_destroy,
        .filter_init_stream = empty_decoder_init_stream,
        .filter_cleanup_stream = empty_decoder_cleanup_stream,
};

static HRESULT empty_decoder_sink_query_interface(struct strmbase_pin *iface, REFIID iid, void **out)
{
    struct empty_decoder *filter = impl_from_strmbase_filter(iface->filter);

    if (IsEqualGUID(iid, &IID_IMemInputPin))
        *out = &filter->sink.IMemInputPin_iface;
    else
        return E_NOINTERFACE;

    IUnknown_AddRef((IUnknown * ) * out);
    return S_OK;
}

static HRESULT empty_decoder_sink_query_accept(struct strmbase_pin *iface, const AM_MEDIA_TYPE *mt)
{
    TRACE(" ---------------- sink_query_accept mt\n");
    strmbase_dump_media_type(mt);
    return S_OK;
}

static HRESULT empty_decoder_sink_connect(struct strmbase_sink *iface, IPin *peer, const AM_MEDIA_TYPE *pmt)
{
    struct empty_decoder *filter = impl_from_strmbase_filter(iface->pin.filter);
    TRACE(" ---------------- copy mt\n");
    strmbase_dump_media_type(pmt);
    CopyMediaType(&filter->mt, pmt);

    return S_OK;
}

static void empty_decoder_sink_disconnect(struct strmbase_sink *iface)
{
}

static HRESULT WINAPI empty_decoder_sink_Receive(struct strmbase_sink *iface, IMediaSample *pSample)
{
    TRACE("------------ sink_RECEIVE-----\n");
    return S_OK;
}

static const struct strmbase_sink_ops empty_decoder_sink_ops =
{
        .base.pin_query_interface = empty_decoder_sink_query_interface,
        .base.pin_query_accept = empty_decoder_sink_query_accept,
        .pfnReceive = empty_decoder_sink_Receive,
        .sink_connect = empty_decoder_sink_connect,
        .sink_disconnect = empty_decoder_sink_disconnect,
};

static HRESULT empty_decoder_source_query_interface(struct strmbase_pin *iface, REFIID iid, void **out)
{
    struct empty_decoder *filter = impl_from_strmbase_filter(iface->filter);

    if (IsEqualGUID(iid, &IID_IMediaSeeking))
        *out = &filter->seek.IMediaSeeking_iface;
    else if (IsEqualGUID(iid, &IID_IQualityControl))
        *out = &filter->IQualityControl_iface;
    else
        return E_NOINTERFACE;

    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static HRESULT empty_decoder_source_query_accept(struct strmbase_pin *iface, const AM_MEDIA_TYPE *mt)
{
    // TODO:
    return S_OK;
}

static HRESULT empty_decoder_source_get_media_type(struct strmbase_pin *iface, unsigned int index, AM_MEDIA_TYPE *mt)
{
    struct empty_decoder *filter = impl_from_strmbase_filter(iface->filter);
    struct wg_format format;

    amt_to_wg_format(&filter->mt, &format);
    memset(mt, 0, sizeof(AM_MEDIA_TYPE));

    if (format.major_type == WG_MAJOR_TYPE_VIDEO)
    {
        static const enum wg_video_format video_formats[] =
        {
            /* Try to prefer YUV formats over RGB ones. Most decoders output in the
             * YUV color space, and it's generally much less expensive for
             * videoconvert to do YUV -> YUV transformations. */
            WG_VIDEO_FORMAT_AYUV,
            WG_VIDEO_FORMAT_I420,
            WG_VIDEO_FORMAT_YV12,
            WG_VIDEO_FORMAT_YUY2,
            WG_VIDEO_FORMAT_UYVY,
            WG_VIDEO_FORMAT_YVYU,
            WG_VIDEO_FORMAT_NV12,
            WG_VIDEO_FORMAT_BGRA,
            WG_VIDEO_FORMAT_BGRx,
            WG_VIDEO_FORMAT_BGR,
            WG_VIDEO_FORMAT_RGB16,
            WG_VIDEO_FORMAT_RGB15,
        };
        if (index < ARRAY_SIZE(video_formats))
        {
            format.u.video.format = video_formats[index];
            if (!amt_from_wg_format(mt, &format, false))
                return E_OUTOFMEMORY;
            return S_OK;
        }
    }
    else if (format.major_type == WG_MAJOR_TYPE_AUDIO && index == 0)
    {
        if (!amt_from_wg_format(mt, &format, false))
            return E_OUTOFMEMORY;
        return S_OK;
    }

    return VFW_S_NO_MORE_ITEMS;
}

static HRESULT WINAPI empty_decoder_source_DecideBufferSize(struct strmbase_source *iface,
        IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *ppropInputRequest)
{
    return S_OK;
}

static const struct strmbase_source_ops empty_decoder_source_ops =
{
        .base.pin_query_interface = empty_decoder_source_query_interface,
        .base.pin_query_accept = empty_decoder_source_query_accept,
        .base.pin_get_media_type = empty_decoder_source_get_media_type,
        .pfnAttemptConnection = BaseOutputPinImpl_AttemptConnection,
        .pfnDecideAllocator = BaseOutputPinImpl_DecideAllocator,
        .pfnDecideBufferSize = empty_decoder_source_DecideBufferSize,
};

HRESULT mpeg_video_decoder_create(IUnknown *outer, IUnknown **out)
{
    struct empty_decoder *object;

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    strmbase_filter_init(&object->filter, outer, &CLSID_CMpegVideoCodec, &empty_decoder_filter_ops);
    strmbase_sink_init(&object->sink, &object->filter, L"Input", &empty_decoder_sink_ops, NULL);
    strmbase_source_init(&object->source, &object->filter, L"Output", &empty_decoder_source_ops);
    object->IQualityControl_iface.lpVtbl = &OutPin_QualityControl_Vtbl;

    TRACE("Created MPEG Video Decoder %p.\n", object);
    *out = &object->filter.IUnknown_inner;
    return S_OK;
}

HRESULT mpeg_audio_decoder_create(IUnknown *outer, IUnknown **out)
{
    struct empty_decoder *object;

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    strmbase_filter_init(&object->filter, outer, &CLSID_CMpegAudioCodec, &empty_decoder_filter_ops);
    strmbase_sink_init(&object->sink, &object->filter, L"XForm In", &empty_decoder_sink_ops, NULL);
    strmbase_source_init(&object->source, &object->filter, L"XForm Out", &empty_decoder_source_ops);
    object->IQualityControl_iface.lpVtbl = &OutPin_QualityControl_Vtbl;
    if (strmbase_seeking_init(&object->seek, &Seeking_Vtbl, ChangeStop, ChangeCurrent, ChangeRate) != S_OK)
        return E_OUTOFMEMORY;

    TRACE("Created MPEG Audio Decoder %p.\n", object);
    *out = &object->filter.IUnknown_inner;
    return S_OK;
}
