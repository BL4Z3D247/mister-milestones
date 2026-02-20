#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../kernel/mmr_memtap.h"

typedef enum {
  ENGINE_BACKEND_NONE = 0,
  ENGINE_BACKEND_RA   = 1,
} engine_backend_t;

typedef struct engine_s engine_t;

/* init/shutdown */
bool engine_init(engine_t **out, engine_backend_t backend, uint32_t core_id);
void engine_destroy(engine_t *eng);

/* load achievements */
bool engine_load_builtin(engine_t *eng);
bool engine_load_ach_file(engine_t *eng, const char *path);

/* per-frame evaluation */
void engine_do_frame(engine_t *eng, const uint8_t *mem, size_t mem_len);
