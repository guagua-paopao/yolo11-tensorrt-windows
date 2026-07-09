<#!
Phase 17 launcher for CLS service only.
Run from project root:
  powershell -ExecutionPolicy Bypass -File .\scripts\start_cls.ps1 -ExeDir .\out\build\x64-Debug
#>
[CmdletBinding()]
param(
    [string]$Root = "",
    [string]$ExeDir = "",
    [int]$StartupDelaySeconds = 2
)

$ErrorActionPreference = "Stop"

function Resolve-ProjectRoot {
    param([string]$GivenRoot)
    if ($GivenRoot -and $GivenRoot.Trim().Length -gt 0) { return (Resolve-Path $GivenRoot).Path }
    return (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

$ProjectRoot = Resolve-ProjectRoot $Root
Set-Location $ProjectRoot

$PidDir = Join-Path $ProjectRoot "runtime\pids"
$ProcLogDir = Join-Path $ProjectRoot "runtime\logs\phase17_cls_process"
New-Item -ItemType Directory -Force -Path $PidDir, $ProcLogDir | Out-Null

function Resolve-Exe {
    param([string]$ExeName)
    $candidates = @()
    if ($ExeDir -and $ExeDir.Trim().Length -gt 0) { $candidates += (Join-Path (Resolve-Path $ExeDir).Path $ExeName) }
    foreach ($d in @("out\build\x64-Debug", "out\build\x64-Release", "build\Debug", "build\Release", "build")) {
        $candidates += (Join-Path (Join-Path $ProjectRoot $d) $ExeName)
    }
    foreach ($p in $candidates) { if (Test-Path $p) { return (Resolve-Path $p).Path } }
    throw "Cannot find $ExeName. Build the project first or pass -ExeDir <build-output-dir>."
}

function Start-YoloProcess {
    param([string]$Name, [string]$ExeName, [string[]]$Arguments)
    $exe = Resolve-Exe $ExeName
    $stdout = Join-Path $ProcLogDir "$Name.stdout.log"
    $stderr = Join-Path $ProcLogDir "$Name.stderr.log"
    Write-Host "[START] $Name" -ForegroundColor Cyan
    Write-Host "        $exe $($Arguments -join ' ')"
    $proc = Start-Process -FilePath $exe -ArgumentList $Arguments -WorkingDirectory $ProjectRoot -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
    return [ordered]@{ name=$Name; pid=$proc.Id; exe=$exe; args=$Arguments; stdout=$stdout; stderr=$stderr; started_at=(Get-Date).ToString("s") }
}

$started = @()
$started += Start-YoloProcess "worker_cls" "yolo11_worker.exe" @("config\worker_cls.yaml", "--consumer-name", "cls_worker_1")
Start-Sleep -Seconds $StartupDelaySeconds
$started += Start-YoloProcess "server_cls_8084" "yolo11_server.exe" @("config\server_cls.yaml")

$pidFile = Join-Path $PidDir "phase17_cls_services.json"
$started | ConvertTo-Json -Depth 8 | Set-Content -Path $pidFile -Encoding UTF8
Write-Host "Started CLS service. PID file: $pidFile" -ForegroundColor Green
Write-Host "Next: python tools\phase17_cls_smoke_test.py --url http://127.0.0.1:8084 --image .\images\bus.png"
