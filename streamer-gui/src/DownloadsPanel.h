#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <atomic>
#include <memory>
#include "Renderer.h"

struct DownloadTask {
    std::wstring url, title;
    enum class Status { Queued, Running, Done, Error, Cancelled } status = Status::Queued;
    int progress = 0;
    std::wstring errMsg;
    std::shared_ptr<std::atomic<bool>> cancel;
    int listRow = -1;
};

struct HistoryRecord {
    std::wstring title;
    std::wstring artist;
    std::wstring date;
    std::wstring quality;
    std::wstring country;
    std::wstring status;
};

// Posted to panel HWND from worker thread
#define WM_TASK_PROGRESS (WM_USER + 1)
#define WM_TASK_DONE     (WM_USER + 2)
#define WM_TASK_LOG      (WM_USER + 3)   // lParam = new std::wstring*

class DownloadsPanel {
public:
    void Create(HWND parent, HINSTANCE hInst, Renderer* rend);
    void Resize(int x, int y, int w, int h);
    void Show(bool visible);
    void AddDownload(const std::wstring& url, const std::wstring& title);
    void AppendLog(const std::wstring& line);
    void LoadHistory();

    // Called from MainWindow when WM_TASK_PROGRESS/DONE arrives
    void OnProgress(int taskIdx, int pct);
    void OnDone(int taskIdx, bool ok, const std::wstring& msg);

    HWND Hwnd() const { return m_hwnd; }

    static LRESULT CALLBACK PanelProc(HWND, UINT, WPARAM, LPARAM);

private:
    HWND m_hwnd       = nullptr;
    HWND m_hList      = nullptr;
    HWND m_hCancelBtn = nullptr;
    HWND m_hLog       = nullptr;
    HWND m_hHistLabel = nullptr;
    HWND m_hHistList  = nullptr;
    HWND m_hExportBtn = nullptr;
    HWND m_hImportBtn = nullptr;
    Renderer* m_rend  = nullptr;

    std::vector<DownloadTask> m_tasks;
    std::vector<HistoryRecord> m_history;

    void RefreshRow(int idx);
    void StartTask(int idx);
    void CancelSelected();
    void OnExport();
    void OnImport();
    LRESULT HandleMsg(HWND, UINT, WPARAM, LPARAM);

    static constexpr int ID_DL_LIST    = 400;
    static constexpr int ID_CANCEL_BTN = 401;
    static constexpr int ID_LOG        = 402;
    static constexpr int ID_HIST_LIST  = 403;
    static constexpr int ID_EXPORT_BTN = 404;
    static constexpr int ID_IMPORT_BTN = 405;
};
