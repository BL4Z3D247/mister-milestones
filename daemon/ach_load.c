#include "ach_load.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static char* mmr_strdup(const char *s) {
  if (!s) return NULL;
  size_t n = strlen(s);
  char *p = (char*)malloc(n + 1);
  if (!p) return NULL;
  memcpy(p, s, n + 1);
  return p;
}

static void rstrip(char *s) {
  if (!s) return;
  size_t n = strlen(s);
  while (n && (s[n-1] == '\n' || s[n-1] == '\r' || isspace((unsigned char)s[n-1]))) {
    s[n-1] = 0;
    n--;
  }
}

static char* lskip(char *s) {
  while (s && *s && isspace((unsigned char)*s)) s++;
  return s;
}

// Format:
// achievement <id> "<title>" <memaddr>
//
// Examples:
// achievement 1 "Entered World 1-1" 0xH00075C=1
// achievement 2 "Stockpile (5 lives)" 0xH00075A=5
// achievement 3 "Counter Hit 5 (coins LE16)" 0xH0007ED=5
static bool parse_line(const char *line_in, mmr_ach_def_t *out) {
  char buf[2048];
  memset(out, 0, sizeof(*out));

  // copy + trim
  strncpy(buf, line_in, sizeof(buf)-1);
  buf[sizeof(buf)-1] = 0;
  rstrip(buf);
  char *s = lskip(buf);

  if (!*s) return false;
  if (*s == '#') return false;
  if (s[0] == '/' && s[1] == '/') return false;

  // must start with "achievement"
  const char *kw = "achievement";
  size_t kwlen = strlen(kw);
  if (strncmp(s, kw, kwlen) != 0 || !isspace((unsigned char)s[kwlen])) return false;
  s = lskip(s + kwlen);

  // parse id
  char *end = NULL;
  unsigned long id = strtoul(s, &end, 10);
  if (end == s) return false;
  s = lskip(end);

  // parse "title"
  if (*s != '"') return false;
  s++;
  char *t0 = s;
  while (*s && *s != '"') s++;
  if (*s != '"') return false;
  *s = 0;
  char *title = t0;
  s++;
  s = lskip(s);

  // remainder is memaddr string
  if (!*s) return false;
  char *memaddr = s;

  out->id = (uint32_t)id;
  out->title = mmr_strdup(title);
  out->memaddr = mmr_strdup(memaddr);
  if (!out->title || !out->memaddr) {
    free(out->title);
    free(out->memaddr);
    memset(out, 0, sizeof(*out));
    return false;
  }
  return true;
}

bool mmr_ach_load_file(const char *path, mmr_ach_list_t *out) {
  memset(out, 0, sizeof(*out));
  FILE *f = fopen(path, "r");
  if (!f) return false;

  mmr_ach_def_t *items = NULL;
  size_t count = 0, cap = 0;

  char line[2048];
  while (fgets(line, sizeof(line), f)) {
    mmr_ach_def_t def;
    if (!parse_line(line, &def)) continue;

    if (count == cap) {
      size_t ncap = cap ? cap * 2 : 16;
      mmr_ach_def_t *nitems = (mmr_ach_def_t*)realloc(items, ncap * sizeof(*nitems));
      if (!nitems) {
        // cleanup
        free(def.title);
        free(def.memaddr);
        fclose(f);
        for (size_t i = 0; i < count; i++) {
          free(items[i].title);
          free(items[i].memaddr);
        }
        free(items);
        return false;
      }
      items = nitems;
      cap = ncap;
    }

    items[count++] = def;
  }

  fclose(f);
  out->items = items;
  out->count = count;
  return true;
}

void mmr_ach_free(mmr_ach_list_t *list) {
  if (!list) return;
  for (size_t i = 0; i < list->count; i++) {
    free(list->items[i].title);
    free(list->items[i].memaddr);
  }
  free(list->items);
  list->items = NULL;
  list->count = 0;
}
