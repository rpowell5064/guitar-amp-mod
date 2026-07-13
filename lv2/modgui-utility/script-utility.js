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

    if (event.type == 'start') {
        var icon = event.icon;
        icon.find('[rata-role=tab]').each(function () {
            var el = this;
            el.addEventListener('click', function (e) {
                e.stopPropagation();
                set_tab(icon, el.getAttribute('data-tab'));
            });
        });
        set_tab(icon, 'input');
    }
}
