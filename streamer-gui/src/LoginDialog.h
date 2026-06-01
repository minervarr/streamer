#pragma once
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <string>

// Modal dialog containing an embedded Edge WebView2 control.
// Navigates to the Qobuz OAuth sign-in page using the supplied app_id.
// Intercepts the redirect back to play.qobuz.com/discover and extracts
// the code_autorisation query parameter, which the caller exchanges for
// a session token via streamer.exe oauth-login.
class LoginDialog {
public:
    bool Show(HWND parent, HINSTANCE hInst, const std::wstring& appId);

    std::wstring code;  // set on success: the code_autorisation value

private:
    HWND m_hwnd   = nullptr;
    HWND m_parent = nullptr;
    bool m_done   = false;
    bool m_ok     = false;

    std::wstring m_appId;

    Microsoft::WRL::ComPtr<ICoreWebView2Controller> m_ctrl;
    Microsoft::WRL::ComPtr<ICoreWebView2>           m_wv;

    void CreateWebView(HINSTANCE hInst);
    void Finish(bool ok);

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMsg(HWND, UINT, WPARAM, LPARAM);

    static constexpr int  ID_CANCEL  = 1;
    static constexpr UINT WM_WV_DONE = WM_USER + 11;
};
