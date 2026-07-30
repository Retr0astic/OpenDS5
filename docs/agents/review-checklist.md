# Review Checklist

## Nix packaging

- GUI package contains only GUI-owned artifacts.
- `vds` owns daemon, CLI, udev, WirePlumber, and optional generic service file.
- Kernel module is built against the configured kernel.
- NixOS module owns persistent system integration.
- Default GUI launch performs no Polkit action.
- No exact store-path compatibility check remains.
- Capability/version mismatch produces a clear error.
- Electron and native Node ABI match.
- `flake.lock` is unchanged unless intentionally required.
- NixOS rollback leaves a coherent daemon/module/GUI generation.

## Privilege and lifecycle

- No normal GUI code starts/stops/replaces the daemon.
- Control/data sockets have least-privilege ownership and mode.
- Bluetooth exclusive input is opt-in and warned.
- Failure does not leave a temporary service or root process.
- User-group changes and relogin requirements are documented.

## Haptics

- Game PCM, game compatible rumble, and OpenDS5 PCM remain distinguishable.
- `off`, `mix`, and `replace` are explicit.
- `mix` preserves both native game PCM and compatible game rumble.
- `replace` suppresses game haptics only by user policy.
- No amplitude/timeout ownership lease remains.
- App stream closure deactivates only the app source.
- Legacy rumble remains native when PCM output is unnecessary.
- Legacy state is synthesized when PCM output is required.
- Gains, limiter, clipping, underrun, and dropped frames are observable.
- Per-controller/port state cannot leak.

## Real-time and concurrency

- No blocking operation in audio or Bluetooth hot paths.
- Queues are bounded.
- Allocations are avoided after setup.
- Stale app frames are dropped.
- Timestamps are monotonic.
- Policy changes are atomic at a defined boundary.
- Disconnect, reconnect, suspend, and helper crash clean up correctly.

## Protocol

- Versions and capabilities are explicit.
- Unknown/malformed frames are rejected safely.
- Lengths and IDs are validated.
- Permissions and peer identity are defined.
- Backward compatibility behavior is tested and documented.

## Documentation and tests

- Documentation describes actual implemented behavior.
- Automated source/policy matrix exists.
- Hardware claims are backed by named manual tests.
- Expedition 33 remains listed as unverified until tested on hardware.
