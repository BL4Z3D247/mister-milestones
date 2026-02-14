# MiSTer Milestones (MMR)

MiSTer Milestones Runtime (MMR) is a MiSTer-native achievement framework prototype.

It provides a userspace daemon that reads FPGA core memory snapshots and evaluates achievement logic using the RetroAchievements (rcheevos) runtime engine.

---

## ⚠️ Current Status (v0.1.0-beta)

This is an early infrastructure release.

### What works

- Mock mode (validated with NES only)
- NES region adapter (validated in mock mode)
- Runtime achievement condition evaluation
- Console logging of triggered achievements
- `/dev/mmr_memtap` userspace interface implemented

### Not tested / not validated

- SNES adapter (implemented but untested)
- Genesis adapter (implemented but untested)
- Real hardware `/dev/mmr_memtap` mode
- MiSTer FPGA core integration

### What does NOT exist yet

- No on-screen display (no pop-ups)
- No RetroAchievements server API integration
- No account login or authentication
- No achievement persistence
- No validated memtap-enabled FPGA cores
- No real hardware testing performed

This release validates the memory-read + runtime evaluation pipeline for NES in mock mode only.

---

## What's Included

### kernel/mmr_memtap.h
Userspace ABI definition for `/dev/mmr_memtap` (ioctl + structures)

### daemon/mmr-daemon
- Mock mode (`--mock <dir>`) for development without FPGA patches
- Real device mode (`--dev /dev/mmr_memtap`) – untested
- NES region adapter (validated in mock mode)
- SNES / Genesis region scaffolding (untested)
- rcheevos runtime engine integration
- Console-based achievement logging

### tools/
Helpers for mutating mock RAM snapshot files during development

---

## Build (Linux / Termux)

cd daemon  
make  

---

## Run (mock mode – development)

Mock mode reads binary files representing region snapshots:

- nes_cpu_ram.bin (2048 bytes)
- snes_wram.bin (131072 bytes)
- gen_68k_ram.bin (65536 bytes)

Example (NES):

mkdir -p /tmp/mmr_mock  
dd if=/dev/zero of=/tmp/mmr_mock/nes_cpu_ram.bin bs=1 count=2048  

./daemon/mmr-daemon --mock /tmp/mmr_mock --core nes --fps 60  

In another terminal, mutate mock RAM:

python3 tools/mock_poke.py --file /tmp/mmr_mock/nes_cpu_ram.bin --offset 0x10 --value 1  

Triggered achievements will print to console.

---

## Run (real hardware mode – untested)

./daemon/mmr-daemon --dev /dev/mmr_memtap --core nes --backend ra  

⚠️ Requires:

- `/dev/mmr_memtap` kernel support
- Core-level memtap implementation
- Compatible MiSTer OS build

Hardware mode has not yet been validated on a physical MiSTer device.

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

The RetroAchievements (rcheevos) runtime engine is integrated locally for condition evaluation only.

Server API integration is NOT implemented.

Future releases will add:

- Account login  
- Server validation  
- Achievement sync  
- Optional OSD overlay  

---

## License

MIT
