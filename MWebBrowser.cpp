// MWebBrowser.cpp --- simple Win32 Web Browser
// Copyright (C) 2019 Katayama Hirofumi MZ <katayama.hirofumi.mz@gmail.com>
// This file is public domain software.

#include "MWebBrowser.hpp"
#include <cstdio>
#include <mshtml.h>
#include <comdef.h>
#include <cassert>

/*static*/ MWebBrowser *
MWebBrowser::Create(HWND hwndParent)
{
    MWebBrowser *pBrowser = new MWebBrowser(hwndParent);
    if (!pBrowser->IsCreated())
    {
        pBrowser->Release();
        pBrowser = NULL;
    }
    return pBrowser;
}

MWebBrowser::MWebBrowser(HWND hwndParent) :
    m_nRefCount(0),
    m_hwndParent(NULL),
    m_hwndCtrl(NULL),
    m_hwndIEServer(NULL),
    m_web_browser2(NULL),
    m_ole_object(NULL),
    m_ole_inplace_object(NULL),
    m_hr(S_OK),
    m_bAllowInsecure(FALSE)
{
    ::SetRectEmpty(&m_rc);

    m_hr = CreateBrowser(hwndParent);
}

BOOL MWebBrowser::IsCreated() const
{
    return m_hr == S_OK;
}

MWebBrowser::~MWebBrowser()
{
    if (m_ole_object)
    {
        m_ole_object->Release();
        m_ole_object = NULL;
    }
    if (m_ole_inplace_object)
    {
        m_ole_inplace_object->Release();
        m_ole_inplace_object = NULL;
    }
    if (m_web_browser2)
    {
        m_web_browser2->Release();
        m_web_browser2 = NULL;
    }
}

IWebBrowser2 *MWebBrowser::GetIWebBrowser2()
{
    return m_web_browser2;
}

HWND MWebBrowser::GetControlWindow()
{
    if (::IsWindow(m_hwndCtrl))
        return m_hwndCtrl;

    if (!m_ole_inplace_object)
        return NULL;

    m_ole_inplace_object->GetWindow(&m_hwndCtrl);
    return m_hwndCtrl;
}

HWND MWebBrowser::GetIEServerWindow()
{
    if (::IsWindow(m_hwndIEServer))
        return m_hwndIEServer;

    HWND hwnd = ::GetWindow(m_hwndParent, GW_CHILD);
    while (hwnd)
    {
        WCHAR szClass[64];
        ::GetClassNameW(hwnd, szClass, 64);
        if (lstrcmpiW(szClass, L"Internet Explorer_Server") == 0)
        {
            m_hwndIEServer = hwnd;
            return hwnd;
        }
        hwnd = ::GetWindow(hwnd, GW_CHILD);
    }

    return NULL;
}

HRESULT MWebBrowser::CreateBrowser(HWND hwndParent)
{
    m_hwndParent = hwndParent;

    HRESULT hr;
    hr = ::OleCreate(CLSID_WebBrowser, IID_IOleObject, OLERENDER_DRAW, NULL,
                     this, this, (void **)&m_ole_object);
    if (FAILED(hr))
    {
        assert(0);
        return hr;
    }

    hr = m_ole_object->SetClientSite(this);
    if (FAILED(hr))
    {
        assert(0);
        return hr;
    }

    hr = ::OleSetContainedObject(m_ole_object, TRUE);
    if (FAILED(hr))
    {
        assert(0);
        return hr;
    }

    RECT rc;
    ::SetRectEmpty(&rc);
    hr = m_ole_object->DoVerb(OLEIVERB_INPLACEACTIVATE, NULL, this, 0,
                              m_hwndParent, &rc);
    if (FAILED(hr))
    {
        assert(0);
        return hr;
    }

    hr = m_ole_object->QueryInterface(&m_web_browser2);
    if (FAILED(hr))
    {
        assert(0);
        return hr;
    }

    HWND hwnd = GetControlWindow();

    DWORD exstyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    SetWindowLong(hwnd, GWL_EXSTYLE, exstyle | WS_EX_CLIENTEDGE);

    ShowWindow(hwnd, SW_SHOWNORMAL);

    Release();

    return S_OK;
}

