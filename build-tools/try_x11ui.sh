#!/usr/bin/env bash
# Open a Hex Chain plugin in its native X11 panel on this machine, exactly the
# way Zynthian's VNC desktop does (jalv.gtk3 --jack-name). Runs under WSL
# (WSLg provides the X server) or any desktop Linux.
#
#   bash build-tools/try_x11ui.sh gate          # any key: amp cab comp delay drive fuzz gate
#                                              #   hexforge modfx nail octave practice reverb utility wah
#   bash build-tools/try_x11ui.sh drive model=3 # start with port values set
#   REBUILD=1 bash build-tools/try_x11ui.sh gate
#   VNC=1 bash build-tools/try_x11ui.sh gate    # view over VNC in a browser, like Zynthian
#
# One-time packages (Debian/Ubuntu):
#   sudo apt install build-essential cmake pkg-config lv2-dev libx11-dev libcairo2-dev jalv jackd2
#
# JACK runs on the dummy driver, so there is no audio: this tests the panel
# (drag, scroll, double-click reset, power, dropdowns, file pickers, meters).
set -euo pipefail

key="${1:-gate}"; shift || true
src="$(cd "$(dirname "$0")/.." && pwd)"
build="${HC_X11_BUILD:-$HOME/hc-x11/build}"
stage="${HC_X11_STAGE:-$HOME/hc-x11/stage}"
bundle="$stage/lib/lv2/guitaramp-suite.lv2"

if [[ ! -f "$bundle/hexchain_ui.so" || "${REBUILD:-0}" == 1 ]]; then
    echo "Building the Zynthian flavour into $build (first run takes a while)..."
    cmake -S "$src" -B "$build" -DCMAKE_BUILD_TYPE=Release -DHEXCHAIN_X11UI=ON >/dev/null
    cmake --build "$build" -j"$(nproc)"
    rm -rf "$stage"
    cmake --install "$build" --prefix "$stage" >/dev/null
fi

uri="https://rpowell5064.github.io/guitaramp-suite/$key"
grep -q "<$uri>" "$bundle/manifest.ttl" || { echo "unknown plugin key: $key" >&2; exit 1; }

if ! pgrep -x jackd >/dev/null; then
    jackd -d dummy -r 48000 -p 256 >/dev/null 2>&1 &
    sleep 1
fi

ctl=()
for kv in "$@"; do ctl+=(-c "$kv"); done

# WSLg's Wayland backend breaks GtkPlug embedding; force X11.
export LV2_PATH="$stage/lib/lv2" GDK_BACKEND=x11

if [[ "${VNC:-0}" == 1 ]]; then
    # Like Zynthian: a private X display served over VNC, viewed in a browser
    # through noVNC. Also the way round a WSLg stuck in "copy mode" (blank windows).
    #   sudo apt install xvfb x11vnc novnc websockify
    pkill -f "Xvfb :7" 2>/dev/null || true; pkill -f "x11vnc.*:7" 2>/dev/null || true
    pkill -f "websockify.*6087" 2>/dev/null || true
    Xvfb :7 -screen 0 1920x1200x24 >/dev/null 2>&1 &
    sleep 1
    env -u WAYLAND_DISPLAY -u XDG_SESSION_TYPE x11vnc -display :7 -rfbport 5907 -forever -shared -nopw -quiet >/dev/null 2>&1 &
    websockify --web /usr/share/novnc 6087 localhost:5907 >/dev/null 2>&1 &
    sleep 1
    echo "Open http://localhost:6087/vnc.html?autoconnect=1&resize=remote in your browser. Ctrl+C here to stop."
    trap 'pkill -f "Xvfb :7"; pkill -f "x11vnc.*:7"; pkill -f "websockify.*6087"' EXIT
    DISPLAY=:7 jalv.gtk3 --jack-name "hc-$key" "${ctl[@]}" "$uri"
    exit
fi

exec jalv.gtk3 --jack-name "hc-$key" "${ctl[@]}" "$uri"
