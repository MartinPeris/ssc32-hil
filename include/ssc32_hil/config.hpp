// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <filesystem>
#include <stdexcept>

#include "ssc32_hil/model.hpp"

namespace ssc32_hil {

class ConfigurationError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

[[nodiscard]] Profile load_profile(const std::filesystem::path& path);
[[nodiscard]] Scenario load_scenario(const std::filesystem::path& path);

}  // namespace ssc32_hil
