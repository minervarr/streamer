#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <string>
#include "LoginDialog.h"
#include "Strings.h"

using namespace Microsoft::WRL;

// Extracts a single query-parameter value from a URL string.
static std::wstring QueryParam(const std::wstring& url, const std::wstring& key) {
    std::wstring needle = key + L"=";
    auto pos = url.find(needle);
    if (pos == std::wstring::npos) return {};
    pos += needle.size();
    auto end = url.find(L'&', pos);
    return url.substr(pos, end == std::wstring::npos ? std::wstring::npos : end - pos);
}

// ── WndProc ───────────────────────────────────────────────────────────────────

LRESULT CALLBACK LoginDialog::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    LoginDialog* self = nullptr;
    if (msg == WM_NCCREATE) {
        self = reinterpret_cast<LoginDialog*>(reinterpret_cast<CREATESTRUCT*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<LoginDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMsg(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT LoginDialog::HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        CreateWindowExW(0, L"BUTTON", T(L"Cancel"),
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_CANCEL, nullptr, nullptr);
        return 0;

    case WM_SIZE: {
        int w = LOWORD(lp), h = HIWORD(lp);
        const int btnH = 34;
        if (m_ctrl) {
            RECT r = { 0, 0, w, h - btnH - 4 };
            m_ctrl->put_Bounds(r);
        }
        HWND hBtn = GetDlgItem(hwnd, ID_CANCEL);
        if (hBtn) SetWindowPos(hBtn, nullptr, w/2 - 50, h - btnH, 100, btnH - 2, SWP_NOZORDER);
        return 0;
    }

    case WM_COMMAND:
        if (LOWORD(wp) == ID_CANCEL) { Finish(false); return 0; }
        break;

    case WM_WV_DONE:
        m_ok = (lp != 0);
        PostQuitMessage(0);
        return 0;

    case WM_CLOSE:
        Finish(false);
        return 0;

    case WM_DESTROY:
        if (!m_done) PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ── Public entry point ────────────────────────────────────────────────────────

bool LoginDialog::Show(HWND parent, HINSTANCE hInst, const std::wstring& appId) {
    m_parent = parent;
    m_appId  = appId;

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = L"QobuzLoginDialog";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    RECT pr = {}; GetWindowRect(parent, &pr);
    int dw = 1100, dh = 700;
    int px = pr.left + (pr.right  - pr.left - dw) / 2;
    int py = pr.top  + (pr.bottom - pr.top  - dh) / 2;

    EnableWindow(parent, FALSE);
    CreateWindowExW(WS_EX_DLGMODALFRAME, L"QobuzLoginDialog",
        T(L"Login with Qobuz"),
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME,
        px, py, dw, dh, parent, nullptr, hInst, this);

    if (!m_hwnd) { EnableWindow(parent, TRUE); return false; }

    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);

    CreateWebView(hInst);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Shut down WebView2 before destroying the host window to prevent
    // dangling COM callbacks from firing against a dead HWND.
    if (m_ctrl) { m_ctrl->Close(); m_ctrl = nullptr; }
    m_wv = nullptr;

    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }

    return m_ok && !code.empty();
}

// ── WebView2 init ─────────────────────────────────────────────────────────────

void LoginDialog::CreateWebView(HINSTANCE) {
    HWND hwnd = m_hwnd;

    // Use a timestamped temp folder so each login starts with a clean slate (no saved cookies/session).
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    SYSTEMTIME st; GetSystemTime(&st);
    wchar_t udFolder[MAX_PATH];
    swprintf_s(udFolder, L"%sstreamer-login-%04d%02d%02d%02d%02d%02d",
        tempPath, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    CreateCoreWebView2EnvironmentWithOptions(nullptr, udFolder, nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this, hwnd](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(hr) || !env) { PostMessage(hwnd, WM_WV_DONE, 0, 0); return hr; }

                env->CreateCoreWebView2Controller(hwnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this, hwnd](HRESULT hr2, ICoreWebView2Controller* ctrl) -> HRESULT {
                            if (FAILED(hr2) || !ctrl) {
                                MessageBoxW(hwnd,
                                    T(L"WebView2 could not initialize.\nMake sure Microsoft Edge is installed."),
                                    T(L"Login Error"), MB_OK | MB_ICONERROR);
                                PostMessage(hwnd, WM_WV_DONE, 0, 0);
                                return hr2;
                            }

                            m_ctrl = ctrl;
                            ctrl->get_CoreWebView2(&m_wv);

                            // Fit to window
                            RECT r; GetClientRect(hwnd, &r); r.bottom -= 38;
                            ctrl->put_Bounds(r);
                            ctrl->put_IsVisible(TRUE);

                            // Intercept the OAuth redirect to extract code_autorisation
                            EventRegistrationToken tok;
                            m_wv->add_NavigationStarting(
                                Callback<ICoreWebView2NavigationStartingEventHandler>(
                                    [this, hwnd](ICoreWebView2*,
                                                 ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                                        LPWSTR uriRaw = nullptr;
                                        args->get_Uri(&uriRaw);
                                        std::wstring uri(uriRaw ? uriRaw : L"");
                                        CoTaskMemFree(uriRaw);

                                        std::wstring c = QueryParam(uri, L"code_autorisation");
                                        if (c.empty()) c = QueryParam(uri, L"code");
                                        if (!c.empty()) {
                                            code = c;
                                            args->put_Cancel(TRUE);
                                            PostMessage(hwnd, WM_WV_DONE, 0, 1);
                                        }
                                        return S_OK;
                                    }).Get(), &tok);

                            // Navigate to Qobuz OAuth sign-in
                            std::wstring oauthUrl =
                                L"https://www.qobuz.com/signin/oauth?ext_app_id=" + m_appId +
                                L"&redirect_url=https%3A%2F%2Fplay.qobuz.com%2Fdiscover";
                            m_wv->Navigate(oauthUrl.c_str());
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());
}

void LoginDialog::Finish(bool ok) {
    if (m_done) return;
    m_done = true;
    m_ok   = ok;
    PostQuitMessage(0);
}
