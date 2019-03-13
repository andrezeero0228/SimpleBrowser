// SimpleBrowser.cpp --- simple Win32 browser
// Copyright (C) 2019 Katayama Hirofumi MZ <katayama.hirofumi.mz@gmail.com>
// This file is public domain software.
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlwapi.h>
#include <mshtml.h>
#include <urlmon.h>
#include "MWebBrowser.hpp"
#include "MEventSink.hpp"
#include <string>
#include <cassert>
#include "resource.h"

static const UINT s_control_ids[] =
{
    ID_BACK,
    ID_NEXT,
    ID_STOP_REFRESH,
    ID_HOME,
    ID_ADDRESS_BAR,
    ID_GO,
    ID_BROWSER
};

// button size
#define BTN_WIDTH 80
#define BTN_HEIGHT 25

static const TCHAR s_szName[] = TEXT("SimpleBrowser");
static HINSTANCE s_hInst = NULL;
static HACCEL s_hAccel = NULL;
static HWND s_hMainWnd = NULL;
static HWND s_hStatusBar = NULL;
static HWND s_hAddressBar = NULL;
static MWebBrowser *s_pWebBrowser = NULL;
static HFONT s_hGUIFont = NULL;
static MEventSink *s_pEventSink = MEventSink::Create();
static BOOL s_bLoadingPage = FALSE;
static HBITMAP s_hbmSecure = NULL;
static HBITMAP s_hbmInsecure = NULL;

void DoUpdateURL(const WCHAR *url)
{
    ::SetWindowTextW(s_hAddressBar, url);
}

void SetDocumentContents(IHTMLDocument2 *pDocument, const WCHAR *html)
{
    if (BSTR bstr = SysAllocString(html))
    {
        if (SAFEARRAY *sa = SafeArrayCreateVector(VT_VARIANT, 0, 1))
        {
            VARIANT *pvar;
            HRESULT hr = SafeArrayAccessData(sa, (void **)&pvar);
            if (SUCCEEDED(hr))
            {
                pvar->vt = VT_BSTR;
                pvar->bstrVal = bstr;
                SafeArrayDestroy(sa);

                pDocument->write(sa);
            }
        }
        SysFreeString(bstr);
    }
}

void SetInternalPageContents(const WCHAR *html)
{
    IDispatch *pDisp = NULL;
    s_pWebBrowser->GetIWebBrowser2()->get_Document(&pDisp);
    if (pDisp)
    {
        if (IHTMLDocument2 *pDocument = static_cast<IHTMLDocument2 *>(pDisp))
        {
            SetDocumentContents(pDocument, L"");
            pDocument->close();
            SetDocumentContents(pDocument, html);
        }
        pDisp->Release();
    }
}

BOOL UrlInBlackList(const WCHAR *url)
{
    std::wstring strURL = url;
    if (strURL.find(L"example.com") != std::wstring::npos)
    {
        return TRUE;
    }
    return FALSE;
}

// load a resource string using rotated buffers
LPTSTR LoadStringDx(INT nID)
{
    static UINT s_index = 0;
    const UINT cchBuffMax = 1024;
    static TCHAR s_sz[4][cchBuffMax];

    TCHAR *pszBuff = s_sz[s_index];
    s_index = (s_index + 1) % _countof(s_sz);
    pszBuff[0] = 0;
    if (!::LoadString(NULL, nID, pszBuff, cchBuffMax))
        assert(0);
    return pszBuff;
}

inline LPTSTR MakeFilterDx(LPTSTR psz)
{
    for (LPTSTR pch = psz; *pch; ++pch)
    {
        if (*pch == TEXT('|'))
            *pch = 0;
    }
    return psz;
}

