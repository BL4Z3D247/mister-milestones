#include "util.h"
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

uint64_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

void sleep_ms(uint32_t ms) {
  struct timespec ts;
  ts.tv_sec = ms / 1000u;
  ts.tv_nsec = (long)(ms % 1000u) * 1000000l;
  nanosleep(&ts, NULL);
}

bool file_exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0;
}
