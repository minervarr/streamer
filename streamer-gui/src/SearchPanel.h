#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <set>
#include <functional>
#include "Renderer.h"
#include "Config.h"
#include "QueryParser.h"
#include "WheelPicker.h"

struct CartItem {
    std::wstring id;
    std::wstring title;
    std::wstring artist;
    std::wstring type;
    std::wstring country;
    int accountIdx = 0;
};

class SearchPanel {
public:
    void Create(HWND parent, HINSTANCE hInst, Renderer* rend);
    void Resize(int x, int y, int w, int h);
    void Show(bool visible);
    void SetDownloadCallback(std::function<void(const std::wstring&, const std::wstring&)> cb) { m_onDownload = cb; }
    void SetConfig(Config* cfg) { m_cfg = cfg; }

    static LRESULT CALLBACK PanelProc(HWND, UINT, WPARAM, LPARAM);

private:
    HWND m_hwnd    = nullptr;
    HWND m_hSearch = nullptr;
    WheelPicker m_type;
    HWND m_hBtn    = nullptr;
    HWND m_hList   = nullptr;
    HWND m_hCount  = nullptr;
    HWND m_hDlBtn  = nullptr;
    Renderer* m_rend = nullptr;
    Config*   m_cfg  = nullptr;

    HWND m_hCartLabel = nullptr;
    HWND m_hCartList  = nullptr;
    HWND m_hCartClear = nullptr;

    std::vector<SearchResult> m_allResults;
    std::vector<int>          m_filtered;
    std::set<std::wstring>    m_checkedIds;
    std::vector<CartItem>     m_cart;
    bool m_suppressCheckNotify = false;

    std::function<void(const std::wstring&, const std::wstring&)> m_onDownload;

    void DoSearch();
    void ParseTsv(const std::string& raw, const std::wstring& defaultType, const std::wstring& country);
    void ApplyFilter(const QueryNode& ast);
    void PopulateList();
    void OnItemSelect(int idx);
    void OnCheckChanged(int idx);
    void OnDownload();
    void OnContextMenu(int listIdx, POINT pt);
    void UpdateDownloadButton();
    void RefreshCart();
    LRESULT HandleMsg(HWND, UINT, WPARAM, LPARAM);

    static std::wstring A2W(const std::string& s);
    static std::string  W2A(const std::wstring& w);

    static constexpr int ID_SEARCH_EDIT = 200;
    static constexpr int ID_TYPE_COMBO  = 201;
    static constexpr int ID_SEARCH_BTN  = 202;
    static constexpr int ID_RESULT_LIST = 203;
    static constexpr int ID_DL_BTN      = 204;
    static constexpr int ID_CART_LIST   = 205;
    static constexpr int ID_CART_CLEAR  = 206;
    static constexpr int IDM_DL_ITEM    = 210;
};
