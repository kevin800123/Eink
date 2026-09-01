#pragma once

#include "canvas_1bit.h"
#include "dashboard_model.h"

class DashboardRenderer {
 public:
  void render(Canvas1Bit& canvas, const DashboardData& data);

 private:
  void providerRow(Canvas1Bit& canvas, int16_t top,
                   const ProviderUsage& provider);
};