void MWebBrowser::Destroy()
{
    if (m_web_browser2)
        m_web_browser2->Quit();

    m_hwndParent = NULL;
    m_hwndCtrl = NULL;
    m_hwndIEServer = NULL;
}

RECT MWebBrowser::PixelToHIMETRIC(const RECT& rc)
{
    HDC hDC = ::GetDC(NULL);
    INT nPixelsPerInchX = ::GetDeviceCaps(hDC, LOGPIXELSX);
    INT nPixelsPerInchY = ::GetDeviceCaps(hDC, LOGPIXELSY);
    RECT ret;
    ret.left = MulDiv(rc.left, 2540, nPixelsPerInchX);
    ret.top = MulDiv(rc.top, 2540, nPixelsPerInchY);
    ret.right = MulDiv(rc.right, 2540, nPixelsPerInchX);
    ret.bottom = MulDiv(rc.bottom, 2540, nPixelsPerInchY);
    ::ReleaseDC(NULL, hDC);
    return ret;
}

void MWebBrowser::MoveWindow(const RECT& rc)
{
    m_rc = rc;

    SIZEL siz;
    RECT rcHIMETRIC = PixelToHIMETRIC(rc);
    siz.cx = rcHIMETRIC.right - rcHIMETRIC.left;
    siz.cy = rcHIMETRIC.bottom - rcHIMETRIC.top;
    m_ole_object->SetExtent(DVASPECT_CONTENT, &siz);

    if (m_ole_inplace_object)
    {
        m_ole_inplace_object->SetObjectRects(&m_rc, &m_rc);
    }
}

void MWebBrowser::GoHome()
{
    if (m_web_browser2)
        m_web_browser2->GoHome();
}

void MWebBrowser::GoBack()
{
    if (m_web_browser2)
        m_web_browser2->GoBack();
}

void MWebBrowser::GoForward()
{
    if (m_web_browser2)
        m_web_browser2->GoForward();
}

void MWebBrowser::Stop()
{
    if (m_web_browser2)
        m_web_browser2->Stop();
}

void MWebBrowser::StopDownload()
{
    if (!m_web_browser2)
        return;

    IDispatch *pDisp;
    m_web_browser2->get_Document(&pDisp);
    if (pDisp)
    {
        IOleCommandTarget *pCmdTarget = NULL;
        pDisp->QueryInterface(&pCmdTarget);
        if (pCmdTarget)
        {
            OLECMDEXECOPT option = OLECMDEXECOPT_DONTPROMPTUSER;
            pCmdTarget->Exec(NULL, OLECMDID_STOPDOWNLOAD, option, NULL, NULL);
            pCmdTarget->Release();
        }
        pDisp->Release();
    }
}

void MWebBrowser::Refresh()
{
    if (m_web_browser2)
        m_web_browser2->Refresh();
}

void MWebBrowser::Navigate(const WCHAR *url)
{
    if (m_web_browser2)
    {
        bstr_t bstrURL(url);
        variant_t flags(0);
        m_web_browser2->Navigate(bstrURL, &flags, 0, 0, 0);
    }
}

void MWebBrowser::Zoom(LONG iZoomFactor)
{
    if (!m_web_browser2)
        return;

    IDispatch *pDisp;
    m_web_browser2->get_Document(&pDisp);
    if (pDisp)
    {
        IOleCommandTarget *pCmdTarget = NULL;
        pDisp->QueryInterface(&pCmdTarget);
        if (pCmdTarget)
        {
            variant_t factor(iZoomFactor);
            OLECMDEXECOPT option = OLECMDEXECOPT_DONTPROMPTUSER;
            pCmdTarget->Exec(NULL, OLECMDID_ZOOM, option, &factor, NULL);
            pCmdTarget->Release();
        }
        pDisp->Release();
    }
}

