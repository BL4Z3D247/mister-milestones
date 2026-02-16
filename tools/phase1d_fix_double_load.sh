#!/data/data/com.termux/files/usr/bin/sh
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
echo "[phase1d-fix-double] root=$ROOT"

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

# Remove the guard inserted into engine_load_builtin (or any builtin loader) that calls engine_try_load_ach_file.
# We remove ONLY the specific Phase 1D guard block we inserted.
pattern = re.compile(
    r'\n\s*// Phase 1D: if an \.ach file is provided and loads, do NOT load builtins\.\n'
    r'\s*if\s*\(\s*engine_try_load_ach_file\s*\(\s*eng\s*\)\s*\)\s*return\s+(true|;)\s*\n',
    re.M
)
s2, n = pattern.subn("\n", s)
if n == 0:
    raise SystemExit("ERROR: did not find the Phase 1D guard block to remove (maybe already fixed?)")

p.write_text(s2)
print(f"[ok] removed duplicate .ach guard from builtin loader (removed {n} block(s))")
PY

cd daemon
make clean
make -j
echo "[phase1d-fix-double] build OK"
