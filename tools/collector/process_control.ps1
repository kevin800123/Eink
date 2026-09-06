# Shared exact-script process identification. Dot-source for installer use;
# -StopScript is used by the watchdog only after a stale heartbeat is confirmed.
param([string]$StopScript, [int]$ExpectedPid, [double]$ExpectedStartEpoch)

function Test-OwnedPythonProcess($ProcessRecord, [string]$ScriptPath) {
  if (-not $ProcessRecord -or -not $ProcessRecord.ExecutablePath -or -not $ProcessRecord.CommandLine) { return $false }
  $exeName = [IO.Path]::GetFileName($ProcessRecord.ExecutablePath)
  if ($exeName -notin @('python.exe', 'pythonw.exe')) { return $false }
  # Direct interpreter invocation only. No substring search through -c text,
  # another clone's path, or arbitrary later arguments. Fail closed on ambiguity.
  $exe = [regex]::Escape([string]$ProcessRecord.ExecutablePath)
  $script = [regex]::Escape([IO.Path]::GetFullPath($ScriptPath))
  $exePattern = '"' + $exe + '"'
  $scriptPattern = '"' + $script + '"'
  if ($ProcessRecord.ExecutablePath -notmatch '\s') { $exePattern += '|' + $exe }
  if ($ScriptPath -notmatch '\s') { $scriptPattern += '|' + $script }
  $pattern = '^\s*(?:' + $exePattern + ')\s+(?:' + $scriptPattern + ')(?:\s|$)'
  return [regex]::IsMatch($ProcessRecord.CommandLine.Replace('/', '\'), $pattern, 'IgnoreCase')
}

function Get-OwnedPythonProcesses([string]$ScriptPath) {
  Get-CimInstance Win32_Process -Filter "Name='pythonw.exe' OR Name='python.exe'" -ErrorAction Stop |
    Where-Object { Test-OwnedPythonProcess $_ $ScriptPath }
}

function Stop-VerifiedProcess($ProcessRecord) {
  $current = Get-CimInstance Win32_Process -Filter "ProcessId=$($ProcessRecord.ProcessId)" -ErrorAction Stop
  if (-not $current) { return }
  if ($current.CreationDate -ne $ProcessRecord.CreationDate -or
      $current.ExecutablePath -ne $ProcessRecord.ExecutablePath -or
      $current.CommandLine -ne $ProcessRecord.CommandLine) { throw 'Process identity changed; refusing to stop it.' }
  $process = [Diagnostics.Process]::GetProcessById([int]$current.ProcessId)
  try {
    # Hold the process handle before checking time; Kill uses this same handle.
    $null = $process.Handle
    if ([Math]::Abs(($process.StartTime.ToUniversalTime() - $current.CreationDate.ToUniversalTime()).TotalSeconds) -gt 0.01) {
      throw 'Process start time changed; refusing to stop it.'
    }
    $process.Kill()
    $null = $process.WaitForExit(5000)
  } finally { $process.Dispose() }
}

function Stop-OwnedPythonProcesses([string]$ScriptPath) {
  foreach ($entry in @(Get-OwnedPythonProcesses $ScriptPath)) { Stop-VerifiedProcess $entry }
}

if ($StopScript) {
  $ErrorActionPreference = 'Stop'
  $entry = Get-CimInstance Win32_Process -Filter "ProcessId=$ExpectedPid"
  if (-not $entry) { exit 0 }
  if (-not (Test-OwnedPythonProcess $entry $StopScript)) { exit 2 }
  $startEpoch = ([DateTimeOffset]$entry.CreationDate).ToUnixTimeMilliseconds() / 1000.0
  if ($ExpectedStartEpoch -le 0 -or [Math]::Abs($startEpoch - $ExpectedStartEpoch) -gt 3) { exit 2 }
  # Capture descendants of the proven daemon before stopping it. Creation
  # times guard against stale parent PIDs; each handle is checked again on kill.
  $all = @(Get-CimInstance Win32_Process)
  $descendants = [Collections.Generic.List[object]]::new()
  $queue = [Collections.Generic.Queue[object]]::new()
  $queue.Enqueue($entry)
  while ($queue.Count) {
    $parent = $queue.Dequeue()
    foreach ($child in $all) {
      if ($child.ParentProcessId -eq $parent.ProcessId -and $child.CreationDate -ge $parent.CreationDate) {
        $descendants.Add($child)
        $queue.Enqueue($child)
      }
    }
  }
  Stop-VerifiedProcess $entry
  foreach ($child in $descendants) {
    if (Get-Process -Id $child.ProcessId -ErrorAction SilentlyContinue) {
      Stop-VerifiedProcess $child
    }
  }
}
