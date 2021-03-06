/*
 * Null renderer filter
 *
 * Copyright 2004 Christian Costa
 * Copyright 2008 Maarten Lankhorst
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

#define COBJMACROS
#include "dshow.h"
#include "wine/debug.h"
#include "wine/strmbase.h"

WINE_DEFAULT_DEBUG_CHANNEL(qedit);

typedef struct NullRendererImpl
{
    BaseRenderer renderer;
    IUnknown IUnknown_inner;
    IUnknown *outer_unk;
} NullRendererImpl;

static HRESULT WINAPI NullRenderer_DoRenderSample(BaseRenderer *iface, IMediaSample *pMediaSample)
{
    return S_OK;
}

static HRESULT WINAPI NullRenderer_CheckMediaType(BaseRenderer *iface, const AM_MEDIA_TYPE * pmt)
{
    TRACE("Not a stub!\n");
    return S_OK;
}

static const BaseRendererFuncTable RendererFuncTable = {
    NullRenderer_CheckMediaType,
    NullRenderer_DoRenderSample,
    /**/
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    /**/
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
};

static inline NullRendererImpl *impl_from_IUnknown(IUnknown *iface)
{
    return CONTAINING_RECORD(iface, NullRendererImpl, IUnknown_inner);
}

static HRESULT WINAPI NullRendererInner_QueryInterface(IUnknown *iface, REFIID riid, void **ppv)
{
    NullRendererImpl *This = impl_from_IUnknown(iface);

    TRACE("filter %p, iid %s, out %p.\n", This, debugstr_guid(riid), ppv);

    *ppv = NULL;

    if (IsEqualIID(riid, &IID_IUnknown))
        *ppv = &This->IUnknown_inner;
    else
    {
        HRESULT hr;
        hr = BaseRendererImpl_QueryInterface(&This->renderer.filter.IBaseFilter_iface, riid, ppv);
        if (SUCCEEDED(hr))
            return hr;
    }

    if (*ppv)
    {
        IUnknown_AddRef((IUnknown *)*ppv);
        return S_OK;
    }

    if (!IsEqualIID(riid, &IID_IPin) && !IsEqualIID(riid, &IID_IVideoWindow))
        FIXME("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));

    return E_NOINTERFACE;
}

static ULONG WINAPI NullRendererInner_AddRef(IUnknown *iface)
{
    NullRendererImpl *This = impl_from_IUnknown(iface);
    return BaseFilterImpl_AddRef(&This->renderer.filter.IBaseFilter_iface);
}

static ULONG WINAPI NullRendererInner_Release(IUnknown *iface)
{
    NullRendererImpl *This = impl_from_IUnknown(iface);
    ULONG refCount = InterlockedDecrement(&This->renderer.filter.refCount);

    if (!refCount)
    {
        TRACE("Destroying Null Renderer\n");
        strmbase_renderer_cleanup(&This->renderer);
        CoTaskMemFree(This);
    }

    return refCount;
}

static const IUnknownVtbl IInner_VTable =
{
    NullRendererInner_QueryInterface,
    NullRendererInner_AddRef,
    NullRendererInner_Release
};

static inline NullRendererImpl *impl_from_IBaseFilter(IBaseFilter *iface)
{
    return CONTAINING_RECORD(iface, NullRendererImpl, renderer.filter.IBaseFilter_iface);
}

static HRESULT WINAPI NullRenderer_QueryInterface(IBaseFilter * iface, REFIID riid, LPVOID * ppv)
{
    NullRendererImpl *This = impl_from_IBaseFilter(iface);
    return IUnknown_QueryInterface(This->outer_unk, riid, ppv);
}

static ULONG WINAPI NullRenderer_AddRef(IBaseFilter * iface)
{
    NullRendererImpl *This = impl_from_IBaseFilter(iface);
    return IUnknown_AddRef(This->outer_unk);
}

static ULONG WINAPI NullRenderer_Release(IBaseFilter * iface)
{
    NullRendererImpl *This = impl_from_IBaseFilter(iface);
    return IUnknown_Release(This->outer_unk);
}

static const IBaseFilterVtbl NullRenderer_Vtbl =
{
    NullRenderer_QueryInterface,
    NullRenderer_AddRef,
    NullRenderer_Release,
    BaseFilterImpl_GetClassID,
    BaseRendererImpl_Stop,
    BaseRendererImpl_Pause,
    BaseRendererImpl_Run,
    BaseRendererImpl_GetState,
    BaseRendererImpl_SetSyncSource,
    BaseFilterImpl_GetSyncSource,
    BaseFilterImpl_EnumPins,
    BaseFilterImpl_FindPin,
    BaseFilterImpl_QueryFilterInfo,
    BaseFilterImpl_JoinFilterGraph,
    BaseFilterImpl_QueryVendorInfo
};

HRESULT NullRenderer_create(IUnknown *pUnkOuter, void **ppv)
{
    static const WCHAR sink_name[] = {'I','n',0};

    HRESULT hr;
    NullRendererImpl *pNullRenderer;

    TRACE("(%p, %p)\n", pUnkOuter, ppv);

    *ppv = NULL;

    pNullRenderer = CoTaskMemAlloc(sizeof(NullRendererImpl));
    pNullRenderer->IUnknown_inner.lpVtbl = &IInner_VTable;

    if (pUnkOuter)
        pNullRenderer->outer_unk = pUnkOuter;
    else
        pNullRenderer->outer_unk = &pNullRenderer->IUnknown_inner;

    hr = strmbase_renderer_init(&pNullRenderer->renderer, &NullRenderer_Vtbl, pUnkOuter,
            &CLSID_NullRenderer, sink_name,
            (DWORD_PTR)(__FILE__ ": NullRendererImpl.csFilter"), &RendererFuncTable);

    if (FAILED(hr))
        CoTaskMemFree(pNullRenderer);
    else
        *ppv = &pNullRenderer->IUnknown_inner;

    return S_OK;
}
