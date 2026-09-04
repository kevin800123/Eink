# Install a hidden, logon-launched Claude quota refresh supervisor.
#
# Task Scheduler and pythonw.exe are intentionally not used: both were proven
# incompatible with the ConPTY session required by refresh_claude.py. A Startup
# folder VBScript launches python.exe inside the logged-on user's interactive
# session with window style 0, leaving a real but invisible console available.
#
# Usage:
#   .\tools\collector\setup_schedule.ps1
#   .\tools\collector\setup_schedule.ps1 -Minutes 30
#   .\tools\collector\setup_schedule.ps1 -Status
#   .\tools\collector\setup_schedule.ps1 -Remove

param(
  [ValidateRange(5, 1440)]
  [int]$Minutes = 30,
  [string]$ClaudeWorkDir,
  [switch]$Status,
  [switch]$Remove
)

$ErrorActionPreference = 'Stop'

$here = $PSScriptRoot
$daemon = Join-Path $here 'claude_refresh_daemon.py'
$repoRoot = (Resolve-Path (Join-Path $here '..\..')).Path
if (-not $ClaudeWorkDir) { $ClaudeWorkDir = $repoRoot }
if (-not (Test-Path -LiteralPath $ClaudeWorkDir -PathType Container)) {
  throw "Claude working directory not found: $ClaudeWorkDir"
}
$ClaudeWorkDir = (Resolve-Path -LiteralPath $ClaudeWorkDir).Path
$userProfileDir = [Environment]::GetEnvironmentVariable('USERPROFILE')
if (-not $userProfileDir) { throw 'USERPROFILE is not available.' }
$stateDir = Join-Path $userProfileDir '.ai-usage-dashboard'
$stopFile = Join-Path $stateDir 'claude_refresh_daemon.stop'
$statusFile = Join-Path $stateDir 'claude_refresh_daemon_status.json'
$logFile = Join-Path $stateDir 'claude_refresh_daemon.log'
$startupDir = [Environment]::GetFolderPath('Startup')
# A clearly readable name so the Startup folder is self-explanatory. The old
# camel-case name is cleaned up on the next install if it is still present.
$launcher = Join-Path $startupDir 'AI Usage Dashboard - Claude Refresh.vbs'
$legacyLauncher = Join-Path $startupDir 'AIUsageDashboardClaudeRefresh.vbs'

$pythonCandidates = @(
  (Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Programs\Python\Python314\python.exe')
)
$python = $pythonCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $python) {
  $pythonCommand = Get-Command python.exe -ErrorAction SilentlyContinue
  if ($pythonCommand) { $python = $pythonCommand.Source }
}

function Read-DaemonStatus {
  if (-not (Test-Path -LiteralPath $statusFile)) { return $null }
  try {
    return Get-Content -Raw -LiteralPath $statusFile | ConvertFrom-Json
  } catch {
    return $null
  }
}

function Test-DaemonProcess([object]$DaemonStatus) {
  if (-not $DaemonStatus -or -not $DaemonStatus.pid) { return $false }
  $process = Get-Process -Id ([int]$DaemonStatus.pid) -ErrorAction SilentlyContinue
  if (-not $process) { return $false }
  if ($DaemonStatus.python) {
    try { return $process.Path -eq [string]$DaemonStatus.python } catch { return $false }
  }
  return $true
}

function Request-DaemonStop {
  New-Item -ItemType Directory -Path $stateDir -Force | Out-Null
  Set-Content -LiteralPath $stopFile -Value 'stop' -Encoding ascii -NoNewline
  $deadline = (Get-Date).AddSeconds(15)
  do {
    $current = Read-DaemonStatus
    if (-not (Test-DaemonProcess $current)) { return $true }
    Start-Sleep -Milliseconds 250
  } while ((Get-Date) -lt $deadline)
  return $false
}

if ($Status) {
  $current = Read-DaemonStatus
  [pscustomobject]@{
    LauncherInstalled = Test-Path -LiteralPath $launcher
    ProcessRunning = Test-DaemonProcess $current
    Pid = if ($current) { $current.pid } else { $null }
    IntervalMinutes = if ($current) { $current.interval_minutes } else { $null }
    ClaudeWorkDir = if ($current) { $current.claude_workdir } else { $null }
    LastCacheCapturedAt = if ($current) { $current.last_cache_captured_at } else { $null }
    NextRunAt = if ($current) { $current.next_run_at } else { $null }
    StatusFile = $statusFile
    LogFile = $logFile
  }
  exit 0
}

if ($Remove) {
  if (Test-Path -LiteralPath $launcher) {
    Remove-Item -LiteralPath $launcher -Force
  }
  if (Test-Path -LiteralPath $legacyLauncher) {
    Remove-Item -LiteralPath $legacyLauncher -Force
  }
  $stopped = Request-DaemonStop
  if ($stopped) {
    Write-Host 'Removed Startup launcher and stopped the refresh supervisor.'
    exit 0
  }
  Write-Warning 'Startup launcher removed, but the current supervisor did not exit within 15 seconds.'
  exit 1
}

if (-not (Test-Path -LiteralPath $daemon)) { throw "Not found: $daemon" }
if (-not $python) { throw 'python.exe was not found.' }

& $python -c 'import winpty' 2>$null
if ($LASTEXITCODE -ne 0) {
  throw "pywinpty is not installed in $python"
}

$existing = Read-DaemonStatus
if (Test-DaemonProcess $existing) {
  if (-not (Request-DaemonStop)) {
    throw 'An existing refresh supervisor did not stop within 15 seconds.'
  }
}
if (Test-Path -LiteralPath $stopFile) {
  Remove-Item -LiteralPath $stopFile -Force
}

# VBScript doubles quotes inside a quoted string. The generated command is:
#   "python.exe" "claude_refresh_daemon.py" --minutes 30 --claude-workdir "repo"
$pythonVbs = $python.Replace('"', '""')
$daemonVbs = $daemon.Replace('"', '""')
$workDirVbs = $ClaudeWorkDir.Replace('"', '""')
$vbs = @"
Set shell = CreateObject("WScript.Shell")
shell.Run """$pythonVbs"" ""$daemonVbs"" --minutes $Minutes --claude-workdir ""$workDirVbs""", 0, False
"@

New-Item -ItemType Directory -Path $startupDir -Force | Out-Null
Set-Content -LiteralPath $launcher -Value $vbs -Encoding Unicode
# Remove the old camel-case launcher so only the readable name remains.
if (Test-Path -LiteralPath $legacyLauncher) {
  Remove-Item -LiteralPath $legacyLauncher -Force
}

$wscript = Join-Path $env:SystemRoot 'System32\wscript.exe'
Start-Process -FilePath $wscript -ArgumentList "`"$launcher`"" -WindowStyle Hidden

$deadline = (Get-Date).AddSeconds(15)
do {
  Start-Sleep -Milliseconds 250
  $current = Read-DaemonStatus
  if (Test-DaemonProcess $current) { break }
} while ((Get-Date) -lt $deadline)

if (-not (Test-DaemonProcess $current)) {
  throw "Startup launcher was written, but the supervisor did not start. See $logFile"
}

Write-Host "Installed hidden Startup launcher: $launcher"
Write-Host "Supervisor PID: $($current.pid)"
Write-Host "Refresh interval: $Minutes minute(s)"
Write-Host "Status: $statusFile"
Write-Host "Log: $logFile"
Write-Host 'Remove with: .\tools\collector\setup_schedule.ps1 -Remove'
