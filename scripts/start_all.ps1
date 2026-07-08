<#
Phase 18 launcher for YOLO11 TensorRT C++ services.
Run from project root:
  powershell -ExecutionPolicy Bypass -File .\scripts\start_all.ps1

Optional:
  powershell -ExecutionPolicy Bypass -File .\scripts\start_all.ps1 -ExeDir .\out\build\x64-Debug
#>
[CmdletBinding()]
param(
    [string]$Root = "",
    [string]$ExeDir = "",
    [int]$StartupDelaySeconds = 2,
    [switch]$NoWorkers,
    [switch]$NoServers
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

$PidDir = Join-Path $ProjectRoot "runtime\pids"
$ProcLogDir = Join-Path $ProjectRoot "runtime\logs\phase18_process"
New-Item -ItemType Directory -Force -Path $PidDir, $ProcLogDir | Out-Null

function Resolve-Exe {
    param([string]$ExeName)

    $candidates = @()
    if ($ExeDir -and $ExeDir.Trim().Length -gt 0) {
        $candidates += (Join-Path (Resolve-Path $ExeDir).Path $ExeName)
    }

    $commonDirs = @(
        "out\build\x64-Debug",
        "out\build\x64-Release",
        "out\build\x64-debug",
        "out\build\x64-release",
        "build\Debug",
        "build\Release",
        "build"
    )
    foreach ($d in $commonDirs) {
        $candidates += (Join-Path (Join-Path $ProjectRoot $d) $ExeName)
    }

    foreach ($p in $candidates) {
        if (Test-Path $p) {
            return (Resolve-Path $p).Path
        }
    }

    $found = Get-ChildItem -Path $ProjectRoot -Filter $ExeName -File -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match "(out|build)" } |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if ($found) {
        return $found.FullName
    }

    throw "Cannot find $ExeName. Build the project first or pass -ExeDir <build-output-dir>."
}

function Start-YoloProcess {
    param(
        [string]$Name,
        [string]$ExeName,
        [string[]]$Arguments
    )

    $exe = Resolve-Exe $ExeName
    $stdout = Join-Path $ProcLogDir "$Name.stdout.log"
    $stderr = Join-Path $ProcLogDir "$Name.stderr.log"

    Write-Host "[START] $Name" -ForegroundColor Cyan
    Write-Host "        $exe $($Arguments -join ' ')"

    $proc = Start-Process -FilePath $exe `
        -ArgumentList $Arguments `
        -WorkingDirectory $ProjectRoot `
        -RedirectStandardOutput $stdout `
        -RedirectStandardError $stderr `
        -PassThru

    return [ordered]@{
        name = $Name
        pid = $proc.Id
        exe = $exe
        args = $Arguments
        stdout = $stdout
        stderr = $stderr
        started_at = (Get-Date).ToString("s")
    }
}

$started = @()

if (-not $NoWorkers) {
    $started += Start-YoloProcess "worker_detect" "yolo11_worker.exe" @("config\worker_detect.yaml", "--consumer-name", "worker_1")
    Start-Sleep -Seconds $StartupDelaySeconds
    $started += Start-YoloProcess "worker_obb" "yolo11_worker.exe" @("config\worker_obb.yaml", "--consumer-name", "obb_worker_1")
    Start-Sleep -Seconds $StartupDelaySeconds
    $started += Start-YoloProcess "worker_video" "yolo11_video_worker.exe" @("config\worker_video.yaml", "--consumer-name", "video_worker_1")
    Start-Sleep -Seconds $StartupDelaySeconds
    $started += Start-YoloProcess "worker_stream" "yolo11_stream_worker.exe" @("config\worker_stream.yaml", "--consumer-name", "stream_worker_1")
    Start-Sleep -Seconds $StartupDelaySeconds
    $started += Start-YoloProcess "worker_cls" "yolo11_worker.exe" @("config\worker_cls.yaml", "--consumer-name", "cls_worker_1")
    Start-Sleep -Seconds $StartupDelaySeconds
    $started += Start-YoloProcess "worker_pose" "yolo11_worker.exe" @("config\worker_pose.yaml", "--consumer-name", "pose_worker_1")
    Start-Sleep -Seconds $StartupDelaySeconds
    $started += Start-YoloProcess "worker_seg" "yolo11_worker.exe" @("config\worker_seg.yaml", "--consumer-name", "seg_worker_1")
    Start-Sleep -Seconds $StartupDelaySeconds
}

if (-not $NoServers) {
    $started += Start-YoloProcess "server_detect_8080" "yolo11_server.exe" @("config\server_detect.yaml")
    Start-Sleep -Seconds $StartupDelaySeconds
    $started += Start-YoloProcess "server_obb_8081" "yolo11_server.exe" @("config\server_obb.yaml")
    Start-Sleep -Seconds $StartupDelaySeconds
    $started += Start-YoloProcess "server_video_8082" "yolo11_server.exe" @("config\server_video.yaml")
    Start-Sleep -Seconds $StartupDelaySeconds
    $started += Start-YoloProcess "server_stream_8083" "yolo11_server.exe" @("config\server_stream.yaml")
    Start-Sleep -Seconds $StartupDelaySeconds
    $started += Start-YoloProcess "server_cls_8084" "yolo11_server.exe" @("config\server_cls.yaml")
    Start-Sleep -Seconds $StartupDelaySeconds
    $started += Start-YoloProcess "server_pose_8085" "yolo11_server.exe" @("config\server_pose.yaml")
    Start-Sleep -Seconds $StartupDelaySeconds
    $started += Start-YoloProcess "server_seg_8086" "yolo11_server.exe" @("config\server_seg.yaml")
}

$pidFile = Join-Path $PidDir "phase18_services.json"
$started | ConvertTo-Json -Depth 8 | Set-Content -Path $pidFile -Encoding UTF8

Write-Host ""
Write-Host "Started $($started.Count) processes. PID file: $pidFile" -ForegroundColor Green
Write-Host "Process stdout/stderr logs: $ProcLogDir"
Write-Host "Next: powershell -ExecutionPolicy Bypass -File .\scripts\check_all.ps1"
