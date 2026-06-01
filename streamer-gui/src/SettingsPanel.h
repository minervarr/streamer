#pragma once
#include <windows.h>
#include <commctrl.h>
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
    HWND m_hwnd = nullptr;

    // ── Account list ──────────────────────────────────────────────
    HWND m_hHdAcct    = nullptr;
    HWND m_hAcctList  = nullptr;
    HWND m_hAddBtn    = nullptr;
    HWND m_hDelBtn    = nullptr;

    // ── Per-account login form ────────────────────────────────────
    HWND m_hLCountry  = nullptr;  HWND m_hCountry  = nullptr;
    HWND m_hLEmail    = nullptr;  HWND m_hEmail    = nullptr;
    HWND m_hLUserId   = nullptr;  HWND m_hUserId   = nullptr;
    HWND m_hLToken    = nullptr;  HWND m_hToken    = nullptr;
    HWND m_hLoginBtn  = nullptr;

    // ── Global settings ───────────────────────────────────────────
    HWND m_hHdSet     = nullptr;
    HWND m_hLDir      = nullptr;  HWND m_hDir    = nullptr;  HWND m_hDirBtn = nullptr;
    HWND m_hLQuality  = nullptr;
    WheelPicker m_quality;
    HWND m_hLConc     = nullptr;  HWND m_hConc   = nullptr;
    HWND m_hLRpm      = nullptr;  HWND m_hRpm    = nullptr;
    HWND m_hSaveSet   = nullptr;

    // ── Export / Import ───────────────────────────────────────────
    HWND m_hExportBtn = nullptr;
    HWND m_hImportBtn = nullptr;

    // ── Language ─────────────────────────────────────────────────
    HWND m_hLLang     = nullptr;
    HWND m_langCombo  = nullptr;

    Renderer*  m_rend  = nullptr;
    Config*    m_cfg   = nullptr;
    HINSTANCE  m_hInst = nullptr;
    int       m_selAcct = -1;

    std::function<void(const std::wstring&)> m_onLog;

    void RefreshAccountList();
    void ShowAccountEdit(bool visible);
    void LoadAccountFields(int idx);
    void DoTokenLogin();
    void SaveSettings();
    void BrowseDir();
    void ExportAccounts();
    void ImportAccounts();
    LRESULT HandleMsg(HWND, UINT, WPARAM, LPARAM);

    static std::wstring W2W(const std::string& s);
    static std::string  W2A(const std::wstring& w);
    static std::wstring GetText(HWND h);

    static constexpr int ID_ACCT_LIST  = 300;
    static constexpr int ID_ADD_ACCT   = 301;
    static constexpr int ID_DEL_ACCT   = 302;
    static constexpr int ID_COUNTRY    = 303;
    static constexpr int ID_EMAIL      = 304;
    static constexpr int ID_USER_ID    = 305;
    static constexpr int ID_TOKEN      = 306;
    static constexpr int ID_LOGIN      = 307;
    static constexpr int ID_DIR        = 310;
    static constexpr int ID_DIR_BTN    = 311;
    static constexpr int ID_QUALITY    = 312;
    static constexpr int ID_CONC       = 313;
    static constexpr int ID_RPM        = 314;
    static constexpr int ID_SAVE_SET   = 315;
    static constexpr int ID_EXPORT     = 316;
    static constexpr int ID_IMPORT     = 317;
    static constexpr int ID_LANG       = 318;
};
