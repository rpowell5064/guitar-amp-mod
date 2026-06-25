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
# NB: detect with `systemctl cat`, NOT `list-unit-files | grep -q`. Under this
# script's `set -o pipefail`, `grep -q` exits on the first match and the large
# `systemctl list-unit-files` producer then dies of SIGPIPE (exit 141), which
# pipefail propagates as a failed condition — so the restart was wrongly skipped.
if command -v systemctl >/dev/null 2>&1 && systemctl cat mod-host.service >/dev/null 2>&1; then
    if sudo systemctl restart mod-host mod-ui; then
        echo "  services restarted"
    else
        echo "  (could not restart automatically — restart MOD manually)"
    fi
else
    echo "  (mod-host service not found — restart your MOD host manually)"
fi

echo "Verifying..."
# Expected count is derived from the installed manifest (one lv2:binary per plugin),
# so adding/removing a plugin never makes this message stale or wrong.
want=$(grep -c 'lv2:binary' "$DEST/guitaramp-suite.lv2/manifest.ttl" 2>/dev/null || echo '?')
if command -v lv2ls >/dev/null 2>&1; then
    n=$(LV2_PATH="$DEST" lv2ls 2>/dev/null | grep -c 'guitaramp-suite' || true)
    echo "  Hex Chain plugins visible to lv2ls: $n (expect $want)"
fi

echo
echo "Done. Open the MOD web UI and search for \"Hex Chain\"."
