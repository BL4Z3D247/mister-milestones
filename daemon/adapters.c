#include "adapters.h"
#include "notify.h"

bool adapter_get(uint32_t core_id, adapter_desc_t *out) {
  if (!out) return false;
  switch (core_id) {
    case MMR_CORE_NES:
      *out = (adapter_desc_t){ .core_id = core_id, .primary_region = MMR_REGION_NES_CPU_RAM, .primary_size = 0x0800 };
      return true;
    case MMR_CORE_SNES:
      *out = (adapter_desc_t){ .core_id = core_id, .primary_region = MMR_REGION_SNES_WRAM, .primary_size = 0x20000 };
      return true;
    case MMR_CORE_GENESIS:
      *out = (adapter_desc_t){ .core_id = core_id, .primary_region = MMR_REGION_GEN_68K_RAM, .primary_size = 0x10000 };
      return true;
    default:
      notify(NOTIFY_ERR, "adapter_get: unsupported core_id=%u", core_id);
      return false;
  }
}

bool adapter_translate(uint32_t core_id, uint32_t addr, uint32_t *out_offset) {
  if (!out_offset) return false;

  switch (core_id) {
    case MMR_CORE_NES:
      if (addr <= 0x1FFFu) {
        *out_offset = addr & 0x07FFu;
        return true;
      }
      return false;

    case MMR_CORE_GENESIS:
      if (addr < 0x10000u) {
        *out_offset = addr;
        return true;
      }
      return false;

    case MMR_CORE_SNES:
      if (addr < 0x20000u) {
        *out_offset = addr;
        return true;
      }
      return false;

    default:
      return false;
  }
}
