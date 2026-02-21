param(
    [string]$Version = "0.1.0",
    [string]$BuildDir = "build-mingw",
    [string]$QtDir = "C:\Qt\6.10.2\mingw_64",
    [string]$OutDir = "dist",
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"

$packageName = "ImgCompare-v$Version-win64"
$stageDir = Join-Path $OutDir $packageName
$zipPath = Join-Path $OutDir ($packageName + ".zip")
$exePath = Join-Path $BuildDir "ImgCompare.exe"

Write-Host "==> Build and deploy runtime"
& .\build.ps1 -BuildDir $BuildDir -QtDir $QtDir -Config $Config -Deploy

if (-not (Test-Path $exePath)) {
    throw "Executable not found: $exePath"
}

if (Test-Path $stageDir) {
    Remove-Item $stageDir -Recurse -Force
}
if (-not (Test-Path $OutDir)) {
    New-Item -ItemType Directory -Path $OutDir | Out-Null
}
if (Test-Path $zipPath) {
    Remove-Item $zipPath -Force
}

New-Item -ItemType Directory -Path $stageDir | Out-Null

# Copy required top-level runtime files
$topLevelPatterns = @(
    "ImgCompare.exe",
    "*.dll",
    "qt.conf",
    "opengl32sw.dll",
    "D3Dcompiler_47.dll"
)

foreach ($pattern in $topLevelPatterns) {
    Get-ChildItem -Path $BuildDir -Filter $pattern -File -ErrorAction SilentlyContinue |
        ForEach-Object { Copy-Item $_.FullName -Destination $stageDir -Force }
}

# Copy Qt plugin/runtime folders if present
$runtimeDirs = @(
    "platforms",
    "imageformats",
    "iconengines",
    "styles",
    "tls",
    "generic",
    "networkinformation",
    "translations"
)

foreach ($dir in $runtimeDirs) {
    $src = Join-Path $BuildDir $dir
    if (Test-Path $src) {
        Copy-Item $src -Destination $stageDir -Recurse -Force
    }
}

Write-Host "==> Create zip: $zipPath"
Compress-Archive -Path (Join-Path $stageDir "*") -DestinationPath $zipPath -Force

Write-Host "Done."
Write-Host "Folder: $stageDir"
Write-Host "Zip:    $zipPath"
