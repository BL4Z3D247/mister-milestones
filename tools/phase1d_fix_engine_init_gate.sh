#!/data/data/com.termux/files/usr/bin/sh
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
echo "[phase1d-gate] root=$ROOT"

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

# --- ensure engine_try_load_ach_file returns bool ---
s = s.replace("static void engine_try_load_ach_file(engine_t* eng)",
              "static bool engine_try_load_ach_file(engine_t* eng)")
s = s.replace("static void engine_try_load_ach_file(engine_t *eng)",
              "static bool engine_try_load_ach_file(engine_t *eng)")

# Ensure stdbool include exists
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

# --- patch helper body to return true/false correctly ---
m = re.search(r'static\s+bool\s+engine_try_load_ach_file\s*\(\s*engine_t\s*\*\s*eng\s*\)\s*\{', s)
if not m:
  raise SystemExit("ERROR: engine_try_load_ach_file helper not found (it must exist from Phase 1D)")

brace_i = s.find("{", m.start())
depth = 0
end = None
for j in range(brace_i, len(s)):
  c = s[j]
  if c == "{": depth += 1
  elif c == "}":
    depth -= 1
    if depth == 0:
      end = j + 1
      break
if end is None:
  raise SystemExit("ERROR: could not find end of engine_try_load_ach_file")

helper = s[m.start():end]

# Convert any bare `return;` to `return false;`
helper = re.sub(r'(\n\s*)return;\s*', r'\1return false;\n', helper)
helper = helper.replace("if (!path || !*path) return;", "if (!path || !*path) return false;")

# Ensure it ends with return true (success path)
if "return true;" not in helper:
  helper = re.sub(r'\n\}\s*$', "\n  return true;\n}\n", helper)

s = s[:m.start()] + helper + s[end:]

# --- gate builtin load inside engine_init ---
mi = re.search(r'\bbool\s+engine_init\s*\(', s)
if not mi:
  raise SystemExit("ERROR: could not find bool engine_init(...) in daemon/engine.c")

open_brace = s.find("{", mi.start())
if open_brace == -1:
  raise SystemExit("ERROR: could not find engine_init opening brace")

depth = 0
end_fn = None
for j in range(open_brace, len(s)):
  c = s[j]
  if c == "{": depth += 1
  elif c == "}":
    depth -= 1
    if depth == 0:
      end_fn = j + 1
      break
if end_fn is None:
  raise SystemExit("ERROR: could not find end of engine_init")

fn = s[open_brace:end_fn]

# Remove any existing direct call line we may have inserted earlier
fn = re.sub(r'^\s*engine_try_load_ach_file\s*\(\s*eng\s*\)\s*;\s*\n', '', fn, flags=re.M)

# Case A: plain call
pat_plain = re.compile(r'^\s*(engine_load_builtins?|engine_load_builtin)\s*\(\s*eng\s*\)\s*;\s*$', re.M)
# Case B: guarded return
pat_guard = re.compile(r'^\s*if\s*\(\s*!\s*(engine_load_builtins?|engine_load_builtin)\s*\(\s*eng\s*\)\s*\)\s*return\s+false\s*;\s*$', re.M)

m_plain = pat_plain.search(fn)
m_guard = pat_guard.search(fn)

if m_guard:
  name = m_guard.group(1)
  repl = (
    "  if (!engine_try_load_ach_file(eng)) {\n"
    f"    if (!{name}(eng)) return false;\n"
    "  }\n"
  )
  fn = fn[:m_guard.start()] + repl + fn[m_guard.end():]
elif m_plain:
  name = m_plain.group(1)
  repl = (
    "  if (!engine_try_load_ach_file(eng)) {\n"
    f"    {name}(eng);\n"
    "  }\n"
  )
  fn = fn[:m_plain.start()] + repl + fn[m_plain.end():]
else:
  raise SystemExit("ERROR: could not find engine_load_builtin(s)(eng) call inside engine_init to gate")

s = s[:open_brace] + fn + s[end_fn:]

p.write_text(s)
print("[ok] engine_init now skips builtins when .ach loads")
PY

cd daemon
make clean
make -j
echo "[phase1d-gate] build OK"
