// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <vector>

namespace vds {

constexpr std::uint32_t kHapticsStreamMagic = 0x3553444f; // "ODS5" LE
constexpr std::uint32_t kHapticsStreamVersion = 1;
constexpr std::uint32_t kHapticsStreamFormatFloat32Le = 1;
constexpr std::size_t kHapticsStreamChannels = 2;
constexpr std::size_t kHapticsStreamMaxFramesPerPacket = 256;
constexpr std::size_t kHapticsStreamHeaderBytes = 32;
constexpr std::size_t kHapticsStreamMaxPacketBytes =
    kHapticsStreamHeaderBytes +
    kHapticsStreamMaxFramesPerPacket * kHapticsStreamChannels * sizeof(float);

struct HapticsStreamFrame {
  std::uint32_t version = kHapticsStreamVersion;
  std::uint32_t stream_id = 0;
  std::uint64_t sequence = 0;
  std::uint64_t monotonic_timestamp_ns = 0;
  std::uint16_t frame_count = 0;
  std::vector<float> samples;
};

// Fixed little-endian wire representation for one SOCK_SEQPACKET payload.
std::vector<std::uint8_t> encode_haptics_stream_frame(
    const HapticsStreamFrame &frame);
HapticsStreamFrame decode_haptics_stream_frame(
    std::span<const std::uint8_t> packet);

struct HapticsStreamQueueStats {
  std::uint64_t accepted = 0;
  std::uint64_t dropped = 0;
  std::uint64_t sequence_gaps = 0;
};

class HapticsStreamQueue {
public:
  explicit HapticsStreamQueue(std::size_t capacity);

  // The queue owns only validated frames. When full, the oldest app frame is
  // discarded so the game output path never waits for a stale app frame.
  void push(HapticsStreamFrame frame);
  bool pop(HapticsStreamFrame &frame);
  void clear();

  std::size_t size() const { return frames_.size(); }
  const HapticsStreamQueueStats &stats() const { return stats_; }

private:
  std::size_t capacity_;
  std::deque<HapticsStreamFrame> frames_;
  HapticsStreamQueueStats stats_;
  bool have_sequence_ = false;
  std::uint64_t next_sequence_ = 0;
};

} // namespace vds
