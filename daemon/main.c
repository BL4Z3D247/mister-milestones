#include "memtap.h"
#include "adapters.h"
#include "notify.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *argv0) {
  fprintf(stderr,
    "Usage: %s [--dev /dev/mmr_memtap] [--mock DIR --core nes|snes|genesis] [--fps N]\n"
    "\n"
    "Modes:\n"
    "  Device: default, reads /dev/mmr_memtap\n"
    "  Mock:   --mock /path/to/dir --core nes|snes|genesis\n"
    "\n"
    "This MVP daemon currently just bulk-reads the primary RAM region once per frame\n"
    "and prints a small checksum to prove the data path.\n",
    argv0
  );
}

static uint32_t parse_core(const char *s) {
  if (!s) return MMR_CORE_UNKNOWN;
  if (strcmp(s, "nes") == 0) return MMR_CORE_NES;
  if (strcmp(s, "snes") == 0) return MMR_CORE_SNES;
  if (strcmp(s, "genesis") == 0) return MMR_CORE_GENESIS;
  if (strcmp(s, "gen") == 0) return MMR_CORE_GENESIS;
  if (strcmp(s, "md") == 0) return MMR_CORE_GENESIS;
  return MMR_CORE_UNKNOWN;
}

static uint32_t simple_checksum(const uint8_t *buf, uint32_t n) {
  uint32_t x = 0x12345678u;
  for (uint32_t i = 0; i < n; i++) {
    x = (x << 5) ^ (x >> 27) ^ buf[i];
  }
  return x;
}

int main(int argc, char **argv) {
  const char *devnode = "/dev/mmr_memtap";
  const char *mock_dir = NULL;
  uint32_t mock_core = MMR_CORE_UNKNOWN;
  uint32_t fps = 60;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--dev") == 0 && i + 1 < argc) {
      devnode = argv[++i];
    } else if (strcmp(argv[i], "--mock") == 0 && i + 1 < argc) {
      mock_dir = argv[++i];
    } else if (strcmp(argv[i], "--core") == 0 && i + 1 < argc) {
      mock_core = parse_core(argv[++i]);
    } else if (strcmp(argv[i], "--fps") == 0 && i + 1 < argc) {
      fps = (uint32_t)atoi(argv[++i]);
      if (fps == 0) fps = 60;
    } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      usage(argv[0]);
      return 0;
    } else {
      usage(argv[0]);
      return 2;
    }
  }

  memtap_t mt;
  bool ok;

  if (mock_dir) {
    if (mock_core == MMR_CORE_UNKNOWN) {
      notify(NOTIFY_ERR, "--mock requires --core nes|snes|genesis");
      return 2;
    }
    ok = memtap_open_mock(&mt, mock_dir, mock_core);
  } else {
    ok = memtap_open_device(&mt, devnode);
  }

  if (!ok) return 1;

  struct mmr_info info;
  if (!memtap_get_info(&mt, &info)) {
    memtap_close(&mt);
    return 1;
  }

  adapter_desc_t ad;
  if (!adapter_get(info.core_id, &ad)) {
    notify(NOTIFY_ERR, "No adapter for core_id=%u", info.core_id);
    memtap_close(&mt);
    return 1;
  }

  if (!memtap_select_region(&mt, ad.primary_region)) {
    memtap_close(&mt);
    return 1;
  }

  uint8_t *framebuf = (uint8_t*)malloc(ad.primary_size);
  if (!framebuf) {
    notify(NOTIFY_ERR, "malloc(%u) failed", ad.primary_size);
    memtap_close(&mt);
    return 1;
  }

  notify(NOTIFY_INFO, "mmr-daemon started (core_id=%u) region=%u size=%u bytes fps=%u",
         info.core_id, ad.primary_region, ad.primary_size, fps);

  uint64_t last_frame = info.frame_counter;
  uint32_t frame_ms = (fps > 0) ? (1000u / fps) : 16u;
  if (frame_ms == 0) frame_ms = 1;

  while (1) {
    (void)memtap_wait_frame(&mt, last_frame, frame_ms);

    if (!memtap_get_info(&mt, &info)) break;
    last_frame = info.frame_counter;

    if (!memtap_seek(&mt, 0)) break;
    ssize_t r = memtap_read(&mt, framebuf, ad.primary_size);
    if (r < 0) break;

    uint32_t csum = simple_checksum(framebuf, (uint32_t)r);
    notify(NOTIFY_INFO, "frame=%llu checksum=0x%08x", (unsigned long long)last_frame, csum);

    if (mock_dir) sleep_ms(frame_ms);
  }

  notify(NOTIFY_ERR, "exiting");
  free(framebuf);
  memtap_close(&mt);
  return 1;
}
