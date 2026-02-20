#!/data/data/com.termux/files/usr/bin/sh
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IN="$ROOT/achievements/smb1_demo.ach"
OUT="$ROOT/achievements/smb1_demo.mock.ach"

IN="$IN" OUT="$OUT" python - <<'PY'
import os, re
inp = os.environ["IN"]
outp = os.environ["OUT"]

with open(inp, "r", encoding="utf-8") as f:
    s = f.read()

# Convert 0xH000XXXX -> 0xHXXXX
s2 = re.sub(r'0xH0{3,}([0-9A-Fa-f]{3,4})', r'0xH\1', s)

with open(outp, "w", encoding="utf-8") as f:
    f.write(s2)

print(f"[ok] wrote {outp}")
PY
