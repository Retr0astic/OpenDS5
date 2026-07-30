// SPDX-License-Identifier: MIT

#pragma once

#include "haptics_stream.hh"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vds {

inline constexpr std::uint32_t kHapticsStreamNegotiationMagic = 0x3553444f;
inline constexpr std::uint32_t kHapticsStreamNegotiationVersion = 1;
inline constexpr std::uint32_t kHapticsStreamNegotiationOpen = 1;
inline constexpr std::size_t kHapticsStreamNegotiationBytes = 32;

struct HapticsStreamNegotiation {
  std::uint32_t version = kHapticsStreamNegotiationVersion;
  std::uint32_t port = 0;
  std::uint32_t stream_id = 0;
  std::uint16_t max_frames = kHapticsStreamMaxFramesPerPacket;
  std::uint16_t channels = kHapticsStreamChannels;
  std::uint32_t sample_rate = 48000;
  std::uint16_t format = kHapticsStreamFormatFloat32Le;
};

std::vector<std::uint8_t> encode_haptics_stream_negotiation(
    const HapticsStreamNegotiation &negotiation);
HapticsStreamNegotiation decode_haptics_stream_negotiation(
    std::span<const std::uint8_t> packet);
bool haptics_peer_authorized(std::uint32_t uid, std::uint32_t gid,
                             std::span<const std::uint32_t> supplementary_gids,
                             std::uint32_t daemon_uid, std::uint32_t vds_gid);

class HapticsStreamEndpoint {
public:
  explicit HapticsStreamEndpoint(std::string path);
  ~HapticsStreamEndpoint();

  HapticsStreamEndpoint(const HapticsStreamEndpoint &) = delete;
  HapticsStreamEndpoint &operator=(const HapticsStreamEndpoint &) = delete;

  void open();
  void close();
  int listener_fd() const { return listener_fd_; }
  const std::string &path() const { return path_; }
  std::optional<int> accept_client();
  std::optional<HapticsStreamNegotiation> receive_negotiation(int fd) const;
  std::optional<HapticsStreamFrame> receive_frame(int fd) const;

private:
  std::string path_;
  int listener_fd_ = -1;
};

} // namespace vds
