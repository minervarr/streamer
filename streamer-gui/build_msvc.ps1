$ErrorActionPreference = 'Stop'

$src    = $PSScriptRoot
$build  = Join-Path $src 'build'
$vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$ninja  = 'C:\PPProgam\ninja_win\bin\ninja.exe'

Write-Host '[1/4] Building streamer (Rust)...' -ForegroundColor Cyan
& cargo build --release --manifest-path (Join-Path $src '..\Cargo.toml')
if ($LASTEXITCODE -ne 0) { throw 'Cargo build failed' }

Write-Host '[2/4] Setting up MSVC x64 environment...' -ForegroundColor Cyan
$envDump = cmd /c "`"$vcvars`" > nul && set"
foreach ($line in $envDump) {
    if ($line -match '^([^=]+)=(.*)$') {
        [System.Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
    }
}

Write-Host '[3/4] Configuring streamer-gui...' -ForegroundColor Cyan
if (-not (Test-Path $build)) { New-Item -ItemType Directory $build | Out-Null }

& cmake -S $src -B $build -G Ninja -DCMAKE_MAKE_PROGRAM="$ninja" -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }

Write-Host "[4/4] Building ($env:NUMBER_OF_PROCESSORS cores)..." -ForegroundColor Cyan
& cmake --build $build -j $env:NUMBER_OF_PROCESSORS
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }

Write-Host "`nDone: $build\streamer-gui.exe" -ForegroundColor Green
