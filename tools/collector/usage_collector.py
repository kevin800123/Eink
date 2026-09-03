#!/usr/bin/env python3
"""Local usage collector for the AI Usage Dashboard.

Serves the normalized JSON described in docs/API_CONTRACT.md so the ESP32 never
needs a provider credential of any kind.

Data sources, and why they are what they are:

  codex   REAL. The official Codex CLI writes a rate_limits object into
          ~/.codex/sessions/YYYY/MM/DD/rollout-*.jsonl on every turn, holding
          used_percent, window_minutes and resets_at for a 5-hour (300 minute)
          primary window and a weekly (10080 minute) secondary window. Reading
          it needs no credential and makes no network call.

  claude  REAL, once tools/collector/claude_statusline.py is configured as the
          Claude Code statusLine command. Claude Code passes rate_limits on
          stdin to that command, carrying five_hour.used_percentage,
          seven_day.used_percentage and their resets_at. This is the only
          officially documented place the Claude.ai subscription quota appears;
          it is offered to Pro and Max subscribers after the first API response
          of a session. Neither the OpenTelemetry surface (8 metrics, 12 events)
          nor any CLI or API exposes it, and the desktop app caches nothing.

  gemini  UNAVAILABLE. Antigravity IDE does display quota, but it stores nothing
          on disk and its local language server requires an auth token for the
          relevant RPC. Not reverse engineered here.

Never invent a number for an unavailable provider. The contract requires a
status of unavailable plus an error_code instead.
"""

import argparse
import json
import os
import sys
import time
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

SCHEMA_VERSION = 1

RATE_LIMITS_KEY = "rate_limits"
RATE_LIMITS_MARKER = '"' + RATE_LIMITS_KEY + '"'

# Written by tools/collector/claude_statusline.py from the documented
# statusLine payload. See build_claude_provider.
CLAUDE_CACHE_DEFAULT = Path.home() / ".ai-usage-dashboard" / "claude.json"

WINDOW_FIVE_HOUR = 300
WINDOW_WEEKLY = 10080


def rate_limits_has_window(limits):
    """True if a rate_limits dict actually carries a usable window.

    When Codex has no active window to report (e.g. right after a limit is hit)
    it still writes a rate_limits object, but with primary and secondary both
    null. Such a record carries no window timing, so it cannot be aged by the
    rollover rule and must not be treated as the current state. We skip it and
    fall back to the most recent record that does carry a window; the rollover
    rule then reports 0 once that window's resets_at has passed.
    """
    if not isinstance(limits, dict):
        return False
    return isinstance(limits.get("primary"), dict) or isinstance(
        limits.get("secondary"), dict
    )


def find_codex_rate_limits(sessions_dir, max_files=40):
    """Return (rate_limits_dict, error_code). Newest usable record wins.

    Within a file the last record that carries a window is kept, since that is
    the most recent turn with real data. Null-shaped records (primary and
    secondary both null, written when there is no window to report) are skipped
    so a fresh limit-exhaustion turn does not blank the dashboard.
    """
    if not sessions_dir.is_dir():
        return None, "sessions_dir_missing"

    try:
        files = sorted(
            (p for p in sessions_dir.rglob("*.jsonl") if p.is_file()),
            key=lambda p: p.stat().st_mtime,
            reverse=True,
        )[:max_files]
    except OSError as exc:
        return None, "scan_failed_" + exc.__class__.__name__

    if not files:
        return None, "no_session_files"

    saw_record = False
    for path in files:
        latest = None
        try:
            with path.open("r", encoding="utf-8", errors="replace") as handle:
                for line in handle:
                    if RATE_LIMITS_MARKER not in line:
                        continue
                    try:
                        record = json.loads(line)
                    except ValueError:
                        continue
                    found = extract_rate_limits(record)
                    if found is not None:
                        saw_record = True
                        if rate_limits_has_window(found):
                            latest = found
        except OSError:
            continue
        if latest is not None:
            return latest, None

    # Records existed but none carried a window: the account has no window to
    # report (typically an idle window that has since rolled over).
    if saw_record:
        return None, "no_window_data"
    return None, "no_rate_limit_record"


