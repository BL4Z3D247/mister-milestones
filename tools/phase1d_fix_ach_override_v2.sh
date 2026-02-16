#!/data/data/com.termux/files/usr/bin/sh
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
echo "[phase1d-fix-v2] root=$ROOT"

# Must be on phase1d branch
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

# Ensure includes for getenv + bool
def ensure_include(src: str, inc: str) -> str:
    if re.search(r'^\s*#include\s+' + re.escape(inc) + r'\s*$', src, re.M):
        return src
    m = re.search(r'^\s*#include\s+"engine\.h"\s*\n', src, re.M)
    if m:
        i = m.end()
        return src[:i] + f'#include {inc}\n' + src[i:]
    m2 = re.search(r'^\s*#include\s*[<"].+[>"]\s*\n', src, re.M)
    if m2:
        i = m2.end()
        return src[:i] + f'#include {inc}\n' + src[i:]
    return f'#include {inc}\n' + src

s = ensure_include(s, "<stdlib.h>")      # getenv
s = ensure_include(s, "<stdbool.h>")     # bool
s = ensure_include(s, '"ach_load.h"')    # mmr_ach_load_file/mmr_ach_free

# 1) Make/ensure engine_try_load_ach_file exists AND returns bool.
# If it already exists as void, convert signature to bool.
if "engine_try_load_ach_file" in s:
    s = s.replace("static void engine_try_load_ach_file(engine_t* eng)", "static bool engine_try_load_ach_file(engine_t* eng)")
    s = s.replace("static void engine_try_load_ach_file(engine_t *eng)", "static bool engine_try_load_ach_file(engine_t *eng)")
else:
    # If missing entirely, insert a fresh helper before engine_init (or before first bool engine_init)
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
    m = re.search(r'\bbool\s+engine_init\s*\(', s)
    if not m:
        raise SystemExit("ERROR: could not find engine_init to anchor helper insert")
    s = s[:m.start()] + helper + s[m.start():]

# Now patch the helper body to guarantee it returns true/false correctly (even if older version existed)
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

# Convert any bare 'return;' inside helper to 'return false;'
helper = re.sub(r'(\n\s*)return;\s*', r'\1return false;\n', helper)

# Ensure the "no path" branch returns false
helper = helper.replace("if (!path || !*path) return;", "if (!path || !*path) return false;")

# Ensure helper ends with return true
if "return true;" not in helper:
    helper = re.sub(r'\n\}\s*$', "\n  return true;\n}\n", helper)

s = s[:m.start()] + helper + s[end:]

# 2) Patch engine_load_builtin(s) to short-circuit when file achievements load.
# We look for a function named engine_load_builtin or engine_load_builtins.
m2 = re.search(r'\bstatic\s+(bool|void)\s+(engine_load_builtins?|engine_load_builtin)\s*\(\s*engine_t\s*\*\s*eng\s*\)\s*\{', s)
if not m2:
    raise SystemExit("ERROR: could not find engine_load_builtin(s) function in engine.c")

ret_type = m2.group(1)
fn_name = m2.group(2)

open_brace = s.find("{", m2.start())
insert_at = open_brace + 1

guard = "\n  // Phase 1D: if an .ach file is provided and loads, do NOT load builtins.\n"
if ret_type == "bool":
    guard += "  if (engine_try_load_ach_file(eng)) return true;\n"
else:
    guard += "  if (engine_try_load_ach_file(eng)) return;\n"

# Only insert once
if "if (engine_try_load_ach_file(eng))" not in s[m2.start():m2.start()+600]:
    s = s[:insert_at] + guard + s[insert_at:]

p.write_text(s)
print("[ok] engine.c patched: file achievements override builtins (no call-site dependency)")
PY

# Rebuild
cd "$ROOT/daemon"
make clean
make -j
echo "[phase1d-fix-v2] build OK"
