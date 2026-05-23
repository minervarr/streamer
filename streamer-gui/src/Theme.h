#pragma once
#include <windows.h>
#include <d2d1.h>
#include <d2d1helper.h>

namespace Theme {
    inline D2D1_COLOR_F SysColor(int index) {
        COLORREF c = GetSysColor(index);
        return D2D1::ColorF(GetRValue(c) / 255.f,
                            GetGValue(c) / 255.f,
                            GetBValue(c) / 255.f);
    }

    inline D2D1_COLOR_F TabBg()       { return SysColor(COLOR_BTNFACE);   }
    inline D2D1_COLOR_F TabActiveBg() { return SysColor(COLOR_WINDOW);    }
    inline D2D1_COLOR_F TabText()     { return SysColor(COLOR_BTNTEXT);   }
    inline D2D1_COLOR_F Accent()      { return SysColor(COLOR_HIGHLIGHT); }
    inline D2D1_COLOR_F Divider()     { return SysColor(COLOR_BTNSHADOW); }
    inline D2D1_COLOR_F WinBg()       { return SysColor(COLOR_WINDOW);    }

    constexpr float TabH    = 34.f;
    constexpr float UrlBarH = 38.f;
    constexpr float FontSz  = 14.f;
    constexpr float FontSzSm= 12.f;
}
