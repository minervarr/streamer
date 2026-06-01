#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <string>
#include <sstream>
#include <thread>
#include "DownloadsPanel.h"
#include "Strings.h"

// ── string helpers ───────────────────────────────────────────────────────────

static std::wstring A2W(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
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

// ── Worker thread ─────────────────────────────────────────────────────────────

struct WorkerArgs {
    HWND   notify;
    int    taskIdx;
    std::wstring url;
    std::shared_ptr<std::atomic<bool>> cancel;
};

static DWORD WINAPI DownloadWorker(LPVOID param) {
    auto* args = reinterpret_cast<WorkerArgs*>(param);

    std::wstring id  = args->url;
    std::wstring acctFlag;
    auto pipe = args->url.rfind(L'|');
    if (pipe != std::wstring::npos) {
        id       = args->url.substr(0, pipe);
        acctFlag = L" --account " + args->url.substr(pipe + 1);
    }

    std::wstring cmd = L"streamer.exe download" + acctFlag + L" \"" + id + L"\"";

    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE hRead, hWrite;
    CreatePipe(&hRead, &hWrite, &sa, 0);

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError  = hWrite;

    PROCESS_INFORMATION pi = {};
    bool ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr,
        TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(hWrite);

    if (!ok) {
        PostMessage(args->notify, WM_TASK_DONE,
            (WPARAM)args->taskIdx, (LPARAM) new std::wstring(T(L"Failed to start process")));
        delete args; CloseHandle(hRead); return 1;
    }

    char buf[256];
    std::string partial;
    DWORD read;
    int lastPct = 0;

    while (!args->cancel->load()) {
        if (!ReadFile(hRead, buf, sizeof(buf) - 1, &read, nullptr) || read == 0) break;
        buf[read] = 0;
        partial += buf;

        size_t pos;
        while ((pos = partial.find('\n')) != std::string::npos) {
            std::string line = partial.substr(0, pos);
            partial = partial.substr(pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) {
                int n = MultiByteToWideChar(CP_UTF8, 0, line.c_str(), -1, nullptr, 0);
                auto* ws = new std::wstring(n > 0 ? n - 1 : 0, 0);
                if (n > 0) MultiByteToWideChar(CP_UTF8, 0, line.c_str(), -1, ws->data(), n);
                PostMessage(args->notify, WM_TASK_LOG, (WPARAM)args->taskIdx, (LPARAM)ws);
            }

            auto pct_pos = line.find('%');
            if (pct_pos != std::string::npos && pct_pos >= 1) {
                int start = (int)pct_pos - 1;
                while (start > 0 && isdigit(line[start - 1])) start--;
                int pct = std::stoi(line.substr(start, pct_pos - start));
                if (pct > lastPct && pct <= 100) {
                    lastPct = pct;
                    PostMessage(args->notify, WM_TASK_PROGRESS,
                        (WPARAM)args->taskIdx, (LPARAM)pct);
                }
            }
        }
    }

    if (args->cancel->load()) {
        TerminateProcess(pi.hProcess, 0);
    }

    DWORD exitCode = 1;
    WaitForSingleObject(pi.hProcess, 5000);
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(hRead);

    bool success = (exitCode == 0) && !args->cancel->load();
    PostMessage(args->notify, WM_TASK_DONE,
        (WPARAM)args->taskIdx,
        (LPARAM)(success ? nullptr : new std::wstring(args->cancel->load() ? T(L"Cancelled") : T(L"Error"))));
    delete args;
    return 0;
}

// ── WndProc ──────────────────────────────────────────────────────────────────

LRESULT CALLBACK DownloadsPanel::PanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    DownloadsPanel* self = nullptr;
    if (msg == WM_NCCREATE) {
        self = reinterpret_cast<DownloadsPanel*>(
            reinterpret_cast<CREATESTRUCT*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<DownloadsPanel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMsg(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT DownloadsPanel::HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wp) == ID_CANCEL_BTN) CancelSelected();
        if (LOWORD(wp) == ID_EXPORT_BTN) OnExport();
        if (LOWORD(wp) == ID_IMPORT_BTN) OnImport();
        break;
    case WM_TASK_PROGRESS:
        OnProgress((int)wp, (int)lp);
        return 0;
    case WM_TASK_DONE: {
        auto* errMsg = reinterpret_cast<std::wstring*>(lp);
        OnDone((int)wp, errMsg == nullptr, errMsg ? *errMsg : L"");
        delete errMsg;
        return 0;
    }
    case WM_TASK_LOG: {
        auto* line = reinterpret_cast<std::wstring*>(lp);
        if (line) { AppendLog(*line); delete line; }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ── Create ────────────────────────────────────────────────────────────────────

void DownloadsPanel::Create(HWND parent, HINSTANCE hInst, Renderer* rend) {
    m_rend = rend;

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = PanelProc;
        wc.hInstance     = hInst;
        wc.lpszClassName = L"DownloadsPanel";
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassExW(&wc);
        registered = true;
    }

    CreateWindowExW(0, L"DownloadsPanel", L"", WS_CHILD,
        0, 0, 0, 0, parent, nullptr, hInst, this);

    HFONT hf = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

    // Active downloads list
    m_hList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 0, 0, m_hwnd, (HMENU)(INT_PTR)ID_DL_LIST, hInst, nullptr);
    ListView_SetExtendedListViewStyle(m_hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    auto addCol = [&](HWND list, const wchar_t* name, int w, int i) {
        LVCOLUMNW col = {};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = const_cast<wchar_t*>(name);
        col.cx = w;
        ListView_InsertColumn(list, i, &col);
    };
    addCol(m_hList, T(L"Title"),    360, 0);
    addCol(m_hList, T(L"Status"),   120, 1);
    addCol(m_hList, T(L"Progress"),  80, 2);

    m_hCancelBtn = CreateWindowExW(0, L"BUTTON", T(L"Cancel Selected"),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, m_hwnd, (HMENU)(INT_PTR)ID_CANCEL_BTN, hInst, nullptr);

    // History section
    m_hHistLabel = CreateWindowExW(0, L"STATIC", T(L"Download History"),
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 0, 0, m_hwnd, nullptr, hInst, nullptr);

    m_hExportBtn = CreateWindowExW(0, L"BUTTON", T(L"Export"),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, m_hwnd, (HMENU)(INT_PTR)ID_EXPORT_BTN, hInst, nullptr);

    m_hImportBtn = CreateWindowExW(0, L"BUTTON", T(L"Import"),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, m_hwnd, (HMENU)(INT_PTR)ID_IMPORT_BTN, hInst, nullptr);

    m_hHistList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 0, 0, m_hwnd, (HMENU)(INT_PTR)ID_HIST_LIST, hInst, nullptr);
    ListView_SetExtendedListViewStyle(m_hHistList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    addCol(m_hHistList, T(L"Title"),   200, 0);
    addCol(m_hHistList, T(L"Artist"),  140, 1);
    addCol(m_hHistList, T(L"Date"),    130, 2);
    addCol(m_hHistList, T(L"Quality"),  70, 3);
    addCol(m_hHistList, T(L"Country"),  60, 4);
    addCol(m_hHistList, T(L"Status"),   50, 5);

    // Log
    m_hLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL |
        ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        0, 0, 0, 0, m_hwnd, (HMENU)(INT_PTR)ID_LOG, hInst, nullptr);
    SendMessageW(m_hLog, EM_SETLIMITTEXT, 0, 0);

    HFONT hfMono = CreateFontW(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");

    for (HWND h : {m_hList, m_hCancelBtn, m_hHistLabel, m_hExportBtn, m_hImportBtn, m_hHistList})
        SendMessage(h, WM_SETFONT, (WPARAM)hf, TRUE);
    SendMessage(m_hLog, WM_SETFONT, (WPARAM)hfMono, TRUE);
}

void DownloadsPanel::Resize(int x, int y, int w, int h) {
    SetWindowPos(m_hwnd, nullptr, x, y, w, h, SWP_NOZORDER);
    const int pad  = 12;
    const int btnH = 28;
    const int logH = 120;

    // Split: ~35% active tasks, ~35% history, ~30% log
    int usable = h - pad * 5 - btnH - btnH - logH;
    int taskH  = usable * 40 / 100;
    int histH  = usable - taskH;
    if (taskH < 60) taskH = 60;
    if (histH < 60) histH = 60;

    // Active downloads
    SetWindowPos(m_hList, nullptr, pad, pad, w - pad*2, taskH, SWP_NOZORDER);
    int cancelY = pad + taskH + pad;
    SetWindowPos(m_hCancelBtn, nullptr, w - pad - 140, cancelY, 140, btnH, SWP_NOZORDER);

    // History header row
    int histLabelY = cancelY + btnH + pad;
    SetWindowPos(m_hHistLabel, nullptr, pad, histLabelY, 140, 18, SWP_NOZORDER);
    SetWindowPos(m_hExportBtn, nullptr, w - pad - 80 - pad - 80, histLabelY - 2, 80, btnH, SWP_NOZORDER);
    SetWindowPos(m_hImportBtn, nullptr, w - pad - 80, histLabelY - 2, 80, btnH, SWP_NOZORDER);

    // History list
    int histListY = histLabelY + btnH + 2;
    SetWindowPos(m_hHistList, nullptr, pad, histListY, w - pad*2, histH, SWP_NOZORDER);

    // Log
    int logY = histListY + histH + pad;
    int actualLogH = h - logY - pad;
    if (actualLogH < 40) actualLogH = 40;
    SetWindowPos(m_hLog, nullptr, pad, logY, w - pad*2, actualLogH, SWP_NOZORDER);
}

void DownloadsPanel::Show(bool visible) {
    ShowWindow(m_hwnd, visible ? SW_SHOW : SW_HIDE);
}

// ── History ──────────────────────────────────────────────────────────────────

void DownloadsPanel::LoadHistory() {
    m_history.clear();
    std::string raw = RunCapture(L"streamer.exe history --tsv -n 200");
    std::istringstream ss(raw);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        std::vector<std::string> f;
        std::istringstream ls(line);
        std::string tok;
        while (std::getline(ls, tok, '\t')) f.push_back(tok);
        if (f.size() < 8) continue;

        HistoryRecord rec;
        // Format: timestamp, qobuz_id, item_type, title, artist, quality, country, status
        long long ts = 0;
        try { ts = std::stoll(f[0]); } catch (...) {}
        // Format timestamp as YYYY-MM-DD HH:MM
        if (ts > 0) {
            time_t t = (time_t)ts;
            struct tm tm_buf;
            gmtime_s(&tm_buf, &t);
            wchar_t buf[32];
            wcsftime(buf, 32, L"%Y-%m-%d %H:%M", &tm_buf);
            rec.date = buf;
        }
        rec.title   = A2W(f[3]);
        rec.artist  = A2W(f[4]);
        rec.quality = A2W(f[5]);
        rec.country = A2W(f[6]);
        rec.status  = A2W(f[7]);
        m_history.push_back(std::move(rec));
    }

    // Populate history ListView
    ListView_DeleteAllItems(m_hHistList);
    for (int i = 0; i < (int)m_history.size(); i++) {
        auto& r = m_history[i];
        LVITEMW item = {};
        item.mask    = LVIF_TEXT;
        item.iItem   = i;
        item.pszText = const_cast<wchar_t*>(r.title.c_str());
        ListView_InsertItem(m_hHistList, &item);
        ListView_SetItemText(m_hHistList, i, 1, const_cast<wchar_t*>(r.artist.c_str()));
        ListView_SetItemText(m_hHistList, i, 2, const_cast<wchar_t*>(r.date.c_str()));
        ListView_SetItemText(m_hHistList, i, 3, const_cast<wchar_t*>(r.quality.c_str()));
        ListView_SetItemText(m_hHistList, i, 4, const_cast<wchar_t*>(r.country.c_str()));
        ListView_SetItemText(m_hHistList, i, 5, const_cast<wchar_t*>(r.status.c_str()));
    }
}

void DownloadsPanel::OnExport() {
    wchar_t path[MAX_PATH] = L"history.json";
    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner   = m_hwnd;
    ofn.lpstrFilter  = L"JSON Files (*.json)\0*.json\0All Files\0*.*\0";
    ofn.lpstrFile    = path;
    ofn.nMaxFile     = MAX_PATH;
    ofn.lpstrDefExt  = L"json";
    ofn.Flags        = OFN_OVERWRITEPROMPT;

    if (!GetSaveFileNameW(&ofn)) return;

    std::wstring cmd = L"streamer.exe history export \"" + std::wstring(path) + L"\"";
    std::string out = RunCapture(cmd);
    MessageBoxW(m_hwnd, A2W(out).c_str(), T(L"Export"), MB_OK | MB_ICONINFORMATION);
}

void DownloadsPanel::OnImport() {
    wchar_t path[MAX_PATH] = L"";
    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner   = m_hwnd;
    ofn.lpstrFilter  = L"JSON Files (*.json)\0*.json\0All Files\0*.*\0";
    ofn.lpstrFile    = path;
    ofn.nMaxFile     = MAX_PATH;
    ofn.Flags        = OFN_FILEMUSTEXIST;

    if (!GetOpenFileNameW(&ofn)) return;

    std::wstring cmd = L"streamer.exe history import \"" + std::wstring(path) + L"\"";
    std::string out = RunCapture(cmd);
    MessageBoxW(m_hwnd, A2W(out).c_str(), T(L"Import"), MB_OK | MB_ICONINFORMATION);
    LoadHistory();
}

// ── Logic ─────────────────────────────────────────────────────────────────────

void DownloadsPanel::AddDownload(const std::wstring& url, const std::wstring& title) {
    DownloadTask task;
    task.url    = url;
    task.title  = title;
    task.cancel = std::make_shared<std::atomic<bool>>(false);

    int idx = (int)m_tasks.size();
    m_tasks.push_back(task);

    LVITEMW item = {};
    item.mask    = LVIF_TEXT;
    item.iItem   = idx;
    item.pszText = const_cast<wchar_t*>(title.c_str());
    ListView_InsertItem(m_hList, &item);
    ListView_SetItemText(m_hList, idx, 1, const_cast<wchar_t*>(T(L"Queued")));
    ListView_SetItemText(m_hList, idx, 2, (LPWSTR)L"0%");

    m_tasks[idx].listRow = idx;
    StartTask(idx);
}

void DownloadsPanel::StartTask(int idx) {
    m_tasks[idx].status = DownloadTask::Status::Running;
    RefreshRow(idx);

    auto* args = new WorkerArgs{
        m_hwnd, idx, m_tasks[idx].url, m_tasks[idx].cancel
    };
    HANDLE h = CreateThread(nullptr, 0, DownloadWorker, args, 0, nullptr);
    if (h) CloseHandle(h);
}

void DownloadsPanel::RefreshRow(int idx) {
    auto& t = m_tasks[idx];
    const wchar_t* statusStr = T(L"Queued");
    switch (t.status) {
    case DownloadTask::Status::Running:   statusStr = T(L"Downloading"); break;
    case DownloadTask::Status::Done:      statusStr = T(L"Done");        break;
    case DownloadTask::Status::Error:     statusStr = T(L"Error");       break;
    case DownloadTask::Status::Cancelled: statusStr = T(L"Cancelled");   break;
    default: break;
    }
    ListView_SetItemText(m_hList, idx, 1, const_cast<wchar_t*>(statusStr));
    std::wstring pct = std::to_wstring(t.progress) + L"%";
    ListView_SetItemText(m_hList, idx, 2, const_cast<wchar_t*>(pct.c_str()));
}

void DownloadsPanel::OnProgress(int taskIdx, int pct) {
    if (taskIdx < 0 || taskIdx >= (int)m_tasks.size()) return;
    m_tasks[taskIdx].progress = pct;
    RefreshRow(taskIdx);
}

void DownloadsPanel::OnDone(int taskIdx, bool ok, const std::wstring& msg) {
    if (taskIdx < 0 || taskIdx >= (int)m_tasks.size()) return;
    auto& t = m_tasks[taskIdx];
    t.progress = ok ? 100 : t.progress;
    t.status   = ok ? DownloadTask::Status::Done :
                 (msg == L"Cancelled" ? DownloadTask::Status::Cancelled : DownloadTask::Status::Error);
    t.errMsg   = msg;
    RefreshRow(taskIdx);
    AppendLog((ok ? L"[Done] " : L"[Error] ") + t.title + (msg.empty() ? L"" : L" — " + msg));

    if (ok) LoadHistory();
}

void DownloadsPanel::AppendLog(const std::wstring& line) {
    int len = GetWindowTextLengthW(m_hLog);
    if (len > 2 * 1024 * 1024) {
        int half = len / 2;
        SendMessageW(m_hLog, EM_SETSEL, 0, half);
        SendMessageW(m_hLog, EM_REPLACESEL, FALSE, (LPARAM)L"");
        len = GetWindowTextLengthW(m_hLog);
    }
    SendMessageW(m_hLog, EM_SETSEL, len, len);
    SendMessageW(m_hLog, EM_REPLACESEL, FALSE, (LPARAM)(line + L"\r\n").c_str());
    SendMessageW(m_hLog, WM_VSCROLL, SB_BOTTOM, 0);
}

void DownloadsPanel::CancelSelected() {
    int idx = ListView_GetNextItem(m_hList, -1, LVNI_SELECTED);
    if (idx < 0 || idx >= (int)m_tasks.size()) return;
    m_tasks[idx].cancel->store(true);
}
