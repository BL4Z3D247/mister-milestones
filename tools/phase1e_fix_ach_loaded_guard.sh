#!/data/data/com.termux/files/usr/bin/sh
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

python - <<'PY'
from pathlib import Path
import re

p = Path("daemon/engine.c")
s = p.read_text()

# --- locate engine_try_load_ach_file block and patch its prologue safely ---
m = re.search(r'static\s+bool\s+engine_try_load_ach_file\s*\(\s*engine_t\s*\*\s*eng\s*\)\s*\{', s)
if not m:
    raise SystemExit("ERROR: engine_try_load_ach_file not found")

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

# Remove any existing MMR_ACH_LOADED handling block (whatever it is)
# We delete from the first line containing getenv("MMR_ACH_LOADED") up to just before the MMR_ACH_FILE getenv line.
helper2, n = re.subn(
    r'^\s*const\s+char\s*\*\s*already\s*=\s*getenv\("MMR_ACH_LOADED"\);\s*\n(?:.*\n)*?\s*const\s+char\s*\*\s*path\s*=\s*getenv\("MMR_ACH_FILE"\);\s*\n',
    '  const char* already = getenv("MMR_ACH_LOADED");\n'
    '  if (already && already[0] == \'1\') return true;\n\n'
    '  const char* path = getenv("MMR_ACH_FILE");\n',
    helper,
    flags=re.M
)

if n == 0:
    # If it didn't have that structure, do a simpler strategy:
    # insert guard right after '{' if not present, and remove any stray early "return true;" before reading MMR_ACH_FILE.
    if 'getenv("MMR_ACH_LOADED")' in helper:
        # remove any lines referencing MMR_ACH_LOADED
        helper2 = re.sub(r'^\s*.*MMR_ACH_LOADED.*\n', '', helper, flags=re.M)
    else:
        helper2 = helper

    # remove a premature "return true;" that appears before getenv("MMR_ACH_FILE")
    # (only the first one, and only if it occurs before the MMR_ACH_FILE line)
    pos_file = helper2.find('getenv("MMR_ACH_FILE")')
    if pos_file != -1:
        before = helper2[:pos_file]
        after = helper2[pos_file:]
        before = re.sub(r'^\s*return\s+true;\s*\n', '', before, count=1, flags=re.M)
        helper2 = before + after

    # insert guard after opening brace if not already correct
    if "if (already && already[0] == '1') return true;" not in helper2:
        helper2 = re.sub(
            r'\{\s*\n',
            '{\n  const char* already = getenv("MMR_ACH_LOADED");\n  if (already && already[0] == \'1\') return true;\n\n',
            helper2,
            count=1
        )

# Ensure on success we set MMR_ACH_LOADED=1 once (before returning true)
if 'setenv("MMR_ACH_LOADED", "1", 1);' not in helper2:
    # Insert before the last 'return true;' in the helper
    helper2 = re.sub(r'\breturn\s+true;\s*', 'setenv("MMR_ACH_LOADED", "1", 1);\n  return true;', helper2, count=1)

# Reinsert patched helper
s = s[:m.start()] + helper2 + s[end:]

# --- remove redundant guard inside engine_load_builtin (engine_init owns this now) ---
s = re.sub(
    r'^\s*// Phase 1E: if an \.ach file is provided and loads, skip builtin activations\.\s*\n'
    r'\s*if\s*\(\s*engine_try_load_ach_file\s*\(\s*eng\s*\)\s*\)\s*return\s+true;\s*\n',
    '',
    s,
    flags=re.M
)

p.write_text(s)
print("[ok] patched engine_try_load_ach_file MMR_ACH_LOADED guard + removed redundant builtin guard")
PY

cd "$ROOT/daemon"
make clean
make -j
echo "[phase1e] rebuild OK"
