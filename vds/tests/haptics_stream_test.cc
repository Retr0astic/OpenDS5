// SPDX-License-Identifier: MIT

#include "haptics_stream.hh"

#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <vector>
#include <array>
#include <chrono>

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
  queue.push(frame(20));
  auto stale = frame(19);
  stale.monotonic_timestamp_ns = 1233;
  queue.push(stale);
  assert(queue.stats().stale == 1);

  auto invalid = frame();
  invalid.samples.pop_back();
  expects_invalid([&] { vds::encode_haptics_stream_frame(invalid); });
  invalid = frame();
  invalid.samples[0] = 0.0f / 0.0f;
  expects_invalid([&] { vds::encode_haptics_stream_frame(invalid); });

  // Samples are assembled across packet boundaries without padding.
  vds::HapticsSampleRing ring(64);
  auto a = frame(30);
  a.frame_count = 256;
  a.samples.assign(512, 0.25f);
  ring.append(a);
  assert(!ring.has_complete_block());
  auto b = frame(31);
  b.frame_count = 256;
  b.samples.assign(512, 0.5f);
  ring.append(b);
  assert(ring.has_complete_block());
  std::array<float, vds::kHapticsOutputSamplesPerBoundary> block{};
  assert(ring.pop_block(block));
  assert(block[0] == 0.25f && block[31] == 0.25f && block[32] == 0.5f);
  assert(!ring.has_partial());

  auto large = frame(40);
  large.frame_count = 256;
  large.samples.assign(512, 0.75f);

  // 480 input frames (two packets) produce one exact 30-frame block.
  vds::HapticsSampleRing split_ring(256);
  split_ring.append(a);
  split_ring.append(b);
  assert(split_ring.pop_block(block) && !split_ring.has_partial());

  // Overflow drops oldest complete stereo samples deterministically.
  for (std::uint64_t seq = 40; seq < 50; ++seq) {
    large.sequence = seq;
    ring.append(large);
  }
  assert(ring.dropped_samples() != 0);

  // Partial data is retained, then stale-dropped without emitting a padded block.
  auto partial = frame(41);
  partial.frame_count = 1;
  partial.samples.assign(2, 0.1f);
  ring.clear();
  ring.append(partial);
  assert(ring.has_partial() && !ring.has_complete_block());
  assert(ring.drop_stale(std::chrono::steady_clock::now() +
                             std::chrono::milliseconds(100),
                         std::chrono::milliseconds(50)));
  assert(!ring.has_partial());
  return 0;
}
