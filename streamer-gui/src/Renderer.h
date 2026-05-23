#pragma once
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <string>
#include "Theme.h"

using Microsoft::WRL::ComPtr;

class Renderer {
public:
    bool Init(HWND hwnd);
    void Resize(UINT w, UINT h);
    void Destroy();

    bool BeginDraw();
    // returns false if device lost (caller should recreate)
    bool EndDraw();

    void Clear(D2D1_COLOR_F color);

    // Filled shapes
    void FillRect(D2D1_RECT_F r, D2D1_COLOR_F color);
    void FillRoundRect(D2D1_RECT_F r, float radius, D2D1_COLOR_F color);

    // Stroked shapes
    void DrawRect(D2D1_RECT_F r, D2D1_COLOR_F color, float stroke = 1.f);
    void DrawRoundRect(D2D1_RECT_F r, float radius, D2D1_COLOR_F color, float stroke = 1.f);
    void DrawLine(float x0, float y0, float x1, float y1, D2D1_COLOR_F color, float stroke = 1.f);

    // Text
    void DrawText(const std::wstring& text, D2D1_RECT_F r, D2D1_COLOR_F color,
                  float fontSize = Theme::FontSz,
                  DWRITE_TEXT_ALIGNMENT hAlign = DWRITE_TEXT_ALIGNMENT_LEADING,
                  DWRITE_PARAGRAPH_ALIGNMENT vAlign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
                  bool bold = false);

    ID2D1HwndRenderTarget* RT() { return m_rt.Get(); }
    IDWriteFactory*        DW() { return m_dwrite.Get(); }

private:
    ComPtr<ID2D1Factory>           m_factory;
    ComPtr<ID2D1HwndRenderTarget>  m_rt;
    ComPtr<IDWriteFactory>         m_dwrite;
    ComPtr<ID2D1SolidColorBrush>   m_brush;

    IDWriteTextFormat* GetFmt(float sz, bool bold);
    ComPtr<IDWriteTextFormat> m_fmtReg, m_fmtBold, m_fmtSmReg, m_fmtSmBold;
};
