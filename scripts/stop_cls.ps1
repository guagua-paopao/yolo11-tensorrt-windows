[CmdletBinding()]
param(
    [string]$Root = ""
)
$ErrorActionPreference = "Stop"
function Resolve-ProjectRoot {
    param([string]$GivenRoot)
    if ($GivenRoot -and $GivenRoot.Trim().Length -gt 0) { return (Resolve-Path $GivenRoot).Path }
    return (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}
$ProjectRoot = Resolve-ProjectRoot $Root
$pidFile = Join-Path $ProjectRoot "runtime\pids\phase17_cls_services.json"
if (-not (Test-Path $pidFile)) {
    Write-Host "PID file not found: $pidFile" -ForegroundColor Yellow
    return
}
$items = Get-Content $pidFile -Raw | ConvertFrom-Json
foreach ($item in $items) {
    $processId = [int]$item.pid
    try {
        $proc = Get-Process -Id $processId -ErrorAction Stop
        Write-Host "[STOP] $($item.name) pid=$processId" -ForegroundColor Cyan
        Stop-Process -Id $processId -Force -ErrorAction Stop
    } catch {
        Write-Host "[SKIP] $($item.name) pid=$processId not running" -ForegroundColor Yellow
    }
}
