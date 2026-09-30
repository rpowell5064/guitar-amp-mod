#!/usr/bin/env bash
# (Re)construct the guitaramp-suite LV2 bundle in the pi-Stomp plugin dir from the
# freshly-built .so files + the source .ttl manifests, then validate.
# Run on the Pi: bash ~/deploy_pi.sh
set -uo pipefail
cd ~/guitar-amp-mod || exit 1
BUNDLE=~/.lv2/guitaramp-suite.lv2

echo "=== (re)constructing bundle at $BUNDLE ==="
mkdir -p "$BUNDLE"
# ATOMIC copies. Writing over a .so that mod-host currently has mmap'd faults
# the running process (SIGBUS, status=7/BUS) and crash-loops it — this cost a
# whole session once, masquerading as "pistomp.local isn't loading". Copying to
# a temp name and renaming keeps the old inode mapped for anyone still using it,
# so the running host is undisturbed until it is restarted.
install_atomic() {
    src="$1"; dst="$2"
    cp "$src" "$dst.new" && mv -f "$dst.new" "$dst"
}
for f in build/guitaramp_*.so; do install_atomic "$f" "$BUNDLE/$(basename "$f")"; done
for f in lv2/*.ttl;            do install_atomic "$f" "$BUNDLE/$(basename "$f")"; done
# Practice: the resynthesised kit (parameters, not sample audio). The plugin
# finds it via the bundle_path handed to instantiate().
[ -f lv2/practice/drumkit.dat ] && install_atomic lv2/practice/drumkit.dat "$BUNDLE/drumkit.dat"
echo "copied $(ls "$BUNDLE"/*.so | wc -l) .so, $(ls "$BUNDLE"/*.ttl | wc -l) .ttl, kit=$([ -f "$BUNDLE/drumkit.dat" ] && echo yes || echo no)"

# modgui resources. Copied FILE BY FILE, never "rm -rf dir && cp -r": if this
# checkout is missing an image the bundle already has, a wipe-and-replace
# deletes it from the bundle and the panel loses its artwork. Overwriting in
# place can only ever add or update, never remove.
# Note mod-ui caches modgui files against the plugin's lv2:microVersion, so a
# changed stylesheet that did not bump it will look unchanged on screen even
# though the md5s here match.
guis=0
for d in lv2/modgui-*; do
    [ -d "$d" ] || continue
    name=$(basename "$d")
    mkdir -p "$BUNDLE/$name"
    (cd "$d" && find . -type d -printf '%P\n') | while read -r sub; do
        [ -n "$sub" ] && mkdir -p "$BUNDLE/$name/$sub"
    done
    (cd "$d" && find . -type f -printf '%P\n') | while read -r f; do
        install_atomic "$d/$f" "$BUNDLE/$name/$f"
    done
    guis=$((guis + 1))
done
echo "copied $guis modgui dir(s)"

echo "--- bundle contents ---"
ls -la --time-style=+%Y-%m-%d_%H:%M:%S "$BUNDLE"/

echo "=== manifest sanity: plugin .so referenced by manifest all present? ==="
for so in $(grep -oE 'guitaramp_[a-z]+\.so' "$BUNDLE"/manifest.ttl | sort -u); do
    if [ -f "$BUNDLE/$so" ]; then echo "  OK   $so"; else echo "  MISS $so"; fi
done

echo "=== lv2ls enumeration (expect 9 URIs) ==="
LV2_PATH=/home/pistomp/.lv2 lv2ls 2>/dev/null | grep guitaramp-suite | sort

echo "=== running MOD / jack services ==="
systemctl list-units --type=service --state=running --no-legend --no-pager \
    | grep -iE "mod|jack|pistomp|browse" | awk '{print $1}'

echo "=== DONE ==="
