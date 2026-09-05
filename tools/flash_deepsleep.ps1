# Flash a board that is running a deep-sleep build.
#
# When deep sleep is enabled, the USB serial port only appears for a few seconds
# on each wake, so a normal upload fails to open the port. This board also has no
# RST button and an internal battery (so unplugging USB is not a reset).
#
# The reliable way in: HOLD the BOOT button. When the board next wakes on its
# timer it re-runs the ROM bootloader, samples BOOT low, and enters serial
# download mode with a STABLE USB port. This script compiles first (no port
# needed), then waits for the port and uploads the fresh build it just made
# (not a stale cache).
#
# Usage:
#   1. Run:  .\tools\flash_deepsleep.ps1
#   2. When it says "waiting", press and HOLD the BOOT button.
#   3. Keep holding until you see "Writing at 0x..." progress, then release.
#
# The wait covers one refresh interval; with a 30-minute interval it can take up
# to ~30 min for the wake, so a shorter interval is easier to reflash.

param(
  [string]$Sketch = 'AI_Usage_Dashboard',
  [string]$Port = '',
  [int]$TimeoutSeconds = 2000
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$sketchPath = Join-Path $repoRoot $Sketch
$buildPath = Join-Path $repoRoot 'build'
$fqbn = 'esp32:esp32:esp32c6:CDCOnBoot=cdc,CPUFreq=160,FlashFreq=80,FlashMode=qio,FlashSize=16M,PartitionScheme=huge_app,UploadSpeed=921600,ZigbeeMode=default'

if (-not (Test-Path -LiteralPath $sketchPath)) { throw "Sketch not found: $sketchPath" }

$cliCommand = Get-Command arduino-cli -ErrorAction SilentlyContinue
if ($null -ne $cliCommand) {
  $cliPath = $cliCommand.Source
} else {
  $cliPath = Join-Path $env:LOCALAPPDATA 'Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'
}
if (-not (Test-Path -LiteralPath $cliPath)) {
  throw 'arduino-cli was not found. Install Arduino IDE 2.x or put arduino-cli on PATH.'
}

Write-Host 'Compiling a fresh build (no board needed yet)...'
& $cliPath compile --clean --fqbn $fqbn --build-path $buildPath $sketchPath
if ($LASTEXITCODE -ne 0) { throw 'Compile failed; not attempting to upload.' }

Write-Host ''
Write-Host 'Leave this running. It waits for the device to wake (the serial port'
Write-Host 'appears), then uploads automatically and retries on the next wake if'
Write-Host 'a window is missed. Holding BOOT is optional but makes it more reliable.'
Write-Host ''

$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
$uploaded = $false
while ((Get-Date) -lt $deadline -and -not $uploaded) {
  # Wait for a serial port to appear on the next wake.
  $before = [System.IO.Ports.SerialPort]::GetPortNames()
  $target = $null
  while ((Get-Date) -lt $deadline) {
    $now = [System.IO.Ports.SerialPort]::GetPortNames()
    if ($Port) {
      if ($now -contains $Port) { $target = $Port; break }
    } else {
      if ($now -contains 'COM3') { $target = 'COM3'; break }
      $fresh = $now | Where-Object { $before -notcontains $_ }
      if ($fresh) { $target = $fresh | Select-Object -First 1; break }
    }
    Start-Sleep -Milliseconds 200
  }
  if (-not $target) { break }

  Write-Host "Port $target appeared - uploading the fresh build now..."
  & $cliPath upload --fqbn $fqbn --input-dir $buildPath -p $target $sketchPath
  if ($LASTEXITCODE -eq 0) { $uploaded = $true; break }

  Write-Host 'Upload attempt failed (likely missed the wake window). Waiting for the next wake...'
  Start-Sleep -Seconds 3
}

if ($uploaded) {
  Write-Host 'Flashed successfully.'
  exit 0
}
throw "Did not flash within $TimeoutSeconds s. Is the board powered and cabled? Try holding BOOT."
