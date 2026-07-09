[CmdletBinding()]
param(
    [string]$PidFile = ".\runtime\pids\phase18_seg_services.json",
    [switch]$KillByPort,
    [switch]$KillByName
)

$ErrorActionPreference = "Continue"
Set-Location (Resolve-Path (Join-Path $PSScriptRoot ".."))

function Stop-ById {
    param([int]$ProcessId, [string]$DisplayName = "")
    $proc = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
    if ($null -eq $proc) { Write-Host "[SKIP] $DisplayName pid=$ProcessId not running"; return }
    Write-Host "[STOP] $DisplayName pid=$ProcessId process=$($proc.ProcessName)"
    Stop-Process -Id $ProcessId -Force -ErrorAction SilentlyContinue
}

if (Test-Path $PidFile) {
    $payload = Get-Content $PidFile -Raw | ConvertFrom-Json
    foreach ($item in @($payload)) { if ($null -ne $item.pid) { Stop-ById -ProcessId ([int]$item.pid) -DisplayName ([string]$item.name) } }
} else {
    Write-Warning "PID file not found: $PidFile"
}

if ($KillByName) {
    Get-Process -Name yolo11_server,yolo11_worker -ErrorAction SilentlyContinue | ForEach-Object {
        Write-Host "[STOP-BY-NAME] $($_.ProcessName) pid=$($_.Id)"
        Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
    }
}

if ($KillByPort) {
    $lines = netstat -ano | Select-String ":8086\s+.*LISTENING\s+(\d+)"
    foreach ($line in $lines) {
        if ([string]$line -match "LISTENING\s+(\d+)") { Stop-ById -ProcessId ([int]$Matches[1]) -DisplayName "port_8086" }
    }
}

Write-Host "SEG stop request completed."