struct MEventHandler : MEventSinkListener
{
    virtual void OnBeforeNavigate2(
        IDispatch *pDisp,
        VARIANT *url,
        VARIANT *Flags,
        VARIANT *TargetFrameName,
        VARIANT *PostData,
        VARIANT *Headers,
        VARIANT_BOOL *Cancel)
    {
        if (Flags->lVal & 0x100)    // ???
        {
            if (UrlInBlackList(url->bstrVal))
            {
                SetInternalPageContents(LoadStringDx(IDS_HITBLACKLIST));
                *Cancel = VARIANT_TRUE;
                return;
            }

            s_bLoadingPage = TRUE;

            DoUpdateURL(url->bstrVal);
            ::SetDlgItemText(s_hMainWnd, ID_STOP_REFRESH, LoadStringDx(IDS_STOP));
            InvalidateRect(s_hAddressBar, NULL, TRUE);
        }
    }

    virtual void OnNavigateComplete2(
        IDispatch *pDispatch,
        VARIANT *URL)
    {
        ::SetDlgItemText(s_hMainWnd, ID_STOP_REFRESH, LoadStringDx(IDS_REFRESH));
        s_pWebBrowser->Zoom();
        s_bLoadingPage = FALSE;
        InvalidateRect(s_hAddressBar, NULL, TRUE);
    }

    virtual void OnNewWindow3(
        IDispatch **ppDisp,
        VARIANT_BOOL *Cancel,
        DWORD dwFlags,
        BSTR bstrUrlContext,
        BSTR bstrUrl)
    {
        // prevent new window open
        *Cancel = VARIANT_TRUE;
    }

    virtual void OnCommandStateChange(
        long Command,
        VARIANT_BOOL Enable)
    {
        static BOOL bEnableForward = FALSE, bEnableBack = FALSE;

        if (Command == CSC_NAVIGATEFORWARD)
        {
            bEnableForward = (Enable == VARIANT_TRUE);
        }
        else if (Command == CSC_NAVIGATEBACK)
        {
            bEnableBack = (Enable == VARIANT_TRUE);
        }

        ::EnableWindow(::GetDlgItem(s_hMainWnd, ID_BACK), bEnableBack);
        ::EnableWindow(::GetDlgItem(s_hMainWnd, ID_NEXT), bEnableForward);
    }

    virtual void OnStatusTextChange(BSTR Text)
    {
        SetWindowTextW(s_hStatusBar, Text);
    }

    virtual void OnTitleTextChange(BSTR Text)
    {
        SetWindowTextW(s_hMainWnd, Text);
    }

    virtual void OnFileDownload(
        VARIANT_BOOL ActiveDocument,
        VARIANT_BOOL *Cancel)
    {
        *Cancel = TRUE;
    }
};
MEventHandler s_listener;

void DoNavigate(HWND hwnd, const WCHAR *url)
{
    s_pWebBrowser->Navigate(url);
}

BOOL DoSetBrowserEmulation(DWORD dwValue)
{
    static const TCHAR s_szFeatureControl[] =
        TEXT("SOFTWARE\\Microsoft\\Internet Explorer\\Main\\FeatureControl");

    TCHAR szPath[MAX_PATH], *pchFileName;
    GetModuleFileName(NULL, szPath, ARRAYSIZE(szPath));
    pchFileName = PathFindFileName(szPath);

    BOOL bOK = FALSE;
    HKEY hkeyControl = NULL;
    RegOpenKeyEx(HKEY_CURRENT_USER, s_szFeatureControl, 0, KEY_ALL_ACCESS, &hkeyControl);
    if (hkeyControl)
    {
        HKEY hkeyEmulation = NULL;
        RegCreateKeyEx(hkeyControl, TEXT("FEATURE_BROWSER_EMULATION"), 0, NULL, 0,
                       KEY_ALL_ACCESS, NULL, &hkeyEmulation, NULL);
        if (hkeyEmulation)
        {
            if (dwValue)
            {
                DWORD value = dwValue, size = sizeof(value);
                LONG result = RegSetValueEx(hkeyEmulation, pchFileName, 0,
                                            REG_DWORD, (LPBYTE)&value, size);
                bOK = (result == ERROR_SUCCESS);
            }
            else
            {
                RegDeleteValue(hkeyEmulation, pchFileName);
                bOK = TRUE;
            }

            RegCloseKey(hkeyEmulation);
        }

        RegCloseKey(hkeyControl);
    }

    return bOK;
}

