function (event, funcs) {
    // Pedal-aware control visibility. Pedal: 0 = Italian Hero (Muff), 1 = Tone Bender MkII,
    // 2 = Octavia (octave-up fuzz: uses Tone, no Variant, no Tone-Bender knobs).
    function update_pedal(icon, value) {
        var p = parseInt(value, 10);
        var tb = p === 1, oct = p === 2, ff = p === 3;
        // Variant selector — Italian Hero only
        icon.find('[rata-role=varonly]').toggleClass('mod-hidden', p !== 0);
        // Tone knob — Italian Hero + Octavia (not Tone Bender, not Fuzz Zachary)
        icon.find('[rata-role=ihgroup]').toggleClass('mod-hidden', tb || ff);
        // Bias / Trim / Temp trio — Tone Bender AND Fuzz Zachary (relabeled Comp/Gate/Stab for FF)
        icon.find('[rata-role=tbgroup]').toggleClass('mod-hidden', !(tb || ff));
        // Shared knobs relabeled per pedal
        icon.find('[rata-role=sustainlabel]').text(tb ? 'Attack' : (oct ? 'Fuzz' : (ff ? 'Drive' : 'Sustain')));
        icon.find('[rata-role=volumelabel]').text(tb ? 'Level'  : 'Volume');
        icon.find('[rata-role=biaslabel]').text(ff ? 'Comp' : 'Bias');
        icon.find('[rata-role=trimlabel]').text(ff ? 'Gate' : 'Input Trim');
        icon.find('[rata-role=templabel]').text(ff ? 'Stab' : 'Ge Temp');
        icon.find('[rata-role=tagline]').text(tb ? 'I Know It · germanium'
                                            : (oct ? 'Octavia · octave-up fuzz'
                                            : (ff ? 'Fuzz Zachary · gated chaos'
                                                   : 'Italian Hero · 6 variants')));
    }

    if (event.type == 'start') {
        // Seed pedal-aware visibility from START values so the Variant selector only shows for
        // pedals that HAVE variants (Italian Hero) — don't rely on an echoed change event.
        var icon = event.icon, map = {};
        (event.ports || []).forEach(function (p) { map[p.symbol] = p.value; });
        if ('pedal' in map) update_pedal(icon, map.pedal);
    } else if (event.type == 'change') {
        if (event.symbol == 'pedal')
            update_pedal(event.icon, event.value);
    }
}
