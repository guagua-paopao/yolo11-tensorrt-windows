$ErrorActionPreference = "Continue"
$Root = (Resolve-Path (Join-Path $PSScriptRoot ".." )).Path
$PidFile = Join-Path $Root "runtime\pids\phase17_5_pose_services.json"

if (!(Test-Path $PidFile)) {
    Write-Host "No POSE PID file found: $PidFile"
    return
}

$items = Get-Content $PidFile -Raw | ConvertFrom-Json
foreach ($item in $items) {
    $processId = [int]$item.pid
    $name = [string]$item.name
    $p = Get-Process -Id $processId -ErrorAction SilentlyContinue
    if ($null -ne $p) {
        Write-Host "[STOP] $name pid=$processId"
        Stop-Process -Id $processId -Force -ErrorAction SilentlyContinue
    }
    else {
        Write-Host "[SKIP] $name pid=$processId not running"
    }
}
Remove-Item $PidFile -Force -ErrorAction SilentlyContinue
