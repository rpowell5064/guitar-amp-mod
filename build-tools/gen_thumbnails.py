# Regenerate Hex Chain screenshot-<p>.png (full pedal, exact WxH) and
# thumbnail-<p>.png (scaled) for each modgui by headless-rendering the real CSS
# with the actual knob/switch sprites (file://) and default-state controls.
import os, subprocess, sys

REPO = r"C:\Development\Projects\guitar-amp-mod"
LV2 = os.path.join(REPO, "lv2")
OUT = os.path.join(os.environ["TEMP"], "hxthumb")
os.makedirs(OUT, exist_ok=True)
CHROME = r"C:\Program Files\Google\Chrome\Application\chrome.exe"

# pedal -> (w, h, thumbW, title, tag, body-html)
def knob(l):   return f'<div class="mod-knob" title="{l}"><div class="mod-knob-image"></div><span class="mod-knob-title">{l}</span><span class="mod-knob-value">0.50</span></div>'
def sw(l):     return f'<div class="mod-switch" title="{l}"><div class="mod-switch-image on"></div><span class="mod-switch-title">{l}</span></div>'
def knobs(*ls):return '<div class="mod-control-group mod-knobs clearfix">' + ''.join(knob(l) for l in ls) + '</div>'
def sel(text, cls="mod-model-select"): return f'<div class="mod-enumerated {cls}"><div class="mod-enumerated-selected">{text}</div></div>'
def modelrow(lbl, text): return f'<div class="mod-amp-modelrow"><span class="mod-section-label">{lbl}</span>{sel(text)}</div>'

DOT = "·"
PED = {
 "amp": dict(w=620,h=680,tw=180,title="AMP",tag=f"5 preamp models {DOT} power-amp stage", body=
    modelrow("AMP MODEL","Crunchy McCrunchFace") + knobs("Gain","Bass","Mid","Treble","Presence","Master","Sag") +
    '<div class="mod-pagroup"><div class="mod-section-divider"></div><span class="mod-section-label">POWER AMP</span>'
    '<div class="mod-control-group mod-row-mixed clearfix">' + sw("PA Auto") + sw("PA Bypass") + '</div>'
    '<div class="mod-control-group mod-knobs clearfix">' + knob("PA Resonance") + sw("Air Feel") + '</div></div>'),
 "cab": dict(w=360,h=403,tw=180,title="CABINET",tag=f"IR loader {DOT} low / high cut", body=
    knobs("Low Cut","High Cut","Mix") +
    f'<div class="mod-irloader-group"><span class="mod-section-label">IMPULSE RESPONSE</span>{sel("-- choose an IR file --","")}</div>'
    '<div class="mod-control-group mod-switches clearfix">' + sw("Bypass") + '</div>'),
 "drive": dict(w=360,h=344,tw=180,title="DRIVE",tag=f"Green Man {DOT} New Dawn {DOT} Dear Rodent Boy", body=
    modelrow("PEDAL MODEL","Green Man") + knobs("Drive","Tone","Level","Mix")),
 "delay": dict(w=480,h=450,tw=180,title="DELAY",tag=f"digital {DOT} tape {DOT} drum echo", body=
    modelrow("DELAY TYPE","Echo Wreck") +
    '<div class="mod-echogroup"><div class="mod-heads-row"><span class="mod-section-label">ECHO HEADS</span>' + sel("11: All Heads") + '</div></div>' +
    knobs("Time","Feedback","Mix","Width") +
    '<div class="mod-tapegroup"><div class="mod-section-divider"></div><span class="mod-section-label">TAPE MOTION</span><div class="mod-control-group mod-knobs clearfix">' + knob("Wow") + knob("Flutter") + '</div></div>'),
 "gate": dict(w=360,h=300,tw=180,title="GATE",tag=f"noise gate {DOT} hysteresis", body=
    knobs("Threshold","Attack","Hold","Release","Hysteresis")),
 "comp": dict(w=420,h=340,tw=210,title="COMPRESSOR",tag=f"VCA glue {DOT} FET squash", body=
    modelrow("MODEL","5 Creature Amp") +
    '<div class="mod-control-group mod-row-mixed clearfix">' + sel("4:1","mod-mini-select") + '<span class="mod-inline-label">Ratio</span></div>' +
    knobs("Threshold","Attack","Release","Knee","Makeup")),
 "modfx": dict(w=400,h=352,tw=200,title="MODULATION",tag=f"chorus {DOT} rotary vibe", body=
    modelrow("MODEL","Lush-2") + knobs("Rate","Depth","Mix","Width")),
 "reverb": dict(w=480,h=320,tw=240,title="REVERB",tag="modulated reverb", body=
    knobs("Pre-Delay","Decay","Damping","Mod Depth","Mod Rate","Mix")),
 "utility": dict(w=320,h=300,tw=160,title="INPUT TRIM",tag=f"gain {DOT} phase {DOT} hum filter", body=
    '<div class="mod-control-group mod-knobs clearfix">' + knob("Gain") + sw("Phase") + sw("Hum Filter") + '</div>'),
 "fuzz": dict(w=360,h=384,tw=180,title="FUZZ",tag=f"I Know It {DOT} germanium", body=
    modelrow("PEDAL","I Know It") +
    knobs("Attack","Bias","Level","Input Trim","Ge Temp")),
 "nail": dict(w=360,h=430,tw=180,title="NAIL",tag=f"industrial distortion {DOT} 5 modes", body=
    modelrow("MODE","Yo, Hey Adrian!") +
    knobs("Drive","Filter","Texture","Level") +
    '<div class="mod-control-group mod-switches clearfix">' + sw("Ring Mod") + '</div>'),
}

