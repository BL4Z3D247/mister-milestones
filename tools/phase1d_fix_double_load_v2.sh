#!/data/data/com.termux/files/usr/bin/sh
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
echo "[phase1d-fix-double-v2] root=$ROOT"

BR_EXPECT="phase1d-ach-integration"
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

needle = "loaded builtin achievement"
idx = s.find(needle)
if idx == -1:
    raise SystemExit("ERROR: could not find 'loaded builtin achievement' in daemon/engine.c")

# Find the nearest function header before the needle
func_re = re.compile(
    r'^\s*(static\s+)?(bool|void|int)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\([^;]*\)\s*\{',
    re.M
)
starts = [m for m in func_re.finditer(s) if m.start() < idx]
if not starts:
    raise SystemExit("ERROR: could not find enclosing function for builtin loader")

fm = starts[-1]
fn_name = fm.group(3)
open_brace = s.find("{", fm.start())
if open_brace == -1:
    raise SystemExit("ERROR: internal: no '{' for function")

# Find end of function by brace counting
depth = 0
end_fn = None
for j in range(open_brace, len(s)):
    c = s[j]
    if c == "{":
        depth += 1
    elif c == "}":
        depth -= 1
        if depth == 0:
            end_fn = j + 1
            break

if end_fn is None:
    raise SystemExit("ERROR: could not find end of builtin loader function")

fn = s[fm.start():end_fn]

# Remove any line(s) inside this function that call engine_try_load_ach_file(eng)
# Also remove nearby Phase 1D comment lines if present.
lines = fn.splitlines(True)
out = []
removed = 0

for line in lines:
    if "engine_try_load_ach_file" in line:
        removed += 1
        continue
    # strip only the exact Phase 1D comment if it appears
    if "Phase 1D" in line and "ach file" in line and "builtins" in line:
        removed += 1
        continue
    out.append(line)

fn2 = "".join(out)

if fn2 == fn:
    # Nothing changed -> maybe it's not in builtin loader; list where the call exists
    hits = [m.start() for m in re.finditer(r'engine_try_load_ach_file\s*\(\s*eng\s*\)', s)]
    if not hits:
        raise SystemExit("ERROR: no engine_try_load_ach_file(eng) call found anywhere (so double-load is coming from elsewhere)")
    raise SystemExit(f"ERROR: builtin loader '{fn_name}' did not contain engine_try_load_ach_file(eng). It exists elsewhere ({len(hits)} hits).")

s2 = s[:fm.start()] + fn2 + s[end_fn:]
p.write_text(s2)

print(f"[ok] removed engine_try_load_ach_file call(s) from builtin loader '{fn_name}' (removed {removed} line(s))")
PY

cd daemon
make clean
make -j
echo "[phase1d-fix-double-v2] build OK"
