# OpenDS5 Haptics Stream Protocol v1

Status: wire contract and daemon-side queue primitives implemented in Step 6;
native endpoint and Linux helper wiring remain in progress.

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
- Peer credentials are checked where supported.

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

Step 5 policy compatibility uses the existing companion command
`SET_AUDIO_REACTIVE_HAPTICS` and does not create the data-plane stream. Its
persisted and observable policy is explicit: `off`, `mix`, or `replace`. These
values are stored and reported only in Step 5; they do not change output
ownership or arbitration. Mix/replace behavior belongs to Steps 7-8. On the
legacy wire, `off` is `value=0`; enabled `mix` is `value=1, mode bit 0`;
enabled `replace` is `value=1, mode bit 1`.

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

The daemon-side bounded queue drops its oldest OpenDS5 frame on overflow and
records drops and forward sequence gaps. It never blocks the game output path.

## Lifecycle

- Stream is per port/controller.
- Policy is explicit and independently stored.
- Socket close immediately deactivates the app source.
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
only signals owned by the current path. Dedicated OpenDS5 PCM, underrun, and
limiter signals are explicitly false/zero until later plan steps add those
owners.

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
