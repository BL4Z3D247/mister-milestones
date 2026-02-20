#pragma once
/*
 * MiSTer Milestones Runtime (MMR) - memtap ABI
 *
 * Device node: /dev/mmr_memtap
 *
 * Design (MVP):
 *  - user selects a region (ioctl SELECT_REGION)
 *  - read() pulls bytes from the latest published snapshot for that region
 *  - optional WAIT_FRAME blocks until a newer frame snapshot exists
 *  - optional SEEK sets per-fd offset for subsequent read()
 *
 * This header is intended for BOTH kernel driver and userspace.
 */

#include <stdint.h>

#ifdef __KERNEL__
  #include <linux/ioctl.h>
#else
  #include <sys/ioctl.h>
#endif

#define MMR_ABI_VERSION 1
#define MMR_MEMTAP_MAGIC 0x4D4D52u /* 'MMR' */

#define MMR_MAX_REGIONS 16

#ifdef __cplusplus
extern "C" {
#endif

enum mmr_core_id {
  MMR_CORE_UNKNOWN = 0,
  MMR_CORE_NES     = 1,
  MMR_CORE_SNES    = 2,
  MMR_CORE_GENESIS = 3,
};

enum mmr_region_id {
  MMR_REGION_NONE = 0,

  /* NES */
  MMR_REGION_NES_CPU_RAM = 10, /* 0x0800 */

  /* SNES */
  MMR_REGION_SNES_WRAM   = 20, /* 0x20000 */

  /* Genesis / Mega Drive */
  MMR_REGION_GEN_68K_RAM = 30, /* 0x10000 */
};

enum mmr_region_flags {
  MMR_RF_SNAPSHOT = 1 << 0, /* reads return latest snapshot (read-only) */
};

struct mmr_info {
  uint32_t abi_version;     /* MMR_ABI_VERSION */
  uint32_t core_id;         /* enum mmr_core_id */
  uint32_t map_version;     /* per-core map revision (0 for loopback) */
  uint32_t region_count;    /* how many region_desc entries are valid */
  uint64_t frame_counter;   /* increments whenever a new snapshot is published */
};

struct mmr_region_desc {
  uint32_t region_id;       /* enum mmr_region_id */
  uint32_t flags;           /* enum mmr_region_flags */
  uint32_t size_bytes;
  uint32_t reserved;
};

/* Optional helper used by userspace to seek within a region before read(). */
struct mmr_seek_req {
  uint32_t offset;
  uint32_t reserved;
};

/* ioctl ABI (shared) */
#define MMR_IOCTL_GET_INFO       _IOR(MMR_MEMTAP_MAGIC, 0x01, struct mmr_info)
#define MMR_IOCTL_GET_REGIONS    _IOR(MMR_MEMTAP_MAGIC, 0x02, struct mmr_region_desc[MMR_MAX_REGIONS])
#define MMR_IOCTL_SELECT_REGION  _IOW(MMR_MEMTAP_MAGIC, 0x03, uint32_t)
#define MMR_IOCTL_WAIT_FRAME     _IOW(MMR_MEMTAP_MAGIC, 0x04, uint64_t)
#define MMR_IOCTL_SEEK           _IOW(MMR_MEMTAP_MAGIC, 0x05, struct mmr_seek_req)

#ifdef __cplusplus
}
#endif
