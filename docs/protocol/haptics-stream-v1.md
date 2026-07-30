# OpenDS5 Haptics Stream Protocol v1

Status: Implemented through Step 9. The daemon owns a per-port bounded source
queue, mixes sources at the output boundary, and uses explicit source/policy
arbitration without a time-based global ownership lease.

## Goals

- Preserve source identity.
- Associate a stream with one virtual-controller port.
- Carry generated stereo actuator PCM without blocking daemon hot paths.
- Support capability negotiation and clean failure behavior.
- Keep control and continuous data responsibilities separate.

## Transport

Recommended:

- Control plane: existing versioned daemon control socket.
- Data plane: Unix `SOCK_SEQPACKET`.
- Socket owner/group and mode grant only configured OpenDS5 users access.
- The daemon authorizes peers by `SO_PEERCRED`: root, the daemon uid, or a
  peer whose primary or supplementary groups include the deployed `vds` gid
  (read from `/proc/<pid>/status` with strict token parsing). Other peers, and
  peers whose group lookup cannot be verified, are rejected before negotiation.

## Control messages

Conceptual messages:

```text
GetCapabilities
OpenHapticsStream
SetHapticsPolicy
UpdateHapticsConfig
CloseHapticsStream
GetHapticsStatus
```

`OpenHapticsStream` negotiates:

- Protocol version.
- Port ID.
- Sample format.
- Sample rate.
- Channel count.
- Maximum frames per packet.
- Stream ID.

The endpoint negotiation packet is fixed-width little-endian and exactly 32
bytes:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | magic `ODS5` |
| 4 | 4 | protocol version (`1`) |
| 8 | 4 | message type (`1`, open stream) |
| 12 | 4 | virtual-controller port |
| 16 | 4 | nonzero stream ID |
| 20 | 2 | maximum frames per packet (`1..256`) |
| 22 | 2 | channel count (`2`) |
| 24 | 4 | sample rate (`48000`) |
| 28 | 2 | sample format (`1`, Float32 little-endian) |
| 30 | 2 | reserved, must be zero |

The listener uses Unix `SOCK_SEQPACKET`, nonblocking accepted descriptors, a
bounded backlog, and rejects malformed or oversized datagrams before they can
reach a source queue.

Step 5 policy compatibility uses the existing companion command
`SET_AUDIO_REACTIVE_HAPTICS` and does not create the data-plane stream. Its
persisted and observable policy is explicit: `off`, `mix`, or `replace`.
Source/policy arbitration is derived per controller: an active explicit
`replace` decision drives the output-state override that suppresses compatible
game rumble, while `off` and `mix` preserve the game's source according to the
source matrix. On the legacy wire, `off` is `value=0`; enabled `mix` is
`value=1, mode bit 0`; enabled `replace` is `value=1, mode bit 1`.

The output-state override is independent of game audio routing. In particular,
`set_audio_out_stream_active(false)` stops the game audio-out path but does not
clear a policy-derived `replace` override. Only the source/policy lifecycle may
clear that override, and source closure recomputes the effective state
immediately rather than relying on a timeout or audio amplitude.

## Data frame

Conceptual fields:

```text
protocol_version: u32
stream_id: u32
sequence: u64
monotonic_timestamp_ns: u64
frame_count: u16
samples: frame_count × stereo samples
```

The implemented frame is fixed-width little-endian and bounded:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | magic `ODS5` (`0x3553444f` as a little-endian integer) |
| 4 | 4 | protocol version (`1`) |
| 8 | 4 | nonzero stream ID |
| 12 | 8 | monotonically increasing sequence |
| 20 | 8 | monotonic timestamp in nanoseconds |
| 28 | 2 | frame count (`1..256`) |
| 30 | 2 | reserved, must be zero |
| 32 | `frame_count × 8` | interleaved stereo Float32 little-endian samples |

The maximum packet is 2080 bytes. NaN and infinity samples, truncated or
overlong packets, unknown versions, and nonzero reserved bits are rejected.

Initial audio contract:

- 48 kHz.
- Stereo actuator samples.
- Float32 or a documented integer format.
- Maximum packet size negotiated and enforced.

## Queue behavior

- Nonblocking producer and consumer behavior.
- Bounded per-stream queue.
- Reject malformed frames.
- Drop stale OpenDS5 frames when overloaded.
- Never delay game audio to preserve app-generated frames.
- Count drops, underruns, and sequence gaps.

The daemon-side bounded stereo-sample ring resamples negotiated 48 kHz input to
3 kHz with a fixed 16-input-frame box average per channel (512 input frames
produce one boundary). It emits one
32-stereo-frame (64-sample) block per 10 ms Bluetooth boundary. Packet
boundaries therefore do not affect continuity. Overflow drops
the oldest complete stereo samples; an incomplete block is retained until the
next packet and is dropped after the stale timeout rather than padded. Drops
are counted and the output path never blocks on the app source.
Bluetooth output uses 10 ms as a minimum send interval; each app block is
produced from 512 input frames (~10.667 ms at 48 kHz). Underrun reporting is
left unset until a producer deadline signal is available, avoiding false
cadence alarms.

## Lifecycle

- Stream is per port/controller.
- Policy is explicit and independently stored.
- Socket close immediately deactivates the app source.
- App-source closure clears only the derived app/source decision; an explicit
  game-audio routing stop does not clear a `replace` policy override.
- A watchdog handles stalled peers, but affects only that app stream.
- Controller disconnect invalidates or suspends the associated stream according
  to explicit reconnect semantics.
- Stream IDs cannot be reused ambiguously.

## Compatibility

Capabilities include:

- Control protocol version.
- Haptics stream versions.
- Supported formats.
- Supported policies.
- Optional diagnostics features.

Unknown major versions are rejected with an explicit error. Optional features
are negotiated rather than inferred.

Legacy reports without a mode payload decode as enabled `mix`, preserving
existing clients. The policy and status fields are additive and backward
compatible: `schemaVersion` remains `1`, older clients may ignore the appended
fields, while JSONL status reports expose
only signals owned by the current path. The source-aware output path reports
dedicated OpenDS5 stream connection, source peaks, queue depth, drops,
underruns, and limiting.

## Security

Validate:

- Peer permission.
- Port ID.
- Stream ownership.
- Frame length.
- Frame count.
- Sequence/timestamp sanity.
- Resource limits.

No data-plane message may request privileged system changes.
