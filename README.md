# MiSTer Milestones

MiSTer Milestones is an achievement system for MiSTer FPGA hardware.

It provides a userspace daemon that reads FPGA core memory snapshots and evaluates achievement logic using the RetroAchievements (rcheevos) engine.

MiSTer Milestones is designed to bring deterministic, low-latency achievement evaluation directly to FPGA-based console cores.
## Releases & Docs

- **Hardware Alpha (v0.1.0-a1):** [`docs/hw-alpha/README_HW_ALPHA.md`](docs/hw-alpha/README_HW_ALPHA.md)

---
## ⚠️ Current Status (v0.1.0-a1 – Hardware Alpha)

This is the first hardware validation release of MiSTer Milestones.

This stage focuses on:

- Kernel module compatibility across MiSTer kernel versions
- Memory tap validation on real hardware
- Service stability during live core execution

Mock mode remains available for development and testing.

This release is experimental and intended for developers and early testers.

---

## What Works

- Mock mode (validated with NES only)
- NES region adapter (validated in mock mode)
- Achievement condition evaluation via rcheevos
- Console logging of triggered achievements
- `/dev/mmr_memtap` userspace interface implemented

---

## Not Yet Validated

- SNES adapter (implemented but untested)
- Genesis adapter (implemented but untested)
- Real hardware `/dev/mmr_memtap` mode across multiple kernels
- MiSTer FPGA core-level memtap integration

---

## Not Implemented Yet

- On-screen achievement display (OSD)
- RetroAchievements server API integration
- Account login or authentication
- Achievement persistence
- Validated memtap-enabled FPGA cores

This release validates the memory-read and achievement evaluation pipeline for NES in mock mode and begins hardware validation for MiSTer integration.

---

## Architecture Overview

MiSTer Milestones consists of:

### Kernel Interface
`/dev/mmr_memtap`
Provides structured memory snapshot access from FPGA cores to userspace.

### Userspace Daemon
`mmr-daemon`
- Reads memory snapshots
- Adapts console-specific memory regions
- Evaluates achievement conditions using rcheevos
- Emits console-based achievement events

### Development Tools
Mock RAM snapshot utilities for deterministic testing without FPGA patches.

---

## Build (Linux / Termux)

```sh
cd daemon
make
```

---

## Run (Mock Mode – Development)

Mock mode reads binary files representing region snapshots:

- `nes_cpu_ram.bin` (2048 bytes)
- `snes_wram.bin` (131072 bytes)
- `gen_68k_ram.bin` (65536 bytes)

Example (NES):

```sh
mkdir -p /tmp/mmr_mock
dd if=/dev/zero of=/tmp/mmr_mock/nes_cpu_ram.bin bs=1 count=2048

./daemon/mmr-daemon --mock /tmp/mmr_mock --core nes --fps 60
```

In another terminal:

```sh
python3 tools/mock_poke.py --file /tmp/mmr_mock/nes_cpu_ram.bin --offset 0x10 --value 1
```

Triggered achievements print to the console.

---

## Run (Real Hardware Mode – Experimental)

```sh
./daemon/mmr-daemon --dev /dev/mmr_memtap --core nes --backend ra
```

Requires:

- `/dev/mmr_memtap` kernel support
- Core-level memtap implementation
- Compatible MiSTer OS build

Hardware mode is experimental and part of the Hardware Alpha validation phase.

---

## Current Limitations

- No on-screen achievement notifications
- No server synchronization
- No persistent unlocked achievement storage
- No frontend UI
- No validated FPGA core integration
- Intended for developer experimentation

---

## RetroAchievements Backend Status

The RetroAchievements (rcheevos) engine is integrated locally for deterministic condition evaluation only.

Server API integration is not yet implemented.

Planned additions:

- Account login
- Server validation
- Achievement synchronization
- Optional OSD overlay

---

## Roadmap (High Level)

Planned progression:

1. Stabilize memtap kernel compatibility
2. Validate real hardware across common MiSTer kernels
3. Integrate on-screen notification system
4. Implement RetroAchievements server API support
5. Validate multiple FPGA cores

---

## License

MIT
