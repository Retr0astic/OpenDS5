#pragma once

#include "haptics_mixer.hh"

#include <array>
#include <cstdint>

namespace vds {

enum class HapticsPhysicalMode : std::uint8_t { Silent, LegacyRumble, Pcm };

// Source arbitration is explicit and per-port: legacy-only output remains
// native, while any PCM source selects the PCM path. Replace suppresses game
// sources, including compatible rumble.
HapticsPhysicalMode arbitrate_haptics_mode(bool game_pcm, bool legacy_rumble,
                                           bool opends5_pcm,
                                           HapticsMixPolicy policy);

// Fixed-cost legacy motor to actuator PCM converter. State is retained across
// boundaries to smooth motor changes without allocations in the output path.
class LegacyRumbleSynth {
public:
  std::array<float, kHapticsSampleSize> render(std::uint8_t left,
                                                std::uint8_t right,
                                                bool enabled = true) noexcept;
  void reset() noexcept { left_ = right_ = 0.0F; }

private:
  float left_ = 0.0F;
  float right_ = 0.0F;
};

// Production seam bundling source arbitration and the per-port synth state.
// No shared/global state is used, so controller reconnects can reset one port.
class LegacyRumbleState {
public:
  void set_policy(HapticsMixPolicy policy) noexcept { policy_ = policy; }
  void update(std::uint8_t left, std::uint8_t right, bool game_pcm,
              bool opends5_pcm) noexcept;
  HapticsPhysicalMode mode() const noexcept { return mode_; }
  HapticsPhysicalMode mode_for(std::uint8_t left, std::uint8_t right,
                               bool game_pcm, bool opends5_pcm) const noexcept {
    return arbitrate_haptics_mode(game_pcm, left != 0 || right != 0,
                                  opends5_pcm, policy_);
  }
  std::array<float, kHapticsSampleSize> render_pcm() noexcept;
  void reset() noexcept {
    synth_.reset();
    mode_ = HapticsPhysicalMode::Silent;
    left_ = right_ = 0;
  }
  void disconnect() noexcept { reset(); }

private:
  LegacyRumbleSynth synth_;
  HapticsMixPolicy policy_ = HapticsMixPolicy::Off;
  HapticsPhysicalMode mode_ = HapticsPhysicalMode::Silent;
  std::uint8_t left_ = 0;
  std::uint8_t right_ = 0;
};

}  // namespace vds
