#!/usr/bin/env python3
"""Execute the Practice modgui's JavaScript and prove it draws.

Nothing else in this repo runs modgui JavaScript. render_modguis.py fills the
Mustache and screenshots the CSS, but never executes the script; the only other
place the script runs is the device. That left the parts of this panel that are
NOT declarative -- the waveform lanes, the step grid, the base64 peak decoder,
the groove-label sync -- with no gate at all, which is a poor trade for a plugin
whose whole UI is two canvases.

This supplies the small subset of jQuery the script actually calls, feeds it a
realistic waveform and pattern plus the port events the host would send, and
then checks that the script ran without throwing AND that both canvases have
non-blank pixels. A canvas that silently stays empty is the exact failure this
is here to catch, so "it didn't throw" is deliberately not enough.

Writes PNGs next to the checks so a human can look at what was verified.

Run:  python build-tools/practice_gui_check.py [--out DIR]
"""
import base64
import json
import math
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LV2 = os.path.join(REPO, "lv2")
BASE = os.path.join(LV2, "modgui-practice")
CHROME = r"C:\Program Files\Google\Chrome\Application\chrome.exe"

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import importlib.util
_spec = importlib.util.spec_from_file_location("rm", os.path.join(REPO, "build-tools", "render_modguis.py"))
rm = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(rm)

failures = []


def check(ok, what, detail=""):
    print("  [%s] %s%s%s" % ("PASS" if ok else "FAIL", what,
                             " - " if detail else "", detail))
    if not ok:
        failures.append(what)


# ── Fixtures: the exact shapes the plugin emits (see practiceSendWaveform /
#    practiceSendBuiltin in practice_plugin.cpp; practice_selftest gates them).
def _peaks(seed):
    b = bytearray()
    for i in range(256):
        env = math.exp(-((i % 64) / 64.0) * 2.4)
        b.append(max(0, min(255, int(235 * env * (0.55 + 0.45 * math.sin(i * seed))))))
    return base64.b64encode(bytes(b)).decode()


WAVE = json.dumps({"v": 3, "bars": 2, "len": 384000,
                   "t": [_peaks(0.31), _peaks(0.17), "", ""]})
PATTERN = json.dumps({
    "builtin": 1, "idx": 0, "name": "Rock 8ths", "spb": 16, "bars": 1,
    "lanes": [{"i": 0, "s": "X...-...X..x-..."},
              {"i": 1, "s": "....X.......X..o"},
              {"i": 6, "s": "x.o.x.o.x.o.x.o."},
              {"i": 9, "s": "X..............."}]})

# The longest pattern the plugin accepts: 8 bars of sixteenths = 128 steps.
# The panel is sized so even this needs no scrollbar, which is a claim with a
# number in it and therefore gets a test.
PATTERN_LONG = json.dumps({
    "builtin": 0, "spb": 16, "bars": 8,
    "lanes": [{"i": 0, "s": ("X...-...X..x-..." * 8)},
              {"i": 1, "s": ("....X.......X..o" * 8)},
              {"i": 6, "s": ("x.o.x.o.x.o.x.o." * 8)}]})

