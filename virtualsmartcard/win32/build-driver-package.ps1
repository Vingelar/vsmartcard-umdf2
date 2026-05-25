param(
    [string]$Configuration = "Release",
    [string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$release = Join-Path $root "BixVReader\x64\Release"
$kit = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0"
$pfx = Join-Path $root "TestCert\BixVReader.pfx"
$msbuild = "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"

Write-Host "Building BixVReader..."
& $msbuild (Join-Path $root "BixVReader\BixVReader.vcxproj") `
    /p:Configuration=$Configuration /p:Platform=$Platform /p:VisualStudioVersion=17.0 /t:Build /m /nologo /v:minimal

Write-Host "Writing ASCII INF..."
$infPath = Join-Path $release "BixVReader.inf"
$srcInf = Join-Path $root "BixVReader\BixVReader.inf"
$lines = [System.IO.File]::ReadAllLines($srcInf)
$out = New-Object System.Collections.Generic.List[string]
foreach ($line in $lines) {
    if ($line -match '^\s*DriverVer\s*=') {
        $out.Add('DriverVer = 05/25/2026,1.0.0.0')
    }
    else {
        $out.Add($line)
    }
}
$text = ($out -join "`r`n") + "`r`n"
[System.IO.File]::WriteAllText($infPath, $text, [System.Text.Encoding]::ASCII)

Write-Host "Running Inf2Cat and signing..."
Set-Location $release
& "$kit\x64\signtool.exe" sign /fd SHA256 /f $pfx /p BixVReader! /v BixVReader.dll | Out-Null
& "$kit\x86\Inf2Cat.exe" /driver:. /os:10_X64,ServerRS5_X64,ServerFE_X64 /verbose
& "$kit\x64\signtool.exe" sign /fd SHA256 /f $pfx /p BixVReader! /v bixvreader.cat | Out-Null

Write-Host "Building MSI..."
Set-Location $root
& $msbuild (Join-Path $root "BixVReaderInstaller\BixVReaderInstaller.wixproj") `
    /p:Configuration=$Configuration /p:Platform=$Platform /p:VisualStudioVersion=17.0 `
    /p:BuildProjectReferences=false /m /nologo /v:minimal

# WiX packages the Release folder; rewrite ASCII INF after any dependency rebuild.
Write-Host "Finalizing ASCII INF for installer..."
$text = ($out -join "`r`n") + "`r`n"
[System.IO.File]::WriteAllText($infPath, $text, [System.Text.Encoding]::ASCII)
Set-Location $release
Remove-Item "bixvreader_ascii.inf" -ErrorAction SilentlyContinue
& "$kit\x86\Inf2Cat.exe" /driver:. /os:10_X64,ServerRS5_X64,ServerFE_X64 /verbose | Out-Null
& "$kit\x64\signtool.exe" sign /fd SHA256 /f $pfx /p BixVReader! /v bixvreader.cat | Out-Null

Set-Location $root
& $msbuild (Join-Path $root "BixVReaderInstaller\BixVReaderInstaller.wixproj") `
    /p:Configuration=$Configuration /p:Platform=$Platform /p:VisualStudioVersion=17.0 `
    /p:BuildProjectReferences=false /t:Rebuild /m /nologo /v:minimal

Copy-Item (Join-Path $root "BixVReaderInstaller\bin\x64\Release\BixVReaderInstaller.msi") `
    (Join-Path $root "BixVReaderInstaller.msi") -Force
Write-Host "Done: $(Join-Path $root 'BixVReaderInstaller.msi')"
