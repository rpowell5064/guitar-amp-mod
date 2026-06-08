# Phase 0: add doap:/foaf: prefixes + doap:license (MIT) + doap:maintainer to all 9 ttls.
import io, glob, os, sys

PREFIX_ANCHOR = "@prefix modgui:<http://moddevices.com/ns/modgui#> ."
PREFIX_ADD = ("@prefix doap:  <http://usefulinc.com/ns/doap#> .\n"
              "@prefix foaf:  <http://xmlns.com/foaf/0.1/> .")

BRAND_ANCHOR = '    mod:brand  "Hex Chain" ;'
META_ADD = ('    doap:license <https://www.gnu.org/licenses/gpl-3.0.html> ;\n'
            '    doap:maintainer [ a foaf:Person ; foaf:name "Ryan Powell" ;\n'
            '                      foaf:homepage <https://rpowell5064.github.io/guitaramp-suite/> ] ;')

ROOT = r"C:\Development\Projects\guitar-amp-mod\lv2"
for fp in sorted(glob.glob(os.path.join(ROOT, "*.ttl"))):
    name = os.path.basename(fp)
    if name == "manifest.ttl":
        continue
    txt = io.open(fp, encoding="utf-8").read()
    if "doap:license" in txt:
        print(f"skip {name} (already has doap:license)"); continue
    assert txt.count(PREFIX_ANCHOR) == 1, f"{name}: prefix anchor count != 1"
    assert txt.count(BRAND_ANCHOR) == 1, f"{name}: brand anchor count != 1"
    txt = txt.replace(PREFIX_ANCHOR, PREFIX_ANCHOR + "\n" + PREFIX_ADD, 1)
    txt = txt.replace(BRAND_ANCHOR, BRAND_ANCHOR + "\n" + META_ADD, 1)
    io.open(fp, "w", encoding="utf-8", newline="\n").write(txt)
    print(f"OK {name}")
print("DONE")
