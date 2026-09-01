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
#if !AI_DASH_USE_MOCK_DEVICE_STATUS
  pinMode(BoardConfig::BatteryAdc, INPUT);

#if AI_DASH_ENABLE_WIFI
  if (strlen(AI_DASH_WIFI_SSID) > 0) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(AI_DASH_WIFI_SSID, AI_DASH_WIFI_PASSWORD);
    const uint32_t started = millis();
    while (WiFi.status() != WL_CONNECTED &&
           millis() - started < AI_DASH_WIFI_TIMEOUT_MS) {
      delay(100);
    }
    if (WiFi.status() == WL_CONNECTED) {
      configTzTime(AI_DASH_TIMEZONE, AI_DASH_NTP_SERVER);
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
  if (millivolts <= emptyMv) {
    return 0;
  }
  if (millivolts >= fullMv) {
    return 100;
  }
  return static_cast<uint8_t>((millivolts - emptyMv) * 100U /
                              (fullMv - emptyMv));
}

void DeviceStatusService::updateTimeLabel(DeviceStatus& status) {
  tm local{};
  if (getLocalTime(&local, 50)) {
    strftime(status.updatedAt, sizeof(status.updatedAt), "%H:%M", &local);
    return;
  }

  const uint32_t minutes = millis() / 60000UL;
  snprintf(status.updatedAt, sizeof(status.updatedAt), "UP %02lu:%02lu",
           static_cast<unsigned long>(minutes / 60UL),
           static_cast<unsigned long>(minutes % 60UL));
}

