function (event, funcs) {
    // Cabinets load IMPULSE RESPONSES only (NAM models amps/pedals, not cabinets — the Neural
    // source toggle was removed 2026-07-13). This just renders the loaded IR's label.
    function set_irfile(icon, value) {
        var box = icon.find('[rata-role=Ir]');
        if (value == null || value == 'None' || value == '' || value == '@factory') {
            box.text('Factory Cab (built-in)');
            return;
        }
        var label = null;
        icon.find('[mod-role=enumeration-option]').each(function () {
            if (this.getAttribute('mod-parameter-value') == value)
                label = (this.textContent || '').replace(/^\s+|\s+$/g, '');
        });
        if (!label) { var s = '' + value; s = s.substring(s.lastIndexOf('/') + 1); s = s.substring(s.lastIndexOf('\\') + 1); label = s; }
        box.text(label);
    }

    if (event.type == 'change') {
        if (event.uri == 'https://rpowell5064.github.io/guitaramp-suite/cab#irfile')
            set_irfile(event.icon, event.value);
    }
}
