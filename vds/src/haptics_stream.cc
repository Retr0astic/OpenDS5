// SPDX-License-Identifier: MIT

#include "haptics_stream.hh"

#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace vds {
namespace {

template <typename T>
void append_le(std::vector<std::uint8_t> &output, T value) {
  static_assert(std::is_unsigned_v<T>);
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    output.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
  }
}

template <typename T>
T read_le(std::span<const std::uint8_t> input, std::size_t offset) {
  static_assert(std::is_unsigned_v<T>);
  if (offset > input.size() || input.size() - offset < sizeof(T)) {
    throw std::invalid_argument("haptics stream header is truncated");
  }
  T value = 0;
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    value |= static_cast<T>(input[offset + i]) << (i * 8);
  }
  return value;
}

void validate_frame(const HapticsStreamFrame &frame) {
  if (frame.version != kHapticsStreamVersion) {
    throw std::invalid_argument("unsupported haptics stream version");
  }
  if (frame.stream_id == 0) {
    throw std::invalid_argument("haptics stream id must be nonzero");
  }
  if (frame.frame_count == 0 ||
      frame.frame_count > kHapticsStreamMaxFramesPerPacket) {
    throw std::invalid_argument("invalid haptics stream frame count");
  }
  const std::size_t sample_count =
      static_cast<std::size_t>(frame.frame_count) * kHapticsStreamChannels;
  if (frame.samples.size() != sample_count) {
    throw std::invalid_argument("haptics stream sample count mismatch");
  }
  for (const float sample : frame.samples) {
    if (!std::isfinite(sample)) {
      throw std::invalid_argument("haptics stream contains non-finite sample");
    }
  }
}

} // namespace

std::vector<std::uint8_t> encode_haptics_stream_frame(
    const HapticsStreamFrame &frame) {
  validate_frame(frame);
  std::vector<std::uint8_t> packet;
  packet.reserve(kHapticsStreamHeaderBytes + frame.samples.size() * sizeof(float));
  append_le(packet, kHapticsStreamMagic);
  append_le(packet, frame.version);
  append_le(packet, frame.stream_id);
  append_le(packet, frame.sequence);
  append_le(packet, frame.monotonic_timestamp_ns);
  append_le(packet, frame.frame_count);
  append_le<std::uint16_t>(packet, 0);
  for (const float sample : frame.samples) {
    append_le(packet, std::bit_cast<std::uint32_t>(sample));
  }
  return packet;
}

HapticsStreamFrame decode_haptics_stream_frame(
    std::span<const std::uint8_t> packet) {
  if (packet.size() < kHapticsStreamHeaderBytes ||
      packet.size() > kHapticsStreamMaxPacketBytes) {
    throw std::invalid_argument("invalid haptics stream packet length");
  }
  if (read_le<std::uint32_t>(packet, 0) != kHapticsStreamMagic) {
    throw std::invalid_argument("invalid haptics stream magic");
  }

  HapticsStreamFrame frame{
      .version = read_le<std::uint32_t>(packet, 4),
      .stream_id = read_le<std::uint32_t>(packet, 8),
      .sequence = read_le<std::uint64_t>(packet, 12),
      .monotonic_timestamp_ns = read_le<std::uint64_t>(packet, 20),
      .frame_count = read_le<std::uint16_t>(packet, 28),
      .samples = {},
  };
  if (read_le<std::uint16_t>(packet, 30) != 0) {
    throw std::invalid_argument("haptics stream reserved header is nonzero");
  }
  const std::size_t expected_with_samples =
      kHapticsStreamHeaderBytes +
      static_cast<std::size_t>(frame.frame_count) * kHapticsStreamChannels *
          sizeof(float);
  if (packet.size() != expected_with_samples) {
    throw std::invalid_argument("haptics stream packet length does not match frame count");
  }
  frame.samples.reserve(static_cast<std::size_t>(frame.frame_count) *
                        kHapticsStreamChannels);
  for (std::size_t offset = kHapticsStreamHeaderBytes; offset < packet.size();
       offset += sizeof(float)) {
    frame.samples.push_back(std::bit_cast<float>(read_le<std::uint32_t>(packet, offset)));
  }
  validate_frame(frame);
  return frame;
}

HapticsStreamQueue::HapticsStreamQueue(std::size_t capacity)
    : capacity_(capacity) {
  if (capacity_ == 0) {
    throw std::invalid_argument("haptics stream queue capacity must be nonzero");
  }
}

