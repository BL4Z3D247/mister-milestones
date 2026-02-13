#include "memtap.h"
#include "util.h"
#include "notify.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static const char* mock_file_for_region(uint32_t region_id) {
  switch (region_id) {
    case MMR_REGION_NES_CPU_RAM: return "nes_cpu_ram.bin";
    case MMR_REGION_SNES_WRAM:   return "snes_wram.bin";
    case MMR_REGION_GEN_68K_RAM: return "gen_68k_ram.bin";
    default: return NULL;
  }
}

static uint32_t default_region_size(uint32_t region_id) {
  switch (region_id) {
    case MMR_REGION_NES_CPU_RAM: return 0x0800;
    case MMR_REGION_SNES_WRAM:   return 0x20000;
    case MMR_REGION_GEN_68K_RAM: return 0x10000;
    default: return 0;
  }
}

bool memtap_open_device(memtap_t *mt, const char *devnode) {
  memset(mt, 0, sizeof(*mt));
  mt->backend = MEMTAP_BACKEND_DEVICE;
  mt->fd = open(devnode, O_RDONLY);
  if (mt->fd < 0) {
    notify(NOTIFY_ERR, "open(%s) failed: %s", devnode, strerror(errno));
    return false;
  }
  mt->selected_region = MMR_REGION_NONE;
  mt->seek_offset = 0;
  return true;
}

bool memtap_open_mock(memtap_t *mt, const char *mock_dir, uint32_t core_id) {
  memset(mt, 0, sizeof(*mt));
  mt->backend = MEMTAP_BACKEND_MOCK;
  mt->fd = -1;
  mt->mock_core_id = core_id;
  snprintf(mt->mock_dir, sizeof(mt->mock_dir), "%s", mock_dir);

  // Populate a minimal region list based on core_id
  mt->mock_region_count = 0;
  if (core_id == MMR_CORE_NES) {
    mt->mock_regions[mt->mock_region_count++] = (struct mmr_region_desc){
      .region_id = MMR_REGION_NES_CPU_RAM, .flags = MMR_RF_SNAPSHOT, .size_bytes = default_region_size(MMR_REGION_NES_CPU_RAM)
    };
  } else if (core_id == MMR_CORE_SNES) {
    mt->mock_regions[mt->mock_region_count++] = (struct mmr_region_desc){
      .region_id = MMR_REGION_SNES_WRAM, .flags = MMR_RF_SNAPSHOT, .size_bytes = default_region_size(MMR_REGION_SNES_WRAM)
    };
  } else if (core_id == MMR_CORE_GENESIS) {
    mt->mock_regions[mt->mock_region_count++] = (struct mmr_region_desc){
      .region_id = MMR_REGION_GEN_68K_RAM, .flags = MMR_RF_SNAPSHOT, .size_bytes = default_region_size(MMR_REGION_GEN_68K_RAM)
    };
  } else {
    notify(NOTIFY_ERR, "mock core_id %u not supported", core_id);
    return false;
  }

  mt->selected_region = mt->mock_regions[0].region_id;
  mt->seek_offset = 0;
  mt->mock_frame_counter = 0;
  return true;
}

void memtap_close(memtap_t *mt) {
  if (!mt) return;
  if (mt->backend == MEMTAP_BACKEND_DEVICE && mt->fd >= 0) {
    close(mt->fd);
  }
  mt->fd = -1;
}

bool memtap_get_info(memtap_t *mt, struct mmr_info *out) {
  if (!mt || !out) return false;

  if (mt->backend == MEMTAP_BACKEND_DEVICE) {
    if (ioctl(mt->fd, MMR_IOCTL_GET_INFO, out) != 0) {
      notify(NOTIFY_ERR, "ioctl(GET_INFO) failed: %s", strerror(errno));
      return false;
    }
    return true;
  }

  // mock
  out->abi_version = MMR_ABI_VERSION;
  out->core_id = mt->mock_core_id;
  out->map_version = 1;
  out->region_count = mt->mock_region_count;
  out->frame_counter = mt->mock_frame_counter;
  return true;
}

