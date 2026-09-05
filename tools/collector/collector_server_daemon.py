#!/usr/bin/env python3
"""Keep the collector HTTP server running (restart it if it ever dies).

Task Scheduler needs admin to register and the collector is a plain HTTP server,
so instead of a task this small supervisor is launched at logon by a hidden
Startup-folder VBScript (see setup_server_autostart.ps1), exactly like the Claude
refresh daemon that has proven reliable across sleeps. It starts
usage_collector.py as a child, waits, and restarts it if it exits (crash, or
killed during PC sleep). No console window: launched via pythonw and the child
is started with CREATE_NO_WINDOW.

usage_collector.py reads token.local itself, so no environment variable is
needed here.
"""

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
SERVER = HERE / "usage_collector.py"
STATE_DIR = Path.home() / ".ai-usage-dashboard"
LOCK_FILE = STATE_DIR / "collector_server_daemon.lock"

CREATE_NO_WINDOW = 0x08000000
RESTART_DELAY_S = 5.0


def acquire_singleton():
    # Prevent a second supervisor (and thus a port clash) if launched twice.
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
    # Under pythonw there is no console; keep any error writes harmless.
    if sys.stderr is None:
        sys.stderr = open(os.devnull, "w", encoding="utf-8")
    if sys.stdout is None:
        sys.stdout = open(os.devnull, "w", encoding="utf-8")

    parser = argparse.ArgumentParser(description="Collector server supervisor")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8770)
    args = parser.parse_args()

    if not SERVER.is_file():
        sys.stderr.write("collector not found: %s\n" % SERVER)
        return 2

    lock = acquire_singleton()
    if lock is None:
        # Another supervisor already owns it.
        return 0

    python = sys.executable  # pythonw.exe when launched hidden; fine for a server
    argv = [python, str(SERVER), "--host", args.host, "--port", str(args.port)]

    try:
        while True:
            try:
                proc = subprocess.Popen(
                    argv, cwd=str(HERE), creationflags=CREATE_NO_WINDOW
                )
                proc.wait()  # blocks until the server exits or is killed
            except Exception:
                pass
            # Server exited (crash / killed during sleep) or failed to bind;
            # pause so a bind failure does not spin, then start it again.
            time.sleep(RESTART_DELAY_S)
    finally:
        try:
            import msvcrt

            msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)
        except OSError:
            pass
        lock.close()


if __name__ == "__main__":
    raise SystemExit(main())
