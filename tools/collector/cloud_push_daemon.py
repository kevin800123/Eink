#!/usr/bin/env python3
"""Push the latest usage snapshot to the private cloud relay on a schedule.

The relay (cloud/worker.js) keeps serving the last snapshot to the ESP32 even
while the PC is asleep/off, so the dashboard no longer needs the PC awake or a
LAN path. This pusher builds the same sanitized payload usage_collector serves
locally (only percentages, reset timestamps and collection times) and POSTs it
to the relay's /api/usage with the ingest token. No provider credential leaves
the machine.

Config: cloud_push.local.json next to this file (gitignored):
    {"ingest_url": "https://<worker-host>/api/usage", "ingest_token": "..."}
or the env vars AI_DASH_CLOUD_INGEST_URL / AI_DASH_CLOUD_INGEST_TOKEN.

Launched hidden at logon via pythonw by setup_cloud_push.ps1.
"""

import json
import os
import sys
import time
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
STATE_DIR = Path.home() / ".ai-usage-dashboard"
LOCK_FILE = STATE_DIR / "cloud_push_daemon.lock"
CONFIG_FILE = HERE / "cloud_push.local.json"

DEFAULT_INTERVAL_S = 720  # 12 minutes, matching the device cadence
ERROR_RETRY_S = 120


def load_config():
    url = os.environ.get("AI_DASH_CLOUD_INGEST_URL", "")
    token = os.environ.get("AI_DASH_CLOUD_INGEST_TOKEN", "")
    if (not url or not token) and CONFIG_FILE.is_file():
        try:
            cfg = json.loads(CONFIG_FILE.read_text(encoding="utf-8"))
            url = url or str(cfg.get("ingest_url", ""))
            token = token or str(cfg.get("ingest_token", ""))
        except (OSError, ValueError):
            pass
    return url.strip(), token.strip()


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


def push_once(url, token):
    import usage_collector

    sessions_dir = Path.home() / ".codex" / "sessions"
    payload = usage_collector.build_payload(sessions_dir, usage_collector.CLAUDE_CACHE_DEFAULT)
    data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(
        url,
        data=data,
        method="POST",
        headers={"Authorization": "Bearer " + token, "Content-Type": "application/json"},
    )
    with urllib.request.urlopen(req, timeout=15) as resp:
        return resp.status


def main():
    if sys.stderr is None:
        sys.stderr = open(os.devnull, "w", encoding="utf-8")
    if sys.stdout is None:
        sys.stdout = open(os.devnull, "w", encoding="utf-8")

    sys.path.insert(0, str(HERE))
    interval = DEFAULT_INTERVAL_S
    if len(sys.argv) > 1:
        try:
            interval = max(60, int(sys.argv[1]))
        except ValueError:
            pass

    lock = acquire_singleton()
    if lock is None:
        return 0

    try:
        while True:
            url, token = load_config()
            slept = interval
            if url and token:
                try:
                    push_once(url, token)
                except Exception:
                    slept = ERROR_RETRY_S  # transient (offline / relay down); retry sooner
            else:
                slept = ERROR_RETRY_S  # not configured yet
            time.sleep(slept)
    finally:
        try:
            import msvcrt

            msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)
        except OSError:
            pass
        lock.close()


if __name__ == "__main__":
    raise SystemExit(main())
