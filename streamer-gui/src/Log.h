#pragma once
#include <string>

namespace Log {
    void Open();
    void Write(const std::wstring& line);
    void Close();
}
