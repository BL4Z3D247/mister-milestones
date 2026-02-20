#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/mmr_memtap.h"
#include "engine.h"
#include "memtap.h"
#include "util.h"

#ifndef MMR_VERSION
#define MMR_VERSION "0.1.0-a1"
#endif

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig) {
  (void)sig;
  g_stop = 1;
}

static void install_signal_handlers(void) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = on_signal;
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);
}

static uint32_t core_id_from_str(const char *s) {
  if (!s) return MMR_CORE_UNKNOWN;
  if (strcmp(s, "nes") == 0) return MMR_CORE_NES;
  if (strcmp(s, "snes") == 0) return MMR_CORE_SNES;
  if (strcmp(s, "genesis") == 0) return MMR_CORE_GENESIS;
  return MMR_CORE_UNKNOWN;
}

static const char* core_str_from_id(uint32_t core_id) {
  switch (core_id) {
    case MMR_CORE_NES: return "nes";
    case MMR_CORE_SNES: return "snes";
    case MMR_CORE_GENESIS: return "genesis";
    default: return "unknown";
  }
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
  if (strcmp(s, "none") == 0) return ENGINE_BACKEND_NONE;
  return ENGINE_BACKEND_NONE;
}

static const char* backend_str_from_id(engine_backend_t b) {
  switch (b) {
    case ENGINE_BACKEND_RA: return "ra";
    case ENGINE_BACKEND_NONE: return "none";
    default: return "none";
  }
}

static int parse_u32(const char *s, uint32_t *out) {
  if (!s || !*s || !out) return 0;
  errno = 0;
  char *end = NULL;
  unsigned long v = strtoul(s, &end, 10);
  if (errno != 0) return 0;
  if (end == s || *end != '\0') return 0;
  if (v > 0xFFFFFFFFul) return 0;
  *out = (uint32_t)v;
  return 1;
}

