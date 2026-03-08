# Architecture

AndromedaCore is a QEMU-derived core intended to be the runtime of a Linux-native Android emulator (LDPlayer-like experience).

## Components

- **AndromedaCore (this repository)**
  - QEMU fork
  - custom devices needed for the target guest stack
- **Launcher / GUI (separate repository; not published yet)**
  - starts QEMU with correct arguments
  - manages per-instance configuration (paths, CPU/RAM, GPU, networking)
  - manages image locations and lifecycle
- **Android images (not shipped here)**
  - typically `system.qcow2`, `data.qcow2`, `sdcard.qcow2`

## Execution flow

1. User configures an instance in the launcher (image paths, RAM/CPU, GPU mode).
2. Launcher starts AndromedaCore QEMU binary with:
   - KVM enabled (`-enable-kvm`)
   - CPU/memory configuration (`-cpu host`, `-m`, `-smp`)
   - display/GPU device (`virtio-vga-gl` + `-display gtk,gl=on` or alternative)
   - storage mapped to qcow2 images (`-drive file=...,format=qcow2`)
   - input devices (`virtio-mouse-pci`, `virtio-keyboard-pci`)
   - optional host integration devices (e.g. `-device fastpipe`)
3. Guest boots from the provided images.
4. Host integration happens via the custom devices (when enabled).

## Custom device: fastpipe

`fastpipe` is a custom PCI device.

- **Implementation**: `hw/misc/fastpipe.c`
- **Build integration**:
  - `hw/misc/Kconfig`: `CONFIG_FASTPIPE`
  - `hw/misc/meson.build`: includes the device source file

### Purpose

- Provide a guest-visible device matching expectations of LDPlayer-style guest drivers.
- Provide a channel for guest→host requests (and later host→guest notifications) using:
  - BAR-mapped shared memory
  - I/O port command register
  - IRQ/MSI notifications

### Current behavior (at a glance)

- BAR0: small I/O space (command/status)
- BAR1: shared memory region
- BAR2: control/version block
- BAR3/BAR4: additional RAM-backed regions

The device currently contains stub behavior for "serial call" completion and a basic pipe connect path. Expect iteration.

## Repository boundaries / data policy

This repository intentionally does **not** include:

- Android images (`*.qcow2`, `*.img`, etc.)
- installed build outputs (`qemu-install/`-style directories)
- reverse engineering databases (`*.i64`, etc.)
- vendor/proprietary ROMs or partitions

Recommended distribution model:

- **Source code** in GitHub (this repo)
- **Large binaries** via GitHub Releases or external storage
- Optional **Git LFS** only if you intentionally want binary artifacts versioned

## Notes on licensing

This is a derivative work based on QEMU.

- See `LICENSE`, `COPYING`, `COPYING.LIB`, and per-file headers.
- Keep upstream attribution intact.
