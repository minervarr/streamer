#pragma once
#include <windows.h>
#include <string>
#include <vector>

class WheelPicker {
public:
    void Create(HWND parent, HINSTANCE hInst, int id);
    void Resize(int x, int y, int w, int h);
    void Show(bool visible);

    void AddItem(const std::wstring& text);
    void SetSel(int index);
    int  GetSel() const { return m_sel; }

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

private:
    HWND m_hwnd = nullptr;
    int  m_id   = 0;
    int  m_sel  = 0;
    std::vector<std::wstring> m_items;
    HFONT m_hf        = nullptr;

    void  Paint();
    void  Step(int delta);
    LRESULT Handle(HWND, UINT, WPARAM, LPARAM);
};
