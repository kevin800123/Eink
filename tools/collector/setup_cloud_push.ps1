# Install a hidden, logon-launched pusher that sends the usage snapshot to the
# private cloud relay so the ESP32 can read it while the PC is asleep/off.
#
# It runs cloud_push_daemon.py under pythonw (no console) via a Startup-folder
# VBScript. No admin required. Configure the relay URL + ingest token first, in
# tools/collector/cloud_push.local.json (gitignored) or via env vars.
#
# Usage:
#   .\tools\collector\setup_cloud_push.ps1
#   .\tools\collector\setup_cloud_push.ps1 -IntervalSeconds 720
#   .\tools\collector\setup_cloud_push.ps1 -Status
#   .\tools\collector\setup_cloud_push.ps1 -Remove

param(
  [int]$IntervalSeconds = 720,
  [switch]$Status,
  [switch]$Remove
)

$ErrorActionPreference = 'Stop'

$here = $PSScriptRoot
$daemon = Join-Path $here 'cloud_push_daemon.py'
$configFile = Join-Path $here 'cloud_push.local.json'
$startupDir = [Environment]::GetFolderPath('Startup')
$launcher = Join-Path $startupDir 'AI Usage Dashboard - Cloud Push.vbs'

function Test-PusherRunning {
  return [bool](Get-CimInstance Win32_Process -Filter "Name='pythonw.exe' OR Name='python.exe'" -ErrorAction SilentlyContinue |
    Where-Object { $_.CommandLine -like '*cloud_push_daemon.py*' })
}
function Stop-Pusher {
  Get-CimInstance Win32_Process -Filter "Name='pythonw.exe' OR Name='python.exe'" -ErrorAction SilentlyContinue |
    Where-Object { $_.CommandLine -like '*cloud_push_daemon.py*' } |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
}

if ($Status) {
  [pscustomobject]@{
    LauncherInstalled = Test-Path -LiteralPath $launcher
    PusherRunning     = Test-PusherRunning
    ConfigPresent     = Test-Path -LiteralPath $configFile
    Launcher          = $launcher
  }
  exit 0
}

if ($Remove) {
  if (Test-Path -LiteralPath $launcher) { Remove-Item -LiteralPath $launcher -Force }
  Stop-Pusher
  Write-Host 'Removed the cloud push launcher and stopped the pusher.'
  exit 0
}

if (-not (Test-Path -LiteralPath $daemon)) { throw "Not found: $daemon" }
if (-not (Test-Path -LiteralPath $configFile) -and -not $env:AI_DASH_CLOUD_INGEST_URL) {
  throw "Missing $configFile (and AI_DASH_CLOUD_INGEST_URL is unset). Create it with ingest_url + ingest_token first."
}

$pythonw = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Programs\Python\Python314\pythonw.exe'
if (-not (Test-Path -LiteralPath $pythonw)) {
  $cmd = Get-Command pythonw.exe -ErrorAction SilentlyContinue
  if ($cmd) { $pythonw = $cmd.Source } else { throw 'pythonw.exe was not found.' }
}

Stop-Pusher

$pythonwVbs = $pythonw.Replace('"', '""')
$daemonVbs = $daemon.Replace('"', '""')
$vbs = @"
Set shell = CreateObject("WScript.Shell")
shell.Run """$pythonwVbs"" ""$daemonVbs"" $IntervalSeconds", 0, False
"@

New-Item -ItemType Directory -Path $startupDir -Force | Out-Null
Set-Content -LiteralPath $launcher -Value $vbs -Encoding Unicode

$wscript = Join-Path $env:SystemRoot 'System32\wscript.exe'
Start-Process -FilePath $wscript -ArgumentList "`"$launcher`"" -WindowStyle Hidden
$deadline = (Get-Date).AddSeconds(8)
do { Start-Sleep -Milliseconds 300 } while (-not (Test-PusherRunning) -and (Get-Date) -lt $deadline)

Write-Host "Installed hidden Startup launcher: $launcher"
Write-Host ("Pusher running: {0}; interval {1}s" -f (Test-PusherRunning), $IntervalSeconds)
Write-Host 'Status:  .\tools\collector\setup_cloud_push.ps1 -Status'
Write-Host 'Remove:  .\tools\collector\setup_cloud_push.ps1 -Remove'
