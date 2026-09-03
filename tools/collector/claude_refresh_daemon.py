#!/usr/bin/env python3
"""Run refresh_claude.py now and then on a fixed cadence.

This process is intentionally launched with python.exe from a Startup-folder
VBScript using WScript window style 0. That keeps a real (hidden) console in the
logged-on user's interactive session, which pywinpty/ConPTY requires. Do not
replace python.exe with pythonw.exe and do not run this daemon as a Scheduled
Task.

The daemon is a supervisor only. It does not read or parse provider data. Each
refresh runs in a child process with a hard timeout, so one stuck ConPTY cannot
stop later cycles.
"""

import argparse
import json
import msvcrt
import os
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent
REFRESH_SCRIPT = HERE / "refresh_claude.py"

STATE_DIR = Path.home() / ".ai-usage-dashboard"
CACHE_FILE = STATE_DIR / "claude.json"
LOCK_FILE = STATE_DIR / "claude_refresh_daemon.lock"
STOP_FILE = STATE_DIR / "claude_refresh_daemon.stop"
STATUS_FILE = STATE_DIR / "claude_refresh_daemon_status.json"
LOG_FILE = STATE_DIR / "claude_refresh_daemon.log"
ROTATED_LOG_FILE = STATE_DIR / "claude_refresh_daemon.log.1"

DEFAULT_MINUTES = 30
DEFAULT_REFRESH_TIMEOUT_SECONDS = 120
MAX_LOG_BYTES = 1_000_000
POLL_SECONDS = 0.5


def now_epoch():
    return int(time.time())


def iso_now():
    return datetime.now(timezone.utc).astimezone().isoformat()


def atomic_json(path, payload):
    STATE_DIR.mkdir(parents=True, exist_ok=True)
    handle, tmp_path = tempfile.mkstemp(dir=str(STATE_DIR), suffix=".tmp")
    try:
        with os.fdopen(handle, "w", encoding="utf-8") as stream:
            json.dump(payload, stream, ensure_ascii=False)
        os.replace(tmp_path, path)
    except Exception:
        try:
            os.unlink(tmp_path)
        except OSError:
            pass
        raise


def append_log(message):
    STATE_DIR.mkdir(parents=True, exist_ok=True)
    rotate_log_if_needed()
    with LOG_FILE.open("a", encoding="utf-8") as stream:
        stream.write("[%s] %s\n" % (iso_now(), message))


def rotate_log_if_needed():
    try:
        if LOG_FILE.stat().st_size < MAX_LOG_BYTES:
            return
        ROTATED_LOG_FILE.unlink(missing_ok=True)
        LOG_FILE.replace(ROTATED_LOG_FILE)
    except OSError:
        pass


def cache_stamp():
    try:
        with CACHE_FILE.open("r", encoding="utf-8") as stream:
            value = json.load(stream).get("captured_at", 0)
        return int(value) if isinstance(value, (int, float)) else 0
    except (OSError, ValueError, TypeError):
        return 0


def acquire_singleton():
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


