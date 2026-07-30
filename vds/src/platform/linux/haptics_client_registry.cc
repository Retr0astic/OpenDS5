#include "haptics_client_registry.hh"
#include <algorithm>

namespace vds {
HapticsClientRegistry::HapticsClientRegistry(std::size_t capacity, std::chrono::seconds timeout)
    : capacity_(capacity), timeout_(timeout) {}
std::optional<HapticsClientRegistry::Token> HapticsClientRegistry::accept(Clock::time_point now) {
  if (clients_.size() >= capacity_) return std::nullopt;
  const Token token = next_token_++;
  clients_.push_back({token, now, std::nullopt, 0});
  return token;
}
bool HapticsClientRegistry::negotiate(Token token, std::size_t port, std::uint32_t stream_id) {
  auto *client = const_cast<Client *>(find(token));
  if (!client || client->port || stream_id == 0 || port_owned(port)) return false;
  client->port = port; client->stream_id = stream_id; return true;
}
std::optional<std::size_t> HapticsClientRegistry::disconnect(Token token) {
  auto it = std::find_if(clients_.begin(), clients_.end(), [&](const Client& c){ return c.token == token; });
  if (it == clients_.end()) return std::nullopt;
  auto port = it->port; clients_.erase(it); return port;
}
std::vector<HapticsClientRegistry::Token> HapticsClientRegistry::expire(Clock::time_point now) {
  std::vector<Token> expired;
  for (const auto &c : clients_) if (!c.port && now - c.accepted_at >= timeout_) expired.push_back(c.token);
  for (Token t : expired) disconnect(t);
  return expired;
}
std::vector<std::size_t> HapticsClientRegistry::reload() {
  std::vector<std::size_t> ports;
  for (const auto &c : clients_) if (c.port) ports.push_back(*c.port);
  clients_.clear(); return ports;
}
const HapticsClientRegistry::Client* HapticsClientRegistry::find(Token token) const {
  auto it = std::find_if(clients_.begin(), clients_.end(), [&](const Client& c){ return c.token == token; });
  return it == clients_.end() ? nullptr : &*it;
}
bool HapticsClientRegistry::port_owned(std::size_t port) const {
  return std::any_of(clients_.begin(), clients_.end(), [&](const Client& c){ return c.port && *c.port == port; });
}
}
