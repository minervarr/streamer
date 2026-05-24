#pragma once
#include <windows.h>
#include <string>
#include <functional>
#include "Renderer.h"
#include "Config.h"
#include "WheelPicker.h"

class SettingsPanel {
public:
    void Create(HWND parent, HINSTANCE hInst, Renderer* rend, Config* cfg);
    void Resize(int x, int y, int w, int h);
    void Show(bool visible);
    void SetLogCallback(std::function<void(const std::wstring&)> cb) { m_onLog = cb; }

    static LRESULT CALLBACK PanelProc(HWND, UINT, WPARAM, LPARAM);

private:
    HWND m_hwnd      = nullptr;
    // Section headings (Static)
    HWND m_hHdCred   = nullptr;
    HWND m_hHdSet    = nullptr;
    // Labels (Static)
    HWND m_hLAppId   = nullptr;
    HWND m_hLSecret  = nullptr;
    HWND m_hLUserId  = nullptr;
    HWND m_hLToken   = nullptr;
    HWND m_hLDir     = nullptr;
    HWND m_hLQuality = nullptr;
    HWND m_hLConc    = nullptr;
    HWND m_hLRpm     = nullptr;
    // Inputs
    HWND m_hAppId    = nullptr;
    HWND m_hSecret   = nullptr;
    HWND m_hUserId   = nullptr;
    HWND m_hToken    = nullptr;
    HWND m_hDir      = nullptr;
    HWND m_hDirBtn   = nullptr;
    WheelPicker m_quality;
    HWND m_hConc     = nullptr;
    HWND m_hRpm      = nullptr;
    HWND m_hSaveBtn  = nullptr;
    HWND m_hLoginBtn = nullptr;
    Renderer* m_rend = nullptr;
    Config*   m_cfg  = nullptr;
    std::function<void(const std::wstring&)> m_onLog;

    void LoadFields();
    void SaveFields();
    void DoLogin();
    void BrowseDir();
    LRESULT HandleMsg(HWND, UINT, WPARAM, LPARAM);

    static constexpr int ID_APP_ID    = 300;
    static constexpr int ID_SECRET    = 301;
    static constexpr int ID_USER_ID   = 302;
    static constexpr int ID_TOKEN     = 303;
    static constexpr int ID_DIR       = 304;
    static constexpr int ID_DIR_BTN   = 305;
    static constexpr int ID_QUALITY   = 306;
    static constexpr int ID_CONC      = 307;
    static constexpr int ID_RPM       = 308;
    static constexpr int ID_SAVE      = 309;
    static constexpr int ID_LOGIN     = 310;
};
