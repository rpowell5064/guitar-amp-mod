# Add the Hex Chain logo badge (bottom-left, black dropped via mix-blend-mode:screen)
# to all 9 modguis. Per-pedal position overrides live in POS.
import io
ROOT = r"C:\Development\Projects\guitar-amp-mod\lv2"

# bottom-left default; override (left,bottom,size) where lower-left would be obscured.
# (left, bottom, size) per pedal — sized as big as the clear bottom-left area allows.
POS = {
    "amp":     (16, 14, 120),
    "cab":     (14, 12, 94),
    "delay":   (14, 12, 94),
    "reverb":  (14, 12, 90),
    "modfx":   (14, 12, 88),
    "comp":    (14, 12, 80),
    "gate":    (14, 12, 74),
    "drive":   (14, 12, 74),
    "utility": (14, 12, 66),
}

CSS_TMPL = """

/* HEX CHAIN logo badge (black background dropped via screen blend) */
.mod-pedal-guitaramp-@P@{{{cns}}} .hx-logo {
    position: absolute; left: @L@px; bottom: @B@px; width: @S@px; height: @S@px;
    background-image: url(/resources/logo.jpg{{{ns}}});
    background-size: contain; background-repeat: no-repeat; background-position: center;
    mix-blend-mode: screen; pointer-events: none; z-index: 4;
}"""

ANCHOR = '    <div class="mod-pedal-input">'

for p, (L, B, S) in POS.items():
    base = f"{ROOT}\\modgui-{p}"
    cssfp = f"{base}\\stylesheet-{p}.css"
    htmlfp = f"{base}\\icon-{p}.html"

    css = io.open(cssfp, encoding="utf-8").read()
    if ".hx-logo" in css:
        # replace existing block (idempotent re-runs / position tweaks)
        head = css.split("\n\n/* HEX CHAIN logo badge")[0]
        css = head
    block = (CSS_TMPL.replace("@P@", p).replace("@L@", str(L))
                     .replace("@B@", str(B)).replace("@S@", str(S)))
    css = css.rstrip() + "\n" + block + "\n"
    io.open(cssfp, "w", encoding="utf-8", newline="\n").write(css)

    html = io.open(htmlfp, encoding="utf-8").read()
    if '"hx-logo"' not in html:
        assert ANCHOR in html, f"{p}: anchor not found"
        html = html.replace(ANCHOR, '    <div class="hx-logo"></div>\n\n' + ANCHOR, 1)
        io.open(htmlfp, "w", encoding="utf-8", newline="\n").write(html)
    print(f"OK {p}: logo at left={L} bottom={B} size={S}")

print("DONE")
