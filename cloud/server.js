// Node relay for the AI usage dashboard (Zeabur / any Node host).
//
// Same contract as cloud/worker.js (the Cloudflare alternative):
//   POST /api/usage    (Authorization: Bearer INGEST_TOKEN) — store snapshot
//   GET  /v1/dashboard (Authorization: Bearer VIEW_TOKEN)   — read snapshot
//   GET  /healthz      — liveness, no auth
//
// Zero dependencies (Node built-in http/fs only). The snapshot is written to
// DATA_DIR/snapshot.json; mount a persistent volume at DATA_DIR so it survives
// redeploys. Only sanitized usage numbers/timestamps are stored — never a
// provider credential.
//
// Env: PORT (Zeabur injects it), INGEST_TOKEN, VIEW_TOKEN, DATA_DIR (default /data).

const http = require("http");
const fs = require("fs");
const path = require("path");

const PORT = process.env.PORT || 8080;
const INGEST_TOKEN = process.env.INGEST_TOKEN || "";
const VIEW_TOKEN = process.env.VIEW_TOKEN || "";
const DATA_DIR = process.env.DATA_DIR || "/data";
const SNAPSHOT_FILE = path.join(DATA_DIR, "snapshot.json");
const MAX_BODY = 65536;

try { fs.mkdirSync(DATA_DIR, { recursive: true }); } catch (e) { /* falls back to error on write */ }

function sendJson(res, status, obj) {
  res.writeHead(status, { "Content-Type": "application/json; charset=utf-8", "Cache-Control": "no-store" });
  res.end(JSON.stringify(obj));
}
function bearer(req) {
  const a = req.headers["authorization"] || "";
  return a.startsWith("Bearer ") ? a.slice(7) : "";
}
function clampInt(n) {
  n = Math.round(Number(n));
  if (!isFinite(n)) return 0;
  return Math.max(0, Math.min(100, n));
}
function okWindow(x) {
  if (!x || typeof x !== "object") return null;
  const o = {};
  if (x.window_minutes != null) o.window_minutes = Number(x.window_minutes);
  o.used_percent = x.used_percent == null ? null : clampInt(x.used_percent);
  if (x.reset_at != null) o.reset_at = String(x.reset_at);
  if (x.rolled_over != null) o.rolled_over = !!x.rolled_over;
  return o;
}
function sanitize(body) {
  const providers = (body.providers || []).slice(0, 5).map((p) => {
    const o = { id: String(p.id || ""), label: String(p.label || ""), status: String(p.status || "") };
    if (p.usage_percent != null) o.usage_percent = clampInt(p.usage_percent);
    if (p.reset_at != null) o.reset_at = String(p.reset_at);
    if (p.error_code != null) o.error_code = String(p.error_code);
    if (p.captured_at != null) o.captured_at = Number(p.captured_at);
    if (p.stale != null) o.stale = !!p.stale;
    if (p.age_seconds != null) o.age_seconds = Number(p.age_seconds);
    if (p.windows && typeof p.windows === "object") {
      o.windows = {};
      const fh = okWindow(p.windows.five_hour);
      const wk = okWindow(p.windows.weekly);
      if (fh) o.windows.five_hour = fh;
      if (wk) o.windows.weekly = wk;
    }
    return o;
  });
  return { schema_version: 1, generated_at: body.generated_at != null ? String(body.generated_at) : null, providers };
}

const server = http.createServer((req, res) => {
  let url;
  try { url = new URL(req.url, "http://localhost"); } catch { return sendJson(res, 400, { error: "bad_request" }); }

  if (url.pathname === "/healthz") return sendJson(res, 200, { service: "ai-usage-relay", ok: true });

  if (req.method === "POST" && url.pathname === "/api/usage") {
    if (!INGEST_TOKEN || bearer(req) !== INGEST_TOKEN) return sendJson(res, 401, { error: "unauthorized" });
    let data = "";
    let aborted = false;
    req.on("data", (c) => {
      data += c;
      if (data.length > MAX_BODY) { aborted = true; sendJson(res, 413, { error: "too_large" }); req.destroy(); }
    });
    req.on("end", () => {
      if (aborted) return;
      let body;
      try { body = JSON.parse(data); } catch { return sendJson(res, 400, { error: "bad_json" }); }
      if (!body || body.schema_version !== 1 || !Array.isArray(body.providers)) return sendJson(res, 400, { error: "bad_schema" });
      const snap = sanitize(body);
      snap.relayed_at = Math.floor(Date.now() / 1000);
      try {
        fs.writeFileSync(SNAPSHOT_FILE + ".tmp", JSON.stringify(snap));
        fs.renameSync(SNAPSHOT_FILE + ".tmp", SNAPSHOT_FILE);
      } catch (e) {
        return sendJson(res, 500, { error: "store_failed" });
      }
      return sendJson(res, 200, { ok: true, relayed_at: snap.relayed_at });
    });
    return;
  }

  if (req.method === "GET" && url.pathname === "/v1/dashboard") {
    if (!VIEW_TOKEN || bearer(req) !== VIEW_TOKEN) return sendJson(res, 401, { error: "unauthorized" });
    let stored;
    try { stored = fs.readFileSync(SNAPSHOT_FILE, "utf-8"); }
    catch { return sendJson(res, 200, { schema_version: 1, providers: [], generated_at: null, relayed_at: null }); }
    res.writeHead(200, { "Content-Type": "application/json; charset=utf-8", "Cache-Control": "no-store" });
    res.end(stored);
    return;
  }

  return sendJson(res, 404, { error: "not_found" });
});

server.listen(PORT, () => console.log("ai-usage-relay listening on " + PORT + ", data " + DATA_DIR));
