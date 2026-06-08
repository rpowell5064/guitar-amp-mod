#!/usr/bin/env bash
# Build the portable aarch64 redistribution tarball on the pi-Stomp.
# Slow on a 2 GB Pi (package_bundle.sh auto-selects -j1). Run on the Pi.
set -uo pipefail
cd ~/guitar-amp-mod || exit 1
VER="${1:-1.1.0}"
echo "=== portable build + package v$VER (slow on 2 GB Pi) ==="
if bash build-tools/package_bundle.sh "$VER"; then
    echo "PKG_OK"
    ls -lh "dist/guitaramp-suite-v${VER}-aarch64.tar.gz"
else
    echo "PKG_FAILED"
fi
