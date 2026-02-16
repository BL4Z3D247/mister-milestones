#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>   // ssize_t

#include "../kernel/mmr_memtap.h"

typedef struct memsrc memsrc_t;

typedef struct {
  bool   (*open_device)(memsrc_t *ms, const char *devnode);
  bool   (*open_mock)(memsrc_t *ms, const char *mock_dir, uint32_t core_id);
  void   (*close)(memsrc_t *ms);

  bool   (*get_info)(memsrc_t *ms, struct mmr_info *out);
  bool   (*get_regions)(memsrc_t *ms, struct mmr_region_desc *out16, uint32_t *out_count);

  bool   (*select_region)(memsrc_t *ms, uint32_t region_id);
  bool   (*seek)(memsrc_t *ms, uint32_t offset);
  ssize_t(*read)(memsrc_t *ms, void *buf, size_t len);

  bool   (*wait_frame)(memsrc_t *ms, uint64_t last_frame, uint32_t timeout_ms);
} memsrc_ops_t;

struct memsrc {
  const memsrc_ops_t *ops;
  void *impl; /* provider-owned state */
};

/* helpers */
static inline bool memsrc_open_device(memsrc_t *ms, const char *devnode) { return ms->ops->open_device(ms, devnode); }
static inline bool memsrc_open_mock(memsrc_t *ms, const char *mock_dir, uint32_t core_id) { return ms->ops->open_mock(ms, mock_dir, core_id); }
static inline void memsrc_close(memsrc_t *ms) { ms->ops->close(ms); }

static inline bool memsrc_get_info(memsrc_t *ms, struct mmr_info *out) { return ms->ops->get_info(ms, out); }
static inline bool memsrc_get_regions(memsrc_t *ms, struct mmr_region_desc *out16, uint32_t *out_count) { return ms->ops->get_regions(ms, out16, out_count); }

static inline bool memsrc_select_region(memsrc_t *ms, uint32_t region_id) { return ms->ops->select_region(ms, region_id); }
static inline bool memsrc_seek(memsrc_t *ms, uint32_t offset) { return ms->ops->seek(ms, offset); }
static inline ssize_t memsrc_read(memsrc_t *ms, void *buf, size_t len) { return ms->ops->read(ms, buf, len); }

static inline bool memsrc_wait_frame(memsrc_t *ms, uint64_t last_frame, uint32_t timeout_ms) { return ms->ops->wait_frame(ms, last_frame, timeout_ms); }
