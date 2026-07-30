#pragma once

#include "vds_protocol.hh"

#include <cstdint>
#include <span>

namespace vds {

enum class HapticsMixPolicy : std::uint8_t { Off = 0, Mix = 1, Replace = 2 };

struct HapticsMixResult {
  HapticsChunk haptics{};
  bool has_signal = false;
  bool limiting = false;
};

// Mixes one 32-frame actuator boundary. The speaker payload is deliberately
// not an input: it is copied unchanged by the caller into the outgoing packet.
HapticsMixResult mix_haptics_boundary(
    std::span<const float, kHapticsSampleSize> game,
    std::span<const float> opends5, HapticsMixPolicy policy,
    float game_gain, float opends5_gain);

HapticsMixResult mix_haptics_boundary(
    std::span<const std::int8_t, kHapticsSampleSize> game,
    std::span<const float> opends5, HapticsMixPolicy policy,
    float game_gain, float opends5_gain);

} // namespace vds
