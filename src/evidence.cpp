// SPDX-License-Identifier: BSD-3-Clause
#include "ssc32_hil/evidence.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace ssc32_hil {
namespace {

std::string json_escape(const std::string_view value) {
  std::ostringstream output;
  for (const char raw_character : value) {
    const auto character = static_cast<unsigned char>(raw_character);
    switch (character) {
      case '\\':
        output << "\\\\";
        break;
      case '"':
        output << "\\\"";
        break;
      case '\n':
        output << "\\n";
        break;
      case '\r':
        output << "\\r";
        break;
      case '\t':
        output << "\\t";
        break;
      default:
        if (character < 0x20U) {
          output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                 << static_cast<unsigned int>(character) << std::dec;
        } else {
          output << static_cast<char>(character);
        }
    }
  }
  return output.str();
}

std::string xml_escape(const std::string_view value) {
  std::string output;
  for (const char character : value) {
    switch (character) {
      case '&':
        output += "&amp;";
        break;
      case '<':
        output += "&lt;";
        break;
      case '>':
        output += "&gt;";
        break;
      case '"':
        output += "&quot;";
        break;
      case '\'':
        output += "&apos;";
        break;
      default:
        output += character;
    }
  }
  return output;
}

std::string bytes_hex(const std::vector<std::uint8_t>& data) {
  std::ostringstream output;
  for (std::size_t index = 0; index < data.size(); ++index) {
    if (index != 0U) {
      output << ' ';
    }
    output << std::hex << std::setw(2) << std::setfill('0')
           << static_cast<unsigned int>(data[index]);
  }
  return output.str();
}

std::ofstream open_report(const std::filesystem::path& path) {
  std::ofstream output(path, std::ios::out | std::ios::trunc);
  if (!output) {
    throw std::runtime_error("failed to open report: " + path.string());
  }
  return output;
}

}  // namespace

void write_json_report(const std::filesystem::path& path, const ExecutionPlan& plan,
                       const RunEvidence& evidence) {
  auto output = open_report(path);
  output << "{\n"
         << "  \"profile\": \"" << json_escape(plan.profile_name) << "\",\n"
         << "  \"scenario\": \"" << json_escape(plan.scenario_name) << "\",\n"
         << "  \"device\": \"" << json_escape(plan.controller.device) << "\",\n"
         << "  \"baud\": " << plan.controller.baud_rate << ",\n"
         << "  \"passed\": " << (evidence.passed ? "true" : "false") << ",\n"
         << "  \"failure\": \"" << json_escape(evidence.failure) << "\",\n"
         << "  \"steps\": [\n";
  for (std::size_t index = 0; index < evidence.steps.size(); ++index) {
    const auto& step = evidence.steps[index];
    output << "    {\"name\": \"" << json_escape(step.name)
           << "\", \"passed\": " << (step.passed ? "true" : "false")
           << ", \"detail\": \"" << json_escape(step.detail) << "\"}"
           << (index + 1U == evidence.steps.size() ? "\n" : ",\n");
  }
  output << "  ],\n  \"transport\": [\n";
  for (std::size_t index = 0; index < evidence.transport.size(); ++index) {
    const auto& event = evidence.transport[index];
    output << "    {\"elapsed_ms\": " << event.elapsed.count() << ", \"direction\": \""
           << event.direction << "\", \"hex\": \"" << bytes_hex(event.data) << "\"}"
           << (index + 1U == evidence.transport.size() ? "\n" : ",\n");
  }
  output << "  ]\n}\n";
}

void write_junit_report(const std::filesystem::path& path, const ExecutionPlan& plan,
                        const RunEvidence& evidence) {
  auto output = open_report(path);
  const auto failures = evidence.passed ? 0U : 1U;
  output << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
         << "<testsuite name=\"" << xml_escape(plan.scenario_name) << "\" tests=\""
         << evidence.steps.size() << "\" failures=\"" << failures << "\">\n";
  for (const auto& step : evidence.steps) {
    output << "  <testcase name=\"" << xml_escape(step.name) << "\">";
    if (!step.passed) {
      output << "<failure message=\"" << xml_escape(step.detail) << "\"/>";
    }
    output << "</testcase>\n";
  }
  output << "</testsuite>\n";
}

}  // namespace ssc32_hil