# The jQuery subset script-practice.js uses. Kept minimal on purpose: if the
# script starts calling something else, this throws and the check fails, which
# is the correct outcome -- it means the script gained a dependency nothing
# here has verified.
SHIM = r"""
<script>
function Q(n){ this.nodes=n||[]; this.length=this.nodes.length;
  for(var i=0;i<this.nodes.length;i++) this[i]=this.nodes[i]; }
Q.prototype.find=function(s){ var o=[]; this.nodes.forEach(function(n){
  o=o.concat(Array.prototype.slice.call(n.querySelectorAll(s))); }); return new Q(o); };
Q.prototype.each=function(f){ this.nodes.forEach(function(n,i){ f.call(n,i,n); }); return this; };
Q.prototype.attr=function(k,v){ if(v===undefined) return this.nodes[0]&&this.nodes[0].getAttribute(k);
  this.nodes.forEach(function(n){n.setAttribute(k,v);}); return this; };
Q.prototype.text=function(t){ if(t===undefined) return this.nodes[0]?this.nodes[0].textContent:'';
  this.nodes.forEach(function(n){n.textContent=t;}); return this; };
Q.prototype.css=function(k,v){ this.nodes.forEach(function(n){n.style[k]=v;}); return this; };
Q.prototype.on=function(e,f){ this.nodes.forEach(function(n){n.addEventListener(e,f);}); return this; };
Q.prototype.toggleClass=function(c,o){ this.nodes.forEach(function(n){n.classList.toggle(c,!!o);}); return this; };
Q.prototype.hasClass=function(c){ return !!(this.nodes[0]&&this.nodes[0].classList.contains(c)); };
Q.prototype.closest=function(s){ var o=[]; this.nodes.forEach(function(n){ var c=n.closest&&n.closest(s); if(c)o.push(c); }); return new Q(o); };
Q.prototype.height=function(){ var n=this.nodes[0]; return n?n.getBoundingClientRect().height:0; };
var _d=new WeakMap();
Q.prototype.data=function(k,v){ var n=this.nodes[0]; if(!n) return undefined;
  var m=_d.get(n); if(!m){m={};_d.set(n,m);} if(v===undefined) return m[k]; m[k]=v; return this; };
function $(x){ if(x instanceof Q) return x;
  if(typeof x==='string') return new Q(Array.prototype.slice.call(document.querySelectorAll(x)));
  return new Q(x?[x]:[]); }

window.__ERR__ = '';
window.onerror = function(m,s,l){ window.__ERR__ = 'line ' + l + ': ' + m; };
(function(){
  var icon = new Q([document.querySelector('.mod-pedal-guitaramp-practice')]);
  var gui = __SCRIPT__;
  window.__SET__ = [];
  var funcs = { set_port_value:function(s,v){ window.__SET__.push([s,v]); },
                patch_set:function(){}, patch_get:function(){} };
  try {
    gui({type:'start', icon:icon, ports:__PORTS__, parameters:[]}, funcs);
    gui({type:'change', icon:icon, uri:'x#waveform', value:__WAVE__}, funcs);
    gui({type:'change', icon:icon, uri:'x#pattern',  value:__PAT__}, funcs);
    [['pattern',0],['out_trk1_state',3],['out_trk2_state',4],['out_progress',0.38],
     ['out_step',6],['out_undo_avail',1],['run',1],['tempo_sync',0],['tempo',138.4],['trk1_level',0],
     ['lvl_kick',3],['drums_level',-4],['beats_per_bar',4],['count_in',1],['out_countin',3]
    // mod-ui has exactly ONE change event: it carries a patch property (uri) OR
    // a control port (symbol). There is no 'port_event' -- this harness used to
    // invent one, so it validated a code path the device never exercises and
    // passed while every live port update was being dropped. Send what mod-ui
    // actually sends.
    ].forEach(function(p){ gui({type:'change', icon:icon, symbol:p[0], value:p[1]}, funcs); });
    // Simulate a tempo drag: press on the grab, move 40px up, release.
    // 2px per BPM, so 40px up is +20 BPM on top of the 138 just pushed.
    // Click a dropdown open and pick an option. The groove picker shipped
    // unusable because mod-ui's custom-select never opened, and nothing here
    // noticed -- a select that cannot be opened looks identical to one that can.
    if (window.__TAB__ === 'drums') {
      var sel = document.querySelector('[rata-role=selgroove]');
      if (sel) {
        sel.querySelector('.mod-enumerated-selected').dispatchEvent(new MouseEvent('click', {bubbles:true}));
        window.__OPENED__ = sel.classList.contains('open');
        var opts = sel.querySelectorAll('[mod-role=enumeration-option]');
        if (opts.length > 3) opts[3].dispatchEvent(new MouseEvent('click', {bubbles:true}));
        window.__PICKED__ = sel.querySelector('.mod-enumerated-selected').textContent;
      }
    }
    // The end of the count is the cue to come in, so it must SAY so rather
    // than the number simply vanishing.
    if (window.__TAB__ === 'loops') {
      gui({type:'change', icon:icon, symbol:'out_countin', value:0}, funcs);
      var ovp = document.querySelector('[rata-role=countin]');
      window.__PLAY__ = {shown: ovp.classList.contains('on'),
                         go: ovp.classList.contains('go'),
                         text: (document.querySelector('[rata-role=cinum]')||{}).textContent};
      // put the count back so the overlay checks below still see it
      gui({type:'change', icon:icon, symbol:'out_countin', value:3}, funcs);
    }
    if (window.__TAB__ === 'loops') {
      var grab = document.querySelector('[rata-role=tempograb]');
      if (grab) {
        var gr = grab.getBoundingClientRect();
        var x = gr.left + gr.width/2, y = gr.top + gr.height/2;
        grab.dispatchEvent(new MouseEvent('mousedown', {clientX:x, clientY:y, bubbles:true, cancelable:true}));
        document.dispatchEvent(new MouseEvent('mousemove', {clientX:x, clientY:y-40, bubbles:true, cancelable:true}));
        document.dispatchEvent(new MouseEvent('mouseup',   {clientX:x, clientY:y-40, bubbles:true, cancelable:true}));
      }
    }

    var tab = (window.__TAB__ === 'drumslong') ? 'drums' : window.__TAB__;
    document.querySelector('[rata-role=pxview]').setAttribute('data-tab', tab);
    if (window.__TAB__ === 'drums')
      gui({type:'change', icon:icon, uri:'x#pattern', value:__PAT__}, funcs);
    if (window.__TAB__ === 'drumslong')
      gui({type:'change', icon:icon, uri:'x#pattern', value:__LONGPAT__}, funcs);
  } catch (e) { window.__ERR__ = (e && e.message) || String(e); }

  // Ink coverage per canvas: a canvas that drew nothing is the failure mode
  // this exists to catch, and it looks identical to success without this.
  window.__INK__ = {};
  ['wave1','wave2','wave3','grid'].forEach(function(role){
    var c = document.querySelector('[rata-role=' + role + ']');
    if (!c || !c.getContext) { window.__INK__[role] = -1; return; }
    try {
      var d = c.getContext('2d').getImageData(0,0,c.width,c.height).data, lit = 0;
      for (var i=3;i<d.length;i+=4) if (d[i] > 8) lit++;
      window.__INK__[role] = lit / (c.width*c.height);
    } catch (e) { window.__INK__[role] = -2; }
  });
  var r = document.createElement('div');
  r.id = 'result';
  r.textContent = JSON.stringify({err: window.__ERR__, ink: window.__INK__,
                                  groove: (document.querySelector('[rata-role=selgroove] .mod-enumerated-selected')||{}).textContent || '',
                                  length: (document.querySelector('[rata-role=sellen] .mod-enumerated-selected')||{}).textContent || '',
                                  status: (document.querySelector('[rata-role=patstatus]')||{}).textContent || '',
                                  bars:   (document.querySelector('[rata-role=barsread]')||{}).textContent || '',
                                  tempo:  (document.querySelector('[rata-role=tempofield]')||{}).textContent || '',
                                  countin: (function(){
                                    var o = document.querySelector('[rata-role=countin]');
                                    if (!o) return null;
                                    return {shown: o.classList.contains('on'),
                                            num: (document.querySelector('[rata-role=cinum]')||{}).textContent,
                                            dots: document.querySelectorAll('[rata-role=cidots] .px-ci-dot').length,
                                            lit: document.querySelectorAll('[rata-role=cidots] .px-ci-dot.lit').length,
                                            recArmed: !!document.querySelector('.px-tbtn.rec.counting')};
                                  })(),
                                  tempoSets: window.__SET__.filter(function(p){ return p[0]==='tempo'; }),
                                  play: window.__PLAY__,
                                  selOpened: window.__OPENED__,
                                  selPicked: window.__PICKED__,
                                  patternSets: window.__SET__.filter(function(p){ return p[0]==='pattern'; }),
                                  grid: (function(){
                                    var c = document.querySelector('[rata-role=grid]');
                                    if (!c) return null;
                                    var box = c.parentNode;
                                    return {cw: c.width,
                                            box: box.clientWidth,
                                            // >0 means the box is scrollable, i.e. a
                                            // scrollbar: exactly what must never happen.
                                            over: box.scrollWidth - box.clientWidth};
                                  })(),
                                  faders: [].slice.call(document.querySelectorAll('.px-fad-fill'))
                                            .filter(function(f){
                                              // RENDERED height, not the style string: a zero
                                              // fraction serialises as "calc(0% + 0px)", which is
                                              // not "0px" and sailed through a string test while
                                              // every fader on the device sat empty.
                                              return f.getBoundingClientRect().height > 1; }).length});
  r.style.cssText = 'position:fixed;left:-9999px';
  document.body.appendChild(r);
})();
</script>
"""


