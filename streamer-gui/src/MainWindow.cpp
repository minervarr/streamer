#include <windows.h>
#include <commctrl.h>
#include "MainWindow.h"
#include "SettingsPanel.h"
#include "SearchPanel.h"
#include "DownloadsPanel.h"
#include "Theme.h"

static std::wstring GetText(HWND hwnd) {
    int len = GetWindowTextLengthW(hwnd);
    if (len <= 0) return {};
    std::wstring s(len + 1, 0);
    GetWindowTextW(hwnd, s.data(), len + 1);
    s.resize(len);
    return s;
}

// ── WndProc ──────────────────────────────────────────────────────────────────

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        self = reinterpret_cast<MainWindow*>(
            reinterpret_cast<CREATESTRUCT*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMsg(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT MainWindow::HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        OnCreate();
        return 0;

    case WM_SIZE:
        if (wp != SIZE_MINIMIZED)
            OnSize(LOWORD(lp), HIWORD(lp));
        return 0;

    case WM_ERASEBKGND:
        return 1;  // D2D handles the full window background

    case WM_PAINT:
        OnPaint();
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == ID_GO_BTN) { OnUrlGo(); return 0; }
        break;
    }

    case WM_LBUTTONDOWN: {
        int x = LOWORD(lp), y = HIWORD(lp);
        if (y >= (int)Theme::UrlBarH && y < (int)(Theme::UrlBarH + Theme::TabH)) {
            RECT rc; GetClientRect(hwnd, &rc);
            int tabW = rc.right / (int)Tab::COUNT;
            int t = x / tabW;
            if (t >= 0 && t < (int)Tab::COUNT)
                OnTab((Tab)t);
        }
        return 0;
    }

    case WM_KEYDOWN:
        if (wp == VK_RETURN && GetFocus() == m_hUrl) { OnUrlGo(); return 0; }
        break;

    case WM_TASK_PROGRESS:
        if (m_downloads) m_downloads->OnProgress((int)wp, (int)lp);
        return 0;
    case WM_TASK_DONE: {
        auto* msg2 = reinterpret_cast<std::wstring*>(lp);
        if (m_downloads) m_downloads->OnDone((int)wp, msg2 == nullptr, msg2 ? *msg2 : L"");
        delete msg2;
        return 0;
    }

    case WM_SYSCOLORCHANGE:
        // System colors changed (theme switch) — repaint tab bar
        InvalidateRect(m_hwnd, nullptr, FALSE);
        break;

    case WM_DESTROY:
        m_rend.Destroy();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ── Create ────────────────────────────────────────────────────────────────────

bool MainWindow::Create(HINSTANCE hInst) {
    m_hInst = hInst;

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = L"StreamerMain";
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassExW(&wc);

    CreateWindowExW(0, L"StreamerMain", L"Streamer",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 960, 680,
        nullptr, nullptr, hInst, this);

    return m_hwnd != nullptr;
}

void MainWindow::Show(int nCmdShow) {
    ShowWindow(m_hwnd, nCmdShow);
    UpdateWindow(m_hwnd);
}

void MainWindow::OnCreate() {
    cfg = Config::Load();
    m_rend.Init(m_hwnd);

    HFONT hf = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

    m_hUrl = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        0, 0, 0, 0, m_hwnd, (HMENU)(INT_PTR)ID_URL_BAR, m_hInst, nullptr);
    SendMessage(m_hUrl, WM_SETFONT, (WPARAM)hf, TRUE);
    SendMessageW(m_hUrl, EM_SETCUEBANNER, TRUE,
        (LPARAM)L"Paste a Qobuz URL and press Go to download directly");

    m_hGoBtn = CreateWindowExW(0, L"BUTTON", L"Go",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, m_hwnd, (HMENU)(INT_PTR)ID_GO_BTN, m_hInst, nullptr);
    SendMessage(m_hGoBtn, WM_SETFONT, (WPARAM)hf, TRUE);

    m_settings  = std::make_unique<SettingsPanel>();
    m_search    = std::make_unique<SearchPanel>();
    m_downloads = std::make_unique<DownloadsPanel>();

    m_settings->Create(m_hwnd, m_hInst, &m_rend, &cfg);
    m_search->Create(m_hwnd, m_hInst, &m_rend);
    m_downloads->Create(m_hwnd, m_hInst, &m_rend);

    m_search->SetDownloadCallback([this](const std::wstring& url, const std::wstring& title) {
        QueueDownload(url, title);
    });

    m_settings->SetLogCallback([this](const std::wstring& line) {
        m_downloads->AppendLog(line);
    });

    OnTab(Tab::Search);
}

