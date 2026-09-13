// Cloudflare Worker: a tiny private relay for the AI usage dashboard.
//
// Why: the ESP32 dashboard otherwise needs the PC awake and reachable on the
// LAN. This relay stores the latest sanitized snapshot the PC pushes, and keeps
// serving it to the device even when the PC is asleep or off — and from
// anywhere, not just the home LAN.
//
// Endpoints:
//   POST /api/usage    (Authorization: Bearer INGEST_TOKEN) — store snapshot
//   GET  /v1/dashboard (Authorization: Bearer VIEW_TOKEN)   — read snapshot
//   GET  /healthz      — liveness, no auth
//
// Secrets (wrangler secret put): INGEST_TOKEN, VIEW_TOKEN.
// KV binding: SNAPSHOT (stores only the latest sanitized snapshot).
//
// Only whitelisted numeric/label fields are stored; anything unexpected in the
// upload is dropped. No provider credential is ever involved.

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const bearer = (request.headers.get("Authorization") || "").replace(/^Bearer\s+/, "");

    if (url.pathname === "/healthz") {
      return json({ service: "ai-usage-relay", ok: true }, 200);
    }

    if (request.method === "POST" && url.pathname === "/api/usage") {
      if (!env.INGEST_TOKEN || bearer !== env.INGEST_TOKEN) return json({ error: "unauthorized" }, 401);
      let body;
      try { body = await request.json(); } catch { return json({ error: "bad_json" }, 400); }
      if (!body || body.schema_version !== 1 || !Array.isArray(body.providers)) {
        return json({ error: "bad_schema" }, 400);
      }
      const snapshot = sanitize(body);
      snapshot.relayed_at = Math.floor(Date.now() / 1000);
      await env.SNAPSHOT.put("latest", JSON.stringify(snapshot));
      return json({ ok: true, relayed_at: snapshot.relayed_at }, 200);
    }

    if (request.method === "GET" && url.pathname === "/v1/dashboard") {
      if (!env.VIEW_TOKEN || bearer !== env.VIEW_TOKEN) return json({ error: "unauthorized" }, 401);
      const stored = await env.SNAPSHOT.get("latest");
      if (!stored) return json({ schema_version: 1, providers: [], generated_at: null, relayed_at: null }, 200);
      return new Response(stored, { status: 200, headers: noStoreJson() });
    }

    return json({ error: "not_found" }, 404);
  },
};

function json(obj, status) {
  return new Response(JSON.stringify(obj), { status, headers: noStoreJson() });
}
function noStoreJson() {
  return { "Content-Type": "application/json; charset=utf-8", "Cache-Control": "no-store" };
}
function clampInt(n) {
  n = Math.round(Number(n));
  if (!isFinite(n)) return 0;
  return Math.max(0, Math.min(100, n));
}
function okWindow(x) {
  if (!x || typeof x !== "object") return null;
  const out = {};
  if (x.window_minutes != null) out.window_minutes = Number(x.window_minutes);
  out.used_percent = x.used_percent == null ? null : clampInt(x.used_percent);
  if (x.reset_at != null) out.reset_at = String(x.reset_at);
  if (x.rolled_over != null) out.rolled_over = !!x.rolled_over;
  return out;
}
function sanitize(body) {
  const providers = (body.providers || []).slice(0, 5).map((p) => {
    const out = { id: String(p.id || ""), label: String(p.label || ""), status: String(p.status || "") };
    if (p.usage_percent != null) out.usage_percent = clampInt(p.usage_percent);
    if (p.reset_at != null) out.reset_at = String(p.reset_at);
    if (p.error_code != null) out.error_code = String(p.error_code);
    if (p.captured_at != null) out.captured_at = Number(p.captured_at);
    if (p.stale != null) out.stale = !!p.stale;
    if (p.age_seconds != null) out.age_seconds = Number(p.age_seconds);
    if (p.windows && typeof p.windows === "object") {
      out.windows = {};
      const fh = okWindow(p.windows.five_hour);
      const wk = okWindow(p.windows.weekly);
      if (fh) out.windows.five_hour = fh;
      if (wk) out.windows.weekly = wk;
    }
    return out;
  });
  return {
    schema_version: 1,
    generated_at: body.generated_at != null ? String(body.generated_at) : null,
    providers,
  };
}
