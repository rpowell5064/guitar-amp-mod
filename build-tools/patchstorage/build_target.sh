#!/usr/bin/env bash
# Build the Hex Chain suite for one PatchStorage target and emit per-plugin .lv2
# bundles ready for the patchstorage-lv2-uploader.
#
#   bash build-tools/patchstorage/build_target.sh <target> [SYSROOT]
#
# <target> ∈ { linux-amd64 | rpi-aarch64 | patchbox-os-arm32 }
#   linux-amd64        native x86_64,  NAM ON,  -march=x86-64-v2 (portable)
#   rpi-aarch64        native aarch64, NAM ON,  -march=armv8-a   (Pi 4/5)
#   patchbox-os-arm32  cross armv7hf,  NAM OFF, needs an armhf sysroot (2nd arg
#                      or $ARMHF_SYSROOT)
#
# Output: dist/patchstorage/<target>/<plugin>.lv2/   (12 bundles)
set -euo pipefail

TARGET="${1:?usage: build_target.sh <target> [sysroot]}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

BUILD="build-ps-$TARGET"
STAGE="$(mktemp -d)"
OUT="$ROOT/dist/patchstorage/$TARGET"
cleanup() { rm -rf "$STAGE"; }
trap cleanup EXIT

CMAKE_ARGS=(-DCMAKE_BUILD_TYPE=Release)
STRIP_NAM=""
case "$TARGET" in
    linux-amd64)
        CMAKE_ARGS+=(-DGUITARAMP_PORTABLE=ON) ;;
    rpi-aarch64)
        CMAKE_ARGS+=(-DGUITARAMP_PORTABLE=ON) ;;
    patchbox-os-arm32)
        # Two ways to build the 32-bit target, both NAM-disabled + NAM-stripped:
        #   (a) cross-compile from x86_64/aarch64 with an armhf sysroot (arg 2 or
        #       $ARMHF_SYSROOT) via armhf-toolchain.cmake, or
        #   (b) native build inside an armv7 (arm32v7) container — no sysroot; the
        #       arch baseline comes from GUITARAMP_PORTABLE (-> armv7-a+neon+hard).
        SYSROOT="${2:-${ARMHF_SYSROOT:-}}"
        if [ -n "$SYSROOT" ]; then
            CMAKE_ARGS+=(-DCMAKE_TOOLCHAIN_FILE="$ROOT/armhf-toolchain.cmake"
                         -DCMAKE_SYSROOT="$SYSROOT"
                         -DGUITARAMP_DISABLE_NAM=ON)
        else
            echo "== [$TARGET] no sysroot given — assuming native armv7 host (container)"
            CMAKE_ARGS+=(-DGUITARAMP_PORTABLE=ON -DGUITARAMP_DISABLE_NAM=ON)
        fi
        STRIP_NAM="--strip-nam" ;;
    *)
        echo "!! unknown target '$TARGET' (linux-amd64 | rpi-aarch64 | patchbox-os-arm32)"; exit 1 ;;
esac

echo "== [$TARGET] configure: ${CMAKE_ARGS[*]}"
cmake -S . -B "$BUILD" "${CMAKE_ARGS[@]}" >/dev/null

# Memory-aware parallelism: NAM/Eigen TUs need ~1.5 GB each at -O3 (NAM targets).
JOBS="${JOBS:-}"
if [ -z "$JOBS" ]; then
    memkb=$(awk '/MemTotal/{print $2}' /proc/meminfo 2>/dev/null || echo 0)
    if   [ "$memkb" -lt 3000000 ]; then JOBS=1
    elif [ "$memkb" -lt 6000000 ]; then JOBS=2
    else JOBS=$(nproc); fi
fi
echo "== [$TARGET] build -j$JOBS"
cmake --build "$BUILD" -j"$JOBS"

echo "== [$TARGET] install to staging"
cmake --install "$BUILD" --prefix "$STAGE" >/dev/null
BUNDLE="$STAGE/lib/lv2/guitaramp-suite.lv2"
[ -d "$BUNDLE" ] || { echo "!! staged bundle missing at $BUNDLE"; exit 1; }

echo "== [$TARGET] split into per-plugin bundles -> $OUT"
rm -rf "$OUT"
python3 "$ROOT/build-tools/patchstorage/split_bundle.py" \
        --bundle "$BUNDLE" --out "$OUT" $STRIP_NAM

echo "== [$TARGET] DONE: $(find "$OUT" -maxdepth 1 -name '*.lv2' | wc -l) bundles in $OUT"
