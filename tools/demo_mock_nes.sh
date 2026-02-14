#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DAEMON_DIR="$REPO_ROOT/daemon"
MOCKDIR="${MOCKDIR:-$HOME/mmr_mock}"
FILE="$MOCKDIR/nes_cpu_ram.bin"

echo "== build =="
cd "$DAEMON_DIR"
make -j

echo "== prepare mock file =="
pkill -f mmr-daemon 2>/dev/null || true
rm -rf "$MOCKDIR"
mkdir -p "$MOCKDIR"
dd if=/dev/zero of="$FILE" bs=1 count=2048 status=none
sync

echo "== start daemon (mock mode) =="
"$DAEMON_DIR/mmr-daemon" \
  --mock "$MOCKDIR" \
  --core nes \
  --backend ra \
  --fps 60 \
  --only-on-change \
  --log-every 0 &
DAEMON_PID=$!

cleanup() {
  kill "$DAEMON_PID" 2>/dev/null || true
}
trap cleanup EXIT

sleep 0.3

echo "== reset values (force false) =="
printf '\x00' | dd of="$FILE" bs=1 seek=$((0x075C)) conv=notrunc status=none  # stage=00
printf '\x00' | dd of="$FILE" bs=1 seek=$((0x075A)) conv=notrunc status=none  # lives=00
printf '\x00' | dd of="$FILE" bs=1 seek=$((0x07ED)) conv=notrunc status=none  # coins low
printf '\x00' | dd of="$FILE" bs=1 seek=$((0x07EE)) conv=notrunc status=none  # coins high
sync

echo "== trigger id=1 (stage -> 01) =="
printf '\x01' | dd of="$FILE" bs=1 seek=$((0x075C)) conv=notrunc status=none
sync
sleep 0.2

echo "== trigger id=2 (lives -> 05) =="
printf '\x05' | dd of="$FILE" bs=1 seek=$((0x075A)) conv=notrunc status=none
sync
sleep 0.2

echo "== trigger id=3 (coins_le16 -> 0005) =="
printf '\x05' | dd of="$FILE" bs=1 seek=$((0x07ED)) conv=notrunc status=none
printf '\x00' | dd of="$FILE" bs=1 seek=$((0x07EE)) conv=notrunc status=none
sync

echo
echo "Done. You should have seen:"
echo "  [ACH] id=1 triggered"
echo "  [ACH] id=2 triggered"
echo "  [ACH] id=3 triggered"
echo
echo "Daemon is still running for 2 seconds so you can see output..."
sleep 2