bool memtap_get_regions(memtap_t *mt, struct mmr_region_desc *out16, uint32_t *out_count) {
  if (!mt || !out16 || !out_count) return false;

  if (mt->backend == MEMTAP_BACKEND_DEVICE) {
    struct mmr_region_desc tmp[16] = {0};
    if (ioctl(mt->fd, MMR_IOCTL_GET_REGIONS, tmp) != 0) {
      notify(NOTIFY_ERR, "ioctl(GET_REGIONS) failed: %s", strerror(errno));
      return false;
    }
    // Kernel returns full array; use GET_INFO for count
    struct mmr_info info;
    if (!memtap_get_info(mt, &info)) return false;
    uint32_t n = info.region_count;
    if (n > 16) n = 16;
    memcpy(out16, tmp, sizeof(tmp));
    *out_count = n;
    return true;
  }

  // mock
  memcpy(out16, mt->mock_regions, sizeof(mt->mock_regions));
  *out_count = mt->mock_region_count;
  return true;
}

bool memtap_select_region(memtap_t *mt, uint32_t region_id) {
  if (!mt) return false;

  if (mt->backend == MEMTAP_BACKEND_DEVICE) {
    if (ioctl(mt->fd, MMR_IOCTL_SELECT_REGION, &region_id) != 0) {
      notify(NOTIFY_ERR, "ioctl(SELECT_REGION=%u) failed: %s", region_id, strerror(errno));
      return false;
    }
    mt->selected_region = region_id;
    mt->seek_offset = 0;
    return true;
  }

  // mock: validate
  for (uint32_t i = 0; i < mt->mock_region_count; i++) {
    if (mt->mock_regions[i].region_id == region_id) {
      mt->selected_region = region_id;
      mt->seek_offset = 0;
      return true;
    }
  }
  notify(NOTIFY_ERR, "mock: region_id %u not available for this core", region_id);
  return false;
}

bool memtap_seek(memtap_t *mt, uint32_t offset) {
  if (!mt) return false;

  if (mt->backend == MEMTAP_BACKEND_DEVICE) {
    struct mmr_seek_req req = {.offset = offset, .reserved = 0};
    if (ioctl(mt->fd, MMR_IOCTL_SEEK, &req) != 0) {
      notify(NOTIFY_ERR, "ioctl(SEEK=%u) failed: %s", offset, strerror(errno));
      return false;
    }
    mt->seek_offset = offset;
    return true;
  }

  // mock: just store offset; applied by memtap_read()
  mt->seek_offset = offset;
  return true;
}

ssize_t memtap_read(memtap_t *mt, void *buf, size_t len) {
  if (!mt || !buf) return -1;

  if (mt->backend == MEMTAP_BACKEND_DEVICE) {
    // assume kernel keeps file position for region/seek; plain read()
    ssize_t r = read(mt->fd, buf, len);
    if (r < 0) notify(NOTIFY_ERR, "read() failed: %s", strerror(errno));
    return r;
  }

  const char *fname = mock_file_for_region(mt->selected_region);
  if (!fname) {
    notify(NOTIFY_ERR, "mock: no file mapping for region %u", mt->selected_region);
    return -1;
  }

  char path[1024];
  snprintf(path, sizeof(path), "%s/%s", mt->mock_dir, fname);

  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    notify(NOTIFY_ERR, "mock: open(%s) failed: %s", path, strerror(errno));
    return -1;
  }

  if (lseek(fd, (off_t)mt->seek_offset, SEEK_SET) < 0) {
    notify(NOTIFY_ERR, "mock: lseek(%u) failed: %s", mt->seek_offset, strerror(errno));
    close(fd);
    return -1;
  }

  ssize_t r = read(fd, buf, len);
  if (r < 0) notify(NOTIFY_ERR, "mock: read failed: %s", strerror(errno));
  close(fd);
  return r;
}

bool memtap_wait_frame(memtap_t *mt, uint64_t last_frame, uint32_t timeout_ms) {
  if (!mt) return false;

  if (mt->backend == MEMTAP_BACKEND_DEVICE) {
    if (ioctl(mt->fd, MMR_IOCTL_WAIT_FRAME, &last_frame) != 0) {
      notify(NOTIFY_ERR, "ioctl(WAIT_FRAME) failed: %s", strerror(errno));
      return false;
    }
    return true;
  }

  // mock: simulate frame advance at roughly caller cadence
  (void)last_frame;
  sleep_ms(timeout_ms ? timeout_ms : 16);
  mt->mock_frame_counter++;
  return true;
}
