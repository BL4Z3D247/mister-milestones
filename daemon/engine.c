#include "engine.h"
#include "notify.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#include "rc_runtime.h"
#include "rc_error.h"

typedef struct {
  uint32_t core_id;
  char game_id[128];
  bool has_context;

  rc_runtime_t runtime;

  const uint8_t *ram;
  uint32_t ram_size;

  uint64_t peek_calls;

  struct {
    uint32_t id;
    const char *name;
  } names[8];
  uint32_t name_count;
} engine_ra_t;

struct engine {
  engine_backend_t backend;
  engine_ra_t ra;
};

static void ra_add_name(engine_ra_t *ra, uint32_t id, const char *name) {
  if (!ra) return;
  if (ra->name_count >= (sizeof(ra->names) / sizeof(ra->names[0]))) return;
  ra->names[ra->name_count].id = id;
  ra->names[ra->name_count].name = name;
  ra->name_count++;
}

static const char* ra_name_for(engine_ra_t *ra, uint32_t id) {
  if (!ra) return "Achievement";
  for (uint32_t i = 0; i < ra->name_count; i++) {
    if (ra->names[i].id == id) return ra->names[i].name;
  }
  return "Achievement";
}

static uint32_t RC_CCONV peek_cb(uint32_t address, uint32_t num_bytes, void* ud) {
  engine_ra_t *ra = (engine_ra_t*)ud;
  if (!ra || !ra->ram || num_bytes == 0) return 0;

  ra->peek_calls++;

  if (address >= ra->ram_size) return 0;
  if (address + num_bytes > ra->ram_size)
    num_bytes = ra->ram_size - address;

  uint32_t value = 0;
  for (uint32_t i = 0; i < num_bytes; i++)
    value |= ((uint32_t)ra->ram[address + i]) << (8u * i);

  return value;
}

/*
 * Your rcheevos build expects:
 *   rc_runtime_event_handler_t(event) with no userdata param.
 * mmr-daemon is single-threaded, so stash the active engine context
 * around rc_runtime_do_frame().
 */
static engine_ra_t *g_ev_ra = NULL;

static void RC_CCONV event_handler(const rc_runtime_event_t* ev) {
  engine_ra_t *ra = g_ev_ra;
  if (!ev || !ra) return;

  if (ev->type == RC_RUNTIME_EVENT_ACHIEVEMENT_TRIGGERED) {
    notify(NOTIFY_INFO, "ACHIEVEMENT UNLOCKED: %s (id=%u)", ra_name_for(ra, ev->id), ev->id);
  }
}

engine_t* engine_create(engine_backend_t backend) {
  engine_t *e = (engine_t*)calloc(1, sizeof(engine_t));
  if (!e) return NULL;

  e->backend = backend;

  if (backend == ENGINE_BACKEND_RA) {
    rc_runtime_init(&e->ra.runtime);
    e->ra.name_count = 0;
    e->ra.has_context = false;
    e->ra.ram = NULL;
    e->ra.ram_size = 0;
    e->ra.peek_calls = 0;
  }

  return e;
}

void engine_destroy(engine_t *e) {
  if (!e) return;

  if (e->backend == ENGINE_BACKEND_RA) {
    rc_runtime_destroy(&e->ra.runtime);
  }

  free(e);
}

const char* engine_name(engine_t *e) {
  if (!e) return "null";
  switch (e->backend) {
    case ENGINE_BACKEND_RA: return "RetroAchievements (rcheevos runtime)";
    default: return "none";
  }
}

bool engine_set_context(engine_t *e, uint32_t core_id, const char *game_id) {
  if (!e) return false;
  if (e->backend != ENGINE_BACKEND_RA) return false;

  engine_ra_t *ra = &e->ra;
  ra->core_id = core_id;
  snprintf(ra->game_id, sizeof(ra->game_id), "%s", game_id ? game_id : "unknown");
  ra->has_context = true;

  rc_runtime_reset(&ra->runtime);
  ra->name_count = 0;
  ra->peek_calls = 0;

  /*
   * Offline self-test achievements:
   *  - ID 1: "0xH0000=1" (RA-style H memory space)
   *  - ID 2: "0x0000=1"  (fallback)
   */
  int ret1 = rc_runtime_activate_achievement(&ra->runtime, 1, "0xH0000=1", NULL, 0);
  if (ret1 != RC_OK) {
    notify(NOTIFY_ERR, "rc_runtime_activate_achievement(1) failed: %s", rc_error_str(ret1));
  } else {
    ra_add_name(ra, 1, "MMR Self-Test A: RAM[0] == 1 (0xH0000=1)");
  }

  int ret2 = rc_runtime_activate_achievement(&ra->runtime, 2, "0x0000=1", NULL, 0);
  if (ret2 != RC_OK) {
    notify(NOTIFY_ERR, "rc_runtime_activate_achievement(2) failed: %s", rc_error_str(ret2));
  } else {
    ra_add_name(ra, 2, "MMR Self-Test B: RAM[0] == 1 (0x0000=1)");
  }

  if (ret1 != RC_OK && ret2 != RC_OK) return false;

  notify(NOTIFY_INFO,
         "engine context set: backend=%s core_id=%u game_id=%s (self-test loaded)",
         engine_name(e), core_id, ra->game_id);

  return true;
}

void engine_tick(engine_t *e, const uint8_t *primary_ram, uint32_t primary_size, uint64_t frame_counter) {
  if (!e) return;
  if (e->backend != ENGINE_BACKEND_RA) return;

  engine_ra_t *ra = &e->ra;
  if (!ra->has_context) return;

  ra->ram = primary_ram;
  ra->ram_size = primary_size;

  g_ev_ra = ra;
  rc_runtime_do_frame(&ra->runtime, event_handler, peek_cb, ra, NULL);
  g_ev_ra = NULL;

  if (frame_counter % 120 == 0) {
    uint32_t b0 = (ra->ram && ra->ram_size > 0) ? ra->ram[0] : 0;
    notify(NOTIFY_INFO, "debug: frame=%" PRIu64 " ram[0]=%u peek_calls=%" PRIu64, frame_counter, b0, ra->peek_calls);
  }
}
