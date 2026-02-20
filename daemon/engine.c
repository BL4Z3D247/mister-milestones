#include "engine.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ach_load.h"
#include "../third_party/rcheevos/include/rc_runtime.h"

/* ----- rcheevos callbacks ----- */

typedef struct {
  const uint8_t *mem;
  size_t mem_len;
} ra_ctx_t;

static uint32_t read_le_safe(const uint8_t *p, size_t avail, uint32_t n) {
  uint32_t v = 0;
  /* if not enough bytes, return 0 (safe default). */
  if (avail < (size_t)n) return 0;
  for (uint32_t i = 0; i < n; i++) v |= ((uint32_t)p[i]) << (8u * i);
  return v;
}

static uint32_t RC_CCONV ra_peek(uint32_t address, uint32_t num_bytes, void *ud) {
  const ra_ctx_t *ctx = (const ra_ctx_t*)ud;
  if (!ctx || !ctx->mem) return 0;

  /* bounds check to prevent segfaults if an achievement reads beyond snapshot */
  if ((size_t)address >= ctx->mem_len) return 0;
  size_t avail = ctx->mem_len - (size_t)address;

  return read_le_safe(ctx->mem + address, avail, num_bytes);
}

static void RC_CCONV ra_event_handler(const rc_runtime_event_t *ev) {
  if (!ev) return;

  if (ev->type == RC_RUNTIME_EVENT_ACHIEVEMENT_TRIGGERED) {
    /* In a real build we'd map id->title. For now just print. */
    printf("[ACH] id=%u triggered\n", ev->id);
    fflush(stdout);
  }
}

/* ----- engine implementation ----- */

struct engine_s {
  engine_backend_t backend;
  uint32_t core_id;

  rc_runtime_t runtime;

  bool builtins_loaded;
  bool file_loaded;
};

bool engine_init(engine_t **out, engine_backend_t backend, uint32_t core_id) {
  if (!out) return false;

  engine_t *eng = (engine_t*)calloc(1, sizeof(engine_t));
  if (!eng) return false;

  eng->backend = backend;
  eng->core_id = core_id;

  if (backend == ENGINE_BACKEND_RA) {
    rc_runtime_init(&eng->runtime);
  }

  *out = eng;
  return true;
}

void engine_destroy(engine_t *eng) {
  if (!eng) return;

  if (eng->backend == ENGINE_BACKEND_RA) {
    rc_runtime_destroy(&eng->runtime);
  }

  free(eng);
}

bool engine_load_ach_file(engine_t *eng, const char *path) {
  if (!eng) return false;
  if (eng->backend != ENGINE_BACKEND_RA) return true; /* nothing to do */
  if (!path || !*path) return false;

  mmr_ach_list_t list;
  if (!mmr_ach_load_file(path, &list)) {
    fprintf(stderr, "[WARN] could not load ach file: %s (fallback to builtins)\n", path);
    return false;
  }

  if (list.count == 0) {
    fprintf(stderr, "[WARN] ach file loaded but empty: %s (fallback to builtins)\n", path);
    mmr_ach_free(&list);
    return false;
  }

  /* replace whatever is active in the runtime with the file set */
  rc_runtime_reset(&eng->runtime);

  size_t ok_count = 0;
  for (size_t j = 0; j < list.count; j++) {
    mmr_ach_def_t *a = &list.items[j];
    int rc = rc_runtime_activate_achievement(&eng->runtime, a->id, a->memaddr, NULL, 0);
    if (rc == RC_OK) {
      ok_count++;
      fprintf(stderr, "[INFO] loaded file achievement %u: %s\n", a->id, a->title);
    } else {
      fprintf(stderr, "[WARN] failed to activate achievement %u from file (%s)\n",
              a->id, rc_error_str(rc));
    }
  }

  mmr_ach_free(&list);

  if (ok_count == 0) {
    fprintf(stderr, "[WARN] ach file had entries but none activated: %s (fallback to builtins)\n", path);
    return false;
  }

  eng->file_loaded = true;
  eng->builtins_loaded = false;
  return true;
}

/* built-in “SMB1-like” mock achievements for NES CPU RAM
 *
 * NOTE: This is NOT official RetroAchievements content.
 * It is a local simulation to validate the runtime+peek pipeline.
 */
bool engine_load_builtin(engine_t *eng) {
  if (!eng) return false;
  if (eng->backend != ENGINE_BACKEND_RA) return true;

  /* Never load builtins if a file set was loaded */
  if (eng->file_loaded) return true;

  /* Never load builtins more than once */
  if (eng->builtins_loaded) return true;
  eng->builtins_loaded = true;

  if (eng->core_id != MMR_CORE_NES) {
    /* For now, only ship the NES mock set */
    return true;
  }

  struct {
    uint32_t id;
    const char *memaddr;
    const char *name;
  } ach[] = {
    { 1, "0xH075F=0_0xH075C=1", "SMB1: Entered World 1-1" },
    { 2, "0xH075A=5",          "SMB1: Stockpile (5 lives)" },
    { 3, "0xH07ED=5_0xH07EE=0","SMB1: Counter Hit 5 (coins LE16)" },
  };

  for (size_t i = 0; i < sizeof(ach) / sizeof(ach[0]); i++) {
    int rc = rc_runtime_activate_achievement(&eng->runtime, ach[i].id, ach[i].memaddr, NULL, 0);
    if (rc != RC_OK) {
      fprintf(stderr, "[ERR] activate_achievement id=%u failed: %s\n",
              ach[i].id, rc_error_str(rc));
      return false;
    }
    printf("[INFO] loaded builtin achievement %u: %s\n", ach[i].id, ach[i].name);
  }

  fflush(stdout);
  return true;
}

void engine_do_frame(engine_t *eng, const uint8_t *mem, size_t mem_len) {
  if (!eng || eng->backend != ENGINE_BACKEND_RA) return;
  if (!mem || mem_len == 0) return;

  ra_ctx_t ctx;
  ctx.mem = mem;
  ctx.mem_len = mem_len;

  rc_runtime_do_frame(&eng->runtime, ra_event_handler, ra_peek, (void*)&ctx, NULL);
}
