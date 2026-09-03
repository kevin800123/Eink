#include "http_usage_collector.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "app_config.h"

#if AI_DASH_USE_HTTP_COLLECTOR

#if !AI_DASH_ENABLE_WIFI
#error "AI_DASH_USE_HTTP_COLLECTOR requires AI_DASH_ENABLE_WIFI=1"
#endif

#include <HTTPClient.h>
#include <WiFi.h>

namespace {

// The three providers the contract fixes for schema_version 1, in display order.
const char* const kProviderIds[kProviderCount] = {"claude", "codex", "gemini"};

// Find the value string of a "key":"value" pair within [from, to). Returns the
// index just past the closing quote, or -1 if not found. Escapes are not needed
// for this schema (values are ASCII labels and RFC 3339 timestamps).
int findStringField(const String& body, int from, int to, const char* key,
                    char* out, size_t outSize) {
  String needle = String("\"") + key + "\"";
  int keyPos = body.indexOf(needle, from);
  if (keyPos < 0 || keyPos >= to) return -1;
  int colon = body.indexOf(':', keyPos + needle.length());
  if (colon < 0 || colon >= to) return -1;
  int firstQuote = body.indexOf('"', colon + 1);
  if (firstQuote < 0 || firstQuote >= to) return -1;
  int endQuote = body.indexOf('"', firstQuote + 1);
  if (endQuote < 0 || endQuote > to) return -1;
  size_t len = static_cast<size_t>(endQuote - firstQuote - 1);
  if (len >= outSize) len = outSize - 1;
  memcpy(out, body.c_str() + firstQuote + 1, len);
  out[len] = '\0';
  return endQuote + 1;
}

// Find the integer value of a "key": <number> pair within [from, to).
bool findIntField(const String& body, int from, int to, const char* key,
                  long& out) {
  String needle = String("\"") + key + "\"";
  int keyPos = body.indexOf(needle, from);
  if (keyPos < 0 || keyPos >= to) return false;
  int colon = body.indexOf(':', keyPos + needle.length());
  if (colon < 0 || colon >= to) return false;
  int i = colon + 1;
  while (i < to && (body[i] == ' ' || body[i] == '\t')) ++i;
  bool neg = false;
  if (i < to && body[i] == '-') {
    neg = true;
    ++i;
  }
  if (i >= to || !isdigit(static_cast<unsigned char>(body[i]))) return false;
  long value = 0;
  while (i < to && isdigit(static_cast<unsigned char>(body[i]))) {
    value = value * 10 + (body[i] - '0');
    ++i;
  }
  out = neg ? -value : value;
  return true;
}

int clampPercent(long value) {
  if (value < 0) return 0;
  if (value > 100) return 100;
  return static_cast<int>(value);
}

// Read used_percent from a nested window object (windows.<window>) within
// [from, to). The windows carry used_percent (distinct from the top-level
// usage_percent), so the first match after the window key is that window's.
bool windowPercent(const String& body, int from, int to, const char* window,
                   long& out) {
  String needle = String("\"") + window + "\"";
  int pos = body.indexOf(needle, from);
  if (pos < 0 || pos >= to) return false;
  return findIntField(body, pos, to, "used_percent", out);
}

// Parse one provider object, whose fields lie in [from, to), into slot.
// The display shows the 5-hour window as the headline number and bar, and both
// the 5-hour and weekly percentages on the lower line ("5H x% WK y%").
bool parseProvider(const String& body, int from, int to, ProviderUsage& slot,
                   const char* fallbackId) {
  char label[sizeof(slot.name)] = {0};
  if (findStringField(body, from, to, "label", label, sizeof(label)) < 0) {
    // Fall back to an upper-cased id so the row still has a name.
    snprintf(label, sizeof(label), "%s", fallbackId);
    for (char* p = label; *p; ++p) *p = toupper(static_cast<unsigned char>(*p));
  }
  snprintf(slot.name, sizeof(slot.name), "%s", label);

  char status[16] = {0};
  findStringField(body, from, to, "status", status, sizeof(status));
  const bool ok = strcmp(status, "ok") == 0;

  if (!ok) {
    // Never invent a number for an unavailable provider.
    slot.usagePercent = 0;
    snprintf(slot.resetLabel, sizeof(slot.resetLabel), "N/A");
    return true;
  }

  // Bound the 5-hour search so it cannot read into the weekly window.
  const int fivePos = body.indexOf("\"five_hour\"", from);
  const int weekPos = body.indexOf("\"weekly\"", from);
  const int fiveEnd = (weekPos > fivePos && weekPos < to) ? weekPos : to;

  long five = -1;
  if (fivePos >= 0) windowPercent(body, fivePos, fiveEnd, "five_hour", five);
  long week = -1;
  if (weekPos >= 0) windowPercent(body, weekPos, to, "weekly", week);

  // Headline is the 5-hour window; fall back to the top-level usage_percent if
  // the windows object is unexpectedly absent.
  long headline = five;
  if (headline < 0) {
    if (!findIntField(body, from, to, "usage_percent", headline)) return false;
  }
  slot.usagePercent = static_cast<uint8_t>(clampPercent(headline));

  if (week >= 0) {
    snprintf(slot.resetLabel, sizeof(slot.resetLabel), "5H %d%% WK %d%%",
             clampPercent(five < 0 ? headline : five), clampPercent(week));
  } else {
    snprintf(slot.resetLabel, sizeof(slot.resetLabel), "5H %d%%",
             clampPercent(five < 0 ? headline : five));
  }
  return true;
}

}  // namespace

