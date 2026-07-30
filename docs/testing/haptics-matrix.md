# Haptics Test Matrix

## Automated policy/source matrix

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

Expected policy rules:

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

- App helper closes normally.
- App helper crashes.
- Data socket disconnects.
- Control socket disconnects.
- Controller disconnects/reconnects.
- Bluetooth disconnects/reconnects.
- Suspend/resume.
- Policy changes mid-stream.
- Game begins/stops native PCM.
- Game changes legacy motor values.
- Idle connected app stream.
- Queue overrun.
- Queue underrun.
- Two ports active with different policies.
- Malformed and wrong-version frames.

## Nix integration tests

- GUI package has no daemon/system artifacts.
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
