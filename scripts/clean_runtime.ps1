<#
Safe local runtime cleaner. It does NOT delete Redis keys.
Run after stopping all services:
  powershell -ExecutionPolicy Bypass -File .\scripts\clean_runtime.ps1 -WhatIf
  powershell -ExecutionPolicy Bypass -File .\scripts\clean_runtime.ps1 -ConfirmDelete
#>
[CmdletBinding()]
param(
    [string]$Root = "",
    [switch]$ConfirmDelete,
    [switch]$KeepLogs
)

$ErrorActionPreference = "Stop"

function Resolve-ProjectRoot {
    param([string]$GivenRoot)
    if ($GivenRoot -and $GivenRoot.Trim().Length -gt 0) {
        return (Resolve-Path $GivenRoot).Path
    }
    return (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

$ProjectRoot = Resolve-ProjectRoot $Root
Set-Location $ProjectRoot

$targets = @(
    "runtime\input",
    "runtime\output",
    "runtime\pids",
    "reports"
)
if (-not $KeepLogs) {
    $targets += "runtime\logs"
}

if (-not $ConfirmDelete) {
    Write-Host "Dry run only. Add -ConfirmDelete to delete these local folders:" -ForegroundColor Yellow
    foreach ($t in $targets) { Write-Host "  $t" }
    exit 0
}

foreach ($t in $targets) {
    $p = Join-Path $ProjectRoot $t
    if (Test-Path $p) {
        Write-Host "Deleting $p" -ForegroundColor Yellow
        Remove-Item $p -Recurse -Force
    }
}

New-Item -ItemType Directory -Force -Path `
    (Join-Path $ProjectRoot "runtime\input"), `
    (Join-Path $ProjectRoot "runtime\output"), `
    (Join-Path $ProjectRoot "runtime\logs"), `
    (Join-Path $ProjectRoot "reports") | Out-Null

Write-Host "Runtime local cleanup completed." -ForegroundColor Green
