#!/data/data/com.termux/files/usr/bin/sh
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
echo "[phase1d-skipbuiltins] root=$ROOT"

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

# Includes we need
s = ensure_include(s, "<stdlib.h>")     # getenv
s = ensure_include(s, "<stdbool.h>")    # bool
s = ensure_include(s, '"ach_load.h"')   # mmr_ach_load_file/mmr_ach_free

# Ensure the global flag exists (insert near top after includes)
if "static bool g_ach_loaded_from_file" not in s:
    # Insert after last include block
    m_last_inc = None
    for m in re.finditer(r'^\s*#include\s+[<"].+[>"]\s*$', s, re.M):
        m_last_inc = m
    if not m_last_inc:
        raise SystemExit("ERROR: could not find any #include lines in daemon/engine.c")
    insert_at = m_last_inc.end()
    s = s[:insert_at] + "\n\nstatic bool g_ach_loaded_from_file = false;\n" + s[insert_at:]

# Normalize helper signature to bool if it exists as void
s = s.replace("static void engine_try_load_ach_file(engine_t* eng)",
              "static bool engine_try_load_ach_file(engine_t* eng)")
s = s.replace("static void engine_try_load_ach_file(engine_t *eng)",
              "static bool engine_try_load_ach_file(engine_t *eng)")

# Find helper (it should exist from Phase 1D apply)
mh = re.search(r'static\s+bool\s+engine_try_load_ach_file\s*\(\s*engine_t\s*\*\s*eng\s*\)\s*\{', s)
if not mh:
    raise SystemExit("ERROR: engine_try_load_ach_file helper not found. (Phase 1D apply must have created it)")

# Extract helper block
brace_i = s.find("{", mh.start())
depth = 0
end_h = None
for j in range(brace_i, len(s)):
    c = s[j]
    if c == "{":
        depth += 1
    elif c == "}":
        depth -= 1
        if depth == 0:
            end_h = j + 1
            break
if end_h is None:
    raise SystemExit("ERROR: could not find end of engine_try_load_ach_file")

helper = s[mh.start():end_h]

# Convert bare return; => return false;
helper = re.sub(r'(\n\s*)return;\s*', r'\1return false;\n', helper)
helper = helper.replace("if (!path || !*path) return;", "if (!path || !*path) return false;")

# On success path, ensure flag is set before returning true.
# If helper already has "return true;", inject "g_ach_loaded_from_file = true;" right before first "return true;"
if "return true;" in helper and "g_ach_loaded_from_file = true;" not in helper:
    helper = helper.replace("return true;", "g_ach_loaded_from_file = true;\n  return true;", 1)

# Also make sure failures clear the flag (cheap safety):
# Add near top after path is validated
if "g_ach_loaded_from_file = false;" not in helper:
    helper = helper.replace("if (!path || !*path) return false;",
                            "if (!path || !*path) return false;\n\n  g_ach_loaded_from_file = false;", 1)

# Put helper back
s = s[:mh.start()] + helper + s[end_h:]

# Ensure engine_init calls engine_try_load_ach_file at least once
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

if "engine_try_load_ach_file(eng)" not in fn:
    # Insert right after runtime init if present, else right after '{'
    m_rt = re.search(r'^\s*rc_runtime_init\s*\([^;]*\)\s*;\s*$', fn, re.M)
    call = "\n  (void)engine_try_load_ach_file(eng);\n"
    if m_rt:
        pos = m_rt.end()
        fn = fn[:pos] + call + fn[pos:]
    else:
        # after opening brace
        if fn.startswith("{\n"):
            fn = "{\n" + call.lstrip("\n") + fn[2:]
        else:
            fn = "{\n" + call.lstrip("\n") + fn[1:]

# Put engine_init back
s = s[:open_brace] + fn + s[end_fn:]

# Now: find the function that prints "loaded builtin achievement" and guard it with g_ach_loaded_from_file
needle = "loaded builtin achievement"
idx = s.find(needle)
if idx == -1:
    raise SystemExit("ERROR: could not find the builtin log string 'loaded builtin achievement' anywhere in daemon/engine.c")

# Find enclosing function start before idx
func_re = re.compile(r'^\s*(static\s+)?(bool|void|int)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\([^;]*\)\s*\{', re.M)
starts = [m for m in func_re.finditer(s) if m.start() < idx]
if not starts:
    raise SystemExit("ERROR: could not locate enclosing function for builtin loader (function regex failed)")

fm = starts[-1]
ret_type = fm.group(2)
fn_name = fm.group(3)
fn_open = s.find("{", fm.start())
if fn_open == -1:
    raise SystemExit("ERROR: internal: couldn't find '{' for enclosing function")

# Insert guard at top of that function (only once)
window = s[fn_open:fn_open+500]
if "g_ach_loaded_from_file" not in window:
    guard = "\n  // Phase 1D: skip builtin achievements if file achievements were loaded.\n"
    if ret_type == "bool":
        guard += "  if (g_ach_loaded_from_file) return true;\n"
    else:
        guard += "  if (g_ach_loaded_from_file) return;\n"
    s = s[:fn_open+1] + guard + s[fn_open+1:]

p.write_text(s)
print(f"[ok] patched '{fn_name}' to skip builtins when g_ach_loaded_from_file=true")
PY

cd daemon
make clean
make -j
echo "[phase1d-skipbuiltins] build OK"
