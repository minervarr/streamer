#include <iostream>
#include <string>
#include <windows.h>

int main() {
    std::string utf8 = "C:/Users/incxiuefb/Documents/uSick\\FR\\Lu\xc3\xadsa Sonza\\Esc\xc3\xa2ndalo \xc3\x8dntimo (Deluxe) (24-48)\\289550652.flac";
    
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring wide(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wide[0], len);
    
    std::wcout << L"Wide string: " << wide.c_str() << L" (length: " << wide.length() << L")" << std::endl;
    
    // Check if the file exists using the wide string
    DWORD attr = GetFileAttributesW(wide.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        std::cout << "File does not exist or cannot be accessed! Error: " << GetLastError() << std::endl;
    } else {
        std::cout << "File exists!" << std::endl;
    }
    
    return 0;
}
