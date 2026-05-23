#include <windows.h>
#include <commctrl.h>
#include <string>
#include <sstream>
#include <algorithm>
#include "SearchPanel.h"
#include "QueryParser.h"

// ── string utils ─────────────────────────────────────────────────────────────

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

static std::wstring GetText(HWND hwnd) {
    int len = GetWindowTextLengthW(hwnd);
    if (len <= 0) return {};
    std::wstring s(len + 1, 0);
    GetWindowTextW(hwnd, s.data(), len + 1);
    s.resize(len);
    return s;
}

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
    char buf[4096];
    DWORD read;
    while (ReadFile(hRead, buf, sizeof(buf), &read, nullptr) && read)
        out.append(buf, read);

    WaitForSingleObject(pi.hProcess, 30000);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(hRead);
    return out;
}

// ── WndProc ──────────────────────────────────────────────────────────────────

LRESULT CALLBACK SearchPanel::PanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    SearchPanel* self = nullptr;
    if (msg == WM_NCCREATE) {
        self = reinterpret_cast<SearchPanel*>(
            reinterpret_cast<CREATESTRUCT*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<SearchPanel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMsg(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT SearchPanel::HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wp) == ID_SEARCH_BTN) DoSearch();
        if (LOWORD(wp) == ID_DL_BTN)     OnDownload();
        if (LOWORD(wp) == ID_SEARCH_EDIT && HIWORD(wp) == EN_CHANGE) {
            if (!m_allResults.empty()) {
                auto ast = QueryParser::Parse(GetText(m_hSearch));
                ApplyFilter(ast);
                PopulateList();
            }
        }
        break;
    case WM_KEYDOWN:
        if (wp == VK_RETURN && GetFocus() == m_hSearch) { DoSearch(); return 0; }
        break;
    case WM_NOTIFY: {
        NMHDR* nm = reinterpret_cast<NMHDR*>(lp);
        if (nm->idFrom == ID_RESULT_LIST) {
            if (nm->code == LVN_ITEMCHANGED) {
                NMLISTVIEW* lv = reinterpret_cast<NMLISTVIEW*>(lp);
                if (lv->uNewState & LVIS_SELECTED) {
                    OnItemSelect(lv->iItem);
                    EnableWindow(m_hDlBtn, lv->iItem >= 0);
                }
            }
            if (nm->code == NM_RCLICK) {
                NMITEMACTIVATE* ia = reinterpret_cast<NMITEMACTIVATE*>(lp);
                POINT pt; GetCursorPos(&pt);
                OnContextMenu(ia->iItem, pt);
            }
        }
        break;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ── Create ────────────────────────────────────────────────────────────────────

void SearchPanel::Create(HWND parent, HINSTANCE hInst, Renderer* rend) {
    m_rend = rend;

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = PanelProc;
        wc.hInstance     = hInst;
        wc.lpszClassName = L"SearchPanel";
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassExW(&wc);
        registered = true;
    }

    CreateWindowExW(0, L"SearchPanel", L"", WS_CHILD,
        0, 0, 0, 0, parent, nullptr, hInst, this);

    HFONT hf = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

    m_hSearch = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        0, 0, 0, 0, m_hwnd, (HMENU)(INT_PTR)ID_SEARCH_EDIT, hInst, nullptr);
    SendMessageW(m_hSearch, EM_SETCUEBANNER, TRUE,
        (LPARAM)L"Search — e.g.  artist:\"Daft Punk\" AND year:2001-2010  |  hires:true  |  type:album");

    m_type.Create(m_hwnd, hInst, ID_TYPE_COMBO);
    for (auto* t : {L"Smart (auto)", L"Albums", L"Tracks", L"Artists", L"Playlists", L"All types"})
        m_type.AddItem(t);
    m_type.SetSel(0);

    m_hBtn = CreateWindowExW(0, L"BUTTON", L"Search",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, m_hwnd, (HMENU)(INT_PTR)ID_SEARCH_BTN, hInst, nullptr);

    m_hList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 0, 0, m_hwnd, (HMENU)(INT_PTR)ID_RESULT_LIST, hInst, nullptr);
    ListView_SetExtendedListViewStyle(m_hList,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_HEADERDRAGDROP);

    auto addCol = [&](const wchar_t* name, int w, int i) {
        LVCOLUMNW col = {};
        col.mask    = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = const_cast<wchar_t*>(name);
        col.cx      = w;
        ListView_InsertColumn(m_hList, i, &col);
    };
    addCol(L"Title",     240, 0);
    addCol(L"Artist",    160, 1);
    addCol(L"Album",     160, 2);
    addCol(L"Year",       50, 3);
    addCol(L"Duration",   70, 4);
    addCol(L"Genre",     110, 5);
    addCol(L"Hi-Res",     55, 6);
    addCol(L"Type",       60, 7);

    m_hCount = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        0, 0, 0, 0, m_hwnd, nullptr, hInst, nullptr);

    m_hDlBtn = CreateWindowExW(0, L"BUTTON", L"Download Selected",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, m_hwnd, (HMENU)(INT_PTR)ID_DL_BTN, hInst, nullptr);
    EnableWindow(m_hDlBtn, FALSE);  // disabled until something is selected

    for (HWND h : {m_hSearch, m_hBtn, m_hList, m_hCount, m_hDlBtn})
        SendMessage(h, WM_SETFONT, (WPARAM)hf, TRUE);
}

