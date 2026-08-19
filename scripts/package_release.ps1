# Brawlhalla Autododge
# Copyright (c) 2026 Shyluko
# Open-source release package helper.
# This project is shared freely, provided as-is, and may not receive regular maintenance.
# Public builds must remain plain, unprotected, and free of external recruiting or social links.

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$releaseDir = Join-Path $root 'release'
$binDir = Join-Path $root 'bin'
$sourceExe = Join-Path $binDir 'brawlhalla_autododge.exe'
$sourceDebug = Join-Path $binDir 'brawlhalla_autododge.exe.debug'

if (-not (Test-Path $sourceExe)) {
    throw "Build output not found: $sourceExe"
}

if (Test-Path $releaseDir) {
    Remove-Item $releaseDir -Recurse -Force
}

New-Item -ItemType Directory -Path $releaseDir | Out-Null
Copy-Item $sourceExe $releaseDir

if (Test-Path $sourceDebug) {
    Copy-Item $sourceDebug (Join-Path $releaseDir 'brawlhalla_autododge.exe.debug') -Force
}

foreach ($folder in @('config', 'data')) {
    $src = Join-Path $root $folder
    if (Test-Path $src) {
        Copy-Item $src $releaseDir -Recurse -Force
    }
}

$mingwBin = $null
$gxxCmd = Get-Command g++.exe -ErrorAction SilentlyContinue
if ($gxxCmd) {
    $mingwBin = Split-Path -Parent $gxxCmd.Source
}
if (-not $mingwBin) {
    throw 'MinGW toolchain not found on PATH. Add your MinGW bin directory to PATH before packaging the release.'
}

foreach ($dll in @('libstdc++-6.dll', 'libgcc_s_seh-1.dll', 'libwinpthread-1.dll')) {
    $srcDll = Join-Path $mingwBin $dll
    if (Test-Path $srcDll) {
        Copy-Item $srcDll $releaseDir -Force
    }
}

Copy-Item (Join-Path $root 'LICENSE.txt') (Join-Path $releaseDir 'LICENSE.txt') -Force
Copy-Item (Join-Path $root 'README.md') (Join-Path $releaseDir 'README.txt') -Force

$uploadChecklist = @'
Brawlhalla Autododge - Upload Checklist

- This is a release build, not a debug build.
- Provide the debug-symbol file alongside the executable: brawlhalla_autododge.exe.debug
- Do not pack, obfuscate, or password-protect the archive.
- Keep the public release free of machine-specific paths or private environment details.
- Include your forum thread URL in the upload form.
- Keep the config/ and data/ folders next to the EXE.
- Do not include external community, Discord, Telegram, or recruiting links in the archive.
- Do not upload a protected or packed binary without approval and the required protection note.
- This project is shared as-is and may not receive regular maintenance.
- Keep the release package plain, staged, and upload-safe.
'@
Set-Content -Path (Join-Path $releaseDir 'README-UPLOAD.txt') -Value $uploadChecklist

Write-Host "Release package ready: $releaseDir"
Write-Host "Brawlhalla Autododge is open-source and distributed by Shyluko."
Write-Host "This build is provided as-is and may not receive regular maintenance."
Write-Host "Upload checklist generated: $releaseDir\README-UPLOAD.txt"
Write-Host "Symbol output included: $releaseDir\brawlhalla_autododge.exe.debug"
