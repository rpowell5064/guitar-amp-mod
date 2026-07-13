# Regenerate screenshot-<p>.png + thumbnail-<p>.png for every Hex Chain pedal by rendering the REAL
# icon-<p>.html against the REAL stylesheet, with the Mustache filled from the plugin's TTL port data
# (knob labels {{name}}, symbols, dropdown {{#scalePoints}}). Headless Chrome, file:// sprite paths.
# The pedals were redesigned to the hf-plate style, so the old representative-HTML renderer is obsolete.
# Run: python build-tools/render_modguis.py
import os, re, subprocess, shutil

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LV2  = os.path.join(REPO, "lv2")
OUT  = os.path.join(os.environ["TEMP"], "hxshots"); os.makedirs(OUT, exist_ok=True)
CHROME = r"C:\Program Files\Google\Chrome\Application\chrome.exe"
THUMB_W = 280

# pedal -> (ttl basename, width, height). Sizes are the .mod-pedal root w/h in each stylesheet.
PEDALS = {
    "amp":    ("amp.ttl",     820, 600),
    "cab":    ("cab.ttl",     540, 424),
    "drive":  ("drive.ttl",   500, 430),
    "delay":  ("delay.ttl",   540, 576),
    "gate":   ("gate.ttl",    360, 340),
    "comp":   ("comp.ttl",    500, 446),
    "modfx":  ("modfx.ttl",   460, 430),
    "reverb": ("reverb.ttl",  480, 506),
    "utility":("utility.ttl", 440, 376),
    "fuzz":   ("fuzz.ttl",    420, 448),
    "nail":   ("nail.ttl",    360, 415),
    "octave": ("octave.ttl",  400, 490),
    "wah":    ("wah.ttl",     460, 440),
}

# ── TTL -> ordered control-input ports (what MOD exposes as `controls`) ───────────────────────────
def parse_controls(ttl_path):
    txt = open(ttl_path, encoding="utf-8").read()
    # Walk top-level [ ... ] blocks with bracket matching (port blocks contain nested scalePoint [...]).
    ctrls = []; i = 0; n = len(txt)
    while True:
        j = txt.find('[', i)
        if j < 0:
            break
        depth = 0; k = j
        while k < n:
            if txt[k] == '[': depth += 1
            elif txt[k] == ']':
                depth -= 1
                if depth == 0:
                    break
            k += 1
        block = txt[j + 1:k]; i = k + 1
        if "ControlPort" not in block or "InputPort" not in block:
            continue
        mi = re.search(r"lv2:index\s+(\d+)", block)
        ms = re.search(r'lv2:symbol\s+"([^"]*)"', block)
        mn = re.search(r'lv2:name\s+"([^"]*)"', block)
        if not (mi and ms and mn):
            continue
        # scalePoints: label + value in either order, one per nested block
        sp = []
        for spb in re.findall(r'rdfs:label\s+"([^"]*)"\s*;\s*rdf:value\s+([0-9.+-]+)', block):
            sp.append((spb[0], spb[1]))
        if not sp:
            for spb in re.findall(r'rdf:value\s+([0-9.+-]+)\s*;\s*rdfs:label\s+"([^"]*)"', block):
                sp.append((spb[1], spb[0]))
        ctrls.append((int(mi.group(1)), {"symbol": ms.group(1), "name": mn.group(1), "sp": sp}))
    ctrls.sort(key=lambda x: x[0])
    return [c for _, c in ctrls]

# ── Mustache-lite: fill {{#controls.N}} blocks from the port list, drop runtime-only loops ────────
def fill_mustache(html, controls):
    def ctrl_block(m):
        idx = int(m.group(1)); body = m.group(2)
        if idx >= len(controls):
            return ""                                   # control doesn't exist -> drop the block
        c = controls[idx]
        def sp(mm):
            inner = mm.group(1)
            return "".join(inner.replace("{{value}}", v).replace("{{label}}", l) for (l, v) in c["sp"])
        body = re.sub(r"\{\{#scalePoints\}\}(.*?)\{\{/scalePoints\}\}", sp, body, flags=re.S)
        body = body.replace("{{name}}", c["name"]).replace("{{symbol}}", c["symbol"])
        if c["sp"]:                                     # show the first option in the selected box
            body = body.replace('class="mod-enumerated-selected"></div>',
                                'class="mod-enumerated-selected">' + c["sp"][0][0] + '</div>', 1)
        return body
    html = re.sub(r"\{\{#controls\.(\d+)\}\}(.*?)\{\{/controls\.\1\}\}", ctrl_block, html, flags=re.S)
    # drop runtime-only loops (audio jacks, NAM file pickers)
    for pat in [r"\{\{#effect\.ports[^}]*\}\}.*?\{\{/effect\.ports[^}]*\}\}",
                r"\{\{#effect\.parameters[^}]*\}\}.*?\{\{/effect\.parameters[^}]*\}\}",
                r"\{\{#path\}\}.*?\{\{/path\}\}", r"\{\{#files\}\}.*?\{\{/files\}\}"]:
        html = re.sub(pat, "", html, flags=re.S)
    html = re.sub(r"\{\{[#/^][^}]*\}\}", "", html)      # leftover section tags
    html = re.sub(r"\{\{\{?[^}]*\}\}\}?", "", html)     # leftover variables incl {{{cns}}}
    return html

def shot(html_path, out_path, w, h, dsf=1.0):
    subprocess.run([CHROME, "--headless", "--disable-gpu", "--hide-scrollbars",
                    "--default-background-color=00000000", f"--window-size={w},{h}",
                    f"--force-device-scale-factor={dsf}", f"--screenshot={out_path}",
                    "file:///" + html_path.replace("\\", "/")], capture_output=True)

def render(pedal, ttl, w, h):
    base = os.path.join(LV2, f"modgui-{pedal}")
    css = open(os.path.join(base, f"stylesheet-{pedal}.css"), encoding="utf-8").read()
    css = css.replace("{{{cns}}}", "").replace("{{{ns}}}", "")
    fileurl = "file:///" + base.replace("\\", "/") + "/"
    css = css.replace("url(/resources/", "url(" + fileurl).replace('url("/resources/', 'url("' + fileurl)
    html = open(os.path.join(base, f"icon-{pedal}.html"), encoding="utf-8").read()
    html = fill_mustache(html, parse_controls(os.path.join(LV2, ttl)))
    html = html.replace('class="mod-powerswitch-image"', 'class="mod-powerswitch-image on"')
    html = html.replace('class="hf-sw-img"', 'class="hf-sw-img on"')
    page = ('<!DOCTYPE html><html><head><meta charset="utf-8"><style>'
            'html,body{margin:0;padding:0;background:#0b0d12;}' + css + '</style></head><body>'
            + html + '</body></html>')
    hp = os.path.join(OUT, f"{pedal}.html"); open(hp, "w", encoding="utf-8").write(page)
    full = os.path.join(base, f"screenshot-{pedal}.png")
    shot(hp, full, w, h)
    shot(hp, os.path.join(base, f"thumbnail-{pedal}.png"), w, h, round(THUMB_W / w, 4))
    print(f"  {pedal:8s} {w}x{h}  (controls: {len(parse_controls(os.path.join(LV2, ttl)))})")

if __name__ == "__main__":
    import sys
    only = sys.argv[1:] or list(PEDALS)
    for p in only:
        render(p, *PEDALS[p])
    print("DONE")
