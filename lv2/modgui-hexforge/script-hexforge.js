function (event, funcs) {
    // Hex Forge node-chain UI: a horizontal signal strip of clickable nodes (one per
    // block). Click a node to edit it in the detail panel below; click its power dot
    // to bypass it (greyed, dry, settings kept); drag to reorder; "+ ADD" pulls a
    // removed effect back from the palette; REMOVE sends it there. Each movable block
    // owns <pfx>_pos (slot 1..11), <pfx>_enable (chain membership) and <pfx>_bypass
    // (active/bypassed). The DSP sorts by pos and runs a block iff enable && !bypass.
    // Input Trim is locked first; its dot toggles it_enable (it has no bypass port).
    var BLOCKS = ['gt','cp','fz','dr','amp','cab','md','dl','rv','wh','oc','nail','eq'];

    // Node subtitle labels for model-bearing blocks — MUST mirror gen_hexforge.py's
    // scalePoints (the source of truth). Scalar blocks bind their value via mod-role in
    // the HTML instead; these are the ones we can't (enumerated → would show a number).
    var NV = {
        amp: ['Clean Meanie','Crunchy McCrunchFace','Gainzilla','Doom Daddy','Tangerang','Neural','Beardo BE','Hi-Volt','Chime Thirty','Backline Plus','Plexiglass','Cali V','Diamond Plate','Tremont 15'],
        dr:  ['Green Man','New Dawn','Dear Rodent Boy','Neural','Grunge DS','Gilded Horse','Super Nova','Preamp 250'],
        fz:  ['Italian Hero','I Know It','Octavius','Fuzz Zachary'],
        md:  ['Lush-2','Uni-Verse','Phaser','Flanger','Tremolo','Rotary','Nevermind Chorus','Seasick Vibe'],
        dl:  ['Digital','Tape','Echo Wreck','Seraph'],
        eq:  ['Manual','Clean Sparkle','De-Mud','Classic Rock','Metal Rhythm','Lead Cut','Cocked Wah']
    };
    function setNodeVal(icon, pfx, txt) { icon.find('[rata-role=nv-' + pfx + ']').text(txt == null ? '' : txt); }
    function setModelVal(icon, pfx, idx) { var a = NV[pfx]; setNodeVal(icon, pfx, (a && a[idx]) || ''); }

    // ── EQ block: preset curves (mirror GraphicEQ kPre in hexforge_plugin.cpp) + the
    // live response scope. Selecting a preset LOADS its curve into the faders and
    // snaps back to Manual (the Cali V pattern), so the values live ON the controls.
    var EQ_BANDS = ['100', '200', '400', '800', '1k6', '3k2'];
    var EQ_FC = [100, 200, 400, 800, 1600, 3200];
    var EQ_PRE = { 1: [3, 0, -1, 2, 1, 2], 2: [-2, -4, -3, 1, 1, 0], 3: [3, 1, -3, 0, 2, 4],
                   4: [4, -2, -5, -2, 2, 4], 5: [-1, 0, 1, 3, 4, 5], 6: [-4, -2, 2, 7, 2, -4] };
    var EQ_SYMS = ['eq_100', 'eq_200', 'eq_400', 'eq_800', 'eq_1k6', 'eq_3k2', 'eq_level'];
    function eqScope(icon) {
        var line = icon.find('[rata-role=eqline]'); if (!line.length) return;
        var v = icon.data('hf_eq_v') || [0, 0, 0, 0, 0, 0];
        var lvl = parseFloat(icon.data('hf_eq_lvl')) || 0;
        var d = '';
        for (var i = 0; i <= 80; ++i) {
            var f = 40 * Math.pow(250, i / 80), db = lvl;
            for (var b = 0; b < 6; ++b) {
                var g = v[b]; if (g > 12) g = 12; if (g < -12) g = -12;
                var o = Math.log(f / EQ_FC[b]) / Math.LN2;
                db += g * Math.exp(-(o * o) / 0.605);          // ~Q 1.1 bell on the log axis
            }
            if (db > 17) db = 17; if (db < -17) db = -17;
            d += (i ? 'L' : 'M') + (400 * i / 80).toFixed(1) + ' ' + (50 - db * (40 / 15)).toFixed(1);
        }
        line.attr('d', d);
        icon.find('[rata-role=eqfill]').attr('d', d + 'L400 100L0 100Z');
    }
    // Selecting a preset (USER CLICK only — never on preset/pedalboard restore,
    // which would clobber saved fader tweaks) loads its curve into the faders.
    // The eq_preset port KEEPS the selection, so it persists in saved presets and
    // pedalboards and the dropdown shows it after reload; the DSP reads only the
    // faders, so the retained selection never double-applies.
    function loadEqBlockPreset(icon, v) {
        var pv = EQ_PRE[parseInt(v, 10)];
        if (!pv || !funcs || typeof funcs.set_port_value !== 'function') return;
        for (var i = 0; i < 6; ++i) funcs.set_port_value(EQ_SYMS[i], pv[i]);
        icon.data('hf_eq_v', pv.slice());
        setNodeVal(icon, 'eq', NV.eq[parseInt(v, 10)]);
        eqScope(icon);
    }
    function eqTrack(icon, sym, val) {   // returns true if sym belongs to the EQ scope state
        if (sym === 'eq_preset') { setModelVal(icon, 'eq', parseInt(val, 10) || 0); return true; }
        var k = EQ_SYMS.indexOf(sym);
        if (k < 0) return false;
        if (sym === 'eq_level') icon.data('hf_eq_lvl', parseFloat(val));
        else { var a = (icon.data('hf_eq_v') || [0, 0, 0, 0, 0, 0]).slice(); a[k] = parseFloat(val); icon.data('hf_eq_v', a); }
        return true;
    }

    // ── Dropdown labels (2026-07-23): mod-ui doesn't reliably render the selected
    // scale-point label into custom-select widgets, so EVERY select shows its value
    // through this: a per-symbol element cache built at start, synced on click,
    // change events and preset recall. O(1) per change — meters never hit it.
    function syncSel(icon, sym, val) {
        var m = icon.data('hf_selmap'); var els = m && m[sym]; if (!els) return;
        els.forEach(function (el) {
            var sel = el.querySelector('.mod-enumerated-selected'); if (!sel) return;
            var lab = null;
            Array.prototype.forEach.call(el.querySelectorAll('[mod-role=enumeration-option]'), function (o) {
                if (parseFloat(o.getAttribute('mod-port-value')) == parseFloat(val)) lab = (o.textContent || '').replace(/^\s+|\s+$/g, '');
            });
            if (lab != null) sel.textContent = lab;
        });
    }
    function buildSelMap(icon, portMap) {
        var m = {};
        icon.find('[mod-widget=custom-select][mod-port-symbol]').each(function () {
            var sym = this.getAttribute('mod-port-symbol');
            (m[sym] = m[sym] || []).push(this);
            var el = this;
            Array.prototype.forEach.call(el.querySelectorAll('[mod-role=enumeration-option]'), function (o) {
                o.addEventListener('click', function () { syncSel(icon, sym, o.getAttribute('mod-port-value')); });
            });
        });
        icon.data('hf_selmap', m);
        for (var sym in m) if (portMap && (sym in portMap)) syncSel(icon, sym, portMap[sym]);
    }

    function nodeOf(icon, b)  { return icon.find('.hf-node[data-block="' + b + '"]'); }
    function panelOf(icon, b) { return icon.find('.hf-detail-panel[data-block="' + b + '"]'); }
    // ── Strobe tuner: note name + a disc that spins by cents (still + green = in tune) ──
    var NOTE_NAMES = ['C','C♯','D','D♯','E','F','F♯','G','G♯','A','A♯','B'];
    function tunerNote(icon, v) {
        var n = parseInt(v, 10), t = icon.find('[rata-role=tuner]');
        if (n < 0 || isNaN(n)) {
            icon.find('[rata-role=tunernote]').text('–'); t.removeClass('hf-tuner-lit hf-tuner-intune');
            icon.find('[rata-role=tunercents]').text('no signal');
            var nd = icon.find('[rata-role=tunerdisc]')[0]; if (nd) nd.style.left = '50%';   // needle to centre
        } else { icon.find('[rata-role=tunernote]').text(NOTE_NAMES[n]); t.addClass('hf-tuner-lit'); }
    }
    function tunerCents(icon, v) {
        var c = parseFloat(v); if (isNaN(c)) return;
        var t = icon.find('[rata-role=tuner]'), needle = icon.find('[rata-role=tunerdisc]')[0];
        var inTune = Math.abs(c) <= 4;
        t.toggleClass('hf-tuner-intune', inTune);
        icon.find('[rata-role=tunercents]').text((c > 0 ? '+' : '') + c.toFixed(0) + ' cents' + (inTune ? ' • in tune' : (c > 0 ? ' • sharp' : ' • flat')));
        if (needle) needle.style.left = (50 + Math.max(-50, Math.min(50, c))).toFixed(1) + '%';   // −50..+50 → 0..100%
    }
    function posOf(icon, b)   { var p = parseInt(nodeOf(icon, b).attr('data-pos'), 10); return isNaN(p) ? 99 : p; }
    function inChain(icon, b) { return nodeOf(icon, b).parent().hasClass('hf-nodes'); }

    // movable blocks currently in the chain, sorted by slot; and the removed ones
    function chainOrder(icon) {
        var a = BLOCKS.filter(function (b) { return inChain(icon, b); })
                      .map(function (b) { return { b: b, p: posOf(icon, b) }; });
        a.sort(function (x, y) { return (x.p - y.p) || (BLOCKS.indexOf(x.b) - BLOCKS.indexOf(y.b)); });
        return a.map(function (o) { return o.b; });
    }
    function removedOrder(icon) {
        var a = BLOCKS.filter(function (b) { return !inChain(icon, b); })
                      .map(function (b) { return { b: b, p: posOf(icon, b) }; });
        a.sort(function (x, y) { return (x.p - y.p) || (BLOCKS.indexOf(x.b) - BLOCKS.indexOf(y.b)); });
        return a.map(function (o) { return o.b; });
    }
    // Assign pos 1..11 across all movable blocks (chain first, removed after), pushing
    // data-pos BEFORE the ports so the echoed change events read as no-ops.
    function writePos(icon, fns, order) {
        order.forEach(function (b, i) { nodeOf(icon, b).attr('data-pos', i + 1); });
        if (fns && typeof fns.set_port_value === 'function')
            order.forEach(function (b, i) { fns.set_port_value(b + '_pos', i + 1); });
    }
    // Re-flow the chain DOM by slot (Input Trim always first); removed nodes live in
    // the palette and are left there.
    function resort(icon) {
        var nodes = icon.find('.hf-nodes');
        nodes.append(nodeOf(icon, 'it'));
        chainOrder(icon).forEach(function (b) { nodes.append(nodeOf(icon, b)); });
    }
    // Move `moveB` to visual slot `want` among the chain blocks; renumber everything.
    function moveToSlot(icon, fns, moveB, want) {
        var chain = chainOrder(icon).filter(function (b) { return b !== moveB; });
        var idx = want - 1; if (idx < 0) idx = 0; if (idx > chain.length) idx = chain.length;
        chain.splice(idx, 0, moveB);
        writePos(icon, fns, chain.concat(removedOrder(icon)));
        resort(icon);
    }

    function selectNode(icon, b) {
        if (!b || !nodeOf(icon, b).length) b = inChain(icon, 'amp') ? 'amp' : 'it';
        icon.data('hf_sel', b);
        icon.find('.hf-node').removeClass('hf-sel');
        nodeOf(icon, b).addClass('hf-sel');
        icon.find('.hf-detail-panel').removeClass('hf-sel');
        panelOf(icon, b).addClass('hf-sel');
    }

    function renderPalette(icon) {
        var pal = icon.find('.hf-palette');
        pal.find('.hf-palette-empty').remove();
        if (!pal.find('.hf-node').length)
            pal.append('<div class="hf-palette-empty">All effects are already in the chain.</div>');
    }

    function addBlock(icon, fns, b) {
        if (b === 'it' || !fns || inChain(icon, b)) return;
        fns.set_port_value(b + '_enable', 1);
        nodeOf(icon, b).attr('data-pos', 99);                 // drop in at the end of the chain
        icon.find('.hf-nodes').append(nodeOf(icon, b));
        writePos(icon, fns, chainOrder(icon).concat(removedOrder(icon)));
        resort(icon); renderPalette(icon);
        icon.find('.hf-palette').removeClass('hf-open');
        selectNode(icon, b);
    }
    function removeBlock(icon, fns, b) {
        if (b === 'it' || !fns || !inChain(icon, b)) return;
        fns.set_port_value(b + '_enable', 0);
        icon.find('.hf-palette').append(nodeOf(icon, b));
        writePos(icon, fns, chainOrder(icon).concat(removedOrder(icon)));
        resort(icon); renderPalette(icon);
        if (icon.data('hf_sel') === b) selectNode(icon, 'it');
    }
    function setBypass(icon, fns, b, byp) {
        if (!fns) return;
        var sym = (b === 'it') ? 'it_enable' : (b + '_bypass');
        var portVal = (b === 'it') ? (byp ? 0 : 1) : (byp ? 1 : 0);   // IT: it_enable 1=active
        fns.set_port_value(sym, portVal);
        nodeOf(icon, b).toggleClass('hf-byp', byp);
    }

    function setupNodes(icon, fns) {
        var dragB = null;
        icon.find('.hf-node').each(function () {
            var nd = this, b = nd.getAttribute('data-block');
            // click node body = select; click the dot = bypass; a palette node = add
            nd.addEventListener('click', function (e) {
                var t = e.target;
                while (t && t !== nd) {
                    if (t.className && (' ' + t.className + ' ').indexOf(' hf-node-dot ') >= 0) {
                        setBypass(icon, fns, b, !nd.classList.contains('hf-byp'));
                        e.stopPropagation(); return;
                    }
                    t = t.parentNode;
                }
                if (nd.parentNode && nd.parentNode.className.indexOf('hf-palette') >= 0) addBlock(icon, fns, b);
                else selectNode(icon, b);
                e.stopPropagation();
            });
            if (b === 'it') return;
            nd.setAttribute('draggable', 'true');
            nd.addEventListener('dragstart', function (e) {
                if (nd.parentNode && nd.parentNode.className.indexOf('hf-palette') >= 0) { e.preventDefault(); return; }
                dragB = b; nd.classList.add('hf-drag');
                if (e.dataTransfer) { e.dataTransfer.effectAllowed = 'move'; try { e.dataTransfer.setData('text/plain', b); } catch (x) {} }
                e.stopPropagation();
            });
            nd.addEventListener('dragend',  function (e) { nd.classList.remove('hf-drag'); dragB = null; e.stopPropagation(); });
            nd.addEventListener('dragover', function (e) { e.preventDefault(); if (e.dataTransfer) e.dataTransfer.dropEffect = 'move'; });
            nd.addEventListener('drop', function (e) {
                e.preventDefault(); e.stopPropagation();
                var targetB = nd.getAttribute('data-block');
                if (!dragB || dragB === targetB || targetB === 'it' || !inChain(icon, dragB)) return;
                moveToSlot(icon, fns, dragB, posOf(icon, targetB));
            });
        });
        // REMOVE buttons in each detail panel
        icon.find('.hf-dremove').each(function () {
            var el = this;
            el.addEventListener('click', function (e) {
                e.stopPropagation();
                var pn = el; while (pn && !(pn.className && (' ' + pn.className + ' ').indexOf(' hf-detail-panel ') >= 0)) pn = pn.parentNode;
                if (pn) removeBlock(icon, fns, pn.getAttribute('data-block'));
            });
        });
        // Cali V (Mesa) two-tier Channel/Mode buttons → write amp_mv_mode = channel*3 + submode.
        icon.find('.hf-mv-btn').each(function () {
            var el = this;
            el.addEventListener('click', function (e) {
                e.stopPropagation();
                if (el.hasAttribute('data-eq')) {                 // graphic-EQ preset: LOAD its curve into the sliders
                    var eq = parseInt(el.getAttribute('data-eq'), 10);
                    var HF_EQ_PRESETS = {                          // port = 0.5 + dB/24 (mirrors kEqPresets in MesaMarkV.cpp)
                        1: [0.5, 0.5, 0.5, 0.5, 0.5],
                        2: [0.66667, 0.58333, 0.25, 0.54167, 0.70833],
                        3: [0.75, 0.54167, 0.08333, 0.41667, 0.75],
                        4: [0.41667, 0.58333, 0.70833, 0.625, 0.45833],
                        5: [0.45833, 0.41667, 0.41667, 0.625, 0.75]
                    };
                    if (fns && typeof fns.set_port_value === 'function') {
                        var pv = HF_EQ_PRESETS[eq];
                        if (pv) for (var gi = 0; gi < 5; gi++) fns.set_port_value('amp_mv_geq' + gi, pv[gi]);
                        fns.set_port_value('amp_mv_eqpreset', 0);  // stay Custom → the loaded sliders drive the EQ
                    }
                    icon.data('hf_mveq', 0);
                    applyMesa(icon);
                    return;
                }
                var v = icon.data('hf_mv'); if (v == null) v = 6;
                var ch = Math.floor(v / 3), sub = v % 3;
                if (el.hasAttribute('data-ch'))       ch  = parseInt(el.getAttribute('data-ch'), 10);
                else if (el.hasAttribute('data-sub')) sub = parseInt(el.getAttribute('data-sub'), 10);
                var nv = ch * 3 + sub;
                icon.data('hf_mv', nv);
                if (fns && typeof fns.set_port_value === 'function') fns.set_port_value('amp_mv_mode', nv);
                applyMesa(icon);
            });
        });
    }

    // ── Amp faceplate: per-model skin class (hf-face-mN) + big parody badge ──
    function applyAmpFace(icon, m) {
        var face = panelOf(icon, 'amp').find('.hf-amp-face');
        if (!face.length) return;
        var el = face[0];
        el.className = el.className.replace(/\bhf-face-m\d+\b/g, '').replace(/\s+/g, ' ').replace(/\s+$/, '');
        el.className += ' hf-face-m' + m;
        face.find('[rata-role=amp-badge]').text((NV.amp && NV.amp[m]) || '');
    }
    // ── Amp detail TABS (Amp / Voicing / Power Amp): show one panel at a time ──
    function setAmpTab(icon, name) {
        var p = panelOf(icon, 'amp');
        p.find('[rata-role=atab]').each(function () {
            this.classList.toggle('hf-atab-on', this.getAttribute('data-tab') === name);
        });
        p.find('[rata-role=apanel]').each(function () {
            this.classList.toggle('hf-atab-on', this.getAttribute('data-tab') === name);
        });
        icon.data('hf_amp_tab', name);
    }
    // Write the amp model port + refresh (mod-ui doesn't reliably echo set_port_value as a change event).
    function setAmpModel(icon, m) {
        if (funcs && typeof funcs.set_port_value === 'function') funcs.set_port_value('amp_model', m);
        icon.data('hf_amp_m', m); applyAmp(icon);
    }
    // ── Conditional control visibility, scoped to the block's DETAIL PANEL ──
    function show(icon, b, sel, on) { panelOf(icon, b).find(sel).toggleClass('mod-hidden', !on); }
    // ── Cab mic pad: reflect cab_micpos/cab_micdist onto the draggable mic marker ──
    // Geometry (viewBox 140x84): X 28..122 = distance, Y centre 42 ± 28 = position across the
    // cone. Position is acoustically SYMMETRIC, so the marker keeps whichever SIDE of the cap
    // the user dragged to (hf_micside) instead of snapping above centre — no "jump" at the cap.
    function micPadUpdate(icon) {
        var pad = icon.find('[rata-role=micpad]'); if (!pad.length) return;
        var pos  = parseFloat(icon.data('hf_micpos'))  || 0;
        var dist = parseFloat(icon.data('hf_micdist')) || 0;
        var side = icon.data('hf_micside') === -1 ? -1 : 1;
        var x = 28 + dist * 94, y = 63 - side * pos * 42;   // viewBox 140x126: centre 63, travel ±42
        pad.find('[rata-role=micdot]').attr('transform', 'translate(' + x.toFixed(1) + ',' + y.toFixed(1) + ')');
        var pn = pos < 0.12 ? 'CAP EDGE' : pos < 0.5 ? 'CONE' : pos < 0.85 ? 'CONE EDGE' : 'SURROUND';
        var dn = dist < 0.06 ? 'CLOSE' : Math.round(2 + dist * 28) + ' CM';
        pad.find('[rata-role=micposv]').text(pn);
        pad.find('[rata-role=micdistv]').text(dn);
    }
    function applyAmp(icon) {
        var m = icon.data('hf_amp_m'); if (m == null) m = 1;
        var a = icon.data('hf_amp_auto'); if (a == null) a = true;
        show(icon, 'amp', '.c-amp-sunn', m === 3);
        show(icon, 'amp', '.c-amp-chan', m === 2 || m === 4);
        show(icon, 'amp', '.c-amp-be',   m === 6);
        show(icon, 'amp', '.c-amp-mesa', m === 11);
        show(icon, 'amp', '.c-amp-recto', m === 12);
        show(icon, 'amp', '.c-amp-mt15', m === 13);
        show(icon, 'amp', '.c-amp-reso', m === 2);
        show(icon, 'amp', '.c-amp-plexi', m === 10);   // Plexiglass: 1959 Vol II (Normal ch, jumpered)
        show(icon, 'amp', '.c-amp-pa',   m !== 3 && m !== 5);
        show(icon, 'amp', '.c-amp-paman', m !== 3 && m !== 5 && !a);
        show(icon, 'amp', '.c-amp-nam',  m === 5);
        // Tab buttons: Voicing only for models with a channel/EQ chassis (Sunn 3 / Beardo 6 /
        // Cali V 11 / Diamond Plate 12); Power Amp hidden for Sunn (auto-bypassed) and NAM (capture has its own).
        var showVoice = (m === 3 || m === 6 || m === 11 || m === 12 || m === 13);
        var showPower = (m !== 3 && m !== 5);
        var p = panelOf(icon, 'amp');
        p.find('[rata-role=atab][data-tab=voice]').toggleClass('hf-atab-gone', !showVoice);
        p.find('[rata-role=atab][data-tab=power]').toggleClass('hf-atab-gone', !showPower);
        p.find('[rata-role=atab][data-tab=nam]').removeClass('hf-atab-gone');   // Neural tab is ALWAYS available (it's the mode switch)
        // Keep the active tab in sync with the mode: entering/leaving NAM (model 5) flips the tab.
        var cur = icon.data('hf_amp_tab') || 'amp';
        if (m === 5 && cur !== 'nam') { setAmpTab(icon, 'nam'); cur = 'nam'; }
        else if (m !== 5 && cur === 'nam') { setAmpTab(icon, 'amp'); cur = 'amp'; }
        var ok = { amp: true, voice: showVoice, power: showPower, nam: true };
        if (!ok[cur]) setAmpTab(icon, 'amp');
        p.find('[rata-role=lbl-amp_gain]').text(m === 3 ? 'Normal Vol' : (m === 10 ? 'Vol I' : (m === 5 ? 'Output' : 'Gain')));
        setModelVal(icon, 'amp', m);
        applyAmpFace(icon, m);
        applyMesa(icon);
    }
    // Cali V (Mesa Mark V) is now DROPDOWNS (Mode + EQ Preset), handled natively by MOD; applyMesa is a
    // no-op kept for its callers. Selecting an EQ preset LOADS its curve into the 5 faders + resets to
    // Custom (0) so they jump to it and stay tweakable. port = 0.5 + dB/24 (mirrors kEqPresets in MesaMarkV.cpp).
    function applyMesa(icon) {}
    var HF_EQ_PRESETS = {
        1: [0.5, 0.5, 0.5, 0.5, 0.5],
        2: [0.66667, 0.58333, 0.25, 0.54167, 0.70833],
        3: [0.75, 0.54167, 0.08333, 0.41667, 0.75],
        4: [0.41667, 0.58333, 0.70833, 0.625, 0.45833],
        5: [0.45833, 0.41667, 0.41667, 0.625, 0.75]
    };
    function loadEqPreset(icon, e) {
        var pv = HF_EQ_PRESETS[parseInt(e, 10)];
        if (!pv || !funcs || typeof funcs.set_port_value !== 'function') return;
        for (var i = 0; i < 5; i++) funcs.set_port_value('amp_mv_geq' + i, pv[i]);
        funcs.set_port_value('amp_mv_eqpreset', 0);
    }
    function applyFuzz(icon) {
        var p = icon.data('hf_fz_p'); if (p == null) p = 0;
        var tb = (p === 1);            // I Know It (Tone Bender)
        var ff = (p === 3);            // Fuzz Zachary (ZVex-style)
        show(icon, 'fz', '.c-fz-ih', p === 0);      // Variant dropdown = ONLY Italian Hero (Muff eras); NOT Octavia/Tone Bender/Fuzz Zachary
        show(icon, 'fz', '.c-fz-tb', tb || ff);     // Bias/Trim/Temp trio (shared by Tone Bender + Fuzz Zachary)
        var pn = panelOf(icon, 'fz');
        pn.find('[rata-role=lbl-fz_sustain]').text(tb ? 'Attack' : (ff ? 'Drive' : 'Sustain'));
        pn.find('[rata-role=lbl-fz_volume]').text(tb ? 'Level' : 'Volume');
        pn.find('[rata-role=lbl-fz_bias]').text(ff ? 'Comp' : 'Bias');
        pn.find('[rata-role=lbl-fz_inputtrim]').text(ff ? 'Gate' : 'Trim');
        pn.find('[rata-role=lbl-fz_getemp]').text(ff ? 'Stab' : 'Temp');
        setModelVal(icon, 'fz', p);
    }
    function applyDelay(icon) {
        var t = icon.data('hf_dl_t'); if (t == null) t = 0;
        show(icon, 'dl', '.c-dl-tape', t === 1 || t === 2);
        show(icon, 'dl', '.c-dl-heads', t === 2);
        show(icon, 'dl', '.c-dl-seraph', t === 3);
        setModelVal(icon, 'dl', t);
    }
    // ── Drive: model-driven conditional visibility + MODE toggle highlight (Internal <-> Neural).
    // Neural = model 3 (kDrNamIdx): shows the NAM picker + Gain/Level, hides the algo knobs + the
    // model dropdown. The MODE toggle mirrors the standalone drive's Internal/Neural switch.
    var DR_NAM = 3;
    function applyDrive(icon, m) {
        if (m == null) m = 0;
        icon.data('hf_dr_m', m);
        var nam = (m === DR_NAM);
        show(icon, 'dr', '.c-dr-oct', m === 1);
        show(icon, 'dr', '.c-dr-nam', nam);
        show(icon, 'dr', '.c-dr-alg', !nam);
        show(icon, 'dr', '.c-dr-int', !nam);
        panelOf(icon, 'dr').find('[rata-role=drmodebtn]').each(function () {
            this.classList.toggle('hf-mode-on', this.getAttribute('data-mode') === (nam ? 'nam' : 'int'));
        });
        setModelVal(icon, 'dr', m);
    }
    // Write the drive model port + refresh (mod-ui doesn't reliably echo set_port_value as a change).
    function setDriveModel(icon, m) {
        if (funcs && typeof funcs.set_port_value === 'function') funcs.set_port_value('dr_model', m);
        applyDrive(icon, m);
    }
    // (Cab loads IMPULSE RESPONSES only — NAM models amps/pedals, not cabinets. The old IR/Neural
    // SOURCE toggle was removed 2026-07-13.)
    function setFile(icon, rata, value, empty) {
        var box = icon.find('[rata-role=' + rata + ']');
        if (value == null || value === 'None' || value === '') { box.text(empty); return; }
        var label = null;
        icon.find('[mod-role=enumeration-option]').each(function () {
            if (this.getAttribute('mod-parameter-value') == value)
                label = (this.textContent || '').replace(/^\s+|\s+$/g, '');
        });
        if (!label) { var s = '' + value; s = s.substring(s.lastIndexOf('/') + 1); s = s.substring(s.lastIndexOf('\\') + 1); label = s; }
        box.text(label);
    }
    var CAB_NAMES = { '@factory':'Factory 4x12 (Thirty-Something)', '@vox2x12':'Chime 2x12 (alnico)',
                      '@american-ob':'American Open-Back 2x12', '@greenback':'Cashback 4x12',
                      '@hiwatt':'Hi-Volt 4x12', '@doom':'Doom 4x12' };
    function setIr(icon, value) {
        if (value == null || value === 'None' || value === '') value = '@factory';
        if (CAB_NAMES[value]) {                      // built-in synthetic cab
            setFile(icon, 'Ir', value, CAB_NAMES[value]);
            setNodeVal(icon, 'cab', CAB_NAMES[value].replace(/ \(.*\)$/, ''));
            return;
        }
        setFile(icon, 'Ir', value, null);            // user .wav → basename
        setNodeVal(icon, 'cab', icon.find('[rata-role=Ir]').first().text());
    }
    // Level meters: the plugin sends in_meter/out_meter as 0..1 (dB-scaled); set the bar width.
    // Hot path (~14 Hz) — cache the raw DOM node (no jQuery .find() per tick) and skip sub-1%
    // changes so steady-state levels cost zero repaints. This is what kept the browser CPU busy.
    function setMeter(icon, rata, v) {
        var key = 'hf_m_' + rata;
        var el = icon.data(key);
        if (!el || !el.isConnected) {
            var f = icon.find('[rata-role=' + rata + ']');
            if (!f.length) return;
            el = f[0]; icon.data(key, el);
        }
        var pct = (v < 0 ? 0 : (v > 1 ? 1 : v)) * 100;
        if (el._hfPct != null && Math.abs(el._hfPct - pct) < 1) return;
        el._hfPct = pct;
        el.style.width = pct.toFixed(1) + '%';
    }

    // ── Presets: pulse command ports, render bank/slot/name + the 32-slot list ──
    var SW = ['sw_a', 'sw_b', 'sw_c', 'sw_d'];
    var PS_NAME_URI = 'https://rpowell5064.github.io/guitaramp-suite/hexforge#ps_name';
    function psPulse(fns, sym) {
        if (!fns || typeof fns.set_port_value !== 'function') return;
        fns.set_port_value(sym, 1);
        setTimeout(function () { fns.set_port_value(sym, 0); }, 40);
    }
    function psGoto(fns, flat) {
        if (!fns || typeof fns.set_port_value !== 'function') return;
        fns.set_port_value('ps_goto', flat);
        setTimeout(function () { fns.set_port_value('ps_goto', -1); }, 60);
    }
    function psBankLabel(icon) {
        var b = icon.data('ps_bank'); if (b == null) b = 0;
        var s = icon.data('ps_slot'); if (s == null) s = 0;
        icon.find('[rata-role=psbank]').text('BANK ' + (b + 1));
        icon.find('.hf-ps-slot').each(function () {
            this.classList.toggle('hf-ps-on', parseInt(this.getAttribute('data-slot'), 10) === s);
        });
    }
    function psRenderList(icon, fns) {
        var box = icon.find('[rata-role=pslist]'); if (!box.length) return;
        var names = icon.data('ps_names') || [];
        var ab = icon.data('ps_bank') || 0, as = icon.data('ps_slot') || 0;
        // Show every bank that has a preset, PLUS one empty "new" bank at the bottom, PLUS any
        // extra empty banks the user revealed with "+ Add Bank" (hf_addbanks). Up to 32 total.
        var maxB = 0;
        for (var i = 0; i < names.length; i++) if (names[i]) maxB = Math.floor(i / 4);
        var showBanks = Math.min(Math.max(maxB + 2, ab + 2, icon.data('hf_addbanks') || 0), 32);
        var html = '';
        for (var b = 0; b < showBanks; b++) {
            html += '<div class="hf-ps-bankrow"><span class="hf-ps-banknum">B' + (b + 1) + '</span>';
            for (var s = 0; s < 4; s++) {
                var flat = b * 4 + s, nm = names[flat] || '';
                var active = (b === ab && s === as) ? ' hf-ps-active' : '';
                var empty = nm ? '' : ' hf-ps-empty';
                html += '<button type="button" class="hf-ps-item' + active + empty + '" data-flat="' + flat + '">'
                      + '<b>' + 'ABCD'.charAt(s) + '</b> ' + (nm || '—') + '</button>';
            }
            html += '</div>';
        }
        // "+ Add Bank": reveal one more empty bank to save presets into (up to 32).
        if (showBanks < 32)
            html += '<div class="hf-ps-addrow"><button type="button" class="hf-ps-addbank">＋ Add Bank</button></div>';
        box[0].innerHTML = html;
        box.find('.hf-ps-item').each(function () {
            var el = this;
            el.addEventListener('click', function (e) {
                e.stopPropagation();
                psGoto(fns, parseInt(el.getAttribute('data-flat'), 10));
                icon.find('[rata-role=psmenu]').removeClass('hf-ps-open');
            });
        });
        var addb = box.find('.hf-ps-addbank')[0];
        if (addb) addb.addEventListener('click', function (e) {
            e.stopPropagation();
            icon.data('hf_addbanks', Math.min(showBanks + 1, 32));   // reveal the next empty bank
            psRenderList(icon, fns);
        });
    }
    function psSetName(icon, nm) {
        var el = icon.find('[rata-role=psname]')[0];
        if (el && document.activeElement !== el) el.value = (nm == null ? '' : '' + nm);
    }
    // Replay the recalled snapshot ("sym=val;..") onto the host ports so the nodes,
    // detail controls, membership/bypass and chain order all follow the preset.
    function psApply(fns, str, icon) {
        if (!fns || typeof fns.set_port_value !== 'function' || !str) return;
        var sawPos = false, drm = null, membership = false;
        ('' + str).split(';').forEach(function (kv) {
            var i = kv.indexOf('='); if (i < 0) return;
            var sym = kv.substring(0, i), val = parseFloat(kv.substring(i + 1));
            if (!sym || isNaN(val)) return;
            if (/_pos$/.test(sym)) {
                nodeOf(icon, sym.replace(/_pos$/, '')).attr('data-pos', val); sawPos = true;
            } else if (/_bypass$/.test(sym)) {
                nodeOf(icon, sym.replace(/_bypass$/, '')).toggleClass('hf-byp', val > 0.5);
            } else if (/_enable$/.test(sym)) {
                var b = sym.replace(/_enable$/, '');
                if (b === 'it') nodeOf(icon, 'it').toggleClass('hf-byp', !(val > 0.5));
                else {
                    var want = val > 0.5;
                    if (want && !inChain(icon, b)) { nodeOf(icon, b).removeClass('hf-byp'); icon.find('.hf-nodes').append(nodeOf(icon, b)); membership = true; }
                    else if (!want && inChain(icon, b)) { icon.find('.hf-palette').append(nodeOf(icon, b)); membership = true; }
                }
            } else if (sym === 'amp_model')        icon.data('hf_amp_m', parseInt(val, 10));
            else if (sym === 'cab_micpos')         icon.data('hf_micpos', val);    // mod-ui doesn't echo set_port_value → sync the pad by hand
            else if (sym === 'cab_micdist')        icon.data('hf_micdist', val);
            else if (sym === 'amp_pamp_auto')      icon.data('hf_amp_auto', val > 0.5);
            else if (sym === 'amp_mv_mode')        icon.data('hf_mv', parseInt(val, 10));
            else if (sym === 'amp_mv_eqpreset')    icon.data('hf_mveq', parseInt(val, 10));
            else if (sym === 'fz_pedal')           icon.data('hf_fz_p', parseInt(val, 10));
            else if (sym === 'dl_type')            icon.data('hf_dl_t', parseInt(val, 10));
            else if (sym === 'md_type')            { var _mt = parseInt(val, 10); setModelVal(icon, 'md', _mt); show(icon, 'md', '.c-md-delay', _mt === 0 || _mt === 3 || _mt === 6 || _mt === 7); }
            else if (eqTrack(icon, sym, val))      { /* scope redrawn after the loop */ }
            else if (sym === 'dr_model')           drm = parseInt(val, 10);
            syncSel(icon, sym, val);               // dropdown labels track recalled values
            fns.set_port_value(sym, val);
        });
        if (sawPos || membership) resort(icon);
        if (membership) renderPalette(icon);
        micPadUpdate(icon);
        eqScope(icon);
        applyAmp(icon); applyFuzz(icon); applyDelay(icon);
        if (drm != null) applyDrive(icon, drm);
        selectNode(icon, icon.data('hf_sel'));   // keep selection valid + refresh the panel
    }

    if (event.type == 'start') {
        var icon = event.icon;
        setupNodes(icon, funcs);
        // Show picked files immediately (2026-07-23): mod-ui applies the patch write
        // on option click but doesn't reliably echo a change event back, so the IR /
        // NAM labels sat stale. Dispatch by the picker's parameter URI; also seed
        // the labels from the current parameter values at load.
        function fileLabel(uri, value) {
            if (uri.indexOf('#irfile') >= 0)      setIr(icon, value);
            else if (uri.indexOf('#ampnam') >= 0) setFile(icon, 'AmpNam', value, '-- choose a NAM file --');
            else if (uri.indexOf('#drnam') >= 0)  setFile(icon, 'DrNam', value, '-- choose a NAM file --');
        }
        (event.parameters || []).forEach(function (pr) {
            if (pr.uri) fileLabel(pr.uri, pr.value);
        });
        icon.find('[mod-role=input-parameter]').each(function () {
            var picker = this, uri = picker.getAttribute('mod-parameter-uri') || '';
            Array.prototype.forEach.call(picker.querySelectorAll('[mod-role=enumeration-option]'), function (el) {
                el.addEventListener('click', function () {
                    fileLabel(uri, el.getAttribute('mod-parameter-value'));
                });
            });
        });
        // Amp detail tabs: wire clicks (ignore tabs hidden for the current model).
        // The Neural tab doubles as the internal⇄Neural MODE SWITCH: clicking it puts the amp on the
        // NAM slot (model 5) and remembers the last internal model; clicking an internal tab restores it.
        panelOf(icon, 'amp').find('[rata-role=atab]').each(function () {
            var el = this;
            el.addEventListener('click', function (e) {
                e.stopPropagation();
                if (el.classList.contains('hf-atab-gone')) return;
                var t = el.getAttribute('data-tab');
                var cur = icon.data('hf_amp_m'); if (cur == null) cur = 1;
                if (t === 'nam') {
                    if (cur !== 5) { icon.data('hf_amp_last_internal', cur); setAmpModel(icon, 5); }
                    setAmpTab(icon, 'nam');
                } else {
                    if (cur === 5) {
                        var li = icon.data('hf_amp_last_internal');
                        if (li == null || li === 5) li = 1;
                        setAmpModel(icon, li);
                    }
                    setAmpTab(icon, t);
                }
            });
        });
        setAmpTab(icon, 'amp');
        // Drive MODE toggle (Internal / Neural): Neural puts the drive on the NAM slot (model 3) and
        // remembers the last internal model; Internal restores it. Mirrors the standalone drive switch.
        panelOf(icon, 'dr').find('[rata-role=drmodebtn]').each(function () {
            var el = this;
            el.addEventListener('click', function (e) {
                e.stopPropagation();
                var mode = el.getAttribute('data-mode');
                var cur = icon.data('hf_dr_m'); if (cur == null) cur = 0;
                if (mode === 'nam') {
                    if (cur !== DR_NAM) { icon.data('hf_dr_last_internal', cur); setDriveModel(icon, DR_NAM); }
                } else if (cur === DR_NAM) {
                    var li = icon.data('hf_dr_last_internal');
                    if (li == null || li === DR_NAM) li = 0;
                    setDriveModel(icon, li);
                }
            });
        });
        // (Node connector is now a cheap CSS-scrolled baked strip — no JS animation loop.)
        // "+ ADD" palette toggle
        icon.find('.hf-add').each(function () {
            var el = this;
            el.addEventListener('click', function (e) { e.stopPropagation(); icon.find('.hf-palette').toggleClass('hf-open'); });
        });

        // Seed conditional visibility, membership, bypass + slot order from START values.
        var map = {};
        (event.ports || []).forEach(function (p) { map[p.symbol] = p.value; });
        if ('amp_model' in map)     icon.data('hf_amp_m', parseInt(map.amp_model, 10));
        if ('amp_pamp_auto' in map) icon.data('hf_amp_auto', map.amp_pamp_auto > 0.5);
        if ('fz_pedal' in map)      icon.data('hf_fz_p', parseInt(map.fz_pedal, 10));
        if ('dl_type' in map)       icon.data('hf_dl_t', parseInt(map.dl_type, 10));
        if ('amp_mv_mode' in map)   icon.data('hf_mv', parseInt(map.amp_mv_mode, 10));
        if ('amp_mv_eqpreset' in map) icon.data('hf_mveq', parseInt(map.amp_mv_eqpreset, 10));
        applyAmp(icon); applyFuzz(icon); applyDelay(icon);
        var drm = parseInt(map.dr_model || 0, 10);
        applyDrive(icon, drm);
        var _mt0 = parseInt(map.md_type || 0, 10);
        setModelVal(icon, 'md', _mt0);
        icon.data('hf_eq_v', EQ_SYMS.slice(0, 6).map(function (k) { return parseFloat(map[k]) || 0; }));
        icon.data('hf_eq_lvl', parseFloat(map.eq_level) || 0);
        setModelVal(icon, 'eq', parseInt(map.eq_preset || 0, 10));
        eqScope(icon);
        buildSelMap(icon, map);   // every dropdown shows its selected value from load on
        icon.find('[mod-widget=custom-select][mod-port-symbol="eq_preset"] [mod-role=enumeration-option]').each(function () {
            var el = this;
            el.addEventListener('click', function () { loadEqBlockPreset(icon, el.getAttribute('mod-port-value')); });
        });
        show(icon, 'md', '.c-md-delay', _mt0 === 0 || _mt0 === 3 || _mt0 === 6 || _mt0 === 7);
        setNodeVal(icon, 'cab', 'Factory Cab');   // updated by setIr once the IR path arrives
        // Input Trim: dot reflects it_enable (1=active)
        if ('it_enable' in map) nodeOf(icon, 'it').toggleClass('hf-byp', !(map.it_enable > 0.5));
        // Movable blocks: membership (enable) → chain vs palette; bypass → grey; pos
        BLOCKS.forEach(function (b) {
            if ((b + '_pos') in map) nodeOf(icon, b).attr('data-pos', parseInt(map[b + '_pos'], 10));
            if ((b + '_bypass') in map) nodeOf(icon, b).toggleClass('hf-byp', map[b + '_bypass'] > 0.5);
            if ((b + '_enable') in map && !(map[b + '_enable'] > 0.5)) icon.find('.hf-palette').append(nodeOf(icon, b));
        });
        if ('oc_micro' in map) nodeOf(icon, 'oc').toggleClass('hf-oc-micro', map.oc_micro > 0.0001);
        resort(icon); renderPalette(icon);
        selectNode(icon, 'amp');

        // Preset strip wiring.
        function wire(sel, fn) { icon.find(sel).each(function () { var el = this;
            el.addEventListener('click', function (e) { e.stopPropagation(); fn(); }); }); }
        wire('.hf-ps-bankdn', function () { psPulse(funcs, 'ps_bank_dn'); });
        wire('.hf-ps-bankup', function () { psPulse(funcs, 'ps_bank_up'); });
        wire('.hf-ps-save',   function () { psPulse(funcs, 'ps_save'); });
        wire('.hf-ps-mvup',   function () { psPulse(funcs, 'ps_move_up'); });
        wire('.hf-ps-mvdn',   function () { psPulse(funcs, 'ps_move_dn'); });
        wire('.hf-ps-backup', function () { psPulse(funcs, 'ps_backup'); });
        wire('.hf-ps-restore',function () { psPulse(funcs, 'ps_restore'); });
        // Strobe tuner: fork icon opens/closes; the ✕ on the strip closes. Update the UI right
        // here (an unbound input port may not echo a change event back to the modgui) then set it.
        function tunerShow(on) {
            icon.data('hf_tuner_on', on);
            icon.find('[rata-role=tuner]').toggleClass('mod-hidden', !on);
            icon.find('[rata-role=tunerbtn]').toggleClass('hf-on', on);
            if (funcs && funcs.set_port_value) funcs.set_port_value('tuner_on', on ? 1 : 0);
        }
        wire('.hf-tunerbtn',   function () { tunerShow(!icon.data('hf_tuner_on')); });
        wire('.hf-tunerclose', function () { tunerShow(false); });
        if ('tuner_on' in map) { var _to = map.tuner_on > 0.5; icon.data('hf_tuner_on', _to);
            icon.find('[rata-role=tuner]').toggleClass('mod-hidden', !_to); icon.find('[rata-role=tunerbtn]').toggleClass('hf-on', _to); }
        wire('.hf-ps-toggle', function () { icon.find('[rata-role=psmenu]').toggleClass('hf-ps-open'); });
        icon.find('.hf-ps-slot').each(function () { var el = this;
            el.addEventListener('click', function (e) { e.stopPropagation();
                psPulse(funcs, SW[parseInt(el.getAttribute('data-slot'), 10)]); }); });
        icon.find('.hf-ps-name').each(function () {
            var el = this;
            el.addEventListener('mousedown', function (e) { e.stopPropagation(); });
            var commit = function () {
                if (funcs && typeof funcs.patch_set === 'function')
                    funcs.patch_set(PS_NAME_URI, 's', el.value.replace(/\|/g, ' '));
                el.blur();
            };
            el.addEventListener('change', commit);
            el.addEventListener('keydown', function (e) { if (e.keyCode === 13) { e.preventDefault(); commit(); } });
        });
        if ('ps_bank' in map) icon.data('ps_bank', parseInt(map.ps_bank, 10));
        if ('ps_slot' in map) icon.data('ps_slot', parseInt(map.ps_slot, 10));
        psBankLabel(icon); psRenderList(icon, funcs);
        // ── Cab mic pad: drag the mic across the cone (Pos) / away from the grille (Dist) ──
        (function () {
            var svg = icon.find('[rata-role=micsvg]')[0]; if (!svg) return;
            if ('cab_micpos'  in map) icon.data('hf_micpos',  parseFloat(map.cab_micpos));
            if ('cab_micdist' in map) icon.data('hf_micdist', parseFloat(map.cab_micdist));
            function write(pos, dist) {
                icon.data('hf_micpos', pos); icon.data('hf_micdist', dist);
                if (funcs && typeof funcs.set_port_value === 'function') {
                    funcs.set_port_value('cab_micpos',  pos);
                    funcs.set_port_value('cab_micdist', dist);
                }
                micPadUpdate(icon);
            }
            function apply(e) {
                var r = svg.getBoundingClientRect();
                var vx = (e.clientX - r.left) / r.width  * 140;
                var vy = (e.clientY - r.top)  / r.height * 126;
                var off  = 63 - vy;                                        // signed: + above cap, - below
                var dist = Math.max(0, Math.min(1, (vx - 28) / 94));
                var pos  = Math.max(0, Math.min(1, Math.abs(off) / 42));
                icon.data('hf_micside', off < 0 ? -1 : 1);                 // marker follows the pointer's side
                if (pos < 0.05) pos = 0;                                   // gentle snap onto the cap axis
                write(pos, dist);
            }
            var drag = false;
            svg.addEventListener('pointerdown', function (e) {
                drag = true; svg.classList.add('hf-mp-live');
                if (svg.setPointerCapture) try { svg.setPointerCapture(e.pointerId); } catch (x) {}
                apply(e); e.preventDefault(); e.stopPropagation();
            });
            svg.addEventListener('pointermove',   function (e) { if (drag) { apply(e); e.preventDefault(); } });
            svg.addEventListener('pointerup',     function ()  { drag = false; svg.classList.remove('hf-mp-live'); });
            svg.addEventListener('pointercancel', function ()  { drag = false; svg.classList.remove('hf-mp-live'); });
            svg.addEventListener('dblclick', function (e) {                // double-click = back to the voiced spot
                icon.data('hf_micside', 1); write(0, 0);
                e.preventDefault(); e.stopPropagation();
            });
            micPadUpdate(icon);
        })();
    } else if (event.type == 'change') {
        var icon = event.icon, s = event.symbol;
        if (s) syncSel(icon, s, event.value);   // dropdown labels track every change
        if (s && /_pos$/.test(s)) {
            var b = s.replace(/_pos$/, ''), want = parseInt(event.value, 10), cur = posOf(icon, b);
            if (want === cur) { resort(icon); return; }
            if (inChain(icon, b)) moveToSlot(icon, funcs, b, want);
            else nodeOf(icon, b).attr('data-pos', want);
        } else if (s && /_bypass$/.test(s)) {
            nodeOf(icon, s.replace(/_bypass$/, '')).toggleClass('hf-byp', event.value > 0.5);
        } else if (s && /_enable$/.test(s)) {
            var eb = s.replace(/_enable$/, '');
            if (eb === 'it') { nodeOf(icon, 'it').toggleClass('hf-byp', !(event.value > 0.5)); }
            else {
                var on = event.value > 0.5, isin = inChain(icon, eb);
                if (on && !isin) { nodeOf(icon, eb).removeClass('hf-byp'); icon.find('.hf-nodes').append(nodeOf(icon, eb)); resort(icon); renderPalette(icon); }
                else if (!on && isin) { icon.find('.hf-palette').append(nodeOf(icon, eb)); resort(icon); renderPalette(icon); if (icon.data('hf_sel') === eb) selectNode(icon, 'it'); }
            }
        } else if (s === 'amp_model') {
            icon.data('hf_amp_m', parseInt(event.value, 10)); applyAmp(icon);
        } else if (s === 'amp_pamp_auto') {
            icon.data('hf_amp_auto', event.value > 0.5); applyAmp(icon);
        } else if (s === 'amp_mv_mode') {
            icon.data('hf_mv', parseInt(event.value, 10)); applyMesa(icon);
        } else if (s === 'amp_mv_eqpreset') {
            if (event.value > 0) loadEqPreset(icon, event.value);   // dropdown preset → load faders + back to Custom
        } else if (s === 'fz_pedal') {
            icon.data('hf_fz_p', parseInt(event.value, 10)); applyFuzz(icon);
        } else if (s === 'dr_model') {
            applyDrive(icon, parseInt(event.value, 10));
        } else if (eqTrack(icon, s, event.value)) {
            eqScope(icon);
        } else if (s === 'md_type') {
            var mt = parseInt(event.value, 10);
            setModelVal(icon, 'md', mt);
            show(icon, 'md', '.c-md-delay', mt === 0 || mt === 3 || mt === 6 || mt === 7);
        } else if (s === 'dl_type') {
            icon.data('hf_dl_t', parseInt(event.value, 10)); applyDelay(icon);
        } else if (s === 'cab_micpos') {
            icon.data('hf_micpos', parseFloat(event.value)); micPadUpdate(icon);
        } else if (s === 'cab_micdist') {
            icon.data('hf_micdist', parseFloat(event.value)); micPadUpdate(icon);
        } else if (s === 'oc_micro') {
            nodeOf(icon, 'oc').toggleClass('hf-oc-micro', parseFloat(event.value) > 0.0001);
        } else if (s === 'clip') {
            icon.find('.hf-clip').toggleClass('hf-clip-on', event.value > 0.5);
        } else if (s === 'tuner_on') {
            var ton = event.value > 0.5;
            icon.data('hf_tuner_on', ton);
            icon.find('[rata-role=tuner]').toggleClass('mod-hidden', !ton);
            icon.find('[rata-role=tunerbtn]').toggleClass('hf-on', ton);
        } else if (s === 'tuner_note') {
            tunerNote(icon, event.value);
        } else if (s === 'tuner_cents') {
            tunerCents(icon, event.value);
        } else if (s === 'ps_bank') {
            icon.data('ps_bank', parseInt(event.value, 10)); psBankLabel(icon); psRenderList(icon, funcs);
        } else if (s === 'ps_slot') {
            icon.data('ps_slot', parseInt(event.value, 10)); psBankLabel(icon); psRenderList(icon, funcs);
        } else if (event.uri && event.uri.indexOf('#irfile') >= 0) {
            setIr(icon, event.value);
        } else if (event.uri && event.uri.indexOf('#ampnam') >= 0) {
            setFile(icon, 'AmpNam', event.value, '-- choose a NAM file --');
        } else if (event.uri && event.uri.indexOf('#drnam') >= 0) {
            setFile(icon, 'DrNam', event.value, '-- choose a NAM file --');
        } else if (event.uri && event.uri.indexOf('#ps_index') >= 0) {
            var parts = ('' + event.value).split('|');
            if (parts.length >= 2) {
                var pb = parseInt(parts[0], 10), ps = parseInt(parts[1], 10);
                icon.data('ps_bank', pb); icon.data('ps_slot', ps); icon.data('ps_names', parts.slice(2));
                psBankLabel(icon); psRenderList(icon, funcs);
                psSetName(icon, parts[2 + pb * 4 + ps]);
            }
        } else if (event.uri && event.uri.indexOf('#ps_name') >= 0) {
            psSetName(icon, event.value);
        } else if (event.uri && event.uri.indexOf('#ps_apply') >= 0) {
            psApply(funcs, event.value, icon);
        } else if (event.uri && event.uri.indexOf('#meters') >= 0) {
            var mv = ('' + event.value).split('|');
            if (mv.length >= 2) { setMeter(icon, 'imeter', parseFloat(mv[0])); setMeter(icon, 'ometer', parseFloat(mv[1])); }
        } else if (event.uri && event.uri.indexOf('#tuner') >= 0) {
            var tv = ('' + event.value).split('|');
            if (tv.length >= 2) { tunerNote(icon, parseInt(tv[0], 10)); tunerCents(icon, parseFloat(tv[1])); }
        }
    }
}