void MainWindow::OnSize(int w, int h) {
    m_rend.Resize((UINT)w, (UINT)h);

    const int pad  = 6;
    const int urlH = (int)Theme::UrlBarH - pad * 2;
    int urlW = w - pad * 3 - 50;
    SetWindowPos(m_hUrl,   nullptr, pad, pad, urlW, urlH, SWP_NOZORDER);
    SetWindowPos(m_hGoBtn, nullptr, pad + urlW + pad, pad, 46, urlH, SWP_NOZORDER);

    LayoutPanels(w, h);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::LayoutPanels(int w, int h) {
    const int topBar = (int)(Theme::UrlBarH + Theme::TabH);
    m_settings->Resize(0, topBar, w, h - topBar);
    m_search->Resize(0, topBar, w, h - topBar);
    m_downloads->Resize(0, topBar, w, h - topBar);
}

void MainWindow::OnTab(Tab t) {
    m_tab = t;
    m_settings->Show(t == Tab::Settings);
    m_search->Show(t == Tab::Search);
    m_downloads->Show(t == Tab::Downloads);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void MainWindow::OnUrlGo() {
    std::wstring url = GetText(m_hUrl);
    if (url.empty()) return;
    QueueDownload(url, url);
    OnTab(Tab::Downloads);
}

void MainWindow::QueueDownload(const std::wstring& url, const std::wstring& title) {
    m_downloads->AddDownload(url, title);
    OnTab(Tab::Downloads);
}

// ── Paint ─────────────────────────────────────────────────────────────────────

void MainWindow::OnPaint() {
    PAINTSTRUCT ps;
    BeginPaint(m_hwnd, &ps);
    if (!m_rend.BeginDraw()) { EndPaint(m_hwnd, &ps); return; }

    RECT rc; GetClientRect(m_hwnd, &rc);
    float W = (float)rc.right;

    // Fill only the top strip (URL bar + tab bar); panels cover the rest
    m_rend.Clear(Theme::TabBg());

    DrawTabBar((int)W);

    m_rend.EndDraw();
    EndPaint(m_hwnd, &ps);
}

void MainWindow::DrawTabBar(int W) {
    const float y0   = Theme::UrlBarH;
    const float y1   = y0 + Theme::TabH;
    const float tabW = (float)W / (float)Tab::COUNT;

    m_rend.FillRect({0, y0, (float)W, y1}, Theme::TabBg());

    const wchar_t* labels[] = { L"Search", L"Downloads", L"Settings" };
    for (int i = 0; i < (int)Tab::COUNT; i++) {
        float x0 = tabW * i;
        float x1 = x0 + tabW;
        bool  active = ((int)m_tab == i);

        if (active) {
            m_rend.FillRect({x0, y0, x1, y1}, Theme::TabActiveBg());
            // accent underline
            m_rend.FillRect({x0 + 6, y1 - 2, x1 - 6, y1}, Theme::Accent());
        }

        m_rend.DrawText(labels[i],
            {x0, y0, x1, y1},
            Theme::TabText(),
            Theme::FontSz,
            DWRITE_TEXT_ALIGNMENT_CENTER,
            DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
            active);

        if (i < (int)Tab::COUNT - 1)
            m_rend.DrawLine(x1, y0 + 6, x1, y1 - 6, Theme::Divider());
    }
}
