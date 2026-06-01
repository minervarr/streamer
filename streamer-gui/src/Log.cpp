#include <windows.h>
#include <shlobj.h>
#include <string>
#include <cstdio>
#include "Log.h"

static FILE* s_file = nullptr;

static std::wstring LogPath() {
    wchar_t appdata[MAX_PATH];
    SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata);
    return std::wstring(appdata) + L"\\streamer\\streamer-gui.log";
}

static std::wstring BakPath() {
    wchar_t appdata[MAX_PATH];
    SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata);
    return std::wstring(appdata) + L"\\streamer\\streamer-gui.log.bak";
}

static void WriteRaw(const wchar_t* line) {
    if (!s_file) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    fwprintf(s_file, L"[%02d:%02d:%02d] %s\r\n", st.wHour, st.wMinute, st.wSecond, line);
    fflush(s_file);
}

void Log::Open() {
    std::wstring path = LogPath();

    // Rotate if file exceeds 1 MB
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info)) {
        ULONGLONG size = ((ULONGLONG)info.nFileSizeHigh << 32) | info.nFileSizeLow;
        if (size > 1024 * 1024) {
            std::wstring bak = BakPath();
            DeleteFileW(bak.c_str());
            MoveFileW(path.c_str(), bak.c_str());
        }
    }

    s_file = _wfopen(path.c_str(), L"a, ccs=UTF-8");
    WriteRaw(L"=== session start ===");
}

void Log::Write(const std::wstring& line) {
    WriteRaw(line.c_str());
}

void Log::Close() {
    WriteRaw(L"=== session end ===");
    if (s_file) { fclose(s_file); s_file = nullptr; }
}
