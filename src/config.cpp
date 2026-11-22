// SPDX-License-Identifier: BSD-3-Clause
#include "ssc32_hil/config.hpp"

#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <toml++/toml.hpp>

namespace ssc32_hil {
namespace {

template <typename T>
T require_value(const toml::table& table, const std::string_view key,
                const std::string_view context) {
  const auto value = table[key].value<T>();
  if (!value) {
    throw ConfigurationError(std::string(context) + ": missing or invalid '" +
                             std::string(key) + "'");
  }
  return *value;
}

template <typename Target>
Target checked_integer(const std::int64_t value, const std::string_view field) {
  if (value < static_cast<std::int64_t>(std::numeric_limits<Target>::min()) ||
      value > static_cast<std::int64_t>(std::numeric_limits<Target>::max())) {
    throw ConfigurationError(std::string(field) + " is out of range");
  }
  return static_cast<Target>(value);
}

Action parse_action(const std::string& action) {
  if (action == "version") {
    return Action::kVersion;
  }
  if (action == "status") {
    return Action::kStatus;
  }
  if (action == "pulse_query") {
    return Action::kPulseQuery;
  }
  if (action == "move") {
    return Action::kMove;
  }
  if (action == "pause") {
    return Action::kPause;
  }
  throw ConfigurationError("unknown scenario action: " + action);
}

std::vector<std::string> parse_string_array(
    const toml::node_view<const toml::node>& node, const std::string_view context) {
  std::vector<std::string> result;
  const auto* array = node.as_array();
  if (array == nullptr) {
    throw ConfigurationError(std::string(context) + " must be an array");
  }
  for (const auto& item : *array) {
    const auto value = item.value<std::string>();
    if (!value) {
      throw ConfigurationError(std::string(context) + " must contain only strings");
    }
    result.push_back(*value);
  }
  return result;
}

}  // namespace

Profile load_profile(const std::filesystem::path& path) {
  try {
    const auto document = toml::parse_file(path.string());
    Profile profile;
    profile.name = require_value<std::string>(document, "name", "profile");

    const auto* controller = document["controller"].as_table();
    if (controller == nullptr) {
      throw ConfigurationError("profile: missing [controller] table");
    }
    profile.controller.device =
        require_value<std::string>(*controller, "device", "controller");
    profile.controller.baud_rate = checked_integer<unsigned int>(
        require_value<std::int64_t>(*controller, "baud", "controller"),
        "controller.baud");
    profile.controller.timeout = std::chrono::milliseconds(
        require_value<std::int64_t>(*controller, "timeout_ms", "controller"));
    if (profile.controller.timeout.count() <= 0) {
      throw ConfigurationError("controller.timeout_ms must be positive");
    }

    const auto* channels = document["channels"].as_array();
    if (channels == nullptr || !channels->is_array_of_tables() || channels->empty()) {
      throw ConfigurationError("profile: [[channels]] must not be empty");
    }
    for (const auto& node : *channels) {
      const auto& table = *node.as_table();
      ChannelConfig channel;
      channel.name = require_value<std::string>(table, "name", "channel");
      channel.number = checked_integer<std::uint8_t>(
          require_value<std::int64_t>(table, "number", channel.name),
          channel.name + ".number");
      channel.minimum_us = checked_integer<std::uint16_t>(
          require_value<std::int64_t>(table, "minimum_us", channel.name),
          channel.name + ".minimum_us");
      channel.home_us = checked_integer<std::uint16_t>(
          require_value<std::int64_t>(table, "home_us", channel.name),
          channel.name + ".home_us");
      channel.maximum_us = checked_integer<std::uint16_t>(
          require_value<std::int64_t>(table, "maximum_us", channel.name),
          channel.name + ".maximum_us");
      channel.speed_us_per_second = checked_integer<std::uint16_t>(
          require_value<std::int64_t>(table, "speed_us_per_second", channel.name),
          channel.name + ".speed_us_per_second");
      profile.channels.push_back(std::move(channel));
    }
    return profile;
  } catch (const toml::parse_error& error) {
    throw ConfigurationError("failed to parse " + path.string() + ": " +
                             std::string(error.description()));
  }
}

Scenario load_scenario(const std::filesystem::path& path) {
  try {
    const auto document = toml::parse_file(path.string());
    Scenario scenario;
    scenario.name = require_value<std::string>(document, "name", "scenario");

    const auto* steps = document["steps"].as_array();
    if (steps == nullptr || !steps->is_array_of_tables() || steps->empty()) {
      throw ConfigurationError("scenario: [[steps]] must not be empty");
    }
    for (const auto& node : *steps) {
      const auto& table = *node.as_table();
      ScenarioStep step;
      step.name = require_value<std::string>(table, "name", "step");
      step.action =
          parse_action(require_value<std::string>(table, "action", step.name));

      if (table.contains("channels")) {
        step.channels = parse_string_array(table["channels"], step.name + ".channels");
      }
      if (const auto value = table["duration_ms"].value<std::int64_t>()) {
        step.duration = std::chrono::milliseconds(*value);
      }
      if (const auto value = table["pause_ms"].value<std::int64_t>()) {
        step.pause = std::chrono::milliseconds(*value);
      }
      step.expected_moving = table["expect_moving"].value<bool>();
      step.expected_prefix = table["expect_prefix"].value_or<std::string>("");
      step.operator_confirmation =
          table["operator_confirmation"].value_or<std::string>("");

      if (const auto* targets = table["targets"].as_table()) {
        for (const auto& [name, target_node] : *targets) {
          const auto* target_table = target_node.as_table();
          if (target_table == nullptr) {
            throw ConfigurationError(step.name + ".targets." + std::string(name.str()) +
                                     " must be a table");
          }
          TargetSpec target;
          target.channel = std::string(name.str());
          if (const auto value = (*target_table)["pulse_us"].value<std::int64_t>()) {
            target.absolute_us = checked_integer<std::uint16_t>(
                *value, step.name + ".targets." + target.channel + ".pulse_us");
          }
          if (const auto value = (*target_table)["offset_us"].value<std::int64_t>()) {
            target.offset_us = checked_integer<std::int32_t>(
                *value, step.name + ".targets." + target.channel + ".offset_us");
          }
          step.targets.push_back(std::move(target));
        }
      }
      scenario.steps.push_back(std::move(step));
    }
    return scenario;
  } catch (const toml::parse_error& error) {
    throw ConfigurationError("failed to parse " + path.string() + ": " +
                             std::string(error.description()));
  }
}

std::string action_name(const Action action) {
  switch (action) {
    case Action::kVersion:
      return "version";
    case Action::kStatus:
      return "status";
    case Action::kPulseQuery:
      return "pulse_query";
    case Action::kMove:
      return "move";
    case Action::kPause:
      return "pause";
  }
  return "unknown";
}

}  // namespace ssc32_hil
