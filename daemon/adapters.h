#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "../kernel/mmr_memtap.h"

typedef struct {
  uint32_t core_id;
  uint32_t primary_region;  // the region we bulk-read each frame
  uint32_t primary_size;    // bytes
} adapter_desc_t;

bool adapter_get(uint32_t core_id, adapter_desc_t *out);

// Translate an achievement runtime address to an offset within the primary region.
// Returns false if address not supported in MVP mapping.
bool adapter_translate(uint32_t core_id, uint32_t addr, uint32_t *out_offset);
