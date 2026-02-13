# MiSTer Milestones (MMR)

MiSTer Milestones Runtime (MMR) is a MiSTer-native achievements framework. It enables automatic achievement
evaluation by exporting safe, read-only memory snapshots from FPGA cores and running an on-device daemon to
evaluate achievement logic. The first planned backend is RetroAchievements, but MMR is designed to support
multiple backends over time.

## What’s included in this starter repo

- `kernel/mmr_memtap.h` — stable userspace ABI (ioctl + structs) for `/dev/mmr_memtap`
- `daemon/` — `mmr-daemon` (MVP) with:
  - real `/dev/mmr_memtap` reader mode
  - mock mode (`--mock <dir>`) for development without FPGA patches
  - region adapters for NES/SNES/Genesis
  - a simple notification logger
- `tools/` — helper to mutate mock RAM snapshot files for testing

## Build (Linux / Termux)

```bash
cd daemon
make
```

## Run (mock mode)

Mock mode reads binary files that represent region snapshots:

- `nes_cpu_ram.bin` (2048 bytes)
- `snes_wram.bin` (131072 bytes)
- `gen_68k_ram.bin` (65536 bytes)

Example:

```bash
mkdir -p /tmp/mmr_mock
dd if=/dev/zero of=/tmp/mmr_mock/nes_cpu_ram.bin bs=1 count=2048
./daemon/mmr-daemon --mock /tmp/mmr_mock --core nes --fps 60
```

Then, in another terminal, mutate the mock RAM (for testing reads):

```bash
python3 tools/mock_poke.py --file /tmp/mmr_mock/nes_cpu_ram.bin --offset 16 --value 255
```

## Notes

This repo intentionally does **not** include RetroAchievements (rcheevos) or server API integration yet.
This MVP locks the memtap plumbing and adapter layer first. Next step is wiring an achievements backend
into `mmr-daemon` once memtap is live on real cores.
