#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

VER="${1:-}"
TAG="${2:-}"

if [[ -z "$VER" ]]; then
  echo "Usage: scripts/release_hw_alpha.sh <ver> [tag]" >&2
  echo "Example: scripts/release_hw_alpha.sh 0.1.0-a2 v0.1.0-a2" >&2
  exit 2
fi

if [[ -z "$TAG" ]]; then
  TAG="v${VER}"
fi

# Ensure tag exists
if ! git rev-parse -q --verify "refs/tags/${TAG}" >/dev/null; then
  echo "ERROR: tag not found: ${TAG}" >&2
  echo "Hint: git tag -l | rg 'v0\.1\.0-a'" >&2
  exit 2
fi

DIST="$ROOT/release/dist"
mkdir -p "$DIST"

TARBALL="$DIST/mmr-hw-alpha-${VER}.tar.gz"
SHAFILE="${TARBALL}.sha256"

rm -f "$TARBALL" "$SHAFILE"

# What goes into the Hardware Alpha artifact:
# - daemon/ : userspace daemon
# - kernel/ : shared ABI header + loopback module source
# - docs/   : hardware alpha guide + release notes
# - scripts/: packaging script (so testers can reproduce)
INCLUDE_PATHS=(daemon kernel docs scripts)

# Validate paths exist in the TAG (prevents empty archives)
echo "[release] verifying paths exist in ${TAG}..."
for p in "${INCLUDE_PATHS[@]}"; do
  if ! git ls-tree -d --name-only "${TAG}" "${p}" | rg -q "^${p}$"; then
    echo "ERROR: path '${p}' not present in tag ${TAG} (would produce empty/partial archive)" >&2
    echo "       Check with: git ls-tree -r --name-only ${TAG} | rg '^${p}/' | head" >&2
    exit 2
  fi
done

echo "[release] building artifact: $(basename "$TARBALL")"
git archive --format=tar "${TAG}" "${INCLUDE_PATHS[@]}" | gzip -n > "$TARBALL"

# Sanity checks
bytes="$(wc -c < "$TARBALL" | tr -d ' ')"
if [[ "$bytes" -lt 4096 ]]; then
  echo "ERROR: artifact too small (${bytes} bytes). Refusing to continue." >&2
  echo "       This indicates the archive was empty or near-empty." >&2
  exit 1
fi

echo "[release] listing first entries:"
tar -tzf "$TARBALL" | head -n 40

sha256sum "$TARBALL" > "$SHAFILE"

echo "[release] wrote:"
ls -lh "$TARBALL" "$SHAFILE"

echo "[release] verifying sha256..."
( cd "$DIST" && sha256sum -c "$(basename "$SHAFILE")" )

echo "[release] done."
