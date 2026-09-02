#!/usr/bin/env python3
"""Claude Code status line that also records subscription rate limits.

Claude Code passes session JSON on stdin to whatever command is configured as
statusLine. That payload is the only officially documented place where the
Claude.ai subscription quota appears:

    rate_limits.five_hour.used_percentage   0-100
    rate_limits.five_hour.resets_at         unix epoch seconds
    rate_limits.seven_day.used_percentage   0-100
    rate_limits.seven_day.resets_at         unix epoch seconds

Per the docs the object appears only for Claude.ai Pro and Max subscribers, only
after the first API response in a session, and Claude Code drops a window once
its resets_at has passed. Each field is therefore treated as optional.

This script does two jobs:
  1. writes the rate limits to a small cache file the collector can read
  2. prints a short status line, because that is what statusLine is for

Configure it in ~/.claude/settings.json:

    "statusLine": {
      "type": "command",
      "command": "python C:/path/to/tools/collector/claude_statusline.py"
    }
"""

import json
import os
import sys
import tempfile
import time
from pathlib import Path

CACHE_DIR = Path.home() / ".ai-usage-dashboard"
CACHE_FILE = CACHE_DIR / "claude.json"


def write_cache(payload):
    """Write atomically so the collector never reads a half-written file."""
    try:
        CACHE_DIR.mkdir(parents=True, exist_ok=True)
        handle, tmp_path = tempfile.mkstemp(dir=str(CACHE_DIR), suffix=".tmp")
        with os.fdopen(handle, "w", encoding="utf-8") as tmp:
            json.dump(payload, tmp, ensure_ascii=False)
        os.replace(tmp_path, CACHE_FILE)
    except OSError:
        # A status line must never break the session over a cache write.
        pass


def window(rate_limits, name):
    value = rate_limits.get(name)
    if not isinstance(value, dict):
        return None
    used = value.get("used_percentage")
    resets_at = value.get("resets_at")
    if not isinstance(used, (int, float)):
        return None
    entry = {"used_percent": max(0, min(100, int(round(used))))}
    if isinstance(resets_at, (int, float)):
        entry["resets_at"] = int(resets_at)
    return entry


def short_reset(entry):
    if not entry or "resets_at" not in entry:
        return ""
    remaining = entry["resets_at"] - time.time()
    if remaining <= 0:
        return ""
    hours = int(remaining // 3600)
    minutes = int((remaining % 3600) // 60)
    if hours >= 24:
        return " %dd%dh" % (hours // 24, hours % 24)
    return " %dh%02dm" % (hours, minutes)


def main():
    try:
        data = json.load(sys.stdin)
    except (ValueError, OSError):
        return 0

    rate_limits = data.get("rate_limits")
    if not isinstance(rate_limits, dict):
        rate_limits = {}

    five_hour = window(rate_limits, "five_hour")
    seven_day = window(rate_limits, "seven_day")

    # Only overwrite the cache when a window is actually present. Claude Code
    # omits rate_limits before the first API response, and writing an empty
    # record then would erase a perfectly good earlier reading.
    if five_hour or seven_day:
        payload = {"captured_at": int(time.time())}
        if five_hour:
            payload["five_hour"] = five_hour
        if seven_day:
            payload["seven_day"] = seven_day
        model = data.get("model")
        if isinstance(model, dict) and model.get("display_name"):
            payload["model"] = model["display_name"]
        write_cache(payload)

    parts = []
    model = data.get("model")
    if isinstance(model, dict) and model.get("display_name"):
        parts.append(str(model["display_name"]))

    context = data.get("context_window")
    if isinstance(context, dict):
        used = context.get("used_percentage")
        if isinstance(used, (int, float)):
            parts.append("ctx %d%%" % int(used))

    if five_hour:
        parts.append("5h %d%%%s" % (five_hour["used_percent"], short_reset(five_hour)))
    if seven_day:
        parts.append("wk %d%%%s" % (seven_day["used_percent"], short_reset(seven_day)))

    sys.stdout.write(" | ".join(parts) if parts else "")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
