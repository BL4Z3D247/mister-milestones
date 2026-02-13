#pragma once
#include <stdbool.h>
#include <stdint.h>

uint64_t now_ms(void);
void sleep_ms(uint32_t ms);
bool file_exists(const char *path);
