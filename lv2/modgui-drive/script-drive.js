function (event, funcs) {
    // Octave control belongs to the Life Pedal (model index 1) only.
    //   0 TS-808, 1 Life Pedal, 2 ProCo RAT
    function update_model(icon, value) {
        var m = parseInt(value, 10);
        icon.find('[rata-role=octavectl]').toggleClass('mod-hidden', m !== 1);
    }

    if (event.type == 'start') {
    } else if (event.type == 'change') {
        if (event.symbol == 'model')
            update_model(event.icon, event.value);
    }
}
