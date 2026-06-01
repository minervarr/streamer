#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <string>
#include "MainWindow.h"
#include "Log.h"
#include "Config.h"
#include "Strings.h"

static LONG WINAPI CrashHandler(EXCEPTION_POINTERS* ep) {
    wchar_t buf[160];
    swprintf_s(buf, L"CRASH: exception 0x%08X at 0x%p",
        ep->ExceptionRecord->ExceptionCode,
        ep->ExceptionRecord->ExceptionAddress);
    Log::Write(buf);
    Log::Close();
    return EXCEPTION_CONTINUE_SEARCH;
}

static bool IsEditFocused(HWND* out) {
    HWND h = GetFocus();
    wchar_t cls[16] = {};
    GetClassNameW(h, cls, 16);
    if (_wcsicmp(cls, L"Edit") != 0) return false;
    if (out) *out = h;
    return true;
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow) {
    SetUnhandledExceptionFilter(CrashHandler);

    Log::Open();
    Log::Write(L"App started");

    {
        Config cfg = Config::Load();
        if (!cfg.language.empty())
            SetLang(StrToLang(cfg.language.c_str()));
        else
            DetectLang();
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    MainWindow win;
    if (!win.Create(hInst)) { Log::Write(L"MainWindow::Create failed"); Log::Close(); return 1; }
    win.Show(nCmdShow);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_KEYDOWN) {
            HWND hFocus = nullptr;
            bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

            // Enter: forward to parent panel (SearchPanel/MainWindow) so its handler fires
            if (msg.wParam == VK_RETURN && IsEditFocused(&hFocus)) {
                SendMessageW(GetParent(hFocus), WM_KEYDOWN, VK_RETURN, msg.lParam);
                continue;
            }
            // Ctrl+A: select all
            if (msg.wParam == 'A' && ctrl && IsEditFocused(&hFocus)) {
                SendMessageW(hFocus, EM_SETSEL, 0, -1);
                continue;
            }
            // Ctrl+Del: delete next word (native EDIT behavior, bypass IsDialogMessage)
            if (msg.wParam == VK_DELETE && ctrl && IsEditFocused(&hFocus)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
                continue;
            }
            // Ctrl+Backspace: delete previous word (not natively supported by EDIT controls)
            if (msg.wParam == VK_BACK && ctrl && IsEditFocused(&hFocus)) {
                DWORD start = 0, end = 0;
                SendMessageW(hFocus, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
                if (start != end) {
                    SendMessageW(hFocus, EM_REPLACESEL, TRUE, (LPARAM)L"");
                } else if (start > 0) {
                    int len = GetWindowTextLengthW(hFocus);
                    std::wstring text(len + 1, 0);
                    GetWindowTextW(hFocus, text.data(), len + 1);
                    int pos = (int)start;
                    while (pos > 0 && (text[pos-1]==L' '||text[pos-1]==L'\t')) pos--;
                    while (pos > 0 && text[pos-1]!=L' ' && text[pos-1]!=L'\t') pos--;
                    SendMessageW(hFocus, EM_SETSEL, pos, start);
                    SendMessageW(hFocus, EM_REPLACESEL, TRUE, (LPARAM)L"");
                }
                continue;
            }
        }
        if (!IsDialogMessage(win.GetHwnd(), &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    CoUninitialize();
    Log::Write(L"App exiting normally");
    Log::Close();
    return (int)msg.wParam;
}
