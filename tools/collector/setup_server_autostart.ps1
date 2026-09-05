# Install a self-healing collector HTTP server that starts at logon.
#
# A hidden Startup-folder VBScript launches collector_server_daemon.py with
# pythonw (no console window). That supervisor keeps usage_collector.py running
# and restarts it if it ever dies (crash, or killed during PC sleep), so the
# dashboard keeps working unattended. This needs no admin rights (unlike a
# Scheduled Task) and no ConPTY (unlike the Claude refresh). The server reads
# token.local itself, so no environment variable is required.
#
# Usage:
#   .\tools\collector\setup_server_autostart.ps1
#   .\tools\collector\setup_server_autostart.ps1 -Port 8770
#   .\tools\collector\setup_server_autostart.ps1 -Status
#   .\tools\collector\setup_server_autostart.ps1 -Remove

param(
  [int]$Port = 8770,
  [switch]$Status,
  [switch]$Remove
)

$ErrorActionPreference = 'Stop'

$here = $PSScriptRoot
$daemon = Join-Path $here 'collector_server_daemon.py'
$startupDir = [Environment]::GetFolderPath('Startup')
$launcher = Join-Path $startupDir 'AI Usage Dashboard - Collector Server.vbs'

function Test-Listening([int]$p) {
  return (Get-NetTCPConnection -State Listen -LocalPort $p -ErrorAction SilentlyContinue |
    Measure-Object).Count -gt 0
}

function Stop-ServerStack([int]$p) {
  # Stop the supervisor first so it does not immediately respawn the server.
  Get-CimInstance Win32_Process -Filter "Name='pythonw.exe' OR Name='python.exe'" -ErrorAction SilentlyContinue |
    Where-Object { $_.CommandLine -like '*collector_server_daemon.py*' } |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
  Start-Sleep -Milliseconds 300
  Get-NetTCPConnection -State Listen -LocalPort $p -ErrorAction SilentlyContinue |
    Select-Object -ExpandProperty OwningProcess -Unique |
    ForEach-Object { Stop-Process -Id $_ -Force -ErrorAction SilentlyContinue }
}

function Test-SupervisorRunning {
  return [bool](Get-CimInstance Win32_Process -Filter "Name='pythonw.exe' OR Name='python.exe'" -ErrorAction SilentlyContinue |
    Where-Object { $_.CommandLine -like '*collector_server_daemon.py*' })
}

if ($Status) {
  [pscustomobject]@{
    LauncherInstalled  = Test-Path -LiteralPath $launcher
    SupervisorRunning  = Test-SupervisorRunning
    PortListening      = Test-Listening $Port
    Port               = $Port
    Launcher           = $launcher
  }
  exit 0
}

if ($Remove) {
  if (Test-Path -LiteralPath $launcher) { Remove-Item -LiteralPath $launcher -Force }
  Stop-ServerStack $Port
  Write-Host 'Removed the collector server launcher and stopped the server + supervisor.'
  exit 0
}

if (-not (Test-Path -LiteralPath $daemon)) { throw "Not found: $daemon" }

# pythonw.exe has no console window; the supervisor and server need none.
$pythonw = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Programs\Python\Python314\pythonw.exe'
if (-not (Test-Path -LiteralPath $pythonw)) {
  $cmd = Get-Command pythonw.exe -ErrorAction SilentlyContinue
  if ($cmd) { $pythonw = $cmd.Source } else { throw 'pythonw.exe was not found.' }
}

# Stop any current server/supervisor so the new one can bind the port cleanly.
Stop-ServerStack $Port

$pythonwVbs = $pythonw.Replace('"', '""')
$daemonVbs = $daemon.Replace('"', '""')
$vbs = @"
Set shell = CreateObject("WScript.Shell")
shell.Run """$pythonwVbs"" ""$daemonVbs"" --host 0.0.0.0 --port $Port", 0, False
"@

New-Item -ItemType Directory -Path $startupDir -Force | Out-Null
Set-Content -LiteralPath $launcher -Value $vbs -Encoding Unicode

$wscript = Join-Path $env:SystemRoot 'System32\wscript.exe'
Start-Process -FilePath $wscript -ArgumentList "`"$launcher`"" -WindowStyle Hidden

$deadline = (Get-Date).AddSeconds(20)
do {
  Start-Sleep -Milliseconds 400
  $listening = Test-Listening $Port
} while (-not $listening -and (Get-Date) -lt $deadline)

if (-not $listening) {
  throw "Launcher written, but the server is not listening on port $Port yet. Check token.local and pythonw."
}

Write-Host "Installed self-healing Startup launcher: $launcher"
Write-Host "Collector server is listening on port $Port; a supervisor restarts it if it dies."
Write-Host 'Status:  .\tools\collector\setup_server_autostart.ps1 -Status'
Write-Host 'Remove:  .\tools\collector\setup_server_autostart.ps1 -Remove'