BOOL OnCreate(HWND hwnd, LPCREATESTRUCT lpCreateStruct)
{
    s_hMainWnd = hwnd;

    s_hbmSecure = LoadBitmap(s_hInst, MAKEINTRESOURCE(IDB_SECURE));
    s_hbmInsecure = LoadBitmap(s_hInst, MAKEINTRESOURCE(IDB_INSECURE));
    s_hAccel = LoadAccelerators(s_hInst, MAKEINTRESOURCE(1));

    DoSetBrowserEmulation(11001);   // EDGE
    //DoSetBrowserEmulation(11000);   // IE
    //DoSetBrowserEmulation(0);

    s_pWebBrowser = MWebBrowser::Create(hwnd);
    if (!s_pWebBrowser)
        return FALSE;

    IWebBrowser2 *pBrowser2 = s_pWebBrowser->GetIWebBrowser2();

    // Don't show JavaScript errors
    pBrowser2->put_Silent(VARIANT_TRUE);

    s_pEventSink->Connect(pBrowser2, &s_listener);

    s_hGUIFont = GetStockFont(DEFAULT_GUI_FONT);

    INT x, y, cx, cy;
    DWORD style = WS_CHILD | WS_VISIBLE;

    x = y = 0;
    cx = BTN_WIDTH;
    cy = BTN_HEIGHT;
    static const TCHAR s_szButton[] = TEXT("BUTTON");
    CreateWindowEx(0, s_szButton, LoadStringDx(IDS_BACK),
                   style, x, y, cx, cy,
                   hwnd, (HMENU) ID_BACK, s_hInst, NULL);
    x += cx;
    CreateWindowEx(0, s_szButton, LoadStringDx(IDS_NEXT),
                   style, x, y, cx, cy,
                   hwnd, (HMENU) ID_NEXT, s_hInst, NULL);
    x += cx;
    CreateWindowEx(0, s_szButton, LoadStringDx(IDS_REFRESH),
                   style, x, y, cx, cy,
                   hwnd, (HMENU) ID_STOP_REFRESH, s_hInst, NULL);
    x += cx;
    CreateWindowEx(0, s_szButton, LoadStringDx(IDS_HOME),
                   style, x, y, cx, cy,
                   hwnd, (HMENU) ID_HOME, s_hInst, NULL);
    x += cx;

    LONG nHeight = BTN_HEIGHT;
    if (HDC hDC = CreateCompatibleDC(NULL))
    {
        HGDIOBJ hFontOld = SelectObject(hDC, s_hGUIFont);
        {
            TCHAR sz[] = TEXT("Mg");
            SIZE siz;
            GetTextExtentPoint32(hDC, sz, 2, &siz);
            nHeight = siz.cy + 8;
        }
        SelectObject(hDC, hFontOld);
        DeleteDC(hDC);
    }

    cx = 260;
    style = WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL;
    CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("EDIT"), NULL, style,
                   x, (BTN_HEIGHT - nHeight) / 2 + 1,
                   cx, nHeight,
                   hwnd, (HMENU)ID_ADDRESS_BAR, s_hInst, NULL);
    s_hAddressBar = GetDlgItem(hwnd, ID_ADDRESS_BAR);
    x += cx;

    cx = BTN_WIDTH;
    cy = BTN_HEIGHT;
    style = WS_CHILD | WS_VISIBLE;
    CreateWindowEx(0, s_szButton, LoadStringDx(IDS_GO),
                   style, x, y, cx, cy,
                   hwnd, (HMENU) ID_GO, s_hInst, NULL);

    SendDlgItemMessage(hwnd, ID_BACK, WM_SETFONT, (WPARAM)s_hGUIFont, TRUE);
    SendDlgItemMessage(hwnd, ID_NEXT, WM_SETFONT, (WPARAM)s_hGUIFont, TRUE);
    SendDlgItemMessage(hwnd, ID_STOP_REFRESH, WM_SETFONT, (WPARAM)s_hGUIFont, TRUE);
    SendDlgItemMessage(hwnd, ID_HOME, WM_SETFONT, (WPARAM)s_hGUIFont, TRUE);
    SendDlgItemMessage(hwnd, ID_ADDRESS_BAR, WM_SETFONT, (WPARAM)s_hGUIFont, TRUE);
    SendDlgItemMessage(hwnd, ID_GO, WM_SETFONT, (WPARAM)s_hGUIFont, TRUE);

    style = WS_CHILD | WS_VISIBLE;
    s_hStatusBar = CreateStatusWindow(style, LoadStringDx(IDS_LOADING), hwnd, stc1);
    if (!s_hStatusBar)
        return FALSE;

    SHAutoComplete(s_hAddressBar, SHACF_URLALL | SHACF_AUTOSUGGEST_FORCE_ON);

    DoNavigate(hwnd, L"about:blank");
    DoNavigate(hwnd, LoadStringDx(IDS_HOMEPAGE));

    PostMessage(hwnd, WM_SIZE, 0, 0);

    return TRUE;
}

