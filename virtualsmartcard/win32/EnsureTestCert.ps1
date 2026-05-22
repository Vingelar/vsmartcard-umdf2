<#
.SYNOPSIS
Creates a self-signed code-signing certificate for test-signing the BixVReader
driver, exports it as PFX (for signtool) and as DER-encoded CER (for
install_vpcd.bat to import into Trusted Root / Trusted Publisher).

Idempotent: skips generation if the PFX already exists.
#>

param(
    [Parameter(Mandatory = $true)][string]$PfxPath,
    [Parameter(Mandatory = $true)][string]$CerPath,
    [Parameter(Mandatory = $true)][string]$Subject,
    [Parameter(Mandatory = $true)][string]$Password
)

$ErrorActionPreference = 'Stop'

if ((Test-Path -LiteralPath $PfxPath) -and (Test-Path -LiteralPath $CerPath)) {
    Write-Host "EnsureTestCert: PFX and CER already exist at '$PfxPath' / '$CerPath' - nothing to do."
    exit 0
}

foreach ($p in @($PfxPath, $CerPath)) {
    $dir = Split-Path -Parent $p
    if ($dir -and -not (Test-Path -LiteralPath $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }
}

Write-Host "EnsureTestCert: Generating self-signed code-signing cert for '$Subject'..."

$cert = New-SelfSignedCertificate `
    -Subject "CN=$Subject" `
    -Type CodeSigningCert `
    -KeyUsage DigitalSignature `
    -KeyExportPolicy Exportable `
    -CertStoreLocation 'Cert:\CurrentUser\My' `
    -NotAfter (Get-Date).AddYears(10) `
    -KeyAlgorithm RSA `
    -KeyLength 2048 `
    -HashAlgorithm SHA256 `
    -FriendlyName "$Subject"

$securePwd = ConvertTo-SecureString -String $Password -AsPlainText -Force
Export-PfxCertificate -Cert $cert -FilePath $PfxPath -Password $securePwd -Force | Out-Null
Export-Certificate -Cert $cert -FilePath $CerPath -Type CERT -Force | Out-Null

Write-Host "EnsureTestCert: Wrote PFX -> $PfxPath"
Write-Host "EnsureTestCert: Wrote CER -> $CerPath"
Write-Host "EnsureTestCert: Thumbprint $($cert.Thumbprint)"
