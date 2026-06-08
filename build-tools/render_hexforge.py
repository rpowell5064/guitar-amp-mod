# Offline render of the REAL Hex Forge modgui (icon-hexforge.html + the actual
# stylesheet) to a PNG, so the tile layout can be eyeballed before deploying.
# Strips the Mustache so a static browser can render it. Also feeds an open-dropdown
# variant. Run: python build-tools/render_hexforge.py
import os, re, subprocess

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE = os.path.join(REPO, "lv2", "modgui-hexforge")
OUT  = os.path.join(os.environ["TEMP"], "hxthumb")
os.makedirs(OUT, exist_ok=True)
CHROME = r"C:\Program Files\Google\Chrome\Application\chrome.exe"
W, H = 1300, 1060
# Conditional classes hidden in the default selection (amp=Crunchy, PA Auto on,
# fuzz=Italian Hero, drive=Green Man, delay=Digital) — mirror script-hexforge.js
# so the static screenshot reflects the real default view.
DEFAULT_HIDDEN = ["c-amp-sunn", "c-amp-chan", "c-amp-reso", "c-amp-paman",
                  "c-fz-tb", "c-dr-oct", "c-dl-tape", "c-dl-heads"]

def strip_mustache(html):
    # Drop the audio-port loops entirely (no jacks in a static render).
    html = re.sub(r"\{\{#effect\.ports[^}]*\}\}.*?\{\{/effect\.ports[^}]*\}\}", "", html, flags=re.S)
    # Cab IR: keep the picker box, drop the section wrappers + file loop.
    html = re.sub(r"\{\{#files\}\}.*?\{\{/files\}\}", "", html, flags=re.S)
    html = re.sub(r"\{\{[#/][^}]*\}\}", "", html)        # remaining section tags
    html = re.sub(r"\{\{\{?[^}]*\}\}\}?", "", html)      # variables incl {{{cns}}}
    return html

def build(open_tiles=False):
    css = open(os.path.join(BASE, "stylesheet-hexforge.css"), encoding="utf-8").read()
    css = css.replace("{{{cns}}}", "").replace("{{{ns}}}", "")
    fileurl = "file:///" + BASE.replace("\\", "/") + "/"
    css = css.replace("url(/resources/", "url(" + fileurl)
    html = strip_mustache(open(os.path.join(BASE, "icon-hexforge.html"), encoding="utf-8").read())
    # Default switches/powerswitch to "on" (enable=1 default) so tiles look engaged.
    html = html.replace('class="hf-on-img"', 'class="hf-on-img on"')
    html = html.replace('class="hf-sw-img"', 'class="hf-sw-img on"')
    html = html.replace('class="mod-powerswitch-image"', 'class="mod-powerswitch-image on"')
    for cls in DEFAULT_HIDDEN:
        html = html.replace(cls + '"', cls + ' mod-hidden"')
    if open_tiles:
        html = html.replace('class="hf-tile"', 'class="hf-tile hf-open"')
    page = ('<!DOCTYPE html><html><head><meta charset="utf-8"><style>'
            'html,body{margin:0;padding:0;background:#0b0d12;}' + css + '</style></head><body>'
            + html + '</body></html>')
    name = "hexforge_open" if open_tiles else "hexforge"
    hp = os.path.join(OUT, name + ".html")
    open(hp, "w", encoding="utf-8").write(page)
    out = os.path.join(OUT, "render-" + name + ".png")
    subprocess.run([CHROME, "--headless", "--disable-gpu", "--hide-scrollbars",
                    "--default-background-color=00000000", f"--window-size={W},{H}",
                    "--force-device-scale-factor=1", f"--screenshot={out}", "file:///" + hp.replace("\\", "/")],
                   capture_output=True)
    print("wrote", out)
    if not open_tiles:
        # ship as the bundle screenshot + a scaled thumbnail
        import shutil
        shutil.copyfile(out, os.path.join(BASE, "screenshot-hexforge.png"))
        dsf = round(280.0 / W, 4)
        th = os.path.join(BASE, "thumbnail-hexforge.png")
        subprocess.run([CHROME, "--headless", "--disable-gpu", "--hide-scrollbars",
                        "--default-background-color=00000000", f"--window-size={W},{H}",
                        f"--force-device-scale-factor={dsf}", f"--screenshot={th}", "file:///" + hp.replace("\\", "/")],
                       capture_output=True)
        print("wrote", os.path.join(BASE, "screenshot-hexforge.png"), "+ thumbnail")

build(False)
build(True)
