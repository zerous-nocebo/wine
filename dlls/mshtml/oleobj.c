/*
 * Copyright 2005 Jacek Caban
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

#include "config.h"

#include <stdarg.h>
#include <stdio.h>

#define COBJMACROS

#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "ole2.h"
#include "shlguid.h"
#include "mshtmdid.h"
#include "idispids.h"

#include "wine/debug.h"

#include "mshtml_private.h"
#include "initguid.h"

WINE_DEFAULT_DEBUG_CHANNEL(mshtml);

DEFINE_OLEGUID(CGID_DocHostCmdPriv, 0x000214D4L, 0, 0);
#define DOCHOST_DOCCANNAVIGATE  0

/**********************************************************
 * IOleObject implementation
 */

static inline HTMLDocument *impl_from_IOleObject(IOleObject *iface)
{
    return CONTAINING_RECORD(iface, HTMLDocument, IOleObject_iface);
}

static HRESULT WINAPI OleObject_QueryInterface(IOleObject *iface, REFIID riid, void **ppv)
{
    HTMLDocument *This = impl_from_IOleObject(iface);
    return htmldoc_query_interface(This, riid, ppv);
}

static ULONG WINAPI OleObject_AddRef(IOleObject *iface)
{
    HTMLDocument *This = impl_from_IOleObject(iface);
    return htmldoc_addref(This);
}

static ULONG WINAPI OleObject_Release(IOleObject *iface)
{
    HTMLDocument *This = impl_from_IOleObject(iface);
    return htmldoc_release(This);
}

static void update_hostinfo(HTMLDocumentObj *This, DOCHOSTUIINFO *hostinfo)
{
    nsIScrollable *scrollable;
    nsresult nsres;

    if(!This->nscontainer)
        return;

    nsres = nsIWebBrowser_QueryInterface(This->nscontainer->webbrowser, &IID_nsIScrollable, (void**)&scrollable);
    if(NS_SUCCEEDED(nsres)) {
        nsres = nsIScrollable_SetDefaultScrollbarPreferences(scrollable, ScrollOrientation_Y,
                (hostinfo->dwFlags & DOCHOSTUIFLAG_SCROLL_NO) ? Scrollbar_Never : Scrollbar_Always);
        if(NS_FAILED(nsres))
            ERR("Could not set default Y scrollbar prefs: %08x\n", nsres);

        nsres = nsIScrollable_SetDefaultScrollbarPreferences(scrollable, ScrollOrientation_X,
                hostinfo->dwFlags & DOCHOSTUIFLAG_SCROLL_NO ? Scrollbar_Never : Scrollbar_Auto);
        if(NS_FAILED(nsres))
            ERR("Could not set default X scrollbar prefs: %08x\n", nsres);

        nsIScrollable_Release(scrollable);
    }else {
        ERR("Could not get nsIScrollable: %08x\n", nsres);
    }
}

