# Cloud relay — deploy

The relay stores the latest sanitized usage snapshot the PC pushes and keeps
serving it to the ESP32 even while the PC is asleep/off, from anywhere (not just
the home LAN). Only percentages, reset times and collection timestamps are
stored — never a provider credential. Two tokens protect it: an ingest token
(PC → relay) and a view token (device → relay).

Two implementations, same contract (`/api/usage`, `/v1/dashboard`, `/healthz`):

- `server.js` + `package.json` — standard Node, for **Zeabur** or any Node host.
- `worker.js` + `wrangler.toml` — Cloudflare Workers alternative.

## Zeabur (recommended here)

The service must run **persistently** (not a sleeping serverless), because the
device must reach it any time. The snapshot is stored on a **volume** so it
survives redeploys.

1. Zeabur dashboard → your Project → **Add Service** → **Deploy from GitHub** →
   pick `kevin800123/Eink`.
2. Open the service → **Settings** → set **Root Directory** to `cloud`
   (so Zeabur builds `cloud/package.json` as a Node app; it runs `npm start`).
3. **Variables** — add:
   - `INGEST_TOKEN` = your ingest token
   - `VIEW_TOKEN` = your view token
   - `DATA_DIR` = `/data`
4. **Volumes** — add a volume, mount path `/data` (persists `snapshot.json`).
5. **Networking** — **Generate Domain** → note `https://<name>.zeabur.app`
   (Zeabur terminates HTTPS for you; the device needs HTTPS).
6. **Deploy**, then verify:
   ```
   curl https://<name>.zeabur.app/healthz
   ```
   Expected: `{"service":"ai-usage-relay","ok":true}`.

Do not set `PORT` yourself — Zeabur injects it and `server.js` reads it.

CLI alternative: from `cloud/`, `npx zeabur@latest deploy` (still set the
variables and volume in the dashboard).

### After deploy

1. Put the domain into `tools/collector/cloud_push.local.json` (gitignored):
   `"ingest_url": "https://<name>.zeabur.app/api/usage"` (keep `ingest_token`).
2. Start the PC pusher: `\tools\collector\setup_cloud_push.ps1`
3. Confirm the relay now serves data (view token):
   ```
   curl.exe -H "Authorization: Bearer <VIEW_TOKEN>" https://<name>.zeabur.app/v1/dashboard
   ```
4. The firmware is then pointed at `https://<name>.zeabur.app/v1/dashboard`
   (HTTPS, view token) and reflashed once.

## Cloudflare Workers (alternative, free)

```powershell
cd <repo>\cloud
npm i -g wrangler
wrangler login
wrangler kv namespace create SNAPSHOT      # paste id into wrangler.toml
wrangler secret put INGEST_TOKEN
wrangler secret put VIEW_TOKEN
wrangler deploy                            # note the workers.dev URL
```

## Security notes

- Two separate tokens: ingest (write) and view (read). Rotate by changing the
  service variables/secrets and updating the config / firmware.
- The relay stores only sanitized usage numbers; it never sees provider OAuth.
- Tokens live in service variables/secrets and gitignored local files, never in
  the repo.
