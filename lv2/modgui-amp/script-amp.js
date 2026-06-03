function (event, funcs) {
    // Model-aware control visibility. LV2 model indices (NAM removed):
    //   0 Fender, 1 Marshall, 2 EVH, 3 Sunn Model T, 4 Orange Rockerverb
    function update_model(icon, value) {
        var m = parseInt(value, 10);
        // Sunn-only controls (Brite Vol + Ch Link)
        icon.find('[rata-role=sunngroup]').toggleClass('mod-hidden', m !== 3);
        // Channel toggle: EVH (2) + Rockerverb (4)
        icon.find('[rata-role=channelctl]').toggleClass('mod-hidden', !(m === 2 || m === 4));
        // Resonance: EVH (2) only
        icon.find('[rata-role=resonancectl]').toggleClass('mod-hidden', m !== 2);
        // Power-amp section: hidden for Sunn (its PA is auto-bypassed)
        icon.find('[rata-role=pagroup]').toggleClass('mod-hidden', m === 3);
    }
    // Manual power-amp controls are only active when PA Auto is off.
    function update_pa_auto(icon, value) {
        var auto = value > 0.5;
        icon.find('[rata-role=pamanual]').toggleClass('mod-hidden', auto);
    }

    if (event.type == 'start') {
        // initial port values arrive as 'change' events after start
    } else if (event.type == 'change') {
        if (event.symbol == 'model')
            update_model(event.icon, event.value);
        else if (event.symbol == 'pamp_auto')
            update_pa_auto(event.icon, event.value);
    }
}
