# PC collector hardening evidence — 2026-09-06

Base: `df292a79de2c42f54f9cafb8f16e74e72d18a154`.
Scope: `tools/collector/` only. No firmware, power sequence, Wi-Fi credentials,
device token or USB flashing changes.

## Behavior

- Source observations older than 3600 seconds are unavailable. Expired windows
  are not replaced with inferred 0%. Both windows must be complete for old
  firmware to show a provider row. Fresh, actually observed 0% is preserved.
- Exact-script process ownership and creation-time verification replace killing
  whichever process happens to listen on the selected port.
- HTTP supervisor probes authenticated `/healthz` and `/v1/dashboard`, checks
  the child PID, restarts after consecutive probe failures and backs off on
  repeated failures. Stale providers do not trigger a restart.
- Claude heartbeat is 30 seconds while idle; watchdog checks twice before
  acting on stale status, validates process identity and stops captured owned
  descendants. Refresh failures are reported without adding rapid paid turns.

## Automated tests

Python 3.14.2, Windows PowerShell, standard-library unittest:

```powershell
$python = Join-Path $env:LOCALAPPDATA 'Programs\Python\Python314\python.exe'
& $python -B -m unittest discover -s tools/collector/tests -p 'test_*.py' -v
powershell -NoProfile -ExecutionPolicy Bypass -File tools/collector/tests/test_process_control.ps1
```

19 Python tests passed: 16 unit/HTTP tests and 3 real Windows process integration
tests. 6 PowerShell ownership assertions passed. All collector Python files
parsed with `ast.parse`; all PowerShell files passed AST parsing;
`git diff --check` passed.

Fault injection uses only disposable loopback fixtures and temporary state:

- Foreign listener survived both install and remove attempts; both operations
  reported port conflict. One recorded run: port 56166, fixture PID 11720.
- An HTTP child kept its process alive while the dashboard request hung. The
  supervisor replaced it; then the replacement deliberately crashed and was
  replaced again. One recorded run: PID 13572 -> 14668 -> 20220.
- Stale-daemon stop refused a mismatched creation time and left that process
  alive; matching identity stopped the owned test fixture successfully.
- Repeated Windows integration run after final installer adjustments passed.

Initial sandbox runs could not access CIM (`0x80041003`); Windows integration
tests were rerun in the normal user session. The test fixture was also corrected
to remain hung for the whole first process and to accept Windows connection reset
as the expected result of an intentional crash. Final results above are from
the corrected tests, not the failed runs.

## Deployment evidence

Reinstalled the existing local server/Claude Startup launchers from this clone,
preserving the active Claude interval of **15 minutes** and trusted working
directory. No cache deletion or firmware upload was required.

- HTTP supervisor PID 25800, child PID 21788; authenticated `/healthz` returned
  `service=ai-usage-collector`, `pid=21788`, `status=ok`.
- `setup_server_autostart.ps1 -Status`: LauncherInstalled, SupervisorRunning,
  PortListening and HTTPHealthy all True.
- Claude daemon PID 952; refresh result: `exit_code=0`, `cache_advanced=true`,
  `timed_out=false`, `cache_after=1788659877`, `finished_at=1788659878`.
- Claude watchdog PID 33680, `daemon_healthy=true`, `consecutive_failures=0`.
- All four processes reported `MainWindowHandle=0` at inspection.
- A later authenticated dashboard request observed Claude timestamp 1788659877
  (age 105s) and Codex timestamp 1788659979.76 (age 2s), both `status=ok`,
  `stale=false`. Gemini remained unavailable.
- Source generation after reverse-file scanning measured about 0.27 seconds on
  this machine. This is a local sample, not a cross-machine benchmark.

## Not verified in this change

- E-paper display refresh on the physical device; no USB was connected for this
  work. Compatibility is based on the existing `status != ok` parser branch.
- Actual reboot/logoff/sleep-resume of the PC, or a long-duration soak test.
- An additional full 15-minute Claude cycle after deployment. The immediate
  production refresh succeeded; hung/crash recovery was exercised with fixtures.
- Visual desktop observation: window handles were checked, not a screen recording.

Plain HTTP on a controlled LAN remains the existing transport limitation.
