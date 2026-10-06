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


    // ── GUITAR readout ───────────────────────────────────────────────────────
    // The Guitar selector changes the CHARACTER of the guitar plugged in. It is
    // a fixed layer in the plugin (PickupVoicer kR[3], kR[4]), not a set of
    // knob moves, so the panel spells out exactly what it applies: the level it
    // adds at the input, the voicing bands, and which controls it leaves alone.
    // Keep these numbers in step with lv2/common/PickupVoicer.h.
    var GUITAR_NOTE = [
        { h: 'Default', rows: ['No change: the Input Trim exactly as its controls are set.'] },
        { h: 'Single coil \u2192 humbucker character',
          rows: ['Input level: +6 dB (a single coil sits well under a humbucker).',
                 'Voicing: the \u201959 Bucker humbucker curve \u2014 2 kHz +2.5 dB, 4.5 kHz \u22125 dB, top shelf \u221213 dB above 4 kHz.',
                 'Gain, Phase, Hum Filter, Pickup Load: as set. Humbucker Voicing and Clean Boost: as set, applied after this.'] },
        { h: 'Hot humbucker \u2192 vintage PAF character',
          rows: ['Input level: \u22124 dB (a hot bridge pickup sits 4\u20136 dB above a PAF).',
                 'Voicing: 85 Hz shelf \u22120.8 dB, 2.1 kHz +0.9, 3 kHz +2.1, 5 kHz \u22122.4, top shelf \u22123.3 dB above 6.8 kHz.',
                 'Gain, Phase, Hum Filter, Pickup Load: as set. Humbucker Voicing and Clean Boost: as set, applied after this.'] }
    ];
    function guitarNote(icon, v) {
        var box = icon.find('[rata-role=gnote]'); if (!box.length) return;
        var g = GUITAR_NOTE[Math.max(0, Math.min(2, Math.round(parseFloat(v) || 0)))];
        var html = '<b>' + g.h + '</b>';
        for (var i = 0; i < g.rows.length; ++i) html += '<span>' + g.rows[i] + '</span>';
        box[0].innerHTML = html;
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
        if (event.symbol === 'guitar') guitarNote(event.icon, event.value);
    }
}
