#!/usr/bin/env python3
# One-off: clear Hex Forge's saved preset store (the #preset_blob State property)
# so the 32 slots start empty and the chain order reverts to the (correct) default.
# Footswitch MIDI bindings live in the pedalboard TTL and are NOT touched.
import re, sys

P = "/home/pistomp/data/.pedalboards/Hex_Forge.pedalboard/effect-2/effect.ttl"
s = open(P).read()
# Empty the base64 value (stored as """...."""^^xsd:base64Binary). A 0-byte blob
# fails the plugin's size check on restore → all 32 slots come back empty.
new, n = re.subn(
    r'(#preset_blob>\s*""")[^"]*("""\^\^xsd:base64Binary)',
    r"\1\2", s, flags=re.DOTALL)
if n == 0:
    print("no preset_blob found (already clear?)"); sys.exit(0)
open(P, "w").write(new)
print("cleared preset_blob (%d removed)" % n)
