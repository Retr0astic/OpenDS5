# Haptics Architecture

## Problem

The temporary 100 ms lease treats recent nonzero rear-channel PCM as haptics
ownership. Continuous game PCM renews the lease indefinitely, forces native
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

## State arbitration

Derived state determines whether physical output uses compatible-rumble mode or
native PCM mode.

A socket disconnect or watchdog marks only `OpenDs5Pcm` inactive. It does not
claim or release global haptics ownership.

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
game PCM activity/peaks, decoded legacy motor values, lease-derived physical
mode, and queue/drop counters. Policy is observable only in this step; it does
not claim output ownership or change arbitration. Dedicated OpenDS5 PCM
activity/peaks, underruns, and limiting are false/zero because the existing
path does not distinguish those sources or implement those counters yet. The continuously renewed
`HapticLease` remains for legacy behavior and is covered by a regression test;
removal belongs to Step 9.

## Validation

Automated tests prove policy and source logic. Hardware tests prove controller
feel and game integration. Expedition 33 remains a named acceptance test.
