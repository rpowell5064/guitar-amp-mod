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

    // ── Mic pad (2026-07-14): drag the mic across the cone (Pos) / away from the grille (Dist) ──
    function micPadUpdate(icon) {
        var pad = icon.find('[rata-role=micpad]'); if (!pad.length) return;
        var pos  = parseFloat(icon.data('cab_micpos'))  || 0;
        var dist = parseFloat(icon.data('cab_micdist')) || 0;
        var side = icon.data('cab_micside') === -1 ? -1 : 1;
        var x = 28 + dist * 94, y = 42 - side * pos * 28;
        pad.find('[rata-role=micdot]').attr('transform', 'translate(' + x.toFixed(1) + ',' + y.toFixed(1) + ')');
        var pn = pos < 0.12 ? 'CAP EDGE' : pos < 0.5 ? 'CONE' : pos < 0.85 ? 'CONE EDGE' : 'SURROUND';
        var dn = dist < 0.06 ? 'CLOSE' : Math.round(2 + dist * 28) + ' CM';
        pad.find('[rata-role=micposv]').text(pn);
        pad.find('[rata-role=micdistv]').text(dn);
    }

    if (event.type == 'start') {
        var icon = event.icon;
        (event.ports || []).forEach(function (p) {
            if (p.symbol === 'mic_pos')  icon.data('cab_micpos',  parseFloat(p.value));
            if (p.symbol === 'mic_dist') icon.data('cab_micdist', parseFloat(p.value));
        });
        var svg = icon.find('[rata-role=micsvg]')[0];
        if (svg) {
            var write = function (pos, dist) {
                icon.data('cab_micpos', pos); icon.data('cab_micdist', dist);
                if (funcs && typeof funcs.set_port_value === 'function') {
                    funcs.set_port_value('mic_pos',  pos);
                    funcs.set_port_value('mic_dist', dist);
                }
                micPadUpdate(icon);
            };
            var apply = function (e) {
                var r = svg.getBoundingClientRect();
                var vx = (e.clientX - r.left) / r.width  * 140;
                var vy = (e.clientY - r.top)  / r.height * 84;
                var off  = 42 - vy;
                var dist = Math.max(0, Math.min(1, (vx - 28) / 94));
                var pos  = Math.max(0, Math.min(1, Math.abs(off) / 28));
                icon.data('cab_micside', off < 0 ? -1 : 1);
                if (pos < 0.05) pos = 0;
                write(pos, dist);
            };
            var drag = false;
            svg.addEventListener('pointerdown', function (e) {
                drag = true; svg.classList.add('hf-mp-live');
                if (svg.setPointerCapture) try { svg.setPointerCapture(e.pointerId); } catch (x) {}
                apply(e); e.preventDefault(); e.stopPropagation();
            });
            svg.addEventListener('pointermove',   function (e) { if (drag) { apply(e); e.preventDefault(); } });
            svg.addEventListener('pointerup',     function ()  { drag = false; svg.classList.remove('hf-mp-live'); });
            svg.addEventListener('pointercancel', function ()  { drag = false; svg.classList.remove('hf-mp-live'); });
            svg.addEventListener('dblclick', function (e) {
                icon.data('cab_micside', 1); write(0, 0);
                e.preventDefault(); e.stopPropagation();
            });
        }
        micPadUpdate(icon);
    } else if (event.type == 'change') {
        if (event.uri == 'https://rpowell5064.github.io/guitaramp-suite/cab#irfile')
            set_irfile(event.icon, event.value);
        else if (event.symbol === 'mic_pos')  { event.icon.data('cab_micpos',  parseFloat(event.value)); micPadUpdate(event.icon); }
        else if (event.symbol === 'mic_dist') { event.icon.data('cab_micdist', parseFloat(event.value)); micPadUpdate(event.icon); }
    }
}
