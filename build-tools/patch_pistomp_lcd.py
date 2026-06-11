#!/usr/bin/env python3
# Patch pi-Stomp's v3 (Tre) handler (modhandler.py) to, in the idle poll loop:
#   1. show Hex Forge's current bank/slot/preset name on the LCD, and
#   2. radio-light the four footswitches (the active preset's switch on, others off)
# from /tmp/hexforge_status (written by the plugin). Run after restoring the .bak.
import sys

P = "/home/pistomp/pi-stomp/modalapi/modhandler.py"
s = open(P).read()

old = ("    def poll_lcd_updates(self):\n"
       "        if self.lcd:\n"
       "            self.lcd.poll_updates()")
new = ("    def poll_lcd_updates(self):\n"
       "        if self.lcd:\n"
       "            self.lcd.poll_updates()\n"
       "        # Hex Forge: LCD preset name + radio LEDs for the 4 preset switches.\n"
       "        try:\n"
       "            with open('/tmp/hexforge_status') as f:\n"
       "                st = f.read().strip()\n"
       "        except Exception:\n"
       "            return\n"
       "        if not st:\n"
       "            return\n"
       "        if st != getattr(self, '_hexforge_status', None):\n"
       "            self._hexforge_status = st\n"
       "            try:\n"
       "                if self.lcd is not None:\n"
       "                    self.lcd.draw_preset(st)\n"
       "            except Exception:\n"
       "                pass\n"
       "        try:\n"
       "            if len(st) >= 2 and self.hardware is not None:\n"
       "                slot = ord(st[1].upper()) - ord('A')\n"
       "                if 0 <= slot < 4:\n"
       "                    fss = self.hardware.footswitches\n"
       "                    for i in range(min(4, len(fss))):\n"
       "                        fss[i]._set_led(i == slot)\n"
       "        except Exception:\n"
       "            pass")

if s.count(old) != 1:
    print("ERROR: original poll_lcd_updates not found uniquely (%d). Restore .bak first." % s.count(old))
    sys.exit(1)
open(P, "w").write(s.replace(old, new))
print("patched modhandler.py OK (LCD + radio LEDs)")
