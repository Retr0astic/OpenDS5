#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace vds {

class HapticsClientRegistry {
 public:
  using Clock = std::chrono::steady_clock;
  using Token = std::uint64_t;
  struct Client { Token token; Clock::time_point accepted_at; std::optional<std::size_t> port; std::uint32_t stream_id = 0; };

  explicit HapticsClientRegistry(std::size_t capacity = 16,
                                 std::chrono::seconds negotiation_timeout = std::chrono::seconds(2));
  std::optional<Token> accept(Clock::time_point now);
  bool negotiate(Token token, std::size_t port, std::uint32_t stream_id);
  std::optional<std::size_t> disconnect(Token token);
  std::vector<Token> expire(Clock::time_point now);
  std::vector<std::size_t> reload();
  const Client* find(Token token) const;
  bool port_owned(std::size_t port) const;
  std::size_t size() const { return clients_.size(); }

 private:
  std::size_t capacity_; std::chrono::seconds timeout_; Token next_token_ = 1;
  std::vector<Client> clients_;
};
}
