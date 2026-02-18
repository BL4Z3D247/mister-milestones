#!/data/data/com.termux/files/usr/bin/sh
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
echo "[phase1e-harden-builtin] root=$ROOT"

python - <<'PY'
from pathlib import Path
import re

p = Path("daemon/engine.c")
s = p.read_text()

# Ensure stdlib include exists (getenv/setenv)
if not re.search(r'^\s*#include\s*<stdlib\.h>\s*$', s, re.M):
    m = re.search(r'^\s*#include\s+"engine\.h"\s*\n', s, re.M)
    if m:
        i = m.end()
        s = s[:i] + "#include <stdlib.h>\n" + s[i:]
    else:
        s = "#include <stdlib.h>\n" + s

# Find engine_load_builtin signature
m = re.search(r'^\s*bool\s+engine_load_builtin\s*\(\s*engine_t\s*\*\s*eng\s*\)\s*\{', s, re.M)
if not m:
    raise SystemExit("ERROR: could not find engine_load_builtin(engine_t* eng)")

open_br = s.find("{", m.start())
insert_at = open_br + 1

guard = r'''
  // Phase 1E hardening:
  // - If file achievements were loaded, NEVER load builtins.
  // - Never load builtins more than once (prevents duplicate startup prints).
  const char* ach_loaded = getenv("MMR_ACH_LOADED");
  if (ach_loaded && ach_loaded[0] == '1') return true;

  const char* builtins_loaded = getenv("MMR_BUILTINS_LOADED");
  if (builtins_loaded && builtins_loaded[0] == '1') return true;
  setenv("MMR_BUILTINS_LOADED", "1", 1);

'''.lstrip("\n")

# Only insert once
window = s[open_br:open_br+600]
if "MMR_BUILTINS_LOADED" not in window:
    s = s[:insert_at] + "\n" + guard + s[insert_at:]
else:
    print("[warn] guard already present (skipping insert)")

p.write_text(s)
print("[ok] engine_load_builtin hardened (skip if file loaded; no double-run)")
PY

cd daemon
make clean
make -j
echo "[phase1e-harden-builtin] build OK"
