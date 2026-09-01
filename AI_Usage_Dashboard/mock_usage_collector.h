#pragma once

#include "usage_collector.h"

class MockUsageCollector final : public UsageCollector {
 public:
  bool begin() override;
  bool fetch(DashboardData& output) override;
  const char* lastError() const override;
};

