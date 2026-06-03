function (event, funcs) {
    // Wow + Flutter are tape mechanisms: shown for Tape (1) and Echorec (2),
    // hidden for Digital (0).
    function update_type(icon, value) {
        var t = parseInt(value, 10);
        icon.find('[rata-role=tapegroup]').toggleClass('mod-hidden', t === 0);
    }

    if (event.type == 'start') {
    } else if (event.type == 'change') {
        if (event.symbol == 'delay_type')
            update_type(event.icon, event.value);
    }
}