void SearchPanel::Resize(int x, int y, int w, int h) {
    SetWindowPos(m_hwnd, nullptr, x, y, w, h, SWP_NOZORDER);

    const int pad = 10;
    const int eh  = 26;

    // Row 1: search + button | type picker (tall, right column)
    const int pickerW = 130;
    const int itemH   = 24;
    const int pickerH = std::min(6 * itemH, h - pad*2 - 20 - 36);
    const int btnW    = 80;
    int searchW = w - pad*3 - pickerW - btnW - pad;
    int row1Y   = pad;
    // Search edit + Search button (left area)
    SetWindowPos(m_hSearch, nullptr, pad, row1Y, searchW, eh, SWP_NOZORDER);
    SetWindowPos(m_hBtn,    nullptr, pad + searchW + pad, row1Y, btnW, eh, SWP_NOZORDER);
    // WheelPicker — right column
    m_type.Resize(w - pad - pickerW, row1Y, pickerW, pickerH);

    // Count label below the taller of search row or picker
    int topH  = pad + pickerH + 4;
    SetWindowPos(m_hCount, nullptr, pad, topH, 280, 18, SWP_NOZORDER);

    // List
    int listY = topH + 20;
    int listH = h - listY - pad - 36;
    SetWindowPos(m_hList, nullptr, pad, listY, w - pad*2, listH, SWP_NOZORDER);

    // Download button — bottom right
    SetWindowPos(m_hDlBtn, nullptr, w - pad - 160, h - pad - 30, 160, 30, SWP_NOZORDER);
}

void SearchPanel::Show(bool visible) {
    ShowWindow(m_hwnd, visible ? SW_SHOW : SW_HIDE);
    m_type.Show(visible);
}

// ── Search logic ──────────────────────────────────────────────────────────────

void SearchPanel::DoSearch() {
    std::wstring query = GetText(m_hSearch);
    if (query.empty()) return;

    auto ast = QueryParser::Parse(query);

    int ti = m_type.GetSel();
    // 0=Smart, 1=Albums, 2=Tracks, 3=Artists, 4=Playlists, 5=All
    const wchar_t* apiTypes[] = { L"", L"albums", L"tracks", L"artists", L"playlists", L"all" };
    std::wstring apiType;
    if (ti == 0) {
        // Smart: read type: hint from query, fall back to all
        std::wstring hint = QueryParser::ExtractTypeHint(ast);
        if (!hint.empty()) {
            if (hint == L"album")    hint = L"albums";
            if (hint == L"track")   hint = L"tracks";
            if (hint == L"artist")  hint = L"artists";
            if (hint == L"playlist")hint = L"playlists";
            apiType = hint;
        } else {
            apiType = L"all";
        }
    } else {
        apiType = apiTypes[ti];
    }

    std::wstring baseTerm = QueryParser::ExtractBaseTerm(ast);
    if (baseTerm.empty()) baseTerm = query;

    std::wstring cmd = L"streamer.exe search --tsv -n 50 --type " + apiType
                     + L" \"" + baseTerm + L"\"";

    SetWindowTextW(m_hCount, L"Searching…");
    std::string raw = RunCapture(cmd);

    m_allResults.clear();
    ParseTsv(raw, apiType);

    ApplyFilter(ast);
    PopulateList();
}

void SearchPanel::ParseTsv(const std::string& raw, const std::wstring& defaultType) {
    std::istringstream ss(raw);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        std::vector<std::string> f;
        std::istringstream ls(line);
        std::string tok;
        while (std::getline(ls, tok, '\t')) f.push_back(tok);
        if (f.size() < 2) continue;

        SearchResult r;
        r.id     = A2W(f.size() > 0 ? f[0] : "");
        r.title  = A2W(f.size() > 1 ? f[1] : "");
        r.artist = A2W(f.size() > 2 ? f[2] : "");
        r.album  = A2W(f.size() > 3 ? f[3] : "");
        r.genre  = A2W(f.size() > 6 ? f[6] : "");
        r.label  = A2W(f.size() > 9 ? f[9] : "");
        r.type   = A2W(f.size() > 8 ? f[8] : W2A(defaultType));
        r.hires  = (f.size() > 7 && f[7] == "true");
        try { r.year     = f.size() > 4 && !f[4].empty() ? std::stoi(f[4]) : 0; } catch (...) {}
        try { r.duration = f.size() > 5 && !f[5].empty() ? std::stoi(f[5]) : 0; } catch (...) {}

        m_allResults.push_back(std::move(r));
    }
}

