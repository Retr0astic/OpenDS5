#include "haptics_mixer.hh"

#include <algorithm>
#include <cmath>

namespace vds {

HapticsMixResult mix_haptics_boundary(
    std::span<const float, kHapticsSampleSize> game,
    std::span<const float> opends5, HapticsMixPolicy policy,
    float game_gain, float opends5_gain) {
  HapticsMixResult result;
  const bool use_app = policy != HapticsMixPolicy::Off && !opends5.empty();
  const bool use_game = policy != HapticsMixPolicy::Replace;
  for (std::size_t i = 0; i < game.size(); ++i) {
    float value = use_game ? game[i] * game_gain : 0.0F;
    if (use_app && i < opends5.size()) value += opends5[i] * opends5_gain;
    if (value > 1.0F || value < -1.0F) result.limiting = true;
    value = std::clamp(value, -1.0F, 1.0F);
    result.haptics[i] = static_cast<std::int8_t>(std::clamp(
        static_cast<int>(std::lround(value * 127.0F)), -128, 127));
    result.has_signal |= result.haptics[i] != 0;
  }
  return result;
}

HapticsMixResult mix_haptics_boundary(
    std::span<const std::int8_t, kHapticsSampleSize> game,
    std::span<const float> opends5, HapticsMixPolicy policy,
    float game_gain, float opends5_gain) {
  std::array<float, kHapticsSampleSize> game_float{};
  for (std::size_t i = 0; i < game.size(); ++i)
    game_float[i] = static_cast<float>(game[i]) / 127.0F;
  return mix_haptics_boundary(std::span<const float, kHapticsSampleSize>(game_float),
                              opends5, policy, game_gain, opends5_gain);
}

} // namespace vds
