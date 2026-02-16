#include "memsrc_memtap.h"
#include "memtap.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
  memtap_t mt;
} memsrc_memtap_impl_t;

static bool ms_open_device(memsrc_t *ms, const char *devnode) {
  memsrc_memtap_impl_t *impl = (memsrc_memtap_impl_t*)ms->impl;
  return memtap_open_device(&impl->mt, devnode);
}

static bool ms_open_mock(memsrc_t *ms, const char *mock_dir, uint32_t core_id) {
  memsrc_memtap_impl_t *impl = (memsrc_memtap_impl_t*)ms->impl;
  return memtap_open_mock(&impl->mt, mock_dir, core_id);
}

static void ms_close(memsrc_t *ms) {
  if (!ms || !ms->impl) return;
  memsrc_memtap_impl_t *impl = (memsrc_memtap_impl_t*)ms->impl;
  memtap_close(&impl->mt);
  free(impl);
  ms->impl = NULL;
}

static bool ms_get_info(memsrc_t *ms, struct mmr_info *out) {
  memsrc_memtap_impl_t *impl = (memsrc_memtap_impl_t*)ms->impl;
  return memtap_get_info(&impl->mt, out);
}

static bool ms_get_regions(memsrc_t *ms, struct mmr_region_desc *out16, uint32_t *out_count) {
  memsrc_memtap_impl_t *impl = (memsrc_memtap_impl_t*)ms->impl;
  return memtap_get_regions(&impl->mt, out16, out_count);
}

static bool ms_select_region(memsrc_t *ms, uint32_t region_id) {
  memsrc_memtap_impl_t *impl = (memsrc_memtap_impl_t*)ms->impl;
  return memtap_select_region(&impl->mt, region_id);
}

static bool ms_seek(memsrc_t *ms, uint32_t offset) {
  memsrc_memtap_impl_t *impl = (memsrc_memtap_impl_t*)ms->impl;
  return memtap_seek(&impl->mt, offset);
}

static ssize_t ms_read(memsrc_t *ms, void *buf, size_t len) {
  memsrc_memtap_impl_t *impl = (memsrc_memtap_impl_t*)ms->impl;
  return memtap_read(&impl->mt, buf, len);
}

static bool ms_wait_frame(memsrc_t *ms, uint64_t last_frame, uint32_t timeout_ms) {
  memsrc_memtap_impl_t *impl = (memsrc_memtap_impl_t*)ms->impl;
  return memtap_wait_frame(&impl->mt, last_frame, timeout_ms);
}

static const memsrc_ops_t g_ops = {
  .open_device   = ms_open_device,
  .open_mock     = ms_open_mock,
  .close         = ms_close,
  .get_info      = ms_get_info,
  .get_regions   = ms_get_regions,
  .select_region = ms_select_region,
  .seek          = ms_seek,
  .read          = ms_read,
  .wait_frame    = ms_wait_frame,
};

void memsrc_init_memtap(memsrc_t *ms) {
  memset(ms, 0, sizeof(*ms));
  ms->ops = &g_ops;
  ms->impl = calloc(1, sizeof(memsrc_memtap_impl_t));
}