static HRESULT WINAPI OleObject_SetClientSite(IOleObject *iface, IOleClientSite *pClientSite)
{
    HTMLDocument *This = impl_from_IOleObject(iface);
    IOleCommandTarget *cmdtrg = NULL;
    IOleWindow *ole_window;
    BOOL hostui_setup;
    VARIANT silent;
    HWND hwnd;
    HRESULT hres;

    TRACE("(%p)->(%p)\n", This, pClientSite);

    if(pClientSite == This->doc_obj->client)
        return S_OK;

    if(This->doc_obj->client) {
        IOleClientSite_Release(This->doc_obj->client);
        This->doc_obj->client = NULL;
        This->doc_obj->usermode = UNKNOWN_USERMODE;
    }

    if(This->doc_obj->hostui && !This->doc_obj->custom_hostui) {
        IDocHostUIHandler_Release(This->doc_obj->hostui);
        This->doc_obj->hostui = NULL;
    }

    memset(&This->doc_obj->hostinfo, 0, sizeof(DOCHOSTUIINFO));

    if(!pClientSite)
        return S_OK;

    IOleClientSite_AddRef(pClientSite);
    This->doc_obj->client = pClientSite;

    hostui_setup = This->doc_obj->hostui_setup;

    if(!This->doc_obj->hostui) {
        IDocHostUIHandler *uihandler;

        This->doc_obj->custom_hostui = FALSE;

        hres = IOleObject_QueryInterface(pClientSite, &IID_IDocHostUIHandler, (void**)&uihandler);
        if(SUCCEEDED(hres))
            This->doc_obj->hostui = uihandler;
    }

    if(This->doc_obj->hostui) {
        DOCHOSTUIINFO hostinfo;
        LPOLESTR key_path = NULL, override_key_path = NULL;
        IDocHostUIHandler2 *uihandler2;

        memset(&hostinfo, 0, sizeof(DOCHOSTUIINFO));
        hostinfo.cbSize = sizeof(DOCHOSTUIINFO);
        hres = IDocHostUIHandler_GetHostInfo(This->doc_obj->hostui, &hostinfo);
        if(SUCCEEDED(hres)) {
            TRACE("hostinfo = {%u %08x %08x %s %s}\n",
                    hostinfo.cbSize, hostinfo.dwFlags, hostinfo.dwDoubleClick,
                    debugstr_w(hostinfo.pchHostCss), debugstr_w(hostinfo.pchHostNS));
            update_hostinfo(This->doc_obj, &hostinfo);
            This->doc_obj->hostinfo = hostinfo;
        }

        if(!hostui_setup) {
            hres = IDocHostUIHandler_GetOptionKeyPath(This->doc_obj->hostui, &key_path, 0);
            if(hres == S_OK && key_path) {
                if(key_path[0]) {
                    /* FIXME: use key_path */
                    TRACE("key_path = %s\n", debugstr_w(key_path));
                }
                CoTaskMemFree(key_path);
            }

            hres = IDocHostUIHandler_QueryInterface(This->doc_obj->hostui, &IID_IDocHostUIHandler2,
                    (void**)&uihandler2);
            if(SUCCEEDED(hres)) {
                hres = IDocHostUIHandler2_GetOverrideKeyPath(uihandler2, &override_key_path, 0);
                if(hres == S_OK && override_key_path && override_key_path[0]) {
                    if(override_key_path[0]) {
                        /*FIXME: use override_key_path */
                        TRACE("override_key_path = %s\n", debugstr_w(override_key_path));
                    }
                    CoTaskMemFree(override_key_path);
                }
                IDocHostUIHandler2_Release(uihandler2);
            }

            This->doc_obj->hostui_setup = TRUE;
        }
    }

    /* Native calls here GetWindow. What is it for?
     * We don't have anything to do with it here (yet). */
    hres = IOleClientSite_QueryInterface(pClientSite, &IID_IOleWindow, (void**)&ole_window);
    if(SUCCEEDED(hres)) {
        IOleWindow_GetWindow(ole_window, &hwnd);
        IOleWindow_Release(ole_window);
    }

    hres = IOleClientSite_QueryInterface(pClientSite, &IID_IOleCommandTarget, (void**)&cmdtrg);
    if(SUCCEEDED(hres)) {
        VARIANT var;
        OLECMD cmd = {OLECMDID_SETPROGRESSTEXT, 0};

        if(!hostui_setup) {
            V_VT(&var) = VT_UNKNOWN;
            V_UNKNOWN(&var) = (IUnknown*)HTMLWINDOW2(This->window);
            IOleCommandTarget_Exec(cmdtrg, &CGID_DocHostCmdPriv, DOCHOST_DOCCANNAVIGATE, 0, &var, NULL);
        }

        IOleCommandTarget_QueryStatus(cmdtrg, NULL, 1, &cmd, NULL);

        V_VT(&var) = VT_I4;
        V_I4(&var) = 0;
        IOleCommandTarget_Exec(cmdtrg, NULL, OLECMDID_SETPROGRESSMAX,
                OLECMDEXECOPT_DONTPROMPTUSER, &var, NULL);
        IOleCommandTarget_Exec(cmdtrg, NULL, OLECMDID_SETPROGRESSPOS, 
                OLECMDEXECOPT_DONTPROMPTUSER, &var, NULL);

        IOleCommandTarget_Release(cmdtrg);
    }

    if(This->doc_obj->usermode == UNKNOWN_USERMODE)
        IOleControl_OnAmbientPropertyChange(&This->IOleControl_iface, DISPID_AMBIENT_USERMODE);

    IOleControl_OnAmbientPropertyChange(&This->IOleControl_iface,
            DISPID_AMBIENT_OFFLINEIFNOTCONNECTED);

    hres = get_client_disp_property(This->doc_obj->client, DISPID_AMBIENT_SILENT, &silent);
    if(SUCCEEDED(hres)) {
        if(V_VT(&silent) != VT_BOOL)
            WARN("V_VT(silent) = %d\n", V_VT(&silent));
        else if(V_BOOL(&silent))
            FIXME("silent == true\n");
    }

    IOleControl_OnAmbientPropertyChange(&This->IOleControl_iface, DISPID_AMBIENT_USERAGENT);
    IOleControl_OnAmbientPropertyChange(&This->IOleControl_iface, DISPID_AMBIENT_PALETTE);

    return S_OK;
}

