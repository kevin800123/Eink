# Start the usage collector with a stable device token.
#
# The token is generated once and kept in tools/collector/token.local, which is
# gitignored. Reusing it means the firmware does not need reflashing every time
# the collector restarts.
#
# Usage:
#   .\tools\collector\run.ps1
#   .\tools\collector\run.ps1 -Port 9000
#   .\tools\collector\run.ps1 -ShowToken     # print the token and exit

param(
  [int]$Port = 8770,
  [string]$BindHost = '0.0.0.0',
  [switch]$ShowToken
)

$ErrorActionPreference = 'Stop'

$here = $PSScriptRoot
$repoRoot = Split-Path -Parent (Split-Path -Parent $here)
$script = Join-Path $here 'usage_collector.py'
$tokenFile = Join-Path $here 'token.local'

if (-not (Test-Path -LiteralPath $script)) {
  throw "Collector not found at $script"
}

if (-not (Test-Path -LiteralPath $tokenFile)) {
  $bytes = New-Object byte[] 24
  [System.Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($bytes)
  $token = [Convert]::ToBase64String($bytes).TrimEnd('=').Replace('+', '-').Replace('/', '_')
  Set-Content -LiteralPath $tokenFile -Value $token -Encoding ascii -NoNewline
  Write-Host "Generated a new device token and saved it to $tokenFile"
}

$token = (Get-Content -LiteralPath $tokenFile -Raw).Trim()

if ($ShowToken) {
  Write-Host $token
  exit 0
}

$python = (Get-Command python -ErrorAction SilentlyContinue).Source
if (-not $python) { $python = (Get-Command py -ErrorAction SilentlyContinue).Source }
if (-not $python) {
  # At logon PATH may not carry python; fall back to the pinned interpreter.
  $pinned = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Programs\Python\Python314\python.exe'
  if (Test-Path -LiteralPath $pinned) { $python = $pinned }
}
if (-not $python) { throw 'Python was not found on PATH.' }

# Report the LAN addresses the ESP32 can actually reach, since 0.0.0.0 is not
# something you can type into firmware.
$addresses = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
  Where-Object { $_.IPAddress -notmatch '^(127\.|169\.254\.)' } |
  Select-Object -ExpandProperty IPAddress
foreach ($ip in $addresses) {
  Write-Host ("Reachable at http://{0}:{1}/v1/dashboard" -f $ip, $Port)
}
Write-Host "Token is in $tokenFile (run with -ShowToken to print it)"
Write-Host ''

$env:AI_DASH_DEVICE_TOKEN = $token
& $python $script --host $BindHost --port $Port
exit $LASTEXITCODE
