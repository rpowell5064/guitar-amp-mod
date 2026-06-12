function (event, funcs) {
    // Restore / update the IR picker's displayed name. mod-ui fires a 'change'
    // for the irfile parameter both on user pick AND on pedalboard load (state
    // restore), so setting the text here is what makes the name persist.
    function set_irfile(icon, value) {
        var box = icon.find('[rata-role=Ir]');
        // Empty / sentinel path == the always-available built-in Factory Cab.
        if (value == null || value == 'None' || value == '' || value == '@factory') {
            box.text('Factory Cab (built-in)');
            return;
        }
        // Prefer the matching list option's label so the text matches the list.
        var label = null;
        icon.find('[mod-role=enumeration-option]').each(function () {
            if (this.getAttribute('mod-parameter-value') == value)
                label = (this.textContent || '').replace(/^\s+|\s+$/g, '');
        });
        if (!label) {
            // Fall back to the file's basename parsed from the stored path.
            var s = '' + value;
            s = s.substring(s.lastIndexOf('/') + 1);
            s = s.substring(s.lastIndexOf('\\') + 1);
            label = s;
        }
        box.text(label);
    }

    function set_namfile(icon, value) {
        var box = icon.find('[rata-role=Nam]');
        if (value == null || value == 'None' || value == '') { box.text('-- choose a NAM file --'); return; }
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
        if (event.uri == 'https://rpowell5064.github.io/guitaramp-suite/cab#irfile')
            set_irfile(event.icon, event.value);
        else if (event.uri == 'https://rpowell5064.github.io/guitaramp-suite/cab#namfile')
            set_namfile(event.icon, event.value);
    }
}
