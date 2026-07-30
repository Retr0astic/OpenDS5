# Haptics Test Matrix

## Automated policy/source matrix

Step 5 coverage verifies persisted `enabled + mode` decoding to explicit
`off`/`mix`/`replace`, legacy wire serialization, and
`vdsctl haptics-status --json` control output. Source/policy arbitration is
explicit per output-state instance: `replace` suppresses compatible game
rumble, while source closure restores game state immediately. Game audio-out
stop is routing-only and cannot clear a policy-derived override. Step 7 has
focused source/policy arbitration tests, including the deterministic
replace-to-audio-out-stop regression. The source matrix below is covered by
the mixer and legacy-rumble unit tests. Step 6 has focused framing/queue and
process-spawn unit coverage for dedicated OpenDS5 IPC negotiation, peer
authorization, malformed packets, bounded overflow, and actuator extraction.
Step 6 now exercises the production-backed Linux haptics client registry for
bounded pending-client admission, negotiation expiry, one owner per port,
disconnect cleanup, and reload owner removal. Physical Bluetooth/controller
integration remains unvalidated until Step 7 hardware tests.
Underrun and
limiting fields remain zero until Step 7 adds their owners.

For every policy test:

| Game PCM | Game legacy | OpenDS5 PCM |
|---:|---:|---:|
| off | off | off |
| on | off | off |
| off | on | off |
| off | off | on |
| on | off | on |
| off | on | on |
| on | on | off |
| on | on | on |

Automated source/arbitration coverage includes source/policy combinations, independent
gains, one-step limiting and int8 quantization, stale/short app frames,
speaker/headphone preservation, and per-port isolation. Hardware timing and
controller feel remain unvalidated here.

### Off

- OpenDS5 contributes no output.
- Game PCM remains.
- Game legacy remains native when selected by the game.

### Mix

- Game PCM and OpenDS5 PCM both appear.
- Game legacy remains native when PCM output is not otherwise required.
- Game legacy is synthesized when PCM output is required.
- Limiter prevents uncontrolled clipping.
- Source gains remain independent.

### Replace

- OpenDS5 PCM appears.
- Game PCM actuator contribution is absent.
- Game compatible-rumble contribution is absent.
- Speaker/headphone audio remains unaffected.

## Lifecycle tests

- App helper closes normally; source closure immediately restores game state
  (unit/process coverage only; full daemon lifecycle integration remains
  pending).
- App helper crashes.
- Data socket disconnects.
- Control socket disconnects.
- Controller disconnects/reconnects.
- Bluetooth disconnects/reconnects.
- Suspend/resume.
- Policy changes mid-stream.
- Game begins/stops native PCM.
- Game changes legacy motor values.
- Idle connected app stream (no timeout-based ownership change).
- Queue overrun.
- Queue underrun.
- Two ports active with different policies (not yet hardware validated).
- Malformed and wrong-version frames.

## Nix integration tests

- GUI package has no daemon/system artifacts.
- Electron/native startup smoke launches the packaged Electron runtime with
  `--version` and loads the bundled `node-hid` native module in isolation.
- Daemon package contains expected assets.
- Kernel package matches configured kernel.
- NixOS module creates service and permissions.
- Normal GUI launch does not invoke `pkexec`.
- Incompatible daemon returns a clear error.
- WirePlumber and udev packages are registered.
- Bluetooth exclusive mode remains opt-in.

## Manual hardware matrix

Record controller firmware, connection mode, Steam Input, game, scene, and
status output.

Required titles/configurations:

1. Expedition 33 reproducer.
2. A known native DualSense PCM title.
3. A conventional compatible-rumble title.
4. OpenDS5 audio-reactive haptics without a game.
5. Steam Input enabled and disabled where relevant.

For each, test `off`, `mix`, and `replace`, plus helper crash and controller
reconnect.
