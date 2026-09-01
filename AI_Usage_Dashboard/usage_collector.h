#pragma once

#include "dashboard_model.h"

class UsageCollector {
 public:
  virtual ~UsageCollector() = default;
  virtual bool begin() = 0;
  virtual bool fetch(DashboardData& output) = 0;
  virtual const char* lastError() const = 0;
};

