#include "notify.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

static const char* lvl_str(notify_level_t lvl) {
  switch (lvl) {
    case NOTIFY_INFO: return "INFO";
    case NOTIFY_WARN: return "WARN";
    case NOTIFY_ERR:  return "ERR";
    default: return "?";
  }
}

void notify(notify_level_t lvl, const char *fmt, ...) {
  time_t t = time(NULL);
  struct tm tmv;
  localtime_r(&t, &tmv);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);

  fprintf(stderr, "[%s] %s: ", buf, lvl_str(lvl));
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
}
