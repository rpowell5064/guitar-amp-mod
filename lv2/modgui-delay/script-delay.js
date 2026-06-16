function (event, funcs) {
    // Wow + Flutter are tape mechanisms: shown for Tape (1) and Echorec (2),
    // hidden for Digital (0).
    function update_type(icon, value) {
        var t = parseInt(value, 10);
        // Wow/Flutter: Tape (1) + Echorec (2).  Echo Heads: Echorec (2) only.
        // Pattern/Ducking/Modulation: Seraph (3) only.
        icon.find('[rata-role=tapegroup]').toggleClass('mod-hidden', t !== 1 && t !== 2);
        icon.find('[rata-role=echogroup]').toggleClass('mod-hidden', t !== 2);
        icon.find('[rata-role=seraphgroup]').toggleClass('mod-hidden', t !== 3);
    }

    if (event.type == 'start') {
    } else if (event.type == 'change') {
        if (event.symbol == 'delay_type')
            update_type(event.icon, event.value);
    }
}