def build_page(tab):
    css = open(os.path.join(BASE, "stylesheet-practice.css"), encoding="utf-8").read()
    css = css.replace("{{{cns}}}", "").replace("{{{ns}}}", "")
    fileurl = "file:///" + BASE.replace("\\", "/") + "/"
    css = css.replace("url(/resources/", "url(" + fileurl).replace('url("/resources/', 'url("' + fileurl)

    controls = rm.parse_controls(os.path.join(LV2, "practice.ttl"))
    html = rm.fill_mustache(open(os.path.join(BASE, "icon-practice.html"), encoding="utf-8").read(), controls)
    html = html.replace('class="mod-powerswitch-image"', 'class="mod-powerswitch-image on"')

    # Mirror mod-ui's real port shape: the range is nested under "ranges".
    # A flat minimum/maximum fixture is what let a fader bug reach the device
    # -- the script read fields the host never sends and every fader sat at
    # zero while this gate reported success.
    ports = [{"symbol": c["symbol"], "value": 0,
              "ranges": {"minimum": -60, "maximum": 12, "default": 0}} for c in controls]
    shim = (SHIM.replace("__SCRIPT__", open(os.path.join(BASE, "script-practice.js"), encoding="utf-8").read())
                .replace("__WAVE__", json.dumps(WAVE))
                .replace("__PAT__", json.dumps(PATTERN))
            .replace("__LONGPAT__", json.dumps(PATTERN_LONG))
                .replace("__PORTS__", json.dumps(ports)))
    return ('<!DOCTYPE html><html><head><meta charset="utf-8"><style>'
            'html,body{margin:0;padding:0;background:#0b0d12;'
            'font-family:"Helvetica Neue",Helvetica,Arial,sans-serif;}' + css + '</style></head><body>'
            + html + '<script>window.__TAB__=' + json.dumps(tab) + ';</script>' + shim
            + '</body></html>')


