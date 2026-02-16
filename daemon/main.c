#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../kernel/mmr_memtap.h"
#include "engine.h"
#include "memtap.h"

/* basic ms sleep */
static void sleep_ms(uint32_t ms) {
  usleep((useconds_t)ms * 1000u);
}

static uint32_t core_id_from_str(const char *s) {
  if (!s) return MMR_CORE_UNKNOWN;
  if (strcmp(s, "nes") == 0) return MMR_CORE_NES;
  if (strcmp(s, "snes") == 0) return MMR_CORE_SNES;
  if (strcmp(s, "genesis") == 0) return MMR_CORE_GENESIS;
  return MMR_CORE_UNKNOWN;
}

static uint32_t expected_region_for_core(uint32_t core_id) {
  switch (core_id) {
    case MMR_CORE_NES:     return MMR_REGION_NES_CPU_RAM;
    case MMR_CORE_SNES:    return MMR_REGION_SNES_WRAM;
    case MMR_CORE_GENESIS: return MMR_REGION_GEN_68K_RAM;
    default: return MMR_REGION_NONE;
  }
}

static engine_backend_t backend_from_str(const char *s) {
  if (!s) return ENGINE_BACKEND_NONE;
  if (strcmp(s, "ra") == 0) return ENGINE_BACKEND_RA;
  return ENGINE_BACKEND_NONE;
}

static void usage(const char *argv0) {
  fprintf(stderr,
    "Usage: %s [--dev /dev/mmr_memtap] [--mock DIR --core nes|snes|genesis] [--fps N] [--backend ra|none] [--only-on-change] [--log-every N]\n",
    argv0);
}

/* FNV-1a 32-bit hash over the whole snapshot (fast, good enough for change-detect) */
static uint32_t fnv1a32(const uint8_t *p, size_t n) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < n; i++) {
    h ^= (uint32_t)p[i];
    h *= 16777619u;
  }
  return h;
}

