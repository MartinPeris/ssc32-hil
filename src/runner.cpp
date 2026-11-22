// SPDX-License-Identifier: BSD-3-Clause
#include "ssc32_hil/runner.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

#include "ssc32/controller.hpp"
#include "ssc32/serial_transport.hpp"
#include "ssc32_hil/recording_transport.hpp"

namespace ssc32_hil {
namespace {

using namespace std::chrono_literals;

std::vector<ssc32::ServoCommand> make_commands(const PlannedStep& step) {
  std::vector<ssc32::ServoCommand> commands;
  commands.reserve(step.targets.size());
  for (const auto& target : step.targets) {
    commands.push_back(
        {target.channel, target.pulse_width_us, target.speed_us_per_second});
  }
  return commands;
}

void wait_until_idle(ssc32::Controller& controller,
                     const std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (controller.is_moving(500ms)) {
    if (std::chrono::steady_clock::now() >= deadline) {
      throw std::runtime_error("timed out waiting for movement to complete");
    }
    std::this_thread::sleep_for(20ms);
  }
}

void verify_pulses(ssc32::Controller& controller, const PlannedStep& step,
                   const std::chrono::milliseconds timeout) {
  const auto actual = controller.query_pulse_widths(step.channels, timeout);
  if (actual.size() != step.targets.size()) {
    throw std::runtime_error("pulse query returned an unexpected result count");
  }
  for (std::size_t index = 0; index < actual.size(); ++index) {
    const auto expected = step.targets[index].pulse_width_us;
    const auto difference =
        std::abs(static_cast<int>(actual[index]) - static_cast<int>(expected));
    if (difference > 10) {
      throw std::runtime_error(step.targets[index].channel_name + " reported " +
                               std::to_string(actual[index]) + " us; expected " +
                               std::to_string(expected) +
                               " us (10 us quantization tolerance)");
    }
  }
}

}  // namespace

RunEvidence run_plan(const ExecutionPlan& plan, const Confirmation& confirm) {
  RunEvidence evidence;
  ssc32::SerialTransport serial(plan.controller.device, plan.controller.baud_rate);
  RecordingTransport recording(serial);
  ssc32::Controller controller(recording);

  try {
    for (const auto& step : plan.steps) {
      std::string detail = "ok";
      switch (step.action) {
        case Action::kVersion: {
          const auto version = controller.version(plan.controller.timeout);
          if (!step.expected_prefix.empty() &&
              version.rfind(step.expected_prefix, 0U) != 0U) {
            throw std::runtime_error("version response '" + version +
                                     "' does not start with '" + step.expected_prefix +
                                     "'");
          }
          detail = version;
          break;
        }
        case Action::kStatus: {
          const bool moving = controller.is_moving(plan.controller.timeout);
          if (moving != *step.expected_moving) {
            throw std::runtime_error("movement status did not match expectation");
          }
          detail = moving ? "moving" : "idle";
          break;
        }
        case Action::kPulseQuery: {
          const auto pulses =
              controller.query_pulse_widths(step.channels, plan.controller.timeout);
          detail = std::to_string(pulses.size()) + " pulse width(s) read";
          break;
        }
        case Action::kMove: {
          controller.move(make_commands(step), step.duration);
          wait_until_idle(controller, *step.duration + plan.controller.timeout + 2s);
          verify_pulses(controller, step, plan.controller.timeout);
          detail =
              std::to_string(step.targets.size()) + " target(s) completed and echoed";
          break;
        }
        case Action::kPause:
          std::this_thread::sleep_for(*step.pause);
          detail = "paused for " + std::to_string(step.pause->count()) + " ms";
          break;
      }

      if (!step.operator_confirmation.empty() && !confirm(step.operator_confirmation)) {
        throw std::runtime_error("operator rejected confirmation for step '" +
                                 step.name + "'");
      }
      evidence.steps.push_back({step.name, true, std::move(detail)});
      std::cout << "PASS " << step.name << '\n';
    }
    evidence.passed = true;
  } catch (const std::exception& error) {
    evidence.failure = error.what();
    evidence.steps.push_back({"run", false, evidence.failure});
    try {
      controller.cancel();
    } catch (...) {
      evidence.failure += "; best-effort cancel also failed";
    }
  }
  evidence.transport = recording.events();
  return evidence;
}

}  // namespace ssc32_hil
