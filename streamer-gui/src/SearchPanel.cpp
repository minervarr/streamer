#include <windows.h>
#include <commctrl.h>
#include <string>
#include <sstream>
#include <algorithm>
#include "SearchPanel.h"
#include "QueryParser.h"
#include "Strings.h"

// ── string utils ─────────────────────────────────────────────────────────────

std::string SearchPanel::W2A(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring SearchPanel::A2W(const std::string& s) {
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
        if (LOWORD(wp) == ID_CART_CLEAR) {
            m_cart.clear();
            m_checkedIds.clear();
            m_suppressCheckNotify = true;
            for (int row = 0; row < (int)m_filtered.size(); row++)
                ListView_SetCheckState(m_hList, row, FALSE);
            m_suppressCheckNotify = false;
            RefreshCart();
            UpdateDownloadButton();
        }
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
                }
                // Detect checkbox state change
                if ((lv->uChanged & LVIF_STATE) &&
                    ((lv->uOldState ^ lv->uNewState) & LVIS_STATEIMAGEMASK)) {
                    OnCheckChanged(lv->iItem);
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
        (LPARAM)T(L"search_placeholder"));

    m_type.Create(m_hwnd, hInst, ID_TYPE_COMBO);
    for (auto* s : {T(L"Smart (auto)"), T(L"Albums"), T(L"Tracks"), T(L"Artists"), T(L"Playlists"), T(L"All types")})
        m_type.AddItem(s);
    m_type.SetSel(0);

    m_hBtn = CreateWindowExW(0, L"BUTTON", T(L"Search"),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, m_hwnd, (HMENU)(INT_PTR)ID_SEARCH_BTN, hInst, nullptr);

    m_hList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS,
        0, 0, 0, 0, m_hwnd, (HMENU)(INT_PTR)ID_RESULT_LIST, hInst, nullptr);
    ListView_SetExtendedListViewStyle(m_hList,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_HEADERDRAGDROP | LVS_EX_CHECKBOXES);

    auto addCol = [&](const wchar_t* name, int w, int i) {
        LVCOLUMNW col = {};
        col.mask    = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = const_cast<wchar_t*>(name);
        col.cx      = w;
        ListView_InsertColumn(m_hList, i, &col);
    };
    addCol(T(L"Title"),     220, 0);
    addCol(T(L"Artist"),    140, 1);
    addCol(T(L"Label"),     120, 2);
    addCol(T(L"Date"),       80, 3);
    addCol(T(L"Duration"),   62, 4);
    addCol(T(L"Genre"),      90, 5);
    addCol(T(L"Hi-Res"),     50, 6);
    addCol(L"E",             30, 7);
    addCol(T(L"Type"),       55, 8);
    addCol(T(L"Country"),    58, 9);

    m_hCount = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        0, 0, 0, 0, m_hwnd, nullptr, hInst, nullptr);

    m_hDlBtn = CreateWindowExW(0, L"BUTTON", T(L"Download Selected"),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, m_hwnd, (HMENU)(INT_PTR)ID_DL_BTN, hInst, nullptr);
    EnableWindow(m_hDlBtn, FALSE);

    m_hCartLabel = CreateWindowExW(0, L"STATIC", (std::wstring(T(L"Selected")) + L" (0)").c_str(),
        WS_CHILD | SS_LEFT,
        0, 0, 0, 0, m_hwnd, nullptr, hInst, nullptr);

    m_hCartClear = CreateWindowExW(0, L"BUTTON", T(L"Clear All"),
        WS_CHILD | BS_PUSHBUTTON,
        0, 0, 0, 0, m_hwnd, (HMENU)(INT_PTR)ID_CART_CLEAR, hInst, nullptr);

    m_hCartList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | LVS_REPORT | LVS_SINGLESEL | LVS_NOCOLUMNHEADER,
        0, 0, 0, 0, m_hwnd, (HMENU)(INT_PTR)ID_CART_LIST, hInst, nullptr);
    ListView_SetExtendedListViewStyle(m_hCartList, LVS_EX_FULLROWSELECT);
    {
        LVCOLUMNW col = {};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = const_cast<wchar_t*>(T(L"Title"));
        col.cx = 220;
        ListView_InsertColumn(m_hCartList, 0, &col);
        col.pszText = const_cast<wchar_t*>(T(L"Artist"));
        col.cx = 140;
        ListView_InsertColumn(m_hCartList, 1, &col);
    }

    for (HWND h : {m_hSearch, m_hBtn, m_hList, m_hCount, m_hDlBtn, m_hCartLabel, m_hCartClear, m_hCartList})
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
    SetWindowPos(m_hSearch, nullptr, pad, row1Y, searchW, eh, SWP_NOZORDER);
    SetWindowPos(m_hBtn,    nullptr, pad + searchW + pad, row1Y, btnW, eh, SWP_NOZORDER);
    m_type.Resize(w - pad - pickerW, row1Y, pickerW, pickerH);

    int topH  = pad + pickerH + 4;
    SetWindowPos(m_hCount, nullptr, pad, topH, 280, 18, SWP_NOZORDER);

    // Cart section height (visible only when cart non-empty)
    const int cartH = m_cart.empty() ? 0 : 90;
    const int cartLabelH = 18;
    const int dlBtnH = 30;

    // Results list
    int listY = topH + 20;
    int listH = h - listY - pad - dlBtnH - (cartH > 0 ? cartH + pad : 0);
    if (listH < 40) listH = 40;
    SetWindowPos(m_hList, nullptr, pad, listY, w - pad*2, listH, SWP_NOZORDER);

    if (cartH > 0) {
        int cartY = listY + listH + pad;
        ShowWindow(m_hCartLabel, SW_SHOW);
        ShowWindow(m_hCartClear, SW_SHOW);
        ShowWindow(m_hCartList,  SW_SHOW);
        SetWindowPos(m_hCartLabel, nullptr, pad, cartY, 200, cartLabelH, SWP_NOZORDER);
        SetWindowPos(m_hCartClear, nullptr, w - pad - 80, cartY, 80, eh, SWP_NOZORDER);
        SetWindowPos(m_hCartList,  nullptr, pad, cartY + cartLabelH + 2, w - pad*2, cartH - cartLabelH - 4, SWP_NOZORDER);
    } else {
        ShowWindow(m_hCartLabel, SW_HIDE);
        ShowWindow(m_hCartClear, SW_HIDE);
        ShowWindow(m_hCartList,  SW_HIDE);
    }

    // Download button — bottom right
    SetWindowPos(m_hDlBtn, nullptr, w - pad - 160, h - pad - dlBtnH, 160, dlBtnH, SWP_NOZORDER);
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
    const wchar_t* apiTypes[] = { L"", L"albums", L"tracks", L"artists", L"playlists", L"all" };
    std::wstring apiType;
    if (ti == 0) {
        std::wstring hint = QueryParser::ExtractTypeHint(ast);
        if (!hint.empty()) {
            if (hint == L"album")     hint = L"albums";
            if (hint == L"track")     hint = L"tracks";
            if (hint == L"artist")    hint = L"artists";
            if (hint == L"playlist")  hint = L"playlists";
            apiType = hint;
        } else {
            apiType = L"all";
        }
    } else {
        apiType = apiTypes[ti];
    }

    std::wstring baseTerm = QueryParser::ExtractBaseTerm(ast);
    if (baseTerm.empty()) baseTerm = query;

    std::vector<SearchResult> kept;
    for (auto& r : m_allResults) {
        if (m_checkedIds.count(r.id))
            kept.push_back(r);
    }

    m_allResults.clear();
    SetWindowTextW(m_hCount, T(L"Searching..."));

    // Search across all configured accounts
    int numAccounts = m_cfg ? (int)m_cfg->accounts.size() : 0;
    if (numAccounts == 0) {
        // No accounts — try default (account 0, which CLI will reject gracefully)
        std::wstring cmd = L"streamer.exe search --tsv -n 50 --type " + apiType
                         + L" \"" + baseTerm + L"\"";
        ParseTsv(RunCapture(cmd), apiType, L"");
    } else {
        for (int i = 0; i < numAccounts; i++) {
            std::wstring country = A2W(m_cfg->accounts[i].country);
            std::wstring cmd = L"streamer.exe search --tsv --account " + std::to_wstring(i)
                             + L" -n 50 --type " + apiType
                             + L" \"" + baseTerm + L"\"";
            ParseTsv(RunCapture(cmd), apiType, country);
        }
    }

    std::set<std::wstring> newIds;
    for (auto& r : m_allResults) newIds.insert(r.id);
    for (auto& r : kept) {
        if (!newIds.count(r.id))
            m_allResults.push_back(std::move(r));
    }

    ApplyFilter(ast);
    PopulateList();
}