void OnSize(HWND hwnd, UINT state, int cx, int cy)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    rc.top += BTN_HEIGHT;

    RECT rcStatus;
    SendMessage(s_hStatusBar, WM_SIZE, 0, 0);
    GetWindowRect(s_hStatusBar, &rcStatus);

    rc.bottom -= rcStatus.bottom - rcStatus.top;

    s_pWebBrowser->MoveWindow(rc);
}

void OnBack(HWND hwnd)
{
    s_pWebBrowser->GoBack();
}

void OnNext(HWND hwnd)
{
    s_pWebBrowser->GoForward();
}

void OnStopRefresh(HWND hwnd)
{
    if (s_bLoadingPage)
    {
        s_pWebBrowser->Stop();
        s_pWebBrowser->StopDownload();
    }
    else
    {
        s_pWebBrowser->Refresh();
    }
}

void OnRefresh(HWND hwnd)
{
    s_pWebBrowser->Refresh();
}

void OnStop(HWND hwnd)
{
    s_pWebBrowser->Stop();
    s_pWebBrowser->StopDownload();
}

void OnGoToAddressBar(HWND hwnd)
{
    SendMessage(s_hAddressBar, EM_SETSEL, 0, -1);
    SetFocus(s_hAddressBar);
}

void OnGo(HWND hwnd)
{
    WCHAR szURL[256];
    GetWindowTextW(s_hAddressBar, szURL, 256);

    StrTrimW(szURL, L" \t\n\r\f\v");

    if (szURL[0] == 0)
        lstrcpyW(szURL, L"about:blank");

    DoNavigate(hwnd, szURL);
}

void OnHome(HWND hwnd)
{
    DoNavigate(hwnd, LoadStringDx(IDS_HOMEPAGE));
}

void OnPrint(HWND hwnd)
{
    s_pWebBrowser->Print(OLECMDEXECOPT_PROMPTUSER);
}

void OnPrintBang(HWND hwnd)
{
    s_pWebBrowser->Print(OLECMDEXECOPT_DONTPROMPTUSER);
}

void OnPrintPreview(HWND hwnd)
{
    s_pWebBrowser->PrintPreview();
}

void OnPageSetup(HWND hwnd)
{
    s_pWebBrowser->PageSetup();
}

