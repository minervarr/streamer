#include "InspectDialog.h"
#include "Strings.h"
#include <sstream>

// ── helpers ───────────────────────────────────────────────────────────────────

static std::string RunCapture(std::wstring cmd) {
    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return {};
    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION pi = {};
    bool ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr,
                             TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(hWrite);
    if (!ok) { CloseHandle(hRead); return {}; }
    std::string out;
    char buf[4096]; DWORD rd;
    while (ReadFile(hRead, buf, sizeof(buf), &rd, nullptr) && rd)
        out.append(buf, rd);
    WaitForSingleObject(pi.hProcess, 30000);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(hRead);
    return out;
}

static std::wstring Utf8ToW(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

static std::vector<std::string> SplitTab(const std::string& line) {
    std::vector<std::string> f;
    std::istringstream ss(line);
    std::string tok;
    while (std::getline(ss, tok, '\t')) f.push_back(tok);
    return f;
}

std::wstring InspectDialog::FmtDur(int s) {
    if (s <= 0) return L"";
    int m = s / 60, sec = s % 60;
    wchar_t buf[16];
    swprintf_s(buf, L"%d:%02d", m, sec);
    return buf;
}

// ── TSV parsing ───────────────────────────────────────────────────────────────

InspectAlbum InspectDialog::ParseTsv(const std::string& raw) {
    InspectAlbum album;
    std::istringstream ss(raw);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        auto f = SplitTab(line);
        if (f.empty()) continue;
        if (f[0] == "album" && f.size() >= 8) {
            album.title       = Utf8ToW(f[1]);
            album.artist      = Utf8ToW(f[2]);
            album.year        = Utf8ToW(f[3]);
            album.label       = Utf8ToW(f[4]);
            album.releaseType = Utf8ToW(f[5]);
            album.hires       = (f[6] == "1");
            try { album.lockedCount = std::stoi(f[7]); } catch (...) {}
        } else if (f[0] == "track" && f.size() >= 9) {
            InspectTrack t;
            try { t.num      = std::stoi(f[1]); } catch (...) {}
            try { t.disc     = std::stoi(f[2]); } catch (...) {}
            t.title           = Utf8ToW(f[3]);
            try { t.duration  = std::stoi(f[4]); } catch (...) {}
            t.streamable      = (f[5] == "1");
            t.downloadable    = (f[6] == "1");
            t.hires           = (f[7] == "1");
            t.explicit_       = (f[8] == "1");
            album.tracks.push_back(std::move(t));
        }
    }
    return album;
}

// ── Dialog data ───────────────────────────────────────────────────────────────

struct DlgData {
    InspectAlbum album;
    HFONT        hFont = nullptr;
};

// ── Colors ────────────────────────────────────────────────────────────────────

static const COLORREF CLR_LOCKED = RGB(160, 160, 160);
static const COLORREF CLR_HIRES  = RGB(0,  100, 200);

// ── Layout helper (called from WM_SIZE) ───────────────────────────────────────

static void DoLayout(HWND hDlg, int w, int h) {
    const int pad = 10, hdrH = 40, btnH = 28, btnW = 80;
    HWND hHdr   = GetDlgItem(hDlg, InspectDialog::ID_HEADER);
    HWND hList  = GetDlgItem(hDlg, InspectDialog::ID_LIST);
    HWND hClose = GetDlgItem(hDlg, InspectDialog::ID_CLOSE);
    if (!hHdr || !hList || !hClose) return;
    int listY = pad + hdrH + 6;
    int listH = h - listY - pad - btnH - 6;
    if (listH < 20) listH = 20;
    SetWindowPos(hHdr,   nullptr, pad, pad,          w - pad*2, hdrH,  SWP_NOZORDER);
    SetWindowPos(hList,  nullptr, pad, listY,        w - pad*2, listH, SWP_NOZORDER);
    SetWindowPos(hClose, nullptr, w - pad - btnW, h - pad - btnH, btnW, btnH, SWP_NOZORDER);
}

// ── Dialog proc ──────────────────────────────────────────────────────────────

