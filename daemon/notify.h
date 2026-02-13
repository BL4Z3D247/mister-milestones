#pragma once
#include <stdint.h>

typedef enum {
  NOTIFY_INFO = 0,
  NOTIFY_WARN = 1,
  NOTIFY_ERR  = 2,
} notify_level_t;

void notify(notify_level_t lvl, const char *fmt, ...);
