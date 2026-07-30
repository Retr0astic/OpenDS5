# OpenDS5 Architecture Overview

## Components

```text
OpenDS5 GUI and companion
  ├── settings and device control
  ├── system-audio capture and haptics generation
  └── unprivileged control/data connections
             │
             ▼
vdsd userspace daemon
  ├── virtual-device protocol
  ├── physical-controller state
  ├── game audio and HID output
  ├── source-aware haptics mixer
  └── Bluetooth/USB packet output
             │
             ▼
vds_hcd kernel module
  └── virtual DualSense device exposed to games
```

## Packaging ownership

- GUI package owns GUI files only.
- `vds` package owns userspace daemon and system-integration assets.
- Kernel package owns the kernel module for one kernel.
- NixOS module composes the packages and declares system state.

## Runtime privilege

The daemon is installed and started during explicit installation or NixOS
activation. The GUI remains unprivileged during normal operation.

## Haptics ownership

There is no global time-based haptics owner. The daemon evaluates explicit
policy and independently tracked sources for each controller/port.
