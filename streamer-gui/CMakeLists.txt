cmake_minimum_required(VERSION 3.20)
project(streamer-gui CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/Renderer.cpp
    src/SettingsPanel.cpp
    src/SearchPanel.cpp
    src/DownloadsPanel.cpp
    src/Config.cpp
    src/QueryParser.cpp
    src/WheelPicker.cpp
    src/Log.cpp
    src/Strings.cpp
    src/InspectDialog.cpp
)

add_executable(streamer-gui WIN32 ${SOURCES})

target_include_directories(streamer-gui PRIVATE
    src
)

target_compile_definitions(streamer-gui PRIVATE
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
)

target_compile_options(streamer-gui PRIVATE /O2 /Ob2 /arch:AVX2 /MP /W3 /utf-8)

target_link_libraries(streamer-gui PRIVATE
    d2d1 dwrite windowscodecs
    kernel32 user32 gdi32 gdiplus
    comctl32 comdlg32 shell32 shlwapi
    ole32 oleaut32 uuid advapi32
    version ws2_32
)
