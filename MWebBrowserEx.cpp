// MWebBrowserEx.cpp --- simple Win32 Web Browser Extended
// Copyright (C) 2019 Katayama Hirofumi MZ <katayama.hirofumi.mz@gmail.com>
// This file is public domain software.

#include "MWebBrowserEx.hpp"
#include "MBindStatusCallback.hpp"
#include <commdlg.h>
#include <cwchar>
#include <comdef.h>
#include <cstring>
#include <mshtmhst.h>

/*static*/ MWebBrowserEx *
MWebBrowserEx::Create(HWND hwndParent)
{
    MWebBrowserEx *pBrowser = new MWebBrowserEx(hwndParent);
    if (!pBrowser->IsCreated())
    {
        pBrowser->Release();
        pBrowser = NULL;
    }
    return pBrowser;
}

MWebBrowserEx::MWebBrowserEx(HWND hwndParent) : MWebBrowser(hwndParent)
{
}

MWebBrowserEx::~MWebBrowserEx()
{
}

// IUnknown interface

STDMETHODIMP MWebBrowserEx::QueryInterface(REFIID riid, void **ppvObj)
{
#ifndef NDEBUG
    LPOLESTR psz = NULL;
    StringFromIID(riid, &psz);
    if (psz)
    {
        printf("MWebBrowserEx::QueryInterface: %ls\n", psz);
        CoTaskMemFree(psz);
    }
#endif
    return MWebBrowser::QueryInterface(riid, ppvObj);
}

STDMETHODIMP_(ULONG) MWebBrowserEx::AddRef()
{
    return MWebBrowser::AddRef();
}

STDMETHODIMP_(ULONG) MWebBrowserEx::Release()
{
    return MWebBrowser::Release();
}

// IServiceProvider interface

STDMETHODIMP MWebBrowserEx::QueryService(
    REFGUID guidService,
    REFIID riid,
    void **ppvObject)
{
#ifndef NDEBUG
    LPOLESTR psz = NULL;
    StringFromIID(riid, &psz);
    if (psz)
    {
        printf("MWebBrowserEx::QueryService: %ls\n", psz);
        CoTaskMemFree(psz);
    }
#endif

    if (riid == __uuidof(IDownloadManager))
    {
        *ppvObject = static_cast<IDownloadManager *>(this);
    }
    else
    {
        return MWebBrowser::QueryService(guidService, riid, ppvObject);
    }

    AddRef();
    return S_OK;
}
