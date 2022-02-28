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

struct mpeg_splitter_source
{
    struct strmbase_source pin;
    struct wg_transform *wg_transform;
    SourceSeeking seek;
};

struct mpeg_splitter
{
    struct strmbase_filter filter;
    IAMStreamSelect IAMStreamSelect_iface;

    struct strmbase_sink sink;
    bool streaming, sink_connected;

    struct mpeg_splitter_source **sources;
    unsigned int source_count;

};

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

#pragma region sink (input pin)

static inline struct mpeg_splitter *impl_from_strmbase_sink(struct strmbase_sink *iface)
{
    return CONTAINING_RECORD(iface, struct mpeg_splitter, sink);
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

    // TODO: connect wg_transorm
    TRACE("------------ source_count: %d\n", filter->source_count);

    for (i = 0; i < filter->source_count; ++i)
    {
        struct mpeg_splitter_source *pin = filter->sources[i];

        // TODO: get duration
        // pin->seek.llDuration = pin->seek.llStop = wg_parser_stream_get_duration(pin->wg_stream);
        pin->seek.llCurrent = 0;
    }

    return S_OK;
}

static void mpeg_splitter_sink_disconnect(struct strmbase_sink *iface)
{
}

static const struct strmbase_sink_ops mpeg_splitter_sink_ops =
{
    .base.pin_query_accept = mpeg_splitter_sink_query_accept,
    .sink_connect = mpeg_splitter_sink_connect,
    .sink_disconnect = mpeg_splitter_sink_disconnect,
};

#pragma endregion

#pragma region source (output pin)

static HRESULT mpeg_splitter_source_query_accept(struct mpeg_splitter_source *pin, const AM_MEDIA_TYPE *mt)
{
//    struct wg_format format;
//    AM_MEDIA_TYPE pad_mt;
//    HRESULT hr;
//
//    wg_parser_stream_get_preferred_format(pin->wg_stream, &format);
//    if (!amt_from_wg_format(&pad_mt, &format, false))
//        return E_OUTOFMEMORY;
//    hr = compare_media_types(mt, &pad_mt) ? S_OK : S_FALSE;
//    FreeMediaType(&pad_mt);
//    return hr;
    return S_OK;
}

static HRESULT mpeg_splitter_source_get_media_type(struct mpeg_splitter_source *pin,
                                                   unsigned int index, AM_MEDIA_TYPE *mt)
{
//    struct wg_format format;
//
//    if (index > 1)
//        return VFW_S_NO_MORE_ITEMS;
//    wg_parser_stream_get_preferred_format(pin->wg_stream, &format);
//    if (!amt_from_wg_format(mt, &format, false))
//        return E_OUTOFMEMORY;
    return S_OK;
}

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
