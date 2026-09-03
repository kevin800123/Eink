# KNOWN BROKEN, kept for reference. See COLLECTOR_HANDOFF.md section 2.
# A Windows Scheduled Task runs in a session with no usable window station, so
# the ConPTY that refresh_claude.py needs cannot attach and the interactive
# claude turn hangs until the time limit kills it. Verified 2026-09-03: the task
# stuck in Running and never wrote the cache. Do not rely on this file; the
# auto-refresh mechanism still needs to be built (a logon-launched hidden
# python.exe loop is the suggested direction).
#
# Register (or remove) a Windows Scheduled Task that refreshes the Claude quota
# cache by running refresh_claude.py on an interval.
#
# refresh_claude.py drives a brief interactive `claude` turn so statusLine
# records the subscription quota. Each run makes one small API call, so the
# interval is a direct cost/freshness tradeoff. 30 minutes is a reasonable
# default; do not go below a few minutes.
#
# Usage:
#   .\tools\collector\setup_schedule.ps1                 # install, every 30 min
#   .\tools\collector\setup_schedule.ps1 -Minutes 15     # install, every 15 min
#   .\tools\collector\setup_schedule.ps1 -Remove         # remove the task
#
# The task runs only while you are logged on, because a ConPTY needs an
# interactive session. It runs hidden, so no console window pops up.

param(
  [int]$Minutes = 30,
  [switch]$Remove
)

$ErrorActionPreference = 'Stop'
$taskName = 'AI Usage Dashboard - Claude refresh'

if ($Remove) {
  if (Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue) {
    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
    Write-Host "Removed scheduled task: $taskName"
  } else {
    Write-Host "No scheduled task named '$taskName' was found."
  }
  exit 0
}

$here = $PSScriptRoot
$script = Join-Path $here 'refresh_claude.py'
if (-not (Test-Path -LiteralPath $script)) { throw "Not found: $script" }

# Pin the interpreter that has pywinpty installed.
$python = 'C:\Users\USER\AppData\Local\Programs\Python\Python314\python.exe'
if (-not (Test-Path -LiteralPath $python)) {
  $cmd = Get-Command python -ErrorAction SilentlyContinue
  if (-not $cmd) { throw 'python.exe not found; edit $python in this script.' }
  $python = $cmd.Source
}

$action = New-ScheduledTaskAction -Execute $python -Argument "`"$script`""

# Repeat forever at the chosen interval, starting a minute from now.
$trigger = New-ScheduledTaskTrigger -Once -At (Get-Date).AddMinutes(1) `
  -RepetitionInterval (New-TimeSpan -Minutes $Minutes)

$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries `
  -DontStopIfGoingOnBatteries -StartWhenAvailable -Hidden `
  -MultipleInstances IgnoreNew -ExecutionTimeLimit (New-TimeSpan -Minutes 5)

$principal = New-ScheduledTaskPrincipal -UserId $env:USERNAME -LogonType Interactive -RunLevel Limited

Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger `
  -Settings $settings -Principal $principal -Force | Out-Null

Write-Host "Installed '$taskName': every $Minutes minute(s), hidden, while logged on."
Write-Host "Each run makes one small API call. Remove with: .\tools\collector\setup_schedule.ps1 -Remove"
Write-Host "Run it once now to verify:"
Write-Host "  Start-ScheduledTask -TaskName `"$taskName`""
