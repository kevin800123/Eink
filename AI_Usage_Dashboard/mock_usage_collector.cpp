#include "mock_usage_collector.h"

#include <stdio.h>
#include <string.h>

namespace {

void setProvider(ProviderUsage& provider, const char* name, bool available,
                 uint8_t fiveHour, uint8_t weekly, const char* resetLabel) {
  snprintf(provider.name, sizeof(provider.name), "%s", name);
  provider.available = available;
  provider.fiveHourPercent = fiveHour;
  provider.weeklyPercent = weekly;
  snprintf(provider.resetLabel, sizeof(provider.resetLabel), "%s", resetLabel);
}

}  // namespace

bool MockUsageCollector::begin() {
  return true;
}

bool MockUsageCollector::fetch(DashboardData& output) {
  memset(&output, 0, sizeof(output));

  setProvider(output.providers[0], "CLAUDE", true, 72, 41, "RESET 3H 42M");
  setProvider(output.providers[1], "CODEX", true, 51, 63, "RESET 1H 18M");
  setProvider(output.providers[2], "GEMINI", false, 0, 0, "N/A");

  output.device.wifiConnected = true;
  output.device.wifiRssi = -58;
  snprintf(output.device.wifiLabel, sizeof(output.device.wifiLabel), "DEMO");
  output.device.batteryPercent = 86;
  snprintf(output.device.updatedAt, sizeof(output.device.updatedAt), "22:48");
  output.isMock = true;
  return true;
}

const char* MockUsageCollector::lastError() const {
  return "ok";
}

