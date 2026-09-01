# Compile and upload in a single arduino-cli invocation.
#
# Always use this instead of a bare `arduino-cli upload`. tools/compile.ps1
# builds into <repo>/build, but `upload` on its own reads arduino-cli's default
# per-sketch build cache. Those two locations drift apart, and uploading from
# the stale cache has already put an old, known-bad firmware onto the board.
# Compiling and uploading in one command makes that impossible.
#
# Usage:
#   .\tools\upload.ps1                       # uploads AI_Usage_Dashboard
#   .\tools\upload.ps1 -Sketch tools\smoke\epd_smoke
#   .\tools\upload.ps1 -Port COM7

param(
  [string]$Sketch = 'AI_Usage_Dashboard',
  [string]$Port = ''
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$sketchPath = Join-Path $repoRoot $Sketch
$fqbn = 'esp32:esp32:esp32c6:CDCOnBoot=cdc,CPUFreq=160,FlashFreq=80,FlashMode=qio,FlashSize=16M,PartitionScheme=huge_app,UploadSpeed=921600,ZigbeeMode=default'

if (-not (Test-Path -LiteralPath $sketchPath)) {
  throw "Sketch not found: $sketchPath"
}

$cliCommand = Get-Command arduino-cli -ErrorAction SilentlyContinue
if ($null -ne $cliCommand) {
  $cliPath = $cliCommand.Source
} else {
  $cliPath = Join-Path $env:LOCALAPPDATA 'Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'
}
if (-not (Test-Path -LiteralPath $cliPath)) {
  throw 'arduino-cli was not found. Install Arduino IDE 2.x or put arduino-cli on PATH.'
}

if ([string]::IsNullOrWhiteSpace($Port)) {
  $detected = & $cliPath board list --format json | ConvertFrom-Json
  foreach ($item in $detected.detected_ports) {
    foreach ($board in $item.matching_boards) {
      if ($board.fqbn -like 'esp32:esp32:*') {
        $Port = $item.port.address
        break
      }
    }
    if (-not [string]::IsNullOrWhiteSpace($Port)) { break }
  }
}
if ([string]::IsNullOrWhiteSpace($Port)) {
  throw 'No ESP32 serial port detected. Connect the board, power it on, and close any Serial Monitor holding the port.'
}

Write-Host "Sketch: $sketchPath"
Write-Host "Port:   $Port"

& $cliPath compile --clean --fqbn $fqbn --upload -p $Port $sketchPath
exit $LASTEXITCODE
