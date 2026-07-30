#include "platform/linux/haptics_client_registry.hh"
#include <cassert>
#include <chrono>
int main() {
  using R = vds::HapticsClientRegistry; using C = R::Clock;
  const auto t = C::time_point{};
  R r(2, std::chrono::seconds(2));
  auto a = r.accept(t); auto b = r.accept(t); assert(a && b); assert(!r.accept(t));
  assert(r.negotiate(*a, 3, 9)); assert(!r.negotiate(*b, 3, 10)); assert(r.negotiate(*b, 4, 11));
  auto disconnected = r.disconnect(*a); assert(disconnected && *disconnected == 3);
  assert(!r.disconnect(*a));
  // Disconnecting one owner leaves the other port isolated.
  assert(r.port_owned(4));
  auto c = r.accept(t); assert(c); auto expired = r.expire(t + std::chrono::seconds(3)); assert(expired.size() == 1);
  auto cleared = r.reload(); assert(cleared.size() == 1 && cleared[0] == 4); assert(r.size() == 0);
}
