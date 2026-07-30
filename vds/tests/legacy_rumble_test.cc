#include "legacy_rumble.hh"

#include <cassert>
#include <cstdint>

int main() {
  using namespace vds;
  assert(arbitrate_haptics_mode(false, false, false, HapticsMixPolicy::Off) ==
         HapticsPhysicalMode::Silent);
  assert(arbitrate_haptics_mode(false, true, false, HapticsMixPolicy::Mix) ==
         HapticsPhysicalMode::LegacyRumble);
  assert(arbitrate_haptics_mode(true, true, false, HapticsMixPolicy::Mix) ==
         HapticsPhysicalMode::Pcm);
  assert(arbitrate_haptics_mode(false, true, true, HapticsMixPolicy::Mix) ==
         HapticsPhysicalMode::Pcm);
  assert(arbitrate_haptics_mode(false, true, true,
                                HapticsMixPolicy::Replace) ==
         HapticsPhysicalMode::Pcm);
  assert(arbitrate_haptics_mode(false, true, false,
                                HapticsMixPolicy::Replace) ==
         HapticsPhysicalMode::Silent);
  LegacyRumbleSynth synth;
  const auto first = synth.render(255, 0);
  const auto second = synth.render(255, 0);
  assert(first[0] > 0 && second[0] > first[0]);
  assert(first[1] == 0 && second[1] == 0);
  const auto silence = synth.render(0, 0);
  assert(silence[0] > 0);  // decay is smoothed, not a click
  synth.reset();
  const auto reset = synth.render(0, 0);
  for (const auto sample : reset) assert(sample == 0);
  LegacyRumbleSynth other;
  assert(other.render(255, 0)[0] == first[0]);
  std::array<float, kHapticsSampleSize> game{};
  game[0] = 0.5F;
  const auto mixed = mix_haptics_boundary(
      std::span<const float, kHapticsSampleSize>(game), {},
      HapticsMixPolicy::Off, 0.5F, 1.0F);
  assert(mixed.haptics[0] == 32);
  LegacyRumbleState state;
  state.set_policy(HapticsMixPolicy::Mix);
  state.update(255, 0, true, false);
  assert(state.mode() == HapticsPhysicalMode::Pcm);
  const auto pcm = state.render_pcm();
  assert(pcm[0] > 0);
  state.update(0, 0, true, false);
  const auto decay = state.render_pcm();
  assert(decay[kHapticsSampleSize - 2] < pcm[kHapticsSampleSize - 2]);
  state.set_policy(HapticsMixPolicy::Replace);
  state.update(255, 255, false, false);
  assert(state.mode() == HapticsPhysicalMode::Silent);
  state.reset();
  assert(state.mode() == HapticsPhysicalMode::Silent);
  LegacyRumbleState other_state;
  other_state.set_policy(HapticsMixPolicy::Mix);
  other_state.update(255, 0, true, false);
  assert(other_state.render_pcm()[0] > 0);
  state.set_policy(HapticsMixPolicy::Mix);
  state.update(255, 255, false, true);
  assert(state.render_pcm()[0] > 0);
  state.disconnect();
  assert(state.mode() == HapticsPhysicalMode::Silent);
  state.update(255, 255, false, true);
  const auto reconnect = state.render_pcm();
  assert(reconnect[0] > 0 && reconnect[0] < 0.5F);
  return 0;
}
