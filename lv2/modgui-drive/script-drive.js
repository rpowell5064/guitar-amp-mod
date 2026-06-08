function (event, funcs) {
    // Model: 0 Green Man (TS-808), 1 New Dawn (Life Pedal), 2 Dear Rodent Boy (RAT),
    //        3 Neural (NAM).
    function update_model(icon, value) {
        var m = parseInt(value, 10);
        // Octave belongs to New Dawn (1) only.
        icon.find('[rata-role=octavectl]').toggleClass('mod-hidden', m !== 1);
        // NAM file picker only when Neural (NAM) is selected.
        icon.find('[rata-role=namgroup]').toggleClass('mod-hidden', m !== 3);
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
    } else if (event.type == 'change') {
        if (event.symbol == 'model')
            update_model(event.icon, event.value);
        else if (event.uri && event.uri.indexOf('#nammodel') >= 0)
            set_nam(event.icon, event.value);
    }
}
