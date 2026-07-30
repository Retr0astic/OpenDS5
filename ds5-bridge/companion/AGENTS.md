# Companion and GUI Instructions

These instructions apply under `ds5-bridge/companion/`.

## Privilege boundary

The normal GUI and its launcher must never:

- Invoke `pkexec`.
- Start or stop the system daemon.
- Install a systemd service.
- Load kernel modules.
- Change udev, WirePlumber, Bluetooth, or group configuration.

Installation belongs to an explicit installer or the NixOS module.

## Haptics policy

The persisted and displayed values `off`, `mix`, and `replace` must be passed to
the daemon without reinterpretation.

- `off`: no OpenDS5-generated haptics.
- `mix`: add OpenDS5 output while preserving game haptics.
- `replace`: intentionally suppress game haptics.

Do not use helper activity, silence, or signal level as a policy substitute.

## Linux helper

After the haptics transport migration:

- Continue capturing the selected source through PipeWire.
- Continue filtering/envelope generation in the helper where appropriate.
- Send generated stereo actuator PCM through the dedicated daemon IPC stream.
- Do not write generated PCM to the virtual controller's game rear channels.
- Keep capture and output abstractions independently testable.
- Reconfigure policy without restarting capture where possible.
- Close the stream cleanly on application exit, controller change, or failure.

## Packaging

- The Nix Electron major must match the package manifest.
- Native modules must be built or patched for that Electron ABI.
- The GUI package contains no system service or kernel artifacts.
- Desktop integration and icons belong to the GUI package.

## Validation

For relevant changes, from `nix develop`:

```sh
npm --prefix ds5-bridge/companion run typecheck
npm --prefix ds5-bridge/companion run test:companion
npm --prefix ds5-bridge/companion run build:app
git diff --check
```

Add focused tests for settings propagation, IPC negotiation, reconnect behavior,
and helper shutdown. Do not claim controller behavior without hardware tests.
