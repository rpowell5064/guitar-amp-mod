function (event, funcs) {
    // Model-aware control visibility. LV2 model indices:
    //   0 Fender, 1 Marshall, 2 EVH, 3 Sunn Model T, 4 Orange Rockerverb, 5 NAM,
    //   6 Beardo BE (Friedman), 7 Hi-Volt (Hiwatt), 8 Chime Thirty (Vox AC30), 9 Backline Plus (Peavey, solid-state),
    //   10 Plexiglass (Marshall Super Lead), 11 Cali V (Mesa Mark V — 9 modes + 5-band graphic EQ)
    var INTERNAL_DEFAULT = 1;   // fall-back internal amp when leaving Neural with no remembered model

    // ── Tabs: fold the stacked panels into one-at-a-time views (keeps the pedal small).
    //    The Neural tab doubles as the Internal⇄Neural mode switch (see the click wiring). ──
    function set_tab(icon, name) {
        icon.find('[rata-role=tab]').each(function () {
            this.classList.toggle('hf-tab-active', this.getAttribute('data-tab') === name);
        });
        icon.find('[rata-role=panel]').each(function () {
            this.classList.toggle('hf-tab-active', this.getAttribute('data-tab') === name);
        });
        icon.data('amp_tab', name);
    }
    // Show/hide the tab BUTTONS per model. Neural is ALWAYS shown (it's the mode switch);
    // Voicing/Power Amp are internal-model-specific, so they hide in Neural mode.
    function refresh_tabs(icon, m) {
        var nam = (m === 5);
        var hasVoice = !nam && (m === 2 || m === 3 || m === 4 || m === 6 || m === 11 || m === 12 || m === 13);
        var hasPower = !(m === 3 || nam);   // Sunn PA auto-bypassed; NAM capture has its own
        function tab(name, on) {
            var el = icon.find('[rata-role=tab][data-tab=' + name + ']')[0];
            if (el) el.classList.toggle('hf-tab-gone', !on);
        }
        tab('voice', hasVoice);
        tab('power', hasPower);
        tab('nam', true);
        var ok = { amp: true, voice: hasVoice, power: hasPower, nam: true };
        var cur = icon.data('amp_tab') || 'amp';
        if (!ok[cur]) set_tab(icon, 'amp');
    }

    // RULE (2026-09-13): the panel shows a knob only if the REAL amp has one.
    //   0 Deluxe Reverb  no Middle / Presence / Master
    //   4 Rockerverb     no Presence; its CLEAN channel also has no Middle / Master
    //   8 AC30 Top Boost no Middle / Master (its Presence IS the Cut control)
    //   9 Backstage Plus no Presence        10 Plexi  non-master amp
    //  13 MT15           no Presence        14 SVT    no Presence / Master
    // Neural (5) keeps all three: there they are our own post-capture EQ, not an amp's.
    // Visibility only — the stored value still feeds the model, so presets are untouched.
    function update_amp_ctls(icon) {
        var m = icon.data('amp_model'); if (m == null) m = 0;
        var rvClean = (m === 4 && (icon.data('amp_channel') || 0) > 0.5);
        icon.find('[rata-role=midctl]').toggleClass('mod-hidden',  (m === 0 || m === 8 || rvClean));
        icon.find('[rata-role=presctl]').toggleClass('mod-hidden', (m === 0 || m === 4 || m === 9 || m === 13 || m === 14 || m === 15));
        icon.find('[rata-role=mastctl]').toggleClass('mod-hidden', (m === 0 || m === 8 || m === 10 || m === 14 || rvClean));
    }

    function update_model(icon, value) {
        var m = parseInt(value, 10);
        var nam = (m === 5);
        icon.data('amp_model', m);
        // Sunn-only controls (Brite Vol + Ch Link)
        icon.find('[rata-role=sunngroup]').toggleClass('mod-hidden', m !== 3);
        // Channel toggle: EVH (2) + Rockerverb (4)
        icon.find('[rata-role=channelctl]').toggleClass('mod-hidden', !(m === 2 || m === 4));
        // Beardo BE (6): 3-way channel + Fat/C45/Sat
        icon.find('[rata-role=friedmangroup]').toggleClass('mod-hidden', m !== 6);
        // Resonance: EVH (2) only
        icon.find('[rata-role=resonancectl]').toggleClass('mod-hidden', m !== 2);
        // Plexiglass (10): Vol II — the 1959's jumpered Normal-channel volume
        icon.find('[rata-role=plexivol2]').toggleClass('mod-hidden', m !== 10);
        icon.find('[rata-role=jcmsir34]').toggleClass('mod-hidden', m !== 1);
        // For Sunn the shared Gain knob IS the Normal-channel volume; for the Plexi it's Vol I.
        icon.find('[rata-role=gainlabel]').text(m === 3 ? 'Normal Vol' : (m === 10 ? 'Vol I' : (nam ? 'Output' : 'Gain')));
        // Cali V (11): 9-mode channel switcher + 5-band graphic EQ
        icon.find('[rata-role=mesagroup]').toggleClass('mod-hidden', m !== 11);
        // Diamond Plate (12): 8-mode channel switcher + Variac/Rectifier feel switches
        icon.find('[rata-role=rectogroup]').toggleClass('mod-hidden', m !== 12);
        // Tremont 15 (13): Clean/Crunch/Lead + bright switch
        icon.find('[rata-role=mt15group]').toggleClass('mod-hidden', m !== 13);
        // Blue Liner (14): Ultra-Lo/Ultra-Hi + 3-position mid selector (bass)
        icon.find('[rata-role=svtgroup]').toggleClass('mod-hidden', m !== 14);
        // Per-model realistic faceplate skin + engraved badge (Forge parity)
        var faceEl = icon.find('[rata-role=ampface]');
        faceEl.attr('class', 'hf-amp-face hf-face-m' + ((m >= 0 && m <= 15) ? m : 1));
        // Neon: start the colour cycle at a random point, ONCE per panel, so a session
        // doesn't always open on red.
        if (faceEl.length) {
            var fel = faceEl[0];
            if (!fel.getAttribute('data-neon-seeded')) {
                fel.setAttribute('data-neon-seeded', '1');
                fel.style.setProperty('--neon-delay', '-' + (Math.random() * 240).toFixed(2) + 's');
            }
        }
        var NAMES = ['Clean Meanie','Crunchy McCrunchFace','Gainzilla','Doom Daddy','Tangerang','Neural','Beardo BE','Hi-Volt','Chime Thirty','Backline Plus','Plexiglass','Cali V','Diamond Plate','Tremont 15','Blue Liner','Citrus 200'];
        icon.find('[rata-role=ampbadge]').text(NAMES[m] || 'AMP');
        // Which tabs make sense for this model, then keep the active tab in sync with the mode:
        // switching the model (via dropdown or preset recall) to/from Neural flips the tab too.
        refresh_tabs(icon, m);
        var cur = icon.data('amp_tab') || 'amp';
        if (nam && cur !== 'nam') set_tab(icon, 'nam');
        else if (!nam && cur === 'nam') set_tab(icon, 'amp');
        update_amp_ctls(icon);
        update_comp(icon);
    }
    // Write the model port (mode switch / tab click) + refresh the UI deterministically
    // (mod-ui doesn't reliably echo set_port_value back as a change event).
    function set_model(icon, m) {
        if (funcs && typeof funcs.set_port_value === 'function') funcs.set_port_value('model', m);
        update_model(icon, m);
    }
    function update_pa_auto(icon, value) {
        var auto = value > 0.5;
        icon.find('[rata-role=pamanual]').toggleClass('mod-hidden', auto);
    }
    // Component Build (2026-09-23): the ENGINE group shows only for models with a
    // schematic-exact twin (keep HAS_COMP in sync with hasComponentModel in amp_plugin.cpp);
    // with the twin on, its own power section runs and the shared Power Amp face hides.
    var HAS_COMP = { 1: 1, 2: 1, 4: 1, 6: 1, 8: 1, 10: 1, 11: 1, 12: 1, 14: 1 };
    function update_comp(icon) {
        var m = icon.data('amp_model'); if (m == null) m = 0;
        var has = !!HAS_COMP[m];
        var on = has && (icon.data('amp_comp') || 0) > 0.5;
        icon.find('[rata-role=compgroup]').toggleClass('mod-hidden', !has);
        icon.find('[rata-role=compctls]').toggleClass('mod-hidden', !on);
        icon.find('[rata-role=pagroup]').toggleClass('mod-hidden', on);
    }
    // Show the loaded NAM file name (fires on user pick AND on pedalboard load).
    function set_nam(icon, value) {
        var box = icon.find('[rata-role=Nam]');
        if (value == null || value === 'None' || value === '') { box.text('-- choose a NAM file --'); return; }
        var label = null;
        icon.find('[mod-role=enumeration-option]').each(function () {
            if (this.getAttribute('mod-parameter-value') == value)
                label = (this.textContent || '').replace(/^\s+|\s+$/g, '');
        });
        if (!label) { var s = '' + value; s = s.substring(s.lastIndexOf('/') + 1); s = s.substring(s.lastIndexOf('\\') + 1); label = s; }
        box.text(label);
    }
    // Cali V graphic-EQ presets: selecting a preset LOADS its curve into the 5 sliders and switches
    // back to Custom (0), so the faders visibly jump to the preset and stay tweakable. Port value =
    // 0.5 + dB/24 (the DSP's mapping); dB curves mirror kEqPresets in MesaMarkV.cpp.
    var EQ_PRESETS = {
        1: [0.5, 0.5, 0.5, 0.5, 0.5],                         // Flat
        2: [0.66667, 0.58333, 0.25, 0.54167, 0.70833],        // V-Scoop
        3: [0.75, 0.54167, 0.08333, 0.41667, 0.75],           // Deep V
        4: [0.41667, 0.58333, 0.70833, 0.625, 0.45833],       // Mid Boost
        5: [0.45833, 0.41667, 0.41667, 0.625, 0.75]           // Bright
    };
    function apply_eq_preset(value) {
        var pv = EQ_PRESETS[parseInt(value, 10)];
        if (!pv || !funcs || typeof funcs.set_port_value !== 'function') return;
        for (var i = 0; i < 5; i++) funcs.set_port_value('mv_geq' + i, pv[i]);
        funcs.set_port_value('mv_eqpreset', 0);   // back to Custom → the loaded sliders drive the EQ
    }

    // Dropdown labels (2026-07-23): mod-ui doesn't reliably render the selected
    // scale-point label into custom-select widgets — sync them ourselves from a
    // per-symbol cache (built at start), on option clicks and change events.
    function syncSel(icon, sym, val) {
        var m = icon.data('hx_selmap'); var els = m && m[sym]; if (!els) return;
        els.forEach(function (el) {
            var sel = el.querySelector('.mod-enumerated-selected'); if (!sel) return;
            var lab = null;
            Array.prototype.forEach.call(el.querySelectorAll('[mod-role=enumeration-option]'), function (o) {
                if (parseFloat(o.getAttribute('mod-port-value')) == parseFloat(val)) lab = (o.textContent || '').replace(/^\s+|\s+$/g, '');
            });
            if (lab != null) sel.textContent = lab;
        });
    }
    function buildSelMap(icon, ports) {
        var m = {};
        icon.find('[mod-widget=custom-select][mod-port-symbol]').each(function () {
            var sym = this.getAttribute('mod-port-symbol');
            (m[sym] = m[sym] || []).push(this);
            var el = this;
            Array.prototype.forEach.call(el.querySelectorAll('[mod-role=enumeration-option]'), function (o) {
                o.addEventListener('click', function () {
                    syncSel(icon, sym, o.getAttribute('mod-port-value'));
                    // Beardo BE lands on its HBE channel when picked BY HAND (user
                    // 2026-07-30); preset recalls keep their saved channel.
                    if (sym === 'model' && parseInt(o.getAttribute('mod-port-value'), 10) === 6
                        && funcs && typeof funcs.set_port_value === 'function') {
                        funcs.set_port_value('fr_channel', 2); syncSel(icon, 'fr_channel', 2);
                    }
                });
            });
        });
        icon.data('hx_selmap', m);
        (ports || []).forEach(function (p) { if (m[p.symbol]) syncSel(icon, p.symbol, p.value); });
    }
    // ── GUITAR / BASS filter for the model list. The bass amps are Blue Liner (14) and
    // Citrus 200 (15); Neural (5) is whatever you load into it, so it belongs to both.
    // This hides LIST OPTIONS ONLY — the port value is never touched, so a preset that
    // recalls a hidden model still sounds exactly right.
    var RIG_BASS_AMPS = { 14: 1, 15: 1 };
    var RIG_BOTH_AMPS = { 5: 1 };
    function rigOf(idx) { return RIG_BOTH_AMPS[idx] ? 'both' : (RIG_BASS_AMPS[idx] ? 'bass' : 'guitar'); }
    function rigShow(want, has) { return want === 'all' || has === 'both' || has === want; }
    function applyRigFilter(icon) {
        var want = icon.data('hf_rig_amp') || 'all';
        icon.find('[mod-widget=custom-select][mod-port-symbol="model"] [mod-role=enumeration-option]').each(function () {
            var v = parseInt(this.getAttribute('mod-port-value'), 10);
            this.style.display = rigShow(want, rigOf(v)) ? '' : 'none';
        });
        icon.find('[rata-role=rigfilter] .hf-rig-btn').each(function () {
            this.classList.toggle('hf-rig-on', this.getAttribute('data-rig') === want);
        });
    }
    function bindRigFilter(icon) {
        icon.find('[rata-role=rigfilter]').each(function () {
            Array.prototype.forEach.call(this.querySelectorAll('.hf-rig-btn'), function (b) {
                b.addEventListener('click', function (e) {
                    e.stopPropagation();
                    icon.data('hf_rig_amp', b.getAttribute('data-rig'));
                    applyRigFilter(icon);
                });
            });
        });
        applyRigFilter(icon);
    }
    if (event.type == 'start') {
        var icon = event.icon;
        buildSelMap(icon, event.ports);
        bindRigFilter(icon);
        // Show the loaded NAM file immediately (2026-07-23): mod-ui applies the patch
        // write on option click but doesn't reliably echo a change event back.
        (event.parameters || []).forEach(function (pr) {
            if (pr.uri && pr.uri.indexOf('#nammodel') >= 0) set_nam(icon, pr.value);
        });
        icon.find('[mod-role=input-parameter] [mod-role=enumeration-option]').each(function () {
            var el = this;
            el.addEventListener('click', function () {
                set_nam(icon, el.getAttribute('mod-parameter-value'));
            });
        });
        // Tab buttons. The Neural tab is the mode switch: clicking it puts the amp into Neural
        // (model 5) and remembers the internal model; clicking any internal tab restores it.
        icon.find('[rata-role=tab]').each(function () {
            var el = this;
            el.addEventListener('click', function (e) {
                e.stopPropagation();
                if (el.classList.contains('hf-tab-gone')) return;
                var t = el.getAttribute('data-tab');
                var cur = icon.data('amp_model'); if (cur == null) cur = INTERNAL_DEFAULT;
                if (t === 'nam') {
                    if (cur !== 5) { icon.data('amp_last_internal', cur); set_model(icon, 5); }
                    set_tab(icon, 'nam');
                } else {
                    if (cur === 5) {
                        var li = icon.data('amp_last_internal');
                        if (li == null || li === 5) li = INTERNAL_DEFAULT;
                        set_model(icon, li);
                    }
                    set_tab(icon, t);
                }
            });
        });
        set_tab(icon, 'amp');
        // Seed model-aware visibility from START values (avoids a flash of all tabs).
        var map = {};
        (event.ports || []).forEach(function (p) { map[p.symbol] = p.value; });
        if ('channel' in map) icon.data('amp_channel', map.channel);
        if ('comp' in map) icon.data('amp_comp', map.comp);
        if ('model' in map) update_model(icon, map.model);
        if ('pamp_auto' in map) update_pa_auto(icon, map.pamp_auto);
        update_amp_ctls(icon);
    } else if (event.type == 'change') {
        if (event.symbol) syncSel(event.icon, event.symbol, event.value);
        if (event.symbol == 'model')
            update_model(event.icon, event.value);
        else if (event.symbol == 'channel')
            { event.icon.data('amp_channel', event.value); update_amp_ctls(event.icon); }
        else if (event.symbol == 'pamp_auto')
            update_pa_auto(event.icon, event.value);
        else if (event.symbol == 'comp')
            { event.icon.data('amp_comp', event.value); update_comp(event.icon); }
        else if (event.symbol == 'mv_eqpreset' && event.value > 0)
            apply_eq_preset(event.value);
        else if (event.uri && event.uri.indexOf('#nammodel') >= 0)
            set_nam(event.icon, event.value);
    }
}
