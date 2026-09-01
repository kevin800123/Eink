#include "dashboard_renderer.h"

#include <stdio.h>

void DashboardRenderer::render(Canvas1Bit& canvas, const DashboardData& data) {
  canvas.clear(false);
  canvas.rect(0, 0, 200, 200, true);

  canvas.text(6, 5, "AI USAGE", 2);
  canvas.wifiIcon(143, 5, data.device.wifiConnected, data.device.wifiRssi);
  canvas.batteryIcon(167, 5, data.device.batteryPercent);
  canvas.line(0, 25, 199, 25);

  for (size_t index = 0; index < kProviderCount; ++index) {
    providerRow(canvas, 26 + static_cast<int16_t>(index) * 48,
                data.providers[index]);
  }

  char status[28];
  snprintf(status, sizeof(status), "BAT %u%% WIFI %s",
           data.device.batteryPercent, data.device.wifiLabel);
  canvas.text(7, 176, status, 1);

  char updated[24];
  snprintf(updated, sizeof(updated), "UPDATED %s", data.device.updatedAt);
  canvas.text(7, 188, updated, 1);

  if (data.isMock) {
    const char* label = "MOCK";
    canvas.text(193 - canvas.textWidth(label), 188, label, 1);
  }
}

void DashboardRenderer::providerRow(Canvas1Bit& canvas, int16_t top,
                                    const ProviderUsage& provider) {
  canvas.text(8, top + 5, provider.name, 2);

  char percent[6];
  snprintf(percent, sizeof(percent), "%u%%", provider.usagePercent);
  canvas.text(192 - canvas.textWidth(percent, 2), top + 5, percent, 2);

  canvas.progressBar(8, top + 23, 184, 9, provider.usagePercent);
  canvas.text(8, top + 36, provider.resetLabel, 1);
  canvas.line(0, top + 47, 199, top + 47);
}

