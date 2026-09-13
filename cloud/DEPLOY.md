# Cloud relay — deploy (Cloudflare Workers, free)

The relay stores the latest sanitized usage snapshot the PC pushes and keeps
serving it to the ESP32 even while the PC is asleep/off, from anywhere (not just
the home LAN). Only percentages, reset times and collection timestamps are
stored — never a provider credential. It is protected by two tokens: an ingest
token (PC → relay) and a view token (device → relay).

## Prerequisites

- A free Cloudflare account.
- Node.js (already present) and wrangler: `npm i -g wrangler` (or use `npx wrangler`).

## Steps (run in the `cloud/` folder)

```powershell
cd <repo>\cloud

# 1. Sign in (opens a browser once)
wrangler login

# 2. Create the KV store, then paste the printed id into wrangler.toml
#    (replace REPLACE_WITH_KV_NAMESPACE_ID)
wrangler kv namespace create SNAPSHOT

# 3. Set the two secrets (paste each token when prompted)
wrangler secret put INGEST_TOKEN
wrangler secret put VIEW_TOKEN

# 4. Deploy — note the printed https://ai-usage-relay.<subdomain>.workers.dev URL
wrangler deploy
```

Generate strong tokens if you need them:

```powershell
python -c "import secrets;print(secrets.token_urlsafe(24))"
```

## After deploy

1. Put the Worker URL into `tools/collector/cloud_push.local.json` (gitignored):
   `"ingest_url": "https://ai-usage-relay.<subdomain>.workers.dev/api/usage"`
   and confirm `ingest_token` matches the INGEST_TOKEN secret.
2. Start the PC pusher:
   `\tools\collector\setup_cloud_push.ps1`
3. Verify the relay is serving (view token):
   ```powershell
   curl.exe -H "Authorization: Bearer <VIEW_TOKEN>" https://ai-usage-relay.<subdomain>.workers.dev/v1/dashboard
   ```
4. The firmware is then pointed at `https://<worker-host>/v1/dashboard` with the
   view token (HTTPS), and reflashed once.

## Security notes

- Two separate tokens: ingest (write) and view (read). Rotate by setting new
  secrets and updating the config / firmware.
- The relay stores only sanitized usage numbers; it never sees provider OAuth.
- `wrangler.toml` holds only the KV namespace id (not a secret). Tokens live as
  Worker secrets and in gitignored local files, never in the repo.
