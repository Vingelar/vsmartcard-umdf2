# Start the virtual smart card emulator (vpicc) for BixVReader on Windows.
# BixVReader (WUDFHost) already listens on TCP port 35963; vpicc connects to it.
param(
    [string]$Type = "iso7816",
    [int]$Port = 35963,
    [string]$HostName = "localhost"
)

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$env:PYTHONPATH = Join-Path (Split-Path -Parent $root) "src\vpicc"
$vicc = Join-Path $env:PYTHONPATH "vicc.in"

if (-not (Test-Path $vicc)) {
    throw "vicc.in not found at $vicc"
}

Write-Host "Starting vpicc ($Type) -> ${HostName}:$Port"
Write-Host "Default PIN: 1234"
Write-Host "Press Ctrl+C to stop."

py -3 $vicc -t $Type -H $HostName -P $Port -v -v -v
