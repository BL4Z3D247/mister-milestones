#!/bin/bash
set -e

echo "[mmr-beta] Installing MiSTer Milestones daemon..."

INSTALL_DIR="/media/fat/mmr"
CONF_DIR="$INSTALL_DIR/conf"

mkdir -p "$INSTALL_DIR"
mkdir -p "$CONF_DIR"

if [ -f "./mmr-daemon" ]; then
    echo "[mmr-beta] Installing bundled mmr-daemon binary"
    cp ./mmr-daemon "$INSTALL_DIR/mmr-daemon"
else
    echo "[mmr-beta] ERROR: mmr-daemon binary not found in beta folder."
    exit 1
fi

cp -r ./conf/* "$CONF_DIR/"

chmod +x "$INSTALL_DIR/mmr-daemon"

echo "[mmr-beta] Installed to $INSTALL_DIR"
echo "[mmr-beta] Run with: /media/fat/mmr/mmr-daemon"
