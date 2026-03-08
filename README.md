# AndromedaCore

AndromedaCore is the core runtime for a **Linux-native Android emulator project** (an LDPlayer-like experience on Linux).

Technically, this repository is a fork/derivative work based on QEMU, extended to better support LDPlayer-style Android guests and host integration.

This repo contains only the **core (QEMU fork)**. The GUI/launcher and image management live in separate repositories.

## Project goal

Provide a convenient, desktop-friendly Android VM/emulator stack on Linux with:

- KVM acceleration
- modern GPU display path (e.g. `virtio-vga-gl`)
- custom host/guest integration devices (e.g. `fastpipe`)

## Architecture (high level)

- **AndromedaCore (this repo)**
  - QEMU fork
  - custom devices (currently: `fastpipe`)
- **Launcher/GUI (separate repo; not published yet)**
  - starts QEMU with the right command line
  - manages profiles, paths, images
- **Android images (not included here)**
  - `system.qcow2`, `data.qcow2`, `sdcard.qcow2` or equivalent

More details: `docs/ARCHITECTURE.md`.

## Building (Linux)

Minimal build (recommended for CI / quick iteration):

```bash
mkdir -p build
cd build
../configure --disable-docs --disable-tools --disable-werror --target-list=x86_64-softmmu
make -j$(nproc)
```

Full upstream-style documentation and build options are available in `README.rst`.

## Running (example)

This is a minimal example to show how AndromedaCore is expected to be invoked.
Paths to images are placeholders (images are not shipped in this repository).

```bash
./build/qemu-system-x86_64 \
  -enable-kvm \
  -m 4G -smp 4 -cpu host \
  -device fastpipe \
  -device virtio-vga-gl \
  -display gtk,gl=on \
  -drive file=system.qcow2,if=ide,index=0,format=qcow2 \
  -drive file=data.qcow2,if=ide,index=1,format=qcow2 \
  -drive file=sdcard.qcow2,if=ide,index=2,format=qcow2 \
  -device virtio-mouse-pci \
  -device virtio-keyboard-pci \
  -net nic,model=virtio -net user
```

## Device: fastpipe

`fastpipe` is a custom PCI device intended to match expectations of LDPlayer-style guests/drivers and enable host integration.

Current implementation lives in:

- `hw/misc/fastpipe.c`

To enable it in builds, the repo provides:

- `hw/misc/Kconfig`: `CONFIG_FASTPIPE`
- `hw/misc/meson.build`: build integration

## Repository scope (what is and is not included)

This repository **does not** include:

- Android images (`*.qcow2`, `*.img`, etc.)
- vendor ROMs / proprietary system partitions
- installed QEMU trees (e.g. `qemu-install/`)
- reverse engineering databases / large analysis artifacts

If you are looking for the GUI/launcher and image orchestration, check the separate Andromeda launcher repository.

## License

See `LICENSE`, `COPYING`, `COPYING.LIB`, and per-file headers.

Because this is a derivative work based on QEMU, please keep upstream attribution and licensing requirements intact.
