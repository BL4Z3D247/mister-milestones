# MiSTer Milestones – Hardware Alpha

**Version:** 0.1.0-a1
**Status:** Hardware Alpha (Early Testing)

---

## Overview

MiSTer Milestones is an achievement system for MiSTer FPGA hardware.

This hardware alpha release focuses on validating:

- Kernel module compatibility across MiSTer kernel versions
- Memory tap behavior on real hardware
- Service stability under live core execution

This package includes the internal daemon and memory tap module used by MiSTer Milestones.

⚠️ Experimental release. Expect rapid iteration.

---

## Package Structure

Inside `release/hw-alpha/`:

- `bin/mmr-daemon`
  Internal userspace service used by MiSTer Milestones.

- `conf/mmr.env`
  Runtime configuration loaded after installation.

- `scripts/`
  - `install.sh` — installs MiSTer Milestones into `/media/fat/mmr/`
  - `start.sh` / `stop.sh` / `status.sh` — manage the service
  - `build-memtap.sh` — attempts local kernel module build (only if headers exist)

- `module-src/`
  Out-of-tree kernel module source (`mmr_memtap`).

- `modules/`
  Optional prebuilt modules directory:
  - `modules/<uname -r>/mmr_memtap.ko`

---

## Installation (MiSTer)

1) Copy the **entire** `release/hw-alpha/` folder to your MiSTer SD card.
   Example destination: `/media/fat/mmr_hw_alpha/`

2) On MiSTer run:

```sh
cd /media/fat/mmr_hw_alpha
./scripts/install.sh
/media/fat/mmr/status.sh
/media/fat/mmr/start.sh
```

Logs:

`/media/fat/mmr/logs/mmr-daemon.log`

Stop:

`/media/fat/mmr/stop.sh`

---

## Kernel Compatibility (Critical)

Kernel modules (`.ko`) must match the MiSTer kernel version exactly.

Check your kernel on MiSTer:

```sh
uname -r
```

This package supports three scenarios:

**A) Prebuilt module exists (recommended)**

If the package contains `modules/<uname -r>/mmr_memtap.ko`, `install.sh` installs it automatically.

**B) Local build on MiSTer**

If `/lib/modules/$(uname -r)/build` exists, `install.sh` will attempt a local build.

Note: most MiSTer systems do **not** ship kernel headers/build trees.

**C) External build (PC/Docker)**

Build `mmr_memtap.ko` externally for your kernel version and place it at:

`modules/<uname -r>/mmr_memtap.ko`

Then re-run:

`./scripts/install.sh`

---

## Configuration

Config file after install:

`/media/fat/mmr/conf/mmr.env`

Defaults:

- `MMR_DEV=/dev/mmr_memtap`
- `MMR_BACKEND=ra`
- `MMR_CORE=nes`
- `MMR_FPS=60`
- `MMR_ONLY_ON_CHANGE=1`
- `MMR_LOG_EVERY=60`

Optional:

- `MMR_ACH_FILE=/media/fat/mmr/achievements/<file>.ach`

---

## Tester Reporting

When reporting issues, please include:

```sh
uname -r
ls -ld /lib/modules/$(uname -r)/build || echo NO_KERNEL_BUILD_TREE
ls -l /dev/mmr_memtap || echo NO_DEVICE_NODE
lsmod 2>/dev/null | grep mmr || echo NO_MODULE_LOADED
tail -n 200 /media/fat/mmr/logs/mmr-daemon.log 2>/dev/null || echo NO_LOG
```

And summarize:

- Kernel version
- Whether module installed vs built
- Whether `/dev/mmr_memtap` exists
- Last ~200 lines of daemon log

---

MiSTer Milestones is under active development.
Hardware alpha builds are focused on kernel compatibility validation first.
