// SPDX-License-Identifier: BSD-3-Clause
#include "ssc32_hil/plan.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

#include "ssc32/protocol.hpp"
#include "ssc32_hil/config.hpp"

namespace ssc32_hil {
namespace {

using ChannelMap = std::map<std::string, const ChannelConfig*>;

const ChannelConfig& find_channel(const ChannelMap& channels, const std::string& name) {
  const auto iterator = channels.find(name);
  if (iterator == channels.end()) {
    throw ConfigurationError("scenario references undeclared channel '" + name + "'");
  }
  return *iterator->second;
}

std::string byte_count_preview(const std::size_t count) {
  return "<read " + std::to_string(count) + " byte" + (count == 1U ? "" : "s") + ">";
}

ChannelMap validate_profile(const Profile& profile) {
  if (profile.controller.device.empty()) {
    throw ConfigurationError("controller.device must not be empty");
  }

  ChannelMap channels_by_name;
  std::set<std::uint8_t> channel_numbers;
  for (const auto& channel : profile.channels) {
    if (channel.name.empty()) {
      throw ConfigurationError("channel name must not be empty");
    }
    if (!channels_by_name.emplace(channel.name, &channel).second) {
      throw ConfigurationError("duplicate channel name: " + channel.name);
    }
    if (!channel_numbers.emplace(channel.number).second) {
      throw ConfigurationError("duplicate channel number: " +
                               std::to_string(channel.number));
    }
    if (channel.number > ssc32::kMaxChannel) {
      throw ConfigurationError(channel.name + ".number exceeds 31");
    }
    if (channel.minimum_us < ssc32::kMinPulseWidthUs ||
        channel.maximum_us > ssc32::kMaxPulseWidthUs ||
        channel.minimum_us > channel.home_us || channel.home_us > channel.maximum_us) {
      throw ConfigurationError(channel.name + " has an invalid pulse safety envelope");
    }
    if (channel.speed_us_per_second == 0U) {
      throw ConfigurationError(channel.name + ".speed_us_per_second must be positive");
    }
  }
  return channels_by_name;
}

PlannedStep make_planned_step(const ScenarioStep& source) {
  return {source.name,
          source.action,
          {},
          {},
          source.duration,
          source.pause,
          source.expected_moving,
          source.expected_prefix,
          source.operator_confirmation,
          {}};
}

void compile_pulse_query(const ScenarioStep& source, const ChannelMap& channels_by_name,
                         PlannedStep& step) {
  if (source.channels.empty()) {
    throw ConfigurationError(source.name + " requires channels");
  }
  std::set<std::uint8_t> seen;
  for (const auto& name : source.channels) {
    const auto& channel = find_channel(channels_by_name, name);
    if (!seen.emplace(channel.number).second) {
      throw ConfigurationError(source.name + " repeats channel " + name);
    }
    step.channels.push_back(channel.number);
  }
  step.wire_preview = {ssc32::Protocol::query_pulse_widths(step.channels),
                       byte_count_preview(step.channels.size())};
}

std::uint16_t resolve_target(const ScenarioStep& source, const TargetSpec& target,
                             const ChannelConfig& channel) {
  if (target.absolute_us.has_value() == target.offset_us.has_value()) {
    throw ConfigurationError(source.name + ".targets." + target.channel +
                             " requires exactly one of pulse_us or offset_us");
  }
  const auto resolved = target.absolute_us
                            ? static_cast<std::int64_t>(*target.absolute_us)
                            : static_cast<std::int64_t>(channel.home_us) +
                                  static_cast<std::int64_t>(*target.offset_us);
  if (resolved < channel.minimum_us || resolved > channel.maximum_us) {
    throw ConfigurationError(source.name + ".targets." + target.channel +
                             " exceeds the configured safety envelope");
  }
  return static_cast<std::uint16_t>(resolved);
}

void compile_move(const ScenarioStep& source, const ChannelMap& channels_by_name,
                  PlannedStep& step) {
  if (!source.duration || source.duration->count() <= 0) {
    throw ConfigurationError(source.name + " requires a positive duration_ms");
  }
  if (source.targets.empty()) {
    throw ConfigurationError(source.name + " requires targets");
  }

  std::vector<ssc32::ServoCommand> commands;
  std::set<std::uint8_t> seen;
  for (const auto& target : source.targets) {
    const auto& channel = find_channel(channels_by_name, target.channel);
    if (!seen.emplace(channel.number).second) {
      throw ConfigurationError(source.name + " repeats channel " + target.channel);
    }
    const auto pulse = resolve_target(source, target, channel);
    step.targets.push_back(
        {target.channel, channel.number, pulse, channel.speed_us_per_second});
    commands.push_back({channel.number, pulse, channel.speed_us_per_second});
    step.channels.push_back(channel.number);
  }
  step.wire_preview = {ssc32::Protocol::move(commands, source.duration),
                       ssc32::Protocol::query_movement_status(), byte_count_preview(1U),
                       ssc32::Protocol::query_pulse_widths(step.channels),
                       byte_count_preview(step.channels.size())};
}

void compile_action(const ScenarioStep& source, const ChannelMap& channels_by_name,
                    PlannedStep& step) {
  switch (source.action) {
    case Action::kVersion:
      step.wire_preview = {ssc32::Protocol::version(), "<read until CR>"};
      return;
    case Action::kStatus:
      if (!source.expected_moving) {
        throw ConfigurationError(source.name + " requires expect_moving=true or false");
      }
      step.wire_preview = {ssc32::Protocol::query_movement_status(),
                           byte_count_preview(1U)};
      return;
    case Action::kPulseQuery:
      compile_pulse_query(source, channels_by_name, step);
      return;
    case Action::kMove:
      compile_move(source, channels_by_name, step);
      return;
    case Action::kPause:
      if (!source.pause || source.pause->count() <= 0) {
        throw ConfigurationError(source.name + " requires a positive pause_ms");
      }
      step.wire_preview = {"<sleep " + std::to_string(source.pause->count()) + " ms>"};
      return;
  }
}

}  // namespace

ExecutionPlan compile_plan(const Profile& profile, const Scenario& scenario) {
  const auto channels_by_name = validate_profile(profile);

  ExecutionPlan plan{
      profile.name, scenario.name, profile.controller, profile.channels, {}};
  std::set<std::string> step_names;
  for (const auto& source : scenario.steps) {
    if (source.name.empty() || !step_names.emplace(source.name).second) {
      throw ConfigurationError("step names must be non-empty and unique");
    }
    auto step = make_planned_step(source);
    compile_action(source, channels_by_name, step);
    plan.steps.push_back(std::move(step));
  }
  return plan;
}

std::string escape_wire(const std::string_view value) {
  std::ostringstream output;
  output << '"';
  for (const char raw_character : value) {
    const auto character = static_cast<unsigned char>(raw_character);
    switch (character) {
      case '\r':
        output << "\\r";
        break;
      case '\n':
        output << "\\n";
        break;
      case '\t':
        output << "\\t";
        break;
      case '\\':
        output << "\\\\";
        break;
      case '"':
        output << "\\\"";
        break;
      default:
        if (std::isprint(character) != 0) {
          output << static_cast<char>(character);
        } else {
          output << "\\x" << std::hex << std::setw(2) << std::setfill('0')
                 << static_cast<unsigned int>(character) << std::dec;
        }
    }
  }
  output << '"';
  return output.str();
}

std::string render_plan(const ExecutionPlan& plan) {
  std::ostringstream output;
  output << "Profile: " << plan.profile_name << '\n'
         << "Scenario: " << plan.scenario_name << '\n'
         << "Device: " << plan.controller.device << " @ " << plan.controller.baud_rate
         << " baud\n\n"
         << "Channels:\n";
  for (const auto& channel : plan.channels) {
    output << "  " << channel.name << " -> "
           << static_cast<unsigned int>(channel.number) << " (VS"
           << (channel.number < 16U ? 1 : 2) << ", " << channel.minimum_us << ".."
           << channel.maximum_us << " us, home " << channel.home_us << " us)\n";
  }
  output << "\nSteps:\n";
  for (std::size_t index = 0; index < plan.steps.size(); ++index) {
    const auto& step = plan.steps[index];
    output << "  " << (index + 1U) << ". " << step.name << " ["
           << action_name(step.action) << "]\n";
    for (const auto& item : step.wire_preview) {
      if (!item.empty() && item.front() == '<') {
        output << "     " << item << '\n';
      } else {
        output << "     TX " << escape_wire(item) << '\n';
      }
    }
  }
  return output.str();
}

}  // namespace ssc32_hil
