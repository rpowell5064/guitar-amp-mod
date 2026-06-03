Hex Chain - LV2 guitar amp & effects suite for MOD / pi-Stomp
=============================================================

Nine plugins in one bundle:
  Amp, Cabinet, Drive, Delay, Modulation, Compressor, Reverb, Gate, Input Trim.

REQUIREMENTS
  - A MOD-based host on a Raspberry Pi (pi-Stomp), running a 64-bit OS (aarch64).
  - The standard mod-host + mod-ui stack.
  - Builds as a generic armv8-a binary, so it runs on Pi 4- and Pi 5-class boards.

INSTALL
  1. Copy this folder onto the Pi (scp -r, or unzip it there).
  2. Run:
         bash install.sh
     This copies guitaramp-suite.lv2 into ~/.lv2 and restarts MOD
     (the restart uses sudo).
  3. In the MOD web UI, search for "Hex Chain".

MANUAL INSTALL (if you prefer)
     cp -r guitaramp-suite.lv2 ~/.lv2/
     sudo systemctl restart mod-host mod-ui

UNINSTALL
     rm -rf ~/.lv2/guitaramp-suite.lv2
     sudo systemctl restart mod-host mod-ui

LICENSE
  MIT (see LICENSE). Source: https://github.com/rpowell5064/guitar-amp-mod
