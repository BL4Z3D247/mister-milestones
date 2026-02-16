#!/data/data/com.termux/files/usr/bin/sh
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
echo "[phase1d-fix] root=$ROOT"

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

# 1) Ensure engine_try_load_ach_file returns bool (true if it replaced builtins)
# Convert signature: static void engine_try_load_ach_file(engine_t* eng) { ... }
# ->             : static bool engine_try_load_ach_file(engine_t* eng) { ... }
if "static void engine_try_load_ach_file" in s:
    s = s.replace("static void engine_try_load_ach_file(engine_t* eng)", "static bool engine_try_load_ach_file(engine_t* eng)", 1)

# Ensure <stdbool.h> is available somewhere (engine.h likely already has it, but keep safe)
if not re.search(r'^\s*#include\s*<stdbool\.h>\s*$', s, re.M):
    # insert after stdlib if present, else after first include
    m = re.search(r'^\s*#include\s*<stdlib\.h>\s*\n', s, re.M)
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

# 2) Make all early exits return false, and successful replacement return true
# In helper: "return;" -> "return false;"
# But avoid touching other functions by restricting to helper body.

m = re.search(r'static bool engine_try_load_ach_file\(engine_t\* eng\)\s*\{', s)
if not m:
    raise SystemExit("ERROR: could not find engine_try_load_ach_file helper to patch")

start = m.end()
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

# Replace bare returns inside helper
helper = re.sub(r'(\n\s*)return;\s*', r'\1return false;\n', helper)

# If we successfully replaced, ensure we return true at end (before final brace)
if "return true;" not in helper:
    helper = re.sub(r'\n\}\s*$', "\n  return true;\n}\n", helper)

# Ensure we still return false if file isn't set
if "if (!path || !*path) return false;" not in helper:
    helper = helper.replace("if (!path || !*path) return;", "if (!path || !*path) return false;")

# Ensure the load-fail branches return false (already handled by return->return false)
# After successful activation loop, it returns true.

# Reinsert patched helper
s = s[:m.start()] + helper + s[end:]

# 3) Fix engine_init so it:
#   - tries ach-file first, and if it succeeds, returns true immediately
#   - otherwise loads builtins

mi = re.search(r'\bengine_init\s*\(', s)
if not mi:
    raise SystemExit("ERROR: could not find engine_init")

open_brace = s.find("{", mi.start())
if open_brace == -1:
    raise SystemExit("ERROR: could not find engine_init opening brace")

depth = 0
end_fn = None
for idx in range(open_brace, len(s)):
    c = s[idx]
    if c == "{":
        depth += 1
    elif c == "}":
        depth -= 1
        if depth == 0:
            end_fn = idx + 1
            break
if end_fn is None:
    raise SystemExit("ERROR: could not find end of engine_init")

fn = s[open_brace:end_fn]

# Remove any existing engine_try_load_ach_file(eng); line inside engine_init (we'll re-add correctly)
fn2 = re.sub(r'^\s*engine_try_load_ach_file\s*\(\s*eng\s*\)\s*;\s*\n', '', fn, flags=re.M)

# Find where builtins are loaded
# We expect something like: engine_load_builtin(eng);
mload = re.search(r'^\s*engine_load_builtin\s*\(\s*eng\s*\)\s*;\s*$', fn2, re.M)
if not mload:
    raise SystemExit("ERROR: could not find engine_load_builtin(eng); inside engine_init")

# Insert "if (engine_try_load_ach_file(eng)) return true;" BEFORE builtin load
insert = "  if (engine_try_load_ach_file(eng)) return true;\n"
pos = mload.start()
fn2 = fn2[:pos] + insert + fn2[pos:]

# Put function back
s = s[:open_brace] + fn2 + s[end_fn:]

p.write_text(s)
print("[ok] daemon/engine.c: ach-file now overrides builtins cleanly")
PY

# Rebuild
cd "$ROOT/daemon"
make clean
make -j
echo "[phase1d-fix] build OK"
