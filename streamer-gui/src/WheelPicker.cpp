#include <windows.h>
#include "WheelPicker.h"

// ── WndProc ──────────────────────────────────────────────────────────────────

LRESULT CALLBACK WheelPicker::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    WheelPicker* self = nullptr;
    if (msg == WM_NCCREATE) {
        self = reinterpret_cast<WheelPicker*>(
            reinterpret_cast<CREATESTRUCT*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<WheelPicker*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->Handle(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT WheelPicker::Handle(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        Paint();
        return 0;

    case WM_MOUSEWHEEL:
        // Positive delta = scroll up → move selection backward (upward in list)
        Step(GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -1 : 1);
        return 0;

    case WM_LBUTTONDOWN: {
        if (m_items.empty()) return 0;
        RECT rc; GetClientRect(hwnd, &rc);
        int n     = (int)m_items.size();
        int half  = n / 2;
        int itemH = rc.bottom / n;
        if (itemH < 1) itemH = 1;
        int slot  = (int)HIWORD(lp) / itemH;
        int delta = slot - half;
        if (delta != 0) Step(delta);
        return 0;
    }

    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ── Create / layout ───────────────────────────────────────────────────────────

void WheelPicker::Create(HWND parent, HINSTANCE hInst, int id) {
    m_id = id;

    m_hf = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = hInst;
        wc.lpszClassName = L"WheelPicker";
        wc.hCursor       = LoadCursorW(nullptr, IDC_HAND);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassExW(&wc);
        registered = true;
    }

    CreateWindowExW(WS_EX_CLIENTEDGE, L"WheelPicker", L"",
        WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, parent, (HMENU)(INT_PTR)id, hInst, this);
}

void WheelPicker::Resize(int x, int y, int w, int h) {
    SetWindowPos(m_hwnd, nullptr, x, y, w, h, SWP_NOZORDER);
}

void WheelPicker::Show(bool visible) {
    ShowWindow(m_hwnd, visible ? SW_SHOW : SW_HIDE);
}

void WheelPicker::AddItem(const std::wstring& text) {
    m_items.push_back(text);
}

void WheelPicker::SetSel(int index) {
    if (!m_items.empty())
        m_sel = ((index % (int)m_items.size()) + (int)m_items.size()) % (int)m_items.size();
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
}

void WheelPicker::Step(int delta) {
    if (m_items.empty()) return;
    m_sel = (m_sel + delta + (int)m_items.size()) % (int)m_items.size();
    InvalidateRect(m_hwnd, nullptr, FALSE);
    PostMessageW(GetParent(m_hwnd), WM_COMMAND,
        MAKEWPARAM(m_id, 0), (LPARAM)m_hwnd);
}

// ── Paint ─────────────────────────────────────────────────────────────────────

void WheelPicker::Paint() {
    if (m_items.empty()) {
        PAINTSTRUCT ps; BeginPaint(m_hwnd, &ps); EndPaint(m_hwnd, &ps);
        return;
    }

    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(m_hwnd, &ps);

    RECT rc; GetClientRect(m_hwnd, &rc);
    int W = rc.right, H = rc.bottom;
    int n       = (int)m_items.size();
    int half    = n / 2;
    int itemH   = H / n;
    if (itemH < 1) itemH = 1;

    // Background
    FillRect(hdc, &rc, GetSysColorBrush(COLOR_WINDOW));

    // Highlight band at center slot (where selected item is drawn)
    int centerY = half * itemH;
    RECT band = { 0, centerY, W, centerY + itemH };
    FillRect(hdc, &band, GetSysColorBrush(COLOR_BTNFACE));

    // Separator lines above/below center band
    HPEN pen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNSHADOW));
    HPEN old = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, 0, centerY,         nullptr); LineTo(hdc, W, centerY);
    MoveToEx(hdc, 0, centerY + itemH, nullptr); LineTo(hdc, W, centerY + itemH);
    SelectObject(hdc, old);
    DeleteObject(pen);

    // Draw all slots; slot at offset 0 (center) = selected item
    SelectObject(hdc, m_hf);
    SetBkMode(hdc, TRANSPARENT);

    for (int slot = 0; slot < n; slot++) {
        int offset  = slot - half;
        int itemIdx = ((m_sel + offset) % n + n) % n;
        SetTextColor(hdc, offset == 0
            ? GetSysColor(COLOR_WINDOWTEXT)
            : RGB(160, 160, 160));
        RECT tr = { 4, slot * itemH, W - 4, (slot + 1) * itemH };
        DrawTextW(hdc, m_items[itemIdx].c_str(), -1, &tr,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    EndPaint(m_hwnd, &ps);
}
