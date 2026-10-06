function (event, funcs) {
    // Tabs: fold the three feature groups (Input / Humbucker / Boost) into one-at-a-time
    // views so the pedal stays compact. Pure UI — no port writes.
    function set_tab(icon, name) {
        icon.find('[rata-role=tab]').each(function () {
            this.classList.toggle('hf-tab-active', this.getAttribute('data-tab') === name);
        });
        icon.find('[rata-role=panel]').each(function () {
            this.classList.toggle('hf-tab-active', this.getAttribute('data-tab') === name);
        });
    }


    // Dropdown label sync: mod-ui does not always repaint a custom-select after a
    // programmatic write, so set the visible label from the option list by hand.
    function selSync(icon, sym, val) {
        icon.find('.mod-enumerated[mod-port-symbol="' + sym + '"]').each(function () {
            var sel = this.querySelector('.mod-enumerated-selected'), lab = null;
            var opts = this.querySelectorAll('[mod-role=enumeration-option]');
            for (var i = 0; i < opts.length; ++i)
                if (Math.round(parseFloat(opts[i].getAttribute('mod-port-value'))) === Math.round(val)) lab = opts[i].textContent;
            if (sel && lab != null) sel.textContent = lab;
        });
    }

    // ── GUITAR preset: writes the controls ───────────────────────────────────
    // The Guitar selector changes the CHARACTER of the guitar plugged in by
    // setting the Input Trim's own controls, so every knob and toggle it uses
    // is visible and can be nudged afterwards. (It used to be a hidden
    // processing layer -- the user could not see what it did.) Single Coil is
    // the '59 Bucker voicing at 100 % plus 2 dB of Gain; Hot Pickups is the
    // Hot -> PAF voicing at 100 % with 4 dB taken off. Default puts the
    // voicing off and the Gain at 0.
    var GUITAR_SET = [
        { h: 'Default \u2014 no character change',
          w: [['humbucker', 0, 'Humbucker', 'OFF'], ['gain_db', 0, 'Gain', '0.0 dB']] },
        { h: 'Single coil \u2192 humbucker character',
          w: [['gain_db', 2, 'Gain', '+2.0 dB'], ['humbucker', 1, 'Humbucker', 'ON'],
              ['hb_model', 0, 'HB Model', '\u201959 Bucker'], ['hb_amount', 1, 'HB Amount', '100 %']] },
        { h: 'Hot humbucker \u2192 vintage PAF character',
          w: [['gain_db', -4, 'Gain', '\u22124.0 dB'], ['humbucker', 1, 'Humbucker', 'ON'],
              ['hb_model', 3, 'HB Model', 'Hot \u2192 PAF'], ['hb_amount', 1, 'HB Amount', '100 %']] }
    ];
    function guitarNote(icon, v) {
        var box = icon.find('[rata-role=gnote]'); if (!box.length) return;
        var g = GUITAR_SET[Math.max(0, Math.min(2, Math.round(parseFloat(v) || 0)))];
        var html = '<b>' + g.h + '</b><span class="hf-gnote-k">Sets</span>';
        for (var i = 0; i < g.w.length; ++i)
            html += '<span><i>' + g.w[i][2] + '</i> \u2192 ' + g.w[i][3] + '</span>';
        html += '<span class="hf-gnote-k">Leaves as set</span><span>Phase, Hum Filter, Mains, Pickup Load, Clean Boost.</span>';
        box[0].innerHTML = html;
    }
    // Write the preset's controls to the host. `fns` is the modgui function set.
    function guitarApply(icon, v, fns) {
        guitarNote(icon, v);
        if (!fns || typeof fns.set_port_value !== 'function') return;
        var g = GUITAR_SET[Math.max(0, Math.min(2, Math.round(parseFloat(v) || 0)))];
        var pvm = icon.data('hf_portv');
        for (var i = 0; i < g.w.length; ++i) {
            fns.set_port_value(g.w[i][0], g.w[i][1]);
            if (pvm) pvm[g.w[i][0]] = g.w[i][1];
            if (typeof syncSel === 'function') syncSel(icon, g.w[i][0], g.w[i][1]);
            else selSync(icon, g.w[i][0], g.w[i][1]);
        }
    }

    if (event.type == 'start') {
        var icon = event.icon;
        var gv = 0;
        (event.ports || []).forEach(function (p) { if (p.symbol === 'guitar') gv = p.value; });
        guitarNote(icon, gv);
        icon.find('[rata-role=tab]').each(function () {
            var el = this;
            el.addEventListener('click', function (e) {
                e.stopPropagation();
                set_tab(icon, el.getAttribute('data-tab'));
            });
        });
        set_tab(icon, 'input');
    } else if (event.type == 'change') {
        if (event.symbol === 'guitar') guitarApply(event.icon, event.value, funcs);   // the selector writes its controls
    }
}
