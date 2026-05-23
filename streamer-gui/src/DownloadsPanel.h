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

// Posted to main HWND from worker thread
#define WM_TASK_PROGRESS (WM_USER + 1)
#define WM_TASK_DONE     (WM_USER + 2)

class DownloadsPanel {
public:
    void Create(HWND parent, HINSTANCE hInst, Renderer* rend);
    void Resize(int x, int y, int w, int h);
    void Show(bool visible);
    void AddDownload(const std::wstring& url, const std::wstring& title);

    // Called from MainWindow when WM_TASK_PROGRESS/DONE arrives
    void OnProgress(int taskIdx, int pct);
    void OnDone(int taskIdx, bool ok, const std::wstring& msg);

    HWND Hwnd() const { return m_hwnd; }

    static LRESULT CALLBACK PanelProc(HWND, UINT, WPARAM, LPARAM);

private:
    HWND m_hwnd       = nullptr;
    HWND m_hList      = nullptr;
    HWND m_hCancelBtn = nullptr;
    Renderer* m_rend  = nullptr;

    std::vector<DownloadTask> m_tasks;

    void RefreshRow(int idx);
    void StartTask(int idx);
    void CancelSelected();
    LRESULT HandleMsg(HWND, UINT, WPARAM, LPARAM);

    static constexpr int ID_DL_LIST   = 400;
    static constexpr int ID_CANCEL_BTN= 401;
};
