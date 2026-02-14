#!/bin/bash

INSTALL_DIR="/media/fat/mmr"
CONF_FILE="$INSTALL_DIR/conf/mmr.env"

if [ ! -f "$INSTALL_DIR/mmr-daemon" ]; then
    echo "[mmr-beta] mmr-daemon not installed."
    exit 1
fi

if [ -f "$CONF_FILE" ]; then
    source "$CONF_FILE"
fi

CORE=${MMR_CORE:-nes}
BACKEND=${MMR_BACKEND:-ra}
FPS=${MMR_FPS:-60}
ONLY=${MMR_ONLY_ON_CHANGE:---only-on-change}
LOGEVERY=${MMR_LOG_EVERY:-0}

echo "[mmr-beta] Starting daemon..."
echo "  core=$CORE"
echo "  backend=$BACKEND"
echo "  fps=$FPS"

"$INSTALL_DIR/mmr-daemon" \
    --dev /dev/mmr_memtap \
    --core "$CORE" \
    --backend "$BACKEND" \
    --fps "$FPS" \
    $ONLY \
    --log-every "$LOGEVERY"