void SearchPanel::ApplyFilter(const QueryNode& ast) {
    m_filtered.clear();
    bool trivial = (ast.kind == NodeKind::Term && ast.term.empty());

    for (int i = 0; i < (int)m_allResults.size(); i++) {
        if (trivial || QueryParser::Match(ast, m_allResults[i]))
            m_filtered.push_back(i);
    }

    if (!trivial) {
        std::stable_sort(m_filtered.begin(), m_filtered.end(), [&](int a, int b) {
            return QueryParser::Score(ast, m_allResults[a]) >
                   QueryParser::Score(ast, m_allResults[b]);
        });
    }

    std::wstring countStr = std::to_wstring(m_filtered.size()) + L" / "
                          + std::to_wstring(m_allResults.size()) + L" results";
    SetWindowTextW(m_hCount, countStr.c_str());
}

void SearchPanel::PopulateList() {
    ListView_DeleteAllItems(m_hList);

    auto fmtDur = [](int s) -> std::wstring {
        if (s <= 0) return L"";
        return std::to_wstring(s / 60) + L":"
             + (s % 60 < 10 ? L"0" : L"") + std::to_wstring(s % 60);
    };

    for (int row = 0; row < (int)m_filtered.size(); row++) {
        auto& r = m_allResults[m_filtered[row]];

        LVITEMW item = {};
        item.mask    = LVIF_TEXT;
        item.iItem   = row;
        item.pszText = const_cast<wchar_t*>(r.title.c_str());
        ListView_InsertItem(m_hList, &item);

        ListView_SetItemText(m_hList, row, 1, const_cast<wchar_t*>(r.artist.c_str()));
        ListView_SetItemText(m_hList, row, 2, const_cast<wchar_t*>(r.album.c_str()));

        std::wstring yr = r.year > 0 ? std::to_wstring(r.year) : L"";
        ListView_SetItemText(m_hList, row, 3, const_cast<wchar_t*>(yr.c_str()));

        std::wstring dur = fmtDur(r.duration);
        ListView_SetItemText(m_hList, row, 4, const_cast<wchar_t*>(dur.c_str()));

        ListView_SetItemText(m_hList, row, 5, const_cast<wchar_t*>(r.genre.c_str()));

        const wchar_t* hi = r.hires ? L"Yes" : L"";
        ListView_SetItemText(m_hList, row, 6, const_cast<wchar_t*>(hi));
        ListView_SetItemText(m_hList, row, 7, const_cast<wchar_t*>(r.type.c_str()));
    }

    // Auto-size columns on first populate
    if (!m_filtered.empty()) {
        for (int c = 0; c < 8; c++)
            ListView_SetColumnWidth(m_hList, c, LVSCW_AUTOSIZE_USEHEADER);
    }

    EnableWindow(m_hDlBtn, FALSE);
}

void SearchPanel::OnItemSelect(int listIdx) {
    if (listIdx < 0 || listIdx >= (int)m_filtered.size()) return;
    auto& r = m_allResults[m_filtered[listIdx]];

    std::wstring info = r.title;
    if (!r.artist.empty()) info += L"  ·  " + r.artist;
    if (r.year > 0)        info += L"  (" + std::to_wstring(r.year) + L")";
    if (!r.genre.empty())  info += L"  ·  " + r.genre;
    if (r.hires)           info += L"  [Hi-Res]";

    // Show in count label temporarily; set back to count on next filter
    SetWindowTextW(m_hCount, info.c_str());
}

void SearchPanel::OnDownload() {
    int idx = ListView_GetNextItem(m_hList, -1, LVNI_SELECTED);
    if (idx < 0 || idx >= (int)m_filtered.size()) return;
    auto& r = m_allResults[m_filtered[idx]];
    if (m_onDownload) m_onDownload(r.id, r.title + L" — " + r.artist);
}

void SearchPanel::OnContextMenu(int listIdx, POINT pt) {
    if (listIdx < 0 || listIdx >= (int)m_filtered.size()) return;
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IDM_DL_ITEM, L"Download");
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);
    if (cmd == IDM_DL_ITEM) {
        auto& r = m_allResults[m_filtered[listIdx]];
        if (m_onDownload) m_onDownload(r.id, r.title + L" — " + r.artist);
    }
}
