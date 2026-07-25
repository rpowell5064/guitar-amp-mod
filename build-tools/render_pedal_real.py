# Render a standalone pedal's REAL icon-<pedal>.html + stylesheet to PNG (headless
# Chrome) with a minimal mustache expansion — layout checks on the actual template,
# not a hand-copied body (which can hide the exact overflow being debugged).
# Usage: python build-tools/render_pedal_real.py <pedal> [w] [h]
import os, re, subprocess, sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LV2 = os.path.join(REPO, "lv2")
OUT = os.path.join(os.environ["TEMP"], "hxthumb"); os.makedirs(OUT, exist_ok=True)
CHROME = r"C:\Program Files\Google\Chrome\Application\chrome.exe"

# modgui:port index -> (symbol, name) per pedal (mirror the .ttl modgui lists)
PORTS = {
    "cab": {0:("low_cut_hz","Low Cut"),1:("high_cut_hz","High Cut"),2:("mix","Mix"),3:("bypass","Bypass"),
            4:("nam_gain","NAM Gain"),5:("nam_vol","NAM Level"),6:("mic_pos","Mic Pos"),7:("mic_dist","Mic Dist"),
            8:("room_on","Room"),9:("room_mix","Room Mix"),10:("room_amt","Room Size"),11:("voice","Voice"),
            12:("room_density","Room Density")},
    "reverb": {0:("pre_delay_ms","Pre-Delay"),1:("decay_time","Decay"),2:("damping","Damping"),
               3:("mod_depth","Mod Depth"),4:("mod_rate","Mod Rate"),5:("mix","Mix"),6:("bypass","Bypass"),
               7:("density","Density"),8:("type","Type"),9:("bloom","Bloom")},
}
SCALES = {("cab","voice"): ["Room","Studio"], ("cab","room_density"): ["Classic","Dense"],
          ("reverb","type"): ["Plate","Spring","Ambient"], ("reverb","density"): ["Classic","Dense"]}

def expand(pedal, html):
    ports = PORTS[pedal]
    # {{#controls.N}} ... {{/controls.N}} -> inner with {{symbol}}/{{name}} filled
    def ctl(m):
        idx = int(m.group(1)); body = m.group(2)
        if idx not in ports: return ""
        sym, name = ports[idx]
        sp = SCALES.get((pedal, sym))
        if sp:
            opts = "".join('<div mod-role="enumeration-option" mod-port-value="%d">%s</div>' % (i, l) for i, l in enumerate(sp))
            body = re.sub(r"\{\{#scalePoints\}\}.*?\{\{/scalePoints\}\}", opts, body, flags=re.S)
        return body.replace("{{symbol}}", sym).replace("{{name}}", name)
    html = re.sub(r"\{\{#controls\.(\d+)\}\}(.*?)\{\{/controls\.\1\}\}", ctl, html, flags=re.S)
    # path picker + audio port loops: keep inner content once, drop the wrappers
    html = re.sub(r"\{\{#effect\.parameters\.0\}\}|\{\{/effect\.parameters\.0\}\}|\{\{#path\}\}|\{\{/path\}\}", "", html)
    html = re.sub(r"\{\{#files\}\}.*?\{\{/files\}\}", "", html, flags=re.S)
    html = re.sub(r"\{\{#effect\.ports\.audio\.(input|output)\}\}(.*?)\{\{/effect\.ports\.audio\.\1\}\}",
                  lambda m: m.group(2).replace("{{symbol}}", "in_l").replace("{{name}}", "In"), html, flags=re.S)
    html = html.replace("{{uri}}", "urn:x").replace("{{{cns}}}", "").replace("{{{ns}}}", "")
    # fill visible value spans so knob rows have realistic text width
    html = html.replace('mod-role="input-control-value"', 'mod-role="input-control-value" data-x="1"')
    return html

def main():
    pedal = sys.argv[1]; w = int(sys.argv[2]) if len(sys.argv) > 2 else 760; h = int(sys.argv[3]) if len(sys.argv) > 3 else 560
    base = os.path.join(LV2, "modgui-" + pedal)
    css = open(os.path.join(base, "stylesheet-%s.css" % pedal), encoding="utf-8").read()
    css = css.replace("{{{cns}}}", "").replace("{{{ns}}}", "")
    fileurl = "file:///" + base.replace("\\", "/") + "/"
    css = css.replace("url(/resources/", "url(" + fileurl).replace('url("/resources/', 'url("' + fileurl)
    body = expand(pedal, open(os.path.join(base, "icon-%s.html" % pedal), encoding="utf-8").read())
    # outline the pedal bounds so overflow is visually obvious
    html = ("<!DOCTYPE html><html><head><meta charset='utf-8'><style>html,body{margin:0;padding:14px;background:#0b0d12;}"
            + css + ".mod-pedal{outline:1px dashed rgba(255,80,80,.85);}</style></head><body>" + body + "</body></html>")
    hp = os.path.join(OUT, "real_%s.html" % pedal); open(hp, "w", encoding="utf-8").write(html)
    png = os.path.join(OUT, "real_%s.png" % pedal)
    subprocess.run([CHROME, "--headless", "--disable-gpu", "--hide-scrollbars",
                    "--default-background-color=00000000", "--window-size=%d,%d" % (w, h),
                    "--force-device-scale-factor=1", "--screenshot=" + png,
                    "file:///" + hp.replace("\\", "/")], capture_output=True)
    print(png)

if __name__ == "__main__":
    main()
