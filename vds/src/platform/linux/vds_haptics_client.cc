// SPDX-License-Identifier: MIT

#include "haptics_stream_endpoint.hh"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

struct Options {
  std::string socket = "/run/vdsd.sock.haptics";
  std::uint32_t port = 0;
  std::uint32_t stream_id = 1;
};

Options parse_options(int argc, char **argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (i + 1 >= argc) throw std::runtime_error(arg + " requires a value");
    const std::string value = argv[++i];
    if (arg == "--socket") options.socket = value;
    else if (arg == "--port") options.port = static_cast<std::uint32_t>(std::stoul(value));
    else if (arg == "--stream-id") options.stream_id = static_cast<std::uint32_t>(std::stoul(value));
    else throw std::runtime_error("unknown argument: " + arg);
  }
  if (options.stream_id == 0) throw std::runtime_error("stream id must be nonzero");
  return options;
}

int connect_socket(const std::string &path) {
  const int fd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
  if (fd < 0) throw std::runtime_error("socket failed: " + std::string(std::strerror(errno)));
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  if (path.size() >= sizeof(address.sun_path)) {
    ::close(fd);
    throw std::runtime_error("socket path is too long");
  }
  std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);
  if (::connect(fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) < 0) {
    const std::string error = std::strerror(errno);
    ::close(fd);
    throw std::runtime_error("connect failed: " + error);
  }
  const int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    ::close(fd);
    throw std::runtime_error("failed to set socket nonblocking");
  }
  return fd;
}

bool send_packet(int fd, std::span<const std::uint8_t> packet) {
  const ssize_t sent = ::send(fd, packet.data(), packet.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
  if (sent == static_cast<ssize_t>(packet.size())) return true;
  if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return false;
  throw std::runtime_error("haptics send failed: " + std::string(std::strerror(errno)));
}

std::uint64_t monotonic_ns() {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}

} // namespace

int main(int argc, char **argv) {
  try {
    const Options options = parse_options(argc, argv);
    const int fd = connect_socket(options.socket);
    const auto negotiation = vds::encode_haptics_stream_negotiation({
        .version = 1, .port = options.port, .stream_id = options.stream_id,
        .max_frames = vds::kHapticsStreamMaxFramesPerPacket});
    if (!send_packet(fd, negotiation)) {
      ::close(fd);
      throw std::runtime_error("haptics negotiation would block");
    }

    constexpr std::size_t input_channels = 4;
    constexpr std::size_t frame_bytes = input_channels * sizeof(float);
    std::vector<std::uint8_t> buffered;
    buffered.reserve(vds::kHapticsStreamMaxFramesPerPacket * frame_bytes * 2);
    std::array<std::uint8_t, 16384> input{};
    std::uint64_t sequence = 0;
    std::uint64_t dropped = 0;

    std::size_t buffered_offset = 0;
    while (true) {
      const ssize_t received = ::read(STDIN_FILENO, input.data(), input.size());
      if (received < 0 && errno == EINTR) continue;
      if (received < 0) throw std::runtime_error("stdin read failed: " + std::string(std::strerror(errno)));
      if (received > 0) buffered.insert(buffered.end(), input.begin(), input.begin() + received);

      while (buffered.size() - buffered_offset >= frame_bytes &&
             (received == 0 || buffered.size() - buffered_offset >= vds::kHapticsStreamMaxFramesPerPacket * frame_bytes)) {
        const std::size_t frames = std::min<std::size_t>(
            (buffered.size() - buffered_offset) / frame_bytes, vds::kHapticsStreamMaxFramesPerPacket);
        vds::HapticsStreamFrame frame{
            .stream_id = options.stream_id,
            .sequence = sequence++,
            .monotonic_timestamp_ns = monotonic_ns(),
            .frame_count = static_cast<std::uint16_t>(frames),
        };
        frame.samples.reserve(frames * 2);
        for (std::size_t i = 0; i < frames; ++i) {
          float left = 0;
          float right = 0;
          std::memcpy(&left, buffered.data() + buffered_offset + (i * input_channels + 2) * sizeof(float),
                      sizeof(left));
          std::memcpy(&right, buffered.data() + buffered_offset + (i * input_channels + 3) * sizeof(float),
                      sizeof(right));
          frame.samples.push_back(left);
          frame.samples.push_back(right);
        }
        const auto packet = vds::encode_haptics_stream_frame(frame);
        if (!send_packet(fd, packet)) ++dropped;
        buffered_offset += frames * frame_bytes;
      }
      if (buffered_offset != 0 &&
          (buffered_offset >= buffered.size() / 2 || received == 0)) {
        buffered.erase(buffered.begin(), buffered.begin() + buffered_offset);
        buffered_offset = 0;
      }
      if (received == 0) break;
    }
    ::close(fd);
    if (dropped != 0) std::cerr << "dropped haptics packets=" << dropped << "\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "vds-haptics-client: " << error.what() << "\n";
    return 1;
  }
}
