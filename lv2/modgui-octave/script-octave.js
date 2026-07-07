function (event) {
    // Reveal the polka-dot shimmer overlay whenever the Microtonal voice is engaged
    // (micro > 0). Purely cosmetic: toggles .hx-micro-on on the pedal root so the CSS
    // fades the dots in/out. The DSP shimmer itself is driven by the port, not this.
    var icon = event.icon;
    function apply(v) { icon.toggleClass('hx-micro-on', parseFloat(v) > 0.0001); }
    if (event.type == 'start') {
        var m = 0;
        (event.ports || []).forEach(function (p) { if (p.symbol == 'micro') m = p.value; });
        apply(m);
    } else if (event.type == 'change' && event.symbol == 'micro') {
        apply(event.value);
    }
}
