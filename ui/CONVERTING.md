# Converting a modgui to a native X11 panel

The native UI exists so Zynthian's VNC desktop (and other X11 LV2 hosts) can show each plugin. Zynthian ignores modgui entirely. The MOD/pi-Stomp bundle never ships it: it is built only with `-DHEXCHAIN_X11UI=ON` (`X11UI=1 bash build-tools/package_bundle.sh <ver>`).

**Every shipped plugin already has a panel.**
- **Generated:** a plugin with no `ui/plugins/<key>_ui.cpp` gets one from its TTL (`ui/core/hc_generic.cpp`). It is a paged layout built from the port groups, using the modgui's theme, title and tagline.
- **Hand-made (a "conversion"):** a panel transcribed from the plugin's modgui replaces the generated one. It looks like the pedal and keeps the modgui's show/hide logic.

Every plugin is converted. Pick the closest one as a reference:

- **Drive:** a MODE segment, show/hide rules, the NAM picker, bare rows.
- **Fuzz:** labels and the tagline that follow a port (`hc::relabel`, `tagFn`).
- **Delay / Modulation:** wrapping groups and a solo dropdown (`s.wrap`, `hc::solo`).
- **Input Trim:** tabs, a text note, a selector that writes other ports (`onEdit`).
- **Amp:** per-model cabinet faces (`s.face`), tabs that come and go per model (`tabVisible`, `onTab`, `tabFollow`), graphic-EQ faders.
- **Cabinet:** built-in IRs in the file browser (`hc::builtins`), an action menu (`hc::menu`), a panel-local switch (`$` symbols), the mic pad, groups above the tabs.

The core (`ui/core/`) already draws the shared Hex Chain pedal language: background, header, power switch, brushed plate, screws, helix, logo badge, titled groups, the cells and the dropdown. A conversion therefore only **describes** the panel; it does not draw anything.

## Steps (one plugin)

1. **Read** `lv2/modgui-<key>/icon-<key>.html` and `stylesheet-<key>.css`. If the modgui has a `script-<key>.js`, read that too.
2. **Create** `ui/plugins/<key>_ui.cpp`. Copy `gate_ui.cpp`, or `drive_ui.cpp` if the plugin needs more than knobs.
   - `title`: the `.hx-title` text. `tag`: the `.hx-tag` text.
   - `theme`: `--acc`, `--encl1` and `--encl2` from the stylesheet's root rule.
   - `width` / `minHeight`: the `.mod-pedal-…` `width` / `height`.
   - `knobW` / `knobImg` / `plateMinH`: the `.hf-knob` width, the `.hf-knob-img` size and the `.hf-plate` `min-height`, when they differ from 52 / 44 / 246.
   - `rows`: one entry per row, top to bottom. Each group is `{ "Title", { controls… } }` in template order. A row outside any `.hf-agroup` (`.hx-moderow`, `.hf-selrow`) is `hc::bare({ … })`.
