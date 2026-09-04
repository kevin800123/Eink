#include <Arduino.h>
#include <esp_sleep.h>

#include "app_config.h"
#include "board_power.h"
#include "canvas_1bit.h"
#include "dashboard_model.h"
#include "dashboard_renderer.h"
#include "device_status.h"
#include "epaper_display.h"
#include "usage_collector.h"
#if AI_DASH_USE_HTTP_COLLECTOR
#include "http_usage_collector.h"
#else
#include "mock_usage_collector.h"
#endif

BoardPower boardPower;
EpaperDisplay display;
Canvas1Bit canvas(display);
DashboardRenderer dashboardRenderer;
DeviceStatusService deviceStatus;
#if AI_DASH_USE_HTTP_COLLECTOR
HttpUsageCollector httpCollector;
UsageCollector* usageCollector = &httpCollector;
#else
MockUsageCollector mockCollector;
UsageCollector* usageCollector = &mockCollector;
#endif

uint32_t lastRefreshAt = 0;
bool firstRefreshComplete = false;
uint8_t consecutiveFailures = 0;
bool halted = false;

void haltWith(const char* reason) {
  halted = true;
  Serial.printf("Halted: %s\n", reason);
  Serial.println("No further I2C or SPI activity. Any image already on the "
                 "panel stays visible.");
}

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
  const uint32_t started = millis();
  if (!display.present()) {
    Serial.printf("EPD refresh failed after %lu ms: %s\n",
                  static_cast<unsigned long>(millis() - started),
                  display.lastError());
    return false;
  }
  const uint32_t elapsed = millis() - started;

  display.sleep();
#if AI_DASH_EPD_POWER_OFF_AFTER_REFRESH
  boardPower.setEpaper(false);
#endif

  Serial.printf("Dashboard refreshed in %lu ms\n",
                static_cast<unsigned long>(elapsed));
  return true;
}

#if AI_DASH_USE_DEEP_SLEEP
void deepSleepFor(uint32_t seconds) {
  Serial.printf("Deep sleeping for %lu s\n",
                static_cast<unsigned long>(seconds));
  Serial.flush();
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(seconds) * 1000000ULL);
  esp_deep_sleep_start();  // does not return
}
#endif

void setup() {
  Serial.begin(115200);
  // Never block on USB CDC while no host is attached; the report below raises
  // this again so the boot lines are not silently dropped.
  Serial.setTxTimeoutMs(0);

  // A deep-sleep timer wake keeps the TCA9554 latched, so the rail and EPD
  // power are already on; resume without begin()'s reset, which could drop the
  // battery rail. A cold boot (PWR button, USB, or reset) runs begin().
  const bool timerWake =
      esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;

  // Latch battery power as early as possible. On a battery boot the PWR button
  // only holds the rail up until EXIO5 is asserted, so this must not wait for
  // logging. The result is printed once the USB CDC host has had time to attach.
  const bool powerOk = timerWake ? boardPower.resume() : boardPower.begin();
  const char* powerError = boardPower.lastError();

  delay(1000);
  Serial.setTxTimeoutMs(200);
  Serial.println();
  Serial.printf("AI Usage Dashboard booting (%s)\n",
                timerWake ? "timer wake" : "cold boot");

  if (!powerOk) {
    Serial.printf("Board power initialization failed: %s\n", powerError);
#if AI_DASH_USE_DEEP_SLEEP
    deepSleepFor(AI_DASH_DEEP_SLEEP_RETRY_SECONDS);
#else
    haltWith("board power initialization failed");
#endif
    return;
  }

  // Waveshare's 07_BATT_PWR_Test.ino:69-71 refuses to continue while PWR is
  // still held. A bound is added here so a pin stuck low cannot hang the boot.
  // A timer wake never involves the button, so skip the wait then.
  if (!timerWake) {
    pinMode(BoardConfig::PowerButton, INPUT_PULLUP);
    const uint32_t waitStarted = millis();
    while (digitalRead(BoardConfig::PowerButton) == LOW &&
           millis() - waitStarted < 5000UL) {
      delay(100);
    }
  }

  deviceStatus.begin();
  if (!usageCollector->begin()) {
    Serial.printf("Usage collector initialization failed: %s\n",
                  usageCollector->lastError());
#if AI_DASH_USE_DEEP_SLEEP
    deepSleepFor(AI_DASH_DEEP_SLEEP_RETRY_SECONDS);
#else
    haltWith("usage collector initialization failed");
#endif
    return;
  }

  const bool ok = refreshDashboard();

#if AI_DASH_USE_DEEP_SLEEP
  // Refresh once per wake, then sleep at microamps. On failure retry sooner so
  // a transient outage (e.g. the PC asleep) recovers without a full interval.
  const uint32_t sleepSeconds = ok
                                    ? (AI_DASH_REFRESH_INTERVAL_MS / 1000UL)
                                    : AI_DASH_DEEP_SLEEP_RETRY_SECONDS;
  deepSleepFor(sleepSeconds);
#else
  firstRefreshComplete = ok;
  lastRefreshAt = millis();
  if (!firstRefreshComplete) {
    consecutiveFailures = 1;
  }
#endif
}

void loop() {
  if (halted) {
    delay(1000);
    return;
  }

  const uint32_t now = millis();
  const uint32_t interval = firstRefreshComplete
                                ? AI_DASH_REFRESH_INTERVAL_MS
                                : 10000UL;
  if (now - lastRefreshAt >= interval) {
    const bool ok = refreshDashboard();
    lastRefreshAt = now;
    if (ok) {
      firstRefreshComplete = true;
      consecutiveFailures = 0;
    } else if (++consecutiveFailures >= AI_DASH_MAX_CONSECUTIVE_FAILURES) {
      // Retrying forever is what the old firmware did against a stuck bus.
      haltWith("too many consecutive refresh failures");
    }
  }
  delay(50);
}
