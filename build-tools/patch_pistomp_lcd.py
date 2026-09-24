#!/usr/bin/env python3
# Patch pi-Stomp 3.3.x (/opt/pistomp/pi-stomp) for Hex Forge, idempotently:
#   1. LCD: the title bar's snapshot slot shows Hex Forge's "<bank><slot> <preset name>"
#      (from /tmp/hexforge_status, written by the plugin on every preset change), and the four
#      footswitch LEDs light radio-style (the active preset's switch on, the others off).
#   2. Footswitches with midi_CC 60..63 (the Hex Forge preset switches) send CC value 127 on EVERY
#      press. Stock 3.3 treats a CC footswitch as a toggle (127, then 0, then 127...) and lights
#      its LED with that toggle: Hex Forge only acts on values >= 64, so every second press did
#      nothing and the LED showed the toggle instead of the active preset ("stuck" UI, 2026-09-24).
# The pi-Stomp updater replaces modhandler.py on upgrades — re-run this after any pi-Stomp update.
#   scp build-tools/patch_pistomp_lcd.py pistomp@pistomp.local:/tmp/ && ssh pistomp@pistomp.local \
#     "python3 /tmp/patch_pistomp_lcd.py && sudo systemctl restart mod-ala-pi-stomp"
import os, shutil, sys

P = "/opt/pistomp/pi-stomp/modalapi/modhandler.py"
MARK = "HEXFORGE_PRESET_CCS"
s = open(P, encoding="utf-8").read()
if MARK in s:
    print("modhandler.py already carries the Hex Forge patch — nothing to do")
    sys.exit(0)
if not os.path.exists(P + ".orig"):
    shutil.copy(P, P + ".orig")

def rep(old, new, tag):
    global s
    n = s.count(old)
    if n != 1:
        print("ERROR: %s: expected the original text once, found %d — pi-Stomp changed; re-port the patch" % (tag, n))
        sys.exit(1)
    s = s.replace(old, new)

# ── 1. momentary preset switches ─────────────────────────────────────────────
rep("""                        if toggle:
                            controller.toggled = not controller.toggled
                            controller.set_led(controller.toggled)
                            self._emit_midi(controller, 127 if controller.toggled else 0)
""", """                        if toggle and controller.midi_CC in HEXFORGE_PRESET_CCS:
                            # Hex Forge preset switch: momentary (always 127); the LED follows the
                            # active preset from /tmp/hexforge_status (see _poll_hexforge_status).
                            self._emit_midi(controller, 127)
                        elif toggle:
                            controller.toggled = not controller.toggled
                            controller.set_led(controller.toggled)
                            self._emit_midi(controller, 127 if controller.toggled else 0)
""", "MidiCcEffect toggle")

# A footswitch bound (through MOD-UI's MIDI learn) to one of the four Hex Forge preset switch
# ports: the press still flips the toggle (the port alternates 1/0 and the plugin recalls on
# either edge), but its LED is not the toggle — it follows the active preset (radio, below).
rep("""                    if fs is not None:
                        new_toggled = not fs.toggled
                        fs.toggled = new_toggled
                        fs.set_led(new_toggled)
                        if fs.midi_CC is not None:
                            self._emit_midi(fs, 127 if new_toggled else 0)
""", """                    if fs is not None:
                        new_toggled = not fs.toggled
                        fs.toggled = new_toggled
                        if not (fs.parameter is not None and getattr(fs.parameter, "symbol", "") in HEXFORGE_SW_SYMS):
                            fs.set_led(new_toggled)
                        if fs.midi_CC is not None:
                            self._emit_midi(fs, 127 if new_toggled else 0)
""", "ParamEffect toggle")

# ── 2. LCD title + radio LEDs, polled with the LCD ───────────────────────────
rep("""    def poll_lcd_updates(self):
        if self._lcd is not None:
            self._lcd.update_wifi(self.wifi_status)
            self._lcd.poll_updates()
""", """    def poll_lcd_updates(self):
        if self._lcd is not None:
            self._lcd.update_wifi(self.wifi_status)
            self._lcd.poll_updates()
        self._poll_hexforge_status()

    # Hex Forge (guitaramp-suite): the plugin writes "<bank><slot> <name>" (e.g. "1A Clean") to
    # /tmp/hexforge_status on every preset change. Show it in the title bar's snapshot slot and
    # light the preset footswitch LEDs radio-style. Cheap: one stat() per poll, a read on change.
    def _poll_hexforge_status(self):
        try:
            mt = os.stat(HEXFORGE_STATUS_FILE).st_mtime
        except OSError:
            return
        changed = mt != getattr(self, "_hexforge_mtime", None)
        if changed:
            self._hexforge_mtime = mt
            try:
                with open(HEXFORGE_STATUS_FILE) as f:
                    st = f.read().strip()
            except OSError:
                return
            if st and st != getattr(self, "_hexforge_status", None):
                self._hexforge_status = st
                try:
                    if self._lcd is not None:
                        self._lcd.draw_preset(st)
                except Exception:
                    pass
        st = getattr(self, "_hexforge_status", None)
        if not st or len(st) < 2:
            return
        # Radio LEDs, re-asserted every poll: a press (toggle) and mod-ui's value feedback
        # both flip the switch's own LED; the active preset wins within one poll.
        try:
            slot = ord(st[1].upper()) - ord("A")
            hw = self._hardware
            if hw is not None and 0 <= slot < 4:
                dirty = False
                for fs in hw.footswitches:
                    if fs.midi_CC in HEXFORGE_PRESET_CCS:
                        on = (fs.midi_CC - HEXFORGE_PRESET_CCS[0]) == slot
                        if fs.toggled != on or getattr(fs, "_hexforge_led", None) != on:
                            fs.toggled = on
                            fs.set_led(on)
                            fs._hexforge_led = on
                            dirty = True
                if dirty and self._lcd is not None:
                    self._lcd.update_footswitches()
        except Exception:
            pass
""", "poll_lcd_updates")

# module constants (after the imports: first top-level 'class ' keeps them above the handler)
i = s.find("\nclass ")
if i < 0:
    print("ERROR: no class definition found"); sys.exit(1)
s = s[:i] + "\n# Hex Forge preset footswitches (pi-Stomp default config: footswitch 0..3 = CC 60..63)\n" \
    + "HEXFORGE_PRESET_CCS = (60, 61, 62, 63)\nHEXFORGE_SW_SYMS = ('sw_a', 'sw_b', 'sw_c', 'sw_d')\nHEXFORGE_STATUS_FILE = '/tmp/hexforge_status'\n" + s[i:]
if "import os" not in s.split("\nclass ")[0]:
    s = "import os\n" + s
open(P, "w", encoding="utf-8").write(s)
print("patched modhandler.py: Hex Forge LCD title + radio LEDs + momentary preset switches")
