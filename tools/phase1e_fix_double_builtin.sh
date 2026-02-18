#!/data/data/com.termux/files/usr/bin/sh
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

python - <<'PY'
from pathlib import Path
import re

p = Path("daemon/main.c")
s = p.read_text()

# Remove any direct call to engine_load_builtin(...) in main.c
# (engine_init should own this decision)
s2, n = re.subn(r'^\s*engine_load_builtin\s*\(\s*[^;]*\)\s*;\s*\n', '', s, flags=re.M)
if n == 0:
    print("[warn] no engine_load_builtin() call found in daemon/main.c (maybe already removed)")
else:
    print(f"[ok] removed {n} engine_load_builtin() call(s) from daemon/main.c")

p.write_text(s2)
PY

cd "$ROOT/daemon"
make clean
make -j
echo "[phase1e] rebuild OK"
