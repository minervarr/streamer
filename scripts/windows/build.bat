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
REM
REM NOTE: never write a bare `>` inside an `echo` line below (not even as
REM decoration like "==>") -- cmd.exe treats it as output redirection, not
REM literal text, and silently creates a file named after whatever token
REM follows instead of printing anything. That exact bug previously left
REM stray files named `clang++` and `Clang` sitting in the repo root instead
REM of ever printing the messages they were supposed to. Use "::" or "*" for
REM decoration if wanted; if a literal `>` is ever needed, escape it as `^>`.
setlocal
cd /d "%~dp0..\.."

REM Resolve the actual clang++.exe path (not just check it's *somewhere* on
REM PATH) and pass it to CMake explicitly. A bare `cmake -B build -G Ninja`
REM does NOT prefer clang++ over c++/g++ when both exist on PATH -- CMake's
REM default compiler search finds c++/g++ first, so an MSYS2 UCRT64 install
REM with both mingw-w64-ucrt-x86_64-clang AND a GCC toolchain package
REM installed would silently build with GCC while this script claimed Clang.
set "CLANGXX="
for /f "delims=" %%i in ('where clang++ 2^>nul') do if not defined CLANGXX set "CLANGXX=%%i"

if defined CLANGXX (
    set "CLANGCC=%CLANGXX:clang++.exe=clang.exe%"
    echo Clang found: %CLANGXX% -- configuring with Ninja + Clang (MSYS2 UCRT64)
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER="%CLANGCC%" -DCMAKE_CXX_COMPILER="%CLANGXX%"
    cmake --build build
    echo.
    echo Done: build\streamer.exe
    if exist build\gui\streamer_gui.exe echo       build\gui\streamer_gui.exe
    echo Run it from outside the MSYS2 shell too -- the runtime is statically
    echo linked, so it does not need MSYS2's DLLs on PATH.
) else (
    echo clang++ not found: configuring with MSVC + vcpkg
    if not defined VCPKG_ROOT (
        echo error: VCPKG_ROOT is not set. Install vcpkg and set VCPKG_ROOT, 1>&2
        echo        or run this script from an MSYS2 UCRT64 shell instead. 1>&2
        exit /b 1
    )
    cmake -B build -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release
    echo.
    echo Done: build\Release\streamer.exe
    if exist build\Release\gui\streamer_gui.exe echo       build\Release\gui\streamer_gui.exe
)
endlocal
