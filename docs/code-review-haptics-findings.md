# Code Review Findings: `fix/linux-bt-hd-haptics-path`

Review of the diff between `main` and `fix/linux-bt-hd-haptics-path`. Findings are
ordered by severity. Each is independently reproducible against
`origin/fix/linux-bt-hd-haptics-path`.

## 1. [CONFIRMED] Native-haptics overlay no longer activates for game PCM haptics

- **File**: `vds/src/platform/linux/vdsd.cc:1906` (`flush_pending_audio_chunk`)
- **Category**: correctness

The old code called `port.output_state.set_haptic_audio_active(true)` on every
audio chunk with `chunk.has_haptics_signal`, forcing
`enable_rumble_emulation`/`use_rumble_not_haptics` off in the outgoing report
so firmware would honor the transmitted native haptics PCM instead of the
host's legacy-rumble request. That call was dropped in the rewrite —
`set_native_haptics_active` (the renamed method) is now only invoked on a
`haptics_policy` transition to `Replace` (lines 2529/2532) or on client
disconnect (line 2689), never from the per-chunk path. The local variable
`legacy_active` computed at line 1906 is never used — evidence the wiring was
removed rather than relocated.

**Failure scenario**: a game streams rear-channel haptic PCM (e.g. DualSense
adaptive-trigger/haptic feedback) while the host (Steam Input, etc.) also has
legacy rumble armed, and `haptics_policy` is `Off` or `Mix` (the default).
The overlay that used to suppress legacy rumble-emulation per-chunk never
activates, so the outgoing BT/USB report can request legacy rumble emulation
at the same time real haptic PCM is sent — the exact conflicting-ownership
state the old lease model existed to prevent. Untested:
`vds/tests/output_state_haptics_test.cc` only exercises
`set_native_haptics_active` directly, not this trigger path.

## 2. [PLAUSIBLE] Unconditional per-frame peak computation on the audio hot path

- **File**: `vds/src/platform/linux/vdsd.cc:1695` (`handle_frame`)
- **Category**: efficiency

The nested peak-scanning loop over every incoming USB audio sample (4
channels, potentially thousands of packets/sec during gameplay) was
previously gated behind `if (output_trace)`. It now runs unconditionally on
every `valid_usb_audio_frame`, adding a constant O(frames×channels) cost to
the real-time audio receive path for a value (`game_pcm_peak_*`) only read by
occasional status queries.

**Fix idea**: compute peaks lazily only when a status query is pending, or
only when `output_trace` is enabled.

## 3. [PLAUSIBLE] Redundant `getgrnam` + blocking `/proc` read per haptics connection

- **File**: `vds/src/haptics_stream_endpoint.cc:935` (`accept_client`)
- **Category**: efficiency

`getgrnam("vds")` is already resolved once in `open()` for chown (line 908)
but is re-resolved via `getgrnam` on every `accept_client()` call, plus a
synchronous `/proc/<pid>/status` parse — both on the single-threaded epoll
loop that also services BT/haptics output.

**Failure scenario**: a slow/loaded NSS backend (LDAP, nscd) stalls the same
thread handling real-time haptics I/O. A flapping or crash-looping
`vds-haptics-client` (game restarts, PipeWire churn) retriggers this
repeatedly.

**Fix idea**: cache the resolved `vds_gid` as a member set once in `open()`;
avoid the `/proc` parse via `getgrouplist()` or a cached credential lookup.

## 4. [CONFIRMED] Duplicated little-endian encode/decode templates

- **Files**: `vds/src/haptics_stream.cc:429` and
  `vds/src/haptics_stream_endpoint.cc` (identical `append_le<T>`/`read_le<T>`)
- **Category**: reuse

Both files define identical byte-at-a-time little-endian templates instead of
sharing them via `haptics_stream.hh`, which `haptics_stream_endpoint.cc`
already includes.

**Failure scenario**: a fix to one copy (bounds checking, `std::endian`/
`memcpy`-based packing) doesn't propagate to the other, silently
reintroducing the bug in only one of the two wire formats (stream frames vs.
negotiation packets).

**Fix idea**: move both templates into the shared header, delete the second
copy.

