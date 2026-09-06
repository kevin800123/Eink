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
import math
import os
import sys
import threading
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
DEFAULT_MAX_AGE_SECONDS = 3600


def observation_epoch(value):
    if isinstance(value, str):
        try:
            parsed = datetime.fromisoformat(value.replace('Z', '+00:00'))
            return parsed.timestamp() if parsed.tzinfo else None
        except (ValueError, OverflowError, OSError):
            return None
    if type(value) in (int, float) and math.isfinite(value) and value > 0:
        return float(value)
    return None


def rate_limits_has_window(limits):
    """True if a rate_limits dict actually carries a usable window.

    When Codex has no active window to report (e.g. right after a limit is hit)
    it still writes a rate_limits object, but with primary and secondary both
    null. Such a record carries no window timing, so it cannot be aged by the
    rollover rule and must not be treated as the current state. We skip it and
    fall back to the most recent record that does carry a window, retaining
    that record's observation time. Expired windows are never inferred as 0%.
    """
    if not isinstance(limits, dict):
        return False
    return isinstance(limits.get("primary"), dict) or isinstance(
        limits.get("secondary"), dict
    )


def reverse_lines(path):
    """Read recent records first without rescanning entire large transcripts."""
    with path.open("rb") as stream:
        stream.seek(0, os.SEEK_END)
        position = stream.tell()
        pending = b""
        while position:
            size = min(position, 256 * 1024)
            position -= size
            stream.seek(position)
            pieces = (stream.read(size) + pending).split(b"\n")
            pending = pieces[0]
            for line in reversed(pieces[1:]):
                yield line.decode("utf-8", "replace")
        if pending:
            yield pending.decode("utf-8", "replace")


def find_codex_rate_limits(sessions_dir, max_files=40):
    """Return (limits, observed_at, error). Compare real record timestamps.

    Within a file the last record that carries a window is kept, since that is
    the most recent turn with real data. Null-shaped records (primary and
    secondary both null, written when there is no window to report) are skipped
    so a fresh limit-exhaustion turn does not blank the dashboard.
    """
    if not sessions_dir.is_dir():
        return None, None, "sessions_dir_missing"

    try:
        files = sorted(
            (p for p in sessions_dir.rglob("*.jsonl") if p.is_file()),
            key=lambda p: p.stat().st_mtime,
            reverse=True,
        )[:max_files]
    except OSError as exc:
        return None, None, "scan_failed_" + exc.__class__.__name__

    if not files:
        return None, None, "no_session_files"

    saw_record = False
    best = None
    best_stamp = None
    for path in files:
        try:
            records = reverse_lines(path)
            try:
                for line in records:
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
                            stamp = observation_epoch(record.get("timestamp"))
                            if stamp is not None and (best_stamp is None or stamp >= best_stamp):
                                best, best_stamp = found, stamp
                            if stamp is not None:
                                break  # last usable record in this append-only file
            finally:
                records.close()
        except OSError:
            continue
    if best is not None:
        return best, best_stamp, None

    # Records existed but none carried a window: the account has no window to
    # report (typically an idle window that has since rolled over).
    if saw_record:
        return None, None, "no_timestamped_window_data"
    return None, None, "no_rate_limit_record"


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
    """Normalize an observed window. A window past its reset has rolled over, so
    its usage is 0 (a fresh window), not unknown — this keeps the dashboard
    showing a real number to glance at instead of N/A when a provider has been
    idle."""
    if not isinstance(window, dict):
        return None
    used = window.get("used_percent")
    if type(used) not in (int, float) or not math.isfinite(used) or not 0 <= used <= 100:
        return None

    resets_at = observation_epoch(window.get("resets_at"))
    if resets_at is None:
        return None
    try:
        reset_iso = datetime.fromtimestamp(resets_at, timezone.utc).isoformat()
    except (ValueError, OverflowError, OSError):
        return None
    expired = resets_at <= now_ts
    return {
        "window_minutes": window.get("window_minutes"),
        "used_percent": 0 if expired else int(round(used)),
        "reset_at": reset_iso,
        "rolled_over": expired,
    }


def observed_provider(provider_id, label, five, week, observed_at, now_ts, max_age):
    """Old firmware has provider-level availability only: fail closed as a row."""
    stamp = observation_epoch(observed_at)
    age = None if stamp is None else max(0, int(now_ts - stamp))
    error = None
    # Only genuinely missing/broken data makes a provider unavailable. Staleness
    # and a rolled-over window are NOT unavailability: we still show the last
    # numbers (with rolled-over windows at 0), and expose `stale`/`age_seconds`
    # so a stale reading can be flagged rather than hidden.
    if stamp is None or stamp > now_ts + 60:
        error = "invalid_observation_time"
    elif five is None or week is None:
        error = "incomplete_windows"
    stale = bool(stamp is not None and now_ts - stamp > max_age)
    if error:
        result = unavailable(provider_id, label, error)
    else:
        result = {
            "id": provider_id, "label": label, "status": "ok",
            "usage_percent": five["used_percent"], "reset_at": five["reset_at"],
            "windows": {"five_hour": five, "weekly": week},
        }
    result.update(observed_at=stamp, age_seconds=age, stale=stale, max_age_seconds=max_age)
    return result


