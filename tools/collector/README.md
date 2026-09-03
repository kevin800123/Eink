# Usage collector

Serves `GET /v1/dashboard` in the shape defined by
[`docs/API_CONTRACT.md`](../../docs/API_CONTRACT.md), so the ESP32 holds no
provider credential and makes no call to Anthropic, OpenAI or Google.

## What is real and what is not

| Provider | Status | Source |
|---|---|---|
| `claude` | **real** | the documented Claude Code `statusLine` payload, captured by `claude_statusline.py` |
| `codex` | **real** | `~/.codex/sessions/**/rollout-*.jsonl`, written by the official Codex CLI on every turn |
| `gemini` | `unavailable` | Antigravity IDE shows quota but stores nothing on disk; its local RPC needs an auth token |

Verified 2026-09-02. An unavailable provider returns `status: "unavailable"`
plus an `error_code`, never an invented number.

## How Claude quota is obtained (this is the subtle part)

The Claude.ai subscription quota `used_percentage` is exposed in exactly one
place: the `statusLine` payload, which Claude Code only produces in a real
interactive session with a pseudo console. It is **not** available from the
Desktop app, `claude -p`, piped stdin, transcripts, hooks, telemetry, or any
CLI/API. `claude -p --output-format stream-json` carries a `rate_limit_event`,
but that has only `status` and `resetsAt`, no percentage. This was all verified
on 2026-09-02.

Two pieces cooperate:

1. `claude_statusline.py` is configured as the Claude Code `statusLine`. Whenever
   an interactive session renders its status line, this captures `five_hour` and
   `seven_day` (`used_percentage` + `resets_at`) to `~/.ai-usage-dashboard/claude.json`.
2. `refresh_claude.py` produces those renders on a schedule. Since the Desktop
   app never runs statusLine, this spawns a brief interactive `claude` turn
   inside a ConPTY (via `pywinpty`), sends one tiny prompt, and waits for the
   cache timestamp to advance. `rate_limits` appears only after the first API
   response in a session, so the round trip is required.

The quota is account-wide, so each refresh reflects Desktop usage too. **Cost:**
one small API call per refresh, against your 5-hour window. The schedule interval
is therefore a cost/freshness tradeoff.

### Enable the Claude side

```powershell
# once: install the dependency into the interpreter the task will use
C:\...\Python314\python.exe -m pip install pywinpty

# schedule the refresher (default every 30 min, hidden, while logged on)
.\tools\collector\setup_schedule.ps1 -Minutes 30
# remove with: .\tools\collector\setup_schedule.ps1 -Remove
```

`refresh_claude.py` finds the CLI via `CLAUDE_BIN`, then `~/.local/bin/claude`,
then PATH. The `statusLine` entry in `~/.claude/settings.json` must point at
`claude_statusline.py` (see below).

## Claude data

Claude Code passes session JSON on stdin to whatever command is configured as
`statusLine`, and that payload is the **only officially documented place** the
Claude.ai subscription quota is exposed:

```json
"rate_limits": {
  "five_hour": {"used_percentage": 37, "resets_at": 1788369805},
  "seven_day": {"used_percentage": 61, "resets_at": 1788662605}
}
```

Per the docs the object appears only for Claude.ai Pro and Max subscribers, only
after the first API response in a session, and Claude Code drops a window once
its `resets_at` has passed. Absence is therefore normal, and the collector
reports `awaiting_statusline_data` rather than guessing.

Nothing here reads a credential, and no request is made to Anthropic. The
statusline script is a passive reader of data Claude Code already hands it.

### Enable it

Add to `~/.claude/settings.json`:

```json
"statusLine": {
  "type": "command",
  "command": "python \"<repo>/tools/collector/claude_statusline.py\""
}
```

Then restart Claude Code. The script prints a compact line such as
`Opus | ctx 8% | 5h 37% 1h59m | wk 61% 3d11h` and writes
`~/.ai-usage-dashboard/claude.json` for the collector. To remove it, delete the
`statusLine` key.

## Codex data

Each turn the CLI records:

```json
"rate_limits": {
  "primary":   {"used_percent": 96.0, "window_minutes": 300,   "resets_at": 1788282383},
  "secondary": {"used_percent": 26.0, "window_minutes": 10080, "resets_at": 1788795017},
  "plan_type": "plus"
}
```

`window_minutes` 300 is the 5-hour window, 10080 is the weekly window.

**The rollover rule matters.** The collector reads the newest record it can
find, which may be hours old if you have not used Codex recently. Rather than
serving a stale number, it compares `resets_at` against the current time: a
window whose reset has already passed has rolled over, so its usage is reported
as `0` with `rolled_over: true`. Staleness therefore corrects itself instead of
lying. A window whose reset is still in the future is by definition still the
current window, so its recorded usage is accurate.

`usage_percent` at the top level is the 5-hour window, because that is the
constraint that blocks work soonest. Both windows are also returned under
`windows`, so the firmware can switch without a collector change.

## Run it

Pick a token, then start the server. It refuses to start without one, because it
listens on the LAN.

```bash
AI_DASH_DEVICE_TOKEN=$(python -c "import secrets;print(secrets.token_urlsafe(24))") python tools/collector/usage_collector.py
```

On Windows, `tools/collector/run.ps1` generates a token on first run, stores it
in `tools/collector/token.local` (gitignored), and reuses it afterwards so the
firmware does not need reflashing every time.

Check the output without starting a server:

```bash
python tools/collector/usage_collector.py --once
```

Options: `--host` (default `0.0.0.0`), `--port` (default `8770`),
`--sessions-dir`, `--cache-seconds` (default `60`, how often the session files
are rescanned).

## Known deviation from the contract

`docs/API_CONTRACT.md` requires HTTPS with certificate validation. This
collector serves plain HTTP, which means **the bearer token crosses your LAN in
cleartext** and anyone on the network who captures it can read your usage
figures. That is the tradeoff for a dependency-free LAN-only v1.

It is acceptable only on a network you control. Before exposing this beyond the
LAN, or on a shared or public network, put it behind TLS — a reverse proxy with
a certificate the ESP32 pins is the smallest change. Never port-forward it to
the internet as it stands.

## Security notes

- Reads local files only. No provider credential is read, stored or forwarded.
- The token is the only access control. Treat `token.local` as a secret; it is
  gitignored.
- The response contains your usage figures and plan type, nothing else.
