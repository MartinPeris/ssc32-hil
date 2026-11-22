// SPDX-License-Identifier: BSD-3-Clause
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "ssc32/controller.hpp"
#include "ssc32/serial_transport.hpp"
#include "ssc32_hil/recording_transport.hpp"

namespace {

int create_pty(std::string& slave_path) {
  const int master = posix_openpt(O_RDWR | O_NOCTTY);
  if (master < 0 || grantpt(master) != 0 || unlockpt(master) != 0) {
    throw std::runtime_error("failed to create pseudoterminal");
  }
  const char* path = ptsname(master);
  if (path == nullptr) {
    close(master);
    throw std::runtime_error("failed to resolve pseudoterminal path");
  }
  slave_path = path;
  return master;
}

void write_all(const int descriptor, const std::string_view value) {
  std::size_t offset = 0;
  while (offset < value.size()) {
    const auto count = write(descriptor, value.data() + offset, value.size() - offset);
    if (count <= 0) {
      throw std::runtime_error("simulator write failed");
    }
    offset += static_cast<std::size_t>(count);
  }
}

void simulator(const int master, std::string& error) noexcept {
  try {
    std::string command;
    char character = '\0';
    while (read(master, &character, 1U) == 1) {
      command += character;
      if (character != '\r') {
        continue;
      }
      if (command == "VER\r") {
        write_all(master, "SSC32-V2.03XE\r");
      } else if (command == "Q\r") {
        write_all(master, ".");
      } else if (command == "QP0QP1\r") {
        const std::string pulses{static_cast<char>(150), static_cast<char>(160)};
        write_all(master, pulses);
      } else {
        throw std::runtime_error("simulator received unexpected command: " + command);
      }
      command.clear();
    }
  } catch (const std::exception& exception) {
    error = exception.what();
  } catch (...) {
    error = "simulator failed with an unknown error";
  }
}

}  // namespace

int main() {
  using namespace std::chrono_literals;
  int master = -1;
  std::thread worker;
  std::string simulator_error;
  try {
    std::string slave_path;
    master = create_pty(slave_path);
    worker = std::thread(simulator, master, std::ref(simulator_error));
    {
      ssc32::SerialTransport serial(slave_path, 115200U);
      ssc32_hil::RecordingTransport recording(serial);
      ssc32::Controller controller(recording);
      if (controller.version(500ms) != "SSC32-V2.03XE") {
        throw std::runtime_error("unexpected version response");
      }
      if (controller.is_moving(500ms)) {
        throw std::runtime_error("unexpected moving response");
      }
      if (controller.query_pulse_widths({0U, 1U}, 500ms) !=
          std::vector<std::uint16_t>{1500U, 1600U}) {
        throw std::runtime_error("unexpected pulse response");
      }
      if (recording.events().size() != 6U) {
        throw std::runtime_error("unexpected transport evidence count");
      }
    }
    close(master);
    master = -1;
    worker.join();
    if (!simulator_error.empty()) {
      throw std::runtime_error(simulator_error);
    }
  } catch (const std::exception& error) {
    if (master >= 0) {
      close(master);
    }
    if (worker.joinable()) {
      worker.join();
    }
    std::cerr << "PTY integration test failed: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
  std::cout << "PTY integration test passed\n";
  return EXIT_SUCCESS;
}
