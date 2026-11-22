// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <functional>
#include <string>

#include "ssc32_hil/evidence.hpp"
#include "ssc32_hil/model.hpp"

namespace ssc32_hil {

using Confirmation = std::function<bool(const std::string&)>;

[[nodiscard]] RunEvidence run_plan(const ExecutionPlan& plan,
                                   const Confirmation& confirm);

}  // namespace ssc32_hil
