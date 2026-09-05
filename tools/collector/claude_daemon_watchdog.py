#!/usr/bin/env python3
"""Restart the Claude refresh daemon if it dies (crash, or killed on PC sleep).

The daemon (claude_refresh_daemon.py) needs a real console for the ConPTY that
drives `claude`, so it is launched hidden via its Startup VBScript, not pythonw.
This watchdog needs no console, so it runs under pythonw. It watches the daemon's
status file — the daemon rewrites it about once a second while alive — and, when
that goes stale, re-runs the daemon's Startup VBScript to bring it back.
"""

import json
import os
import subprocess
import sys
import time
from pathlib import Path

STATE_DIR = Path.home() / ".ai-usage-dashboard"
STATUS_FILE = STATE_DIR / "claude_refresh_daemon_status.json"
LOCK_FILE = STATE_DIR / "claude_daemon_watchdog.lock"

# A dead daemon pid is detected immediately; STALE_SECONDS only guards against
# pid reuse (the daemon rewrites status ~1x/second, pausing at most ~120s during
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

    PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
    STILL_ACTIVE = 259
    handle = ctypes.windll.kernel32.OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION, False, int(pid)
    )
    if not handle:
        return False
    try:
        code = ctypes.c_ulong()
        ok = ctypes.windll.kernel32.GetExitCodeProcess(handle, ctypes.byref(code))
        return bool(ok) and code.value == STILL_ACTIVE
    finally:
        ctypes.windll.kernel32.CloseHandle(handle)


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

    try:
        while True:
            if not daemon_alive():
                relaunch_daemon()
                time.sleep(AFTER_RELAUNCH_GRACE)
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
