#requires -version 5.1
<#
.SYNOPSIS
  Build the Remix bridge: the x64 server (.trex\NvRemixBridge.exe) and the x86 client (d3d9.dll).

.DESCRIPTION
  bridge\build_bridge_*.bat loads the compiler with `cmd /c "vcvarsall.bat ..."` after Push-Location.
  In shells where cmd does not search the current directory (NoDefaultCurrentDirectoryInExePath=1),
  that call fails silently, the build runs with whatever compiler is already loaded, and the "x86" pass
  builds a second x64 server instead of the client. This loads vcvarsall by full path for each
  architecture and then runs bridge\build_bridge.ps1's Build for it.

  Output: bridge\_output\d3d9.dll (client, the game's d3d9_remix.dll) and
          bridge\_output\.trex\NvRemixBridge.exe (server).

.EXAMPLE
  .\scripts\build_bridge.ps1
  .\scripts\build_bridge.ps1 -Flavor debugoptimized
#>
[CmdletBinding()]
param(
    [ValidateSet('release','debug','debugoptimized')]
    [string]$Flavor = 'release'
)

$ErrorActionPreference = 'Stop'

$bridgeDir = (Resolve-Path (Join-Path $PSScriptRoot '..\bridge')).Path
$vsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = & $vsWhere -latest -version '[16.0,18.0)' -property installationPath
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvarsall.bat'
if (-not (Test-Path $vcvars)) { throw "vcvarsall.bat not found at '$vcvars'." }

$flavorTag = (Get-Culture).TextInfo.ToTitleCase($Flavor)
foreach ($platform in 'x64', 'x86') {
    $subDir = "_comp${flavorTag}_$platform"
    Write-Host "=== bridge $Flavor $platform -> $subDir ===" -ForegroundColor Cyan
    $ps = "& { Set-Location '$bridgeDir'; . '$bridgeDir\build_bridge.ps1'; Build -Platform $platform -BuildFlavour $Flavor -BuildSubDir $subDir; exit `$LastExitCode }"
    $prevEAP = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        cmd.exe /c "`"$vcvars`" $platform >nul && cd /d `"$bridgeDir`" && powershell -NoProfile -ExecutionPolicy Bypass -Command `"$ps`""
        $exit = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $prevEAP
    }
    if ($exit -ne 0) { throw "bridge $platform build failed (exit $exit)." }
}

foreach ($rel in '_output\d3d9.dll', '_output\.trex\NvRemixBridge.exe') {
    $abs = Join-Path $bridgeDir $rel
    if (-not (Test-Path $abs)) { throw "Build reported success but $rel is missing." }
    $info = Get-Item $abs
    Write-Host ("  {0:yyyy-MM-dd HH:mm}  {1,12:N0} bytes  bridge\{2}" -f $info.LastWriteTime, $info.Length, $rel) -ForegroundColor Green
}
