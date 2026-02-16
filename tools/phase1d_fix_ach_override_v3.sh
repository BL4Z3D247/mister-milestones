#!/data/data/com.termux/files/usr/bin/sh
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
echo "[phase1d-fix-v3] root=$ROOT"

BR_EXPECT="phase1d-ach-integration"
BR_NOW="$(git rev-parse --abbrev-ref HEAD)"
if [ "$BR_NOW" != "$BR_EXPECT" ]; then
  echo "ERROR: you are on '$BR_NOW' but expected '$BR_EXPECT'"
  exit 1
fi

python - <<'PY'
import re
from pathlib import Path

p = Path("daemon/engine.c")
s = p.read_text()

# Ensure needed includes
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

s = ensure_include(s, "<stdlib.h>")      # getenv
s = ensure_include(s, "<stdbool.h>")     # bool
s = ensure_include(s, '"ach_load.h"')    # mmr_ach_load_file/mmr_ach_free

# Normalize/ensure engine_try_load_ach_file exists and returns bool
if "engine_try_load_ach_file" in s:
    s = s.replace("static void engine_try_load_ach_file(engine_t* eng)", "static bool engine_try_load_ach_file(engine_t* eng)")
    s = s.replace("static void engine_try_load_ach_file(engine_t *eng)", "static bool engine_try_load_ach_file(engine_t *eng)")
else:
    helper = r'''

// Phase 1D: load achievements from a .ach file if MMR_ACH_FILE is set.
// Returns true if it successfully replaced the active achievements.
static bool engine_try_load_ach_file(engine_t* eng) {
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
  return true;
}
'''
    m = re.search(r'\bengine_init\s*\(', s)
    if not m:
        raise SystemExit("ERROR: could not find engine_init to anchor helper insert")
    s = s[:m.start()] + helper + s[m.start():]

# Patch helper returns defensively
m = re.search(r'static\s+bool\s+engine_try_load_ach_file\s*\(\s*engine_t\s*\*\s*eng\s*\)\s*\{', s)
if not m:
    raise SystemExit("ERROR: engine_try_load_ach_file not found after normalization")

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
    raise SystemExit("ERROR: could not find end of engine_try_load_ach_file")

helper = s[m.start():end]
helper = re.sub(r'(\n\s*)return;\s*', r'\1return false;\n', helper)
helper = helper.replace("if (!path || !*path) return;", "if (!path || !*path) return false;")
if "return true;" not in helper:
    helper = re.sub(r'\n\}\s*$', "\n  return true;\n}\n", helper)

s = s[:m.start()] + helper + s[end:]

# Find the builtin loader function by locating the builtin log string
needle = "loaded builtin achievement"
idx = s.find(needle)
if idx == -1:
    raise SystemExit("ERROR: could not find builtin achievement log string in daemon/engine.c")

# Find the nearest function start before idx
func_re = re.compile(r'^\s*(static\s+)?(bool|void|int)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\([^;]*\)\s*\{', re.M)
starts = [m for m in func_re.finditer(s) if m.start() < idx]
if not starts:
    raise SystemExit("ERROR: could not locate enclosing function for builtin loader")

fm = starts[-1]
ret_type = fm.group(2)
fn_name = fm.group(3)
open_brace = s.find("{", fm.start())
insert_at = open_brace + 1

# Only insert if not already present in first ~300 chars of the function
window = s[open_brace:open_brace+600]
if "engine_try_load_ach_file(eng)" not in window:
    guard = "\n  // Phase 1D: if an .ach file is provided and loads, do NOT load builtins.\n"
    if ret_type == "bool":
        guard += "  if (engine_try_load_ach_file(eng)) return true;\n"
    else:
        guard += "  if (engine_try_load_ach_file(eng)) return;\n"
    s = s[:insert_at] + guard + s[insert_at:]

p.write_text(s)
print(f"[ok] patched builtin loader function '{fn_name}' (return type {ret_type}) to skip when .ach loads")
PY

cd daemon
make clean
make -j
echo "[phase1d-fix-v3] build OK"