def extract_rate_limits(node):
    """Depth-first search for a dict stored under the rate_limits key."""
    if isinstance(node, dict):
        value = node.get(RATE_LIMITS_KEY)
        if isinstance(value, dict):
            return value
        for child in node.values():
            found = extract_rate_limits(child)
            if found is not None:
                return found
    elif isinstance(node, list):
        for child in node:
            found = extract_rate_limits(child)
            if found is not None:
                return found
    return None


def window_state(window, now_ts):
    """Normalize one window and apply the rollover rule.

    A window whose resets_at has already passed has rolled over, so its recorded
    usage no longer applies and the correct value is 0. This is what makes stale
    session data safe to read: staleness corrects itself instead of lying.
    """
    if not isinstance(window, dict):
        return None
    used = window.get("used_percent")
    if not isinstance(used, (int, float)):
        return None

    resets_at = window.get("resets_at")
    if not isinstance(resets_at, (int, float)):
        # A percentage with no reset time is still real, it just cannot be aged
        # out. Report it rather than discarding it, and say the reset is unknown.
        return {
            "window_minutes": window.get("window_minutes"),
            "used_percent": max(0, min(100, int(round(used)))),
            "reset_at": None,
            "rolled_over": False,
        }

    expired = resets_at <= now_ts
    return {
        "window_minutes": window.get("window_minutes"),
        "used_percent": 0 if expired else max(0, min(100, int(round(used)))),
        "reset_at": datetime.fromtimestamp(resets_at).astimezone().isoformat(),
        "rolled_over": expired,
    }


def build_codex_provider(sessions_dir):
    now_ts = time.time()
    limits, error = find_codex_rate_limits(sessions_dir)
    if limits is None:
        return unavailable("codex", "CODEX", error or "unknown")

    primary = window_state(limits.get("primary"), now_ts)
    secondary = window_state(limits.get("secondary"), now_ts)
    if primary is None and secondary is None:
        return unavailable("codex", "CODEX", "malformed_rate_limits")

    # The 5-hour window is the constraint that blocks work soonest, so it is the
    # headline number. The weekly window travels alongside it so the firmware can
    # switch to it later without a collector change.
    headline = primary or secondary
    provider = {
        "id": "codex",
        "label": "CODEX",
        "status": "ok",
        "usage_percent": headline["used_percent"],
        "reset_at": headline["reset_at"],
        "plan_type": limits.get("plan_type"),
        "windows": {},
    }
    if primary is not None:
        provider["windows"]["five_hour"] = primary
    if secondary is not None:
        provider["windows"]["weekly"] = secondary
    return provider


def build_claude_provider(cache_path):
    """Claude quota comes from the documented statusLine payload.

    tools/collector/claude_statusline.py captures rate_limits.five_hour and
    rate_limits.seven_day, which Claude Code supplies on stdin to whatever
    command is configured as statusLine. That is the only officially documented
    place the Claude.ai subscription quota is exposed. It is present only for
    Pro and Max subscribers and only after the first API response in a session,
    so absence is normal and is reported rather than guessed at.
    """
    if not cache_path.is_file():
        # Either the statusLine is not configured yet, or no Claude Code session
        # has produced an API response since it was. Both are normal states.
        return unavailable("claude", "CLAUDE", "awaiting_statusline_data")

    try:
        with cache_path.open("r", encoding="utf-8") as handle:
            cached = json.load(handle)
    except (OSError, ValueError):
        return unavailable("claude", "CLAUDE", "cache_unreadable")

    if not isinstance(cached, dict):
        return unavailable("claude", "CLAUDE", "cache_malformed")

    now_ts = time.time()
    five_hour = window_state(cached.get("five_hour"), now_ts)
    seven_day = window_state(cached.get("seven_day"), now_ts)
    if five_hour is None and seven_day is None:
        return unavailable("claude", "CLAUDE", "no_rate_limit_record")

    if five_hour is not None:
        five_hour["window_minutes"] = WINDOW_FIVE_HOUR
    if seven_day is not None:
        seven_day["window_minutes"] = WINDOW_WEEKLY

    headline = five_hour or seven_day
    provider = {
        "id": "claude",
        "label": "CLAUDE",
        "status": "ok",
        "usage_percent": headline["used_percent"],
        "reset_at": headline["reset_at"],
        "captured_at": cached.get("captured_at"),
        "windows": {},
    }
    if five_hour is not None:
        provider["windows"]["five_hour"] = five_hour
    if seven_day is not None:
        provider["windows"]["weekly"] = seven_day
    return provider


