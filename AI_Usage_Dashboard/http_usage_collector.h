#pragma once

#include "usage_collector.h"

// Fetches the normalized dashboard JSON from the local collector
// (tools/collector/usage_collector.py) over HTTP and fills DashboardData.
//
// The device holds no provider credential. It only holds a device token for the
// collector, which serves GET /v1/dashboard on the LAN. See docs/API_CONTRACT.md
// and tools/collector/README.md. This v1 talks plain HTTP, matching the
// collector's documented LAN-only deviation; put it behind TLS before exposing
// it beyond a network you control.
class HttpUsageCollector final : public UsageCollector {
 public:
  bool begin() override;
  bool fetch(DashboardData& output) override;
  const char* lastError() const override;

 private:
  char error_[48] = "ok";
};
