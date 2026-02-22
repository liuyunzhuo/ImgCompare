param(
    [string]$BuildDir = "build-mingw",
    [string]$QtDir = "C:\Qt\6.10.2\mingw_64",
    [string]$Generator = "MinGW Makefiles",
    [string]$Config = "Release",
    [switch]$Clean,
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
$qtHome = Split-Path -Parent $qtRoot

$candidateMingwDirs = @(
    (Join-Path $qtHome "Tools\mingw1310_64\bin"),
    (Join-Path $qtHome "Tools\mingw1120_64\bin"),
    (Join-Path $qtHome "Tools\mingw1100_64\bin"),
    (Join-Path $qtHome "Tools\mingw900_64\bin")
)
$mingwDir = $candidateMingwDirs | Where-Object { Test-Path (Join-Path $_ "g++.exe") } | Select-Object -First 1
if (-not $mingwDir) {
    # Fallback: scan C:\Qt\Tools\mingw*_64\bin
    $toolsRoot = Join-Path $qtHome "Tools"
    if (Test-Path $toolsRoot) {
        $found = Get-ChildItem -Path $toolsRoot -Directory -Filter "mingw*_64" -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending |
            ForEach-Object { Join-Path $_.FullName "bin" } |
            Where-Object { Test-Path (Join-Path $_ "g++.exe") } |
            Select-Object -First 1
        $mingwDir = $found
    }
}
Add-ToPathIfExists $mingwDir

$gccPath = Join-Path $mingwDir "gcc.exe"
$gxxPath = Join-Path $mingwDir "g++.exe"
$makePath = Join-Path $mingwDir "mingw32-make.exe"

if (-not (Test-Path $gccPath) -or -not (Test-Path $gxxPath) -or -not (Test-Path $makePath)) {
    throw "Qt MinGW toolchain not found under: $mingwDir"
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "cmake not found in PATH. Please install CMake and reopen terminal."
}

$cachePath = Join-Path $BuildDir "CMakeCache.txt"
if ($Clean -and (Test-Path $BuildDir)) {
    Remove-Item $BuildDir -Recurse -Force
}

# If cache exists but points to a different compiler, wipe it to avoid ABI mismatch.
if (Test-Path $cachePath) {
    $cacheText = Get-Content $cachePath -Raw
    if (($cacheText -notmatch [Regex]::Escape($gccPath)) -or ($cacheText -notmatch [Regex]::Escape($gxxPath))) {
        Write-Host "==> Detected compiler mismatch in cache, cleaning $BuildDir"
        Remove-Item $BuildDir -Recurse -Force
    }
}

$exePath = Join-Path $BuildDir "ImgCompare.exe"
if (Test-Path $exePath) {
    Get-Process ImgCompare -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
}

Write-Host "==> Configure ($BuildDir)"
cmake -S . -B $BuildDir -G $Generator `
    -DCMAKE_PREFIX_PATH="$QtDir" `
    -DCMAKE_C_COMPILER="$gccPath" `
    -DCMAKE_CXX_COMPILER="$gxxPath" `
    -DCMAKE_MAKE_PROGRAM="$makePath"
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed with exit code $LASTEXITCODE"
}

Write-Host "==> Build ($Config)"
cmake --build $BuildDir --config $Config -j
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed with exit code $LASTEXITCODE"
}

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
    if ($LASTEXITCODE -ne 0) {
        throw "windeployqt failed with exit code $LASTEXITCODE"
    }
}

Write-Host "Done."
