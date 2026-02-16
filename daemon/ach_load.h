#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {
  uint32_t id;
  char *title;     // heap allocated
  char *memaddr;   // heap allocated (rcheevos memaddr string)
} mmr_ach_def_t;

typedef struct {
  mmr_ach_def_t *items;
  size_t count;
} mmr_ach_list_t;

// Loads a simple .ach text file (see achievements/smb1_demo.ach for format).
// Returns true on success, false on failure.
bool mmr_ach_load_file(const char *path, mmr_ach_list_t *out);

// Frees all heap allocations in a list.
void mmr_ach_free(mmr_ach_list_t *list);