INT_PTR CALLBACK InspectDialog::DlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp) {
    DlgData* d = reinterpret_cast<DlgData*>(GetWindowLongPtrW(hDlg, DWLP_USER));

    switch (msg) {

    case WM_INITDIALOG: {
        d = reinterpret_cast<DlgData*>(lp);
        SetWindowLongPtrW(hDlg, DWLP_USER, lp);

        // Create font
        d->hFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

        HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hDlg, GWLP_HINSTANCE);
        auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id, DWORD exStyle = 0) -> HWND {
            HWND h = CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style,
                0, 0, 10, 10, hDlg, (HMENU)(INT_PTR)id, hInst, nullptr);
            if (h) SendMessageW(h, WM_SETFONT, (WPARAM)d->hFont, FALSE);
            return h;
        };

        // Header label
        mk(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, ID_HEADER);

        // Track list
        HWND hList = mk(WC_LISTVIEWW, L"",
            LVS_REPORT | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER, ID_LIST, WS_EX_CLIENTEDGE);
        ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

        auto addCol = [&](const wchar_t* name, int w, int i) {
            LVCOLUMNW col = {}; col.mask = LVCF_TEXT | LVCF_WIDTH;
            col.pszText = const_cast<wchar_t*>(name); col.cx = w;
            ListView_InsertColumn(hList, i, &col);
        };
        addCol(T(L"#"),        40, 0);
        addCol(T(L"Title"),   300, 1);
        addCol(T(L"Duration"), 64, 2);
        addCol(T(L"Hi-Res"),   54, 3);
        addCol(L"E",           28, 4);
        addCol(T(L"Status"),   88, 5);

        // Close button
        mk(L"BUTTON", T(L"Close"), BS_PUSHBUTTON | BS_DEFPUSHBUTTON, ID_CLOSE);

        // Populate header text
        auto& a = d->album;
        std::wstring hdr = a.artist + L"  ·  " + a.title;
        if (!a.year.empty())        hdr += L"  (" + a.year + L")";
        if (!a.label.empty())       hdr += L"   " + a.label;
        if (!a.releaseType.empty()) hdr += L"  [" + a.releaseType + L"]";
        if (a.hires)                hdr += L"  [Hi-Res]";
        if (!a.country.empty())     hdr += L"     Region: " + a.country;
        if (a.lockedCount > 0)
            hdr += L"     ⚠ " + std::to_wstring(a.lockedCount) + L" locked";
        else
            hdr += L"     ✓ all available";
        SetDlgItemTextW(hDlg, ID_HEADER, hdr.c_str());

        // Window title
        SetWindowTextW(hDlg, (a.artist + L" — " + a.title).c_str());

        // Populate rows
        bool multiDisc = false;
        for (auto& t : a.tracks) if (t.disc > 1) { multiDisc = true; break; }

        for (int row = 0; row < (int)a.tracks.size(); row++) {
            auto& t = a.tracks[row];
            std::wstring numStr = multiDisc
                ? std::to_wstring(t.disc) + L"-" + std::to_wstring(t.num)
                : std::to_wstring(t.num);

            LVITEMW item = {}; item.mask = LVIF_TEXT;
            item.iItem = row; item.pszText = numStr.data();
            ListView_InsertItem(hList, &item);
            ListView_SetItemText(hList, row, 1, const_cast<wchar_t*>(t.title.c_str()));
            std::wstring dur = FmtDur(t.duration);
            ListView_SetItemText(hList, row, 2, dur.data());
            ListView_SetItemText(hList, row, 3, t.hires     ? const_cast<wchar_t*>(T(L"Yes")) : const_cast<wchar_t*>(L""));
            ListView_SetItemText(hList, row, 4, t.explicit_ ? const_cast<wchar_t*>(L"E")      : const_cast<wchar_t*>(L""));
            const wchar_t* status = !t.streamable   ? T(L"Locked")
                                  : !t.downloadable ? T(L"Stream only")
                                  :                   L"";
            ListView_SetItemText(hList, row, 5, const_cast<wchar_t*>(status));
        }

        // Size and center the window, which triggers WM_SIZE for layout
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);
        int dw = sw * 55 / 100, dh = sh * 65 / 100;
        SetWindowPos(hDlg, HWND_TOP, (sw - dw) / 2, (sh - dh) / 2, dw, dh, 0);

        return TRUE;
    }

    case WM_SIZE:
        DoLayout(hDlg, LOWORD(lp), HIWORD(lp));
        return 0;

    case WM_NOTIFY: {
        auto* nm = reinterpret_cast<NMHDR*>(lp);
        if (nm->idFrom == ID_LIST && nm->code == NM_CUSTOMDRAW) {
            auto* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(lp);
            if (cd->nmcd.dwDrawStage == CDDS_PREPAINT)
                return CDRF_NOTIFYITEMDRAW;
            if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT && d) {
                int row = (int)cd->nmcd.dwItemSpec;
                if (row >= 0 && row < (int)d->album.tracks.size()) {
                    auto& t = d->album.tracks[row];
                    if (!t.streamable) {
                        cd->clrText   = CLR_LOCKED;
                        cd->clrTextBk = RGB(250, 248, 248);
                    } else if (t.hires) {
                        cd->clrText = CLR_HIRES;
                    }
                }
                return CDRF_NEWFONT;
            }
        }
        break;
    }

    case WM_COMMAND:
        if (LOWORD(wp) == ID_CLOSE || LOWORD(wp) == IDCANCEL || LOWORD(wp) == IDOK)
            EndDialog(hDlg, 0);
        break;

    case WM_DESTROY:
        if (d && d->hFont) { DeleteObject(d->hFont); d->hFont = nullptr; }
        break;

    case WM_CLOSE:
        EndDialog(hDlg, 0);
        return TRUE;
    }
    return FALSE;
}

// ── Public entry point ────────────────────────────────────────────────────────

// Minimal in-memory dialog template with no controls and no font spec.
// DS_CENTER is not used here since we position manually in WM_INITDIALOG.
#pragma pack(push, 2)
struct EmptyDlgTpl {
    DLGTEMPLATE hdr;
    WORD menu, cls, title;
};
#pragma pack(pop)

void InspectDialog::Show(HWND parent, HINSTANCE hInst,
                         const std::wstring& albumId, int accountIdx,
                         const std::wstring& country)
{
    std::wstring cmd = L"streamer.exe inspect --tsv --account "
                     + std::to_wstring(accountIdx) + L" " + albumId;
    std::string raw = RunCapture(cmd);

    DlgData d;
    d.album = ParseTsv(raw);
    d.album.country = country;

    EmptyDlgTpl tpl = {};
    tpl.hdr.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME;
    tpl.hdr.cx    = 400;
    tpl.hdr.cy    = 300;

    // DialogBoxIndirectParamW is truly modal: disables parent, runs its own
    // message loop, re-enables parent, and returns only when EndDialog is called.
    DialogBoxIndirectParamW(hInst,
        reinterpret_cast<LPCDLGTEMPLATEW>(&tpl),
        parent, DlgProc, reinterpret_cast<LPARAM>(&d));
}
