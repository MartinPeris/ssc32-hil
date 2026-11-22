// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "ssc32_hil/model.hpp"
#include "ssc32_hil/recording_transport.hpp"

namespace ssc32_hil {

struct StepResult {
  std::string name;
  bool passed{};
  std::string detail;
};

struct RunEvidence {
  bool passed{};
  std::string failure;
  std::vector<StepResult> steps;
  std::vector<TransportEvent> transport;
};

void write_json_report(const std::filesystem::path& path, const ExecutionPlan& plan,
                       const RunEvidence& evidence);
void write_junit_report(const std::filesystem::path& path, const ExecutionPlan& plan,
                        const RunEvidence& evidence);

}  // namespace ssc32_hil
