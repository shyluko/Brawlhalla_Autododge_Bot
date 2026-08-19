[CmdletBinding()]
param(
    [string]$BinaryPath,
    [string]$SlintDir = $env:SLINT_DIR,
    [string]$OutputDirectory,
    [string]$Version = 'dev'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot

if ([string]::IsNullOrWhiteSpace($BinaryPath)) {
    $candidates = @(Get-ChildItem -Path (Join-Path $projectRoot 'x64\Release') -Filter '*.exe' -File -ErrorAction SilentlyContinue)
    if ($candidates.Count -ne 1) { throw 'Pass -BinaryPath explicitly when the Release output does not contain exactly one executable.' }
    $BinaryPath = $candidates[0].FullName
}
$BinaryPath = (Resolve-Path -LiteralPath $BinaryPath).Path

if ([string]::IsNullOrWhiteSpace($SlintDir)) { $SlintDir = 'C:\Program Files\Slint-cpp 1.17.1' }
$slintDll = Join-Path $SlintDir 'lib\slint_cpp.dll'
if (-not (Test-Path -LiteralPath $slintDll)) { throw "Slint runtime DLL was not found: $slintDll" }

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) { $OutputDirectory = Join-Path $projectRoot "dist\BrawlhallaAutododge-$Version" }
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
$outputRoot = [System.IO.Path]::GetFullPath((Join-Path $projectRoot 'dist'))
$outputPrefix = $outputRoot.TrimEnd([System.IO.Path]::DirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
if (-not $OutputDirectory.StartsWith($outputPrefix, [System.StringComparison]::OrdinalIgnoreCase)) { throw 'OutputDirectory must be inside the repository dist directory.' }

if (Test-Path -LiteralPath $OutputDirectory) { Remove-Item -LiteralPath $OutputDirectory -Recurse -Force }
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null

Copy-Item -LiteralPath $BinaryPath -Destination (Join-Path $OutputDirectory 'BrawlhallaAutododge.exe')
Copy-Item -LiteralPath $slintDll -Destination $OutputDirectory
Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE.txt') -Destination $OutputDirectory
Copy-Item -LiteralPath (Join-Path $projectRoot 'README.md') -Destination (Join-Path $OutputDirectory 'README.md')
New-Item -ItemType Directory -Path (Join-Path $OutputDirectory 'config') | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'config\config.json') -Destination (Join-Path $OutputDirectory 'config\config.json')
Copy-Item -LiteralPath (Join-Path $projectRoot 'data') -Destination (Join-Path $OutputDirectory 'data') -Recurse

$checksum = Get-FileHash -LiteralPath (Join-Path $OutputDirectory 'BrawlhallaAutododge.exe') -Algorithm SHA256
"$($checksum.Hash)  BrawlhallaAutododge.exe" | Set-Content -LiteralPath (Join-Path $OutputDirectory 'SHA256SUMS.txt') -NoNewline

$archive = "${OutputDirectory}.zip"
if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
Compress-Archive -Path $OutputDirectory -DestinationPath $archive -CompressionLevel Optimal
Write-Output "Package created: $archive"
