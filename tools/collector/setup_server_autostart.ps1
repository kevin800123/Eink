# Install a hidden, logon-launched collector HTTP server.
#
# The server (usage_collector.py, via run.ps1) is an idle socket listener: it
# uses no CPU until the device polls, and it does not keep the PC awake. A
# Startup-folder VBScript launches it hidden at logon so the dashboard works
# unattended. The device token stays in token.local; it is never written into
# the launcher.
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
$run = Join-Path $here 'run.ps1'
$startupDir = [Environment]::GetFolderPath('Startup')
# A clearly readable name so the Startup folder is self-explanatory.
$launcher = Join-Path $startupDir 'AI Usage Dashboard - Collector Server.vbs'

function Test-Listening([int]$p) {
  return (Get-NetTCPConnection -State Listen -LocalPort $p -ErrorAction SilentlyContinue |
    Measure-Object).Count -gt 0
}

function Stop-ServerOnPort([int]$p) {
  Get-NetTCPConnection -State Listen -LocalPort $p -ErrorAction SilentlyContinue |
    Select-Object -ExpandProperty OwningProcess -Unique |
    ForEach-Object { Stop-Process -Id $_ -Force -ErrorAction SilentlyContinue }
}

if ($Status) {
  [pscustomobject]@{
    LauncherInstalled = Test-Path -LiteralPath $launcher
    PortListening     = Test-Listening $Port
    Port              = $Port
    Launcher          = $launcher
  }
  exit 0
}

if ($Remove) {
  if (Test-Path -LiteralPath $launcher) { Remove-Item -LiteralPath $launcher -Force }
  Stop-ServerOnPort $Port
  Write-Host 'Removed the collector server Startup launcher and stopped the server.'
  exit 0
}

if (-not (Test-Path -LiteralPath $run)) { throw "Not found: $run" }

# Free the port so the hidden instance can bind (this also stops a foreground
# run.ps1 window you may have open).
Stop-ServerOnPort $Port
Start-Sleep -Milliseconds 500

$ps = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$psVbs = $ps.Replace('"', '""')
$runVbs = $run.Replace('"', '""')
# WScript.Shell.Run(..., 0, False): window style 0 = hidden, still a real
# console-less background process.
$vbs = @"
Set shell = CreateObject("WScript.Shell")
shell.Run "$psVbs -ExecutionPolicy Bypass -WindowStyle Hidden -NoProfile -File ""$runVbs"" -Port $Port", 0, False
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
  throw "Startup launcher written, but the server is not listening on port $Port yet. Check that Python and token.local are in place."
}

Write-Host "Installed hidden Startup launcher: $launcher"
Write-Host "Collector server is listening on port $Port and will start at every logon."
Write-Host 'Remove with: .\tools\collector\setup_server_autostart.ps1 -Remove'
