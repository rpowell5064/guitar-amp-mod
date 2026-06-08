function (event, funcs) {
    // Pedal-aware control visibility. Pedal: 0 = Italian Hero (Muff), 1 = Tone Bender MkII.
    function update_pedal(icon, value) {
        var tb = parseInt(value, 10) === 1;
        // Italian Hero controls (Variant selector + Tone knob)
        icon.find('[rata-role=ihgroup]').toggleClass('mod-hidden', tb);
        // Tone Bender controls (Bias / Input Trim / Ge Temp)
        icon.find('[rata-role=tbgroup]').toggleClass('mod-hidden', !tb);
        // Shared knobs relabeled per pedal
        icon.find('[rata-role=sustainlabel]').text(tb ? 'Attack' : 'Sustain');
        icon.find('[rata-role=volumelabel]').text(tb ? 'Level'  : 'Volume');
        icon.find('[rata-role=tagline]').text(tb ? 'I know it · germanium'
                                                 : 'Italian Hero · 6 variants');
    }

    if (event.type == 'start') {
        // initial port values arrive as 'change' events after start
    } else if (event.type == 'change') {
        if (event.symbol == 'pedal')
            update_pedal(event.icon, event.value);
    }
}
