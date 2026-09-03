#!/usr/bin/env python3
"""Refresh the Claude quota cache by briefly driving an interactive session.

Why this is needed: the Claude.ai subscription quota (used_percentage) is only
ever exposed through the statusLine payload, and statusLine only runs in a real
interactive session with a pseudo console. The Desktop app, `claude -p`, piped
stdin, transcripts and telemetry all lack the percentage. `claude -p` with
stream-json carries only rate_limit_info (status and resets_at, no percentage).

So to get the percentage headlessly we spawn `claude` inside a ConPTY via
pywinpty, send one short prompt, and wait for statusLine to write the cache. The
returned rate_limits are account-wide, so this reflects Desktop usage too, since
each run makes a fresh API call.

Cost: one very small API call per run. Schedule it no more often than you need.

Success is detected by watching the cache file's captured_at timestamp rather
than by scraping terminal output, which keeps this robust against display
changes. Run it on a schedule (see setup_schedule.ps1).
"""

import json
import os
import sys
import time
from pathlib import Path

CACHE_DIR = Path.home() / ".ai-usage-dashboard"
CACHE_FILE = CACHE_DIR / "claude.json"
HEARTBEAT_FILE = CACHE_DIR / "statusline_heartbeat.json"

# A cheap prompt. The reply content is irrelevant; we only need the API round
# trip so the server returns fresh rate_limits for statusLine to record.
PROMPT = "hi"

# statusLine needs believable console dimensions or it may not render.
PTY_COLS = 120
PTY_ROWS = 30

STARTUP_GRACE_S = 12.0
OVERALL_TIMEOUT_S = 90.0
POLL_INTERVAL_S = 0.5


def cache_stamp():
    try:
        with CACHE_FILE.open("r", encoding="utf-8") as handle:
            return json.load(handle).get("captured_at", 0)
    except (OSError, ValueError):
        return 0


def heartbeat_has_rate_limits():
    try:
        with HEARTBEAT_FILE.open("r", encoding="utf-8") as handle:
            return bool(json.load(handle).get("has_rate_limits"))
    except (OSError, ValueError):
        return False


def find_claude():
    # Prefer an explicit override, then the native install path, then PATH.
    override = os.environ.get("CLAUDE_BIN")
    if override and Path(override).exists():
        return override
    candidate = Path.home() / ".local" / "bin" / "claude.exe"
    if candidate.exists():
        return str(candidate)
    candidate = Path.home() / ".local" / "bin" / "claude"
    if candidate.exists():
        return str(candidate)
    from shutil import which

    found = which("claude")
    if found:
        return found
    return "claude"


def main():
    try:
        from winpty import PtyProcess
    except ImportError:
        sys.stderr.write(
            "pywinpty is required: "
            "\"%s\" -m pip install pywinpty\n" % sys.executable
        )
        return 3

    before = cache_stamp()
    claude = find_claude()

    # Interactive session with NO positional prompt, so it does not fall into
    # print mode. statusLine only runs this way. The prompt is typed into the
    # pty afterwards. plan mode keeps the throwaway turn from touching anything.
    argv = [claude, "--permission-mode", "plan"]
    debug = os.environ.get("REFRESH_DEBUG")
    sys.stderr.write("spawning: %s\n" % " ".join(argv))

    proc = PtyProcess.spawn(argv, dimensions=(PTY_ROWS, PTY_COLS))

    deadline = time.time() + OVERALL_TIMEOUT_S
    time.sleep(STARTUP_GRACE_S)

    # Type the prompt, pause so the TUI registers the text, then send Enter.
    # Doing it in one burst sometimes loses the submit on a still-initializing UI.
    try:
        proc.write(PROMPT)
        time.sleep(1.0)
        proc.write("\r")
    except Exception as exc:
        sys.stderr.write("failed to write prompt: %r\n" % exc)

    resent = False

    updated = False
    try:
        while time.time() < deadline:
            try:
                if proc.isalive():
                    chunk = proc.read(4096)
                    if debug and chunk:
                        sys.stderr.write(chunk if isinstance(chunk, str) else chunk.decode("utf-8", "replace"))
            except EOFError:
                pass
            except Exception:
                pass

            if cache_stamp() > before:
                updated = True
                break

            # If statusLine has run but rate_limits still has not appeared a
            # while after we sent the prompt, the submit was probably missed.
            # Send Enter once more.
            if (
                not resent
                and not heartbeat_has_rate_limits()
                and time.time() > deadline - OVERALL_TIMEOUT_S + STARTUP_GRACE_S + 20
            ):
                try:
                    proc.write("\r")
                except Exception:
                    pass
                resent = True

            time.sleep(POLL_INTERVAL_S)
    finally:
        try:
            proc.write("/exit\r")
            time.sleep(0.5)
        except Exception:
            pass
        try:
            proc.terminate(force=True)
        except Exception:
            pass

    if not updated:
        sys.stderr.write(
            "cache was not updated within %ds. The session may not have reached "
            "an API response, or rate_limits is not offered on this plan.\n"
            % OVERALL_TIMEOUT_S
        )
        return 1

    try:
        with CACHE_FILE.open("r", encoding="utf-8") as handle:
            fresh = json.load(handle)
        sys.stderr.write("cache updated: %s\n" % json.dumps(fresh))
    except (OSError, ValueError):
        pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