static HRESULT WINAPI OleObject_GetClientSite(IOleObject *iface, IOleClientSite **ppClientSite)
{
    HTMLDocument *This = impl_from_IOleObject(iface);

    TRACE("(%p)->(%p)\n", This, ppClientSite);

    if(!ppClientSite)
        return E_INVALIDARG;

    if(This->doc_obj->client)
        IOleClientSite_AddRef(This->doc_obj->client);
    *ppClientSite = This->doc_obj->client;

    return S_OK;
}

static HRESULT WINAPI OleObject_SetHostNames(IOleObject *iface, LPCOLESTR szContainerApp, LPCOLESTR szContainerObj)
{
    HTMLDocument *This = impl_from_IOleObject(iface);
    FIXME("(%p)->(%s %s)\n", This, debugstr_w(szContainerApp), debugstr_w(szContainerObj));
    return E_NOTIMPL;
}

static HRESULT WINAPI OleObject_Close(IOleObject *iface, DWORD dwSaveOption)
{
    HTMLDocument *This = impl_from_IOleObject(iface);

    TRACE("(%p)->(%08x)\n", This, dwSaveOption);

    if(dwSaveOption == OLECLOSE_PROMPTSAVE)
        FIXME("OLECLOSE_PROMPTSAVE not implemented\n");

    if(This->doc_obj->in_place_active)
        IOleInPlaceObjectWindowless_InPlaceDeactivate(&This->IOleInPlaceObjectWindowless_iface);

    HTMLDocument_LockContainer(This->doc_obj, FALSE);

    if(This->advise_holder)
        IOleAdviseHolder_SendOnClose(This->advise_holder);
    
    return S_OK;
}

static HRESULT WINAPI OleObject_SetMoniker(IOleObject *iface, DWORD dwWhichMoniker, IMoniker *pmk)
{
    HTMLDocument *This = impl_from_IOleObject(iface);
    FIXME("(%p %d %p)->()\n", This, dwWhichMoniker, pmk);
    return E_NOTIMPL;
}

static HRESULT WINAPI OleObject_GetMoniker(IOleObject *iface, DWORD dwAssign, DWORD dwWhichMoniker, IMoniker **ppmk)
{
    HTMLDocument *This = impl_from_IOleObject(iface);
    FIXME("(%p)->(%d %d %p)\n", This, dwAssign, dwWhichMoniker, ppmk);
    return E_NOTIMPL;
}

static HRESULT WINAPI OleObject_InitFromData(IOleObject *iface, IDataObject *pDataObject, BOOL fCreation,
                                        DWORD dwReserved)
{
    HTMLDocument *This = impl_from_IOleObject(iface);
    FIXME("(%p)->(%p %x %d)\n", This, pDataObject, fCreation, dwReserved);
    return E_NOTIMPL;
}

static HRESULT WINAPI OleObject_GetClipboardData(IOleObject *iface, DWORD dwReserved, IDataObject **ppDataObject)
{
    HTMLDocument *This = impl_from_IOleObject(iface);
    FIXME("(%p)->(%d %p)\n", This, dwReserved, ppDataObject);
    return E_NOTIMPL;
}