def run_tab(tab, outdir):
    hp = os.path.join(outdir, "gui_%s.html" % tab)
    open(hp, "w", encoding="utf-8").write(build_page(tab))
    url = "file:///" + hp.replace("\\", "/")

    dom = subprocess.run([CHROME, "--headless", "--disable-gpu", "--hide-scrollbars",
                          "--window-size=1280,980", "--virtual-time-budget=2500",
                          "--dump-dom", url], capture_output=True, timeout=120).stdout
    # The page contains the plugin's own UTF-8 (en dashes in the hints), so the
    # dump must be decoded as UTF-8 rather than the console's ANSI codepage.
    dom = dom.decode("utf-8", "replace")
    subprocess.run([CHROME, "--headless", "--disable-gpu", "--hide-scrollbars",
                    "--window-size=1280,980", "--virtual-time-budget=2500",
                    "--screenshot=" + os.path.join(outdir, "gui_%s.png" % tab), url],
                   capture_output=True, timeout=120)

    m = re.search(r'id="result"[^>]*>(.*?)</div>', dom, re.S)
    if not m:
        check(False, "%s: the script produced a result" % tab, "no #result node -- the page did not run")
        return None
    import html as _h
    try:
        return json.loads(_h.unescape(m.group(1)))
    except Exception as e:
        check(False, "%s: result parses" % tab, str(e))
        return None


