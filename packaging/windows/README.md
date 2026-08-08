# Windows packaging

Builds the installer produced by `scripts\windows\package.ps1`. Mirrors
`packaging/arch/`'s role for Linux.

## One-time setup

Install Inno Setup 6 (free): `winget install --id JRSoftware.InnoSetup -e`,
or download from https://jrsoftware.org/isdl.php.

## Build

```bat
scripts\windows\package.ps1
```

Runs `scripts\windows\build.ps1` (Ninja + MSYS2 UCRT64 Clang, or MSVC+vcpkg
as a fallback), stages the exact runtime file set into a scratch directory,
and compiles `streamer.iss` into `dist\windows\streamer-setup-<version>.exe`.
The version comes from the repo-root `VERSION` file — bump it there before
packaging a new release.

## AppId

`streamer.iss`'s `AppId` is a fixed GUID, generated once, and must never
change — it's what makes running a newer installer over an older install
upgrade in place instead of installing side-by-side. Everything else in the
`[Setup]` section can change freely between releases.

## What gets installed, and what doesn't

`streamer.exe` (the CLI) and `gui\streamer_gui.exe` (the GUI) plus
`gui\assets\` (compiled shaders + fonts) — exactly what `scripts\windows\
build.ps1` produces in `build\` and `build\gui\`. Both are fully statically
linked (root `CMakeLists.txt`'s `-static -static-libgcc -static-libstdc++`
block), so no MinGW runtime DLLs need shipping. `%APPDATA%\streamer\
config.toml` and the user's chosen `download_dir` (with its `.streamer\
library.db`) live entirely outside the install directory, are never listed
in `streamer.iss`'s `[Files]` section, and are therefore never touched by
install OR uninstall.
