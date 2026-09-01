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

#ifndef AI_DASH_EPD_POWER_OFF_AFTER_REFRESH
#define AI_DASH_EPD_POWER_OFF_AFTER_REFRESH 1
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