static HRESULT WINAPI OleObject_DoVerb(IOleObject *iface, LONG iVerb, LPMSG lpmsg, IOleClientSite *pActiveSite,
                                        LONG lindex, HWND hwndParent, LPCRECT lprcPosRect)
{
    HTMLDocument *This = impl_from_IOleObject(iface);
    IOleDocumentSite *pDocSite;
    HRESULT hres;

    TRACE("(%p)->(%d %p %p %d %p %p)\n", This, iVerb, lpmsg, pActiveSite, lindex, hwndParent, lprcPosRect);

    if(iVerb != OLEIVERB_SHOW && iVerb != OLEIVERB_UIACTIVATE && iVerb != OLEIVERB_INPLACEACTIVATE) { 
        FIXME("iVerb = %d not supported\n", iVerb);
        return E_NOTIMPL;
    }

    if(!pActiveSite)
        pActiveSite = This->doc_obj->client;

    hres = IOleClientSite_QueryInterface(pActiveSite, &IID_IOleDocumentSite, (void**)&pDocSite);
    if(SUCCEEDED(hres)) {
        HTMLDocument_LockContainer(This->doc_obj, TRUE);

        /* FIXME: Create new IOleDocumentView. See CreateView for more info. */
        hres = IOleDocumentSite_ActivateMe(pDocSite, &This->IOleDocumentView_iface);
        IOleDocumentSite_Release(pDocSite);
    }else {
        hres = IOleDocumentView_UIActivate(&This->IOleDocumentView_iface, TRUE);
        if(SUCCEEDED(hres)) {
            if(lprcPosRect) {
                RECT rect; /* We need to pass rect as not const pointer */
                rect = *lprcPosRect;
                IOleDocumentView_SetRect(&This->IOleDocumentView_iface, &rect);
            }
            IOleDocumentView_Show(&This->IOleDocumentView_iface, TRUE);
        }
    }

    return hres;
}

static HRESULT WINAPI OleObject_EnumVerbs(IOleObject *iface, IEnumOLEVERB **ppEnumOleVerb)
{
    HTMLDocument *This = impl_from_IOleObject(iface);
    FIXME("(%p)->(%p)\n", This, ppEnumOleVerb);
    return E_NOTIMPL;
}