void OnSave(HWND hwnd)
{
    BSTR bstrURL = NULL;
    if (FAILED(s_pWebBrowser->get_LocationURL(&bstrURL)))
    {
        assert(0);
        return;
    }

    LPWSTR pszMime = NULL;
    DWORD dwFlags = FMFD_URLASFILENAME;
    FindMimeFromData(NULL, NULL, bstrURL, MAX_PATH, NULL,
                     dwFlags, &pszMime, 0);
    MessageBoxW(NULL, pszMime, NULL, 0);

    TCHAR file[MAX_PATH] = L"*";

    OPENFILENAME ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = file;
    ofn.nMaxFile = ARRAYSIZE(file);
    ofn.Flags = OFN_EXPLORER | OFN_ENABLESIZING | OFN_PATHMUSTEXIST |
                OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT;

    if (lstrcmpiW(pszMime, L"text/plain") == 0)
    {
        ofn.lpstrFilter = MakeFilterDx(LoadStringDx(IDS_TXTFILTER));
        ofn.lpstrDefExt = L"txt";
    }
    else if (lstrcmpiW(pszMime, L"text/html") == 0)
    {
        ofn.lpstrFilter = MakeFilterDx(LoadStringDx(IDS_HTMLFILTER));
        ofn.lpstrDefExt = L"html";
    }
    else if (lstrcmpiW(pszMime, L"image/jpeg") == 0)
    {
        ofn.lpstrFilter = MakeFilterDx(LoadStringDx(IDS_IMGFILTER));
        ofn.lpstrDefExt = L"jpg";
    }
    else if (lstrcmpiW(pszMime, L"image/png") == 0)
    {
        ofn.lpstrFilter = MakeFilterDx(LoadStringDx(IDS_IMGFILTER));
        ofn.lpstrDefExt = L"png";
    }
    else if (lstrcmpiW(pszMime, L"image/gif") == 0)
    {
        ofn.lpstrFilter = MakeFilterDx(LoadStringDx(IDS_IMGFILTER));
        ofn.lpstrDefExt = L"gif";
    }
    else if (lstrcmpiW(pszMime, L"image/tiff") == 0)
    {
        ofn.lpstrFilter = MakeFilterDx(LoadStringDx(IDS_IMGFILTER));
        ofn.lpstrDefExt = L"tif";
    }
    else if (lstrcmpiW(pszMime, L"image/bmp") == 0)
    {
        ofn.lpstrFilter = MakeFilterDx(LoadStringDx(IDS_IMGFILTER));
        ofn.lpstrDefExt = L"bmp";
    }
    else if (lstrcmpiW(pszMime, L"application/pdf") == 0)
    {
        ofn.lpstrFilter = MakeFilterDx(LoadStringDx(IDS_PDFFILTER));
        ofn.lpstrDefExt = L"pdf";
    }
    else
    {
        ofn.lpstrFilter = MakeFilterDx(LoadStringDx(IDS_ALLFILTER));
        ofn.lpstrDefExt = NULL;
    }

    if (::GetSaveFileName(&ofn))
    {
        s_pWebBrowser->Save(file);
    }

    ::CoTaskMemFree(pszMime);
    ::SysFreeString(bstrURL);
}

void OnCommand(HWND hwnd, int id, HWND hwndCtl, UINT codeNotify)
{
    static INT s_nLevel = 0;

    if (s_nLevel == 0)
    {
        SendMessage(s_hStatusBar, SB_SETTEXT, 0, (LPARAM)LoadStringDx(IDS_EXECUTING_CMD));
    }
    s_nLevel++;

    switch (id)
    {
    case ID_BACK:
        OnBack(hwnd);
        break;
    case ID_NEXT:
        OnNext(hwnd);
        break;
    case ID_STOP_REFRESH:
        OnStopRefresh(hwnd);
        break;
    case ID_GO:
        OnGo(hwnd);
        break;
    case ID_HOME:
        OnHome(hwnd);
        break;
    case ID_REFRESH:
        OnRefresh(hwnd);
        break;
    case ID_STOP:
        OnStop(hwnd);
        break;
    case ID_GO_TO_ADDRESS_BAR:
        OnGoToAddressBar(hwnd);
        break;
    case ID_PRINT:
        OnPrint(hwnd);
        break;
    case ID_PRINT_BANG:
        OnPrintBang(hwnd);
        break;
    case ID_PRINT_PREVIEW:
        OnPrintPreview(hwnd);
        break;
    case ID_PAGE_SETUP:
        OnPageSetup(hwnd);
        break;
    case ID_SAVE:
        OnSave(hwnd);
        break;
    }

    --s_nLevel;
    if (s_nLevel == 0)
    {
        SendMessage(s_hStatusBar, SB_SETTEXT, 0, (LPARAM)LoadStringDx(IDS_READY));
    }
}