## 5. [PLAUSIBLE] Triplicated negotiation-expiry sweep

- **File**: `vds/src/platform/linux/vdsd.cc:2855` (main epoll loop)
- **Category**: simplification

The same "close any unnegotiated `haptics_clients` entry past
`kHapticsNegotiationTimeout`" loop appears three times within one loop
iteration (folded into `timeout_ms` computation, right after with an
explanatory comment, and again inside the `ready == 0` branch).

**Failure scenario**: a future change to expiry semantics (grace period,
logging, a metric) must be applied identically in three places or the
sweeps silently diverge.

**Fix idea**: extract one `expire_stale_haptics_clients(...)` helper called
from both the pre-wait and post-timeout sites.

## 6. [CONFIRMED] Drop diagnostic only ever fires once per process

- **File**: `ds5-bridge/companion/native/audio-helper-linux.mjs:12`
  (`createHapticsInputWriter`)
- **Category**: correctness

The internal `dropped` counter is never reset, so the `onDrop` callback
(gated on `dropped === 1`) only ever fires once for the lifetime of the
process.

**Failure scenario**: if the child `vds-haptics-client` blocks
(`writableNeedDrain`) repeatedly over a long session (e.g. recurring socket
backpressure), only the very first drop is logged
(`status: haptics-input-drop`) and the caller's `droppedInputChunks` counter
never increments again — ongoing haptics data loss becomes invisible to
diagnostics.

**Fix idea**: report every drop, or track a running count instead of a
one-shot flag.

## 7. [PLAUSIBLE] Renderer haptics-support gate out of sync with main process

- **File**: `ds5-bridge/companion/src/main/bridge-service.ts:1970`
  (`audioReactiveHapticsSupported`) vs.
  `ds5-bridge/companion/src/renderer/App.tsx:3762`
- **Category**: correctness

`bridge-service.ts` now additionally requires
`this.device.supportsFeature('haptics-policy-v1')` on Linux. `App.tsx` was
not updated and still derives support purely from firmware flags.

**Failure scenario**: on Linux with an older vdsd daemon (no `capabilities`
command / missing `haptics-policy-v1`), the renderer still shows the
audio-reactive-haptics controls as available. The user toggles/tunes
settings, but `applyAudioReactiveHapticsSettings()` silently no-ops because
the main process's own `audioReactiveHapticsSupported()` returns false — user
changes appear to do nothing, with no error surfaced.

**Fix idea**: have `App.tsx` read support from the same capability signal
the main process uses (e.g. surface it in the status snapshot), or move the
check fully into the main process and disable the UI based on that.

## 8. [PLAUSIBLE] Stuck partial block can permanently disable stale-cleanup

- **File**: `vds/src/haptics_stream.cc:2005`
  (`HapticsSampleRing::pop_block` / `drop_stale`)
- **Category**: correctness / test-coverage

`partial_since_` is cleared whenever `pop_block` fully drains the ring, even
if a nonzero `accum_frames_` (leftover raw-frame downsample accumulator)
remains. `drop_stale()` short-circuits when `partial_since_ ==
time_point{}`, so it can never fire for that stuck partial state.

**Failure scenario**: an app streams a total sample count that leaves 1-15
leftover raw frames in the 16:1 downsample accumulator exactly when
`pop_block` fully drains the ring. If the app then stalls or hangs without
disconnecting, `has_partial()` stays true forever but `drop_stale()` never
trips (timer reset to epoch), so `next_wakeup_timeout_ms` busy-polls at the
50ms partial timeout indefinitely instead of hitting the intended
stale-drop/cleanup path.

**Fix idea**: only clear `partial_since_` when `accum_frames_ == 0` as well.

---

## Ruled out during verification

- **Windows regression claim**: an earlier pass flagged that
  `vds/src/platform/win32/vdsd.cc` lost its `set_haptic_audio_active`/
  `set_native_haptics_active` calls during the rename. Verified false —
  `set_native_haptics_active` does not exist in `win32/vdsd.cc` on either
  `main` or `fix/linux-bt-hd-haptics-path`; the native-haptics overlay is
  Linux-only in both versions.
