#!/data/data/com.termux/files/usr/bin/sh
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

python - <<'PY'
from pathlib import Path
import re

p = Path("daemon/engine.c")
s = p.read_text()

# Replace the simple file-load call inside engine_init
s = re.sub(
    r'rc_runtime_init\(&eng->runtime\);\s*\n\s*}\s*\n\s*\*out = eng;\s*\n\s*engine_try_load_ach_file\(eng\);\s*\n\s*return true;',
    r'''rc_runtime_init(&eng->runtime);
  }

  bool ach_from_file = engine_try_load_ach_file(eng);
  if (!ach_from_file) {
    engine_load_builtin(eng);
  }

  *out = eng;
  return true;''',
    s
)

p.write_text(s)
print("[ok] engine_init logic patched")
PY

cd daemon
make clean
make -j
echo "[phase1e-fix-init] build OK"
