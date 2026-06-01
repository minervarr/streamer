#include <windows.h>
#include <shlobj.h>
#include <commctrl.h>
#include <commdlg.h>
#include <fstream>
#include <sstream>
#include <string>
#include "SettingsPanel.h"
#include "Strings.h"

// ── String helpers ────────────────────────────────────────────────────────────

std::wstring SettingsPanel::W2W(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

std::string SettingsPanel::W2A(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring SettingsPanel::GetText(HWND h) {
    int len = GetWindowTextLengthW(h);
    if (len <= 0) return {};
    std::wstring s(len + 1, 0);
    GetWindowTextW(h, s.data(), len + 1);
    s.resize(len);
    return s;
}

static std::wstring EscapeArg(const std::wstring& v) {
    std::wstring out;
    for (wchar_t c : v) { if (c == L'"') out += L'\\'; out += c; }
    return out;
}

static HWND MakeLabel(HWND p, const wchar_t* t, HFONT hf) {
    HWND h = CreateWindowExW(0, L"STATIC", t, WS_CHILD | WS_VISIBLE | SS_LEFT,
        0,0,0,0, p, nullptr, nullptr, nullptr);
    SendMessage(h, WM_SETFONT, (WPARAM)hf, TRUE);
    return h;
}
static HWND MakeEdit(HWND p, int id, HFONT hf, bool pw = false) {
    DWORD st = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | (pw ? ES_PASSWORD : 0);
    HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", st,
        0,0,0,0, p, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessage(h, WM_SETFONT, (WPARAM)hf, TRUE);
    return h;
}
static HWND MakeBtn(HWND p, int id, const wchar_t* t, HFONT hf) {
    HWND h = CreateWindowExW(0, L"BUTTON", t, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0,0,0,0, p, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessage(h, WM_SETFONT, (WPARAM)hf, TRUE);
    return h;
}

// ── WndProc ───────────────────────────────────────────────────────────────────

LRESULT CALLBACK SettingsPanel::PanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    SettingsPanel* self = nullptr;
    if (msg == WM_NCCREATE) {
        self = reinterpret_cast<SettingsPanel*>(reinterpret_cast<CREATESTRUCT*>(lp)->lpCreateParams);
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
        case ID_ADD_ACCT:
            {
                m_cfg->accounts.push_back(Account{});
                RefreshAccountList();
                // Select the new row
                int newRow = (int)m_cfg->accounts.size() - 1;
                ListView_SetItemState(m_hAcctList, newRow, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                m_selAcct = newRow;
                LoadAccountFields(newRow);
                ShowAccountEdit(true);
            }
            return 0;
        case ID_DEL_ACCT:
            if (m_selAcct >= 0 && m_selAcct < (int)m_cfg->accounts.size()) {
                m_cfg->accounts.erase(m_cfg->accounts.begin() + m_selAcct);
                m_cfg->Save();
                m_selAcct = -1;
                RefreshAccountList();
                ShowAccountEdit(false);
            }
            return 0;
        case ID_LOGIN:    DoWebLogin();     return 0;
        case ID_DIR_BTN:  BrowseDir();      return 0;
        case ID_SAVE_SET: SaveSettings();   return 0;
        case ID_EXPORT:   ExportAccounts(); return 0;
        case ID_IMPORT:   ImportAccounts(); return 0;
        }
    }
    if (msg == WM_NOTIFY) {
        NMHDR* nm = reinterpret_cast<NMHDR*>(lp);
        if (nm->idFrom == ID_ACCT_LIST && nm->code == LVN_ITEMCHANGED) {
            NMLISTVIEW* lv = reinterpret_cast<NMLISTVIEW*>(lp);
            if ((lv->uNewState & LVIS_SELECTED) && lv->iItem >= 0) {
                m_selAcct = lv->iItem;
                LoadAccountFields(m_selAcct);
                ShowAccountEdit(true);
            }
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ── Create ────────────────────────────────────────────────────────────────────

void SettingsPanel::Create(HWND parent, HINSTANCE hInst, Renderer* rend, Config* cfg) {
    m_rend  = rend;
    m_cfg   = cfg;
    m_hInst = hInst;

    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = PanelProc;
        wc.hInstance     = hInst;
        wc.lpszClassName = L"SettingsPanel";
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassExW(&wc);
        reg = true;
    }
    CreateWindowExW(0, L"SettingsPanel", L"", WS_CHILD,
        0,0,0,0, parent, nullptr, hInst, this);

    HFONT hf = CreateFontW(-14,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    HFONT hfB= CreateFontW(-14,0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");

    // Account list
    m_hHdAcct = CreateWindowExW(0,L"STATIC",T(L"Accounts"),WS_CHILD|WS_VISIBLE|SS_LEFT,0,0,0,0,m_hwnd,nullptr,nullptr,nullptr);
    SendMessage(m_hHdAcct, WM_SETFONT, (WPARAM)hfB, TRUE);

    m_hAcctList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0,0,0,0, m_hwnd, (HMENU)(INT_PTR)ID_ACCT_LIST, hInst, nullptr);
    ListView_SetExtendedListViewStyle(m_hAcctList, LVS_EX_FULLROWSELECT);
    SendMessage(m_hAcctList, WM_SETFONT, (WPARAM)hf, TRUE);

    auto addCol = [&](const wchar_t* name, int w, int i) {
        LVCOLUMNW col = {}; col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = const_cast<wchar_t*>(name); col.cx = w;
        ListView_InsertColumn(m_hAcctList, i, &col);
    };
    addCol(T(L"Country"), 70,  0);
    addCol(T(L"Email"),   220, 1);
    addCol(T(L"Status"),  100, 2);

    m_hAddBtn = MakeBtn(m_hwnd, ID_ADD_ACCT, T(L"+ Add"),  hf);
    m_hDelBtn = MakeBtn(m_hwnd, ID_DEL_ACCT, T(L"Remove"), hf);

    // Per-account login form
    m_hLCountry  = MakeLabel(m_hwnd, T(L"Country"),  hf);  m_hCountry  = MakeEdit(m_hwnd, ID_COUNTRY,  hf);
    m_hLEmail    = MakeLabel(m_hwnd, T(L"Email"),    hf);  m_hEmail    = MakeEdit(m_hwnd, ID_EMAIL,    hf);
    m_hLPassword = MakeLabel(m_hwnd, T(L"Password"), hf);  m_hPassword = MakeEdit(m_hwnd, ID_PASSWORD, hf, true);
    m_hLoginBtn  = MakeBtn(m_hwnd, ID_LOGIN, T(L"Login with Qobuz"), hf);

    // Global settings
    m_hHdSet = CreateWindowExW(0,L"STATIC",T(L"Settings"),WS_CHILD|WS_VISIBLE|SS_LEFT,0,0,0,0,m_hwnd,nullptr,nullptr,nullptr);
    SendMessage(m_hHdSet, WM_SETFONT, (WPARAM)hfB, TRUE);

    m_hLDir   = MakeLabel(m_hwnd, T(L"Download Dir"),   hf);
    m_hDir    = MakeEdit(m_hwnd, ID_DIR, hf);
    m_hDirBtn = MakeBtn(m_hwnd, ID_DIR_BTN, T(L"Browse..."), hf);

    m_hLQuality = MakeLabel(m_hwnd, T(L"Quality"), hf);
    m_quality.Create(m_hwnd, hInst, ID_QUALITY);
    for (auto* q : {L"mp3", L"flac", L"flac-hi", L"flac-ultra"})
        m_quality.AddItem(q);

    m_hLConc = MakeLabel(m_hwnd, T(L"Concurrency"),    hf);  m_hConc = MakeEdit(m_hwnd, ID_CONC, hf);
    m_hLRpm  = MakeLabel(m_hwnd, T(L"Requests / min"), hf);  m_hRpm  = MakeEdit(m_hwnd, ID_RPM,  hf);
    m_hSaveSet = MakeBtn(m_hwnd, ID_SAVE_SET, T(L"Save Settings"), hf);

    m_hExportBtn = MakeBtn(m_hwnd, ID_EXPORT, T(L"Export Accounts"), hf);
    m_hImportBtn = MakeBtn(m_hwnd, ID_IMPORT, T(L"Import Accounts"), hf);

    // Language picker
    m_hLLang = MakeLabel(m_hwnd, T(L"Language"), hf);
    m_langCombo = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP,
        0, 0, 0, 0, m_hwnd, (HMENU)(INT_PTR)ID_LANG, nullptr, nullptr);
    SendMessage(m_langCombo, WM_SETFONT, (WPARAM)hf, TRUE);
    SendMessageW(m_langCombo, CB_ADDSTRING, 0, (LPARAM)L"English");
    SendMessageW(m_langCombo, CB_ADDSTRING, 0, (LPARAM)L"Español");
    SendMessageW(m_langCombo, CB_SETCURSEL, (GetLang() == Lang::Es) ? 1 : 0, 0);

    // Load settings fields
    SetWindowTextW(m_hDir, W2W(m_cfg->download_dir).c_str());
    const wchar_t* quals[] = { L"mp3", L"flac", L"flac-hi", L"flac-ultra" };
    auto wq = W2W(m_cfg->quality); int sel = 1;
    for (int i = 0; i < 4; i++) if (wq == quals[i]) { sel = i; break; }
    m_quality.SetSel(sel);
    SetWindowTextW(m_hConc, std::to_wstring(m_cfg->concurrency).c_str());
    SetWindowTextW(m_hRpm,  std::to_wstring(m_cfg->requests_per_min).c_str());

    RefreshAccountList();
    ShowAccountEdit(false);
}

// ── Layout ────────────────────────────────────────────────────────────────────

void SettingsPanel::Resize(int x, int y, int w, int h) {
    SetWindowPos(m_hwnd, nullptr, x, y, w, h, SWP_NOZORDER);

    const int pad = 14;
    const int lw  = 100;
    const int eh  = 24;
    const int row = 34;
    const int hdH = 20;
    int lx = pad, ix = pad + lw + 8;
    int iw = std::min(380, w - ix - pad);
    int cy = pad;

    // ── Accounts heading + list ──
    SetWindowPos(m_hHdAcct,   nullptr, lx, cy, 120, hdH, SWP_NOZORDER); cy += hdH + 4;
    int listH = 80;
    int listW = iw + lw + 8;
    SetWindowPos(m_hAcctList, nullptr, lx, cy, listW, listH, SWP_NOZORDER);
    SetWindowPos(m_hAddBtn,   nullptr, lx + listW + 6, cy,      70, eh, SWP_NOZORDER);
    SetWindowPos(m_hDelBtn,   nullptr, lx + listW + 6, cy + 30, 70, eh, SWP_NOZORDER);
    cy += listH + 6;

    // ── Per-account login form ──
    auto placeRow = [&](HWND lbl, HWND inp, int inputW = -1) {
        int iW = inputW < 0 ? iw : inputW;
        SetWindowPos(lbl, nullptr, lx, cy + (eh-16)/2, lw, 16, SWP_NOZORDER);
        SetWindowPos(inp, nullptr, ix, cy, iW, eh, SWP_NOZORDER);
        cy += row;
    };
    placeRow(m_hLCountry,  m_hCountry,  60);
    placeRow(m_hLEmail,    m_hEmail);
    placeRow(m_hLPassword, m_hPassword);
    SetWindowPos(m_hLoginBtn, nullptr, ix, cy, 150, 28, SWP_NOZORDER);
    cy += 38;

    // ── Settings heading ──
    cy += 6;
    SetWindowPos(m_hHdSet, nullptr, lx, cy, 120, hdH, SWP_NOZORDER); cy += hdH + 4;

    // Dir row
    SetWindowPos(m_hLDir,   nullptr, lx, cy + (eh-16)/2, lw, 16, SWP_NOZORDER);
    SetWindowPos(m_hDir,    nullptr, ix, cy, iw - 82, eh, SWP_NOZORDER);
    SetWindowPos(m_hDirBtn, nullptr, ix + iw - 78, cy, 74, eh, SWP_NOZORDER);
    cy += row;

    const int qualH = std::min(4 * 24, h - cy - pad - 80);
    SetWindowPos(m_hLQuality, nullptr, lx, cy + (qualH-16)/2, lw, 16, SWP_NOZORDER);
    m_quality.Resize(ix, cy, 160, qualH); cy += qualH + 8;

    placeRow(m_hLConc, m_hConc, 60);
    placeRow(m_hLRpm,  m_hRpm,  80);

    SetWindowPos(m_hSaveSet,  nullptr, ix,       cy, 120, 28, SWP_NOZORDER);
    cy += 38;

    // Language row
    SetWindowPos(m_hLLang,    nullptr, lx, cy + (eh-16)/2, lw, 16, SWP_NOZORDER);
    SetWindowPos(m_langCombo, nullptr, ix, cy, 120, 200, SWP_NOZORDER);
    cy += row;

    SetWindowPos(m_hExportBtn, nullptr, lx,       cy, 140, 28, SWP_NOZORDER);
    SetWindowPos(m_hImportBtn, nullptr, lx + 150, cy, 140, 28, SWP_NOZORDER);
}

void SettingsPanel::Show(bool visible) {
    ShowWindow(m_hwnd, visible ? SW_SHOW : SW_HIDE);
}

// ── Account helpers ───────────────────────────────────────────────────────────

void SettingsPanel::RefreshAccountList() {
    ListView_DeleteAllItems(m_hAcctList);
    for (int i = 0; i < (int)m_cfg->accounts.size(); i++) {
        auto& a = m_cfg->accounts[i];
        LVITEMW item = {}; item.mask = LVIF_TEXT; item.iItem = i;
        auto wc = W2W(a.country);
        item.pszText = const_cast<wchar_t*>(wc.c_str());
        ListView_InsertItem(m_hAcctList, &item);
        // Column 1: email (or user_id if no email set yet)
        std::wstring ident = a.email.empty() ? W2W(a.user_id) : W2W(a.email);
        ListView_SetItemText(m_hAcctList, i, 1, const_cast<wchar_t*>(ident.c_str()));
        const wchar_t* status = a.auth_token.empty() ? T(L"Not logged in") : T(L"Authenticated");
        ListView_SetItemText(m_hAcctList, i, 2, const_cast<wchar_t*>(status));
    }
}

void SettingsPanel::ShowAccountEdit(bool visible) {
    int sw = visible ? SW_SHOW : SW_HIDE;
    for (HWND h : {m_hLCountry, m_hCountry, m_hLEmail, m_hEmail,
                   m_hLPassword, m_hPassword, m_hLoginBtn})
        ShowWindow(h, sw);
}

void SettingsPanel::LoadAccountFields(int idx) {
    if (idx < 0 || idx >= (int)m_cfg->accounts.size()) return;
    auto& a = m_cfg->accounts[idx];
    SetWindowTextW(m_hCountry, W2W(a.country).c_str());
    SetWindowTextW(m_hEmail,   W2W(a.email).c_str());
    SetWindowTextW(m_hPassword, L"");  // never pre-fill password from storage
}

// ── Web login (via embedded WebView2 browser) ─────────────────────────────────

// Runs a streamer.exe command synchronously and returns its stdout output.
static std::wstring RunStreamerCapture(const std::wstring& args) {
    std::wstring cmd = L"streamer.exe " + args;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return {};
    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = hWrite; si.hStdError = hWrite;
    PROCESS_INFORMATION pi = {};
    bool ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(hWrite);
    if (!ok) { CloseHandle(hRead); return {}; }
    std::string out; char buf[512]; DWORD rd;
    while (ReadFile(hRead, buf, sizeof(buf)-1, &rd, nullptr) && rd) { buf[rd]=0; out += buf; }
    CloseHandle(hRead);
    WaitForSingleObject(pi.hProcess, 30000);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    // trim whitespace
    while (!out.empty() && (out.back()=='\n'||out.back()=='\r'||out.back()==' ')) out.pop_back();
    if (out.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, out.c_str(), -1, nullptr, 0);
    std::wstring w(n-1, 0);
    MultiByteToWideChar(CP_UTF8, 0, out.c_str(), -1, w.data(), n);
    return w;
}

void SettingsPanel::DoWebLogin() {
    if (m_selAcct < 0 || m_selAcct >= (int)m_cfg->accounts.size()) {
        MessageBoxW(m_hwnd, T(L"Select an account slot first."), T(L"Login"), MB_OK | MB_ICONWARNING);
        return;
    }

    std::wstring country = GetText(m_hCountry);
    std::wstring email   = GetText(m_hEmail);

    // Step 1: fetch app_id from web player bundle
    if (m_onLog) m_onLog(L"[Login] Fetching app credentials from Qobuz...");
    std::wstring appId = RunStreamerCapture(L"fetch-app-id");
    if (appId.empty()) {
        MessageBoxW(m_hwnd, T(L"Could not fetch Qobuz app credentials.\nCheck your internet connection."),
                    T(L"Login"), MB_OK | MB_ICONERROR);
        return;
    }

    // Step 2: open OAuth browser — user logs in, we intercept code_autorisation
    LoginDialog dlg;
    if (!dlg.Show(m_hwnd, m_hInst, appId)) return;  // cancelled or WebView2 unavailable

    if (dlg.code.empty()) {
        MessageBoxW(m_hwnd, T(L"Could not obtain authorization code from Qobuz.\nTry logging in again."),
                    T(L"Login"), MB_OK | MB_ICONWARNING);
        return;
    }

    if (m_onLog) m_onLog(L"[Login] Authorization code obtained. Exchanging for credentials...");

    // Step 3: exchange code via streamer.exe oauth-login
    std::wstring cmd = L"streamer.exe oauth-login"
        L" --code \""    + EscapeArg(dlg.code) + L"\""
        L" --country \"" + EscapeArg(country)  + L"\""
        L" --email \""   + EscapeArg(email)     + L"\"";

    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return;

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = hWrite; si.hStdError = hWrite;
    PROCESS_INFORMATION pi = {};
    bool ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(hWrite);
    if (!ok) { CloseHandle(hRead); if (m_onLog) m_onLog(L"[Login] Failed to start streamer.exe"); return; }

    char buf[1024]; std::string partial; DWORD rd;
    while (ReadFile(hRead, buf, sizeof(buf)-1, &rd, nullptr) && rd) {
        buf[rd] = 0; partial += buf;
        size_t pos;
        while ((pos = partial.find('\n')) != std::string::npos) {
            std::string line = partial.substr(0, pos); partial = partial.substr(pos+1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty() && m_onLog) m_onLog(L"[Login] " + W2W(line));
        }
    }
    if (!partial.empty() && m_onLog) m_onLog(W2W(partial));
    CloseHandle(hRead);

    WaitForSingleObject(pi.hProcess, 60000);
    DWORD exitCode = 1; GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);

    if (exitCode == 0) {
        if (m_onLog) m_onLog(L"[Login] Success.");
        *m_cfg = Config::Load();
        RefreshAccountList();
        if (m_selAcct < (int)m_cfg->accounts.size()) {
            ListView_SetItemState(m_hAcctList, m_selAcct, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            LoadAccountFields(m_selAcct);
        }
        MessageBoxW(m_hwnd, T(L"Login successful."), T(L"Login"), MB_OK | MB_ICONINFORMATION);
    } else {
        if (m_onLog) m_onLog(L"[Login] Failed (exit " + std::to_wstring(exitCode) + L"). Check Downloads tab.");
        MessageBoxW(m_hwnd, T(L"Login failed. Check Downloads tab for details."), T(L"Login"), MB_OK | MB_ICONERROR);
    }
}

static std::string StripQuotes(std::string s) {
    while (s.size() >= 2 && ((s.front()=='\'' && s.back()=='\'') || (s.front()=='"' && s.back()=='"')))
        s = s.substr(1, s.size() - 2);
    return s;
}

void SettingsPanel::SaveSettings() {
    m_cfg->download_dir    = StripQuotes(W2A(GetText(m_hDir)));
    int qi = m_quality.GetSel();
    const char* quals[] = { "mp3", "flac", "flac-hi", "flac-ultra" };
    if (qi >= 0 && qi < 4) m_cfg->quality = quals[qi];
    try { m_cfg->concurrency     = std::stoi(W2A(GetText(m_hConc))); } catch (...) {}
    try { m_cfg->requests_per_min= std::stoi(W2A(GetText(m_hRpm)));  } catch (...) {}
    int langIdx = (int)SendMessageW(m_langCombo, CB_GETCURSEL, 0, 0);
    Lang newLang = (langIdx == 1) ? Lang::Es : Lang::En;
    m_cfg->language = LangToStr(newLang);
    SetLang(newLang);
    m_cfg->Save();
    MessageBoxW(m_hwnd, T(L"Settings saved."), T(L"Saved"), MB_OK | MB_ICONINFORMATION);
}

void SettingsPanel::BrowseDir() {
    wchar_t path[MAX_PATH] = {};
    BROWSEINFOW bi = {};
    bi.hwndOwner = m_hwnd;
    bi.pszDisplayName = path;
    bi.lpszTitle = T(L"Select download directory");
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (pidl) { SHGetPathFromIDListW(pidl, path); SetWindowTextW(m_hDir, path); CoTaskMemFree(pidl); }
}

// ── Export / Import ───────────────────────────────────────────────────────────

static std::string JsonEsc(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (c == '"')  { out += "\\\""; }
        else if (c == '\\') { out += "\\\\"; }
        else if (c == '\n') { out += "\\n"; }
        else if (c == '\r') { out += "\\r"; }
        else { out += (char)c; }
    }
    return out;
}

static std::string JsonField(const std::string& key, const std::string& val, bool last = false) {
    return "    \"" + key + "\": \"" + JsonEsc(val) + "\"" + (last ? "" : ",") + "\n";
}

void SettingsPanel::ExportAccounts() {
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner   = m_hwnd;
    ofn.lpstrFilter = L"JSON files\0*.json\0All files\0*.*\0";
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrDefExt = L"json";
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return;

    std::ofstream f(path);
    f << "[\n";
    for (int i = 0; i < (int)m_cfg->accounts.size(); i++) {
        auto& a = m_cfg->accounts[i];
        f << "  {\n";
        f << JsonField("country",    a.country);
        f << JsonField("email",      a.email);
        f << JsonField("app_id",     a.app_id);
        f << JsonField("app_secret", a.app_secret);
        f << JsonField("user_id",    a.user_id);
        f << JsonField("auth_token", a.auth_token, true);
        f << "  }" << (i + 1 < (int)m_cfg->accounts.size() ? "," : "") << "\n";
    }
    f << "]\n";
    MessageBoxW(m_hwnd, T(L"Accounts exported."), T(L"Export"), MB_OK | MB_ICONINFORMATION);
}

static std::string JsonGetVal(const std::string& obj, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto kpos = obj.find(needle);
    if (kpos == std::string::npos) return {};
    auto colon = obj.find(':', kpos + needle.size());
    if (colon == std::string::npos) return {};
    auto q1 = obj.find('"', colon + 1);
    if (q1 == std::string::npos) return {};
    std::string val;
    size_t i = q1 + 1;
    while (i < obj.size() && obj[i] != '"') {
        if (obj[i] == '\\' && i + 1 < obj.size()) { ++i; }
        val += obj[i++];
    }
    return val;
}

void SettingsPanel::ImportAccounts() {
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner   = m_hwnd;
    ofn.lpstrFilter = L"JSON files\0*.json\0All files\0*.*\0";
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;

    std::ifstream f(path);
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    int imported = 0;
    size_t pos = 0;
    while ((pos = text.find('{', pos)) != std::string::npos) {
        auto end = text.find('}', pos);
        if (end == std::string::npos) break;
        std::string obj = text.substr(pos, end - pos + 1);
        pos = end + 1;

        Account a;
        a.country    = JsonGetVal(obj, "country");
        a.email      = JsonGetVal(obj, "email");
        a.app_id     = JsonGetVal(obj, "app_id");
        a.app_secret = JsonGetVal(obj, "app_secret");
        a.user_id    = JsonGetVal(obj, "user_id");
        a.auth_token = JsonGetVal(obj, "auth_token");

        // Merge: match by email; append if not found
        bool found = false;
        for (auto& existing : m_cfg->accounts) {
            if (!a.email.empty() && existing.email == a.email) {
                existing = a; found = true; break;
            }
            if (!a.country.empty() && a.email.empty() && existing.country == a.country) {
                existing = a; found = true; break;
            }
        }
        if (!found) m_cfg->accounts.push_back(a);
        ++imported;
    }

    m_cfg->Save();
    RefreshAccountList();
    MessageBoxW(m_hwnd, (std::to_wstring(imported) + L" " + T(L"account(s) imported.")).c_str(),
                T(L"Import"), MB_OK | MB_ICONINFORMATION);
}
