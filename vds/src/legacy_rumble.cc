#include "legacy_rumble.hh"

#include <algorithm>
#include <cmath>

namespace vds {

HapticsPhysicalMode arbitrate_haptics_mode(bool game_pcm, bool legacy_rumble,
                                           bool opends5_pcm,
                                           HapticsMixPolicy policy) {
  if (game_pcm || opends5_pcm) return HapticsPhysicalMode::Pcm;
  return legacy_rumble && policy != HapticsMixPolicy::Replace
             ? HapticsPhysicalMode::LegacyRumble
                       : HapticsPhysicalMode::Silent;
}

std::array<float, kHapticsSampleSize> LegacyRumbleSynth::render(
    std::uint8_t left, std::uint8_t right, bool enabled) noexcept {
  constexpr float kSmoothing = 0.25F;
  const float target_left = enabled ? static_cast<float>(left) / 255.0F : 0.0F;
  const float target_right = enabled ? static_cast<float>(right) / 255.0F : 0.0F;
  std::array<float, kHapticsSampleSize> output{};
  for (std::size_t i = 0; i < output.size(); ++i) {
    left_ += (target_left - left_) * kSmoothing;
    right_ += (target_right - right_) * kSmoothing;
    const float value = (i % 2 == 0) ? left_ : right_;
    output[i] = value;
  }
  return output;
}

void LegacyRumbleState::update(std::uint8_t left, std::uint8_t right,
                               bool game_pcm, bool opends5_pcm) noexcept {
  left_ = left;
  right_ = right;
  mode_ = arbitrate_haptics_mode(game_pcm, left != 0 || right != 0,
                                 opends5_pcm, policy_);
}

std::array<float, kHapticsSampleSize> LegacyRumbleState::render_pcm() noexcept {
  if (mode_ != HapticsPhysicalMode::Pcm || policy_ == HapticsMixPolicy::Replace)
    return {};
  return synth_.render(left_, right_);
}

}  // namespace vds
