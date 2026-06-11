Hex Chain - LV2 guitar amp & effects suite for MOD / pi-Stomp
=============================================================

Eleven plugins in one bundle:
  Amp, Cabinet, Drive, Delay, Modulation, Compressor, Reverb, Gate, Input Trim,
  Fuzz, and Hex Forge (the whole chain in one reorderable super-block).

REQUIREMENTS
  - A MOD-based host on a Raspberry Pi (pi-Stomp), running a 64-bit OS (aarch64).
  - The standard mod-host + mod-ui stack.
  - Builds as a generic armv8-a binary, so it runs on Pi 4- and Pi 5-class boards.

INSTALL
  1. Copy this folder onto the Pi (scp -r, or unzip it there).
  2. Run:
         bash install.sh
     This copies guitaramp-suite.lv2 into ~/.lv2, installs the factory
     "Hex Forge" pedalboard, and restarts MOD (the restart uses sudo).
  3. In the MOD web UI, search for "Hex Chain", or load the "Hex Forge"
     pedalboard to get the full chain ready to play.

FOOTSWITCHES (pi-Stomp)
  The bundled "Hex Forge" pedalboard comes pre-mapped: the four footswitches
  (A/B/C/D) recall the four presets in the current bank, and double-tapping A
  or D moves a bank down/up. No MIDI setup is required — it works on load.
  (If you build your own pedalboard from scratch, map the switches once via the
  MOD web UI: right-click each A/B/C/D switch -> MIDI -> Learn -> tap the pedal.)

MANUAL INSTALL (if you prefer)
     cp -r guitaramp-suite.lv2 ~/.lv2/
     sudo systemctl restart mod-host mod-ui

UNINSTALL
     rm -rf ~/.lv2/guitaramp-suite.lv2
     rm -rf ~/data/.pedalboards/Hex_Forge.pedalboard   # or ~/.pedalboards/...
     sudo systemctl restart mod-host mod-ui

LICENSE
  GPL-3.0-or-later, or a commercial license (dual-licensed; see LICENSE).
  Source: https://github.com/rpowell5064/guitar-amp-mod
