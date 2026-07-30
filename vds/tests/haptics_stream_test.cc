// SPDX-License-Identifier: MIT

#include "haptics_stream.hh"

#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {

vds::HapticsStreamFrame frame(std::uint64_t sequence = 7) {
  return vds::HapticsStreamFrame{
      .stream_id = 42,
      .sequence = sequence,
      .monotonic_timestamp_ns = 1234,
      .frame_count = 2,
      .samples = {0.25f, -0.5f, 0.75f, -1.0f},
  };
}

template <typename Function>
void expects_invalid(Function &&function) {
  bool rejected = false;
  try {
    function();
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
}

} // namespace

int main() {
  const auto encoded = vds::encode_haptics_stream_frame(frame());
  assert(encoded.size() == vds::kHapticsStreamHeaderBytes + 16);
  const auto decoded = vds::decode_haptics_stream_frame(encoded);
  assert(decoded.stream_id == 42);
  assert(decoded.sequence == 7);
  assert(decoded.samples == frame().samples);

  auto malformed = encoded;
  malformed[0] ^= 0xff;
  expects_invalid([&] { vds::decode_haptics_stream_frame(malformed); });
  malformed = encoded;
  malformed.resize(malformed.size() - 1);
  expects_invalid([&] { vds::decode_haptics_stream_frame(malformed); });
  malformed = encoded;
  malformed[28] = 0;
  malformed[29] = 2; // 512 frames, above the negotiated bound.
  expects_invalid([&] { vds::decode_haptics_stream_frame(malformed); });

  vds::HapticsStreamQueue queue(2);
  queue.push(frame(10));
  queue.push(frame(12));
  queue.push(frame(13));
  assert(queue.size() == 2);
  assert(queue.stats().accepted == 3);
  assert(queue.stats().dropped == 1);
  assert(queue.stats().sequence_gaps == 1);
  vds::HapticsStreamFrame popped;
  assert(queue.pop(popped) && popped.sequence == 12);
  assert(queue.pop(popped) && popped.sequence == 13);
  assert(!queue.pop(popped));

  auto invalid = frame();
  invalid.samples.pop_back();
  expects_invalid([&] { vds::encode_haptics_stream_frame(invalid); });
  invalid = frame();
  invalid.samples[0] = 0.0f / 0.0f;
  expects_invalid([&] { vds::encode_haptics_stream_frame(invalid); });
  return 0;
}
