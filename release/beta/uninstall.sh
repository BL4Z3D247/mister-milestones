#!/bin/bash
set -e

INSTALL_DIR="/media/fat/mmr"

echo "[mmr-beta] Removing MiSTer Milestones..."

if [ -d "$INSTALL_DIR" ]; then
    rm -rf "$INSTALL_DIR"
    echo "[mmr-beta] Removed $INSTALL_DIR"
else
    echo "[mmr-beta] Nothing installed."
fi