def main():
    outdir = os.path.join(os.environ.get("TEMP", "/tmp"), "hxpractice")
    if "--out" in sys.argv:
        outdir = sys.argv[sys.argv.index("--out") + 1]
    os.makedirs(outdir, exist_ok=True)

    if not os.path.exists(CHROME):
        print("Chrome not found at %s -- skipping (this gate is Windows-side)." % CHROME)
        return 0

    print("Practice modgui script check\n")

    print("LOOPS")
    r = run_tab("loops", outdir)
    if r:
        check(not r["err"], "the script runs without throwing", r["err"])
        check(r["ink"].get("wave1", 0) > 0.02, "track 1's waveform is drawn",
              "ink %.3f" % r["ink"].get("wave1", 0))
        check(r["ink"].get("wave2", 0) > 0.02, "track 2's waveform is drawn",
              "ink %.3f" % r["ink"].get("wave2", 0))
        # Track 3 was sent as empty: it must show the bar rules and nothing else.
        check(0 < r["ink"].get("wave3", 0) < 0.02, "an empty track draws no waveform",
              "ink %.4f" % r["ink"].get("wave3", 0))
        check(r["bars"].strip() != "", "the bar readout is filled in", r["bars"])
        # The tempo readout is a SPAN the script fills, not a mod-ui value
        # widget: mod-ui writes readouts with textContent, which an <input>
        # silently does not display, and that is how it shipped blank once.
        check(r["tempo"].strip() == "158", "dragging the tempo sets it", r["tempo"])
        # The count-in has to be UNMISSABLE -- that was the whole request --
        # so check it is actually on screen, not merely that a port arrived.
        ci = r.get("countin") or {}
        check(ci.get("shown") is True, "the count-in overlay is visible while counting")
        check(ci.get("num") == "3", "it shows the beats remaining", str(ci.get("num")))
        check(ci.get("dots") == 3 and ci.get("lit") == 3,
              "a dot per beat, all still to go",
              "%s dots, %s lit" % (ci.get("dots"), ci.get("lit")))
        check(ci.get("recArmed") is True, "the record button shows it is counting")
        check(r.get("length") == "Free", "the loop length control defaults to Free",
              str(r.get("length")))
        pl = r.get("play") or {}
        check(pl.get("shown") is True and pl.get("go") is True and pl.get("text") == "PLAY",
              "the count ends by saying PLAY", str(pl))
        # The readout moving is not enough: it must actually reach the port.
        sets = r.get("tempoSets") or []
        check(len(sets) > 0 and abs(float(sets[-1][1]) - 158) < 0.51,
              "the drag writes the tempo port",
              "%d write(s), last %s" % (len(sets), sets[-1] if sets else "none"))

    print("\nMIX")
    r = run_tab("mix", outdir)
    if r:
        check(not r["err"], "the script runs without throwing", r["err"])
        # Faders are positioned in PERCENT precisely because this tab is hidden
        # when `start` fires: measuring a hidden element returns zero, which
        # pinned every fader to the bottom of its track on the device.
        check(r.get("faders", 0) >= 9, "the faders are positioned while the tab is hidden",
              "%d of 11 have a fill" % r.get("faders", 0))

    print("\nDRUMS")
    r = run_tab("drums", outdir)
    if r:
        check(not r["err"], "the script runs without throwing", r["err"])
        check(r["ink"].get("grid", 0) > 0.05, "the step grid is drawn",
              "ink %.3f" % r["ink"].get("grid", 0))
        check("Rock 8ths" in r["groove"], "the groove picker shows the selected groove",
              r["groove"])
        check("Rock 8ths" in r["status"], "the status line names the factory groove",
              r["status"])
        check(r.get("selOpened") is True, "the groove dropdown opens when clicked")
        picked = r.get("patternSets") or []
        check(len(picked) == 1, "picking an option writes the pattern port",
              "%d write(s): %s" % (len(picked), picked))
        check(bool(r.get("selPicked")) and r.get("selPicked") != "Rock 8ths",
              "and the box shows what was picked", str(r.get("selPicked")))

    print("\nDRUMS (8 bars / 128 steps)")
    r = run_tab("drumslong", outdir)
    if r:
        check(not r["err"], "the script runs without throwing", r["err"])
        g = r.get("grid") or {}
        # The whole point of the 1280px width: the longest pattern the plugin
        # accepts still lays out without a scrollbar.
        check(g.get("over", 1) <= 0, "128 steps fit without scrolling",
              "canvas %s in a %s box, overflow %s" % (g.get("cw"), g.get("box"), g.get("over")))
        check(r["ink"].get("grid", 0) > 0.05, "the 8-bar grid is drawn",
              "ink %.3f" % r["ink"].get("grid", 0))

    print("\nimages in %s" % outdir)
    if failures:
        print("\nFAILED (%d)" % len(failures))
        return 1
    print("\nALL CHECKS PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
