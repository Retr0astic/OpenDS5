# Haptics Architecture

## Problem

Haptics state is source- and policy-driven. The daemon no longer treats recent
rear-channel PCM as global ownership; continuous game PCM cannot renew or
expire any controller-wide lease. Explicit `replace` policy forces native
audio-haptics mode, and prevents compatible game rumble from reaching the
controller. Step 5 policy is persisted and observable only; it does not alter
this behavior. Mix/replace arbitration is future work in Steps 7-8.

Shortening the timeout does not create coexistence; it creates mode flapping.

## Sources

Track independently per controller/port:

```text
GamePcm
GameLegacyRumble
OpenDs5Pcm
```

OpenDS5 PCM must reach `vdsd` through a dedicated IPC stream. It must not be
pre-mixed into the game's PipeWire rear channels.

## Policies

### Off

- Ignore OpenDS5 PCM.
- Preserve game native PCM.
- Preserve compatible game rumble natively.

### Mix

- Mix game native PCM and OpenDS5 PCM.
- Preserve compatible rumble natively when no PCM output is needed.
- When PCM output is needed, synthesize compatible-rumble state into PCM and
  mix it with the other sources.

### Replace

- Use OpenDS5 PCM.
- Exclude native game PCM from actuator output.
- Suppress compatible game rumble.
- Preserve unrelated speaker/headphone audio.

## Mixer

The daemon owns:

```text
GamePcmQueue
OpenDs5PcmQueue
LegacyRumbleSynth
HapticsPolicyController
HapticsStateArbiter
HapticsMixer
Limiter
```

Use internal floating-point samples, independent gains, bounded queues, and one
final quantization step.

### Compatible-rumble arbitration

Arbitration is evaluated independently for each controller port. A port stays
in native compatible-rumble mode when no PCM boundary is required. If game or
OpenDS5 PCM is emitted, the current legacy motor bytes are retained and passed
through a fixed-cost smoothed synthesizer before mixing; `replace` is the only
policy that suppresses them. The synthesizer is a calibrated approximation,
not a claim of physical equivalence. Calibration and feel must be validated on
each controller/firmware family before making that claim.

The reference `vds-haptics-client` sender runs outside the daemon real-time
path. It uses a bounded input buffer and advances a read offset, compacting
only after a batch (never front-erasing per packet); nonblocking sends account
blocked packets as drops and continue draining stdin.

The Step 7 seam runs at the 10 ms Bluetooth output boundary. It preserves the
speaker/headphone payload byte-for-byte, drops stale or short OpenDS5 frames,
and never delays native game audio. OpenDS5 and game gains are independent;
legacy-only game output remains native, while source closure immediately
restores pure game behavior.

## State arbitration

Derived state determines whether physical output uses compatible-rumble mode or
native PCM mode.

A socket disconnect or watchdog marks only `OpenDs5Pcm` inactive. It does not
claim or release global haptics ownership; only explicit source/policy state
changes the effective controller output state.

## Observability

Expose per-port:

- Policy.
- Active sources.
- Game motor values.
- Source peaks.
- Effective physical mode.
- Queue depth.
- Dropped blocks.
- Underruns.
- Limiter/clipping count.
- Last timestamps.

Step 5 exposes persisted policy and currently owned signals through
`vdsctl haptics-status --json`. Each port reports `off`, `mix`, or `replace`,
game PCM activity/peaks, decoded legacy motor values, policy-derived physical
mode, and queue/drop counters. Policy is observable only in this step; it does
not claim output ownership or change arbitration. Dedicated OpenDS5 PCM
activity, peaks, queue depth, and drops are source-owned after Step 6. The
dedicated source is queued and mixed at the Bluetooth output boundary;
underruns and limiting are reported per source. No time-based haptics lease
remains.

## Validation

Automated tests prove policy and source logic. Hardware tests prove controller
feel and game integration. Expedition 33 remains a named acceptance test.
