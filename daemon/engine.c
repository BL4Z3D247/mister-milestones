#include "engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../third_party/rcheevos/include/rc_runtime.h"

struct engine_s {
  engine_backend_t backend;
  uint32_t core_id;

  rc_runtime_t runtime;

  /* simple logging guard so we don't spam */
  uint32_t triggered_mask;
};

/* ----- rcheevos callbacks ----- */

static uint32_t read_le(const uint8_t *p, uint32_t n) {
  uint32_t v = 0;
  for (uint32_t i = 0; i < n; i++) v |= ((uint32_t)p[i]) << (8u * i);
  return v;
}

static uint32_t RC_CCONV ra_peek(uint32_t address, uint32_t num_bytes, void *ud) {
  /* ud is a pointer to a struct holding the current snapshot buffer */
  const uint8_t *mem = ((const uint8_t*)ud);
  /* We cannot bounds-check without length here, so we keep achievements within RAM size. */
  return read_le(mem + address, num_bytes);
}

static void RC_CCONV ra_event_handler(const rc_runtime_event_t *ev) {
  if (!ev) return;

  if (ev->type == RC_RUNTIME_EVENT_ACHIEVEMENT_TRIGGERED) {
    /* In a real build we'd map id->title. For now just print. */
    printf("[ACH] id=%u triggered\n", ev->id);
    fflush(stdout);
  }
}

/* ----- engine API ----- */

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

/* built-in “SMB1-like” mock achievements for NES CPU RAM
 *
 * Addresses:
 *  - 0x075F: world (0=World 1)
 *  - 0x075C: stage (1=Stage 1)
 *  - 0x075A: lives
 *  - 0x07ED/0x07EE: coins (LE16)
 *
 * NOTE: This is NOT official RetroAchievements content.
 * It is a local simulation to validate the runtime+peek pipeline.
 */
bool engine_load_builtin(engine_t *eng) {
  if (!eng) return false;
  if (eng->backend != ENGINE_BACKEND_RA) return true;

  if (eng->core_id != MMR_CORE_NES) {
    /* For now, only ship the NES mock set */
    return true;
  }

  /* RA “memaddr” strings (condition language).
   *
   * Common pattern in RA memstrings:
   *   0xHADDR=VALUE
   * AND is represented by '_' in many RA contexts.
   *
   * We keep them simple and 8-bit safe.
   */

  struct {
    uint32_t id;
    const char *memaddr;
    const char *name;
  } ach[] = {
    { 1, "0xH075F=0_0xH075C=1", "SMB1: Entered World 1-1" },
    { 2, "0xH075A=5",          "SMB1: Stockpile (5 lives)" },
    { 3, "0xH07ED=5_0xH07EE=0","SMB1: Counter Hit 5 (coins LE16)" },
  };

  for (size_t i = 0; i < sizeof(ach)/sizeof(ach[0]); i++) {
    int rc = rc_runtime_activate_achievement(&eng->runtime, ach[i].id, ach[i].memaddr, NULL, 0);
    if (rc != RC_OK) {
      fprintf(stderr, "[ERR] activate_achievement id=%u failed: %s\n", ach[i].id, rc_error_str(rc));
      return false;
    }
    printf("[INFO] loaded builtin achievement %u: %s\n", ach[i].id, ach[i].name);
  }

  fflush(stdout);
  return true;
}

void engine_do_frame(engine_t *eng, const uint8_t *mem, size_t mem_len) {
  (void)mem_len;

  if (!eng || eng->backend != ENGINE_BACKEND_RA) return;

  /* rcheevos runtime calls peek(address,num_bytes,ud); we pass mem as ud */
  rc_runtime_do_frame(&eng->runtime, ra_event_handler, ra_peek, (void*)mem, NULL);
}