def build_codex_provider(sessions_dir, max_age=DEFAULT_MAX_AGE_SECONDS):
    now_ts = time.time()
    limits, observed_at, error = find_codex_rate_limits(sessions_dir)
    if limits is None:
        return unavailable("codex", "CODEX", error or "unknown")

    primary = window_state(limits.get("primary"), now_ts)
    secondary = window_state(limits.get("secondary"), now_ts)
    if primary is None and secondary is None:
        return unavailable("codex", "CODEX", "malformed_rate_limits")

    provider = observed_provider("codex", "CODEX", primary, secondary, observed_at, now_ts, max_age)
    provider["plan_type"] = limits.get("plan_type")
    return provider


def build_claude_provider(cache_path, max_age=DEFAULT_MAX_AGE_SECONDS):
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

    provider = observed_provider("claude", "CLAUDE", five_hour, seven_day,
                                 cached.get("captured_at"), now_ts, max_age)
    provider["captured_at"] = provider["observed_at"]
    return provider


def unavailable(provider_id, label, error_code):
    return {
        "id": provider_id,
        "label": label,
        "status": "unavailable",
        "error_code": error_code,
    }


def build_payload(sessions_dir, claude_cache, max_age=DEFAULT_MAX_AGE_SECONDS):
    return {
        "schema_version": SCHEMA_VERSION,
        "generated_at": datetime.now(timezone.utc).astimezone().isoformat(),
        "providers": [
            build_claude_provider(claude_cache, max_age),
            build_codex_provider(sessions_dir, max_age),
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
    max_age_seconds = DEFAULT_MAX_AGE_SECONDS
    cache_lock = threading.Lock()

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
        route = self.path.split("?")[0]
        if route not in ("/v1/dashboard", "/healthz"):
            self.send_json(404, {"error": "not_found"})
            return

        if Handler.token:
            if self.headers.get("Authorization") != "Bearer " + Handler.token:
                self.send_json(401, {"error": "unauthorized"})
                return

        # Health is independent of provider freshness. An idle Codex or offline
        # Claude must not trigger an endless HTTP-server restart loop.
        if route == "/healthz":
            self.send_json(200, {"service": "ai-usage-collector", "pid": os.getpid(), "status": "ok"})
            return
        with Handler.cache_lock:
            now = time.monotonic()
            if Handler.cache is None or now - Handler.cache_at >= Handler.cache_seconds:
                try:
                    Handler.cache = build_payload(Handler.sessions_dir, Handler.claude_cache, Handler.max_age_seconds)
                except Exception:
                    self.send_json(503, {"error": "source_read_failed"})
                    return
                Handler.cache_at = now
            payload = Handler.cache
        self.send_json(200, payload)


def _ensure_std_streams():
    # Under pythonw.exe there is no console, so sys.stdout/sys.stderr are None
    # and any write to them raises. The scheduled/hidden launch uses pythonw, so
    # redirect them to the null device to keep logging calls harmless.
    if sys.stderr is None:
        sys.stderr = open(os.devnull, "w", encoding="utf-8")
    if sys.stdout is None:
        sys.stdout = open(os.devnull, "w", encoding="utf-8")


def main():
    _ensure_std_streams()
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
        "--max-age-seconds", type=int, default=DEFAULT_MAX_AGE_SECONDS,
        help="Maximum source observation age; older data is unavailable",
    )
    parser.add_argument(
        "--once",
        action="store_true",
        help="Print the payload once and exit instead of serving",
    )
    args = parser.parse_args()
    if args.max_age_seconds < 60 or args.cache_seconds < 0:
        parser.error("max age must be >=60 and cache seconds must be >=0")

    sessions_dir = Path(args.sessions_dir)

    if args.once:
        payload = build_payload(sessions_dir, Path(args.claude_cache), args.max_age_seconds)
        print(json.dumps(payload, ensure_ascii=False, indent=2))
        return 0

    token = os.environ.get("AI_DASH_DEVICE_TOKEN", "")
    if not token:
        # Fall back to token.local next to this script so a scheduled task can
        # run the server (e.g. via pythonw) without setting an env var.
        token_file = Path(__file__).resolve().parent / "token.local"
        try:
            token = token_file.read_text(encoding="utf-8").strip()
        except OSError:
            token = ""
    if not token:
        sys.stderr.write(
            "Refusing to start without a token (AI_DASH_DEVICE_TOKEN env or "
            "token.local). The collector listens on the LAN, so an "
            "unauthenticated endpoint would expose your usage to anyone on the "
            "network.\n"
        )
        return 2

    Handler.token = token
    Handler.sessions_dir = sessions_dir
    Handler.claude_cache = Path(args.claude_cache)
    Handler.cache_seconds = args.cache_seconds
    Handler.max_age_seconds = args.max_age_seconds

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
