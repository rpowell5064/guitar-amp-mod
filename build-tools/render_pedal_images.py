# Render bundle screenshot + thumbnail for the simple standalone pedals (Octave, Wah)
# from their REAL modgui CSS, so the listing art reflects the actual Hex Chain UI
# instead of a placeholder. Headless Chrome, same approach as render_hexforge.py.
# Knob images render at their default sprite position (no JS), like every MOD static
# render; the value readouts are filled with representative settings.
# Run: python build-tools/render_pedal_images.py
import os, subprocess, shutil

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LV2  = os.path.join(REPO, "lv2")
OUT  = os.path.join(os.environ["TEMP"], "hxthumb"); os.makedirs(OUT, exist_ok=True)
CHROME = r"C:\Program Files\Google\Chrome\Application\chrome.exe"
THUMB_W = 280

def _knob(l, v):
    return (f'<div class="mod-knob" title="{l}"><div class="mod-knob-image"></div>'
            f'<span class="mod-knob-title">{l}</span><span class="mod-knob-value">{v}</span></div>')

def knobs(*ks):
    return '<div class="mod-control-group mod-knobs clearfix">' + ''.join(_knob(*k) for k in ks) + '</div>'

def modelrow(lbl, sel):
    return (f'<div class="mod-amp-modelrow"><span class="mod-section-label">{lbl}</span>'
            f'<div class="mod-enumerated mod-model-select"><div class="mod-enumerated-selected">{sel}</div></div></div>')

def _shot(html_path, out_path, w, h, dsf=1.0):
    subprocess.run([CHROME, "--headless", "--disable-gpu", "--hide-scrollbars",
                    "--default-background-color=00000000", f"--window-size={w},{h}",
                    f"--force-device-scale-factor={dsf}", f"--screenshot={out_path}",
                    "file:///" + html_path.replace("\\", "/")], capture_output=True)

def render(pedal, w, h, title, tag, body):
    base = os.path.join(LV2, f"modgui-{pedal}")
    css = open(os.path.join(base, f"stylesheet-{pedal}.css"), encoding="utf-8").read()
    css = css.replace("{{{cns}}}", "").replace("{{{ns}}}", "")
    fileurl = "file:///" + base.replace("\\", "/") + "/"
    css = css.replace("url(/resources/", "url(" + fileurl).replace('url("/resources/', 'url("' + fileurl)
    page = (f'<!DOCTYPE html><html><head><meta charset="utf-8"><style>'
            f'html,body{{margin:0;padding:0;background:#0b0d12;}}{css}</style></head><body>'
            f'<div class="mod-pedal mod-pedal-guitaramp-{pedal} "><div class="mod-drag-handle"></div>'
            f'<div class="mod-powerswitch"><div class="mod-powerswitch-image on"></div></div>'
            f'<div class="hx-header"><div class="hx-brand"><span class="hx-mark"></span>HEX CHAIN</div>'
            f'<div class="hx-title">{title}</div><div class="hx-tag">{tag}</div></div>{body}'
            f'<div class="hx-logo"></div></div></body></html>')
    hp = os.path.join(OUT, f"img_{pedal}.html"); open(hp, "w", encoding="utf-8").write(page)
    full = os.path.join(OUT, f"render-{pedal}.png")
    _shot(hp, full, w, h)
    shutil.copyfile(full, os.path.join(base, f"screenshot-{pedal}.png"))
    _shot(hp, os.path.join(base, f"thumbnail-{pedal}.png"), w, h, round(THUMB_W / w, 4))
    print(f"wrote modgui-{pedal}/screenshot + thumbnail ({w}x{h})")

# Octave — sub + octave-up, 3 knobs
render("octave", 400, 352, "OCTAVE", "sub · octave-up",
       knobs(("Octave Up", "0.40"), ("Sub Octave", "0.55"), ("Dry", "0.85")))

# Wah — Auto/Fixed mode + 5 knobs
render("wah", 400, 352, "WAH", "auto · cocked",
       modelrow("MODE", "Auto") +
       knobs(("Freq", "0.40"), ("Range", "0.70"), ("Sens", "0.50"), ("Resonance", "0.60"), ("Mix", "0.80")))

print("DONE")
