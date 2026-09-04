#pragma once

#include <Arduino.h>

constexpr size_t kProviderCount = 3;

struct ProviderUsage {
  char name[12];
  bool available;          // false => show as unavailable, never a fake number
  uint8_t fiveHourPercent; // 5-hour window
  uint8_t weeklyPercent;   // 7-day window
  char resetLabel[16];     // e.g. "RESET 3H 12M" (from the 5-hour reset)
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

