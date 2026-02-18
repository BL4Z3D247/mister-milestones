#!/data/data/com.termux/files/usr/bin/sh
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
echo "[phase1e-force-replace-ach-loader] root=$ROOT"

python - <<'PY'
from pathlib import Path
import re

p = Path("daemon/engine.c")
s = p.read_text()

# Ensure required includes (getenv/setenv/bool)
def ensure_include(src: str, inc_line: str) -> str:
    if re.search(r'^\s*#include\s+' + re.escape(inc_line) + r'\s*$', src, re.M):
        return src
    m = re.search(r'^\s*#include\s+"engine\.h"\s*\n', src, re.M)
    if m:
        i = m.end()
        return src[:i] + f'#include {inc_line}\n' + src[i:]
    m2 = re.search(r'^\s*#include\s*[<"].+[>"]\s*\n', src, re.M)
    if m2:
        i = m2.end()
        return src[:i] + f'#include {inc_line}\n' + src[i:]
    return f'#include {inc_line}\n' + src

s = ensure_include(s, "<stdlib.h>")
s = ensure_include(s, "<stdbool.h>")
s = ensure_include(s, '"ach_load.h"')

# Locate the function start
m = re.search(r'^\s*static\s+(?:bool|void)\s+engine_try_load_ach_file\s*\(\s*engine_t\s*\*\s*eng\s*\)\s*\{', s, re.M)
if not m:
    raise SystemExit("ERROR: could not find engine_try_load_ach_file(...) function start")

# Find end of function via brace matching
brace_i = s.find("{", m.start())
depth = 0
end = None
for j in range(brace_i, len(s)):
    c = s[j]
    if c == "{":
        depth += 1
    elif c == "}":
        depth -= 1
        if depth == 0:
            end = j + 1
            break
if end is None:
    raise SystemExit("ERROR: could not find end of engine_try_load_ach_file block")

replacement = r'''
// Phase 1E: load achievements from a .ach file if MMR_ACH_FILE is set.
// Returns true iff it successfully replaced the active achievements.
static bool engine_try_load_ach_file(engine_t* eng) {
  const char* already = getenv("MMR_ACH_LOADED");
  if (already && already[0] == '1') return true;

  const char* path = getenv("MMR_ACH_FILE");
  if (!path || !*path) return false;

  mmr_ach_list_t list;
  if (!mmr_ach_load_file(path, &list)) {
    fprintf(stderr, "[WARN] could not load ach file: %s (using builtins)\n", path);
    return false;
  }

  if (list.count == 0) {
    fprintf(stderr, "[WARN] ach file loaded but empty: %s (using builtins)\n", path);
    mmr_ach_free(&list);
    return false;
  }

  // Replace whatever is active in the runtime with the file set.
  rc_runtime_reset(&eng->runtime);
  for (size_t j = 0; j < list.count; j++) {
    mmr_ach_def_t* a = &list.items[j];
    int rc = rc_runtime_activate_achievement(&eng->runtime, a->id, a->memaddr, NULL, 0);
    if (rc == 0) {
      fprintf(stderr, "[INFO] loaded file achievement %u: %s\n", a->id, a->title);
    } else {
      fprintf(stderr, "[WARN] failed to activate achievement %u from file\n", a->id);
    }
  }

  mmr_ach_free(&list);
  setenv("MMR_ACH_LOADED", "1", 1);
  return true;
}
'''.lstrip("\n")

s = s[:m.start()] + replacement + s[end:]

# Remove any stray "Phase 1E guard" inside engine_load_builtin (engine_init should own gating)
s = re.sub(
    r'^\s*// Phase 1E: if an \.ach file is provided and loads, skip builtin activations\.\s*\n'
    r'\s*if\s*\(\s*engine_try_load_ach_file\s*\(\s*eng\s*\)\s*\)\s*return\s+true;\s*\n',
    '',
    s,
    flags=re.M
)

p.write_text(s)
print("[ok] replaced engine_try_load_ach_file() with known-good implementation")
PY

cd daemon
make clean
make -j
echo "[phase1e-force-replace-ach-loader] build OK"
