"""Small, local runtime helpers; no provider credentials or network access."""
import json
import os
import tempfile
import time
from datetime import datetime
from pathlib import Path


def atomic_json(path, payload):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(dir=path.parent, suffix=".tmp")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            json.dump(payload, stream, ensure_ascii=False, allow_nan=False)
        for attempt in range(4):
            try:
                os.replace(temporary, path)
                break
            except PermissionError:
                if attempt == 3:
                    raise
                time.sleep(0.05 * (attempt + 1))
    finally:
        Path(temporary).unlink(missing_ok=True)


def log_event(path, message):
    """Bound diagnostics even for a permanently failing service."""
    path = Path(path)
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        if path.exists() and path.stat().st_size >= 1_000_000:
            rotated = path.with_suffix(path.suffix + ".1")
            rotated.unlink(missing_ok=True)
            path.replace(rotated)
        with path.open("a", encoding="utf-8") as stream:
            stream.write(f"[{datetime.now().astimezone().isoformat()}] {message}\n")
    except OSError:
        pass


def singleton(path):
    import msvcrt
    path.parent.mkdir(parents=True, exist_ok=True)
    handle = path.open("a+b")
    handle.seek(0, os.SEEK_END)
    if not handle.tell():
        handle.write(b"0")
        handle.flush()
    handle.seek(0)
    try:
        msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
        return handle
    except OSError:
        handle.close()
        return None


def restart_delay(failures):
    return min(300, 5 * (2 ** min(max(failures - 1, 0), 6)))
