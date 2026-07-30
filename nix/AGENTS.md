# Nix and NixOS Instructions

These instructions apply under `nix/`.

## Package boundaries

Maintain separate derivations:

- `opends5`: GUI, Electron runtime, companion code, Linux audio helper, desktop
  integration, and GUI-specific runtime dependencies.
- `vds`: `vdsd`, `vdsctl`, udev assets, WirePlumber configuration, and optional
  generic non-NixOS systemd unit.
- `vds-kmod`: `vds_hcd.ko`, parameterized by the target kernel.
- NixOS module: system service, kernel-module registration/loading, groups,
  udev registration, WirePlumber integration, and explicit Bluetooth policy.

The GUI package must not own, copy, or symlink daemon, kernel, udev,
WirePlumber, or service artifacts.

## Privilege model

Normal `opends5` execution must:

- Never invoke `pkexec`.
- Never stop, replace, or install `vdsd.service`.
- Never write persistent system configuration.
- Connect to the declaratively configured daemon as an unprivileged user.
- Fail with a precise compatibility or permission message when unavailable.

An optional portable launcher may remain only if clearly named, explicitly
invoked, and documented as noncanonical. It must not be the default app.

## Compatibility

Do not compare exact Nix store paths to determine daemon compatibility.
Implement a version/capability handshake through `vdsctl` or the control
protocol.

Keep GUI, daemon, control protocol, and haptics-stream compatibility explicit.

## Kernel module

Build the module against:

```nix
config.boot.kernelPackages.kernel
```

Expose the NixOS module as the supported way to install the correct
kernel-specific package. Do not imply that a top-level default-kernel module can
be loaded on arbitrary configured kernels.

## NixOS integration

Prefer standard module mechanisms:

- `boot.extraModulePackages`
- `boot.kernelModules`
- `services.udev.packages`
- `services.pipewire.wireplumber.configPackages`
- `systemd.services`
- `users.groups` and `users.users.<name>.extraGroups`

Bluetooth exclusive-input behavior is globally disruptive and must be an
explicit option with a warning.

## Build rules

- Use the repository's existing dev flake.
- Do not update `flake.lock` without an intentional input change.
- Align the Electron major in Nix with `package.json`.
- Build the companion from its natural source directory or a precise fileset.
- Prefer deterministic native-module patching/building over broad
  `LD_LIBRARY_PATH`.
- Add package-output and startup smoke checks.
- Keep Nix formatting localized to changed files.

## Validation

At minimum for relevant changes:

```sh
nix flake check -L
nix build -L .#opends5 .#vds
nix eval .#nixosModules.default
git diff --check
```

For module changes, add an evaluation or NixOS VM check. Verify the output
contents rather than only successful derivation construction.
