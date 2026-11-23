// SPDX-License-Identifier: BSD-3-Clause
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "ssc32/transport.hpp"
#include "ssc32_hil/config.hpp"
#include "ssc32_hil/evidence.hpp"
#include "ssc32_hil/plan.hpp"
#include "ssc32_hil/recording_transport.hpp"
#include "ssc32_hil/runner.hpp"

namespace {

using namespace std::chrono_literals;

int failures = 0;

void expect(const bool condition, const std::string_view name) {
  if (!condition) {
    std::cerr << "FAIL: " << name << '\n';
    ++failures;
  }
}

template <typename Exception, typename Callable>
void expect_throws(Callable&& callable, const std::string_view name) {
  try {
    callable();
    std::cerr << "FAIL: " << name << " did not throw\n";
    ++failures;
  } catch (const Exception&) {
  }
}

class FakeTransport final : public ssc32::Transport {
 public:
  void write_all(const std::string_view data) override {
    writes.emplace_back(data);
    if (fail_cancel && data == std::string_view("\x1B\r", 2U)) {
      throw std::runtime_error("cancel failed");
    }
  }

  std::vector<std::uint8_t> read_exact(const std::size_t size,
                                       std::chrono::milliseconds) override {
    if (binary.empty()) {
      throw std::runtime_error("no binary response queued");
    }
    auto response = std::move(binary.front());
    binary.pop_front();
    if (response.size() != size) {
      throw std::runtime_error("unexpected binary response size");
    }
    return response;
  }

  std::string read_until(char, std::size_t, std::chrono::milliseconds) override {
    if (text.empty()) {
      throw std::runtime_error("no text response queued");
    }
    auto response = std::move(text.front());
    text.pop_front();
    return response;
  }

