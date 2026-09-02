# Usage collector

Serves `GET /v1/dashboard` in the shape defined by
[`docs/API_CONTRACT.md`](../../docs/API_CONTRACT.md), so the ESP32 holds no
provider credential and makes no call to Anthropic, OpenAI or Google.

## What is real and what is not

| Provider | Status | Source |
|---|---|---|
| `codex` | **real** | `~/.codex/sessions/**/rollout-*.jsonl`, written by the official Codex CLI on every turn |
| `claude` | `unavailable` | no official API, CLI or telemetry exposes subscription quota |
| `gemini` | `unavailable` | Antigravity IDE shows quota but stores nothing on disk; its local RPC needs an auth token |

Verified 2026-09-02. The unavailable providers return `status: "unavailable"`
plus an `error_code`, never an invented number.

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
