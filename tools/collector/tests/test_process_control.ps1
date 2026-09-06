$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\process_control.ps1')
$scriptPath = 'C:\fixture\with spaces\usage_collector.py'
$exe = 'C:\Python\pythonw.exe'
function Assert-Ownership([string]$Line, [bool]$Expected) {
  $entry = [pscustomobject]@{ExecutablePath=$exe; CommandLine=$Line}
  if ((Test-OwnedPythonProcess $entry $scriptPath) -ne $Expected) { throw "Ownership assertion failed: $Line" }
}
Assert-Ownership '"C:\Python\pythonw.exe" "C:\fixture\with spaces\usage_collector.py" --port 8770' $true
Assert-Ownership '"C:/Python/pythonw.exe" "C:/fixture/with spaces/usage_collector.py"' $true
Assert-Ownership '"C:\Python\pythonw.exe" "C:\another clone\usage_collector.py"' $false
Assert-Ownership '"C:\Python\pythonw.exe" -c "print(''C:\fixture\with spaces\usage_collector.py'')"' $false
Assert-Ownership '"C:\Python\pythonw.exe" "C:\fixture\with spaces\usage_collector.py.evil"' $false
Assert-Ownership '"C:\Python\pythonw.exe" C:\fixture\with spaces\usage_collector.py' $false
Write-Output '6 exact ownership checks passed'
