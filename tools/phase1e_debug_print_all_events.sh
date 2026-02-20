#!/data/data/com.termux/files/usr/bin/sh
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

python - <<'PY'
from pathlib import Path
import re

p = Path("daemon/engine.c")
s = p.read_text()

# Replace ra_event_handler with a verbose version.
# It prints every event type it receives.
pat = re.compile(r'static void RC_CCONV ra_event_handler\s*\([^)]*\)\s*\{[\s\S]*?\n\}', re.M)

replacement = r'''static const char* ev_name(int t) {
  switch (t) {
    case RC_RUNTIME_EVENT_ACHIEVEMENT_ACTIVATED: return "ACH_ACTIVATED";
    case RC_RUNTIME_EVENT_ACHIEVEMENT_PAUSED: return "ACH_PAUSED";
    case RC_RUNTIME_EVENT_ACHIEVEMENT_RESET: return "ACH_RESET";
    case RC_RUNTIME_EVENT_ACHIEVEMENT_TRIGGERED: return "ACH_TRIGGERED";
    case RC_RUNTIME_EVENT_ACHIEVEMENT_PRIMED: return "ACH_PRIMED";
    case RC_RUNTIME_EVENT_ACHIEVEMENT_DISABLED: return "ACH_DISABLED";
    case RC_RUNTIME_EVENT_ACHIEVEMENT_UNPRIMED: return "ACH_UNPRIMED";
    case RC_RUNTIME_EVENT_ACHIEVEMENT_PROGRESS_UPDATED: return "ACH_PROGRESS";
    case RC_RUNTIME_EVENT_LBOARD_STARTED: return "LBOARD_STARTED";
    case RC_RUNTIME_EVENT_LBOARD_CANCELED: return "LBOARD_CANCELED";
    case RC_RUNTIME_EVENT_LBOARD_UPDATED: return "LBOARD_UPDATED";
    case RC_RUNTIME_EVENT_LBOARD_TRIGGERED: return "LBOARD_TRIGGERED";
    case RC_RUNTIME_EVENT_LBOARD_DISABLED: return "LBOARD_DISABLED";
    default: return "UNKNOWN";
  }
}

static void RC_CCONV ra_event_handler(const rc_runtime_event_t *ev) {
  if (!ev) return;
  printf("[RA] event=%s type=%d id=%u\n", ev_name(ev->type), ev->type, ev->id);
  fflush(stdout);
}'''

s2, n = pat.subn(replacement, s, count=1)
if n != 1:
  raise SystemExit(f"ERROR: couldn't find ra_event_handler to replace (matches={n})")

p.write_text(s2)
print("[ok] patched daemon/engine.c to print all rc_runtime events")
PY

cd daemon
make clean
make -j
echo "[ok] rebuilt mmr-daemon with verbose runtime events"
