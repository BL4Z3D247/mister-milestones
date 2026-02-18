#!/data/data/com.termux/files/usr/bin/sh
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
echo "[phase1e] root=$ROOT"

BR_EXPECT="phase1e-stability"
BR_NOW="$(git rev-parse --abbrev-ref HEAD)"
if [ "$BR_NOW" != "$BR_EXPECT" ]; then
  echo "ERROR: you are on '$BR_NOW' but expected '$BR_EXPECT'"
  exit 1
fi

python - <<'PY'
from pathlib import Path
import re

p = Path("daemon/engine.c")
s = p.read_text()

def ensure_include(src: str, inc_line: str) -> str:
    # inc_line should look like <stdlib.h> or "ach_load.h"
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

# Ensure needed includes for getenv/setenv/bool
s = ensure_include(s, "<stdlib.h>")
s = ensure_include(s, "<stdbool.h>")
s = ensure_include(s, '"ach_load.h"')

# Normalize helper signature to bool if present as void
s = s.replace("static void engine_try_load_ach_file(engine_t* eng)",
              "static bool engine_try_load_ach_file(engine_t* eng)")
s = s.replace("static void engine_try_load_ach_file(engine_t *eng)",
              "static bool engine_try_load_ach_file(engine_t *eng)")

# Locate helper
m = re.search(r'static\s+bool\s+engine_try_load_ach_file\s*\(\s*engine_t\s*\*\s*eng\s*\)\s*\{', s)
if not m:
    raise SystemExit("ERROR: engine_try_load_ach_file helper not found in daemon/engine.c (Phase 1D must exist)")

# Extract helper block by brace matching
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

helper = s[m.start():end]

# 1) Make helper idempotent: if already loaded, return true immediately (no logs)
if 'getenv("MMR_ACH_LOADED")' not in helper:
    helper = re.sub(
        r'\{\s*',
        '{\n  const char* already = getenv("MMR_ACH_LOADED");\n  if (already && already[0] == \'1\') return true;\n\n  ',
        helper,
        count=1
    )

# 2) Ensure "no path" returns false
helper = helper.replace("if (!path || !*path) return;", "if (!path || !*path) return false;")

# 3) Convert any bare `return;` to `return false;` inside helper
helper = re.sub(r'(\n\s*)return;\s*', r'\1return false;\n', helper)

# 4) On successful load, set MMR_ACH_LOADED=1 once (so later calls are no-ops)
# Insert just before the final return true (or before closing brace if needed)
if 'setenv("MMR_ACH_LOADED"' not in helper:
    if "return true;" in helper:
        helper = helper.replace("return true;", 'setenv("MMR_ACH_LOADED", "1", 1);\n  return true;', 1)
    else:
        helper = re.sub(r'\n\}\s*$',
                        '\n  setenv("MMR_ACH_LOADED", "1", 1);\n  return true;\n}\n',
                        helper)

# Reinsert patched helper
s = s[:m.start()] + helper + s[end:]

# Now: add a guard to the builtin loader function (so builtins don't load if file loaded)
# We find the function that prints "loaded builtin achievement" and inject at top:
needle = "loaded builtin achievement"
idx = s.find(needle)
if idx == -1:
    raise SystemExit("ERROR: could not find builtin achievement log string in daemon/engine.c")

func_re = re.compile(r'^\s*(static\s+)?(bool|void|int)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\([^;]*\)\s*\{', re.M)
starts = [mm for mm in func_re.finditer(s) if mm.start() < idx]
if not starts:
    raise SystemExit("ERROR: could not locate enclosing function for builtin loader")
fm = starts[-1]
ret_type = fm.group(2)
open_br = s.find("{", fm.start())
insert_at = open_br + 1
window = s[open_br:open_br+900]

guard = "\n  // Phase 1E: if an .ach file is provided and loads, skip builtin activations.\n"
if ret_type == "bool":
    guard += "  if (engine_try_load_ach_file(eng)) return true;\n"
else:
    guard += "  if (engine_try_load_ach_file(eng)) return;\n"

# Insert only once
if "Phase 1E: if an .ach file is provided" not in window:
    s = s[:insert_at] + guard + s[insert_at:]

p.write_text(s)
print("[ok] engine.c patched: idempotent file load + builtin skip guard")
PY

cd "$ROOT/daemon"
make clean
make -j
echo "[phase1e] build OK"
