# Add a live value readout under every knob across all Hex Chain modguis (so the
# current value shows on the pedal face without opening advanced view), and make the
# knob block tall enough / non-clipping to show it. Idempotent.
import re, os
LV2 = r"C:\Development\Projects\guitar-amp-mod\lv2"
PEDALS = ["amp","cab","comp","delay","drive","gate","modfx","reverb","utility"]

VALSPAN = '<span class="mod-knob-value" mod-role="input-control-value" mod-port-symbol="{{symbol}}"></span>'

for p in PEDALS:
    d = os.path.join(LV2, f"modgui-{p}")
    icon = os.path.join(d, f"icon-{p}.html")
    css  = os.path.join(d, f"stylesheet-{p}.css")
    h = open(icon, encoding="utf-8").read()
    if "mod-knob-value" in h:
        print(f"{p}: icon already has values, skipping icon")
    else:
        # insert the value span immediately after each knob title span
        h2 = re.sub(r'(<span class="mod-knob-title"[^>]*>\{\{name\}\}</span>)',
                    r'\1' + VALSPAN, h)
        n = h2.count("mod-knob-value")
        open(icon, "w", encoding="utf-8").write(h2)
        print(f"{p}: added {n} value spans to icon")

    c = open(css, encoding="utf-8").read()
    prefix = f".mod-pedal-guitaramp-{p}{{{{{{cns}}}}}}"   # .mod-pedal-guitaramp-<p>{{{cns}}}
    # 1) make knob blocks non-clipping so the value (3rd child) shows
    c = re.sub(r'(\.mod-knob\s*\{[^}]*?)overflow:\s*hidden', r'\1overflow: visible', c)
    # 2) append the value-readout rule if absent
    if "mod-knob-value" not in c:
        rule = ("\n/* Live control value readout (visible without advanced view) */\n"
                f"{prefix} .mod-knob .mod-knob-value {{ font-family:\"Helvetica Neue\",Helvetica,Arial,sans-serif;"
                " font-size:10px; font-weight:600; color:#19e0ff; text-align:center; display:block;"
                " margin-top:2px; line-height:1.2; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }\n")
        c += rule
        print(f"{p}: appended .mod-knob-value css")
    open(css, "w", encoding="utf-8").write(c)
print("DONE")
