$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$sketchPath = Join-Path $repoRoot 'AI_Usage_Dashboard'
$buildPath = Join-Path $repoRoot 'build'
$fqbn = 'esp32:esp32:esp32c6:CDCOnBoot=cdc,CPUFreq=160,FlashFreq=80,FlashMode=qio,FlashSize=16M,PartitionScheme=huge_app,UploadSpeed=921600,ZigbeeMode=default'

$cliCommand = Get-Command arduino-cli -ErrorAction SilentlyContinue
if ($null -ne $cliCommand) {
  $cliPath = $cliCommand.Source
} else {
  $cliPath = Join-Path $env:LOCALAPPDATA 'Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'
}

if (-not (Test-Path -LiteralPath $cliPath)) {
  throw 'arduino-cli was not found. Install Arduino IDE 2.x or put arduino-cli on PATH.'
}

& $cliPath compile --fqbn $fqbn --build-path $buildPath $sketchPath
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

