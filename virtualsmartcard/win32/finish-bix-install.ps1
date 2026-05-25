#Requires -RunAsAdministrator
$ErrorActionPreference = "Continue"
$dir = Split-Path -Parent $MyInvocation.MyCommand.Path
$cer = Join-Path $dir "BixVReader.cer"
$msi = Join-Path $dir "BixVReaderInstaller.msi"
$log = Join-Path $dir "install-final.log"

Write-Host "Removing orphan ROOT\SMARTCARDREADER devices..."
for ($i = 1; $i -le 20; $i++) {
    pnputil /remove-device ("ROOT\SMARTCARDREADER\{0:D4}" -f $i) 2>$null | Out-Null
}

Write-Host "Importing test certificate..."
certutil -addstore -f Root $cer | Out-Null
certutil -addstore -f TrustedPublisher $cer | Out-Null

Write-Host "Installing MSI..."
$p = Start-Process msiexec.exe -ArgumentList "/i `"$msi`" /qn /norestart /L* `"$log`"" -Wait -PassThru
Write-Host "msiexec exit code: $($p.ExitCode)"

Get-PnpDevice -ErrorAction SilentlyContinue |
    Where-Object { $_.InstanceId -like '*BixVirtual*' -or $_.FriendlyName -like '*Bix Virtual*' } |
    Format-Table Status, FriendlyName, InstanceId -AutoSize

if ($p.ExitCode -ne 0 -and (Test-Path $log)) {
    Write-Host "--- install log errors ---"
    Select-String -Path $log -Pattern "Error|failed|1603|Return value 3" | Select-Object -Last 8 | ForEach-Object { Write-Host $_.Line }
}

exit $p.ExitCode
