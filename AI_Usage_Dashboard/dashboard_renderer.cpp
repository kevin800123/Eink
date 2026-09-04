#include "dashboard_renderer.h"

#include <stdio.h>

namespace {

// Only Claude and Codex are shown; Gemini has no official quota source, so its
// row would be dead space. The collector still returns it, we just skip it.
constexpr size_t kShownProviders = 2;

// Draw a labeled window bar: "5H [========....]  21%".
void windowRow(Canvas1Bit& canvas, int16_t y, const char* label,
               uint8_t percent) {
  canvas.text(6, y + 1, label, 1);
  char pct[6];
  snprintf(pct, sizeof(pct), "%u%%", percent);
  const int16_t pctWidth = canvas.textWidth(pct, 1);
  const int16_t barLeft = 28;
  const int16_t barRight = 190 - pctWidth - 4;
  canvas.progressBar(barLeft, y, barRight - barLeft, 9, percent);
  canvas.text(190 - pctWidth, y + 1, pct, 1);
}

}  // namespace

void DashboardRenderer::render(Canvas1Bit& canvas, const DashboardData& data) {
  canvas.clear(false);
  canvas.rect(0, 0, 200, 200, true);

  canvas.text(6, 5, "AI USAGE", 2);
  canvas.wifiIcon(143, 5, data.device.wifiConnected, data.device.wifiRssi);
  canvas.batteryIcon(167, 5, data.device.batteryPercent);
  canvas.line(0, 24, 199, 24);

  // Two provider blocks fill the space between the header and footer.
  const int16_t blockTops[kShownProviders] = {28, 101};
  for (size_t index = 0; index < kShownProviders; ++index) {
    providerRow(canvas, blockTops[index], data.providers[index]);
  }
  canvas.line(0, 99, 199, 99);

  canvas.line(0, 173, 199, 173);
  char status[28];
  snprintf(status, sizeof(status), "BAT %u%% WIFI %s",
           data.device.batteryPercent, data.device.wifiLabel);
  canvas.text(7, 178, status, 1);
  if (data.isMock) {
    const char* label = "MOCK";
    canvas.text(193 - canvas.textWidth(label, 1), 178, label, 1);
  }

  char updated[24];
  snprintf(updated, sizeof(updated), "UPDATED %s", data.device.updatedAt);
  canvas.text(7, 189, updated, 1);
}

void DashboardRenderer::providerRow(Canvas1Bit& canvas, int16_t top,
                                    const ProviderUsage& provider) {
  canvas.text(8, top + 2, provider.name, 2);

  if (!provider.available) {
    canvas.text(8, top + 30, "UNAVAILABLE", 1);
    canvas.text(8, top + 44, provider.resetLabel, 1);
    return;
  }

  windowRow(canvas, top + 22, "5H", provider.fiveHourPercent);
  windowRow(canvas, top + 37, "WK", provider.weeklyPercent);
  canvas.text(8, top + 53, provider.resetLabel, 1);
}
