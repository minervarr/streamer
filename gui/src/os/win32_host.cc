// Win32 skin of the GUI (host.hh), on the engine's raw-Win32 backend
// (framework/Vk_Canvas_Lb_LAW/platform/windows/). Single window, same shape
// as wayland_host.cc.

#include "../host.hh"

#include "win32_platform.hh"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>   // ShellExecuteW
#include <objbase.h>    // CoInitializeEx / CoCreateInstance / CoTaskMemFree
#define INITGUID
#include <shlobj.h>     // IFileOpenDialog, FOLDERID_*
#include <shobjidl.h>   // IFileOpenDialog, CLSID_FileOpenDialog

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

namespace {

// UTF-8 -> UTF-16, using MultiByteToWideChar's own length-probe idiom.
std::wstring widen(const std::string& utf8)
{
    if (utf8.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, w.data(), n);
    w.resize(static_cast<size_t>(n) - 1); // drop the embedded NUL
    return w;
}

// UTF-16 -> UTF-8, same idiom in reverse.
std::string narrow(const wchar_t* utf16)
{
    if (!utf16 || !*utf16) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, utf16, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, utf16, -1, s.data(), n, nullptr, nullptr);
    s.resize(static_cast<size_t>(n) - 1);
    return s;
}

class Win32Host : public gui::AppHost {
public:
    bool init() override
    {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = &Win32Host::wnd_proc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        // IDC_ARROW expands via MAKEINTRESOURCE, which is ANSI unless UNICODE
        // is defined project-wide (it isn't here) — spell it out for the W API.
        wc.hCursor       = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        wc.lpszClassName = L"streamer_gui_window";
        RegisterClassExW(&wc);

        DWORD style = WS_OVERLAPPEDWINDOW;
        RECT rc{0, 0, 1280, 800};
        AdjustWindowRect(&rc, style, FALSE);
        hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"streamer", style,
                                 CW_USEDEFAULT, CW_USEDEFAULT,
                                 rc.right - rc.left, rc.bottom - rc.top,
                                 nullptr, nullptr, wc.hInstance, nullptr);
        if (!hwnd_) {
            std::fprintf(stderr, "[x] CreateWindowExW failed\n");
            return false;
        }
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        ShowWindow(hwnd_, SW_SHOW);

        provider_ = std::make_unique<Win32SurfaceProvider>(hwnd_);
        // Desktop on MAILBOX: 3 swapchain images (see renderer.hh's ctor note).
        renderer_ = std::make_unique<Renderer>(*provider_, assets_, 3);

        com_ok_ = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
        return true;
    }

    ~Win32Host() override
    {
        if (com_ok_) CoUninitialize();
        if (hwnd_) DestroyWindow(hwnd_);
    }

    AssetReader& assets()   override { return assets_; }
    Renderer&    renderer() override { return *renderer_; }

    void pump(int timeout_ms, FrameInput& input) override
    {
        pump_sink_ = &input;
        // Sleep up to timeout_ms for a window message, then drain everything
        // pending. A plain PeekMessage loop would busy-spin; a plain
        // GetMessage can't honor a timeout.
        MsgWaitForMultipleObjects(0, nullptr, FALSE,
                                   static_cast<DWORD>(timeout_ms), QS_ALLINPUT);
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                quit_ = true;
                break;
            }
            TranslateMessage(&msg); // needed for WM_CHAR to be generated
            DispatchMessageW(&msg);
        }
        pump_sink_ = nullptr;

        if (resized_) {
            resized_ = false;
            renderer_->notifyResized();
            dirty_ = true;
        }
    }

    bool quit_requested() override { return quit_; }
    bool take_dirty() override { bool d = dirty_; dirty_ = false; return d; }

    void beep() override
    {
        MessageBeep(MB_ICONEXCLAMATION);
    }

    bool open_url(const std::string& url) override
    {
        // Same guard as the Linux host: only ever hand a plain http(s) URL to
        // the shell, never anything that could be misread as a flag/path.
        if (url.find("://") == std::string::npos) return false;
        if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) return false;

        std::wstring w = widen(url);
        HINSTANCE r = ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<INT_PTR>(r) > 32;
    }

    void pick_directory(std::function<void(const std::string&)> cb) override
    {
        if (!com_ok_) { cb(""); return; }

        IFileOpenDialog* dlg = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&dlg));
        if (FAILED(hr) || !dlg) { cb(""); return; }

        DWORD opts = 0;
        dlg->GetOptions(&opts);
        dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);

        std::string result;
        if (SUCCEEDED(dlg->Show(hwnd_))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&item)) && item) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                    result = narrow(path);
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dlg->Release();
        cb(result);
    }

    void set_clipboard_text(const std::string& utf8) override
    {
        std::wstring w = widen(utf8);
        if (!OpenClipboard(hwnd_)) return;
        EmptyClipboard();
        size_t bytes = (w.size() + 1) * sizeof(wchar_t);
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (h) {
            void* dst = GlobalLock(h);
            if (dst) {
                memcpy(dst, w.c_str(), bytes);
                GlobalUnlock(h);
                // Clipboard owns `h` once SetClipboardData succeeds; do not
                // GlobalFree it ourselves.
                if (!SetClipboardData(CF_UNICODETEXT, h))
                    GlobalFree(h);
            } else {
                GlobalFree(h);
            }
        }
        CloseClipboard();
    }

    std::string get_clipboard_text() override
    {
        if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return "";
        if (!OpenClipboard(hwnd_)) return "";
        std::string result;
        HGLOBAL h = GetClipboardData(CF_UNICODETEXT);
        if (h) {
            const wchar_t* src = static_cast<const wchar_t*>(GlobalLock(h));
            if (src) {
                result = narrow(src);
                GlobalUnlock(h);
            }
        }
        CloseClipboard();
        return result;
    }

private:
    static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        auto* self = reinterpret_cast<Win32Host*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self && self->pump_sink_ &&
            win32_translate_input(hwnd, msg, wp, lp, *self->pump_sink_))
            return 0;

        switch (msg) {
            case WM_CLOSE:
            case WM_DESTROY:
                if (self) self->quit_ = true;
                PostQuitMessage(0);
                return 0;
            case WM_SIZE:
                if (self) self->resized_ = true;
                return 0;
            default:
                return DefWindowProcW(hwnd, msg, wp, lp);
        }
    }

    FileAssetReader assets_;
    HWND hwnd_ = nullptr;
    std::unique_ptr<Win32SurfaceProvider> provider_;
    std::unique_ptr<Renderer>             renderer_;

    FrameInput* pump_sink_ = nullptr; // valid only for the duration of pump()
    bool quit_    = false;
    bool resized_ = false;
    bool dirty_   = false;
    bool com_ok_  = false;
};

} // namespace

namespace gui {
std::unique_ptr<AppHost> make_host() { return std::make_unique<Win32Host>(); }
} // namespace gui
