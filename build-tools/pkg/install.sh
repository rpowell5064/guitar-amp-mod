#!/usr/bin/env bash
# Install the Hex Chain LV2 suite for the current user, then reload MOD.
#   bash install.sh
# Override the destination with: LV2_DEST=/some/lv2/dir bash install.sh
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
DEST="${LV2_DEST:-$HOME/.lv2}"

echo "Installing guitaramp-suite.lv2 -> $DEST"
mkdir -p "$DEST"
rm -rf "$DEST/guitaramp-suite.lv2"
cp -r "$HERE/guitaramp-suite.lv2" "$DEST/"

# Factory pedalboard(s): Hex Forge ships pre-wired so the pi-Stomp footswitches
# (CC 60-63 -> A/B/C/D) work the moment you load it — no MIDI setup needed.
# Installed non-destructively: an existing pedalboard of the same name is kept.
if [ -d "$HERE/pedalboards" ]; then
    if [ -d "$HOME/data/.pedalboards" ]; then PB="$HOME/data/.pedalboards"   # pi-Stomp layout
    else PB="$HOME/.pedalboards"; fi
    mkdir -p "$PB"
    for pb in "$HERE"/pedalboards/*.pedalboard; do
        [ -e "$pb" ] || continue
        name="$(basename "$pb")"
        if [ -e "$PB/$name" ]; then
            echo "Pedalboard $name already present in $PB — leaving your copy untouched"
        else
            cp -r "$pb" "$PB/" && echo "Installed factory pedalboard: $name -> $PB"
        fi
    done
fi

echo "Restarting MOD (mod-host, mod-ui)..."
if command -v systemctl >/dev/null 2>&1 && systemctl list-unit-files 2>/dev/null | grep -q '^mod-host'; then
    sudo systemctl restart mod-host mod-ui \
        && echo "  services restarted" \
        || echo "  (could not restart automatically — restart MOD manually)"
else
    echo "  (mod-host service not found — restart your MOD host manually)"
fi

echo "Verifying..."
if command -v lv2ls >/dev/null 2>&1; then
    n=$(LV2_PATH="$DEST" lv2ls 2>/dev/null | grep -c 'guitaramp-suite' || true)
    echo "  Hex Chain plugins visible to lv2ls: $n (expect 9)"
fi

echo
echo "Done. Open the MOD web UI and search for \"Hex Chain\"."
