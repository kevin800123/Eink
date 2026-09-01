#include "mock_usage_collector.h"

#include <stdio.h>
#include <string.h>

namespace {

void setProvider(ProviderUsage& provider, const char* name, uint8_t percent,
                 const char* resetLabel) {
  snprintf(provider.name, sizeof(provider.name), "%s", name);
  provider.usagePercent = percent;
  snprintf(provider.resetLabel, sizeof(provider.resetLabel), "%s", resetLabel);
}

}  // namespace

bool MockUsageCollector::begin() {
  return true;
}

bool MockUsageCollector::fetch(DashboardData& output) {
  memset(&output, 0, sizeof(output));

  setProvider(output.providers[0], "CLAUDE", 72, "RESET 3H 42M");
  setProvider(output.providers[1], "CODEX", 51, "RESET 1H 18M");
  setProvider(output.providers[2], "GEMINI", 86, "RESET 4H 07M");

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

