# Nix Packaging Architecture

## Outputs

The flake should expose:

```text
packages.<system>.opends5
packages.<system>.vds
packages.<system>.default
apps.<system>.opends5
apps.<system>.default
nixosModules.opends5
nixosModules.default
checks.<system>.*
devShells.<system>.default
```

A generic kernel-module output may be omitted or named
`vds-module-default-kernel` to avoid implying compatibility with every host
kernel.

## `opends5`

Contains:

- Compiled Electron application.
- Matching Electron runtime.
- Native Node modules built/patched for that Electron ABI.
- Linux audio helper.
- Icons and desktop entry.
- GUI-specific runtime tools.

Does not contain:

- `vdsd` or `vdsctl`.
- Kernel module.
- Udev rules.
- WirePlumber configuration.
- Systemd unit.
- Privileged runtime helper.

## `vds`

Contains:

- `bin/vdsd`
- `bin/vdsctl`
- udev rules under `lib/udev/rules.d`
- WirePlumber config under `share/wireplumber/wireplumber.conf.d`
- optional generic service unit for non-NixOS packaging

The daemon and CLI are versioned together.

## Kernel derivation

Parameterize by `kernel` and build through the host's selected
`boot.kernelPackages`.

The NixOS module supplies the resulting package to
`boot.extraModulePackages` and loads the intended module declaratively.

## NixOS module

Owns:

- Packages installed on the system.
- `vds` group and configured members.
- Kernel module package and load list.
- Udev package registration.
- WirePlumber config package registration.
- `vdsd` systemd service.
- Explicit optional Bluetooth-exclusive policy.

The service starts at boot or module activation and is not managed by the GUI.

## Launch behavior

`apps.opends5` launches the unprivileged GUI directly.

Before enabling features, the GUI checks daemon protocol/capabilities. It may
show an actionable error, but it does not invoke Polkit or replace the daemon.

An optional portable path must be separately named and is not canonical on
NixOS. Because the kernel module must still match the running kernel, avoid
presenting it as universally portable.

## Compatibility

Use protocol and feature negotiation:

```json
{
  "controlProtocol": 4,
  "hapticsStreamProtocol": 1,
  "features": [
    "source-aware-haptics",
    "haptics-policy-v1"
  ]
}
```

Do not use exact Nix store paths as a compatibility test.

## Checks

- Nix formatting/evaluation.
- GUI derivation.
- Daemon derivation.
- Electron startup smoke.
- Native-module load smoke.
- Package output ownership.
- NixOS module evaluation.
- NixOS VM service/socket test.
