function (event, funcs) {
    // Model-aware control visibility. LV2 model indices:
    //   0 Fender, 1 Marshall, 2 EVH, 3 Sunn Model T, 4 Orange Rockerverb, 5 NAM,
    //   6 Beardo BE (Friedman), 7 Hi-Volt (Hiwatt), 8 Chime Thirty (Vox AC30), 9 Backline Plus (Peavey, solid-state),
    //   10 Plexiglass (Marshall Super Lead), 11 Cali V (Mesa Mark V — 9 modes + 5-band graphic EQ)
    function update_model(icon, value) {
        var m = parseInt(value, 10);
        var nam = (m === 5);
        // Sunn-only controls (Brite Vol + Ch Link)
        icon.find('[rata-role=sunngroup]').toggleClass('mod-hidden', m !== 3);
        // Channel toggle: EVH (2) + Rockerverb (4)
        icon.find('[rata-role=channelctl]').toggleClass('mod-hidden', !(m === 2 || m === 4));
        // Beardo BE (6): 3-way channel + Fat/C45/Sat
        icon.find('[rata-role=friedmangroup]').toggleClass('mod-hidden', m !== 6);
        // Resonance: EVH (2) only
        icon.find('[rata-role=resonancectl]').toggleClass('mod-hidden', m !== 2);
        // Power-amp section: hidden for Sunn (auto-bypassed) and NAM (capture has its own)
        icon.find('[rata-role=pagroup]').toggleClass('mod-hidden', m === 3 || nam);
        // NAM file picker: only when Neural (NAM) is selected
        icon.find('[rata-role=namgroup]').toggleClass('mod-hidden', !nam);
        // For Sunn the shared Gain knob IS the Normal-channel volume; relabel it.
        icon.find('[rata-role=gainlabel]').text(m === 3 ? 'Normal Vol' : (nam ? 'Output' : 'Gain'));
        // Cali V (11): 9-mode channel switcher + 5-band graphic EQ
        icon.find('[rata-role=mesagroup]').toggleClass('mod-hidden', m !== 11);
        // Per-model realistic faceplate skin + engraved badge (Forge parity)
        icon.find('[rata-role=ampface]').attr('class', 'hf-amp-face hf-face-m' + ((m >= 0 && m <= 11) ? m : 1));
        var NAMES = ['Clean Meanie','Crunchy McCrunchFace','Gainzilla','Doom Daddy','Tangerang','Neural','Beardo BE','Hi-Volt','Chime Thirty','Backline Plus','Plexiglass','Cali V'];
        icon.find('[rata-role=ampbadge]').text(NAMES[m] || 'AMP');
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

    if (event.type == 'start') {
        // initial port values arrive as 'change' events after start
    } else if (event.type == 'change') {
        if (event.symbol == 'model')
            update_model(event.icon, event.value);
        else if (event.symbol == 'pamp_auto')
            update_pa_auto(event.icon, event.value);
        else if (event.uri && event.uri.indexOf('#nammodel') >= 0)
            set_nam(event.icon, event.value);
    }
}