def render(p, d):
    base = os.path.join(LV2, f"modgui-{p}")
    css = open(os.path.join(base, f"stylesheet-{p}.css"), encoding="utf-8").read()
    css = css.replace("{{{cns}}}", "").replace("{{{ns}}}", "")
    fileurl = "file:///" + base.replace("\\", "/") + "/"
    css = css.replace("url(/resources/", "url(" + fileurl)            # bare url()
    css = css.replace("url(\"/resources/", "url(\"" + fileurl)        # quoted (data uri unaffected)
    html = f"""<!DOCTYPE html><html><head><meta charset="utf-8"><style>
html,body{{margin:0;padding:0;background:#0b0d12;}}
{css}
</style></head><body>
<div class="mod-pedal mod-pedal-guitaramp-{p} ">
  <div class="mod-drag-handle"></div>
  <div class="mod-powerswitch"><div class="mod-powerswitch-image on"></div></div>
  <div class="hx-header">
    <div class="hx-brand"><span class="hx-mark"></span>HEX CHAIN</div>
    <div class="hx-title">{d['title']}</div>
    <div class="hx-tag">{d['tag']}</div>
  </div>
  {d['body']}
  <div class="hx-logo"></div>
</div></body></html>"""
    hp = os.path.join(OUT, f"{p}.html")
    open(hp, "w", encoding="utf-8").write(html)
    url = "file:///" + hp.replace("\\", "/")
    common = [CHROME, "--headless", "--disable-gpu", "--hide-scrollbars",
              "--default-background-color=00000000"]
    # full screenshot (exact WxH)
    subprocess.run(common + [f"--window-size={d['w']},{d['h']}",
                   "--force-device-scale-factor=1",
                   f"--screenshot={os.path.join(OUT, f'screenshot-{p}.png')}", url],
                   capture_output=True)
    # thumbnail (scaled by device-scale-factor)
    dsf = round(d['tw'] / d['w'], 4)
    subprocess.run(common + [f"--window-size={d['w']},{d['h']}",
                   f"--force-device-scale-factor={dsf}",
                   f"--screenshot={os.path.join(OUT, f'thumbnail-{p}.png')}", url],
                   capture_output=True)
    print(f"rendered {p}: screenshot {d['w']}x{d['h']}, thumbnail dsf={dsf}")

for p, d in PED.items():
    render(p, d)
print("DONE ->", OUT)
