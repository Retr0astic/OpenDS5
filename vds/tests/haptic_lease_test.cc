// SPDX-License-Identifier: AGPL-3.0-only
#include <cassert>
#include <chrono>
#include <cstdio>

#include "../src/vds_protocol.hh"

int main() {
  using namespace std::chrono_literals;
  const auto start = vds::HapticLease::TimePoint{} + 1s;
  vds::HapticLease lease;

  assert(lease.activate(start));
  assert(lease.active());
  assert(lease.deadline() == start + 100ms);
  assert(!lease.expire(start + 99ms));

  // A later nonzero chunk extends the lease; silence and empty queue perform
  // no operation and therefore cannot shorten it.
  assert(!lease.activate(start + 50ms));
  assert(lease.deadline() == start + 150ms);
  assert(!lease.expire(start + 100ms));
  assert(lease.expire(start + 150ms));
  assert(!lease.expire(start + 151ms));

  assert(lease.activate(start + 200ms));
  assert(lease.clear());
  assert(!lease.active());
  assert(!lease.clear());

  std::puts("haptic_lease_test OK");
  return 0;
}
