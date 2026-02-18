#!/data/data/com.termux/files/usr/bin/sh
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
echo "[phase1e-fix-ach-loader] root=$ROOT"

python - <<'PY'
from pathlib import Path
import re

p = Path("daemon/engine.c")
s = p.read_text()

# 1) Fix the broken early-return block inside engine_try_load_ach_file
# Replace:
#   const char* already = getenv("MMR_ACH_LOADED");
#   if (already && already[0] == '1') setenv(...);
#   return true;
#
# With:
#   const char* already = getenv("MMR_ACH_LOADED");
#   if (already && already[0] == '1') return true;
#
pat = re.compile(
    r'const char\*\s+already\s*=\s*getenv\("MMR_ACH_LOADED"\);\s*\n'
    r'\s*if\s*\(\s*already\s*&&\s*already\[0\]\s*==\s*\'1\'\s*\)\s*setenv\("MMR_ACH_LOADED",\s*"1",\s*1\);\s*\n'
    r'\s*return\s+true;\s*\n',
    re.M
)

s2, n = pat.subn(
    'const char* already = getenv("MMR_ACH_LOADED");\n'
    '  if (already && already[0] == \'1\') return true;\n\n',
    s
)

if n != 1:
    raise SystemExit(f"ERROR: expected to patch 1 broken early-return block, found {n}")

s = s2

# 2) Ensure we set MMR_ACH_LOADED on success.
# If the helper ends with 'return true;' but doesn't setenv, add it once.
# We'll replace the first "return true;" AFTER g_ach_loaded_from_file=true or after mmr_ach_free.
if 'setenv("MMR_ACH_LOADED"' not in s:
    # Prefer after "g_ach_loaded_from_file = true;"
    s3, n2 = re.subn(
        r'(g_ach_loaded_from_file\s*=\s*true;\s*\n\s*)return\s+true;',
        r'\1setenv("MMR_ACH_LOADED", "1", 1);\n  return true;',
        s,
        count=1,
        flags=re.M
    )
    if n2 == 0:
        # Fallback: after mmr_ach_free(&list);
        s3, n2 = re.subn(
            r'(mmr_ach_free\s*\(\s*&list\s*\)\s*;\s*\n\s*)return\s+true;',
            r'\1setenv("MMR_ACH_LOADED", "1", 1);\n  return true;',
            s,
            count=1,
            flags=re.M
        )
    if n2 == 0:
        raise SystemExit("ERROR: couldn't find a suitable 'return true;' success point to attach setenv(MMR_ACH_LOADED)")
    s = s3

# 3) Remove the Phase 1E guard from engine_load_builtin (it must not call engine_try_load_ach_file)
s = re.sub(
    r'^\s*// Phase 1E: if an \.ach file is provided and loads, skip builtin activations\.\s*\n'
    r'\s*if\s*\(\s*engine_try_load_ach_file\s*\(\s*eng\s*\)\s*\)\s*return\s+true;\s*\n',
    '',
    s,
    flags=re.M
)

# 4) Ensure engine_init gates builtins correctly (and does NOT call engine_try_load_ach_file blindly)
# Remove any standalone call inside engine_init body:
#   engine_try_load_ach_file(eng);
s = re.sub(
    r'(\bbool\s+engine_init\s*\([^\)]*\)\s*\{[\s\S]*?)^\s*engine_try_load_ach_file\s*\(\s*eng\s*\)\s*;\s*\n',
    r'\1',
    s,
    flags=re.M
)

# Ensure engine_init has:
#   bool ach_from_file = engine_try_load_ach_file(eng);
#   if (!ach_from_file) { engine_load_builtin(eng); }
# If it already has this, leave it alone.
if "bool ach_from_file" not in s:
    # Insert after rc_runtime_init(&eng->runtime); (or after backend init block)
    m_init = re.search(r'rc_runtime_init\s*\(\s*&\s*eng->runtime\s*\)\s*;\s*\n', s)
    if not m_init:
        raise SystemExit("ERROR: couldn't find rc_runtime_init(&eng->runtime); to anchor engine_init insert")
    insert = "\n  bool ach_from_file = engine_try_load_ach_file(eng);\n  if (!ach_from_file) {\n    engine_load_builtin(eng);\n  }\n"
    s = s[:m_init.end()] + insert + s[m_init.end():]

p.write_text(s)
print("[ok] engine.c: fixed ach loader early return + removed builtin guard + ensured engine_init gating")
PY

cd daemon
make clean
make -j
echo "[phase1e-fix-ach-loader] build OK"
