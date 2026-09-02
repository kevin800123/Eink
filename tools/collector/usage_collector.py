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

  claude  UNAVAILABLE. Verified 2026-09-02: no official API, CLI command or
          telemetry exposes subscription quota. Claude Code's OpenTelemetry
          surface is 8 metrics and 12 events, none of which carry quota, limits
          or reset times, and the desktop app caches nothing on disk. A record
          appears only at the moment a limit is actually hit.

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


def find_codex_rate_limits(sessions_dir, max_files=40):
    """Return (rate_limits_dict, error_code). Newest session file wins.

    Within a file the last record is kept, since that is the most recent turn.
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
                        latest = found
        except OSError:
            continue
        if latest is not None:
            return latest, None

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
    resets_at = window.get("resets_at")
    used = window.get("used_percent")
    if not isinstance(resets_at, (int, float)):
        return None
    if not isinstance(used, (int, float)):
        return None

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


def unavailable(provider_id, label, error_code):
    return {
        "id": provider_id,
        "label": label,
        "status": "unavailable",
        "error_code": error_code,
    }


def build_payload(sessions_dir):
    return {
        "schema_version": SCHEMA_VERSION,
        "generated_at": datetime.now(timezone.utc).astimezone().isoformat(),
        "providers": [
            unavailable("claude", "CLAUDE", "no_official_api"),
            build_codex_provider(sessions_dir),
            unavailable("gemini", "GEMINI", "no_official_api"),
        ],
    }


class Handler(BaseHTTPRequestHandler):
    server_version = "AIUsageCollector/0.1"
    token = ""
    sessions_dir = Path()
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
            Handler.cache = build_payload(Handler.sessions_dir)
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
        payload = build_payload(sessions_dir)
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
