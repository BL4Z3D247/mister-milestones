\
#pragma once
/*
 * MiSTer Achievements (mra) - memtap ABI
 *
 * Device node: /dev/mra_memtap
 *
 * Design (MVP):
 *  - user selects a region (ioctl SELECT_REGION)
 *  - read() pulls bytes from the latest published snapshot for that region
 *  - optional WAIT_FRAME blocks until a newer frame snapshot exists
 *
 * This header is intended for BOTH kernel driver and userspace.
 */

#include <stdint.h>

#ifndef __KERNEL__
  #include <sys/ioctl.h>
#endif

#define MRA_ABI_VERSION 1
#define MRA_MEMTAP_MAGIC 0x4D5241u /* 'MRA' */

#ifdef __cplusplus
extern "C" {
#endif

enum mra_core_id {
  MRA_CORE_UNKNOWN = 0,
  MRA_CORE_NES     = 1,
  MRA_CORE_SNES    = 2,
  MRA_CORE_GENESIS = 3,
};

enum mra_region_id {
  MRA_REGION_NONE = 0,

  /* NES */
  MRA_REGION_NES_CPU_RAM = 10, /* 0x0800 */

  /* SNES */
  MRA_REGION_SNES_WRAM   = 20, /* 0x20000 */

  /* Genesis / Mega Drive */
  MRA_REGION_GEN_68K_RAM = 30, /* 0x10000 */
};

enum mra_region_flags {
  MRA_RF_SNAPSHOT = 1 << 0, /* reads return latest snapshot (read-only) */
};

struct mra_info {
  uint32_t abi_version;     /* MRA_ABI_VERSION */
  uint32_t core_id;         /* enum mra_core_id */
  uint32_t map_version;     /* per-core map revision */
  uint32_t region_count;    /* how many region_desc entries are valid */
  uint64_t frame_counter;   /* increments whenever a new snapshot is published */
};

struct mra_region_desc {
  uint32_t region_id;       /* enum mra_region_id */
  uint32_t flags;           /* enum mra_region_flags */
  uint32_t size_bytes;
  uint32_t reserved;
};

/* Optional helper used by userspace to seek within a region before read(). */
struct mra_seek_req {
  uint32_t offset;
  uint32_t reserved;
};

#ifndef __KERNEL__
  #define MRA_IOCTL_GET_INFO       _IOR(MRA_MEMTAP_MAGIC, 0x01, struct mra_info)
  #define MRA_IOCTL_GET_REGIONS    _IOR(MRA_MEMTAP_MAGIC, 0x02, struct mra_region_desc[16])
  #define MRA_IOCTL_SELECT_REGION  _IOW(MRA_MEMTAP_MAGIC, 0x03, uint32_t)
  #define MRA_IOCTL_WAIT_FRAME     _IOW(MRA_MEMTAP_MAGIC, 0x04, uint64_t)
  #define MRA_IOCTL_SEEK           _IOW(MRA_MEMTAP_MAGIC, 0x05, struct mra_seek_req)
#endif

#ifdef __cplusplus
}
#endif
