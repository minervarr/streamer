#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>

struct InspectTrack {
    int  num        = 0;
    int  disc       = 1;
    std::wstring title;
    int  duration   = 0;   // seconds
    bool streamable = false;
    bool downloadable = false;
    bool hires      = false;
    bool explicit_  = false;
};

struct InspectAlbum {
    std::wstring title;
    std::wstring artist;
    std::wstring year;
    std::wstring label;
    std::wstring releaseType;
    std::wstring country;
    bool hires      = false;
    int  lockedCount = 0;
    std::vector<InspectTrack> tracks;
};

class InspectDialog {
public:
    // Runs streamer.exe inspect --tsv for the given albumId/accountIdx,
    // parses the output, and shows a modal dialog on top of `parent`.
    static void Show(HWND parent, HINSTANCE hInst,
                     const std::wstring& albumId, int accountIdx,
                     const std::wstring& country);

    static constexpr int ID_LIST   = 100;
    static constexpr int ID_HEADER = 101;
    static constexpr int ID_CLOSE  = 102;

private:
    static InspectAlbum ParseTsv(const std::string& raw);
    static std::wstring FmtDur(int s);
    static INT_PTR CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
};
