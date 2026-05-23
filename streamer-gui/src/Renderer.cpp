#include <windows.h>
#include "Renderer.h"

bool Renderer::Init(HWND hwnd) {
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_factory.GetAddressOf())))
        return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(m_dwrite.GetAddressOf()))))
        return false;

    RECT rc; GetClientRect(hwnd, &rc);
    D2D1_RENDER_TARGET_PROPERTIES rtp = D2D1::RenderTargetProperties();
    D2D1_HWND_RENDER_TARGET_PROPERTIES hwndp = D2D1::HwndRenderTargetProperties(
        hwnd, D2D1::SizeU(rc.right, rc.bottom));
    if (FAILED(m_factory->CreateHwndRenderTarget(rtp, hwndp, m_rt.GetAddressOf())))
        return false;

    m_rt->CreateSolidColorBrush(D2D1::ColorF(0,0,0,1), m_brush.GetAddressOf());

    // pre-create text formats
    m_dwrite->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        Theme::FontSz, L"", m_fmtReg.GetAddressOf());
    m_dwrite->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        Theme::FontSz, L"", m_fmtBold.GetAddressOf());
    m_dwrite->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        Theme::FontSzSm, L"", m_fmtSmReg.GetAddressOf());
    m_dwrite->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        Theme::FontSzSm, L"", m_fmtSmBold.GetAddressOf());

    return true;
}

void Renderer::Resize(UINT w, UINT h) {
    if (m_rt) m_rt->Resize(D2D1::SizeU(w, h));
}

void Renderer::Destroy() {
    m_brush.Reset(); m_fmtReg.Reset(); m_fmtBold.Reset();
    m_fmtSmReg.Reset(); m_fmtSmBold.Reset();
    m_rt.Reset(); m_dwrite.Reset(); m_factory.Reset();
}

bool Renderer::BeginDraw() {
    if (!m_rt) return false;
    m_rt->BeginDraw();
    return true;
}

bool Renderer::EndDraw() {
    return m_rt->EndDraw() != D2DERR_RECREATE_TARGET;
}

void Renderer::Clear(D2D1_COLOR_F color) {
    m_rt->Clear(color);
}

void Renderer::FillRect(D2D1_RECT_F r, D2D1_COLOR_F color) {
    m_brush->SetColor(color);
    m_rt->FillRectangle(r, m_brush.Get());
}

void Renderer::FillRoundRect(D2D1_RECT_F r, float radius, D2D1_COLOR_F color) {
    m_brush->SetColor(color);
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), m_brush.Get());
}

void Renderer::DrawRect(D2D1_RECT_F r, D2D1_COLOR_F color, float stroke) {
    m_brush->SetColor(color);
    m_rt->DrawRectangle(r, m_brush.Get(), stroke);
}

void Renderer::DrawRoundRect(D2D1_RECT_F r, float radius, D2D1_COLOR_F color, float stroke) {
    m_brush->SetColor(color);
    m_rt->DrawRoundedRectangle(D2D1::RoundedRect(r, radius, radius), m_brush.Get(), stroke);
}

void Renderer::DrawLine(float x0, float y0, float x1, float y1, D2D1_COLOR_F color, float stroke) {
    m_brush->SetColor(color);
    m_rt->DrawLine(D2D1::Point2F(x0, y0), D2D1::Point2F(x1, y1), m_brush.Get(), stroke);
}

IDWriteTextFormat* Renderer::GetFmt(float sz, bool bold) {
    if (sz <= Theme::FontSzSm + 0.5f)
        return bold ? m_fmtSmBold.Get() : m_fmtSmReg.Get();
    return bold ? m_fmtBold.Get() : m_fmtReg.Get();
}

void Renderer::DrawText(const std::wstring& text, D2D1_RECT_F r, D2D1_COLOR_F color,
                        float fontSize, DWRITE_TEXT_ALIGNMENT hAlign,
                        DWRITE_PARAGRAPH_ALIGNMENT vAlign, bool bold) {
    auto* fmt = GetFmt(fontSize, bold);
    if (!fmt) return;
    fmt->SetTextAlignment(hAlign);
    fmt->SetParagraphAlignment(vAlign);
    m_brush->SetColor(color);
    m_rt->DrawTextW(text.c_str(), (UINT32)text.size(), fmt, r, m_brush.Get());
}
