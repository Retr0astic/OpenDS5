// SPDX-License-Identifier: MIT

#include "haptics_stream_endpoint.hh"

#include <cassert>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <thread>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

std::string socket_path() {
  return "/tmp/opends5-haptics-endpoint-" + std::to_string(::getpid()) + ".sock";
}

vds::HapticsStreamFrame frame() {
  return vds::HapticsStreamFrame{
      .stream_id = 17,
      .sequence = 1,
      .monotonic_timestamp_ns = 2,
      .frame_count = 1,
      .samples = {0.25f, -0.5f},
  };
}

} // namespace

int main(int argc, char **argv) {
  assert(argc == 2);
  const std::array<std::uint32_t, 1> supplementary = {4242};
  assert(vds::haptics_peer_authorized(1000, 1000, supplementary, 2000, 4242));
  assert(!vds::haptics_peer_authorized(1000, 1000, {}, 2000, 4242));
  const auto path = socket_path();
  vds::HapticsStreamEndpoint endpoint(path);
  endpoint.open();
  assert(std::filesystem::exists(path));

  const int client = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
  assert(client >= 0);
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);
  assert(::connect(client, reinterpret_cast<const sockaddr *>(&address),
                   sizeof(address)) == 0);

  int accepted = -1;
  for (int attempt = 0; attempt < 100 && accepted < 0; ++attempt) {
    if (const auto candidate = endpoint.accept_client()) accepted = *candidate;
    if (accepted < 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  assert(accepted >= 0);

  const vds::HapticsStreamNegotiation negotiation{
      .version = 1, .port = 3, .stream_id = 17, .max_frames = 2};
  const auto negotiation_packet =
      vds::encode_haptics_stream_negotiation(negotiation);
  assert(::send(client, negotiation_packet.data(), negotiation_packet.size(), 0) ==
         static_cast<ssize_t>(negotiation_packet.size()));
  auto decoded_negotiation = endpoint.receive_negotiation(accepted);
  assert(decoded_negotiation && decoded_negotiation->port == 3);

  const auto frame_packet = vds::encode_haptics_stream_frame(frame());
  assert(::send(client, frame_packet.data(), frame_packet.size(), 0) ==
         static_cast<ssize_t>(frame_packet.size()));
  auto decoded_frame = endpoint.receive_frame(accepted);
  assert(decoded_frame && decoded_frame->stream_id == 17);
  assert(std::fabs(decoded_frame->samples[0] - 0.25f) < 0.0001f);

  auto malformed = negotiation_packet;
  malformed[30] = 1;
  bool rejected = false;
  try {
    (void)vds::decode_haptics_stream_negotiation(malformed);
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);

  ::close(accepted);
  ::close(client);

  int input_pipe[2]{};
  assert(::pipe(input_pipe) == 0);
  const pid_t child = ::fork();
  assert(child >= 0);
  if (child == 0) {
    ::close(input_pipe[1]);
    assert(::dup2(input_pipe[0], STDIN_FILENO) >= 0);
    ::close(input_pipe[0]);
    ::execl(argv[1], argv[1], "--socket", path.c_str(), "--port", "2",
            "--stream-id", "23", static_cast<char *>(nullptr));
    _exit(127);
  }
  ::close(input_pipe[0]);

  int sender = -1;
  for (int attempt = 0; attempt < 100 && sender < 0; ++attempt) {
    if (const auto candidate = endpoint.accept_client()) sender = *candidate;
    if (sender < 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  assert(sender >= 0);
  std::optional<vds::HapticsStreamNegotiation> sender_negotiation;
  for (int attempt = 0; attempt < 100 && !sender_negotiation; ++attempt) {
    sender_negotiation = endpoint.receive_negotiation(sender);
    if (!sender_negotiation) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  assert(sender_negotiation && sender_negotiation->port == 2 &&
         sender_negotiation->stream_id == 23);

  std::vector<float> four_channel(256 * 4, 0.0f);
  for (std::size_t i = 0; i < 256; ++i) {
    four_channel[i * 4 + 2] = 0.125f;
    four_channel[i * 4 + 3] = -0.25f;
  }
  const auto *bytes = reinterpret_cast<const std::uint8_t *>(four_channel.data());
  const std::size_t byte_count = four_channel.size() * sizeof(float);
  assert(::write(input_pipe[1], bytes, byte_count) ==
         static_cast<ssize_t>(byte_count));
  ::close(input_pipe[1]);

  std::optional<vds::HapticsStreamFrame> sender_frame;
  for (int attempt = 0; attempt < 100 && !sender_frame; ++attempt) {
    sender_frame = endpoint.receive_frame(sender);
    if (!sender_frame) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  assert(sender_frame && sender_frame->frame_count == 256);
  assert(std::fabs(sender_frame->samples[0] - 0.125f) < 0.0001f);
  assert(std::fabs(sender_frame->samples[1] + 0.25f) < 0.0001f);
  ::close(sender);
  int child_status = 0;
  assert(::waitpid(child, &child_status, 0) == child);
  assert(WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0);

  endpoint.close();
  assert(!std::filesystem::exists(path));
  return 0;
}