void SearchPanel::ParseTsv(const std::string& raw, const std::wstring& defaultType, const std::wstring& country) {
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
        r.id       = A2W(f.size() > 0 ? f[0] : "");
        r.title    = A2W(f.size() > 1 ? f[1] : "");
        r.artist   = A2W(f.size() > 2 ? f[2] : "");
        r.album    = A2W(f.size() > 3 ? f[3] : "");
        r.date     = A2W(f.size() > 4 ? f[4] : "");
        r.genre    = A2W(f.size() > 6 ? f[6] : "");
        r.label    = A2W(f.size() > 9 ? f[9] : "");
        r.type     = A2W(f.size() > 8 ? f[8] : W2A(defaultType));
        r.hires    = (f.size() > 7 && f[7] == "true");
        r.explicit_= (f.size() > 10 && f[10] == "true");
        r.country  = country;
        try { r.year     = !r.date.empty() ? std::stoi(W2A(r.date)) : 0; } catch (...) {}
        try { r.duration = f.size() > 5 && !f[5].empty() ? std::stoi(f[5]) : 0; } catch (...) {}

        m_allResults.push_back(std::move(r));
    }
}

void SearchPanel::ApplyFilter(const QueryNode& ast) {
    m_filtered.clear();
    bool trivial = (ast.kind == NodeKind::Term && ast.term.empty());

    for (int i = 0; i < (int)m_allResults.size(); i++) {
        if (trivial || m_checkedIds.count(m_allResults[i].id) || QueryParser::Match(ast, m_allResults[i]))
            m_filtered.push_back(i);
    }

    if (!trivial) {
        std::stable_sort(m_filtered.begin(), m_filtered.end(), [&](int a, int b) {
            return QueryParser::Score(ast, m_allResults[a]) >
                   QueryParser::Score(ast, m_allResults[b]);
        });
    }

    // Group by artist, then newest first within each artist
    std::stable_sort(m_filtered.begin(), m_filtered.end(), [&](int a, int b) {
        auto& ra = m_allResults[a]; auto& rb = m_allResults[b];
        int cmp = _wcsicmp(ra.artist.c_str(), rb.artist.c_str());
        if (cmp != 0) return cmp < 0;
        return ra.date > rb.date;
    });

    std::wstring countStr = std::to_wstring(m_filtered.size()) + L" / "
                          + std::to_wstring(m_allResults.size()) + L" " + T(L"results");
    SetWindowTextW(m_hCount, countStr.c_str());
}

