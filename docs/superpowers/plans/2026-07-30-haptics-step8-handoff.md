# Haptics coexistence Step 8 handoff

Date: 2026-07-30  
Branch: `fix/linux-bt-hd-haptics-path`  
HEAD: `ebf97b8 feat: add bounded haptics stream framing`

## Current status

Step 8 implementation is present in the dirty worktree and is not committed.
Compatible legacy-rumble synthesis and source/policy state arbitration are in
place per the accepted plan. Legacy game motor state remains available for the
native compatible-rumble path when it is the only required source; when PCM is
required, deterministic per-port smoothed legacy-rumble PCM is incorporated into
the source-aware mixer. Legacy game rumble is suppressed only by `replace`;
OpenDS5 PCM is ignored by `off` and participates in `mix` and `replace`.
Gains, smoothing, and time response remain
provisional pending hardware calibration; no physical equivalence is claimed.

Independent review is clear with no blocking findings. This handoff does not
claim live daemon, Bluetooth, controller, or hardware validation.

## Step 8 implementation

- `vds/src/legacy_rumble.{hh,cc}` provides bounded, deterministic per-port
  smoothing and PCM synthesis without runtime allocation.
- `vds/src/haptics_mixer.{hh,cc}` and `vds/src/platform/linux/vdsd.cc` arbitrate
  explicit source and policy state, preserve native legacy-only output, and
  combine legacy and OpenDS5 PCM with one limiter and quantization path. This
  Step 8 arbitration integration is Linux-only.
- `vds/src/platform/win32/vdsd.cc` remains on its lease-era
  `set_haptic_audio_active` path; removing that ownership is explicit Step 9
  work, not a Step 8 deliverable.
- `vds/tests/legacy_rumble_test.cc`, `vds/tests/haptics_mixer_test.cc`,
  `vds/tests/output_state_haptics_test.cc`, and
  `vds/tests/haptics_client_registry_test.cc` cover motor updates, silence,
  policy/source/lifecycle transitions, reconnect, and per-port isolation.
- `docs/architecture/haptics.md`, `docs/protocol/haptics-stream-v1.md`, and
  `docs/testing/haptics-matrix.md` record the source-aware behavior and
  provisional calibration boundary.

## Validation completed

- Final relevant vDS build: passed.
- Focused Step 8 tests: 7/7 passed.
- Full affected automated matrix: 10/10 passed.
- `git diff --check`: passed.
- `scripts/dev/graphify-checkpoint`: passed inside `nix develop`.
- The vDS compiler still reports the existing ignored-`chown` warning from
  `open_control_socket`; it is non-blocking and unrelated to Step 8.

No live Bluetooth, controller, suspend/resume, or hardware feel/equivalence
testing was performed.

## Worktree safety

Preserve all existing dirty and untracked work, including the accepted-plan
changes, modified GUI/Nix/vDS/docs files, new haptics sources and tests, and the
untracked generated `result-1`, `result-2`, and `result-3` symlinks. No unrelated
changes were reverted or overwritten. Do not commit, push, reset, clean, or
remove generated outputs without explicit user permission.

## Required next actions: Step 9

1. Remove `HapticLease` and global `haptic_audio_active_` ownership from
   `vds_protocol` and Linux/Win32 `vdsd`.
2. Replace lease-derived behavior with explicit source/policy arbitration and
   retain only source-specific OpenDS5 disconnect/watchdog handling.
3. Rewrite `haptic_lease_test` and lease-era `output_state` tests around the new
   contract.
4. Prove app closure immediately restores pure game behavior; idle, zero, and
   nonzero app streams must never transfer ownership.
5. Cover policy/source/lifecycle/per-port transitions and update authoritative
   architecture documentation.
6. Run the full affected automated matrix, obtain an independent review, and
   run `scripts/dev/graphify-checkpoint` at the numbered-step boundary.

Step 9 must remain decision-complete before implementation begins; do not claim
physical coexistence or controller equivalence from automated tests alone.
