# MiSTer Milestones (MMR)

MiSTer Milestones Runtime (MMR) is a MiSTer-native achievement framework prototype.

It provides a userspace daemon that reads FPGA core memory snapshots and evaluates achievement logic using the RetroAchievements (rcheevos) runtime engine.

---

## ⚠️ Current Status (v0.1.0-beta)

This is an early infrastructure release.

What works:

- Mock mode (fully functional)
- Real `/dev/mmr_memtap` reader implemented
- NES / SNES / Genesis region adapters
- On-device achievement condition evaluation
- Console logging of triggered achievements

What does NOT exist yet:

- No on-screen display (no pop-ups)
- No RetroAchievements server API integration
- No account login or authentication
- No achievement persistence
- Requires memtap-enabled FPGA cores for real hardware mode

This release validates the memory-read + runtime evaluation pipeline only.

---

## What's Included

- `kernel/mmr_memtap.h`  
  Stable userspace ABI (ioctl + structs) for `/dev/mmr_memtap`

- `daemon/mmr-daemon` (MVP)  
  - Real `/dev/mmr_memtap` reader mode  
  - Mock mode (`--mock <dir>`) for development without FPGA patches  
  - Region adapters for NES / SNES / Genesis  
  - Runtime evaluation engine  
  - Console achievement logger  

- `tools/`  
  Helpers for mutating mock RAM snapshot files during development

---

## Build (Linux / Termux)

```
cd daemon
make
```

---

## Run (mock mode – development)

Mock mode reads binary files representing region snapshots:

- `nes_cpu_ram.bin` (2048 bytes)
- `snes_wram.bin` (131072 bytes)
- `gen_68k_ram.bin` (65536 bytes)

Example:

```
mkdir -p /tmp/mmr_mock
dd if=/dev/zero of=/tmp/mmr_mock/nes_cpu_ram.bin bs=1 count=2048

./daemon/mmr-daemon --mock /tmp/mmr_mock --core nes --fps 60
```

In another terminal, mutate mock RAM:

```
python3 tools/mock_poke.py --file /tmp/mmr_mock/nes_cpu_ram.bin --offset 0x10 --value 1
```

Triggered achievements will print to console.

---

## Run (real hardware mode)

```
./daemon/mmr-daemon --dev /dev/mmr_memtap --core nes --backend ra
```

⚠️ This requires:

- `/dev/mmr_memtap` support
- Core-level memtap implementation
- Compatible MiSTer OS build

Without memtap support, hardware mode will not function.

---

## Current Limitations

- No on-screen achievement notifications
- No server synchronization
- No persistent unlocked achievement storage
- No frontend UI
- Intended for developer testing

---

## RetroAchievements Backend Status

The RetroAchievements (rcheevos) runtime engine is integrated locally for condition evaluation.

Server API integration is NOT implemented yet.

Future releases will add:

- Account login
- Server validation
- Achievement sync
- Optional OSD overlay

---

## License

MIT
