#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "../kernel/mmr_memtap.h"

typedef enum {
  MEMTAP_BACKEND_DEVICE = 0,
  MEMTAP_BACKEND_MOCK   = 1,
} memtap_backend_t;

typedef struct {
  memtap_backend_t backend;

  // device mode
  int fd;
  uint32_t selected_region;
  uint32_t seek_offset;

  // mock mode
  char mock_dir[512];
  uint32_t mock_core_id;
  uint64_t mock_frame_counter;
  uint32_t mock_region_count;
  struct mmr_region_desc mock_regions[16];
} memtap_t;

bool memtap_open_device(memtap_t *mt, const char *devnode);
bool memtap_open_mock(memtap_t *mt, const char *mock_dir, uint32_t core_id);

void memtap_close(memtap_t *mt);

bool memtap_get_info(memtap_t *mt, struct mmr_info *out);
bool memtap_get_regions(memtap_t *mt, struct mmr_region_desc *out16, uint32_t *out_count);

bool memtap_select_region(memtap_t *mt, uint32_t region_id);
bool memtap_seek(memtap_t *mt, uint32_t offset);

ssize_t memtap_read(memtap_t *mt, void *buf, size_t len);

bool memtap_wait_frame(memtap_t *mt, uint64_t last_frame, uint32_t timeout_ms);
