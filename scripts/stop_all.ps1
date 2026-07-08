param(
    [switch]$KillByName,
    [switch]$KillByPort,
    [string]$PidFile = ".\runtime\pids\phase18_services.json"
)

$ErrorActionPreference = "Continue"
Set-Location (Resolve-Path (Join-Path $PSScriptRoot ".."))

function Stop-Phase16ProcessById {
    param(
        [Parameter(Mandatory=$true)]
        [int]$ProcessId,

        [string]$DisplayName = ""
    )

    try {
        $proc = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
        if ($null -eq $proc) {
            Write-Host "[SKIP] $DisplayName pid=$ProcessId not running"
            return
        }

        Write-Host "[STOP] $DisplayName pid=$ProcessId process=$($proc.ProcessName)"
        Stop-Process -Id $ProcessId -Force -ErrorAction Stop
    }
    catch {
        Write-Warning "Failed to stop $DisplayName pid=$ProcessId : $($_.Exception.Message)"
    }
}

function Stop-Phase16ProcessByName {
    param([string[]]$Names)

    foreach ($name in $Names) {
        Get-Process -Name $name -ErrorAction SilentlyContinue | ForEach-Object {
            try {
                Write-Host "[STOP-BY-NAME] $($_.ProcessName) pid=$($_.Id)"
                Stop-Process -Id $_.Id -Force -ErrorAction Stop
            }
            catch {
                Write-Warning "Failed to stop process $($_.ProcessName) pid=$($_.Id): $($_.Exception.Message)"
            }
        }
    }
}

function Stop-Phase16ProcessByPort {
    param([int[]]$Ports)

    foreach ($port in $Ports) {
        try {
            $lines = netstat -ano | Select-String ":$port\s+.*LISTENING\s+(\d+)"
            foreach ($line in $lines) {
                $text = [string]$line
                if ($text -match "LISTENING\s+(\d+)") {
                    $processIdFromPort = [int]$Matches[1]
                    Stop-Phase16ProcessById -ProcessId $processIdFromPort -DisplayName "port_$port"
                }
            }
        }
        catch {
            Write-Warning "Failed to inspect port $port : $($_.Exception.Message)"
        }
    }
}

if (Test-Path $PidFile) {
    try {
        $payload = Get-Content $PidFile -Raw | ConvertFrom-Json
        $items = @()

        if ($payload.processes) {
            $items = @($payload.processes)
        }
        elseif ($payload -is [array]) {
            $items = @($payload)
        }

        foreach ($item in $items) {
            if ($null -ne $item.pid) {
                Stop-Phase16ProcessById -ProcessId ([int]$item.pid) -DisplayName ([string]$item.name)
            }
        }
    }
    catch {
        Write-Warning "Failed to read PID file $PidFile : $($_.Exception.Message)"
    }
}
else {
    Write-Warning "PID file not found: $PidFile"
}

if ($KillByName) {
    Stop-Phase16ProcessByName -Names @(
        "yolo11_server",
        "yolo11_worker",
        "yolo11_video_worker",
        "yolo11_stream_worker"
    )
}

if ($KillByPort) {
    Stop-Phase16ProcessByPort -Ports @(8080, 8081, 8082, 8083, 8084, 8085, 8086)
}

Write-Host "Stop request completed."
