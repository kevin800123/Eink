#!/usr/bin/env python3
"""Restart the Claude refresh daemon if it dies (crash, or killed on PC sleep).

The daemon (claude_refresh_daemon.py) needs a real console for the ConPTY that
drives `claude`, so it is launched hidden via its Startup VBScript, not pythonw.
This watchdog needs no console, so it runs under pythonw. It watches the daemon's
status file — the daemon rewrites it every 30 seconds while idle — and, when
that goes stale, re-runs the daemon's Startup VBScript to bring it back.
"""

import json
import os
import subprocess
import sys
import time
from pathlib import Path
from runtime_support import atomic_json, log_event, restart_delay

HERE = Path(__file__).resolve().parent
STATE_DIR = Path.home() / ".ai-usage-dashboard"
STATUS_FILE = STATE_DIR / "claude_refresh_daemon_status.json"
LOCK_FILE = STATE_DIR / "claude_daemon_watchdog.lock"
WATCHDOG_STATUS = STATE_DIR / "claude_watchdog_status.json"
LOG_FILE = STATE_DIR / "claude_watchdog.log"

# A dead daemon pid is detected immediately; STALE_SECONDS only guards against
# pid reuse (the daemon rewrites status every 30s, pausing at most ~120s during
# a refresh, so a live daemon's status is never 5 minutes old).
STALE_SECONDS = 300
CHECK_SECONDS = 30
AFTER_RELAUNCH_GRACE = 30
CREATE_NO_WINDOW = 0x08000000


def daemon_launcher():
    appdata = os.environ.get("APPDATA")
    if not appdata:
        return None
    return (
        Path(appdata)
        / "Microsoft"
        / "Windows"
        / "Start Menu"
        / "Programs"
        / "Startup"
        / "AI Usage Dashboard - Claude Refresh.vbs"
    )


def pid_is_running(pid):
    import ctypes
    from ctypes import wintypes

    PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
    STILL_ACTIVE = 259
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = kernel.OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION, False, int(pid)
    )
    if not handle:
        return False
    try:
        code = ctypes.c_ulong()
        ok = kernel.GetExitCodeProcess(handle, ctypes.byref(code))
        return bool(ok) and code.value == STILL_ACTIVE
    finally:
        kernel.CloseHandle(handle)


def stop_stale_daemon():
    """PowerShell verifies exact script, executable and process creation time."""
    try:
        if not STATUS_FILE.exists():
            return True  # singleton makes relaunch safe when no PID is known
        status = json.loads(STATUS_FILE.read_text(encoding="utf-8"))
        pid = status.get("pid")
        if not pid or not pid_is_running(pid):
            return True
        if time.time() - STATUS_FILE.stat().st_mtime < STALE_SECONDS:
            return False  # heartbeat recovered before action
        powershell = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32/WindowsPowerShell/v1.0/powershell.exe"
        result = subprocess.run(
            [str(powershell), "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
             str(HERE / "process_control.ps1"), "-StopScript", str(HERE / "claude_refresh_daemon.py"),
             "-ExpectedPid", str(pid), "-ExpectedStartEpoch", str(status.get("started_at", 0))],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            creationflags=CREATE_NO_WINDOW, timeout=30,
        )
        return result.returncode == 0
    except (OSError, ValueError, subprocess.SubprocessError):
        return False


def daemon_alive():
    try:
        status = json.loads(STATUS_FILE.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return False
    pid = status.get("pid")
    if not pid or not pid_is_running(pid):
        return False  # daemon process is gone -> restart it
    # pid is alive; guard against pid reuse by requiring a recently updated file.
    try:
        age = time.time() - STATUS_FILE.stat().st_mtime
    except OSError:
        return False
    return age < STALE_SECONDS


def relaunch_daemon():
    vbs = daemon_launcher()
    if not vbs or not vbs.is_file():
        return
    wscript = os.path.join(
        os.environ.get("SystemRoot", r"C:\Windows"), "System32", "wscript.exe"
    )
    try:
        subprocess.Popen([wscript, str(vbs)], creationflags=CREATE_NO_WINDOW)
    except Exception:
        pass


def acquire_singleton():
    import msvcrt

    STATE_DIR.mkdir(parents=True, exist_ok=True)
    handle = LOCK_FILE.open("a+b")
    handle.seek(0, os.SEEK_END)
    if handle.tell() == 0:
        handle.write(b"0")
        handle.flush()
    handle.seek(0)
    try:
        msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
    except OSError:
        handle.close()
        return None
    return handle


def main():
    if sys.stderr is None:
        sys.stderr = open(os.devnull, "w", encoding="utf-8")
    if sys.stdout is None:
        sys.stdout = open(os.devnull, "w", encoding="utf-8")

    lock = acquire_singleton()
    if lock is None:
        return 0  # another watchdog already running

    failures = 0
    restarts = 0
    next_restart = 0.0
    try:
        while True:
            healthy = daemon_alive()
            failures = 0 if healthy else failures + 1
            # Two observations let the daemon recover its heartbeat after PC
            # resume or a transient file-sharing error before we terminate it.
            error = None
            if failures >= 2 and time.monotonic() >= next_restart:
                if stop_stale_daemon():
                    relaunch_daemon()
                    restarts += 1
                    delay = max(AFTER_RELAUNCH_GRACE, restart_delay(failures))
                    next_restart = time.monotonic() + delay
                    log_event(LOG_FILE, f"daemon_relaunched restart_count={restarts}")
                else:
                    error = "identity_unverified_or_heartbeat_recovered"
                    next_restart = time.monotonic() + 60
                    log_event(LOG_FILE, error)
            try:
                atomic_json(WATCHDOG_STATUS, {"pid": os.getpid(), "heartbeat_at": int(time.time()),
                    "daemon_healthy": healthy, "restart_count": restarts,
                    "consecutive_failures": failures, "error": error})
            except OSError:
                pass
            time.sleep(CHECK_SECONDS)
    finally:
        try:
            import msvcrt

            msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)
        except OSError:
            pass
        lock.close()


if __name__ == "__main__":
    raise SystemExit(main())
