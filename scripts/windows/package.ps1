# Windows installer packaging -> dist\windows\streamer-setup-<version>.exe
# Thin driver, same scripts/<platform>/ convention as build.bat: runs the
# Release build, stages the exact runtime file set (never a raw copy of
# build\ — see the comment below), then hands the stage off to Inno Setup.
#
# Usage: scripts\windows\package.ps1 [-SkipBuild] [-IsccPath <path>] [-Msys2Root <path>]
param(
    [switch]$SkipBuild,
    [string]$IsccPath,
    [string]$Msys2Root = "C:\msys64"
)

$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..\..")

if (-not $SkipBuild) {
    Write-Host "Building Release..."
    # build.bat picks its toolchain via `where clang++`, so this script must
    # put MSYS2 UCRT64 on PATH itself first — otherwise, on a machine where
    # it isn't already on PATH, build.bat silently falls back to MSVC+vcpkg,
    # which root CMakeLists.txt's `-static -static-libgcc -static-libstdc++`
    # block does NOT apply to (it's gated `if(NOT MSVC)`). Packaging must
    # always take the Clang/UCRT64 path so the "no MinGW DLLs to ship" and
    # "no console flash" properties this installer relies on actually hold.
    $Ucrt64Bin = Join-Path $Msys2Root "ucrt64\bin"
    if ($env:PATH -notlike "*$Ucrt64Bin*") {
        $env:PATH = "$Ucrt64Bin;$env:PATH"
    }
    if (-not (Get-Command clang++.exe -ErrorAction SilentlyContinue)) {
        throw "MSYS2 UCRT64 Clang not found at '$Ucrt64Bin'. Install MSYS2 (https://www.msys2.org/), then from an MSYS2 UCRT64 shell run: pacman -S mingw-w64-ucrt-x86_64-clang mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja . Or pass -Msys2Root if MSYS2 is installed somewhere other than '$Msys2Root'."
    }
    & (Join-Path $PSScriptRoot "build.bat")
    if ($LASTEXITCODE -ne 0) { throw "Release build failed." }
}

$BuildDir = "build"
if (-not (Test-Path (Join-Path $BuildDir "streamer.exe"))) {
    throw "$BuildDir\streamer.exe not found. Run without -SkipBuild, or run scripts\windows\build.bat first."
}
if (-not (Test-Path (Join-Path $BuildDir "gui\streamer_gui.exe"))) {
    throw "$BuildDir\gui\streamer_gui.exe not found. Run without -SkipBuild, or run scripts\windows\build.bat first."
}

$Version = (Get-Content "VERSION" -Raw).Trim()
Write-Host "Packaging Streamer v$Version..."

# Explicit allowlist copy into a scratch directory — deliberately never a
# raw copy of build\. streamer.exe/streamer_gui.exe don't drop dev-only
# scrap next to themselves the way Matrix Player's exe does (config and
# library data both live under %APPDATA%/the user's download_dir, never
# next to the exe), but the allowlist is kept anyway: it's the only thing
# that keeps this script and streamer.iss's [Files] section honest about
# exactly what ships.
$StageDir = "dist\windows\stage"
if (Test-Path $StageDir) { Remove-Item -Recurse -Force $StageDir }
New-Item -ItemType Directory -Force -Path "$StageDir\gui" | Out-Null

Copy-Item (Join-Path $BuildDir "streamer.exe") (Join-Path $StageDir "streamer.exe")
Copy-Item (Join-Path $BuildDir "gui\streamer_gui.exe") (Join-Path $StageDir "gui\streamer_gui.exe")

$AssetsSrc = Join-Path $BuildDir "gui\assets"
if (-not (Test-Path $AssetsSrc)) { throw "Expected build output missing: $AssetsSrc" }
Copy-Item $AssetsSrc (Join-Path $StageDir "gui\assets") -Recurse
Write-Host "Staged streamer.exe, gui\streamer_gui.exe, and gui\assets\ into $StageDir"

if (-not $IsccPath) {
    # winget's default scope varies by machine (some install per-user under
    # LOCALAPPDATA\Programs, some per-machine under Program Files) — check
    # all three rather than assume one.
    $candidates = @(
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "${env:ProgramFiles}\Inno Setup 6\ISCC.exe"
    )
    $IsccPath = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $IsccPath) {
        $cmd = Get-Command iscc.exe -ErrorAction SilentlyContinue
        if ($cmd) { $IsccPath = $cmd.Source }
    }
}
if (-not $IsccPath -or -not (Test-Path $IsccPath)) {
    throw "Inno Setup's ISCC.exe not found. Install it (winget install --id JRSoftware.InnoSetup -e, or https://jrsoftware.org/isdl.php), or pass -IsccPath explicitly."
}

$OutDir = "dist\windows"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$StageDirAbs = (Resolve-Path $StageDir).Path
$OutDirAbs = (Resolve-Path $OutDir).Path

Write-Host "Compiling installer with Inno Setup..."
& $IsccPath "/DMyAppVersion=$Version" "/DMyStageDir=$StageDirAbs" "/DMyOutDir=$OutDirAbs" "packaging\windows\streamer.iss"
if ($LASTEXITCODE -ne 0) { throw "Inno Setup compilation failed." }

Write-Host ""
Write-Host "Success! Output: $OutDir\streamer-setup-$Version.exe"
