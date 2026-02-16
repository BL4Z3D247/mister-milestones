#!/data/data/com.termux/files/usr/bin/sh
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
echo "[phase1d-gate-v2] root=$ROOT"

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

# Ensure <stdbool.h> include exists
if not re.search(r'^\s*#include\s*<stdbool\.h>\s*$', s, re.M):
  m = re.search(r'^\s*#include\s+"engine\.h"\s*\n', s, re.M)
  if m:
    i = m.end()
    s = s[:i] + "#include <stdbool.h>\n" + s[i:]
  else:
    m2 = re.search(r'^\s*#include\s*[<"].+[>"]\s*\n', s, re.M)
    if m2:
      i = m2.end()
      s = s[:i] + "#include <stdbool.h>\n" + s[i:]
    else:
      s = "#include <stdbool.h>\n" + s

# Normalize helper signature to bool (if it exists as void)
s = s.replace("static void engine_try_load_ach_file(engine_t* eng)",
              "static bool engine_try_load_ach_file(engine_t* eng)")
s = s.replace("static void engine_try_load_ach_file(engine_t *eng)",
              "static bool engine_try_load_ach_file(engine_t *eng)")

# Find engine_init block
mi = re.search(r'\bbool\s+engine_init\s*\(', s)
if not mi:
  raise SystemExit("ERROR: could not find 'bool engine_init' in daemon/engine.c")

open_brace = s.find("{", mi.start())
if open_brace == -1:
  raise SystemExit("ERROR: could not find engine_init opening brace")

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
  raise SystemExit("ERROR: could not find end of engine_init")

fn = s[open_brace:end_fn]

# Remove any previous direct call(s) to engine_try_load_ach_file(eng);
fn = re.sub(r'^\s*engine_try_load_ach_file\s*\(\s*eng\s*\)\s*;\s*\n', '', fn, flags=re.M)

# Insert: bool ach_from_file = engine_try_load_ach_file(eng);
# Prefer after rc_runtime_init(&eng->runtime);
insert_line = "  bool ach_from_file = engine_try_load_ach_file(eng);\n"

if "ach_from_file" not in fn:
  m_runtime = re.search(r'^\s*rc_runtime_init\s*\(\s*&\s*eng->runtime\s*\)\s*;\s*$', fn, re.M)
  if m_runtime:
    pos = m_runtime.end()
    fn = fn[:pos] + "\n" + insert_line + fn[pos:]
  else:
    # fallback: after first rc_runtime_init(...)
    m_runtime2 = re.search(r'^\s*rc_runtime_init\s*\([^;]*\)\s*;\s*$', fn, re.M)
    if m_runtime2:
      pos = m_runtime2.end()
      fn = fn[:pos] + "\n" + insert_line + fn[pos:]
    else:
      # fallback: right after opening brace
      fn = "{\n" + insert_line + fn[2:] if fn.startswith("{\n") else "{\n" + insert_line + fn[1:]

# Now wrap the builtin activation loop/block (identified by "loaded builtin achievement") with if (!ach_from_file) { ... }
needle = "loaded builtin achievement"
idx = fn.find(needle)
if idx == -1:
  # If we can't find the needle inside engine_init, fallback to wrapping any engine_load_builtin* call if present
  m_call = re.search(r'^\s*(engine_load_builtins?|engine_load_builtin)\s*\(\s*eng\s*\)\s*;\s*$', fn, re.M)
  if not m_call:
    raise SystemExit("ERROR: could not find builtin activation block in engine_init (no 'loaded builtin achievement' and no engine_load_builtin call)")
  name = m_call.group(1)
  repl = (
    "  if (!ach_from_file) {\n"
    f"    {name}(eng);\n"
    "  }\n"
  )
  fn = fn[:m_call.start()] + repl + fn[m_call.end():]
else:
  # Find nearest 'for (...) {' before the needle, and wrap that loop.
  pre = fn[:idx]
  for_pos = pre.rfind("for")
  if for_pos == -1:
    raise SystemExit("ERROR: found builtin log string, but could not locate preceding 'for' loop to wrap")

  # Find the first '{' after that 'for'
  loop_open = fn.find("{", for_pos)
  if loop_open == -1:
    raise SystemExit("ERROR: could not find '{' for builtin loop")

  # Find end of that loop by brace counting starting at loop_open
  d = 0
  loop_end = None
  for k in range(loop_open, len(fn)):
    c = fn[k]
    if c == "{":
      d += 1
    elif c == "}":
      d -= 1
      if d == 0:
        loop_end = k + 1
        break
  if loop_end is None:
    raise SystemExit("ERROR: could not find end of builtin loop")

  loop_block = fn[for_pos:loop_end]
  wrapped = "  if (!ach_from_file) {\n" + loop_block + "\n  }\n"
  fn = fn[:for_pos] + wrapped + fn[loop_end:]

# Put function back
s = s[:open_brace] + fn + s[end_fn:]
p.write_text(s)
print("[ok] engine_init gated: builtins skipped when .ach loads")
PY

cd daemon
make clean
make -j
echo "[phase1d-gate-v2] build OK"
