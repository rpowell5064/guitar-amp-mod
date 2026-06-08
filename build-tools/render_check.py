# Ad-hoc modgui layout check: render a pedal's real CSS with a given body state to
# PNG (headless Chrome) so layout/fit/value-readouts can be eyeballed before deploy.
import os, subprocess, sys
REPO = r"C:\Development\Projects\guitar-amp-mod"; LV2 = os.path.join(REPO, "lv2")
OUT = os.path.join(os.environ["TEMP"], "hxthumb"); os.makedirs(OUT, exist_ok=True)
CHROME = r"C:\Program Files\Google\Chrome\Application\chrome.exe"
DOT = "·"

def knob(l, v): return (f'<div class="mod-knob" title="{l}"><div class="mod-knob-image"></div>'
                        f'<span class="mod-knob-title">{l}</span><span class="mod-knob-value">{v}</span></div>')
def sel(t, cls="mod-model-select"): return f'<div class="mod-enumerated {cls}"><div class="mod-enumerated-selected">{t}</div></div>'
def modelrow(lbl, t, extra=""): return f'<div class="mod-amp-modelrow {extra}"><span class="mod-section-label">{lbl}</span>{sel(t)}</div>'
def knobs(*ks): return '<div class="mod-control-group mod-knobs clearfix">' + ''.join(knob(*k) for k in ks) + '</div>'

def render(pedal, name, w, h, title, tag, body):
    base = os.path.join(LV2, f"modgui-{pedal}")
    css = open(os.path.join(base, f"stylesheet-{pedal}.css"), encoding="utf-8").read()
    css = css.replace("{{{cns}}}", "").replace("{{{ns}}}", "")
    fileurl = "file:///" + base.replace("\\", "/") + "/"
    css = css.replace("url(/resources/", "url(" + fileurl).replace('url("/resources/', 'url("' + fileurl)
    html = (f'<!DOCTYPE html><html><head><meta charset="utf-8"><style>'
            f'html,body{{margin:0;padding:0;background:#0b0d12;}}{css}</style></head><body>'
            f'<div class="mod-pedal mod-pedal-guitaramp-{pedal} "><div class="mod-drag-handle"></div>'
            f'<div class="mod-powerswitch"><div class="mod-powerswitch-image on"></div></div>'
            f'<div class="hx-header"><div class="hx-brand"><span class="hx-mark"></span>HEX CHAIN</div>'
            f'<div class="hx-title">{title}</div><div class="hx-tag">{tag}</div></div>{body}'
            f'<div class="hx-logo"></div></div></body></html>')
    hp = os.path.join(OUT, f"chk_{name}.html"); open(hp, "w", encoding="utf-8").write(html)
    url = "file:///" + hp.replace("\\", "/")
    subprocess.run([CHROME, "--headless", "--disable-gpu", "--hide-scrollbars",
                    "--default-background-color=00000000", f"--window-size={w},{h}",
                    "--force-device-scale-factor=1",
                    f"--screenshot={os.path.join(OUT, f'chk_{name}.png')}", url], capture_output=True)
    print("rendered", os.path.join(OUT, f"chk_{name}.png"))

render("fuzz", "fuzz_tb", 360, 384, "FUZZ", "Tone Bender MkII " + DOT + " germanium",
       modelrow("PEDAL", "Tone Bender MkII") +
       knobs(("Attack","6.8"),("Bias","4.5V"),("Level","-3.2dB"),("Input Trim","0.50"),("Ge Temp","16C")))
render("fuzz", "fuzz_ih", 360, 384, "FUZZ", "Italian Hero " + DOT + " 6 variants",
       modelrow("PEDAL", "Italian Hero") + modelrow("VARIANT", "Gotham", "hx-row2") +
       knobs(("Sustain","0.55"),("Tone","0.50"),("Volume","0.65")))
print("DONE", OUT)
