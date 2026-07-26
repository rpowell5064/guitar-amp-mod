function (event, funcs) {
    // Center Delay pushes the modulation centre out, so it only applies to the delay-line
    // types: 0 Lush-2 (chorus), 3 Flanger, 6 Nevermind Chorus (Small Clone), 7 Seasick Vibe.
    // The phase/amplitude types (Uni-Verse, Phaser, Tremolo, Rotary) have no delay line.
    function update_type(icon, value) {
        var t = parseInt(value, 10);
        var hasDelay = (t === 0 || t === 3 || t === 6 || t === 7);
        icon.find('[rata-role=offsetctl]').toggleClass('mod-hidden', !hasDelay);
        // Shape (bias/opto/harmonic waveform) only applies to Tremolo (type 4).
        icon.find('[rata-role=shapectl]').toggleClass('mod-hidden', t !== 4);
    }
    if (event.type == 'start') {
        var icon = event.icon, map = {};
        (event.ports || []).forEach(function (p) { map[p.symbol] = p.value; });
        if ('mod_type' in map) update_type(icon, map.mod_type);
    } else if (event.type == 'change') {
        if (event.symbol == 'mod_type') update_type(event.icon, event.value);
    }
}
