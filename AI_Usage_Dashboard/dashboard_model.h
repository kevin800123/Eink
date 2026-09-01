#pragma once

#include <Arduino.h>

constexpr size_t kProviderCount = 3;

struct ProviderUsage {
  char name[12];
  uint8_t usagePercent;
  char resetLabel[16];
};

struct DeviceStatus {
  bool wifiConnected;
  int8_t wifiRssi;
  char wifiLabel[12];
  uint8_t batteryPercent;
  char updatedAt[16];
};

struct DashboardData {
  ProviderUsage providers[kProviderCount];
  DeviceStatus device;
  bool isMock;
};