static HRESULT WINAPI OleObject_Update(IOleObject *iface)
{
    HTMLDocument *This = impl_from_IOleObject(iface);
    FIXME("(%p)\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI OleObject_IsUpToDate(IOleObject *iface)
{
    HTMLDocument *This = impl_from_IOleObject(iface);
    FIXME("(%p)\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI OleObject_GetUserClassID(IOleObject *iface, CLSID *pClsid)
{
    HTMLDocument *This = impl_from_IOleObject(iface);

    TRACE("(%p)->(%p)\n", This, pClsid);

    if(!pClsid)
        return E_INVALIDARG;

    *pClsid = CLSID_HTMLDocument;
    return S_OK;
}

static HRESULT WINAPI OleObject_GetUserType(IOleObject *iface, DWORD dwFormOfType, LPOLESTR *pszUserType)
{
    HTMLDocument *This = impl_from_IOleObject(iface);
    FIXME("(%p)->(%d %p)\n", This, dwFormOfType, pszUserType);
    return E_NOTIMPL;
}

static HRESULT WINAPI OleObject_SetExtent(IOleObject *iface, DWORD dwDrawAspect, SIZEL *psizel)
{
    HTMLDocument *This = impl_from_IOleObject(iface);
    FIXME("(%p)->(%d %p)\n", This, dwDrawAspect, psizel);
    return E_NOTIMPL;
}

static HRESULT WINAPI OleObject_GetExtent(IOleObject *iface, DWORD dwDrawAspect, SIZEL *psizel)
{
    HTMLDocument *This = impl_from_IOleObject(iface);
    FIXME("(%p)->(%d %p)\n", This, dwDrawAspect, psizel);
    return E_NOTIMPL;
}

static HRESULT WINAPI OleObject_Advise(IOleObject *iface, IAdviseSink *pAdvSink, DWORD *pdwConnection)
{
    HTMLDocument *This = impl_from_IOleObject(iface);
    TRACE("(%p)->(%p %p)\n", This, pAdvSink, pdwConnection);

    if(!pdwConnection)
        return E_INVALIDARG;

    if(!pAdvSink) {
        *pdwConnection = 0;
        return E_INVALIDARG;
    }

    if(!This->advise_holder) {
        CreateOleAdviseHolder(&This->advise_holder);
        if(!This->advise_holder)
            return E_OUTOFMEMORY;
    }

    return IOleAdviseHolder_Advise(This->advise_holder, pAdvSink, pdwConnection);
}

static HRESULT WINAPI OleObject_Unadvise(IOleObject *iface, DWORD dwConnection)
{
    HTMLDocument *This = impl_from_IOleObject(iface);
    TRACE("(%p)->(%d)\n", This, dwConnection);

    if(!This->advise_holder)
        return OLE_E_NOCONNECTION;

    return IOleAdviseHolder_Unadvise(This->advise_holder, dwConnection);
}

static HRESULT WINAPI OleObject_EnumAdvise(IOleObject *iface, IEnumSTATDATA **ppenumAdvise)
{
    HTMLDocument *This = impl_from_IOleObject(iface);

    if(!This->advise_holder) {
        *ppenumAdvise = NULL;
        return S_OK;
    }

    return IOleAdviseHolder_EnumAdvise(This->advise_holder, ppenumAdvise);
}

static HRESULT WINAPI OleObject_GetMiscStatus(IOleObject *iface, DWORD dwAspect, DWORD *pdwStatus)
{
    HTMLDocument *This = impl_from_IOleObject(iface);
    FIXME("(%p)->(%d %p)\n", This, dwAspect, pdwStatus);
    return E_NOTIMPL;
}

static HRESULT WINAPI OleObject_SetColorScheme(IOleObject *iface, LOGPALETTE *pLogpal)
{
    HTMLDocument *This = impl_from_IOleObject(iface);
    FIXME("(%p)->(%p)\n", This, pLogpal);
    return E_NOTIMPL;
}

static const IOleObjectVtbl OleObjectVtbl = {
    OleObject_QueryInterface,
    OleObject_AddRef,
    OleObject_Release,
    OleObject_SetClientSite,
    OleObject_GetClientSite,
    OleObject_SetHostNames,
    OleObject_Close,
    OleObject_SetMoniker,
    OleObject_GetMoniker,
    OleObject_InitFromData,
    OleObject_GetClipboardData,
    OleObject_DoVerb,
    OleObject_EnumVerbs,
    OleObject_Update,
    OleObject_IsUpToDate,
    OleObject_GetUserClassID,
    OleObject_GetUserType,
    OleObject_SetExtent,
    OleObject_GetExtent,
    OleObject_Advise,
    OleObject_Unadvise,
    OleObject_EnumAdvise,
    OleObject_GetMiscStatus,
    OleObject_SetColorScheme
};

/**********************************************************
 * IOleDocument implementation
 */

static inline HTMLDocument *impl_from_IOleDocument(IOleDocument *iface)
{
    return CONTAINING_RECORD(iface, HTMLDocument, IOleDocument_iface);
}

static HRESULT WINAPI OleDocument_QueryInterface(IOleDocument *iface, REFIID riid, void **ppv)
{
    HTMLDocument *This = impl_from_IOleDocument(iface);
    return htmldoc_query_interface(This, riid, ppv);
}

static ULONG WINAPI OleDocument_AddRef(IOleDocument *iface)
{
    HTMLDocument *This = impl_from_IOleDocument(iface);
    return htmldoc_addref(This);
}

static ULONG WINAPI OleDocument_Release(IOleDocument *iface)
{
    HTMLDocument *This = impl_from_IOleDocument(iface);
    return htmldoc_release(This);
}

static HRESULT WINAPI OleDocument_CreateView(IOleDocument *iface, IOleInPlaceSite *pIPSite, IStream *pstm,
                                   DWORD dwReserved, IOleDocumentView **ppView)
{
    HTMLDocument *This = impl_from_IOleDocument(iface);
    HRESULT hres;

    TRACE("(%p)->(%p %p %d %p)\n", This, pIPSite, pstm, dwReserved, ppView);

    if(!ppView)
        return E_INVALIDARG;

    /* FIXME:
     * Windows implementation creates new IOleDocumentView when function is called for the
     * first time and returns E_FAIL when it is called for the second time, but it doesn't matter
     * if the application uses returned interfaces, passed to ActivateMe or returned by
     * QueryInterface, so there is no reason to create new interface. This needs more testing.
     */

    if(pIPSite) {
        hres = IOleDocumentView_SetInPlaceSite(&This->IOleDocumentView_iface, pIPSite);
        if(FAILED(hres))
            return hres;
    }

    if(pstm)
        FIXME("pstm is not supported\n");

    IOleDocumentView_AddRef(&This->IOleDocumentView_iface);
    *ppView = &This->IOleDocumentView_iface;
    return S_OK;
}

static HRESULT WINAPI OleDocument_GetDocMiscStatus(IOleDocument *iface, DWORD *pdwStatus)
{
    HTMLDocument *This = impl_from_IOleDocument(iface);
    FIXME("(%p)->(%p)\n", This, pdwStatus);
    return E_NOTIMPL;
}

static HRESULT WINAPI OleDocument_EnumViews(IOleDocument *iface, IEnumOleDocumentViews **ppEnum,
                                   IOleDocumentView **ppView)
{
    HTMLDocument *This = impl_from_IOleDocument(iface);
    FIXME("(%p)->(%p %p)\n", This, ppEnum, ppView);
    return E_NOTIMPL;
}

static const IOleDocumentVtbl OleDocumentVtbl = {
    OleDocument_QueryInterface,
    OleDocument_AddRef,
    OleDocument_Release,
    OleDocument_CreateView,
    OleDocument_GetDocMiscStatus,
    OleDocument_EnumViews
};

/**********************************************************
 * IOleControl implementation
 */

static inline HTMLDocument *impl_from_IOleControl(IOleControl *iface)
{
    return CONTAINING_RECORD(iface, HTMLDocument, IOleControl_iface);
}

static HRESULT WINAPI OleControl_QueryInterface(IOleControl *iface, REFIID riid, void **ppv)
{
    HTMLDocument *This = impl_from_IOleControl(iface);
    return htmldoc_query_interface(This, riid, ppv);
}

static ULONG WINAPI OleControl_AddRef(IOleControl *iface)
{
    HTMLDocument *This = impl_from_IOleControl(iface);
    return htmldoc_addref(This);
}

static ULONG WINAPI OleControl_Release(IOleControl *iface)
{
    HTMLDocument *This = impl_from_IOleControl(iface);
    return htmldoc_release(This);
}

static HRESULT WINAPI OleControl_GetControlInfo(IOleControl *iface, CONTROLINFO *pCI)
{
    HTMLDocument *This = impl_from_IOleControl(iface);
    FIXME("(%p)->(%p)\n", This, pCI);
    return E_NOTIMPL;
}

static HRESULT WINAPI OleControl_OnMnemonic(IOleControl *iface, MSG *pMsg)
{
    HTMLDocument *This = impl_from_IOleControl(iface);
    FIXME("(%p)->(%p)\n", This, pMsg);
    return E_NOTIMPL;
}

HRESULT get_client_disp_property(IOleClientSite *client, DISPID dispid, VARIANT *res)
{
    IDispatch *disp = NULL;
    DISPPARAMS dispparams = {NULL, 0};
    UINT err;
    HRESULT hres;

    hres = IOleClientSite_QueryInterface(client, &IID_IDispatch, (void**)&disp);
    if(FAILED(hres)) {
        TRACE("Could not get IDispatch\n");
        return hres;
    }

    VariantInit(res);

    hres = IDispatch_Invoke(disp, dispid, &IID_NULL, LOCALE_SYSTEM_DEFAULT,
            DISPATCH_PROPERTYGET, &dispparams, res, NULL, &err);

    IDispatch_Release(disp);

    return hres;
}

static HRESULT on_change_dlcontrol(HTMLDocument *This)
{
    VARIANT res;
    HRESULT hres;
    
    hres = get_client_disp_property(This->doc_obj->client, DISPID_AMBIENT_DLCONTROL, &res);
    if(SUCCEEDED(hres))
        FIXME("unsupported dlcontrol %08x\n", V_I4(&res));

    return S_OK;
}

static HRESULT WINAPI OleControl_OnAmbientPropertyChange(IOleControl *iface, DISPID dispID)
{
    HTMLDocument *This = impl_from_IOleControl(iface);
    IOleClientSite *client;
    VARIANT res;
    HRESULT hres;

    client = This->doc_obj->client;
    if(!client) {
        TRACE("client = NULL\n");
        return S_OK;
    }

    switch(dispID) {
    case DISPID_AMBIENT_USERMODE:
        TRACE("(%p)->(DISPID_AMBIENT_USERMODE)\n", This);
        hres = get_client_disp_property(client, DISPID_AMBIENT_USERMODE, &res);
        if(FAILED(hres))
            return S_OK;

        if(V_VT(&res) == VT_BOOL) {
            if(V_BOOL(&res)) {
                This->doc_obj->usermode = BROWSEMODE;
            }else {
                FIXME("edit mode is not supported\n");
                This->doc_obj->usermode = EDITMODE;
            }
        }else {
            FIXME("V_VT(res)=%d\n", V_VT(&res));
        }
        return S_OK;
    case DISPID_AMBIENT_DLCONTROL:
        TRACE("(%p)->(DISPID_AMBIENT_DLCONTROL)\n", This);
        return on_change_dlcontrol(This);
    case DISPID_AMBIENT_OFFLINEIFNOTCONNECTED:
        TRACE("(%p)->(DISPID_AMBIENT_OFFLINEIFNOTCONNECTED)\n", This);
        on_change_dlcontrol(This);
        hres = get_client_disp_property(client, DISPID_AMBIENT_OFFLINEIFNOTCONNECTED, &res);
        if(FAILED(hres))
            return S_OK;

        if(V_VT(&res) == VT_BOOL) {
            if(V_BOOL(&res)) {
                FIXME("offline connection is not supported\n");
                hres = E_FAIL;
            }
        }else {
            FIXME("V_VT(res)=%d\n", V_VT(&res));
        }
        return S_OK;
    case DISPID_AMBIENT_SILENT:
        TRACE("(%p)->(DISPID_AMBIENT_SILENT)\n", This);
        on_change_dlcontrol(This);
        hres = get_client_disp_property(client, DISPID_AMBIENT_SILENT, &res);
        if(FAILED(hres))
            return S_OK;

        if(V_VT(&res) == VT_BOOL) {
            if(V_BOOL(&res)) {
                FIXME("silent mode is not supported\n");
                hres = E_FAIL;
            }
        }else {
            FIXME("V_VT(res)=%d\n", V_VT(&res));
        }
        return S_OK;
    case DISPID_AMBIENT_USERAGENT:
        TRACE("(%p)->(DISPID_AMBIENT_USERAGENT)\n", This);
        hres = get_client_disp_property(client, DISPID_AMBIENT_USERAGENT, &res);
        if(FAILED(hres))
            return S_OK;

        FIXME("not supported AMBIENT_USERAGENT\n");
        hres = E_FAIL;
        return S_OK;
    case DISPID_AMBIENT_PALETTE:
        TRACE("(%p)->(DISPID_AMBIENT_PALETTE)\n", This);
        hres = get_client_disp_property(client, DISPID_AMBIENT_PALETTE, &res);
        if(FAILED(hres))
            return S_OK;

        FIXME("not supported AMBIENT_PALETTE\n");
        hres = E_FAIL;
        return S_OK;
    }

    FIXME("(%p) unsupported dispID=%d\n", This, dispID);
    return E_FAIL;
}

static HRESULT WINAPI OleControl_FreezeEvents(IOleControl *iface, BOOL bFreeze)
{
    HTMLDocument *This = impl_from_IOleControl(iface);
    FIXME("(%p)->(%x)\n", This, bFreeze);
    return E_NOTIMPL;
}

static const IOleControlVtbl OleControlVtbl = {
    OleControl_QueryInterface,
    OleControl_AddRef,
    OleControl_Release,
    OleControl_GetControlInfo,
    OleControl_OnMnemonic,
    OleControl_OnAmbientPropertyChange,
    OleControl_FreezeEvents
};

/**********************************************************
 * IObjectWithSite implementation
 */

static inline HTMLDocument *impl_from_IObjectWithSite(IObjectWithSite *iface)
{
    return CONTAINING_RECORD(iface, HTMLDocument, IObjectWithSite_iface);
}

static HRESULT WINAPI ObjectWithSite_QueryInterface(IObjectWithSite *iface, REFIID riid, void **ppv)
{
    HTMLDocument *This = impl_from_IObjectWithSite(iface);
    return htmldoc_query_interface(This, riid, ppv);
}

static ULONG WINAPI ObjectWithSite_AddRef(IObjectWithSite *iface)
{
    HTMLDocument *This = impl_from_IObjectWithSite(iface);
    return htmldoc_addref(This);
}

static ULONG WINAPI ObjectWithSite_Release(IObjectWithSite *iface)
{
    HTMLDocument *This = impl_from_IObjectWithSite(iface);
    return htmldoc_release(This);
}

static HRESULT WINAPI ObjectWithSite_SetSite(IObjectWithSite *iface, IUnknown *pUnkSite)
{
    HTMLDocument *This = impl_from_IObjectWithSite(iface);
    FIXME("(%p)->(%p)\n", This, pUnkSite);
    return E_NOTIMPL;
}

static HRESULT WINAPI ObjectWithSite_GetSite(IObjectWithSite* iface, REFIID riid, PVOID *ppvSite)
{
    HTMLDocument *This = impl_from_IObjectWithSite(iface);
    FIXME("(%p)->(%p)\n", This, ppvSite);
    return E_NOTIMPL;
}

static const IObjectWithSiteVtbl ObjectWithSiteVtbl = {
    ObjectWithSite_QueryInterface,
    ObjectWithSite_AddRef,
    ObjectWithSite_Release,
    ObjectWithSite_SetSite,
    ObjectWithSite_GetSite
};

static inline HTMLDocument *impl_from_IOleContainer(IOleContainer *iface)
{
    return CONTAINING_RECORD(iface, HTMLDocument, IOleContainer_iface);
}

static HRESULT WINAPI OleContainer_QueryInterface(IOleContainer *iface, REFIID riid, void **ppv)
{
    HTMLDocument *This = impl_from_IOleContainer(iface);
    return htmldoc_query_interface(This, riid, ppv);
}

static ULONG WINAPI OleContainer_AddRef(IOleContainer *iface)
{
    HTMLDocument *This = impl_from_IOleContainer(iface);
    return htmldoc_addref(This);
}

static ULONG WINAPI OleContainer_Release(IOleContainer *iface)
{
    HTMLDocument *This = impl_from_IOleContainer(iface);
    return htmldoc_release(This);
}

static HRESULT WINAPI OleContainer_ParseDisplayName(IOleContainer *iface, IBindCtx *pbc, LPOLESTR pszDisplayName,
        ULONG *pchEaten, IMoniker **ppmkOut)
{
    HTMLDocument *This = impl_from_IOleContainer(iface);
    FIXME("(%p)->(%p %s %p %p)\n", This, pbc, debugstr_w(pszDisplayName), pchEaten, ppmkOut);
    return E_NOTIMPL;
}

static HRESULT WINAPI OleContainer_EnumObjects(IOleContainer *iface, DWORD grfFlags, IEnumUnknown **ppenum)
{
    HTMLDocument *This = impl_from_IOleContainer(iface);
    FIXME("(%p)->(%x %p)\n", This, grfFlags, ppenum);
    return E_NOTIMPL;
}

static HRESULT WINAPI OleContainer_LockContainer(IOleContainer *iface, BOOL fLock)
{
    HTMLDocument *This = impl_from_IOleContainer(iface);
    FIXME("(%p)->(%x)\n", This, fLock);
    return E_NOTIMPL;
}

static const IOleContainerVtbl OleContainerVtbl = {
    OleContainer_QueryInterface,
    OleContainer_AddRef,
    OleContainer_Release,
    OleContainer_ParseDisplayName,
    OleContainer_EnumObjects,
    OleContainer_LockContainer
};

void HTMLDocument_LockContainer(HTMLDocumentObj *This, BOOL fLock)
{
    IOleContainer *container;
    HRESULT hres;

    if(!This->client || This->container_locked == fLock)
        return;

    hres = IOleClientSite_GetContainer(This->client, &container);
    if(SUCCEEDED(hres)) {
        IOleContainer_LockContainer(container, fLock);
        This->container_locked = fLock;
        IOleContainer_Release(container);
    }
}

void HTMLDocument_OleObj_Init(HTMLDocument *This)
{
    This->IOleObject_iface.lpVtbl = &OleObjectVtbl;
    This->IOleDocument_iface.lpVtbl = &OleDocumentVtbl;
    This->IOleControl_iface.lpVtbl = &OleControlVtbl;
    This->IObjectWithSite_iface.lpVtbl = &ObjectWithSiteVtbl;
    This->IOleContainer_iface.lpVtbl = &OleContainerVtbl;
}
