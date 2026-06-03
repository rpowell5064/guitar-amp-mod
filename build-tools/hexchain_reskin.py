# Apply the Hex Chain flat-futuristic reskin to the 8 non-amp modguis.
# (amp was done by hand as the template.) Literal find/replace with assertions.
import sys, io
ROOT = r"C:\Development\Projects\guitar-amp-mod\lv2"

HEXPATH = ("M13.99%209.25l13%207.5v15l-13%207.5L1%2031.75v-15l12.99-7.5zM3%2017.9v12.7l10.99"
           "%206.34%2011-6.35V17.9l-11-6.34L3%2017.9zM0%2015l12.98-7.5V0h-2v6.35L0%2012.69v2.3"
           "zm0%2018.5L12.98%2041v8h-2v-6.85L0%2035.81v-2.3zM15%200v7.5L27.99%2015H28v-2.31h-.01"
           "L17%206.35V0h-2zm0%2049v-8l12.99-7.5H28v2.31h-.01L17%2042.15V49h-2z")

NEWROOT = """.mod-pedal-guitaramp-@P@{{{cns}}} {
    /* Hex Chain flat-futuristic panel: dark base + accent-tinted hex-grid
       watermark + top accent glow. (Branding rendered in CSS, not baked PNG.) */
    background-color: #0b0d12;
    background-image:
        url("data:image/svg+xml,%3Csvg%20width='28'%20height='49'%20viewBox='0%200%2028%2049'%20xmlns='http://www.w3.org/2000/svg'%3E%3Cg%20fill='%23@HEX@'%20fill-opacity='0.06'%20fill-rule='evenodd'%3E%3Cpath%20d='@PATH@'/%3E%3C/g%3E%3C/svg%3E"),
        radial-gradient(150% 70% at 50% -12%, rgba(@R@,@G@,@B@,.14), rgba(11,13,18,0) 55%),
        linear-gradient(180deg, #0d1016 0%, #070809 100%);
    background-repeat: repeat, no-repeat, no-repeat;
    background-position: center top, center top, center top;
    background-size: 28px 49px, 100% 100%, 100% 100%;
    height: @H@px;
    width: @W@px;
    position: absolute;
    border-radius: 12px;
    border-top: 2px solid #@HEX@;
    box-shadow: inset 0 0 0 1px rgba(255,255,255,.05), inset 0 -50px 70px rgba(0,0,0,.45);
}

/* HEX CHAIN HEADER (brand wordmark + effect title + tagline) */
.mod-pedal-guitaramp-@P@{{{cns}}} .hx-header { position:absolute; top:8px; left:24px; right:24px; z-index:5; pointer-events:none; border-bottom:1px solid rgba(@R@,@G@,@B@,.28); padding-bottom:8px; }
.mod-pedal-guitaramp-@P@{{{cns}}} .hx-brand { font-family:"Helvetica Neue",Helvetica,Arial,sans-serif; font-size:10px; font-weight:800; letter-spacing:3px; line-height:1; text-transform:uppercase; background:linear-gradient(90deg,#19e0ff 0%,#ff2bd6 100%); -webkit-background-clip:text; background-clip:text; -webkit-text-fill-color:transparent; color:transparent; }
.mod-pedal-guitaramp-@P@{{{cns}}} .hx-mark { display:inline-block; width:9px; height:10px; margin-right:7px; vertical-align:-1px; background:linear-gradient(135deg,#19e0ff 0%,#ff2bd6 100%); clip-path:polygon(50% 0,100% 25%,100% 75%,50% 100%,0 75%,0 25%); }
.mod-pedal-guitaramp-@P@{{{cns}}} .hx-title { font-family:"Helvetica Neue",Helvetica,Arial,sans-serif; font-size:@TF@px; font-weight:800; letter-spacing:2px; line-height:1; margin-top:7px; text-transform:uppercase; color:#@HEX@; text-shadow:0 0 16px rgba(@R@,@G@,@B@,.45); }
.mod-pedal-guitaramp-@P@{{{cns}}} .hx-tag { font-family:"Helvetica Neue",Helvetica,Arial,sans-serif; font-size:9px; font-weight:600; letter-spacing:.6px; line-height:1; margin-top:7px; color:#828893; }"""

# pedal -> dict of params
P = {
 "cab":    dict(hex="56aaff", rgb=(86,170,255),  w=360, h=403, tf=24, title="CABINET",    tag="IR loader · low / high cut"),
 "drive":  dict(hex="eb5046", rgb=(235,80,70),   w=360, h=300, tf=26, title="DRIVE",      tag="TS-808 · Life Pedal · ProCo RAT"),
 "delay":  dict(hex="3cc8be", rgb=(60,200,190),  w=480, h=360, tf=26, title="DELAY",      tag="digital · tape · echorec"),
 "gate":   dict(hex="8c9baf", rgb=(140,155,175), w=360, h=300, tf=26, title="GATE",       tag="noise gate · hysteresis"),
 "comp":   dict(hex="4687eb", rgb=(70,135,235),  w=420, h=340, tf=20, title="COMPRESSOR", tag="VCA · 1176-style"),
 "modfx":  dict(hex="a56eeb", rgb=(165,110,235), w=400, h=300, tf=22, title="MODULATION", tag="CE-2 chorus · uni-vibe"),
 "reverb": dict(hex="5f73e1", rgb=(95,115,225),  w=480, h=320, tf=26, title="REVERB",     tag="modulated reverb"),
 "utility":dict(hex="6eaf87", rgb=(110,175,135), w=320, h=240, tf=20, title="INPUT TRIM", tag="gain · phase · hum filter"),
}

