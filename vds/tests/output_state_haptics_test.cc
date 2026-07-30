// SPDX-License-Identifier: AGPL-3.0-only
#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>

#include "../include/vds/ds5_protocol.h"
#include "../src/vds_protocol.hh"

namespace {
constexpr std::size_t kPowerSaveOffset = 9;
constexpr std::size_t kImprovedRumbleOffset = 38;
constexpr std::size_t kBtStateOffset = 3;

std::array<std::uint8_t, VDS_SET_STATE_SIZE> legacy_host_report() {
  std::array<std::uint8_t, VDS_SET_STATE_SIZE> report{};
  report[0] = 0x03; // enable_rumble_emulation | use_rumble_not_haptics
  report[1] = 0x02; // allow_audio_mute
  report[kPowerSaveOffset] = 0x84; // haptic_power_save | haptic_mute
  report[kImprovedRumbleOffset] = 0x04;
  return report;
}

bool bit(std::uint8_t value, unsigned position) {
  return (value & (1u << position)) != 0;
}
} // namespace

int main() {
  using namespace std::chrono_literals;
  const auto host_report = legacy_host_report();
  vds::DsOutputState first;
  assert(first.apply_usb_output_report(host_report));
  const auto base_state = first.state();

  // Speaker-only and silent PCM never enter audio-haptics arbitration.
  first.set_haptic_audio_active(false);
  assert(bit(first.state()[0], 0));
  assert(bit(first.state()[0], 1));
  assert(bit(first.state()[kImprovedRumbleOffset], 2));
  assert(bit(first.state()[kPowerSaveOffset], 2));
  assert(bit(first.state()[kPowerSaveOffset], 7));

  // A nonzero rear-channel chunk temporarily forces native audio haptics.
  first.set_haptic_audio_active(true);
  assert(!bit(first.state()[0], 0));
  assert(!bit(first.state()[0], 1));
  assert(!bit(first.state()[kImprovedRumbleOffset], 2));
  assert(!bit(first.state()[kPowerSaveOffset], 2));
  assert(!bit(first.state()[kPowerSaveOffset], 7));
  // Model the daemon's deterministic lease transitions without sleeping: each
  // nonzero chunk renews the deadline, and legacy rumble stays suppressed
  // until the final expiry.
  const auto lease_start = vds::HapticLease::TimePoint{} + std::chrono::seconds(1);
  vds::HapticLease lease;
  const auto consume_nonzero_chunk = [&](vds::HapticLease::TimePoint now) {
    lease.activate(now);
    first.set_haptic_audio_active(true);
    assert(!bit(first.state()[0], 0));
    assert(!bit(first.state()[0], 1));
    assert(!bit(first.state()[kImprovedRumbleOffset], 2));
  };
  consume_nonzero_chunk(lease_start);
  consume_nonzero_chunk(lease_start + 50ms);
  consume_nonzero_chunk(lease_start + 100ms);
  assert(lease.deadline() == lease_start + 200ms);
  assert(!lease.expire(lease_start + 199ms));
  assert(!bit(first.state()[0], 1));
  assert(!bit(first.state()[kImprovedRumbleOffset], 2));
  assert(lease.expire(lease_start + 200ms));
  first.set_haptic_audio_active(false);
  assert(bit(first.state()[0], 0));
  assert(bit(first.state()[0], 1));
  assert(bit(first.state()[kImprovedRumbleOffset], 2));
  for (std::size_t i = 0; i < first.state().size(); ++i) {
    if (i == 0 || i == 1 || i == kPowerSaveOffset ||
        i == kImprovedRumbleOffset) continue;
    assert(first.state()[i] == base_state[i]);
  }

  // The same effective overlay is present in a constructed Bluetooth state
  // packet and unrelated host-owned bytes remain untouched.
  const auto bt_report = first.build_bt_state_report();
  assert(!bit(bt_report[kBtStateOffset + 0], 0));
  assert(!bit(bt_report[kBtStateOffset + 0], 1));
  assert(!bit(bt_report[kBtStateOffset + kImprovedRumbleOffset], 2));
  assert(!bit(bt_report[kBtStateOffset + kPowerSaveOffset], 2));
  assert(!bit(bt_report[kBtStateOffset + kPowerSaveOffset], 7));

  // Host reports received during the lease update the base state while the
  // overlay stays active; expiry restores the newest report, not a snapshot.
  auto updated_host_report = host_report;
  updated_host_report[0] = 0x01; // newest host state no longer requests bit 1
  updated_host_report[kPowerSaveOffset] = 0x04; // no longer haptic-muted
  assert(first.apply_usb_output_report(updated_host_report));
  assert(!bit(first.state()[0], 1));

  // The exact host request is restored after the haptic chunk ends.
  first.set_haptic_audio_active(false);
  assert(bit(first.state()[0], 0));
  assert(!bit(first.state()[0], 1));
  assert(bit(first.state()[kImprovedRumbleOffset], 2));
  assert(bit(first.state()[kPowerSaveOffset], 2));
  assert(!bit(first.state()[kPowerSaveOffset], 7));

  // Closing the audio stream also ends arbitration if no trailing silent
  // chunk arrives.
  first.set_haptic_audio_active(true);
  first.set_audio_out_stream_active(false);
  assert(!bit(first.state()[0], 1));
  assert(!bit(first.state()[kPowerSaveOffset], 7));
  assert(bit(first.state()[kImprovedRumbleOffset], 2));
  assert(bit(first.state()[kPowerSaveOffset], 2));

  // Arbitration belongs to one controller/output-state instance only.
  vds::DsOutputState second;
  assert(second.apply_usb_output_report(host_report));
  first.set_haptic_audio_active(true);
  assert(!bit(first.state()[0], 1));
  assert(bit(second.state()[0], 1));
  assert(bit(second.state()[kPowerSaveOffset], 7));

  std::puts("output_state_haptics_test OK");
  return 0;
}
