#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail

FILE="${1:-}"
if [ -z "$FILE" ] || [ ! -f "$FILE" ]; then
  echo "usage: $0 /path/to/nes_cpu_ram.bin" >&2
  exit 2
fi

# Offsets (SMB1-ish demo offsets you chose)
OFF_WORLD=$((0x075F))
OFF_STAGE=$((0x075C))
OFF_LIVES=$((0x075A))
OFF_COINS_LO=$((0x07ED))
OFF_COINS_HI=$((0x07EE))

# Persist unlocks + previous state per file (so restarting doesn't insta-fire)
STATE_DIR="${TMPDIR:-/tmp}/smb1_watch_state"
mkdir -p "$STATE_DIR"
KEY="$(echo -n "$FILE" | md5sum | awk '{print $1}')"
STATE_FILE="$STATE_DIR/$KEY.state"

# state vars
prev_world=""
prev_stage=""
prev_lives=""
prev_coins=""

unlocked_world11=0
unlocked_lives5=0
unlocked_coins5=0

# load previous state/unlocks (if any)
if [ -f "$STATE_FILE" ]; then
  # shellcheck disable=SC1090
  source "$STATE_FILE" || true
fi

save_state() {
  cat > "$STATE_FILE" <<EOF
prev_world="$prev_world"
prev_stage="$prev_stage"
prev_lives="$prev_lives"
prev_coins="$prev_coins"
unlocked_world11=$unlocked_world11
unlocked_lives5=$unlocked_lives5
unlocked_coins5=$unlocked_coins5
EOF
}

read_u8() {
  local off="$1"
  od -An -tu1 -N1 -j "$off" "$FILE" 2>/dev/null | tr -d ' '
}

read_u16le() {
  local lo hi
  lo="$(read_u8 "$OFF_COINS_LO")"
  hi="$(read_u8 "$OFF_COINS_HI")"
  # handle empty reads (file too small / transient)
  [ -z "$lo" ] && lo=0
  [ -z "$hi" ] && hi=0
  echo $(( (hi << 8) | lo ))
}

echo "[watch] file=$FILE"
echo "[watch] world@0x075F stage@0x075C lives@0x075A coins@0x07ED(le16)"
echo "[watch] state=$STATE_FILE"

# Initialize prev_* from current snapshot if unset
init_snapshot() {
  local w s l c
  w="$(read_u8 "$OFF_WORLD")"; [ -z "$w" ] && w=0
  s="$(read_u8 "$OFF_STAGE")"; [ -z "$s" ] && s=0
  l="$(read_u8 "$OFF_LIVES")"; [ -z "$l" ] && l=0
  c="$(read_u16le)"

  if [ -z "$prev_world" ]; then
    prev_world="$w"; prev_stage="$s"; prev_lives="$l"; prev_coins="$c"
    save_state
  fi
}

init_snapshot

# Main loop
while :; do
  world="$(read_u8 "$OFF_WORLD")"; [ -z "$world" ] && world=0
  stage="$(read_u8 "$OFF_STAGE")"; [ -z "$stage" ] && stage=0
  lives="$(read_u8 "$OFF_LIVES")"; [ -z "$lives" ] && lives=0
  coins="$(read_u16le)"

  # Edge-trigger helpers
  became_world11=0
  if [ "$world" -eq 0 ] && [ "$stage" -eq 1 ]; then
    if ! ( [ "$prev_world" -eq 0 ] && [ "$prev_stage" -eq 1 ] ); then
      became_world11=1
    fi
  fi

  became_lives5=0
  if [ "$lives" -ge 5 ] && [ "$prev_lives" -lt 5 ]; then
    became_lives5=1
  fi

  became_coins5=0
  if [ "$coins" -eq 5 ] && [ "$prev_coins" -ne 5 ]; then
    became_coins5=1
  fi

  # Fire once
  if [ $unlocked_world11 -eq 0 ] && [ $became_world11 -eq 1 ]; then
    echo "[ACH] SMB1: Entered World 1-1"
    unlocked_world11=1
  fi
  if [ $unlocked_lives5 -eq 0 ] && [ $became_lives5 -eq 1 ]; then
    echo "[ACH] SMB1: Stockpile (5 lives)"
    unlocked_lives5=1
  fi
  if [ $unlocked_coins5 -eq 0 ] && [ $became_coins5 -eq 1 ]; then
    echo "[ACH] SMB1: Counter Hit 5 (0x07ED/EE)"
    unlocked_coins5=1
  fi

  echo "[ram] world=$world stage=$stage lives=$lives coins=$coins"

  # update prev + persist
  prev_world="$world"
  prev_stage="$stage"
  prev_lives="$lives"
  prev_coins="$coins"
  save_state

  sleep 1
done
