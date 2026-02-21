param(
    [string]$BuildDir = "build-mingw",
    [string]$QtDir = "C:\Qt\6.10.2\mingw_64",
    [string]$Generator = "MinGW Makefiles",
    [string]$Config = "Release",
    [switch]$Deploy
)

$ErrorActionPreference = "Stop"

function Add-ToPathIfExists([string]$PathToAdd) {
    if (Test-Path $PathToAdd) {
        if (-not ($env:Path -split ";" | Where-Object { $_ -eq $PathToAdd })) {
            $env:Path = "$PathToAdd;$env:Path"
        }
    }
}

if (-not (Test-Path $QtDir)) {
    throw "QtDir not found: $QtDir"
}

Add-ToPathIfExists (Join-Path $QtDir "bin")

# Common Qt MinGW toolchain location
$qtRoot = Split-Path -Parent $QtDir
$mingwDir = Join-Path $qtRoot "Tools\mingw1310_64\bin"
Add-ToPathIfExists $mingwDir

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "cmake not found in PATH. Please install CMake and reopen terminal."
}

$exePath = Join-Path $BuildDir "ImgCompare.exe"
if (Test-Path $exePath) {
    Get-Process ImgCompare -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
}

Write-Host "==> Configure ($BuildDir)"
cmake -S . -B $BuildDir -G $Generator -DCMAKE_PREFIX_PATH="$QtDir"

Write-Host "==> Build ($Config)"
cmake --build $BuildDir --config $Config -j

if ($Deploy) {
    $windeployqt = Join-Path $QtDir "bin\windeployqt.exe"
    if (-not (Test-Path $windeployqt)) {
        throw "windeployqt not found: $windeployqt"
    }
    if (-not (Test-Path $exePath)) {
        throw "Exe not found for deploy: $exePath"
    }
    Write-Host "==> Deploy Qt runtime"
    & $windeployqt $exePath
}

Write-Host "Done."
