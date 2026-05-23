#include <windows.h>
#include <commctrl.h>
#include <string>
#include <thread>
#include "DownloadsPanel.h"

// ── Worker thread ─────────────────────────────────────────────────────────────

struct WorkerArgs {
    HWND   notify;   // main HWND for PostMessage
    int    taskIdx;
    std::wstring url;
    std::shared_ptr<std::atomic<bool>> cancel;
};

static DWORD WINAPI DownloadWorker(LPVOID param) {
    auto* args = reinterpret_cast<WorkerArgs*>(param);
    std::wstring cmd = L"streamer.exe download \"" + args->url + L"\"";

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
            (WPARAM)args->taskIdx, (LPARAM) new std::wstring(L"Failed to start process"));
        delete args; CloseHandle(hRead); return 1;
    }

    // Read output line by line, parse progress hints
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

            // simple heuristic: if line contains a percentage
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
        (LPARAM)(success ? nullptr : new std::wstring(args->cancel->load() ? L"Cancelled" : L"Error")));
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

    m_hList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 0, 0, m_hwnd, (HMENU)(INT_PTR)ID_DL_LIST, hInst, nullptr);
    ListView_SetExtendedListViewStyle(m_hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    auto addCol = [&](const wchar_t* name, int w, int i) {
        LVCOLUMNW col = {};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = const_cast<wchar_t*>(name);
        col.cx = w;
        ListView_InsertColumn(m_hList, i, &col);
    };
    addCol(L"Title",   360, 0);
    addCol(L"Status",  120, 1);
    addCol(L"Progress", 80, 2);

    m_hCancelBtn = CreateWindowExW(0, L"BUTTON", L"Cancel Selected",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, m_hwnd, (HMENU)(INT_PTR)ID_CANCEL_BTN, hInst, nullptr);

    SendMessage(m_hList,      WM_SETFONT, (WPARAM)hf, TRUE);
    SendMessage(m_hCancelBtn, WM_SETFONT, (WPARAM)hf, TRUE);
}

void DownloadsPanel::Resize(int x, int y, int w, int h) {
    SetWindowPos(m_hwnd, nullptr, x, y, w, h, SWP_NOZORDER);
    const int pad = 12;
    int listH = h - pad * 3 - 36;
    SetWindowPos(m_hList,      nullptr, pad, pad, w - pad*2, listH, SWP_NOZORDER);
    SetWindowPos(m_hCancelBtn, nullptr, w - pad - 160, pad + listH + pad, 160, 32, SWP_NOZORDER);
}

void DownloadsPanel::Show(bool visible) {
    ShowWindow(m_hwnd, visible ? SW_SHOW : SW_HIDE);
}

// ── Logic ─────────────────────────────────────────────────────────────────────

void DownloadsPanel::AddDownload(const std::wstring& url, const std::wstring& title) {
    DownloadTask task;
    task.url    = url;
    task.title  = title;
    task.cancel = std::make_shared<std::atomic<bool>>(false);

    int idx = (int)m_tasks.size();
    m_tasks.push_back(task);

    // Insert row
    LVITEMW item = {};
    item.mask    = LVIF_TEXT;
    item.iItem   = idx;
    item.pszText = const_cast<wchar_t*>(title.c_str());
    ListView_InsertItem(m_hList, &item);
    ListView_SetItemText(m_hList, idx, 1, (LPWSTR)L"Queued");
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
    const wchar_t* statusStr = L"Queued";
    switch (t.status) {
    case DownloadTask::Status::Running:   statusStr = L"Downloading"; break;
    case DownloadTask::Status::Done:      statusStr = L"Done";        break;
    case DownloadTask::Status::Error:     statusStr = L"Error";       break;
    case DownloadTask::Status::Cancelled: statusStr = L"Cancelled";   break;
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
}

void DownloadsPanel::CancelSelected() {
    int idx = ListView_GetNextItem(m_hList, -1, LVNI_SELECTED);
    if (idx < 0 || idx >= (int)m_tasks.size()) return;
    m_tasks[idx].cancel->store(true);
}
