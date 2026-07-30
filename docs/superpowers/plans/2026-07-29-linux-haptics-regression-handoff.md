# Linux haptics regression handoff

Date: 2026-07-29
Branch: `fix/linux-bt-hd-haptics-path`

## Current status

The launch IPC error is fixed, but physical/Test Haptics output is not yet
confirmed working. A new chat should continue from the live PipeWire path,
not assume the core haptics issue is solved.

## Confirmed history and root causes

- Branch commit `d5a9c92` made endpoint discovery require new WirePlumber tags.
- Existing installs retained older WirePlumber configuration, so valid sinks
  were rejected and Test Haptics threw/quietly skipped.
- Linux unavailable errors were not classified by `BridgeService`.
- The Nix package did not bundle the WirePlumber expected-content file, causing
  `bridge:getLinuxHapticsRepairStatus` to throw at launch.

## Changes currently in the dirty worktree

- `audio-helper-linux.mjs`: tagged endpoint preference; strict legacy OpenDS5
  and exact Sony DualSense compatibility paths; tagged-card authority; exact
  parent/profile/4-channel/channel-map checks; PipeWire string-form
  `audio.position` normalization.
- `audio-helper-linux.test.ts`: endpoint, ambiguity, wrong-map, Sony runtime,
  tagged-authority, and string-position regression coverage.
- `bridge-service.ts` and `bridge-service.test.ts`: Linux
  `capture-unavailable endpoint-*` handling and tests.
- `nix/opends5.nix`: bundles the WirePlumber config and sets a default
  `OPENDS5_WIREPLUMBER_CONFIG` path.
- `flake.nix`: `nix develop` bootstraps locked companion dependencies with
  `npm ci` when Vite is absent; failed bootstrap exits the shell.

## Live evidence

`pw-dump` showed:

- Device id `90`: `alsa_card.usb-Sony_Interactive_Entertainment_DualSense_Wireless_Controller-00`
- Profile: `pro-audio`
- Sink id `84`: `alsa_output.usb-Sony_Interactive_Entertainment_DualSense_Wireless_Controller-00.pro-output-0`
- Parent `device.id=90`
- Four channels, `FL,FR,RL,RR`
- `audio.position` is a quoted string (`[ "FL", "FR", "RL", "RR" ]`), not an array.

The updated resolver selects this endpoint from the recorded dump (`issue:
null`). This proves endpoint classification was one failure, but does not prove
that `pw-play` reaches the controller or that the kernel/audio path produces
physical haptics.

## Validation completed

- Nix package build succeeded; bundled WirePlumber config exists and wrapper
  exports `OPENDS5_WIREPLUMBER_CONFIG`.
- `nix run .#opends5` no longer reports the missing-config IPC error.
- Nix-shell `node --check` and `git diff --check` passed.
- After flake bootstrap, companion typecheck passed and focused tests passed:
  `audio-helper-linux.test.ts` + `bridge-service.test.ts`: 160 tests.
- Physical Bluetooth haptics, Test Haptics UI output, `pw-play` stream
  negotiation, disconnect/reconnect, and native game haptics remain unverified.

## Next investigation

1. Rebuild/run the current app and click Test Haptics with the controller
   connected.
2. Capture helper stderr and run the exact packaged helper with
   `--play-test-haptics`; verify `pw-play` exits successfully.
3. Observe PipeWire stream/node creation during playback (`pw-top`, `pw-dump`,
   or `wpctl status`) and verify samples reach sink channels 2/3 (`RL/RR`).
4. If `pw-play` succeeds but no vibration occurs, investigate vDS kernel output
   routing/HID report handling and WirePlumber profile activation; do not loosen
   endpoint matching further without runtime evidence.

Do not overwrite user WirePlumber files automatically, and do not restore the
old broad `/dualsense|vds/i` matcher.