void SearchPanel::PopulateList() {
    m_suppressCheckNotify = true;
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
        ListView_SetItemText(m_hList, row, 3, const_cast<wchar_t*>(r.date.c_str()));

        std::wstring dur = fmtDur(r.duration);
        ListView_SetItemText(m_hList, row, 4, const_cast<wchar_t*>(dur.c_str()));

        ListView_SetItemText(m_hList, row, 5, const_cast<wchar_t*>(r.genre.c_str()));

        const wchar_t* hi = r.hires ? T(L"Yes") : L"";
        ListView_SetItemText(m_hList, row, 6, const_cast<wchar_t*>(hi));
        ListView_SetItemText(m_hList, row, 7, const_cast<wchar_t*>(r.explicit_ ? L"E" : L""));
        ListView_SetItemText(m_hList, row, 8, const_cast<wchar_t*>(r.type.c_str()));
        ListView_SetItemText(m_hList, row, 9, const_cast<wchar_t*>(r.country.c_str()));
    }

    // Re-apply checkmarks for items that were previously checked
    for (int row = 0; row < (int)m_filtered.size(); row++) {
        auto& r = m_allResults[m_filtered[row]];
        if (m_checkedIds.count(r.id))
            ListView_SetCheckState(m_hList, row, TRUE);
    }

    // Auto-size columns on first populate
    if (!m_filtered.empty()) {
        for (int c = 0; c < 10; c++)
            ListView_SetColumnWidth(m_hList, c, LVSCW_AUTOSIZE_USEHEADER);
    }

    m_suppressCheckNotify = false;
    UpdateDownloadButton();
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

void SearchPanel::OnCheckChanged(int listIdx) {
    if (m_suppressCheckNotify) return;
    if (listIdx < 0 || listIdx >= (int)m_filtered.size()) return;
    auto& r = m_allResults[m_filtered[listIdx]];
    BOOL checked = ListView_GetCheckState(m_hList, listIdx);
    if (checked) {
        m_checkedIds.insert(r.id);
        // Add to cart if not already there
        bool found = false;
        for (auto& c : m_cart) { if (c.id == r.id) { found = true; break; } }
        if (!found) {
            CartItem ci;
            ci.id      = r.id;
            ci.title   = r.title;
            ci.artist  = r.artist;
            ci.type    = r.type;
            ci.country = r.country;
            ci.accountIdx = 0;
            if (m_cfg) {
                for (int i = 0; i < (int)m_cfg->accounts.size(); i++) {
                    if (A2W(m_cfg->accounts[i].country) == r.country) { ci.accountIdx = i; break; }
                }
            }
            m_cart.push_back(std::move(ci));
        }
    } else {
        m_checkedIds.erase(r.id);
        m_cart.erase(std::remove_if(m_cart.begin(), m_cart.end(),
            [&](const CartItem& c) { return c.id == r.id; }), m_cart.end());
    }
    RefreshCart();
    UpdateDownloadButton();
}