# exact existing root blocks (verified by reading the files)
OLDROOT_MULTILINE = """.mod-pedal-guitaramp-@P@{{{cns}}} {
    background-image: url(/resources/pedals/@P@.png{{{ns}}});
    background-position: center center;
    background-repeat: no-repeat;
    background-size: @W@px @H@px;
    height: @H@px;
    width: @W@px;
    position: absolute;
    border-radius: 12px;
}"""
OLDROOT_COMPACT = """.mod-pedal-guitaramp-@P@{{{cns}}} {
    background-image: url(/resources/pedals/@P@.png{{{ns}}});
    background-position:center center; background-repeat:no-repeat;
    background-size:@W@px @H@px; height:@H@px; width:@W@px; position:absolute; border-radius:12px;
}"""

MULTILINE = {"cab","drive","delay"}

POWERSWITCH = """    <div class="mod-powerswitch" mod-role="bypass">
        <div class="mod-powerswitch-image" mod-role="bypass-light"></div>
    </div>"""

def sub(tpl, d):
    return (tpl.replace("@PATH@", HEXPATH).replace("@P@", d["_p"]).replace("@HEX@", d["hex"])
               .replace("@R@", str(d["rgb"][0])).replace("@G@", str(d["rgb"][1]))
               .replace("@B@", str(d["rgb"][2])).replace("@W@", str(d["w"]))
               .replace("@H@", str(d["h"])).replace("@TF@", str(d["tf"])))

def must(cond, msg):
    if not cond:
        print("ASSERT FAIL:", msg); sys.exit(1)

for p, d in P.items():
    d["_p"] = p
    base = f"{ROOT}\\modgui-{p}"
    cssfp = f"{base}\\stylesheet-{p}.css"
    htmlfp = f"{base}\\icon-{p}.html"

    # ---- CSS ----
    css = io.open(cssfp, encoding="utf-8").read()
    oldroot = sub(OLDROOT_MULTILINE if p in MULTILINE else OLDROOT_COMPACT, d)
    must(oldroot in css, f"{p}: old root block not found")
    css = css.replace(oldroot, sub(NEWROOT, d))

    # margin bumps
    if p in ("drive","delay"):
        old = ".mod-amp-modelrow { margin-top: 60px;"; new = ".mod-amp-modelrow { margin-top: 80px;"
    elif p in ("comp","modfx"):
        old = ".mod-amp-modelrow { margin-top:58px;"; new = ".mod-amp-modelrow { margin-top:80px;"
    elif p == "cab":
        old = "margin-top: 72px;"; new = "margin-top: 80px;"
    else:  # gate, reverb, utility -> knobs become first row
        old = ".mod-knobs { margin-top:8px;"; new = ".mod-knobs { margin-top:80px;"
    must(css.count(old) == 1, f"{p}: margin anchor '{old}' count={css.count(old)} (want 1)")
    css = css.replace(old, new)
    io.open(cssfp, "w", encoding="utf-8", newline="\n").write(css)

    # ---- HTML ----
    html = io.open(htmlfp, encoding="utf-8").read()
    must(POWERSWITCH in html, f"{p}: powerswitch block not found in html")
    header = ("\n\n    <div class=\"hx-header\">\n"
              "        <div class=\"hx-brand\"><span class=\"hx-mark\"></span>HEX CHAIN</div>\n"
              f"        <div class=\"hx-title\">{d['title']}</div>\n"
              f"        <div class=\"hx-tag\">{d['tag']}</div>\n"
              "    </div>")
    html = html.replace(POWERSWITCH, POWERSWITCH + header, 1)

    if p in ("gate","reverb","utility"):
        # remove the redundant titlerow (effect name now lives in the header)
        import re
        new_html = re.sub(r'\n\s*<div class="mod-titlerow"><span class="mod-section-label">[^<]+</span></div>', "", html)
        must(new_html != html, f"{p}: titlerow not found to remove")
        html = new_html
    if p == "comp":
        must('<span class="mod-section-label">COMPRESSOR</span>' in html, "comp: section label")
        html = html.replace('<span class="mod-section-label">COMPRESSOR</span>', '<span class="mod-section-label">MODEL</span>')
    if p == "modfx":
        must('<span class="mod-section-label">MODULATION</span>' in html, "modfx: section label")
        html = html.replace('<span class="mod-section-label">MODULATION</span>', '<span class="mod-section-label">MODEL</span>')

    io.open(htmlfp, "w", encoding="utf-8", newline="\n").write(html)
    print(f"OK {p}: css+html reskinned (title={d['title']}, accent #{d['hex']})")

print("ALL DONE")
