#!/bin/bash

echo "[mmr-beta] Checking system..."

if [ ! -e "/dev/mmr_memtap" ]; then
    echo "[FAIL] /dev/mmr_memtap not found."
    echo "Kernel module not loaded."
    exit 1
fi

echo "[OK] memtap device present"

if [ -x "/media/fat/mmr/mmr-daemon" ]; then
    echo "[OK] mmr-daemon installed"
else
    echo "[WARN] mmr-daemon not installed"
fi

echo "[mmr-beta] Check complete."
