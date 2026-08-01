#include <cassert>
#include <climits>
#include <cstdint>
#include <exception>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "vds_companion.hh"
#include "vds_log.hh"
#include "vdsd_common.hh"
#include "vdsctl_common.hh"

namespace {

std::vector<std::string> argv_values(std::initializer_list<std::string> values) {
  return std::vector<std::string>(values);
}

std::vector<char *> argv_pointers(std::vector<std::string> &values) {
  std::vector<char *> pointers;
  for (auto &value : values) {
    pointers.push_back(value.data());
  }
  return pointers;
}

} // namespace

int main() {
  assert(vds::parse_vdsctl_command("audio-stats") ==
         vds::VdsctlCommand::AudioStats);
  assert(vds::parse_vdsctl_command("haptics-status") ==
         vds::VdsctlCommand::HapticsStatus);
  assert(vds::parse_vdsctl_command("list") == vds::VdsctlCommand::List);

  std::string request;
  const auto capture = [&](const std::string &value) {
    request = value;
    return std::string("reply\n");
  };
  auto args = argv_values({"vdsctl", "audio-stats"});
  auto pointers = argv_pointers(args);
  assert(vds::run_vdsctl_audio_stats(static_cast<int>(pointers.size()),
                                     capture) == "reply\n");
  assert(request == "{\"command\":\"audio-stats\"}\n");
  args = argv_values({"vdsctl", "haptics-status", "--json"});
  pointers = argv_pointers(args);
  assert(vds::run_vdsctl_haptics_status(static_cast<int>(pointers.size()),
                                        pointers.data(), capture) == "reply\n");
  assert(request == "{\"command\":\"haptics-status\",\"format\":\"json\"}\n");
  args.push_back("extra");
  pointers = argv_pointers(args);
  bool rejected_extra = false;
  try {
    (void)vds::run_vdsctl_audio_stats(static_cast<int>(pointers.size()),
                                      capture);
  } catch (const std::exception &) {
    rejected_extra = true;
  }
  assert(rejected_extra);
  assert(vds::vdsctl_usage("1", "2026").find("vdsctl audio-stats") !=
         std::string::npos);

  const std::uint64_t large = static_cast<std::uint64_t>(UINT32_MAX) + 17;
  const std::vector<vds::VdsdControlAudioStats> stats = {
      {.port = 2,
       .path = "/dev/vds2",
       .audio_out_stream_active = false,
       .haptics_policy = "mix"},
      {.port = 0,
       .path = "/dev/vds0",
       .audio_out_stream_active = true,
       .audio_usb_frame_count = large,
       .nonzero_haptics_chunk_count = large + 1,
       .bt_0x36_sent_count = large + 2,
       .queue_drop_count = large + 3,
       .stale_drop_count = large + 4,
       .blocked_drop_count = large + 5,
       .pending_queue_depth = large + 6,
       .max_pending_queue_depth = large + 7},
      {.port = 1,
       .path = "/dev/vds1",
       .haptics_policy = "replace",
       .game_pcm_active = true,
       .game_legacy_motor_left = 12,
       .game_legacy_motor_right = 34,
       .effective_physical_mode = "native-audio",
       .game_pcm_peak_left = 400,
       .underrun_count = 0},
  };
  const std::string serialized = vds::format_vdsd_control_audio_stats(stats);
  assert(serialized.find(
             "{\"schemaVersion\":1,\"port\":2,\"path\":\"/dev/vds2\"") ==
         0);
  assert(serialized.find("\"audioOutStreamActive\":false") !=
         std::string::npos);
  assert(serialized.find("\"path\":\"/dev/vds2\"") <
         serialized.find("\"hapticsPolicy\":\"mix\"") );
  assert(serialized.find("\"audioOutStreamActive\":true") !=
         std::string::npos);
  assert(serialized.find(std::to_string(large + 7)) != std::string::npos);
  assert(serialized.find("nonZeroHapticsChunkCount") <
         serialized.find("bt0x36SentCount"));
  assert(serialized.find("\"hapticsPolicy\":\"replace\"") !=
         std::string::npos);
  assert(serialized.find("\"openDs5PcmActive\":false") !=
         std::string::npos);
  assert(serialized.find("/dev/vds2") < serialized.find("/dev/vds0"));

  vds::CompanionRuntime companion;
  vds::Logger logger("/tmp/audio_stats_control_test.log");
  std::uint32_t trace_flags = 0;
  bool reload_requested = false;
  const std::string unknown = vds::handle_vdsd_control_command(
      "{\"command\":\"audio-stats\",\"extra\":1}", "/tmp/no-vds-db",
      {}, {}, [] { return std::vector<vds::ControllerTarget>{}; }, trace_flags,
      reload_requested, companion, logger, stats);
  assert(unknown.find("\"OK\":false") != std::string::npos);

  const std::string status = vds::handle_vdsd_control_command(
      "{\"command\":\"haptics-status\",\"format\":\"json\"}",
      "/tmp/no-vds-db", {}, {},
      [] { return std::vector<vds::ControllerTarget>{}; }, trace_flags,
      reload_requested, companion, logger, stats);
  assert(status.find("\"hapticsPolicy\":\"replace\"") !=
         std::string::npos);
  assert(status.find("\"schemaVersion\":1") != std::string::npos);

  const std::string capabilities = vds::handle_vdsd_control_command(
      "{\"command\":\"capabilities\"}", "/tmp/no-vds-db", {}, {},
      [] { return std::vector<vds::ControllerTarget>{}; }, trace_flags,
      reload_requested, companion, logger, stats);
  assert(capabilities ==
         "{\"OK\":true,\"controlProtocol\":4,"
         "\"hapticsStreamProtocol\":1,\"features\":["
         "\"source-aware-haptics\",\"haptics-policy-v1\"]}\n");

  return 0;
}