  std::vector<std::string> writes;
  std::deque<std::vector<std::uint8_t>> binary;
  std::deque<std::string> text;
  bool fail_cancel{};
};

ssc32_hil::ChannelConfig channel(const std::string& name, const std::uint8_t number) {
  return {name, number, 1000U, 1500U, 2000U, 200U};
}

ssc32_hil::Profile valid_profile() {
  return {"profile",
          {"/dev/null", 115200U, 20ms},
          {channel("base", 0U), channel("shoulder", 1U)}};
}

ssc32_hil::ScenarioStep step(const std::string& name, const ssc32_hil::Action action) {
  ssc32_hil::ScenarioStep value;
  value.name = name;
  value.action = action;
  return value;
}

ssc32_hil::ExecutionPlan full_plan() {
  auto version = step("version", ssc32_hil::Action::kVersion);
  version.expected_prefix = "SSC32";

  auto status = step("status", ssc32_hil::Action::kStatus);
  status.expected_moving = false;

  auto query = step("query", ssc32_hil::Action::kPulseQuery);
  query.channels = {"base", "shoulder"};

  auto move = step("move", ssc32_hil::Action::kMove);
  move.duration = 1ms;
  move.targets = {{"base", 1550U, std::nullopt}, {"shoulder", 1650U, std::nullopt}};

  auto pause = step("pause", ssc32_hil::Action::kPause);
  pause.pause = 1ms;
  pause.operator_confirmation = "still safe?";

  return ssc32_hil::compile_plan(valid_profile(),
                                 {"scenario", {version, status, query, move, pause}});
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void test_recording_transport_and_evidence() {
  FakeTransport inner;
  inner.text.push_back("version");
  inner.binary.push_back({1U, 2U});
  ssc32_hil::RecordingTransport recording(inner);
  recording.write_all("TX");
  expect(recording.read_until('\r', 16U, 10ms) == "version", "recording text response");
  expect(recording.read_exact(2U, 10ms) == std::vector<std::uint8_t>({1U, 2U}),
         "recording binary response");
  expect(recording.events().size() == 3U, "recording event count");

  auto plan = full_plan();
  plan.profile_name = "profile\"\n";
  plan.scenario_name = "scenario<&\"'";
  plan.controller.device = std::string("device\t") + static_cast<char>(1);
  const ssc32_hil::RunEvidence evidence{
      false,
      "failure\n\"",
      {{"good<&", true, "ok"}, {"bad\"", false, "wrong<&\"'"}},
      {{1ms, "tx", {0U, 15U, 255U}}, {2ms, "rx", {42U}}}};

  const auto directory = std::filesystem::temp_directory_path() /
                         ("ssc32-hil-coverage-" + std::to_string(getpid()));
  std::filesystem::create_directories(directory);
  const auto json_path = directory / "report.json";
  const auto xml_path = directory / "report.xml";
  ssc32_hil::write_json_report(json_path, plan, evidence);
  ssc32_hil::write_junit_report(xml_path, plan, evidence);
  const auto json = read_file(json_path);
  const auto xml = read_file(xml_path);
  expect(json.find("profile\\\"\\n") != std::string::npos,
         "JSON escapes quotes and newlines");
  expect(json.find("device\\t\\u0001") != std::string::npos,
         "JSON escapes tabs and control bytes");
  expect(json.find("00 0f ff") != std::string::npos, "JSON renders transport bytes");
  expect(xml.find("failures=\"1\"") != std::string::npos,
         "JUnit reports failure count");
  expect(xml.find("scenario&lt;&amp;&quot;&apos;") != std::string::npos,
         "XML escapes special characters");
  std::filesystem::remove_all(directory);

  expect_throws<std::runtime_error>(
      [&plan, &evidence] {
        ssc32_hil::write_json_report("/definitely/missing/report.json", plan, evidence);
      },
      "report open failure");
}

void test_successful_runner() {
  FakeTransport transport;
  transport.text.push_back("SSC32-V2.03XE");
  transport.binary.push_back({static_cast<std::uint8_t>('.')});
  transport.binary.push_back({150U, 160U});
  transport.binary.push_back({static_cast<std::uint8_t>('+')});
  transport.binary.push_back({static_cast<std::uint8_t>('.')});
  transport.binary.push_back({155U, 165U});
  int confirmations = 0;
  const auto evidence = ssc32_hil::run_plan_with_transport(
      full_plan(), transport, [&confirmations](const std::string&) {
        ++confirmations;
        return true;
      });
  expect(evidence.passed, "full plan passes");
  expect(evidence.steps.size() == 5U, "all actions produce step evidence");
  expect(confirmations == 1, "operator confirmation invoked");
  expect(!evidence.transport.empty(), "runner records transport");
}

ssc32_hil::ExecutionPlan one_step_plan(ssc32_hil::ScenarioStep source) {
  return ssc32_hil::compile_plan(valid_profile(), {"failure", {std::move(source)}});
}

void test_runner_failures() {
  auto version = step("version", ssc32_hil::Action::kVersion);
  version.expected_prefix = "SSC32";
  FakeTransport bad_version;
  bad_version.text.push_back("OTHER");
  auto evidence = ssc32_hil::run_plan_with_transport(
      one_step_plan(version), bad_version, [](const std::string&) { return true; });
  expect(
      !evidence.passed && evidence.failure.find("does not start") != std::string::npos,
      "version mismatch fails and records evidence");
  expect(bad_version.writes.back() == std::string("\x1B\r", 2U),
         "failure sends best-effort cancel");

  FakeTransport cancel_failure;
  cancel_failure.text.push_back("OTHER");
  cancel_failure.fail_cancel = true;
  evidence = ssc32_hil::run_plan_with_transport(
      one_step_plan(version), cancel_failure, [](const std::string&) { return true; });
  expect(evidence.failure.find("cancel also failed") != std::string::npos,
         "cancel failure is retained in evidence");

  auto status = step("status", ssc32_hil::Action::kStatus);
  status.expected_moving = false;
  FakeTransport wrong_status;
  wrong_status.binary.push_back({static_cast<std::uint8_t>('+')});
  evidence = ssc32_hil::run_plan_with_transport(
      one_step_plan(status), wrong_status, [](const std::string&) { return true; });
  expect(evidence.failure.find("status") != std::string::npos, "status mismatch fails");

  version.operator_confirmation = "approve";
  FakeTransport rejected;
  rejected.text.push_back("SSC32-V2.03XE");
  evidence = ssc32_hil::run_plan_with_transport(
      one_step_plan(version), rejected, [](const std::string&) { return false; });
  expect(evidence.failure.find("operator rejected") != std::string::npos,
         "operator rejection fails");

  auto move = step("move", ssc32_hil::Action::kMove);
  move.duration = 1ms;
  move.targets = {{"base", 1550U, std::nullopt}};
  auto mismatched_plan = one_step_plan(move);
  mismatched_plan.steps.front().channels.push_back(1U);
  FakeTransport wrong_count;
  wrong_count.binary.push_back({static_cast<std::uint8_t>('.')});
  wrong_count.binary.push_back({155U, 160U});
  evidence = ssc32_hil::run_plan_with_transport(
      mismatched_plan, wrong_count, [](const std::string&) { return true; });
  expect(evidence.failure.find("result count") != std::string::npos,
         "pulse count mismatch fails");

  FakeTransport wrong_pulse;
  wrong_pulse.binary.push_back({static_cast<std::uint8_t>('.')});
  wrong_pulse.binary.push_back({100U});
  evidence = ssc32_hil::run_plan_with_transport(
      one_step_plan(move), wrong_pulse, [](const std::string&) { return true; });
  expect(evidence.failure.find("reported") != std::string::npos,
         "pulse tolerance mismatch fails");

  auto missing_device = full_plan();
  missing_device.controller.device = "/definitely/missing/ssc32";
  expect_throws<std::system_error>(
      [&missing_device] {
        (void)ssc32_hil::run_plan(missing_device,
                                  [](const std::string&) { return true; });
      },
      "real runner reports serial open failure");
}

void expect_configuration_error(const std::string_view name,
                                const std::function<void()>& callable) {
  expect_throws<ssc32_hil::ConfigurationError>(callable, name);
}

void test_plan_validation_paths() {
  auto profile = valid_profile();
  profile.controller.device.clear();
  expect_configuration_error("empty device", [&profile] {
    (void)ssc32_hil::compile_plan(profile, {"scenario", {}});
  });

  profile = valid_profile();
  profile.channels[0].name.clear();
  expect_configuration_error("empty channel name", [&profile] {
    (void)ssc32_hil::compile_plan(profile, {"scenario", {}});
  });
  profile = valid_profile();
  profile.channels[1].name = profile.channels[0].name;
  expect_configuration_error("duplicate channel name", [&profile] {
    (void)ssc32_hil::compile_plan(profile, {"scenario", {}});
  });
  profile = valid_profile();
  profile.channels[1].number = profile.channels[0].number;
  expect_configuration_error("duplicate channel number", [&profile] {
    (void)ssc32_hil::compile_plan(profile, {"scenario", {}});
  });
  profile = valid_profile();
  profile.channels[0].number = 32U;
  expect_configuration_error("channel above 31", [&profile] {
    (void)ssc32_hil::compile_plan(profile, {"scenario", {}});
  });
  profile = valid_profile();
  profile.channels[0].minimum_us = 499U;
  expect_configuration_error("invalid envelope", [&profile] {
    (void)ssc32_hil::compile_plan(profile, {"scenario", {}});
  });
  profile = valid_profile();
  profile.channels[0].speed_us_per_second = 0U;
  expect_configuration_error("zero speed", [&profile] {
    (void)ssc32_hil::compile_plan(profile, {"scenario", {}});
  });

  auto status = step("status", ssc32_hil::Action::kStatus);
  expect_configuration_error("status expectation required",
                             [&status] { (void)one_step_plan(status); });
  auto query = step("query", ssc32_hil::Action::kPulseQuery);
  expect_configuration_error("query channels required",
                             [&query] { (void)one_step_plan(query); });
  query.channels = {"missing"};
  expect_configuration_error("unknown query channel",
                             [&query] { (void)one_step_plan(query); });
  query.channels = {"base", "base"};
  expect_configuration_error("duplicate query channel",
                             [&query] { (void)one_step_plan(query); });

  auto move = step("move", ssc32_hil::Action::kMove);
  expect_configuration_error("move duration required",
                             [&move] { (void)one_step_plan(move); });
  move.duration = 1ms;
  expect_configuration_error("move targets required",
                             [&move] { (void)one_step_plan(move); });
  move.targets = {{"base", 1500U, 0}};
  expect_configuration_error("exactly one target form",
                             [&move] { (void)one_step_plan(move); });
  move.targets = {{"base", 2500U, std::nullopt}};
  expect_configuration_error("target outside envelope",
                             [&move] { (void)one_step_plan(move); });
  move.targets = {{"base", 1500U, std::nullopt}, {"base", 1600U, std::nullopt}};
  expect_configuration_error("duplicate move channel",
                             [&move] { (void)one_step_plan(move); });

  auto pause = step("pause", ssc32_hil::Action::kPause);
  expect_configuration_error("positive pause required",
                             [&pause] { (void)one_step_plan(pause); });

  auto version = step("same", ssc32_hil::Action::kVersion);
  expect_configuration_error("duplicate step names", [&version] {
    (void)ssc32_hil::compile_plan(valid_profile(), {"scenario", {version, version}});
  });
  expect(ssc32_hil::action_name(static_cast<ssc32_hil::Action>(255U)) == "unknown",
         "unknown action name");
  expect(ssc32_hil::escape_wire("\r\n\t\\\"\x01") == "\"\\r\\n\\t\\\\\\\"\\x01\"",
         "wire escaping covers control characters");
}

}  // namespace

int main() {
  try {
    test_recording_transport_and_evidence();
    test_successful_runner();
    test_runner_failures();
    test_plan_validation_paths();
  } catch (const std::exception& error) {
    std::cerr << "Unexpected exception: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
  if (failures != 0) {
    std::cerr << failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "All SSC-32 HIL coverage tests passed\n";
  return EXIT_SUCCESS;
}
