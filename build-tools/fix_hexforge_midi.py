#!/usr/bin/env python3
# DEV/PACKAGING HELPER — end users do NOT run this. It bakes the pi-Stomp
# footswitch CC bindings into a pedalboard's TTL so the switches drive Hex Forge.
# The shipped factory pedalboard (pedalboards/Hex_Forge.pedalboard, installed by
# install.sh) already carries these bindings, so a normal install needs nothing.
# Use this only to (re)generate that mapping in a fresh/hand-built pedalboard.
#
# CC60->sw_a, CC61->sw_b, CC62->sw_c, CC63->sw_d (channel 13 = MIDI ch 14).
# Rebuilds each <hexforge/sw_X> block so any prior (wrong) binding is replaced.
import re, sys

P = "/home/pistomp/data/.pedalboards/Hex_Forge.pedalboard/Hex_Forge.ttl"
s = open(P).read()

def binding(cc):
    return ("    midi:binding [\n"
            "        midi:channel 13 ;\n"
            "        midi:controllerNumber %d ;\n"
            "        lv2:minimum 0.000000 ;\n"
            "        lv2:maximum 1.000000 ;\n"
            "        a midi:Controller ;\n"
            "    ] ;\n" % cc)

for sw, cc in [("sw_a", 60), ("sw_b", 61), ("sw_c", 62), ("sw_d", 63)]:
    pat = re.compile(
        r"(<hexforge/%s>\n    ingen:value [0-9.]+ ;\n)(.*?)(    a lv2:ControlPort ,\n        lv2:InputPort \.)" % sw,
        re.DOTALL)
    s, n = pat.subn(r"\1" + binding(cc) + r"\3", s)
    if n != 1:
        print("ERROR: %s matched %d times" % (sw, n)); sys.exit(1)

open(P, "w").write(s)
print("OK: bound sw_a..sw_d to CC 60..63")
