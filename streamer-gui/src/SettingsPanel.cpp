#include <windows.h>
#include <shlobj.h>
#include <string>
#include "SettingsPanel.h"

// ── helpers ──────────────────────────────────────────────────────────────────

static HWND MakeStatic(HWND parent, const wchar_t* text, bool bold, HFONT hf, HFONT hfBold) {
    HWND h = CreateWindowExW(0, L"STATIC", text,
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 0, 0, parent, nullptr, nullptr, nullptr);
    SendMessage(h, WM_SETFONT, (WPARAM)(bold ? hfBold : hf), TRUE);
    return h;
}

static HWND MakeEdit(HWND parent, int id, HFONT hf, bool password = false) {
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL;
    if (password) style |= ES_PASSWORD;
    HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", style,
        0, 0, 0, 0, parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessage(h, WM_SETFONT, (WPARAM)hf, TRUE);
    return h;
}

static HWND MakeBtn(HWND parent, int id, const wchar_t* label, HFONT hf) {
    HWND h = CreateWindowExW(0, L"BUTTON", label,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessage(h, WM_SETFONT, (WPARAM)hf, TRUE);
    return h;
}

static std::wstring GetText(HWND hwnd) {
    int len = GetWindowTextLengthW(hwnd);
    if (len <= 0) return {};
    std::wstring s(len + 1, 0);
    GetWindowTextW(hwnd, s.data(), len + 1);
    s.resize(len);
    return s;
}

static std::string W2A(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

static std::wstring A2W(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

static std::wstring EscapeArg(const std::wstring& v) {
    std::wstring out;
    for (wchar_t c : v) {
        if (c == L'"') out += L'\\';
        out += c;
    }
    return out;
}

// ── WndProc ──────────────────────────────────────────────────────────────────

LRESULT CALLBACK SettingsPanel::PanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    SettingsPanel* self = nullptr;
    if (msg == WM_NCCREATE) {
        self = reinterpret_cast<SettingsPanel*>(
            reinterpret_cast<CREATESTRUCT*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<SettingsPanel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMsg(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT SettingsPanel::HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_COMMAND) {
        switch (LOWORD(wp)) {
        case ID_SAVE:    SaveFields(); return 0;
        case ID_LOGIN:   DoLogin();    return 0;
        case ID_DIR_BTN: BrowseDir();  return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ── Create ────────────────────────────────────────────────────────────────────

void SettingsPanel::Create(HWND parent, HINSTANCE hInst, Renderer* rend, Config* cfg) {
    m_rend = rend;
    m_cfg  = cfg;

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = PanelProc;
        wc.hInstance     = hInst;
        wc.lpszClassName = L"SettingsPanel";
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassExW(&wc);
        registered = true;
    }

    CreateWindowExW(0, L"SettingsPanel", L"", WS_CHILD,
        0, 0, 0, 0, parent, nullptr, hInst, this);

    HFONT hf = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HFONT hfBold = CreateFontW(-14, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

    // Section headings
    m_hHdCred  = MakeStatic(m_hwnd, L"Credentials", true,  hf, hfBold);
    m_hHdSet   = MakeStatic(m_hwnd, L"Settings",    true,  hf, hfBold);

    // Labels
    m_hLAppId   = MakeStatic(m_hwnd, L"App ID",          false, hf, hfBold);
    m_hLSecret  = MakeStatic(m_hwnd, L"App Secret",      false, hf, hfBold);
    m_hLUserId  = MakeStatic(m_hwnd, L"User ID",         false, hf, hfBold);
    m_hLToken   = MakeStatic(m_hwnd, L"Auth Token",      false, hf, hfBold);
    m_hLDir     = MakeStatic(m_hwnd, L"Download Dir",    false, hf, hfBold);
    m_hLQuality = MakeStatic(m_hwnd, L"Quality",         false, hf, hfBold);
    m_hLConc    = MakeStatic(m_hwnd, L"Concurrency",     false, hf, hfBold);
    m_hLRpm     = MakeStatic(m_hwnd, L"Requests / min",  false, hf, hfBold);

    // Inputs
    m_hAppId   = MakeEdit(m_hwnd, ID_APP_ID,  hf);
    m_hSecret  = MakeEdit(m_hwnd, ID_SECRET,  hf);
    m_hUserId  = MakeEdit(m_hwnd, ID_USER_ID, hf);
    m_hToken   = MakeEdit(m_hwnd, ID_TOKEN,   hf, true);
    m_hDir     = MakeEdit(m_hwnd, ID_DIR,     hf);
    m_hDirBtn  = MakeBtn(m_hwnd, ID_DIR_BTN, L"Browse...", hf);

    m_quality.Create(m_hwnd, hInst, ID_QUALITY);
    for (auto* q : {L"mp3", L"flac", L"flac-hi", L"flac-ultra"})
        m_quality.AddItem(q);

    m_hConc    = MakeEdit(m_hwnd, ID_CONC, hf);
    m_hRpm     = MakeEdit(m_hwnd, ID_RPM,  hf);
    m_hSaveBtn  = MakeBtn(m_hwnd, ID_SAVE,  L"Save",  hf);
    m_hLoginBtn = MakeBtn(m_hwnd, ID_LOGIN, L"Login", hf);

    LoadFields();
}

// ── Layout ────────────────────────────────────────────────────────────────────

void SettingsPanel::Resize(int x, int y, int w, int h) {
    SetWindowPos(m_hwnd, nullptr, x, y, w, h, SWP_NOZORDER);

    const int pad  = 16;
    const int lw   = 130;   // label column width
    const int eh   = 24;    // edit height
    const int row  = 36;    // row stride
    const int hdH  = 22;
    const int gapH = 12;    // gap between sections

    int lx = pad;
    int ix = pad + lw + 10; // input x
    int iw = std::min(400, w - ix - pad);
    int cy = pad;

    auto placeRow = [&](HWND lbl, HWND input, int inputW = -1) {
        int iW = inputW < 0 ? iw : inputW;
        SetWindowPos(lbl,   nullptr, lx, cy + (eh - 16) / 2, lw, 16, SWP_NOZORDER);
        SetWindowPos(input, nullptr, ix, cy, iW, eh, SWP_NOZORDER);
        cy += row;
    };

    // Credentials heading
    SetWindowPos(m_hHdCred, nullptr, lx, cy, 200, hdH, SWP_NOZORDER);
    cy += hdH + 6;

    placeRow(m_hLAppId,  m_hAppId);
    placeRow(m_hLSecret, m_hSecret);
    placeRow(m_hLUserId, m_hUserId);
    placeRow(m_hLToken,  m_hToken);

    cy += gapH;

    // Settings heading
    SetWindowPos(m_hHdSet, nullptr, lx, cy, 200, hdH, SWP_NOZORDER);
    cy += hdH + 6;

    // Dir row: edit + browse button
    SetWindowPos(m_hLDir,   nullptr, lx, cy + (eh - 16) / 2, lw, 16, SWP_NOZORDER);
    SetWindowPos(m_hDir,    nullptr, ix, cy, iw - 82, eh, SWP_NOZORDER);
    SetWindowPos(m_hDirBtn, nullptr, ix + iw - 78, cy, 74, eh, SWP_NOZORDER);
    cy += row;

    const int itemH = 24;
    const int qualH = std::min(4 * itemH, h - cy - pad - 40);
    SetWindowPos(m_hLQuality, nullptr, lx, cy + (qualH - 16) / 2, lw, 16, SWP_NOZORDER);
    m_quality.Resize(ix, cy, 160, qualH);
    cy += qualH + 8;

    SetWindowPos(m_hLConc, nullptr, lx, cy + (eh - 16) / 2, lw, 16, SWP_NOZORDER);
    SetWindowPos(m_hConc,  nullptr, ix, cy, 60, eh, SWP_NOZORDER);
    cy += row;

    SetWindowPos(m_hLRpm, nullptr, lx, cy + (eh - 16) / 2, lw, 16, SWP_NOZORDER);
    SetWindowPos(m_hRpm,  nullptr, ix, cy, 80, eh, SWP_NOZORDER);
    cy += row + gapH;

    // Action buttons
    SetWindowPos(m_hSaveBtn,  nullptr, ix,       cy, 100, 30, SWP_NOZORDER);
    SetWindowPos(m_hLoginBtn, nullptr, ix + 110, cy, 100, 30, SWP_NOZORDER);
}

void SettingsPanel::Show(bool visible) {
    ShowWindow(m_hwnd, visible ? SW_SHOW : SW_HIDE);
}

// ── Logic ─────────────────────────────────────────────────────────────────────

void SettingsPanel::LoadFields() {
    SetWindowTextW(m_hAppId,  A2W(m_cfg->app_id).c_str());
    SetWindowTextW(m_hSecret, A2W(m_cfg->app_secret).c_str());
    SetWindowTextW(m_hUserId, A2W(m_cfg->user_id).c_str());
    SetWindowTextW(m_hToken,  A2W(m_cfg->auth_token).c_str());
    SetWindowTextW(m_hDir,    A2W(m_cfg->download_dir).c_str());

    const wchar_t* quals[] = { L"mp3", L"flac", L"flac-hi", L"flac-ultra" };
    auto wq = A2W(m_cfg->quality);
    int sel = 1;
    for (int i = 0; i < 4; i++) if (wq == quals[i]) { sel = i; break; }
    m_quality.SetSel(sel);

    SetWindowTextW(m_hConc, std::to_wstring(m_cfg->concurrency).c_str());
    SetWindowTextW(m_hRpm,  std::to_wstring(m_cfg->requests_per_min).c_str());
}

void SettingsPanel::SaveFields() {
    m_cfg->app_id       = W2A(GetText(m_hAppId));
    m_cfg->app_secret   = W2A(GetText(m_hSecret));
    m_cfg->user_id      = W2A(GetText(m_hUserId));
    m_cfg->auth_token   = W2A(GetText(m_hToken));
    m_cfg->download_dir = W2A(GetText(m_hDir));

    int qi = m_quality.GetSel();
    const char* quals[] = { "mp3", "flac", "flac-hi", "flac-ultra" };
    if (qi >= 0 && qi < 4) m_cfg->quality = quals[qi];

    try { m_cfg->concurrency      = std::stoi(W2A(GetText(m_hConc))); } catch (...) {}
    try { m_cfg->requests_per_min = std::stoi(W2A(GetText(m_hRpm)));  } catch (...) {}

    m_cfg->Save();
    MessageBoxW(m_hwnd, L"Configuration saved.", L"Saved", MB_OK | MB_ICONINFORMATION);
}

void SettingsPanel::DoLogin() {
    // Persist fields silently before launching the subprocess
    m_cfg->app_id       = W2A(GetText(m_hAppId));
    m_cfg->app_secret   = W2A(GetText(m_hSecret));
    m_cfg->user_id      = W2A(GetText(m_hUserId));
    m_cfg->auth_token   = W2A(GetText(m_hToken));
    m_cfg->download_dir = W2A(GetText(m_hDir));
    m_cfg->Save();

    if (m_onLog) m_onLog(L"[Login] Attempting login...");

    std::wstring cmd = L"streamer.exe login"
        L" --user-id \"" + EscapeArg(A2W(m_cfg->user_id))   + L"\""
        L" --token \""   + EscapeArg(A2W(m_cfg->auth_token)) + L"\"";

    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return;

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError  = hWrite;

    PROCESS_INFORMATION pi = {};
    bool ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr,
        TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(hWrite);

    if (!ok) {
        CloseHandle(hRead);
        if (m_onLog) m_onLog(L"[Login] Failed to start streamer.exe");
        MessageBoxW(m_hwnd, L"Failed to start streamer.exe", L"Login", MB_OK | MB_ICONERROR);
        return;
    }

    // Read all output and log each line
    char buf[1024];
    std::string partial;
    DWORD read;
    while (ReadFile(hRead, buf, sizeof(buf) - 1, &read, nullptr) && read) {
        buf[read] = 0;
        partial += buf;
        size_t pos;
        while ((pos = partial.find('\n')) != std::string::npos) {
            std::string line = partial.substr(0, pos);
            partial = partial.substr(pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty() && m_onLog) m_onLog(L"[Login] " + A2W(line));
        }
    }
    if (!partial.empty() && m_onLog) m_onLog(L"[Login] " + A2W(partial));
    CloseHandle(hRead);

    WaitForSingleObject(pi.hProcess, 15000);
    DWORD exit = 1;
    GetExitCodeProcess(pi.hProcess, &exit);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);

    if (exit == 0) {
        if (m_onLog) m_onLog(L"[Login] Success.");
        MessageBoxW(m_hwnd, L"Login successful.", L"Login", MB_OK | MB_ICONINFORMATION);
    } else {
        if (m_onLog) m_onLog(L"[Login] Failed (exit code " + std::to_wstring(exit) + L").");
        MessageBoxW(m_hwnd, L"Login failed. Check the Downloads tab for details.", L"Login", MB_OK | MB_ICONERROR);
    }
}

void SettingsPanel::BrowseDir() {
    wchar_t path[MAX_PATH] = {};
    BROWSEINFOW bi = {};
    bi.hwndOwner = m_hwnd;
    bi.pszDisplayName = path;
    bi.lpszTitle = L"Select download directory";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        SHGetPathFromIDListW(pidl, path);
        SetWindowTextW(m_hDir, path);
        CoTaskMemFree(pidl);
    }
}