3. **Map the controls by port symbol, never by index.** In the template, `{{#controls.N}}` refers to the N-th `modgui:port` in the TTL. Look up that entry's `lv2:symbol`.

   | template markup | builder |
   |---|---|
   | `.hf-knob` (film-strip knob) | `hc::knob("symbol")` |
   | `.hf-sel-wrap` / `mod-widget="custom-select"` | `hc::select("symbol")` |
   | `.hf-sw` (toggle) | `hc::toggle("symbol")` |
   | `mod-widget="custom-select-path"` (NAM / IR file) | `hc::file("nammodel", {".nam"}, hc::namRoots(), "Label")`, using `hc::irRoots()` for `.wav` |
   | `.hx-moderow` segmented buttons | `hc::segment("symbol", "MODE", { {label, active, pick}, … })` |
   | an output value / LED | `hc::readout("symbol")` |
   | `.hf-eqslider` fader | `hc::fader("symbol", "label")` |
   | `.hf-tabs` tab bar | `s.tabs` + `hc::onTab(group, i)` |
   | `.hf-gnote` text box | `hc::note(textFn)` |

   Match the stylesheet's `.hf-agroup-body` gap and wrap and the `.hf-agroup` padding with `s.gap`, `s.wrap` and `s.groupPadX`.

   - Pass a second argument to override the label (it defaults to the port's `lv2:name`).
   - `hc::wide(control, 240)` sets a `.hf-sel-wrap` width that isn't 176.
   - A file control's symbol is the parameter's URI fragment (`#nammodel` → `"nammodel"`), not a port.
   - Skip the `bypass` port. It is the header power switch (`PanelSpec::powerSymbol`).
   - Skip `enabled`. The host binds it.
4. **Translate the script's logic.**
   - **Show/hide:** `script-<key>.js` show/hide rules become `VisibleFn`s on the control or group. For example, `if (model == 3) $('.cab-mic').show()` becomes:

     ```cpp
     hc::knob("mic_pos", {}, [](const hc::Values& v){ return v.get("model") == 3; })
     ```

     Hidden controls drop out of the layout, and the row re-centres, like the CSS flexbox.
   - **Buttons that write a computed value** (Drive's MODE remembers the last internal model): a `SegOption`'s `pick` lambda, with any state in a captured `std::shared_ptr`.
5. **Regenerate** with `python build-tools/gen_ui_ports.py`. It picks the new file up by name, and the plugin stops using its generated panel. Commit the generated files.
6. **Review headless**, with no X server needed:

   ```bash
   cmake -S . -B build-x11 -DHEXCHAIN_X11UI=ON && cmake --build build-x11 --target hc_render
   build-x11/ui/hc_render lv2 <key> /tmp/<key>.png            # defaults
   build-x11/ui/hc_render lv2 <key> /tmp/<key>-open.png model=3 --open=model
   build-x11/ui/hc_render lv2 hexforge /tmp/hf-amp.png --page=6   # a generated panel's page
   ```

   Compare the result against `lv2/modgui-<key>/screenshot-<key>.png`. Standing bar: precision, alignment, restraint. Nothing may overlap or clip, and the values must be legible.
7. **Test in a real host.** Zynthian runs exactly this command:

   ```bash
   jackd -d dummy &
   GDK_BACKEND=x11 jalv.gtk3 -d --jack-name t <plugin-uri>
   ```

   - Drag, scroll, double-click (resets to the default), switch and dropdown must all work.
   - `jalv -c sym=val` must show on open.
   - `-d` dumps the `patch:Set` / `patch:Get` traffic of the file pickers.

## Rules

- **Never hand-type a port index or range.** They come from the TTL through the generator, and `makeSpec()` refuses a layout that names an unknown symbol or file parameter.
- **Amp panels show only the real controls** (the same rule as the modgui).
- **Keep rig-dependent or pedalboard-level UI out.** The panel mirrors the pedal.

## Application panels (Scratch Pad, Hex Forge)

A modgui that is an application rather than a pedal (custom canvases, text entry, drag and drop, timers) becomes an `App` (`ui/core/hc_app.h`): `PanelSpec::app` returns an object that draws the whole window and handles input itself, reaching the plugin through `AppHost` (ports, `pulse()` for edge-triggered ports, text/path parameters, `sendFloat`, timers, images, keyboard focus).

- `ui/core/hc_kit.h` is the immediate-mode kit: hit regions registered while drawing, text fields, port drags, scrollbars, wrapped paragraphs and a file-browser sheet.
- `ui/core/hc_json.h` parses and writes the plugins' JSON strings.
- An App can embed declarative panels (`PanelSpec::embedded`, `Panel(..., outer)`): Hex Forge's block panels are PanelSpecs on its own port symbols (`ui/plugins/hexforge_blocks.inc`) and share the App panel's values.
- Review headless with `hc_render ... --app=key=value` (the App's `review()`) and `--param=key=FILE` (a patch parameter as the plugin would send it).

## Generated panels

`hc_generic.cpp` builds them from the TTL, and the theme comes from the modgui.

- **Pages:** one per port group. A block's "<Block> More" and "<Block> Power Amp" groups become extra sections on the "<Block>" page.
- **Widgets:** a Select for a port with scale points, a Switch for a toggled port, a Readout for an output (an LED when toggled), and a Knob for anything else.
- **Left out:** `notOnGUI` ports, plus `bypass` and `enabled` (the header power switch drives those).
- **File parameters:** each lands on the page whose title begins its label ("Cab 2 IR" → Cab 2).
- **Labels:** a first word that most of a page shares is dropped ("Modulation Rate" → "Rate"), unless that would give two controls the same label.

Hex Forge uses one too, so its 37 pages track `gen_hexforge.py`'s groups with no hand edits. Hex Forge's chain view, preset browser and rig picker are its modgui's own application logic and aren't reproduced.