int main(int argc, char **argv) {
  const char* ach_file = NULL;

  const char *dev_path = "/dev/mmr_memtap";
  const char *mock_dir = NULL;
  const char *core_str = NULL;
  const char *backend_str = "ra";

  uint32_t fps = 60;
  uint32_t log_every = 60;
  int only_on_change = 0;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--ach-file") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "ERROR: --ach-file requires a path\n");
        return 1;
      }
      ach_file = argv[++i];
      continue;
    }

    if (strcmp(argv[i], "--dev") == 0 && i + 1 < argc) {
      dev_path = argv[++i];
    } else if (strcmp(argv[i], "--mock") == 0 && i + 1 < argc) {
      mock_dir = argv[++i];
    } else if (strcmp(argv[i], "--core") == 0 && i + 1 < argc) {
      core_str = argv[++i];
    } else if (strcmp(argv[i], "--backend") == 0 && i + 1 < argc) {
      backend_str = argv[++i];
    } else if (strcmp(argv[i], "--fps") == 0 && i + 1 < argc) {
      fps = (uint32_t)atoi(argv[++i]);
      if (fps == 0) fps = 60;
    } else if (strcmp(argv[i], "--only-on-change") == 0) {
      only_on_change = 1;
    } else if (strcmp(argv[i], "--log-every") == 0 && i + 1 < argc) {
      log_every = (uint32_t)atoi(argv[++i]);
    } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      usage(argv[0]);
      return 0;
    } else {
      fprintf(stderr, "Unknown arg: %s\n", argv[i]);
      usage(argv[0]);
      return 2;
    }
  }
  // Phase 1D: pass optional .ach file path to engine via environment
  if (ach_file && *ach_file) {
    setenv("MMR_ACH_FILE", ach_file, 1);
  }


  uint32_t core_id = core_id_from_str(core_str);
  if (mock_dir && core_id == MMR_CORE_UNKNOWN) {
    fprintf(stderr, "Mock mode requires --core nes|snes|genesis\n");
    return 2;
  }

  memtap_t mt;
  memset(&mt, 0, sizeof(mt));

  if (mock_dir) {
    if (!memtap_open_mock(&mt, mock_dir, core_id)) {
      fprintf(stderr, "ERR: memtap_open_mock(%s) failed\n", mock_dir);
      return 1;
    }
  } else {
    if (!memtap_open_device(&mt, dev_path)) {
      fprintf(stderr, "ERR: memtap_open_device(%s) failed\n", dev_path);
      return 1;
    }

    struct mmr_info info;
    memset(&info, 0, sizeof(info));
    if (memtap_get_info(&mt, &info)) {
      core_id = info.core_id;
    }
  }

  engine_backend_t backend = backend_from_str(backend_str);

  engine_t *eng = NULL;
  if (!engine_init(&eng, backend, core_id)) {
    fprintf(stderr, "ERR: engine_init failed\n");
    memtap_close(&mt);
    return 1;
  }

  if (!engine_load_builtin(eng)) {
    fprintf(stderr, "ERR: engine_load_builtin failed\n");
    engine_destroy(eng);
    memtap_close(&mt);
    return 1;
  }

  struct mmr_region_desc regions[16];
  uint32_t region_count = 0;
  if (!memtap_get_regions(&mt, regions, &region_count)) {
    fprintf(stderr, "ERR: memtap_get_regions failed\n");
    engine_destroy(eng);
    memtap_close(&mt);
    return 1;
  }

  uint32_t want_region = expected_region_for_core(core_id);
  if (want_region == MMR_REGION_NONE) {
    fprintf(stderr, "ERR: unknown core_id=%u\n", core_id);
    engine_destroy(eng);
    memtap_close(&mt);
    return 1;
  }

  uint32_t size = 0;
  for (uint32_t i = 0; i < region_count; i++) {
    if (regions[i].region_id == want_region) {
      size = regions[i].size_bytes;
      break;
    }
  }

  if (size == 0) {
    fprintf(stderr, "ERR: region %u not found in GET_REGIONS list\n", want_region);
    engine_destroy(eng);
    memtap_close(&mt);
    return 1;
  }

  if (!memtap_select_region(&mt, want_region)) {
    fprintf(stderr, "ERR: memtap_select_region(%u) failed\n", want_region);
    engine_destroy(eng);
    memtap_close(&mt);
    return 1;
  }

  uint8_t *buf = (uint8_t*)malloc(size);
  if (!buf) {
    fprintf(stderr, "ERR: malloc(%u) failed\n", size);
    engine_destroy(eng);
    memtap_close(&mt);
    return 1;
  }

  const uint32_t frame_ms = (fps ? (1000u / fps) : 16u);

  uint64_t frame = 0;
  uint64_t last_logged = 0;

  uint32_t last_hash = 0;

  fprintf(stdout, "[INFO] mmr-daemon started core_id=%u region=%u size=%u fps=%u backend=%s\n",
          core_id, want_region, size, fps, backend_str);
  fflush(stdout);

  while (1) {
    sleep_ms(frame_ms);

    if (!memtap_seek(&mt, 0)) {
      fprintf(stderr, "[ERR] memtap_seek(0) failed\n");
      break;
    }

    ssize_t n = memtap_read(&mt, buf, size);
    if (n < 0 || (uint32_t)n != size) {
      fprintf(stderr, "[ERR] memtap_read got %zd (expected %u)\n", n, size);
      break;
    }

    frame++;

    uint32_t h = fnv1a32(buf, size);
    int changed = (h != last_hash);

    if (!only_on_change || changed) {
      engine_do_frame(eng, buf, size);
      last_hash = h;
    }

    if (log_every && (frame - last_logged) >= log_every) {
      fprintf(stdout, "[INFO] frame=%llu size=%u changed=%s hash=0x%08x\n",
              (unsigned long long)frame, size, changed ? "yes" : "no", h);
      fflush(stdout);
      last_logged = frame;
    }
  }

  free(buf);
  engine_destroy(eng);
  memtap_close(&mt);
  return 0;
}