void MWebBrowser::Print(OLECMDEXECOPT option)
{
    if (!m_web_browser2)
        return;

    IDispatch *pDisp;
    m_web_browser2->get_Document(&pDisp);
    if (pDisp)
    {
        IOleCommandTarget *pCmdTarget = NULL;
        pDisp->QueryInterface(&pCmdTarget);
        if (pCmdTarget)
        {
            pCmdTarget->Exec(NULL, OLECMDID_PRINT, option, NULL, NULL);
            pCmdTarget->Release();
        }
        pDisp->Release();
    }
}

void MWebBrowser::PrintPreview()
{
    if (!m_web_browser2)
        return;

    IDispatch *pDisp;
    m_web_browser2->get_Document(&pDisp);
    if (pDisp)
    {
        IOleCommandTarget *pCmdTarget = NULL;
        pDisp->QueryInterface(&pCmdTarget);
        if (pCmdTarget)
        {
            OLECMDEXECOPT option = OLECMDEXECOPT_DONTPROMPTUSER;
            pCmdTarget->Exec(NULL, OLECMDID_PRINTPREVIEW, option, NULL, NULL);
            pCmdTarget->Release();
        }
        pDisp->Release();
    }
}

void MWebBrowser::PageSetup()
{
    if (!m_web_browser2)
        return;

    IDispatch *pDisp;
    m_web_browser2->get_Document(&pDisp);
    if (pDisp)
    {
        IOleCommandTarget *pCmdTarget = NULL;
        pDisp->QueryInterface(&pCmdTarget);
        if (pCmdTarget)
        {
            OLECMDEXECOPT option = OLECMDEXECOPT_DONTPROMPTUSER;
            pCmdTarget->Exec(NULL, OLECMDID_PAGESETUP, option, NULL, NULL);
            pCmdTarget->Release();
        }
        pDisp->Release();
    }
}

HRESULT MWebBrowser::Save(LPCWSTR file)
{
    if (!m_web_browser2)
        return E_FAIL;

    HRESULT hr;
    IDispatch *pDisp = NULL;
    hr = m_web_browser2->get_Document(&pDisp);
    if (pDisp)
    {
        IPersistFile *pPersistFile = NULL;
        hr = pDisp->QueryInterface(&pPersistFile);
        if (pPersistFile)
        {
            hr = pPersistFile->Save(file, 0);
            pPersistFile->Release();
        }
        pDisp->Release();
    }

    return hr;
}

BOOL MWebBrowser::TranslateAccelerator(LPMSG pMsg)
{
    if (!m_web_browser2)
        return FALSE;

    IOleInPlaceActiveObject *pOIPAO = NULL;
    HRESULT hr = m_web_browser2->QueryInterface(&pOIPAO);
    if (SUCCEEDED(hr))
    {
        hr = pOIPAO->TranslateAccelerator(pMsg);
        pOIPAO->Release();
        return hr == S_OK;
    }
    return FALSE;
}

HRESULT MWebBrowser::get_LocationURL(BSTR *bstrURL) const
{
    if (!m_web_browser2)
    {
        *bstrURL = NULL;
        return E_FAIL;
    }

    return m_web_browser2->get_LocationURL(bstrURL);
}

HRESULT MWebBrowser::get_mimeType(BSTR *bstrMIME) const
{
    if (!m_web_browser2)
    {
        *bstrMIME = NULL;
        return E_FAIL;
    }

    HRESULT hr = E_FAIL;
    IDispatch *pDisp;
    m_web_browser2->get_Document(&pDisp);
    if (pDisp)
    {
        IHTMLDocument2 *pDocument = static_cast<IHTMLDocument2 *>(pDisp);
        hr = pDocument->get_mimeType(bstrMIME);
        pDisp->Release();
    }

    return hr;
}

void MWebBrowser::AllowInsecure(BOOL bAllow)
{
    m_bAllowInsecure = bAllow;
}

// IUnknown interface

