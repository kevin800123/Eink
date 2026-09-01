#pragma once

#include "dashboard_model.h"

class DeviceStatusService {
 public:
  void begin();
  void apply(DashboardData& data);

 private:
  uint8_t readBatteryPercent();
  void updateTimeLabel(DeviceStatus& status);
};

