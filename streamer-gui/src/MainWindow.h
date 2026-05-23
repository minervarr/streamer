#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <memory>
#include "Renderer.h"
#include "Config.h"
#include "SettingsPanel.h"
#include "SearchPanel.h"
#include "DownloadsPanel.h"

enum class Tab { Search = 0, Downloads, Settings, COUNT };

class MainWindow {
public:
    bool Create(HINSTANCE hInst);
    void Show(int nCmdShow);
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

    // Called by panels to queue a download
    void QueueDownload(const std::wstring& url, const std::wstring& title);

    Config  cfg;

private:
    HWND      m_hwnd   = nullptr;
    HINSTANCE m_hInst  = nullptr;
    Renderer  m_rend;
    Tab       m_tab    = Tab::Search;

    // URL bar
    HWND m_hUrl    = nullptr;
    HWND m_hGoBtn  = nullptr;

    // Panels (child HWNDs managed by each class)
    std::unique_ptr<SettingsPanel>   m_settings;
    std::unique_ptr<SearchPanel>     m_search;
    std::unique_ptr<DownloadsPanel>  m_downloads;

    void OnCreate();
    void OnSize(int w, int h);
    void OnPaint();
    void OnTab(Tab t);
    void OnUrlGo();
    void LayoutPanels(int w, int h);
    void DrawTabBar(int w);

    LRESULT HandleMsg(HWND, UINT, WPARAM, LPARAM);

    static constexpr int ID_URL_BAR = 100;
    static constexpr int ID_GO_BTN  = 101;
};
