// Verifies SET_HAPTICS_BUFFER_LENGTH (0x0B) is stored in CompanionSettings
// through the control-request path (it was previously ack-and-ignore).
#include <cassert>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

#include "jsonl.hh"
#include "vds_companion.hh"
#include "vds_log.hh"

namespace {

std::string command_request_json(std::uint8_t command_id, std::uint16_t value,
                                std::vector<unsigned> payload = {}) {
  std::vector<unsigned> report(vds::kCompanionReportLength, 0);
  report[0] = 0x02; // command report id
  report[1] = 'D';
  report[2] = 'S';
  report[3] = '5';
  report[4] = 'B';
  report[5] = vds::kCompanionProtocolMajor;
  report[6] = vds::kCompanionProtocolMinor;
  report[7] = command_id;
  report[8] = 1;    // sequence
  report[9] = value & 0xff;
  report[10] = (value >> 8) & 0xff;
  for (std::size_t i = 0; i < payload.size() && i + 11 < report.size(); ++i) {
    report[i + 11] = payload[i];
  }
  std::string json = "{\"command\":\"companion\",\"op\":\"write\",\"report\":[";
  for (std::size_t i = 0; i < report.size(); ++i) {
    if (i != 0) {
      json += ",";
    }
    json += std::to_string(report[i]);
  }
  json += "]}";
  return json;
}

void send_command(vds::CompanionRuntime &runtime, std::uint16_t value,
                  vds::Logger &logger) {
  const auto fields =
      vds::parse_jsonl_object(command_request_json(0x0B, value), "test");
  vds::handle_companion_control_request(fields, runtime, "/dev/null", {},
                                        logger);
}

} // namespace

int main() {
  vds::Logger logger("/tmp/companion_buffer_length_test.log");
  vds::CompanionRuntime runtime;

  assert(runtime.settings.haptics_buffer_samples == 0);

  send_command(runtime, 115, logger);
  assert(runtime.last_result_code == 0); // kAckOk
  assert(runtime.settings.haptics_buffer_samples == 115);

  // Out-of-range values are rejected and do not clobber the stored value.
  send_command(runtime, 8, logger);
  assert(runtime.last_result_code != 0);
  assert(runtime.settings.haptics_buffer_samples == 115);

  send_command(runtime, 300, logger);
  assert(runtime.last_result_code != 0);
  assert(runtime.settings.haptics_buffer_samples == 115);

  send_command(runtime, 16, logger);
  assert(runtime.settings.haptics_buffer_samples == 16);
  send_command(runtime, 240, logger);
  assert(runtime.settings.haptics_buffer_samples == 240);

  auto send_policy = [&](std::uint16_t enabled, unsigned mode) {
    const auto fields = vds::parse_jsonl_object(
        command_request_json(0x22, enabled, {mode}), "test");
    vds::handle_companion_control_request(fields, runtime, "/dev/null", {},
                                          logger);
  };
  send_policy(0, 1);
  assert(runtime.settings.haptics_policy == 0); // off
  send_policy(1, 0);
  assert(runtime.settings.haptics_policy == 1); // mix
  send_policy(1, 1);
  assert(runtime.settings.haptics_policy == 2); // replace

  // The decoded additive policy is surfaced by the same status serializer;
  // this does not imply that Step 5 changes output ownership.
  const vds::VdsdControlAudioStats status{
      .haptics_policy = vds::haptics_policy_name(runtime.settings.haptics_policy),
  };
  const std::string status_json =
      vds::format_vdsd_control_audio_stats(std::span(&status, 1));
  assert(status_json.find("\"hapticsPolicy\":\"replace\"") !=
         std::string::npos);

  std::puts("companion_buffer_length_test OK");
  return 0;
}
