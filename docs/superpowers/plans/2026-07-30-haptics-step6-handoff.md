# Haptics coexistence Step 6 handoff

Date: 2026-07-30
Branch: `fix/linux-bt-hd-haptics-path`
HEAD: `ebf97b8 feat: add bounded haptics stream framing`

## Current status

Step 6 implementation is present in the dirty worktree but is not committed.
The branch matches its remote. The daemon can receive source-identifiable
OpenDS5 actuator PCM over a bounded Unix `SOCK_SEQPACKET` stream, and the Linux
helper's production engine sends to that stream instead of writing generated
PCM into the virtual game sink with `pw-play`.

The daemon receives and accounts for the dedicated source, but Step 7 still
owns consuming/mixing that queue at the Bluetooth output boundary. Existing
game PCM and lease-era physical arbitration are unchanged; no hardware output
claim is made by Step 6.

Independent review is cleared with no blocking findings. Step 6 is formally
closed; this does not claim live daemon, Bluetooth, or hardware validation.

## Step 6 implementation

- `vds/src/haptics_stream_endpoint.{hh,cc}` defines the fixed 32-byte v1
  negotiation, validates version/port/stream/format/rate/channels/bounds,
  creates the nonblocking listener, applies `vds` group permissions, and checks
  peer credentials.
- `vds/src/platform/linux/vds_haptics_client.cc` reads helper four-channel
  Float32 frames, extracts `RL/RR`, emits bounded stereo v1 packets, and drops
  rather than blocking when the daemon socket is backpressured.
- `vds/src/platform/linux/vdsd.cc` registers listener and clients in epoll,
  limits packets processed per wake, permits one stream owner per port, keeps a
  separate bounded OpenDS5 queue, reports connection/peaks/depth/drops, and
  clears only that source on disconnect or port reload.
- `ds5-bridge/companion/native/audio-helper-linux.mjs` preserves PipeWire
  capture, source selection, filtering, envelope generation, and volume
  compensation while replacing production `pw-play` output with
  `vds-haptics-client`. Speaker and explicit test-tone playback still use
  `pw-play`.
- `vds/CMakeLists.txt` and `nix/vds.nix` build and package the native sender.
- `vds/tests/haptics_stream_endpoint_test.cc` covers negotiation, malformed
  reserved data, real seqpacket transfer, sender-process launch, and actuator
  channel extraction. Existing queue tests cover bounded overflow and sequence
  gaps. The production-backed registry test covers cap, expiry, per-port
  ownership, disconnect cleanup, and reload behavior.

The authoritative wire layout is in
`docs/protocol/haptics-stream-v1.md`. Architecture and test expectations are in
`docs/architecture/haptics.md` and `docs/testing/haptics-matrix.md`.

## Validation completed

- vDS CMake/Ninja build: passed using a clean writable `/tmp` cache.
- vDS CTest: 8/8 passed in final validation.
- Companion typecheck: passed.
- Companion tests: 64 files, 891 tests passed.
- Companion production build: passed; Vite reported only its existing large
  chunk warning.
- Working-tree Nix `vds` package build using `path:.#vds`: passed and includes
  `vds-haptics-client`; the GUI wrapper resolves it through the vDS package on
  PATH without copying daemon artifacts into the GUI package.
- `git diff --check`: passed.
- Unmerged paths/conflict markers: none.
- `scripts/dev/graphify-checkpoint` inside `nix develop`: passed.

Step 7 P2 carryovers are source-specific status semantics,
backpressure/drop observability, and sender allocation rationale/fix.

The vDS compiler still reports the pre-existing ignored-`chown` warning in
`open_control_socket`. No hardware, Bluetooth feel, suspend/resume, or live
daemon/helper integration was validated.

## Worktree safety

The worktree contains the accepted-plan changes from earlier packaging,
module, compatibility, observability, and Step 6 work. Preserve all of them.
The untracked `result-1`, `result-2`, and `result-3` symlinks are generated Nix
outputs and were left untouched. The four new Step 6 source/test files are also
untracked until the user explicitly authorizes staging or committing.

Do not commit, push, reset, clean, rebase, or remove generated symlinks without
explicit user permission.

## Required next actions

1. Run a Terra-medium read-only review focused on the complete Step 6 diff and
   `docs/agents/review-checklist.md`. Pay particular attention to event-loop
   starvation, stream ownership, disconnect/reload cleanup, socket permissions,
   negotiation compatibility, package/runtime discovery, and overclaimed tests.
2. Route accepted findings to one Luna-medium complex implementer, then rerun
   the affected vDS, companion, Nix, hygiene, and Graphify checks.
3. Mark Step 6 formally closed only when no blocking review finding remains.
4. Begin Step 7 at `flush_pending_audio_chunk` in Linux `vdsd`: consume the
   dedicated queue at the existing 10 ms output boundary, preserve speaker and
   headphone channels, and implement `off`/`mix`/native-PCM `replace` with
   floating-point mixing, one limiter, and one quantization step.
5. Add deterministic source/policy, clipping, stale-data, independent-gain,
   and per-port isolation tests before changing lease-era arbitration.

Do not remove `HapticLease`, synthesize compatible rumble, or claim physical
coexistence in Step 7; those belong to Steps 8-9 and hardware validation.