void SearchPanel::UpdateDownloadButton() {
    if (m_checkedIds.empty()) {
        SetWindowTextW(m_hDlBtn, T(L"Download Selected"));
        EnableWindow(m_hDlBtn, FALSE);
    } else {
        std::wstring label = std::wstring(T(L"Download")) + L" (" + std::to_wstring(m_checkedIds.size()) + L")";
        SetWindowTextW(m_hDlBtn, label.c_str());
        EnableWindow(m_hDlBtn, TRUE);
    }
}

void SearchPanel::OnDownload() {
    if (!m_cart.empty()) {
        for (auto& ci : m_cart) {
            if (m_onDownload)
                m_onDownload(ci.id + L"|" + std::to_wstring(ci.accountIdx),
                             ci.title + L" — " + ci.artist);
        }
    } else {
        // Fallback: use the highlighted row (single-click / context-menu download)
        int idx = ListView_GetNextItem(m_hList, -1, LVNI_SELECTED);
        if (idx < 0 || idx >= (int)m_filtered.size()) return;
        auto& r = m_allResults[m_filtered[idx]];
        int acctIdx = 0;
        if (m_cfg) {
            for (int i = 0; i < (int)m_cfg->accounts.size(); i++) {
                if (A2W(m_cfg->accounts[i].country) == r.country) { acctIdx = i; break; }
            }
        }
        if (m_onDownload)
            m_onDownload(r.id + L"|" + std::to_wstring(acctIdx),
                         r.title + L" — " + r.artist);
    }

    // Clear cart and checked state after queueing
    m_cart.clear();
    m_checkedIds.clear();
    m_suppressCheckNotify = true;
    for (int row = 0; row < (int)m_filtered.size(); row++)
        ListView_SetCheckState(m_hList, row, FALSE);
    m_suppressCheckNotify = false;
    RefreshCart();
    UpdateDownloadButton();
}

void SearchPanel::OnContextMenu(int listIdx, POINT pt) {
    if (listIdx < 0 || listIdx >= (int)m_filtered.size()) return;
    auto& r = m_allResults[m_filtered[listIdx]];

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IDM_DL_ITEM, T(L"Download"));
    // Inspect is only meaningful for albums
    bool isAlbum = (r.type == L"album" || r.type == L"albums" || r.type.empty());
    if (isAlbum)
        AppendMenuW(menu, MF_STRING, IDM_INSPECT, T(L"Inspect tracks"));
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);

    if (cmd == IDM_DL_ITEM) {
        ListView_SetItemState(m_hList, listIdx, LVIS_SELECTED, LVIS_SELECTED);
        OnDownload();
    } else if (cmd == IDM_INSPECT) {
        int acctIdx = 0;
        if (m_cfg) {
            for (int i = 0; i < (int)m_cfg->accounts.size(); i++) {
                if (A2W(m_cfg->accounts[i].country) == r.country) { acctIdx = i; break; }
            }
        }
        std::wstring country = m_cfg && acctIdx < (int)m_cfg->accounts.size()
            ? A2W(m_cfg->accounts[acctIdx].country) : L"";
        HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(m_hwnd, GWLP_HINSTANCE);
        InspectDialog::Show(m_hwnd, hInst, r.id, acctIdx, country);
    }
}

void SearchPanel::RefreshCart() {
    ListView_DeleteAllItems(m_hCartList);
    for (int i = 0; i < (int)m_cart.size(); i++) {
        LVITEMW item = {};
        item.mask    = LVIF_TEXT;
        item.iItem   = i;
        item.pszText = const_cast<wchar_t*>(m_cart[i].title.c_str());
        ListView_InsertItem(m_hCartList, &item);
        ListView_SetItemText(m_hCartList, i, 1, const_cast<wchar_t*>(m_cart[i].artist.c_str()));
    }

    std::wstring label = std::wstring(T(L"Selected")) + L" (" + std::to_wstring(m_cart.size()) + L")";
    SetWindowTextW(m_hCartLabel, label.c_str());

    // Re-layout by querying current panel position
    RECT rc;
    GetWindowRect(m_hwnd, &rc);
    HWND parent = GetParent(m_hwnd);
    POINT pt = { rc.left, rc.top };
    ScreenToClient(parent, &pt);
    Resize(pt.x, pt.y, rc.right - rc.left, rc.bottom - rc.top);
}
