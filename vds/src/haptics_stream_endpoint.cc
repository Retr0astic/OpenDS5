// SPDX-License-Identifier: MIT

#include "haptics_stream_endpoint.hh"

#include <array>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <fstream>
#include <sstream>

#include <fcntl.h>
#include <grp.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace vds {
namespace {

template <typename T>
void append_le(std::vector<std::uint8_t> &output, T value) {
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    output.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
  }
}

template <typename T>
T read_le(std::span<const std::uint8_t> input, std::size_t offset) {
  if (offset > input.size() || input.size() - offset < sizeof(T)) {
    throw std::invalid_argument("haptics negotiation packet is truncated");
  }
  T value = 0;
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    value |= static_cast<T>(input[offset + i]) << (i * 8);
  }
  return value;
}

void set_nonblocking(int fd) {
  const int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    throw std::runtime_error("failed to set haptics socket nonblocking: " +
                             std::string(std::strerror(errno)));
  }
}

} // namespace

bool haptics_peer_authorized(std::uint32_t uid, std::uint32_t gid,
                             std::span<const std::uint32_t> supplementary_gids,
                             std::uint32_t daemon_uid, std::uint32_t vds_gid) {
  return uid == 0 || uid == daemon_uid || gid == vds_gid ||
         std::find(supplementary_gids.begin(), supplementary_gids.end(), vds_gid) != supplementary_gids.end();
}

std::vector<std::uint8_t> encode_haptics_stream_negotiation(
    const HapticsStreamNegotiation &negotiation) {
  if (negotiation.version != kHapticsStreamNegotiationVersion ||
      negotiation.stream_id == 0 || negotiation.max_frames == 0 ||
      negotiation.max_frames > kHapticsStreamMaxFramesPerPacket ||
      negotiation.channels != kHapticsStreamChannels ||
      negotiation.sample_rate != 48000 ||
      negotiation.format != kHapticsStreamFormatFloat32Le) {
    throw std::invalid_argument("invalid haptics stream negotiation");
  }
  std::vector<std::uint8_t> packet;
  packet.reserve(kHapticsStreamNegotiationBytes);
  append_le(packet, kHapticsStreamNegotiationMagic);
  append_le(packet, negotiation.version);
  append_le(packet, kHapticsStreamNegotiationOpen);
  append_le(packet, negotiation.port);
  append_le(packet, negotiation.stream_id);
  append_le(packet, negotiation.max_frames);
  append_le(packet, negotiation.channels);
  append_le(packet, negotiation.sample_rate);
  append_le(packet, negotiation.format);
  append_le<std::uint16_t>(packet, 0);
  return packet;
}

HapticsStreamNegotiation decode_haptics_stream_negotiation(
    std::span<const std::uint8_t> packet) {
  if (packet.size() != kHapticsStreamNegotiationBytes ||
      read_le<std::uint32_t>(packet, 0) != kHapticsStreamNegotiationMagic ||
      read_le<std::uint32_t>(packet, 8) != kHapticsStreamNegotiationOpen ||
      read_le<std::uint16_t>(packet, 30) != 0) {
    throw std::invalid_argument("invalid haptics stream negotiation packet");
  }
  HapticsStreamNegotiation negotiation{
      .version = read_le<std::uint32_t>(packet, 4),
      .port = read_le<std::uint32_t>(packet, 12),
      .stream_id = read_le<std::uint32_t>(packet, 16),
      .max_frames = read_le<std::uint16_t>(packet, 20),
      .channels = read_le<std::uint16_t>(packet, 22),
      .sample_rate = read_le<std::uint32_t>(packet, 24),
      .format = read_le<std::uint16_t>(packet, 28),
  };
  if (negotiation.version != kHapticsStreamNegotiationVersion ||
      negotiation.stream_id == 0 || negotiation.max_frames == 0 ||
      negotiation.max_frames > kHapticsStreamMaxFramesPerPacket ||
      negotiation.channels != kHapticsStreamChannels ||
      negotiation.sample_rate != 48000 ||
      negotiation.format != kHapticsStreamFormatFloat32Le) {
    throw std::invalid_argument("unsupported haptics stream negotiation");
  }
  return negotiation;
}

HapticsStreamEndpoint::HapticsStreamEndpoint(std::string path)
    : path_(std::move(path)) {}

HapticsStreamEndpoint::~HapticsStreamEndpoint() { close(); }

