// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "ssc32/transport.hpp"

namespace ssc32_hil {

struct TransportEvent {
  std::chrono::milliseconds elapsed{};
  std::string direction;
  std::vector<std::uint8_t> data;
};

class RecordingTransport final : public ssc32::Transport {
 public:
  explicit RecordingTransport(ssc32::Transport& inner);

  void write_all(std::string_view data) override;
  [[nodiscard]] std::vector<std::uint8_t> read_exact(
      std::size_t size, std::chrono::milliseconds timeout) override;
  [[nodiscard]] std::string read_until(char delimiter, std::size_t max_size,
                                       std::chrono::milliseconds timeout) override;

  [[nodiscard]] const std::vector<TransportEvent>& events() const noexcept;

 private:
  void record(std::string direction, std::string_view data);
  void record(std::string direction, const std::vector<std::uint8_t>& data);

  ssc32::Transport& inner_;
  std::chrono::steady_clock::time_point started_;
  std::vector<TransportEvent> events_;
};

}  // namespace ssc32_hil