static void usage(const char *argv0) {
  fprintf(stderr,
    "MiSTer Milestones daemon (mmr-daemon) %s\n"
    "\n"
    "Usage:\n"
    "  %s [--dev /dev/mmr_memtap] [--backend ra|none] [--fps N] [--only-on-change] [--log-every N]\n"
    "  %s --mock DIR --core nes|snes|genesis [--backend ra|none] [--fps N] [--only-on-change] [--log-every N]\n"
    "\n"
    "Options:\n"
    "  --dev PATH            memtap device path (default: /dev/mmr_memtap)\n"
    "  --mock DIR            mock snapshot directory (enables mock mode)\n"
    "  --core NAME           required in mock mode: nes|snes|genesis\n"
    "  --backend NAME        ra|none (default: ra)\n"
    "  --fps N               evaluation rate (default: 60)\n"
    "  --only-on-change      only evaluate when snapshot changes\n"
    "  --log-every N         log every N frames (0 disables; default: 60)\n"
    "  --ach-file PATH       load achievements from a .ach file (replaces builtins)\n"
    "  --print-config        print resolved config and exit\n"
    "  --version             print version and exit\n"
    "  -h, --help            show help\n",
    MMR_VERSION, argv0, argv0);
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
  const char *ach_file_cli = NULL;

  const char *dev_path = "/dev/mmr_memtap";
  const char *mock_dir = NULL;
  const char *core_str = NULL;
  const char *backend_str = "ra";

  uint32_t fps = 60;
  uint32_t log_every = 60;
  int only_on_change = 0;
  int print_config = 0;
  int dev_explicit = 0;

  for (int i = 1; i < argc; i++) {
    const char *a = argv[i];

    if (strcmp(a, "--ach-file") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "ERROR: --ach-file requires a path\n");
        return 2;
      }
      ach_file_cli = argv[++i];
      continue;
    }

    if (strcmp(a, "--dev") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "ERROR: --dev requires a path\n");
        return 2;
      }
      dev_path = argv[++i];
      dev_explicit = 1;
      continue;
    }

    if (strcmp(a, "--mock") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "ERROR: --mock requires a directory\n");
        return 2;
      }
      mock_dir = argv[++i];
      continue;
    }

    if (strcmp(a, "--core") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "ERROR: --core requires nes|snes|genesis\n");
        return 2;
      }
      core_str = argv[++i];
      continue;
    }

    if (strcmp(a, "--backend") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "ERROR: --backend requires ra|none\n");
        return 2;
      }
      backend_str = argv[++i];
      continue;
    }

    if (strcmp(a, "--fps") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "ERROR: --fps requires a number\n");
        return 2;
      }
      uint32_t v = 0;
      if (!parse_u32(argv[i + 1], &v) || v == 0 || v > 1000) {
        fprintf(stderr, "ERROR: invalid --fps '%s' (1..1000)\n", argv[i + 1]);
        return 2;
      }
      fps = v;
      i++;
      continue;
    }

    if (strcmp(a, "--log-every") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "ERROR: --log-every requires a number\n");
        return 2;
      }
      uint32_t v = 0;
      if (!parse_u32(argv[i + 1], &v)) {
        fprintf(stderr, "ERROR: invalid --log-every '%s'\n", argv[i + 1]);
        return 2;
      }
      log_every = v;
      i++;
      continue;
    }

    if (strcmp(a, "--only-on-change") == 0) {
      only_on_change = 1;
      continue;
    }

    if (strcmp(a, "--print-config") == 0) {
      print_config = 1;
      continue;
    }

    if (strcmp(a, "--version") == 0) {
      printf("%s\n", MMR_VERSION);
      return 0;
    }

    if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
      usage(argv[0]);
      return 0;
    }

    fprintf(stderr, "Unknown arg: %s\n", a);
    usage(argv[0]);
    return 2;
  }

  /* Enforce mode correctness */
  if (mock_dir && dev_explicit) {
    fprintf(stderr, "ERROR: --mock and --dev are mutually exclusive\n");
    return 2;
  }
  if (mock_dir && !core_str) {
    fprintf(stderr, "ERROR: mock mode requires --core nes|snes|genesis\n");
    return 2;
  }

  uint32_t core_id = core_id_from_str(core_str);
  if (mock_dir && core_id == MMR_CORE_UNKNOWN) {
    fprintf(stderr, "ERROR: invalid --core '%s' (use nes|snes|genesis)\n", core_str ? core_str : "");
    return 2;
  }

  engine_backend_t backend = backend_from_str(backend_str);
  if (backend == ENGINE_BACKEND_NONE && backend_str && strcmp(backend_str, "none") != 0) {
    if (strcmp(backend_str, "ra") != 0) {
      fprintf(stderr, "ERROR: invalid --backend '%s' (use ra|none)\n", backend_str);
      return 2;
    }
  }

  const char *ach_path = ach_file_cli;
  if (!ach_path || !*ach_path) ach_path = getenv("MMR_ACH_FILE");

  if (print_config) {
    printf("mmr-daemon config\n");
    printf("  version:        %s\n", MMR_VERSION);
    printf("  mode:           %s\n", mock_dir ? "mock" : "device");
    printf("  dev_path:       %s\n", dev_path ? dev_path : "");
    printf("  mock_dir:       %s\n", mock_dir ? mock_dir : "");
    printf("  core:           %s\n", mock_dir ? (core_str ? core_str : "unknown") : "auto");
    printf("  backend:        %s\n", backend_str_from_id(backend));
    printf("  fps:            %u\n", fps);
    printf("  only_on_change: %s\n", only_on_change ? "yes" : "no");
    printf("  log_every:      %u\n", log_every);
    printf("  ach_file:       %s\n", (ach_path && *ach_path) ? ach_path : "");
    return 0;
  }

  install_signal_handlers();

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

  engine_t *eng = NULL;
  if (!engine_init(&eng, backend, core_id)) {
    fprintf(stderr, "ERR: engine_init failed\n");
    memtap_close(&mt);
    return 1;
  }

  /* Load achievements: file overrides builtins */
  if (ach_path && *ach_path) {
    (void)engine_load_ach_file(eng, ach_path);
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

  fprintf(stdout,
          "[INFO] mmr-daemon started mode=%s core_id=%u(%s) region=%u size=%u fps=%u backend=%s\n",
          mock_dir ? "mock" : "device",
          core_id, core_str_from_id(core_id),
          want_region, size, fps, backend_str_from_id(backend));
  fflush(stdout);

  while (!g_stop) {
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

    if (log_every && (frame - last_logged) >= (uint64_t)log_every) {
      fprintf(stdout, "[INFO] frame=%" PRIu64 " size=%u changed=%s hash=0x%08x\n",
              frame, size, changed ? "yes" : "no", h);
      fflush(stdout);
      last_logged = frame;
    }
  }

  fprintf(stdout, "[INFO] mmr-daemon stopping (signal)\n");
  fflush(stdout);

  free(buf);
  engine_destroy(eng);
  memtap_close(&mt);
  return 0;
}
