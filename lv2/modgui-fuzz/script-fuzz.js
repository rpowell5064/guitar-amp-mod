function (event, funcs) {
    // Pedal-aware control visibility. Pedal: 0 = Italian Hero (Muff), 1 = Tone Bender MkII,
    // 2 = Octavia (octave-up fuzz: uses Tone, no Variant, no Tone-Bender knobs).
    function update_pedal(icon, value) {
        var p = parseInt(value, 10);
        var tb = p === 1, oct = p === 2;
        // Variant selector — Italian Hero only
        icon.find('[rata-role=varonly]').toggleClass('mod-hidden', p !== 0);
        // Tone knob — Italian Hero + Octavia (not Tone Bender)
        icon.find('[rata-role=ihgroup]').toggleClass('mod-hidden', tb);
        // Tone Bender controls (Bias / Input Trim / Ge Temp)
        icon.find('[rata-role=tbgroup]').toggleClass('mod-hidden', !tb);
        // Shared knobs relabeled per pedal
        icon.find('[rata-role=sustainlabel]').text(tb ? 'Attack' : (oct ? 'Fuzz' : 'Sustain'));
        icon.find('[rata-role=volumelabel]').text(tb ? 'Level'  : 'Volume');
        icon.find('[rata-role=tagline]').text(tb ? 'I Know It · germanium'
                                            : (oct ? 'Octavia · octave-up fuzz'
                                                   : 'Italian Hero · 6 variants'));
    }

    if (event.type == 'start') {
        // initial port values arrive as 'change' events after start
    } else if (event.type == 'change') {
        if (event.symbol == 'pedal')
            update_pedal(event.icon, event.value);
    }
}
