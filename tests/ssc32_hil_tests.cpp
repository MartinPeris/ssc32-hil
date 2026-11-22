// SPDX-License-Identifier: BSD-3-Clause
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

#include "ssc32_hil/config.hpp"
#include "ssc32_hil/plan.hpp"

namespace {

int failures = 0;

void expect(const bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void test_good_plan() {
  const std::string root = SSC32_HIL_SOURCE_DIR;
  const auto profile =
      ssc32_hil::load_profile(root + "/profiles/al5c-conservative.toml");
  const auto scenario =
      ssc32_hil::load_scenario(root + "/scenarios/single-channel-smoke.toml");
  const auto plan = ssc32_hil::compile_plan(profile, scenario);
  expect(plan.steps.size() == 3U, "single-channel scenario step count");
  expect(plan.steps[1].targets.size() == 1U, "move target count");
  expect(plan.steps[1].targets[0].channel == 0U, "base channel resolution");
  expect(plan.steps[1].targets[0].pulse_width_us == 1550U, "relative pulse resolution");
  expect(plan.steps[1].wire_preview.front() == "#0 P1550 S100 T1000 \r",
         "exact move command preview");
  const auto rendered = ssc32_hil::render_plan(plan);
  expect(rendered.find("TX \"#0 P1550 S100 T1000 \\r\"") != std::string::npos,
         "rendered wire command");
}

void test_duplicate_channel_rejected() {
  const std::string root = SSC32_HIL_SOURCE_DIR;
  try {
    const auto profile =
        ssc32_hil::load_profile(root + "/tests/fixtures/duplicate-channel.toml");
    const auto scenario =
        ssc32_hil::load_scenario(root + "/scenarios/controller-smoke.toml");
    (void)ssc32_hil::compile_plan(profile, scenario);
    expect(false, "duplicate channel should be rejected");
  } catch (const ssc32_hil::ConfigurationError&) {
  }
}

void test_safety_envelope_rejected() {
  const ssc32_hil::Profile profile{
      "test",
      {"/dev/null", 115200U, std::chrono::milliseconds(100)},
      {{"joint", 0U, 1450U, 1500U, 1550U, 100U}}};
  const ssc32_hil::Scenario scenario{"unsafe",
                                     {{"unsafe move",
                                       ssc32_hil::Action::kMove,
                                       {},
                                       {{"joint", std::nullopt, 100}},
                                       std::chrono::milliseconds(1000),
                                       std::nullopt,
                                       std::nullopt,
                                       "",
                                       ""}}};
  try {
    (void)ssc32_hil::compile_plan(profile, scenario);
    expect(false, "unsafe offset should be rejected");
  } catch (const ssc32_hil::ConfigurationError&) {
  }
}

}  // namespace

int main() {
  try {
    test_good_plan();
    test_duplicate_channel_rejected();
    test_safety_envelope_rejected();
  } catch (const std::exception& error) {
    std::cerr << "Unexpected exception: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
  if (failures != 0) {
    return EXIT_FAILURE;
  }
  std::cout << "All ssc32-hil tests passed\n";
  return EXIT_SUCCESS;
}