void HapticsStreamQueue::push(HapticsStreamFrame frame) {
  validate_frame(frame);
  if (have_sequence_ && frame.sequence < next_sequence_) {
    ++stats_.stale;
    ++stats_.dropped;
    return;
  }
  if (last_timestamp_ns_ != 0 && frame.monotonic_timestamp_ns < last_timestamp_ns_) {
    ++stats_.stale;
    ++stats_.dropped;
    return;
  }
  if (have_sequence_ && frame.sequence > next_sequence_) {
    stats_.sequence_gaps += frame.sequence - next_sequence_;
  }
  have_sequence_ = true;
  next_sequence_ = frame.sequence + 1;
  last_timestamp_ns_ = frame.monotonic_timestamp_ns;
  if (frames_.size() == capacity_) {
    frames_.pop_front();
    ++stats_.dropped;
  }
  frames_.push_back(std::move(frame));
  ++stats_.accepted;
}

bool HapticsStreamQueue::pop(HapticsStreamFrame &frame) {
  if (frames_.empty()) {
    return false;
  }
  frame = std::move(frames_.front());
  frames_.pop_front();
  return true;
}

void HapticsStreamQueue::clear() {
  frames_.clear();
  have_sequence_ = false;
  next_sequence_ = 0;
  last_timestamp_ns_ = 0;
}

HapticsSampleRing::HapticsSampleRing(std::size_t capacity_frames)
    : samples_(capacity_frames * kHapticsStreamChannels) {
  if (capacity_frames == 0) {
    throw std::invalid_argument("haptics sample ring capacity must be nonzero");
  }
}

void HapticsSampleRing::append(const HapticsStreamFrame& frame) {
  validate_frame(frame);
  if ((have_sequence_ && frame.sequence < next_sequence_) ||
      (last_timestamp_ns_ != 0 &&
       frame.monotonic_timestamp_ns < last_timestamp_ns_)) {
    dropped_samples_ += frame.samples.size();
    stale_dropped_samples_ += frame.samples.size();
    return;
  }
  have_sequence_ = true;
  next_sequence_ = frame.sequence + 1;
  last_timestamp_ns_ = frame.monotonic_timestamp_ns;
  const auto now = std::chrono::steady_clock::now();
  if (size_samples_ == 0 && accum_frames_ == 0) partial_since_ = now;
  for (std::size_t i = 0; i < frame.samples.size(); i += 2) {
    accum_left_ += frame.samples[i];
    accum_right_ += frame.samples[i + 1];
    ++accum_frames_;
    if (accum_frames_ < 16) continue;
    const float reduced[2] = {accum_left_ / 16.0F, accum_right_ / 16.0F};
    accum_left_ = accum_right_ = 0.0F;
    accum_frames_ = 0;
    for (const float sample : reduced) {
    if (size_samples_ == samples_.size()) {
      // Drop one complete stereo sample, preserving channel alignment.
      read_ = (read_ + kHapticsStreamChannels) % samples_.size();
      size_samples_ -= kHapticsStreamChannels;
      dropped_samples_ += kHapticsStreamChannels;
    }
    samples_[write_] = sample;
    write_ = (write_ + 1) % samples_.size();
    ++size_samples_;
    }
  }
}

bool HapticsSampleRing::pop_block(
    std::span<float, kHapticsOutputSamplesPerBoundary> block) {
  if (size_samples_ < block.size()) return false;
  for (float& sample : block) {
    sample = samples_[read_];
    read_ = (read_ + 1) % samples_.size();
    --size_samples_;
  }
  if (size_samples_ == 0) partial_since_ = {};
  return true;
}

bool HapticsSampleRing::drop_stale(std::chrono::steady_clock::time_point now,
                                   std::chrono::milliseconds timeout) {
  if (!has_partial() || size_samples_ >= kHapticsOutputSamplesPerBoundary || partial_since_ == std::chrono::steady_clock::time_point{}) return false;
  if (now - partial_since_ < timeout) return false;
  dropped_samples_ += size_samples_;
  stale_dropped_samples_ += size_samples_;
  clear();
  return true;
}

void HapticsSampleRing::clear() {
  read_ = write_ = size_samples_ = 0;
  partial_since_ = {};
  accum_left_ = accum_right_ = 0.0F;
  accum_frames_ = 0;
  have_sequence_ = false;
  next_sequence_ = 0;
  last_timestamp_ns_ = 0;
}

} // namespace vds