STDMETHODIMP MWebBrowser::QueryInterface(REFIID riid, void **ppvObj)
{
    if (riid == __uuidof(IUnknown))
    {
        *ppvObj = static_cast<IOleClientSite *>(this);
    }
    else if (riid == __uuidof(IOleInPlaceSite))
    {
        *ppvObj = static_cast<IOleInPlaceSite *>(this);
    }
    else if (riid == __uuidof(IServiceProvider))
    {
        *ppvObj = static_cast<IServiceProvider *>(this);
    }
    else if (riid == __uuidof(IWindowForBindingUI) ||
             riid == __uuidof(IHttpSecurity))
    {
        *ppvObj = static_cast<IHttpSecurity *>(this);
    }
    else
    {
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) MWebBrowser::AddRef()
{
    m_nRefCount++;
    return m_nRefCount;
}

STDMETHODIMP_(ULONG) MWebBrowser::Release()
{
    --m_nRefCount;
    if (m_nRefCount != 0)
        return m_nRefCount;

    delete this;
    return 0;
}

// IOleWindow interface

STDMETHODIMP MWebBrowser::GetWindow(HWND *phwnd)
{
    *phwnd = m_hwndParent;
    return S_OK;
}

STDMETHODIMP MWebBrowser::ContextSensitiveHelp(BOOL fEnterMode)
{
    return E_NOTIMPL;
}

// IOleInPlaceSite interface

STDMETHODIMP MWebBrowser::CanInPlaceActivate()
{
    return S_OK;
}

STDMETHODIMP MWebBrowser::OnInPlaceActivate()
{
    ::OleLockRunning(m_ole_object, TRUE, FALSE);
    m_ole_object->QueryInterface(&m_ole_inplace_object);
    m_ole_inplace_object->SetObjectRects(&m_rc, &m_rc);
    return S_OK;
}

STDMETHODIMP MWebBrowser::OnUIActivate()
{
    return S_OK;
}

STDMETHODIMP MWebBrowser::GetWindowContext(
    IOleInPlaceFrame **ppFrame,
    IOleInPlaceUIWindow **ppDoc,
    LPRECT lprcPosRect,
    LPRECT lprcClipRect,
    LPOLEINPLACEFRAMEINFO lpFrameInfo)
{
    *ppFrame = NULL;
    *ppDoc = NULL;
    *lprcPosRect = m_rc;
    *lprcClipRect = *lprcPosRect;

    lpFrameInfo->fMDIApp = FALSE;
    lpFrameInfo->hwndFrame = m_hwndParent;
    lpFrameInfo->haccel = NULL;
    lpFrameInfo->cAccelEntries = 0;

    return S_OK;
}

STDMETHODIMP MWebBrowser::Scroll(SIZE scrollExtant)
{
    return E_NOTIMPL;
}

STDMETHODIMP MWebBrowser::OnUIDeactivate(BOOL fUndoable)
{
    return S_OK;
}

STDMETHODIMP MWebBrowser::OnInPlaceDeactivate()
{
    m_hwndCtrl = NULL;
    m_ole_inplace_object = NULL;
    return S_OK;
}

STDMETHODIMP MWebBrowser::DiscardUndoState()
{
    return E_NOTIMPL;
}

STDMETHODIMP MWebBrowser::DeactivateAndUndo()
{
    return E_NOTIMPL;
}

STDMETHODIMP MWebBrowser::OnPosRectChange(LPCRECT lprcPosRect)
{
    return E_NOTIMPL;
}

// IOleClientSite interface

STDMETHODIMP MWebBrowser::SaveObject()
{
    return E_NOTIMPL;
}

STDMETHODIMP MWebBrowser::GetMoniker(
    DWORD dwAssign,
    DWORD dwWhichMoniker,
    IMoniker **ppmk)
{
    if (dwAssign == OLEGETMONIKER_ONLYIFTHERE &&
        dwWhichMoniker == OLEWHICHMK_CONTAINER)
    {
        return E_FAIL;
    }

    return E_NOTIMPL;
}

STDMETHODIMP MWebBrowser::GetContainer(IOleContainer **ppContainer)
{
    return E_NOINTERFACE;
}

STDMETHODIMP MWebBrowser::ShowObject()
{
    return S_OK;
}

STDMETHODIMP MWebBrowser::OnShowWindow(BOOL fShow)
{
    return S_OK;
}

STDMETHODIMP MWebBrowser::RequestNewObjectLayout()
{
    return E_NOTIMPL;
}

// IStorage interface

STDMETHODIMP MWebBrowser::CreateStream(
    const OLECHAR *pwcsName,
    DWORD grfMode,
    DWORD reserved1,
    DWORD reserved2,
    IStream **ppstm)
{
    return E_NOTIMPL;
}

STDMETHODIMP MWebBrowser::OpenStream(
    const OLECHAR *pwcsName,
    void *reserved1,
    DWORD grfMode,
    DWORD reserved2,
    IStream **ppstm)
{
    return E_NOTIMPL;
}

STDMETHODIMP MWebBrowser::CreateStorage(
    const OLECHAR *pwcsName,
    DWORD grfMode,
    DWORD reserved1,
    DWORD reserved2,
    IStorage **ppstg)
{
    return E_NOTIMPL;
}

STDMETHODIMP MWebBrowser::OpenStorage(
    const OLECHAR *pwcsName,
    IStorage *pstgPriority,
    DWORD grfMode,
    SNB snbExclude,
    DWORD reserved,
    IStorage **ppstg)
{
    return E_NOTIMPL;
}

STDMETHODIMP MWebBrowser::CopyTo(
    DWORD ciidExclude,
    const IID *rgiidExclude,
    SNB snbExclude,
    IStorage *pstgDest)
{
    return E_NOTIMPL;
}

STDMETHODIMP MWebBrowser::MoveElementTo(
    const OLECHAR *pwcsName,
    IStorage *pstgDest,
    const OLECHAR *pwcsNewName,
    DWORD grfFlags)
{
    return E_NOTIMPL;
}

STDMETHODIMP MWebBrowser::Commit(DWORD grfCommitFlags)
{
    return E_NOTIMPL;
}

STDMETHODIMP MWebBrowser::Revert()
{
    return E_NOTIMPL;
}

STDMETHODIMP MWebBrowser::EnumElements(
    DWORD reserved1,
    void *reserved2,
    DWORD reserved3,
    IEnumSTATSTG **ppenum)
{
    return E_NOTIMPL;
}

STDMETHODIMP MWebBrowser::DestroyElement(
    const OLECHAR *pwcsName)
{
    return E_NOTIMPL;
}

STDMETHODIMP MWebBrowser::RenameElement(
    const OLECHAR *pwcsOldName,
    const OLECHAR *pwcsNewName)
{
    return E_NOTIMPL;
}

STDMETHODIMP MWebBrowser::SetElementTimes(
    const OLECHAR *pwcsName,
    const FILETIME *pctime,
    const FILETIME *patime,
    const FILETIME *pmtime)
{
    return E_NOTIMPL;
}

STDMETHODIMP MWebBrowser::SetClass(REFCLSID clsid)
{
    return S_OK;
}

STDMETHODIMP MWebBrowser::SetStateBits(DWORD grfStateBits, DWORD grfMask)
{
    return E_NOTIMPL;
}

STDMETHODIMP MWebBrowser::Stat(STATSTG *pstatstg, DWORD grfStatFlag)
{
    return E_NOTIMPL;
}

// IServiceProvider interface

STDMETHODIMP MWebBrowser::QueryService(
    REFGUID guidService,
    REFIID riid,
    void **ppvObject)
{
    return QueryInterface(guidService, ppvObject);
}

// IWindowForBindingUI interface

STDMETHODIMP MWebBrowser::GetWindow(REFGUID rguidReason, HWND *phwnd)
{
    *phwnd = m_hwndParent;
    return S_OK;
}

// IHttpSecurity interface

STDMETHODIMP MWebBrowser::OnSecurityProblem(DWORD dwProblem)
{
    if (m_bAllowInsecure)
        return S_OK;
    return E_ABORT;
}
