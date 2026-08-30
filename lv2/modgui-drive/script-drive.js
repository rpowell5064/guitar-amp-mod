function (event, funcs) {
    // Model: 0 Green Man (TS-808), 1 New Dawn (Life Pedal), 2 Dear Rodent Boy (RAT),
    //        3 Neural (NAM), 4 Grunge DS, 5 Gilded Horse, 6 Super Nova, 7 Preamp 250, 8 Echo Primer,
    //        9 Tube Chauffeur, 10 Helsinki Grind (B7K-style: Mix = Blend, Tone = Grunt fat/tight).
    var NAM_IDX = 3, INTERNAL_DEFAULT = 0;
    function update_model(icon, value) {
        var m = parseInt(value, 10);
        var nam = (m === NAM_IDX);
        icon.data('drive_model', m);
        // Octave belongs to New Dawn (1) only.
        icon.find('[rata-role=octavectl]').toggleClass('mod-hidden', m !== 1);
        // NAM file picker only when Neural (NAM) is selected; the model dropdown only when Internal.
        icon.find('[rata-role=namgroup]').toggleClass('mod-hidden', !nam);
        icon.find('[rata-role=modelgroup]').toggleClass('mod-hidden', nam);
        // Drive + Tone + Level don't affect a NAM capture, so hide them in Neural mode; the NAM
        // path has its own Gain (input) + Level (output) knobs shown instead. Mix stays for both.
        icon.find('[rata-role=algctl]').toggleClass('mod-hidden', nam);
        icon.find('[rata-role=namctl]').toggleClass('mod-hidden', !nam);
        // MODE switch highlight follows the model.
        icon.find('[rata-role=modebtn]').each(function () {
            this.classList.toggle('hx-mode-on', this.getAttribute('data-mode') === (nam ? 'nam' : 'int'));
        });
    }
    // Write the model port (from the MODE switch) + refresh the UI deterministically
    // (mod-ui doesn't reliably echo set_port_value back as a change event).
    function set_model(icon, m) {
        if (funcs && typeof funcs.set_port_value === 'function') funcs.set_port_value('model', m);
        update_model(icon, m);
    }
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

    if (event.type == 'start') {
        var icon = event.icon;
        // MODE switch: Neural puts the pedal on the NAM slot (model 3) and remembers the internal
        // model; Internal restores it. Clicks write the model port + refresh the UI.
        icon.find('[rata-role=modebtn]').each(function () {
            var el = this;
            el.addEventListener('click', function (e) {
                e.stopPropagation();
                var mode = el.getAttribute('data-mode');
                var cur = icon.data('drive_model'); if (cur == null) cur = INTERNAL_DEFAULT;
                if (mode === 'nam') {
                    if (cur !== NAM_IDX) { icon.data('drive_last_internal', cur); set_model(icon, NAM_IDX); }
                } else {
                    if (cur === NAM_IDX) {
                        var li = icon.data('drive_last_internal');
                        if (li == null || li === NAM_IDX) li = INTERNAL_DEFAULT;
                        set_model(icon, li);
                    }
                }
            });
        });
        // Seed from START values (avoids a flash of the wrong mode).
        var map = {};
        (event.ports || []).forEach(function (p) { map[p.symbol] = p.value; });
        if ('model' in map) update_model(icon, map.model);
    } else if (event.type == 'change') {
        if (event.symbol == 'model')
            update_model(event.icon, event.value);
        else if (event.uri && event.uri.indexOf('#nammodel') >= 0)
            set_nam(event.icon, event.value);
    }
}
