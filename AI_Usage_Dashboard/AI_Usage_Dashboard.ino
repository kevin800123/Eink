#include <Arduino.h>

#include "app_config.h"
#include "board_power.h"
#include "canvas_1bit.h"
#include "dashboard_model.h"
#include "dashboard_renderer.h"
#include "device_status.h"
#include "epaper_display.h"
#include "mock_usage_collector.h"
#include "usage_collector.h"

BoardPower boardPower;
EpaperDisplay display;
Canvas1Bit canvas(display);
DashboardRenderer dashboardRenderer;
DeviceStatusService deviceStatus;
MockUsageCollector mockCollector;
UsageCollector* usageCollector = &mockCollector;

uint32_t lastRefreshAt = 0;
bool firstRefreshComplete = false;

bool refreshDashboard() {
  DashboardData data{};
  if (!usageCollector->fetch(data)) {
    Serial.printf("Usage fetch failed: %s\n", usageCollector->lastError());
    return false;
  }
  deviceStatus.apply(data);

  if (!boardPower.setEpaper(true)) {
    Serial.printf("EPD power-on failed: %s\n", boardPower.lastError());
    return false;
  }
  delay(20);

  if (!display.begin()) {
    Serial.printf("EPD initialization failed: %s\n", display.lastError());
    return false;
  }

  dashboardRenderer.render(canvas, data);
  if (!display.present()) {
    Serial.printf("EPD refresh failed: %s\n", display.lastError());
    return false;
  }

  display.sleep();
#if AI_DASH_EPD_POWER_OFF_AFTER_REFRESH
  boardPower.setEpaper(false);
#endif

  Serial.println("Dashboard refreshed");
  return true;
}

void setup() {
  Serial.begin(115200);
  // Latch battery power as early as possible after the PWR button starts boot.
  if (!boardPower.begin()) {
    delay(100);
    Serial.printf("Board power initialization failed: %s\n",
                  boardPower.lastError());
    return;
  }

  delay(1000);
  Serial.println("AI Usage Dashboard booting");

  deviceStatus.begin();
  if (!usageCollector->begin()) {
    Serial.printf("Usage collector initialization failed: %s\n",
                  usageCollector->lastError());
    return;
  }

  firstRefreshComplete = refreshDashboard();
  lastRefreshAt = millis();
}

void loop() {
  const uint32_t now = millis();
  const uint32_t interval = firstRefreshComplete
                                ? AI_DASH_REFRESH_INTERVAL_MS
                                : 10000UL;
  if (now - lastRefreshAt >= interval) {
    firstRefreshComplete = refreshDashboard();
    lastRefreshAt = now;
  }
  delay(50);
}