void OnDestroy(HWND hwnd)
{
    if (s_hbmSecure)
    {
        DeleteObject(s_hbmSecure);
        s_hbmSecure = NULL;
    }
    if (s_hbmInsecure)
    {
        DeleteObject(s_hbmInsecure);
        s_hbmInsecure = NULL;
    }
    if (s_hAccel)
    {
        DestroyAcceleratorTable(s_hAccel);
        s_hAccel = NULL;
    }
    if (s_pWebBrowser)
    {
        s_pWebBrowser->Destroy();
        s_pWebBrowser->Release();
        s_pWebBrowser = NULL;
    }
    if (s_pEventSink)
    {
        s_pEventSink->Disconnect();
        s_pEventSink->Release();
        s_pEventSink = NULL;
    }
    PostQuitMessage(0);
}

LRESULT CALLBACK
WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    HANDLE_MSG(hwnd, WM_CREATE, OnCreate);
    HANDLE_MSG(hwnd, WM_SIZE, OnSize);
    HANDLE_MSG(hwnd, WM_COMMAND, OnCommand);
    HANDLE_MSG(hwnd, WM_DESTROY, OnDestroy);
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}

BOOL PreProcessBrowserKeys(LPMSG pMsg)
{
    if (s_pWebBrowser)
    {
        if (pMsg->hwnd == s_pWebBrowser->GetIEServerWindow())
        {
            BOOL bIgnore = FALSE;
            switch (pMsg->message)
            {
            case WM_RBUTTONDBLCLK:
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
                return TRUE;
            case WM_KEYDOWN:
            case WM_KEYUP:
            case WM_CHAR:
            case WM_IME_KEYDOWN:
            case WM_IME_KEYUP:
            case WM_IME_CHAR:
                if (GetAsyncKeyState(VK_CONTROL) < 0)
                {
                    switch (pMsg->wParam)
                    {
                    case 'L':   // Ctrl+L
                    case 'S':   // Ctrl+S
                    case 'O':   // Ctrl+O
                        bIgnore = TRUE;
                        break;
                    }
                }
                break;
            }

            if (!bIgnore && s_pWebBrowser->TranslateAccelerator(pMsg))
                return TRUE;
        }
    }

    //switch (pMsg->message)
    //{
    //case WM_SYSKEYDOWN:
    //    if (pMsg->wParam == 'D')
    //    {
    //        // Alt+D
    //        SetFocus(s_hAddressBar);
    //        SendMessage(s_hAddressBar, EM_SETSEL, 0, -1);
    //        return TRUE;
    //    }
    //    break;
    //}

    if (pMsg->hwnd == s_hAddressBar)
    {
        switch (pMsg->message)
        {
        case WM_KEYDOWN:
            if (pMsg->wParam == VK_RETURN)
            {
                // [Enter] key
                SendMessage(s_hMainWnd, WM_COMMAND, ID_GO, 0);
                return TRUE;
            }
            else if (pMsg->wParam == VK_ESCAPE && s_pWebBrowser)
            {
                // [Esc] key
                if (IWebBrowser2 *pBrowser2 = s_pWebBrowser->GetIWebBrowser2())
                {
                    BSTR bstrURL = NULL;
                    pBrowser2->get_LocationURL(&bstrURL);
                    if (bstrURL)
                    {
                        DoUpdateURL(bstrURL);
                        ::SysFreeString(bstrURL);
                    }
                }
                ::SetFocus(s_pWebBrowser->GetControlWindow());
                return TRUE;
            }
            else if (pMsg->wParam == 'A' && ::GetAsyncKeyState(VK_CONTROL) < 0)
            {
                // Ctrl+A
                SendMessage(s_hAddressBar, EM_SETSEL, 0, -1);
                return TRUE;
            }
            break;
        }
    }

    switch (pMsg->message)
    {
    case WM_KEYDOWN:
        if (pMsg->wParam == VK_TAB)
        {
            UINT nCtrlID = GetDlgCtrlID(pMsg->hwnd);
            if (pMsg->hwnd == s_pWebBrowser->GetControlWindow() ||
                pMsg->hwnd == s_pWebBrowser->GetIEServerWindow() ||
                pMsg->hwnd == s_hMainWnd)
            {
                nCtrlID = ID_BROWSER;
            }
            INT nCount = 0;
            if (::GetAsyncKeyState(VK_SHIFT) < 0)
            {
                for (size_t i = 0; i < ARRAYSIZE(s_control_ids); ++i)
                {
                    if (s_control_ids[i] == nCtrlID)
                    {
                        HWND hwnd = NULL;
                        do
                        {
                            i += ARRAYSIZE(s_control_ids) - 1;
                            i %= (INT)ARRAYSIZE(s_control_ids);
                            nCtrlID = s_control_ids[i];
                            if (nCtrlID == ID_BROWSER)
                            {
                                HWND hwndServer = s_pWebBrowser->GetIEServerWindow();
                                ::SetFocus(hwndServer);
                                return TRUE;
                            }
                            hwnd = GetDlgItem(s_hMainWnd, s_control_ids[i]);
                            if (++nCount > ARRAYSIZE(s_control_ids))
                                return TRUE;
                        } while (!::IsWindowEnabled(hwnd));
                        if (nCtrlID == ID_ADDRESS_BAR)
                        {
                            SendMessage(s_hAddressBar, EM_SETSEL, 0, -1);
                        }
                        ::SetFocus(hwnd);
                        return TRUE;
                    }
                }
            }
            else
            {
                for (size_t i = 0; i < ARRAYSIZE(s_control_ids); ++i)
                {
                    if (s_control_ids[i] == nCtrlID)
                    {
                        HWND hwnd = NULL;
                        do
                        {
                            i += 1;
                            i %= (INT)ARRAYSIZE(s_control_ids);
                            nCtrlID = s_control_ids[i];
                            if (nCtrlID == ID_BROWSER)
                            {
                                HWND hwndServer = s_pWebBrowser->GetIEServerWindow();
                                ::SetFocus(hwndServer);
                                return TRUE;
                            }
                            hwnd = GetDlgItem(s_hMainWnd, s_control_ids[i]);
                            if (++nCount > ARRAYSIZE(s_control_ids))
                                return TRUE;
                        } while (!::IsWindowEnabled(hwnd));
                        if (nCtrlID == ID_ADDRESS_BAR)
                        {
                            SendMessage(s_hAddressBar, EM_SETSEL, 0, -1);
                        }
                        ::SetFocus(hwnd);
                        return TRUE;
                    }
                }
            }
        }
        break;
    }

    return FALSE;
}

INT WINAPI
WinMain(HINSTANCE   hInstance,
        HINSTANCE   hPrevInstance,
        LPSTR       lpCmdLine,
        INT         nCmdShow)
{
    WNDCLASS wc;

    OleInitialize(NULL);
    InitCommonControls();
    s_hInst = hInstance;

    ZeroMemory(&wc, sizeof(wc));
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(1));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_3DFACE + 1);
    wc.lpszClassName = s_szName;
    if (!RegisterClass(&wc))
    {
        MessageBoxA(NULL, "RegisterClass failed", NULL, MB_ICONERROR);
        return 1;
    }

    DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    DWORD exstyle = 0;
    HWND hwnd = CreateWindowEx(exstyle, s_szName, s_szName, style,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        NULL, NULL, hInstance, NULL);
    if (!hwnd)
    {
        MessageBoxA(NULL, "CreateWindow failed", NULL, MB_ICONERROR);
        return 2;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        if (PreProcessBrowserKeys(&msg))
            continue;

        if (TranslateAccelerator(hwnd, s_hAccel, &msg))
            continue;

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    OleUninitialize();

    return 0;
}
