$ErrorActionPreference = 'Stop'

$root   = $PSScriptRoot
$build  = Join-Path $root 'build'
$vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$ninja  = 'C:\PPProgam\ninja_win\bin\ninja.exe'

Write-Host '[1/3] Setting up MSVC x64 environment...' -ForegroundColor Cyan
$envDump = cmd /c "`"$vcvars`" > nul && set"
foreach ($line in $envDump) {
    if ($line -match '^([^=]+)=(.*)$') {
        [System.Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
    }
}

Write-Host '[2/3] Configuring...' -ForegroundColor Cyan
if (-not (Test-Path $build)) { New-Item -ItemType Directory $build | Out-Null }

& cmake -S $root -B $build -G Ninja `
    -DCMAKE_MAKE_PROGRAM="$ninja" `
    -DCMAKE_C_COMPILER=cl `
    -DCMAKE_CXX_COMPILER=cl `
    -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }

Write-Host "[3/3] Building ($env:NUMBER_OF_PROCESSORS cores)..." -ForegroundColor Cyan
& cmake --build $build -j $env:NUMBER_OF_PROCESSORS
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }

Write-Host "`nDone: $build\streamer.exe" -ForegroundColor Green
