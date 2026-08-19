[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [ValidateSet('x64')]
    [string]$Platform = 'x64',
    [string]$SlintDir = $env:SLINT_DIR
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$solution = Join-Path $projectRoot 'brawlhalla autododge.sln'

if (-not (Test-Path -LiteralPath $solution)) { throw "Solution was not found: $solution" }
if ([string]::IsNullOrWhiteSpace($SlintDir)) { $SlintDir = 'C:\Program Files\Slint-cpp 1.17.1' }
New-Item -ItemType Directory -Path (Join-Path $projectRoot 'src\generated') -Force | Out-Null

$compiler = Join-Path $SlintDir 'bin\slint-compiler.exe'
$library = Join-Path $SlintDir 'lib\slint_cpp.dll.lib'
if (-not (Test-Path -LiteralPath $compiler) -or -not (Test-Path -LiteralPath $library)) {
    throw "Slint C++ 1.17.1 was not found at '$SlintDir'. Set SLINT_DIR or pass -SlintDir."
}

$msbuild = (Get-Command msbuild.exe -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Source)
if ([string]::IsNullOrWhiteSpace($msbuild)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\Current\Bin\MSBuild.exe' | Select-Object -First 1
    }
}
if ([string]::IsNullOrWhiteSpace($msbuild) -or -not (Test-Path -LiteralPath $msbuild)) { throw 'MSBuild was not found in the installed Visual Studio Build Tools.' }

& $msbuild $solution /m "/p:Configuration=$Configuration" "/p:Platform=$Platform" "/p:SlintDir=$SlintDir"
if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE." }