def unavailable(provider_id, label, error_code):
    return {
        "id": provider_id,
        "label": label,
        "status": "unavailable",
        "error_code": error_code,
    }


def build_payload(sessions_dir, claude_cache):
    return {
        "schema_version": SCHEMA_VERSION,
        "generated_at": datetime.now(timezone.utc).astimezone().isoformat(),
        "providers": [
            build_claude_provider(claude_cache),
            build_codex_provider(sessions_dir),
            unavailable("gemini", "GEMINI", "no_official_api"),
        ],
    }


class Handler(BaseHTTPRequestHandler):
    server_version = "AIUsageCollector/0.1"
    token = ""
    sessions_dir = Path()
    claude_cache = CLAUDE_CACHE_DEFAULT
    cache_seconds = 60
    cache = None
    cache_at = 0.0

    def log_message(self, fmt, *args):
        sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))

    def send_json(self, code, body):
        raw = json.dumps(body, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(raw)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(raw)

    def do_GET(self):
        if self.path.split("?")[0] != "/v1/dashboard":
            self.send_json(404, {"error": "not_found"})
            return

        if Handler.token:
            if self.headers.get("Authorization") != "Bearer " + Handler.token:
                self.send_json(401, {"error": "unauthorized"})
                return

        now = time.time()
        if Handler.cache is None or now - Handler.cache_at >= Handler.cache_seconds:
            Handler.cache = build_payload(Handler.sessions_dir, Handler.claude_cache)
            Handler.cache_at = now
        self.send_json(200, Handler.cache)


def main():
    parser = argparse.ArgumentParser(description="AI Usage Dashboard collector")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8770)
    parser.add_argument(
        "--sessions-dir",
        default=str(Path.home() / ".codex" / "sessions"),
        help="Codex session directory to read",
    )
    parser.add_argument(
        "--claude-cache",
        default=str(CLAUDE_CACHE_DEFAULT),
        help="File written by claude_statusline.py",
    )
    parser.add_argument(
        "--cache-seconds",
        type=int,
        default=60,
        help="Minimum seconds between rescans of the session files",
    )
    parser.add_argument(
        "--once",
        action="store_true",
        help="Print the payload once and exit instead of serving",
    )
    args = parser.parse_args()

    sessions_dir = Path(args.sessions_dir)

    if args.once:
        payload = build_payload(sessions_dir, Path(args.claude_cache))
        print(json.dumps(payload, ensure_ascii=False, indent=2))
        return 0

    token = os.environ.get("AI_DASH_DEVICE_TOKEN", "")
    if not token:
        sys.stderr.write(
            "Refusing to start without AI_DASH_DEVICE_TOKEN set. The collector "
            "listens on the LAN, so an unauthenticated endpoint would expose "
            "your usage to anyone on the network.\n"
        )
        return 2

    Handler.token = token
    Handler.sessions_dir = sessions_dir
    Handler.claude_cache = Path(args.claude_cache)
    Handler.cache_seconds = args.cache_seconds

    server = ThreadingHTTPServer((args.host, args.port), Handler)
    sys.stderr.write(
        "serving GET /v1/dashboard on %s:%d, reading %s\n"
        % (args.host, args.port, sessions_dir)
    )
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        sys.stderr.write("stopping\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
