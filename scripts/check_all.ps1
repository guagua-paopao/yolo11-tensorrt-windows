<#
Phase 16 health/ready/workers/metrics + Redis XPENDING checker.
Fix: avoid using PowerShell automatic variable name $Args for Redis arguments.
Run from project root:
  powershell -ExecutionPolicy Bypass -File .\scripts\check_all.ps1

If redis-cli is inside a specific WSL distro:
  powershell -ExecutionPolicy Bypass -File .\scripts\check_all.ps1 -UseWslRedisCli -WslDistro Ubuntu
#>
[CmdletBinding()]
param(
    [string]$Root = "",
    [string]$RedisHost = "172.19.196.109",
    [int]$RedisPort = 6379,
    [string]$RedisCli = "redis-cli",
    [switch]$UseWslRedisCli,
    [string]$WslDistro = "",
    [int]$TimeoutSec = 8
)

$ErrorActionPreference = "Continue"

function Resolve-ProjectRoot {
    param([string]$GivenRoot)
    if ($GivenRoot -and $GivenRoot.Trim().Length -gt 0) {
        return (Resolve-Path $GivenRoot).Path
    }
    return (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

$ProjectRoot = Resolve-ProjectRoot $Root
Set-Location $ProjectRoot

$services = @(
    [ordered]@{ name="detect"; url="http://127.0.0.1:8080"; model="detect"; task_kind="image_async"; worker_group="image_detect_gpu0" },
    [ordered]@{ name="obb";    url="http://127.0.0.1:8081"; model="obb";    task_kind="image_async"; worker_group="image_obb_gpu0" },
    [ordered]@{ name="video";  url="http://127.0.0.1:8082"; model="video";  task_kind="video_file";  worker_group="video_detect_gpu0" },
    [ordered]@{ name="stream"; url="http://127.0.0.1:8083"; model="stream"; task_kind="live_stream"; worker_group="stream_detect_gpu0" },
    [ordered]@{ name="cls";    url="http://127.0.0.1:8084"; model="cls";    task_kind="image_async"; worker_group="image_cls_gpu0" },
    [ordered]@{ name="pose";   url="http://127.0.0.1:8085"; model="pose";   task_kind="image_async"; worker_group="image_pose_gpu0" },
    [ordered]@{ name="seg";    url="http://127.0.0.1:8086"; model="seg";    task_kind="image_async"; worker_group="image_seg_gpu0" }
)

$queues = @(
    [ordered]@{ name="detect"; stream="yolo:stream:detect"; group="yolo11_group" },
    [ordered]@{ name="obb";    stream="yolo:stream:obb"; group="yolo11_obb_group" },
    [ordered]@{ name="video";  stream="yolo:stream:video:detect"; group="yolo11_video_detect_group" },
    [ordered]@{ name="stream"; stream="yolo:stream:live:detect"; group="yolo11_stream_detect_group" },
    [ordered]@{ name="cls";    stream="yolo:stream:cls"; group="yolo11_cls_group" },
    [ordered]@{ name="pose";   stream="yolo:stream:pose"; group="yolo11_pose_group" },
    [ordered]@{ name="seg";    stream="yolo:stream:seg"; group="yolo11_seg_group" }
)

function Invoke-ApiJson {
    param([string]$Url)
    try {
        return Invoke-RestMethod -Method GET -Uri $Url -TimeoutSec $TimeoutSec
    } catch {
        return [pscustomobject]@{ success=$false; error=$_.Exception.Message }
    }
}

function Remove-WslWarningLines {
    param([string[]]$Lines)
    $clean = @()
    foreach ($line in $Lines) {
        # WSL may print a localhost proxy warning before the real command output.
        # Keep Redis output; drop WSL launcher warnings so PING/XPENDING parsing stays stable.
        if ($line -match '^wsl:') { continue }
        if ($line -match 'localhost.*WSL') { continue }
        if ($line -match 'localhost.*代理.*WSL') { continue }
        $clean += $line
    }
    return $clean
}

function Invoke-ExternalTextWithTimeout {
    param(
        [string]$FilePath,
        [string[]]$ArgumentList,
        [int]$TimeoutSeconds
    )
    $stdoutPath = [System.IO.Path]::GetTempFileName()
    $stderrPath = [System.IO.Path]::GetTempFileName()
    try {
        $proc = Start-Process -FilePath $FilePath -ArgumentList $ArgumentList -NoNewWindow -PassThru -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
        if (-not $proc.WaitForExit($TimeoutSeconds * 1000)) {
            try { $proc.Kill() } catch {}
            return "ERROR: command timeout after ${TimeoutSeconds}s: $FilePath $($ArgumentList -join ' ')"
        }
        $stdout = Get-Content -Path $stdoutPath -ErrorAction SilentlyContinue
        $stderr = Get-Content -Path $stderrPath -ErrorAction SilentlyContinue
        $lines = @()
        if ($stdout) { $lines += $stdout }
        if ($stderr) { $lines += $stderr }
        return (($lines) -join "`n").Trim()
    } catch {
        return "ERROR: $($_.Exception.Message)"
    } finally {
        Remove-Item -Path $stdoutPath,$stderrPath -Force -ErrorAction SilentlyContinue
    }
}

function Invoke-RedisCliText {
    param([string[]]$RedisArgs)
    try {
        if ($UseWslRedisCli) {
            $wslArgs = @()
            if ($WslDistro -and $WslDistro.Trim().Length -gt 0) {
                $wslArgs += @("-d", $WslDistro)
            }
            # Important: pass redis-cli arguments as one flat array. Do NOT use "-- @redisCmd" here;
            # on some Windows PowerShell versions it can fail to expand and make WSL appear stuck.
            $wslArgs += @("--", "redis-cli", "-h", $RedisHost, "-p", [string]$RedisPort)
            $wslArgs += $RedisArgs
            $rawText = Invoke-ExternalTextWithTimeout -FilePath "wsl.exe" -ArgumentList $wslArgs -TimeoutSeconds $TimeoutSec
            $outputLines = $rawText -split "`n"
            $outputLines = Remove-WslWarningLines $outputLines
            return ($outputLines -join "`n").Trim()
        }
        $full = @("-h", $RedisHost, "-p", [string]$RedisPort) + $RedisArgs
        return Invoke-ExternalTextWithTimeout -FilePath $RedisCli -ArgumentList $full -TimeoutSeconds $TimeoutSec
    } catch {
        return "ERROR: $($_.Exception.Message)"
    }
}

function Parse-XPendingTotal {
    param([string]$Text)
    $t = ($Text | Out-String).Trim()
    if ($t -match "NOGROUP") { return -2 }
    if ($t -match "not found") { return -3 }
    if ($t -match "ERROR") { return -1 }

    # redis-cli often prints XPENDING summary like:
    # 1) (integer) 0
    # or with --raw/other modes the first line may be just 0.
    foreach ($line in ($t -split "`n")) {
        $l = $line.Trim()
        if ($l -match "^\d+$") { return [int]$l }
        if ($l -match "\(integer\)\s*(\d+)") { return [int]$Matches[1] }
        if ($l -match "^1\)\s*\(integer\)\s*(\d+)") { return [int]$Matches[1] }
    }
    return -1
}

$overallOk = $true
$rows = @()

Write-Host "=== HTTP service checks ===" -ForegroundColor Cyan
foreach ($svc in $services) {
    $suffix = "?model=$($svc.model)&task_kind=$($svc.task_kind)&worker_group=$($svc.worker_group)"
    $health = Invoke-ApiJson "$($svc.url)/api/v1/health"
    $ready = Invoke-ApiJson "$($svc.url)/api/v1/ready$suffix"
    $workers = Invoke-ApiJson "$($svc.url)/api/v1/workers$suffix"
    $metrics = Invoke-ApiJson "$($svc.url)/api/v1/metrics?model=$($svc.model)"

    $healthOk = ($health.success -eq $true)
    $readyOk = ($ready.ready -eq $true)
    $workerCount = 0
    if ($workers.workers) { $workerCount = @($workers.workers).Count }
    $metricsOk = ($metrics.success -eq $true)

    if (-not ($healthOk -and $readyOk -and $workerCount -ge 1 -and $metricsOk)) { $overallOk = $false }

    $rows += [pscustomobject]@{
        service = $svc.name
        health = $healthOk
        ready = $readyOk
        workers = $workerCount
        metrics = $metricsOk
        phase = $health.phase
    }
}
$rows | Format-Table -AutoSize

Write-Host "=== Redis checks ===" -ForegroundColor Cyan
$ping = Invoke-RedisCliText -RedisArgs @("PING")
Write-Host "PING: $ping"
if ($ping -notmatch "PONG") { $overallOk = $false }

$pendingRows = @()
foreach ($q in $queues) {
    $text = Invoke-RedisCliText -RedisArgs @("XPENDING", $q.stream, $q.group)
    $total = Parse-XPendingTotal $text
    if ($total -ne 0) { $overallOk = $false }
    $pendingRows += [pscustomobject]@{
        queue = $q.name
        stream = $q.stream
        group = $q.group
        pending = $total
        raw = $text
    }
}
$pendingRows | Select-Object queue,stream,group,pending | Format-Table -AutoSize

$reportDir = Join-Path $ProjectRoot "reports"
New-Item -ItemType Directory -Force -Path $reportDir | Out-Null
$reportPath = Join-Path $reportDir "phase18_check_all.json"
[ordered]@{
    timestamp = (Get-Date).ToString("s")
    success = $overallOk
    services = $rows
    redis_ping = $ping
    pending = $pendingRows
} | ConvertTo-Json -Depth 8 | Set-Content -Path $reportPath -Encoding UTF8

Write-Host "Report: $reportPath"
if ($overallOk) {
    Write-Host "PASS: all services are ready and Redis pending looks clean." -ForegroundColor Green
    exit 0
}
Write-Host "FAIL: at least one check failed." -ForegroundColor Red
Write-Host "Hint: if WSL default distro has no redis-cli, run: wsl -l -v, then use -WslDistro <UbuntuDistroName>." -ForegroundColor Yellow
exit 1
