#!/data/data/com.termux/files/usr/bin/sh
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "[phase1d] root=$ROOT"

# Require we are on the correct branch
BR_EXPECT="phase1d-ach-integration"
BR_NOW="$(git rev-parse --abbrev-ref HEAD)"
if [ "$BR_NOW" != "$BR_EXPECT" ]; then
  echo "ERROR: you are on '$BR_NOW' but expected '$BR_EXPECT'"
  exit 1
fi

# Clean tree required
if [ -n "$(git status --porcelain | grep -vE '^\?\? tools/phase1d_apply\.sh$')" ]; then
  echo "ERROR: working tree not clean. Commit/stash first."
  git status --porcelain
  exit 1
fi

# ---------- Patch daemon/main.c ----------
python - <<'PY'
import pathlib, re

p = pathlib.Path("daemon/main.c")
s = p.read_text()

def ensure_sys_include(src: str, header: str) -> str:
    if re.search(r'^\s*#include\s*<%s>\s*$' % re.escape(header), src, re.M):
        return src
    # Prefer insert after stdio.h
    m = re.search(r'^\s*#include\s*<stdio\.h>\s*$\n', src, re.M)
    if m:
        i = m.end()
        return src[:i] + f'#include <{header}>\n' + src[i:]
    # Else after first include
    m2 = re.search(r'^\s*#include\s*[<"].+[>"]\s*$\n', src, re.M)
    if m2:
        i = m2.end()
        return src[:i] + f'#include <{header}>\n' + src[i:]
    return f'#include <{header}>\n' + src

s = ensure_sys_include(s, "stdlib.h")  # setenv/getenv
s = ensure_sys_include(s, "string.h")  # strcmp

# Ensure ach_file declaration exists
if not re.search(r'\bconst\s+char\s*\*\s*ach_file\s*=\s*NULL\s*;', s):
    inserted = False
    for pat in [
        r'(\bconst\s+char\s*\*\s*backend\s*=\s*[^;]+;\s*\n)',
        r'(\bconst\s+char\s*\*\s*core\s*=\s*[^;]+;\s*\n)',
        r'(\bint\s+fps\s*=\s*\d+\s*;\s*\n)',
    ]:
        s2, n = re.subn(pat, r'\1const char* ach_file = NULL;\n', s, count=1)
        if n:
            s = s2
            inserted = True
            break
    if not inserted:
        m = re.search(r'\bint\s+main\s*\([^)]*\)\s*\{', s)
        if not m:
            raise SystemExit("ERROR: could not locate main() to declare ach_file")
        i = m.end()
        s = s[:i] + "\n  const char* ach_file = NULL;\n" + s[i:]

# Find argv for-loop
m = re.search(r'for\s*\(\s*int\s+i\s*=\s*1\s*;\s*i\s*<\s*argc\s*;\s*i\+\+\s*\)\s*\{', s)
if not m:
    raise SystemExit("ERROR: could not find argv parsing for-loop in daemon/main.c")

loop_insert = m.end()

# Insert --ach-file handler (only once)
if "--ach-file" not in s:
    inject = r'''
    if (strcmp(argv[i], "--ach-file") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "ERROR: --ach-file requires a path\n");
        return 1;
      }
      ach_file = argv[++i];
      continue;
    }
'''
    s = s[:loop_insert] + inject + s[loop_insert:]

# Find end of argv loop via brace counting
start = m.start()
brace_i = s.find("{", start)
depth = 0
end_loop = None
for j in range(brace_i, len(s)):
    c = s[j]
    if c == "{":
        depth += 1
    elif c == "}":
        depth -= 1
        if depth == 0:
            end_loop = j + 1
            break
if end_loop is None:
    raise SystemExit("ERROR: could not find end of argv loop")

hook = r'''
  // Phase 1D: pass optional .ach file path to engine via environment
  if (ach_file && *ach_file) {
    setenv("MMR_ACH_FILE", ach_file, 1);
  }
'''
if "setenv(\"MMR_ACH_FILE\"" not in s:
    s = s[:end_loop] + hook + s[end_loop:]

p.write_text(s)
print("[ok] daemon/main.c patched")
PY

# ---------- Patch daemon/engine.c ----------
python - <<'PY'
import pathlib, re

p = pathlib.Path("daemon/engine.c")
s = p.read_text()

def ensure_include(src: str, line: str) -> str:
    if line in src:
        return src
    m = re.search(r'(#include\s+"engine\.h"\s*\n)', src)
    if m:
        i = m.end()
        return src[:i] + line + "\n" + src[i:]
    m2 = re.search(r'(^\s*#include\s*[<"].+[>"]\s*\n)', src, re.M)
    if m2:
        i = m2.end()
        return src[:i] + line + "\n" + src[i:]
    return line + "\n" + src

s = ensure_include(s, '#include <stdlib.h>')     # getenv
s = ensure_include(s, '#include "ach_load.h"')   # loader API

# Helper insertion (only if missing)
if "static void engine_try_load_ach_file" not in s:
    helper = r'''

// Phase 1D: load achievements from a .ach file if MMR_ACH_FILE is set.
// If it loads successfully (and has entries), it REPLACES builtin activations.
static void engine_try_load_ach_file(engine_t* eng) {
  const char* path = getenv("MMR_ACH_FILE");
  if (!path || !*path) return;

  mmr_ach_list_t list;
  if (!mmr_ach_load_file(path, &list)) {
    fprintf(stderr, "[WARN] could not load ach file: %s (using builtins)\n", path);
    return;
  }

  if (list.count == 0) {
    fprintf(stderr, "[WARN] ach file loaded but empty: %s (using builtins)\n", path);
    mmr_ach_free(&list);
    return;
  }

  rc_runtime_reset(&eng->runtime);

  for (size_t j = 0; j < list.count; j++) {
    mmr_ach_def_t* a = &list.items[j];
    int rc = rc_runtime_activate_achievement(&eng->runtime, a->id, a->memaddr, NULL, 0);
    if (rc == 0) {
      fprintf(stderr, "[INFO] loaded file achievement %u: %s\n", a->id, a->title);
    } else {
      fprintf(stderr, "[WARN] failed to activate achievement %u from file\n", a->id);
    }
  }

  mmr_ach_free(&list);
}
'''
    # Insert helper immediately before engine_init
    m = re.search(r'\bbool\s+engine_init\s*\(', s)
    if not m:
        m = re.search(r'\bengine_init\s*\(', s)
    if not m:
        raise SystemExit("ERROR: could not find engine_init in daemon/engine.c")
    s = s[:m.start()] + helper + s[m.start():]

# Insert call before return true inside engine_init
mi = re.search(r'\bengine_init\s*\(', s)
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

fn_block = s[open_brace:end_fn]
rt = fn_block.rfind("return true;")
if rt == -1:
    raise SystemExit("ERROR: could not find 'return true;' inside engine_init")

call = "  engine_try_load_ach_file(eng);\n"
if call.strip() not in fn_block:
    insert_pos = open_brace + rt
    s = s[:insert_pos] + call + s[insert_pos:]

p.write_text(s)
print("[ok] daemon/engine.c patched")
PY

# ---------- Build ----------
cd "$ROOT/daemon"
make clean
make -j
echo "[phase1d] build OK"