bool HttpUsageCollector::begin() {
  if (strlen(AI_DASH_COLLECTOR_HOST) == 0) {
    snprintf(error_, sizeof(error_), "collector host not configured");
    return false;
  }
  if (strlen(AI_DASH_DEVICE_TOKEN) == 0) {
    snprintf(error_, sizeof(error_), "device token not configured");
    return false;
  }
  Serial.printf("HTTP collector: http://%s:%d%s\n", AI_DASH_COLLECTOR_HOST,
                static_cast<int>(AI_DASH_COLLECTOR_PORT),
                AI_DASH_COLLECTOR_PATH);
  return true;
}

bool HttpUsageCollector::fetch(DashboardData& output) {
  if (WiFi.status() != WL_CONNECTED) {
    snprintf(error_, sizeof(error_), "wifi offline");
    return false;
  }

  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(AI_DASH_HTTP_TIMEOUT_MS);
  http.setTimeout(AI_DASH_HTTP_TIMEOUT_MS);
  if (!http.begin(client, AI_DASH_COLLECTOR_HOST, AI_DASH_COLLECTOR_PORT,
                  AI_DASH_COLLECTOR_PATH)) {
    snprintf(error_, sizeof(error_), "http begin failed");
    return false;
  }
  http.addHeader("Authorization", String("Bearer ") + AI_DASH_DEVICE_TOKEN);
  http.addHeader("Accept", "application/json");

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    // Log the status category only, never the token or full body.
    snprintf(error_, sizeof(error_), "http status %d", code);
    http.end();
    return false;
  }

  const String body = http.getString();
  http.end();

  long schema = 0;
  if (!findIntField(body, 0, body.length(), "schema_version", schema) ||
      schema != 1) {
    snprintf(error_, sizeof(error_), "bad schema_version");
    return false;
  }

  // Parse into a temporary, and only publish after all three providers validate.
  ProviderUsage parsed[kProviderCount] = {};
  const int bodyLen = body.length();
  for (size_t i = 0; i < kProviderCount; ++i) {
    String idNeedle = String("\"id\":\"") + kProviderIds[i] + "\"";
    // Tolerate a space after the colon ("id": "claude").
    int idPos = body.indexOf(idNeedle);
    if (idPos < 0) {
      String spaced = String("\"id\": \"") + kProviderIds[i] + "\"";
      idPos = body.indexOf(spaced);
    }
    if (idPos < 0) {
      snprintf(error_, sizeof(error_), "missing provider %s", kProviderIds[i]);
      return false;
    }
    // This provider's fields run until the next provider's "id", or end.
    int next = bodyLen;
    for (size_t j = 0; j < kProviderCount; ++j) {
      if (j == i) continue;
      int p = body.indexOf(String("\"id\":\"") + kProviderIds[j] + "\"");
      if (p < 0) p = body.indexOf(String("\"id\": \"") + kProviderIds[j] + "\"");
      if (p > idPos && p < next) next = p;
    }
    if (!parseProvider(body, idPos, next, parsed[i], kProviderIds[i])) {
      snprintf(error_, sizeof(error_), "parse failed %s", kProviderIds[i]);
      return false;
    }
  }

  for (size_t i = 0; i < kProviderCount; ++i) {
    output.providers[i] = parsed[i];
  }
  output.isMock = false;
  snprintf(error_, sizeof(error_), "ok");
  return true;
}

const char* HttpUsageCollector::lastError() const {
  return error_;
}

#endif  // AI_DASH_USE_HTTP_COLLECTOR