void HapticsStreamEndpoint::open() {
  close();
  sockaddr_un address{};
  if (path_.empty() || path_.size() >= sizeof(address.sun_path)) {
    throw std::invalid_argument("invalid haptics stream socket path");
  }
  listener_fd_ = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
  if (listener_fd_ < 0) {
    throw std::runtime_error("failed to create haptics stream socket: " +
                             std::string(std::strerror(errno)));
  }
  address.sun_family = AF_UNIX;
  std::strncpy(address.sun_path, path_.c_str(), sizeof(address.sun_path) - 1);
  ::unlink(path_.c_str());
  if (::bind(listener_fd_, reinterpret_cast<const sockaddr *>(&address),
             sizeof(address)) < 0 || ::listen(listener_fd_, 4) < 0) {
    const std::string error = std::strerror(errno);
    close();
    throw std::runtime_error("failed to open haptics stream socket: " + error);
  }
  set_nonblocking(listener_fd_);
  if (const struct group *vds_group = ::getgrnam("vds")) {
    const int chown_result = ::chown(path_.c_str(), 0, vds_group->gr_gid);
    (void)chown_result;
  }
  (void)::chmod(path_.c_str(), 0660);
}

void HapticsStreamEndpoint::close() {
  if (listener_fd_ >= 0) {
    ::close(listener_fd_);
    listener_fd_ = -1;
  }
  if (!path_.empty()) ::unlink(path_.c_str());
}

std::optional<int> HapticsStreamEndpoint::accept_client() {
  if (listener_fd_ < 0) throw std::logic_error("haptics endpoint is not open");
  const int fd = ::accept4(listener_fd_, nullptr, nullptr,
                           SOCK_NONBLOCK | SOCK_CLOEXEC);
  if (fd >= 0) {
    struct ucred credentials{};
    socklen_t length = sizeof(credentials);
    if (::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &length) < 0 ||
        length != sizeof(credentials)) {
      ::close(fd);
      throw std::runtime_error("failed to validate haptics client credentials");
    }
    const struct group *vds_group = ::getgrnam("vds");
    const auto vds_gid = vds_group ? static_cast<std::uint32_t>(vds_group->gr_gid)
                                   : static_cast<std::uint32_t>(-1);
    std::vector<std::uint32_t> supplementary;
    std::ifstream status("/proc/" + std::to_string(credentials.pid) + "/status");
    std::string line;
    while (std::getline(status, line)) {
      if (line.rfind("Groups:", 0) != 0) continue;
      std::istringstream groups(line.substr(7));
      std::uint32_t value = 0;
      while (groups >> value) supplementary.push_back(value);
      break;
    }
    if (!haptics_peer_authorized(static_cast<std::uint32_t>(credentials.uid),
                                 static_cast<std::uint32_t>(credentials.gid),
                                 supplementary,
                                 static_cast<std::uint32_t>(::getuid()), vds_gid)) {
      ::close(fd);
      return std::nullopt;
    }
    return fd;
  }
  if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return std::nullopt;
  throw std::runtime_error("failed to accept haptics client: " +
                           std::string(std::strerror(errno)));
}

std::optional<HapticsStreamNegotiation>
HapticsStreamEndpoint::receive_negotiation(int fd) const {
  std::array<std::uint8_t, kHapticsStreamNegotiationBytes + 1> packet{};
  const ssize_t received = ::recv(fd, packet.data(), packet.size(), MSG_DONTWAIT);
  if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
    return std::nullopt;
  }
  if (received != static_cast<ssize_t>(kHapticsStreamNegotiationBytes)) {
    throw std::invalid_argument("invalid haptics negotiation datagram length");
  }
  return decode_haptics_stream_negotiation(
      std::span<const std::uint8_t>(packet.data(), static_cast<std::size_t>(received)));
}

std::optional<HapticsStreamFrame> HapticsStreamEndpoint::receive_frame(int fd) const {
  std::array<std::uint8_t, kHapticsStreamMaxPacketBytes + 1> packet{};
  const ssize_t received = ::recv(fd, packet.data(), packet.size(), MSG_DONTWAIT);
  if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
    return std::nullopt;
  }
  if (received <= 0) return std::nullopt;
  if (received > static_cast<ssize_t>(kHapticsStreamMaxPacketBytes)) {
    throw std::invalid_argument("haptics stream packet exceeds maximum size");
  }
  return decode_haptics_stream_frame(
      std::span<const std::uint8_t>(packet.data(), static_cast<std::size_t>(received)));
}

} // namespace vds
