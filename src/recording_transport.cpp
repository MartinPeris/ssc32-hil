// SPDX-License-Identifier: BSD-3-Clause
#include "ssc32_hil/recording_transport.hpp"

#include <utility>

namespace ssc32_hil {

RecordingTransport::RecordingTransport(ssc32::Transport& inner)
    : inner_(inner), started_(std::chrono::steady_clock::now()) {}

void RecordingTransport::write_all(const std::string_view data) {
  inner_.write_all(data);
  record("tx", data);
}

std::vector<std::uint8_t> RecordingTransport::read_exact(
    const std::size_t size, const std::chrono::milliseconds timeout) {
  auto value = inner_.read_exact(size, timeout);
  record("rx", value);
  return value;
}

std::string RecordingTransport::read_until(const char delimiter,
                                           const std::size_t max_size,
                                           const std::chrono::milliseconds timeout) {
  auto value = inner_.read_until(delimiter, max_size, timeout);
  record("rx", value);
  return value;
}

const std::vector<TransportEvent>& RecordingTransport::events() const noexcept {
  return events_;
}

void RecordingTransport::record(std::string direction, const std::string_view data) {
  record(std::move(direction), std::vector<std::uint8_t>(data.begin(), data.end()));
}

void RecordingTransport::record(std::string direction,
                                const std::vector<std::uint8_t>& data) {
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started_);
  events_.push_back({elapsed, std::move(direction), data});
}

}  // namespace ssc32_hil
