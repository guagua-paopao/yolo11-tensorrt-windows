param(
    [string]$ExeDir = ".\out\build\x64-Debug"
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot ".." )).Path
$PidDir = Join-Path $Root "runtime\pids"
$LogDir = Join-Path $Root "runtime\logs\phase17_5_pose_process"
New-Item -ItemType Directory -Force -Path $PidDir | Out-Null
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$ExeRoot = if ([System.IO.Path]::IsPathRooted($ExeDir)) { $ExeDir } else { Join-Path $Root $ExeDir }
$WorkerExe = Join-Path $ExeRoot "yolo11_worker.exe"
$ServerExe = Join-Path $ExeRoot "yolo11_server.exe"

if (!(Test-Path $WorkerExe)) { throw "Missing worker exe: $WorkerExe" }
if (!(Test-Path $ServerExe)) { throw "Missing server exe: $ServerExe" }

$items = @()
function Start-ManagedProcess {
    param(
        [string]$Name,
        [string]$FilePath,
        [string[]]$ArgsList
    )
    $stdout = Join-Path $LogDir "$Name.out.log"
    $stderr = Join-Path $LogDir "$Name.err.log"
    Write-Host "[START] $Name"
    Write-Host "        $FilePath $($ArgsList -join ' ')"
    $p = Start-Process -FilePath $FilePath -ArgumentList $ArgsList -WorkingDirectory $Root -PassThru -WindowStyle Normal -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $script:items += [pscustomobject]@{
        name = $Name
        pid = $p.Id
        file = $FilePath
        args = $ArgsList -join ' '
        stdout = $stdout
        stderr = $stderr
    }
}

Start-ManagedProcess -Name "worker_pose" -FilePath $WorkerExe -ArgsList @("config\worker_pose.yaml", "--consumer-name", "pose_worker_1")
Start-Sleep -Seconds 3
Start-ManagedProcess -Name "server_pose_8085" -FilePath $ServerExe -ArgsList @("config\server_pose.yaml")

$PidFile = Join-Path $PidDir "phase17_5_pose_services.json"
$items | ConvertTo-Json -Depth 5 | Set-Content -Path $PidFile -Encoding UTF8
Write-Host "Started POSE service. PID file: $PidFile"
Write-Host "Next: python tools\phase17_5_pose_smoke_test.py --url http://127.0.0.1:8085 --image .\images\bus.png"
