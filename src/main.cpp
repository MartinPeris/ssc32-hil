// SPDX-License-Identifier: BSD-3-Clause
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include "ssc32_hil/config.hpp"
#include "ssc32_hil/evidence.hpp"
#include "ssc32_hil/plan.hpp"
#include "ssc32_hil/runner.hpp"

namespace {

void usage(std::ostream& output) {
  output << "Usage:\n"
         << "  ssc32-hil validate PROFILE SCENARIO\n"
         << "  ssc32-hil plan PROFILE SCENARIO [--device PATH]\n"
         << "  ssc32-hil run PROFILE SCENARIO --execute [--device PATH] [--yes]\n"
         << "                [--json PATH] [--junit PATH]\n";
}

bool prompt(const std::string& question) {
  std::cout << question << " [y/N] " << std::flush;
  std::string answer;
  std::getline(std::cin, answer);
  return answer == "y" || answer == "Y" || answer == "yes" || answer == "YES";
}

struct Options {
  std::string command;
  std::filesystem::path profile;
  std::filesystem::path scenario;
  std::string device;
  bool execute{};
  bool assume_yes{};
  std::filesystem::path json_report{"ssc32-hil-report.json"};
  std::filesystem::path junit_report{"ssc32-hil-report.xml"};
};

Options parse_options(const int argc, char** argv) {
  if (argc < 4) {
    throw std::invalid_argument("missing command, profile, or scenario");
  }
  Options options;
  options.command = argv[1];
  options.profile = argv[2];
  options.scenario = argv[3];
  for (int index = 4; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--execute") {
      options.execute = true;
    } else if (argument == "--yes") {
      options.assume_yes = true;
    } else if (argument == "--device" || argument == "--json" ||
               argument == "--junit") {
      if (index + 1 >= argc) {
        throw std::invalid_argument(argument + " requires a value");
      }
      const std::string value = argv[++index];
      if (argument == "--device") {
        options.device = value;
      } else if (argument == "--json") {
        options.json_report = value;
      } else {
        options.junit_report = value;
      }
    } else {
      throw std::invalid_argument("unknown option: " + argument);
    }
  }
  if (options.command != "validate" && options.command != "plan" &&
      options.command != "run") {
    throw std::invalid_argument("unknown command: " + options.command);
  }
  return options;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const auto options = parse_options(argc, argv);
    auto profile = ssc32_hil::load_profile(options.profile);
    if (!options.device.empty()) {
      profile.controller.device = options.device;
    }
    const auto scenario = ssc32_hil::load_scenario(options.scenario);
    const auto plan = ssc32_hil::compile_plan(profile, scenario);

    if (options.command == "validate") {
      std::cout << "Valid profile and scenario: " << plan.steps.size()
                << " planned step(s)\n";
      return EXIT_SUCCESS;
    }

    std::cout << ssc32_hil::render_plan(plan);
    if (options.command == "plan") {
      return EXIT_SUCCESS;
    }
    if (!options.execute) {
      throw std::invalid_argument("run requires --execute; use 'plan' for a dry run");
    }

    if (!options.assume_yes &&
        !prompt("Confirm the arm is clear, mechanically supported, and an "
                "accessible power cutoff is available.")) {
      std::cout << "Aborted before opening the serial device.\n";
      return EXIT_FAILURE;
    }

    const ssc32_hil::Confirmation confirmation =
        options.assume_yes
            ? ssc32_hil::Confirmation([](const std::string&) { return true; })
            : ssc32_hil::Confirmation(prompt);
    const auto evidence = ssc32_hil::run_plan(plan, confirmation);
    ssc32_hil::write_json_report(options.json_report, plan, evidence);
    ssc32_hil::write_junit_report(options.junit_report, plan, evidence);
    std::cout << "Evidence: " << options.json_report << " and " << options.junit_report
              << '\n';
    if (!evidence.passed) {
      std::cerr << "FAIL: " << evidence.failure << '\n';
      return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << "Error: " << error.what() << '\n';
    usage(std::cerr);
    return EXIT_FAILURE;
  }
}
