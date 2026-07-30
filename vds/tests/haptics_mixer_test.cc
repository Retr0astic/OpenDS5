#include "haptics_mixer.hh"

#include <array>
#include <cassert>
#include <cmath>

int main() {
  vds::HapticsChunk game{};
  std::array<float, vds::kHapticsSampleSize> app{};
  game.fill(32);
  app.fill(0.25F);
  const auto off = vds::mix_haptics_boundary(game, app, vds::HapticsMixPolicy::Off, 1.0F, 1.0F);
  assert(off.haptics == game);
  const auto mix = vds::mix_haptics_boundary(game, app, vds::HapticsMixPolicy::Mix, 1.0F, 1.0F);
  assert(mix.haptics[0] == 64);
  const auto replace = vds::mix_haptics_boundary(game, app, vds::HapticsMixPolicy::Replace, 1.0F, 1.0F);
  assert(replace.haptics[0] == 32);
  const auto limited = vds::mix_haptics_boundary(game, app, vds::HapticsMixPolicy::Mix, 4.0F, 4.0F);
  assert(limited.limiting && limited.haptics[0] == 127);
  const auto independent = vds::mix_haptics_boundary(
      game, app, vds::HapticsMixPolicy::Mix, 0.5F, 0.25F);
  assert(independent.haptics[0] == 24);
  std::array<float, 2> short_app{};
  const auto padded = vds::mix_haptics_boundary(game, short_app, vds::HapticsMixPolicy::Mix, 1.0F, 1.0F);
  assert(padded.haptics[0] == 32 && padded.haptics[1] == 32 && padded.haptics[2] == 32);
}