def terminate_tree(proc):
    if proc.poll() is not None:
        return
    creation_flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    try:
        subprocess.run(
            ["taskkill.exe", "/PID", str(proc.pid), "/T", "/F"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            creationflags=creation_flags,
            timeout=10,
            check=False,
        )
    except Exception:
        try:
            proc.kill()
        except OSError:
            pass


def run_refresh(timeout_seconds, claude_workdir):
    before = cache_stamp()
    started_at = now_epoch()
    append_log(
        "refresh start pid=%d python=%s before=%d"
        % (os.getpid(), sys.executable, before)
    )

    rotate_log_if_needed()
    with LOG_FILE.open("a", encoding="utf-8") as log:
        proc = subprocess.Popen(
            [sys.executable, str(REFRESH_SCRIPT)],
            cwd=str(claude_workdir),
            stdout=log,
            stderr=subprocess.STDOUT,
            env=os.environ.copy(),
        )
        deadline = time.monotonic() + timeout_seconds
        stopped = False
        timed_out = False
        while proc.poll() is None:
            if STOP_FILE.exists():
                stopped = True
                terminate_tree(proc)
                break
            if time.monotonic() >= deadline:
                timed_out = True
                terminate_tree(proc)
                break
            time.sleep(POLL_SECONDS)
        try:
            exit_code = proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            terminate_tree(proc)
            exit_code = -1

    after = cache_stamp()
    result = {
        "started_at": started_at,
        "finished_at": now_epoch(),
        "exit_code": exit_code,
        "cache_before": before,
        "cache_after": after,
        "cache_advanced": after > before,
        "timed_out": timed_out,
        "stop_requested": stopped,
    }
    append_log("refresh result %s" % json.dumps(result, separators=(",", ":")))
    return result


def write_status(status):
    try:
        atomic_json(STATUS_FILE, status)
    except OSError as exc:
        append_log("status write failed: %r" % exc)


def main():
    parser = argparse.ArgumentParser(description="Claude quota refresh supervisor")
    parser.add_argument("--minutes", type=int, default=DEFAULT_MINUTES)
    parser.add_argument(
        "--refresh-timeout-seconds",
        type=int,
        default=DEFAULT_REFRESH_TIMEOUT_SECONDS,
    )
    parser.add_argument(
        "--claude-workdir",
        type=Path,
        required=True,
        help="Existing Claude-trusted project directory used for the throwaway turn",
    )
    args = parser.parse_args()

    if args.minutes < 5:
        parser.error("--minutes must be at least 5")
    if args.refresh_timeout_seconds < 30:
        parser.error("--refresh-timeout-seconds must be at least 30")
    if not REFRESH_SCRIPT.is_file():
        sys.stderr.write("refresh script not found: %s\n" % REFRESH_SCRIPT)
        return 2
    claude_workdir = args.claude_workdir.expanduser().resolve()
    if not claude_workdir.is_dir():
        sys.stderr.write("Claude working directory not found: %s\n" % claude_workdir)
        return 2

    lock = acquire_singleton()
    if lock is None:
        return 0

    status = {}
    try:
        STOP_FILE.unlink(missing_ok=True)
        interval_seconds = args.minutes * 60
        status = {
            "running": True,
            "pid": os.getpid(),
            "python": sys.executable,
            "daemon": str(Path(__file__).resolve()),
            "claude_workdir": str(claude_workdir),
            "interval_minutes": args.minutes,
            "refresh_timeout_seconds": args.refresh_timeout_seconds,
            "started_at": now_epoch(),
        }
        write_status(status)
        append_log(
            "daemon started pid=%d interval_minutes=%d"
            % (os.getpid(), args.minutes)
        )

        next_run = time.monotonic()
        while not STOP_FILE.exists():
            while time.monotonic() < next_run and not STOP_FILE.exists():
                remaining = max(0, int(next_run - time.monotonic()))
                status["seconds_until_next_run"] = remaining
                status["next_run_at"] = now_epoch() + remaining
                write_status(status)
                time.sleep(min(1.0, max(0.1, next_run - time.monotonic())))

            if STOP_FILE.exists():
                break

            result = run_refresh(args.refresh_timeout_seconds, claude_workdir)
            status["last_refresh"] = result
            status["last_cache_captured_at"] = cache_stamp()
            next_run += interval_seconds
            if next_run <= time.monotonic():
                next_run = time.monotonic() + interval_seconds
            status["next_run_at"] = now_epoch() + int(
                max(0, next_run - time.monotonic())
            )
            write_status(status)

        status["running"] = False
        status["stopped_at"] = now_epoch()
        write_status(status)
        append_log("daemon stopped")
        return 0
    except Exception as exc:
        append_log("daemon crashed: %r" % exc)
        try:
            status["running"] = False
            status["crashed_at"] = now_epoch()
            status["error"] = repr(exc)
            write_status(status)
        except Exception:
            pass
        return 1
    finally:
        try:
            msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)
        except OSError:
            pass
        lock.close()


if __name__ == "__main__":
    raise SystemExit(main())
