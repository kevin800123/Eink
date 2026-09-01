# Usage Collector API contract

## Boundary

The ESP32 should call one normalized collector endpoint. Provider credentials,
browser sessions, refresh tokens, and vendor-specific scraping logic stay on a
server you control; they do not belong in firmware.

The firmware boundary is `UsageCollector`:

```cpp
class UsageCollector {
 public:
  virtual ~UsageCollector() = default;
  virtual bool begin() = 0;
  virtual bool fetch(DashboardData& output) = 0;
  virtual const char* lastError() const = 0;
};
```

Version 0.1 supplies `MockUsageCollector`. A later `HttpUsageCollector` should
implement the same interface and write the same `DashboardData` model.

## Proposed HTTP endpoint

```http
GET /v1/dashboard
Authorization: Bearer <device-token>
Accept: application/json
```

Successful response:

```json
{
  "schema_version": 1,
  "generated_at": "2026-08-31T22:48:00+08:00",
  "providers": [
    {
      "id": "claude",
      "label": "CLAUDE",
      "usage_percent": 72,
      "reset_at": "2026-09-01T02:30:00+08:00"
    },
    {
      "id": "codex",
      "label": "CODEX",
      "usage_percent": 51,
      "reset_at": "2026-09-01T00:06:00+08:00"
    },
    {
      "id": "gemini",
      "label": "GEMINI",
      "usage_percent": 86,
      "reset_at": "2026-09-01T02:55:00+08:00"
    }
  ]
}
```

Rules:

- `schema_version` must be `1`.
- Exactly three provider records are expected in version 0.1.
- `usage_percent` is an integer from 0 through 100.
- `reset_at` is RFC 3339 with an explicit timezone offset.
- The device calculates the short `RESET 3H 42M` label from `reset_at`.
- An unavailable provider should return `status: "unavailable"` plus a short
  `error_code`; do not substitute invented usage.
- Recommended cache TTL: 5–15 minutes.

## Security requirements

- Use HTTPS and validate the server certificate.
- Give each device a revocable, least-privilege token.
- Never send Claude, OpenAI, or Google account credentials to the ESP32.
- Avoid unofficial provider endpoints in firmware. Put unstable integrations
  behind the collector so firmware remains unchanged.
- Log failure categories, not bearer tokens or response bodies containing
  secrets.

## Firmware integration checklist

1. Add `http_usage_collector.h/.cpp` implementing `UsageCollector`.
2. Parse into a temporary `DashboardData`; only publish it after complete
   validation.
3. Preserve the previous display on network or schema failure.
4. Add a visible stale marker after an agreed threshold.
5. Replace `MockUsageCollector` in `AI_Usage_Dashboard.ino` with the HTTP
   implementation.

