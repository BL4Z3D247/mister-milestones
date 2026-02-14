#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
  ENGINE_BACKEND_NONE = 0,
  ENGINE_BACKEND_RA   = 1,
} engine_backend_t;

typedef struct engine engine_t;

// Create + init an engine backend.
engine_t* engine_create(engine_backend_t backend);

// Free engine and any resources.
void engine_destroy(engine_t *e);

// Called once after memtap detects the active core/game context.
// For now, game_id is just a placeholder string (we’ll implement real ROM hashing later).
bool engine_set_context(engine_t *e, uint32_t core_id, const char *game_id);

// Called once per frame with the primary RAM snapshot.
void engine_tick(engine_t *e, const uint8_t *primary_ram, uint32_t primary_size, uint64_t frame_counter);

// Returns backend name for logging.
const char* engine_name(engine_t *e);
