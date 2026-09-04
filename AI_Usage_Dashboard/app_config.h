#pragma once

// Optional private overrides. Copy config.local.h.example to config.local.h.
#if __has_include("config.local.h")
#include "config.local.h"
#endif

#ifndef AI_DASH_ENABLE_WIFI
#define AI_DASH_ENABLE_WIFI 0
#endif

#ifndef AI_DASH_USE_MOCK_DEVICE_STATUS
#define AI_DASH_USE_MOCK_DEVICE_STATUS 1
#endif

#ifndef AI_DASH_REFRESH_INTERVAL_MS
#define AI_DASH_REFRESH_INTERVAL_MS (15UL * 60UL * 1000UL)
#endif

#ifndef AI_DASH_WIFI_TIMEOUT_MS
#define AI_DASH_WIFI_TIMEOUT_MS 12000UL
#endif

#ifndef AI_DASH_BUSY_TIMEOUT_MS
#define AI_DASH_BUSY_TIMEOUT_MS 15000UL
#endif

// The official example powers the EPD rail once and leaves it on. Repeatedly
// switching that rail is an untested deviation on this board, so it stays off
// by default until repeated-refresh behaviour has real evidence behind it.
#ifndef AI_DASH_EPD_POWER_OFF_AFTER_REFRESH
#define AI_DASH_EPD_POWER_OFF_AFTER_REFRESH 0
#endif

// Stop refreshing after this many consecutive failures instead of retrying
// against a possibly stuck bus forever. The last good image stays on the panel.
#ifndef AI_DASH_MAX_CONSECUTIVE_FAILURES
#define AI_DASH_MAX_CONSECUTIVE_FAILURES 3
#endif

#ifndef AI_DASH_WIFI_SSID
#define AI_DASH_WIFI_SSID ""
#endif

#ifndef AI_DASH_WIFI_PASSWORD
#define AI_DASH_WIFI_PASSWORD ""
#endif

// Taiwan / UTC+8, expressed as a POSIX TZ string.
#ifndef AI_DASH_TIMEZONE
#define AI_DASH_TIMEZONE "CST-8"
#endif

#ifndef AI_DASH_NTP_SERVER
#define AI_DASH_NTP_SERVER "pool.ntp.org"
#endif

// --- Phase D: real usage over HTTP from the local collector ---------------
// Set AI_DASH_USE_HTTP_COLLECTOR to 1 in config.local.h and fill in the host,
// port and token to replace the mock data with the collector's real figures.
// Requires AI_DASH_ENABLE_WIFI=1. The device holds only the collector token,
// never a provider credential.
#ifndef AI_DASH_USE_HTTP_COLLECTOR
#define AI_DASH_USE_HTTP_COLLECTOR 0
#endif

#ifndef AI_DASH_COLLECTOR_HOST
#define AI_DASH_COLLECTOR_HOST ""
#endif

#ifndef AI_DASH_COLLECTOR_PORT
#define AI_DASH_COLLECTOR_PORT 8770
#endif

#ifndef AI_DASH_COLLECTOR_PATH
#define AI_DASH_COLLECTOR_PATH "/v1/dashboard"
#endif

#ifndef AI_DASH_DEVICE_TOKEN
#define AI_DASH_DEVICE_TOKEN ""
#endif

#ifndef AI_DASH_HTTP_TIMEOUT_MS
#define AI_DASH_HTTP_TIMEOUT_MS 8000UL
#endif

// --- Deep sleep (for battery use without USB) ------------------------------
// When enabled, the device refreshes once per wake then deep-sleeps for the
// refresh interval, drawing microamps instead of keeping the ESP32 + Wi-Fi
// awake. The e-paper keeps its image with no power during sleep. On a timer
// wake the board resumes without the full power reset (see BoardPower::resume).
// Default off, so USB-powered always-on behaviour is unchanged.
#ifndef AI_DASH_USE_DEEP_SLEEP
#define AI_DASH_USE_DEEP_SLEEP 0
#endif

// After a failed refresh, sleep this long before retrying instead of the full
// interval, so a transient outage (e.g. PC asleep) recovers sooner. Never busy-
// halt in deep-sleep mode; that would drain the battery.
#ifndef AI_DASH_DEEP_SLEEP_RETRY_SECONDS
#define AI_DASH_DEEP_SLEEP_RETRY_SECONDS 300UL
#endif

