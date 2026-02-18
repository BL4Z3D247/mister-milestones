#!/data/data/com.termux/files/usr/bin/sh
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

python - <<'PY'
from pathlib import Path
import re

p = Path("daemon/engine.c")
s = p.read_text()

# Fix the broken "already loaded" guard (it currently returns true unconditionally)
# Replace the bad block with the correct early return.
s = re.sub(
    r'const char\* already = getenv\("MMR_ACH_LOADED"\);\s*\n\s*if \(already && already\[0\] == \'1\'\) setenv\("MMR_ACH_LOADED", "1", 1\);\s*\n\s*return true;\s*\n',
    'const char* already = getenv("MMR_ACH_LOADED");\n  if (already && already[0] == \'1\') return true;\n\n',
    s,
    count=1
)

# Also: engine_load_builtin currently calls engine_try_load_ach_file again.
# That creates double-loading and duplicate logs. Remove that guard entirely.
s = re.sub(
    r'\n\s*// Phase 1E: if an \.ach file is provided and loads, skip builtin activations\.\n\s*if \(engine_try_load_ach_file\(eng\)\) return true;\n',
    '\n',
    s,
    count=1
)

p.write_text(s)
print("[ok] fixed engine_try_load_ach_file guard + removed redundant file-check in engine_load_builtin")
PY

cd "$ROOT/daemon"
make clean
make -j
echo "[phase1e-fix] build OK"
