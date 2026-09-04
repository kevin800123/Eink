#include "device_status.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "app_config.h"
#include "board_config.h"

#if AI_DASH_ENABLE_WIFI
#include <WiFi.h>
#endif

void DeviceStatusService::begin() {
#if AI_DASH_USE_MOCK_DEVICE_STATUS
  Serial.println("Device status: mock");
#else
  Serial.println("Device status: real (battery ADC + Wi-Fi + NTP)");
  pinMode(BoardConfig::BatteryAdc, INPUT);

#if AI_DASH_ENABLE_WIFI
  if (strlen(AI_DASH_WIFI_SSID) == 0) {
    Serial.println("Wi-Fi: no SSID configured, staying offline");
    return;
  }
  // The SSID is logged to make connection failures diagnosable. The password is
  // never logged.
  Serial.printf("Wi-Fi: connecting to \"%s\" (timeout %lu ms)\n",
                AI_DASH_WIFI_SSID,
                static_cast<unsigned long>(AI_DASH_WIFI_TIMEOUT_MS));
  WiFi.mode(WIFI_STA);
  WiFi.begin(AI_DASH_WIFI_SSID, AI_DASH_WIFI_PASSWORD);
  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - started < AI_DASH_WIFI_TIMEOUT_MS) {
    delay(100);
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("Wi-Fi: failed after %lu ms (status %d); continuing offline\n",
                  static_cast<unsigned long>(millis() - started),
                  static_cast<int>(WiFi.status()));
    return;
  }
  Serial.printf("Wi-Fi: connected in %lu ms, IP %s, RSSI %d dBm\n",
                static_cast<unsigned long>(millis() - started),
                WiFi.localIP().toString().c_str(), WiFi.RSSI());

  // The ESP32 RTC keeps time across deep sleep, so only sync when the clock is
  // not already set. This avoids a multi-second NTP wait on every timer wake.
  if (time(nullptr) >= 1600000000L) {
    tm now{};
    if (getLocalTime(&now, 50)) {
      char stamp[32];
      strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &now);
      Serial.printf("NTP: RTC already set, local time %s\n", stamp);
    }
  } else {
    configTzTime(AI_DASH_TIMEZONE, AI_DASH_NTP_SERVER);
    tm now{};
    if (getLocalTime(&now, 5000)) {
      char stamp[32];
      strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &now);
      Serial.printf("NTP: synced, local time %s\n", stamp);
    } else {
      Serial.println("NTP: no sync yet; the updated label falls back to uptime");
    }
  }
#endif
#endif
}

void DeviceStatusService::apply(DashboardData& data) {
#if AI_DASH_USE_MOCK_DEVICE_STATUS
  (void)data;
#else
  data.device.batteryPercent = readBatteryPercent();

#if AI_DASH_ENABLE_WIFI
  data.device.wifiConnected = WiFi.status() == WL_CONNECTED;
  data.device.wifiRssi = data.device.wifiConnected ? WiFi.RSSI() : -127;
  snprintf(data.device.wifiLabel, sizeof(data.device.wifiLabel), "%s",
           data.device.wifiConnected ? "ONLINE" : "OFFLINE");
#else
  data.device.wifiConnected = false;
  data.device.wifiRssi = -127;
  snprintf(data.device.wifiLabel, sizeof(data.device.wifiLabel), "OFFLINE");
#endif

  updateTimeLabel(data.device);
#endif
}

uint8_t DeviceStatusService::readBatteryPercent() {
  uint32_t millivolts = 0;
  constexpr uint8_t samples = 8;
  for (uint8_t sample = 0; sample < samples; ++sample) {
    millivolts += analogReadMilliVolts(BoardConfig::BatteryAdc);
    delay(2);
  }
  millivolts = (millivolts / samples) * 2U;  // Board has a 1:1 divider.

  constexpr uint32_t emptyMv = 3000;
  constexpr uint32_t fullMv = 4120;
  uint8_t percent;
  if (millivolts <= emptyMv) {
    percent = 0;
  } else if (millivolts >= fullMv) {
    percent = 100;
  } else {
    percent = static_cast<uint8_t>((millivolts - emptyMv) * 100U /
                                   (fullMv - emptyMv));
  }
  // Logged raw so an implausible reading is visible rather than silently
  // rendered as a confident percentage.
  Serial.printf("Battery: %lu mV -> %u%%\n",
                static_cast<unsigned long>(millivolts), percent);
  return percent;
}

void DeviceStatusService::updateTimeLabel(DeviceStatus& status) {
  // Use the absolute epoch plus a fixed local offset rather than getLocalTime,
  // which depends on configTzTime having taken effect (it did not on this
  // board, so the clock read 8 hours behind). time(nullptr) is UTC epoch once
  // SNTP has synced, which is timezone-independent.
  const time_t now = time(nullptr);
  if (now >= 1600000000L) {
    time_t shifted = now + AI_DASH_UTC_OFFSET_SECONDS;
    tm local{};
    gmtime_r(&shifted, &local);
    strftime(status.updatedAt, sizeof(status.updatedAt), "%H:%M", &local);
    return;
  }

  const uint32_t minutes = millis() / 60000UL;
  snprintf(status.updatedAt, sizeof(status.updatedAt), "UP %02lu:%02lu",
           static_cast<unsigned long>(minutes / 60UL),
           static_cast<unsigned long>(minutes % 60UL));
}

