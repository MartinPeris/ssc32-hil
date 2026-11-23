// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ssc32_hil {

struct ControllerConfig {
  std::string device;
  unsigned int baud_rate{115200};
  std::chrono::milliseconds timeout{500};
};

struct ChannelConfig {
  std::string name;
  std::uint8_t number{};
  std::uint16_t minimum_us{};
  std::uint16_t home_us{1500};
  std::uint16_t maximum_us{};
  std::uint16_t speed_us_per_second{150};
};

struct Profile {
  std::string name;
  ControllerConfig controller;
  std::vector<ChannelConfig> channels;
};

enum class Action : std::uint8_t { kVersion, kStatus, kPulseQuery, kMove, kPause };

struct TargetSpec {
  std::string channel;
  std::optional<std::uint16_t> absolute_us;
  std::optional<std::int32_t> offset_us;
};

struct ScenarioStep {
  std::string name;
  Action action{Action::kVersion};
  std::vector<std::string> channels;
  std::vector<TargetSpec> targets;
  std::optional<std::chrono::milliseconds> duration;
  std::optional<std::chrono::milliseconds> pause;
  std::optional<bool> expected_moving;
  std::string expected_prefix;
  std::string operator_confirmation;
};

struct Scenario {
  std::string name;
  std::vector<ScenarioStep> steps;
};

struct ResolvedTarget {
  std::string channel_name;
  std::uint8_t channel{};
  std::uint16_t pulse_width_us{};
  std::uint16_t speed_us_per_second{};
};

struct PlannedStep {
  std::string name;
  Action action{Action::kVersion};
  std::vector<ResolvedTarget> targets;
  std::vector<std::uint8_t> channels;
  std::optional<std::chrono::milliseconds> duration;
  std::optional<std::chrono::milliseconds> pause;
  std::optional<bool> expected_moving;
  std::string expected_prefix;
  std::string operator_confirmation;
  std::vector<std::string> wire_preview;
};

struct ExecutionPlan {
  std::string profile_name;
  std::string scenario_name;
  ControllerConfig controller;
  std::vector<ChannelConfig> channels;
  std::vector<PlannedStep> steps;
};

[[nodiscard]] std::string action_name(Action action);

}  // namespace ssc32_hil
