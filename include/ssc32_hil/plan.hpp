// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <string>

#include "ssc32_hil/model.hpp"

namespace ssc32_hil {

[[nodiscard]] ExecutionPlan compile_plan(const Profile& profile,
                                         const Scenario& scenario);
[[nodiscard]] std::string render_plan(const ExecutionPlan& plan);
[[nodiscard]] std::string escape_wire(std::string_view value);

}  // namespace ssc32_hil
