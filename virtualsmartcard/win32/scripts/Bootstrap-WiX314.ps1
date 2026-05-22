<#
.SYNOPSIS
Downloads and extracts the portable WiX 3.14 toolset to .tools\wix314.

.DESCRIPTION
The DevMsi.vcxproj and BixVReaderInstaller.wixproj projects in the BixVReader
solution depend on WiX 3.x. The official WiX 3.14 MSI installer is no longer
maintained against modern Windows / Visual Studio versions, so this script
fetches the portable `wix314-binaries.zip` archive from the WiX Toolset
GitHub release and extracts it next to the solution. The build then locates
this portable toolset via Directory.Build.props.

Idempotent: skips download/extract if the toolset is already present.

.PARAMETER Url
Optional override of the WiX 3.14 binaries download URL.

.PARAMETER Force
Re-download and re-extract even if the toolset is already present.

.EXAMPLE
PS> .\Bootstrap-WiX314.ps1
#>

[CmdletBinding()]
param(
    [string]$Url    = 'https://github.com/wixtoolset/wix3/releases/download/wix3141rtm/wix314-binaries.zip',
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

$win32Root = (Resolve-Path "$PSScriptRoot\..").Path
$toolsDir  = Join-Path $win32Root '.tools'
$wixDir    = Join-Path $toolsDir  'wix314'
$marker    = Join-Path $wixDir    'wix.targets'

if ((Test-Path $marker) -and -not $Force) {
    Write-Host "Bootstrap-WiX314: $wixDir already populated (wix.targets present). Nothing to do."
    exit 0
}

New-Item -ItemType Directory -Path $toolsDir -Force | Out-Null
$zipPath = Join-Path $toolsDir 'wix314-binaries.zip'

Write-Host "Bootstrap-WiX314: downloading $Url ..."
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 -bor `
    [Net.SecurityProtocolType]::Tls13
Invoke-WebRequest -Uri $Url -OutFile $zipPath -UseBasicParsing
Write-Host ("Bootstrap-WiX314: downloaded {0:N0} bytes -> {1}" -f (Get-Item $zipPath).Length, $zipPath)

if (Test-Path $wixDir) {
    Write-Host "Bootstrap-WiX314: removing stale $wixDir ..."
    Remove-Item -Recurse -Force $wixDir
}
New-Item -ItemType Directory -Path $wixDir -Force | Out-Null

Write-Host "Bootstrap-WiX314: extracting to $wixDir ..."
Expand-Archive -Path $zipPath -DestinationPath $wixDir -Force

if (-not (Test-Path $marker)) {
    throw "Bootstrap-WiX314: extraction completed but '$marker' is missing. Was the archive layout changed?"
}

Remove-Item $zipPath -Force

Write-Host "Bootstrap-WiX314: done."
Write-Host "  WIX                = $wixDir\"
Write-Host "  WixTargetsPath     = $marker"
