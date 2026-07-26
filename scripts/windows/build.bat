@echo off
REM streamer desktop build (Windows). Stub: the CLI + legacy streamer-gui
REM build via the existing vcpkg CMake flow documented in CLAUDE.md; the
REM new gui/ target's Windows host (gui/src/os/win32_host.cc) has not been
REM implemented yet, so this script currently mirrors CLAUDE.md's manual
REM steps for the CLI/legacy GUI. Update once a Windows host lands.
setlocal
cd /d "%~dp0..\.."

cmake -B build -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

echo.
echo Done: build\Release\streamer.exe (and streamer-gui.exe, the legacy Win32 GUI)
echo The new cross-platform gui/ target needs -DSTREAMER_GUI=ON, -DVCE_SLANGC=...,
echo and gui/src/os/win32_host.cc (not yet written) before it builds on Windows.
endlocal
