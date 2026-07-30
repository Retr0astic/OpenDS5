// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <array>
#include <chrono>
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
constexpr std::size_t kHapticsOutputSamplesPerBoundary = 64; // 32 stereo frames @ 3 kHz

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
  std::uint64_t stale = 0;
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
  std::uint64_t last_timestamp_ns_ = 0;
};

// Fixed-capacity stereo sample FIFO used by the real-time output path.
class HapticsSampleRing {
 public:
  explicit HapticsSampleRing(std::size_t capacity_frames);
  void append(const HapticsStreamFrame& frame);
  bool pop_block(std::span<float, kHapticsOutputSamplesPerBoundary> block);
  bool has_complete_block() const { return size_samples_ >= kHapticsOutputSamplesPerBoundary; }
  bool has_partial() const { return size_samples_ != 0 || accum_frames_ != 0; }
  std::size_t size_samples() const { return size_samples_; }
  std::size_t capacity_samples() const { return samples_.size(); }
  std::uint64_t dropped_samples() const { return dropped_samples_; }
  std::uint64_t stale_dropped_samples() const { return stale_dropped_samples_; }
  // Drops an incomplete block which has remained buffered past timeout.
  bool drop_stale(std::chrono::steady_clock::time_point now,
                  std::chrono::milliseconds timeout);
  void clear();

 private:
  std::vector<float> samples_;
  std::size_t read_ = 0;
  std::size_t write_ = 0;
  std::size_t size_samples_ = 0;
  std::uint64_t dropped_samples_ = 0;
  std::uint64_t stale_dropped_samples_ = 0;
  bool have_sequence_ = false;
  std::uint64_t next_sequence_ = 0;
  std::uint64_t last_timestamp_ns_ = 0;
  std::chrono::steady_clock::time_point partial_since_{};
  float accum_left_ = 0.0F;
  float accum_right_ = 0.0F;
  std::size_t accum_frames_ = 0;
};

} // namespace vds
