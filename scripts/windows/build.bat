@echo off
REM streamer desktop build (Windows) -> build\streamer.exe (+ build\gui\streamer_gui.exe
REM when a Slang shader compiler is found). Two toolchains are supported;
REM this script picks whichever one is actually on PATH:
REM
REM   1) Ninja + Clang from an MSYS2 UCRT64 shell (recommended for the GUI --
REM      gui\src\os\win32_host.cc is a plain Win32 host, no MSVC dependency).
REM      Run this script from an "MSYS2 UCRT64" shell so clang++/ninja/
REM      pkg-config resolve; a plain cmd.exe / PowerShell prompt normally
REM      does not have C:\msys64\ucrt64\bin on PATH.
REM   2) MSVC + vcpkg, per CLAUDE.md, when clang++ isn't found.
REM
REM First time: git submodule update --init --recursive
setlocal
cd /d "%~dp0..\.."

where clang++ >nul 2>nul
if %errorlevel%==0 (
    echo ==> clang++ found on PATH: configuring with Ninja + Clang (MSYS2 UCRT64)
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    echo.
    echo Done: build\streamer.exe
    if exist build\gui\streamer_gui.exe echo       build\gui\streamer_gui.exe
    echo Run it from outside the MSYS2 shell too -- the runtime is statically
    echo linked, so it does not need MSYS2's DLLs on PATH.
) else (
    echo ==> clang++ not found: configuring with MSVC + vcpkg
    if not defined VCPKG_ROOT (
        echo error: VCPKG_ROOT is not set. Install vcpkg and set VCPKG_ROOT, >&2
        echo        or run this script from an MSYS2 UCRT64 shell instead. >&2
        exit /b 1
    )
    cmake -B build -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release
    echo.
    echo Done: build\Release\streamer.exe
    if exist build\Release\gui\streamer_gui.exe echo       build\Release\gui\streamer_gui.exe
)
endlocal
