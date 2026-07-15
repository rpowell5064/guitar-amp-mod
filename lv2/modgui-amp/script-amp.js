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
        var hasVoice = !nam && (m === 2 || m === 3 || m === 4 || m === 6 || m === 11);
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
        // For Sunn the shared Gain knob IS the Normal-channel volume; for the Plexi it's Vol I.
        icon.find('[rata-role=gainlabel]').text(m === 3 ? 'Normal Vol' : (m === 10 ? 'Vol I' : (nam ? 'Output' : 'Gain')));
        // Cali V (11): 9-mode channel switcher + 5-band graphic EQ
        icon.find('[rata-role=mesagroup]').toggleClass('mod-hidden', m !== 11);
        // Per-model realistic faceplate skin + engraved badge (Forge parity)
        icon.find('[rata-role=ampface]').attr('class', 'hf-amp-face hf-face-m' + ((m >= 0 && m <= 11) ? m : 1));
        var NAMES = ['Clean Meanie','Crunchy McCrunchFace','Gainzilla','Doom Daddy','Tangerang','Neural','Beardo BE','Hi-Volt','Chime Thirty','Backline Plus','Plexiglass','Cali V'];
        icon.find('[rata-role=ampbadge]').text(NAMES[m] || 'AMP');
        // Which tabs make sense for this model, then keep the active tab in sync with the mode:
        // switching the model (via dropdown or preset recall) to/from Neural flips the tab too.
        refresh_tabs(icon, m);
        var cur = icon.data('amp_tab') || 'amp';
        if (nam && cur !== 'nam') set_tab(icon, 'nam');
        else if (!nam && cur === 'nam') set_tab(icon, 'amp');
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

    if (event.type == 'start') {
        var icon = event.icon;
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
        if ('model' in map) update_model(icon, map.model);
        if ('pamp_auto' in map) update_pa_auto(icon, map.pamp_auto);
    } else if (event.type == 'change') {
        if (event.symbol == 'model')
            update_model(event.icon, event.value);
        else if (event.symbol == 'pamp_auto')
            update_pa_auto(event.icon, event.value);
        else if (event.symbol == 'mv_eqpreset' && event.value > 0)
            apply_eq_preset(event.value);
        else if (event.uri && event.uri.indexOf('#nammodel') >= 0)
            set_nam(event.icon, event.value);
    }
}
