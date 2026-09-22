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
    // MIC 1 / MIC 2 tabs (2026-09-22): the pad shows whichever mic is on top
    function micKeys(icon) {
        return icon.data('cab_mictab') === 2
            ? { pos: 'cab_m2pos', dist: 'cab_m2dist', side: 'cab_m2side', posSym: 'mic2_pos', distSym: 'mic2_dist' }
            : { pos: 'cab_micpos', dist: 'cab_micdist', side: 'cab_micside', posSym: 'mic_pos', distSym: 'mic_dist' };
    }
    function micPadUpdate(icon) {
        var pad = icon.find('[rata-role=micpad]'); if (!pad.length) return;
        var K = micKeys(icon);
        pad.attr('data-mic', icon.data('cab_mictab') === 2 ? '2' : '1');
        var pos  = parseFloat(icon.data(K.pos))  || 0;
        var dist = parseFloat(icon.data(K.dist)) || 0;
        var side = icon.data(K.side) === -1 ? -1 : 1;
        var x = 28 + dist * 94, y = 75 - side * pos * 50;   // viewBox 140x150: centre 75, travel ±50
        pad.find('[rata-role=micdot]').attr('transform', 'translate(' + x.toFixed(1) + ',' + y.toFixed(1) + ')');
        var pn = pos < 0.12 ? 'CAP EDGE' : pos < 0.5 ? 'CONE' : pos < 0.85 ? 'CONE EDGE' : 'SURROUND';
        var dn = dist < 0.06 ? 'CLOSE' : Math.round(2 + dist * 28) + ' CM';
        pad.find('[rata-role=micposv]').text(pn);
        pad.find('[rata-role=micdistv]').text(dn);
    }

    // Dropdown labels (2026-07-23): mod-ui doesn't reliably render the selected
    // scale-point label into custom-select widgets — sync them ourselves from a
    // per-symbol cache (built at start), on option clicks and change events.
    function syncSel(icon, sym, val) {
        var m = icon.data('hx_selmap'); var els = m && m[sym]; if (!els) return;
        els.forEach(function (el) {
            var sel = el.querySelector('.mod-enumerated-selected'); if (!sel) return;
            var lab = null;
            Array.prototype.forEach.call(el.querySelectorAll('[mod-role=enumeration-option]'), function (o) {
                if (parseFloat(o.getAttribute('mod-port-value')) == parseFloat(val)) lab = (o.textContent || '').replace(/^\s+|\s+$/g, '');
            });
            if (lab != null) sel.textContent = lab;
        });
    }
    function buildSelMap(icon, ports) {
        var m = {};
        icon.find('[mod-widget=custom-select][mod-port-symbol]').each(function () {
            var sym = this.getAttribute('mod-port-symbol');
            (m[sym] = m[sym] || []).push(this);
            var el = this;
            Array.prototype.forEach.call(el.querySelectorAll('[mod-role=enumeration-option]'), function (o) {
                o.addEventListener('click', function () { syncSel(icon, sym, o.getAttribute('mod-port-value')); });
            });
        });
        icon.data('hx_selmap', m);
        (ports || []).forEach(function (p) { if (m[p.symbol]) syncSel(icon, p.symbol, p.value); });
    }

    // ── RIGS (2026-09-22, mirrors Hex Forge): named cab-stage setups. Each rig is the
    // cab IR plus a value for every control; applying one writes the ports through
    // set_port_value (not echoed as change events, so labels/pad are synced by hand)
    // and sets the IR with the host's patch_set on the #irfile path parameter. The
    // list shows each rig under the SAME cab name the IR picker uses. The label drops
    // to "Custom" on any hand edit.
    // Values: [lowcut, highcut, mix, micpos, micdist, roomon, roommix, roomamt, roomdense,
    //          voice, spkdrive, mic2type, mic2pos, mic2dist, mic2lvl, mic2align, mic2pol]
    var RIG_SYMS = ['low_cut_hz', 'high_cut_hz', 'mix', 'mic_pos', 'mic_dist', 'room_on', 'room_mix',
                    'room_amt', 'room_density', 'voice', 'spk_drive', 'mic2_type', 'mic2_pos',
                    'mic2_dist', 'mic2_lvl', 'mic2_align', 'mic2_pol'];
    var RIG_IR_URI = 'https://rpowell5064.github.io/guitaramp-suite/cab#irfile';
    var RIGS = [
        ['Tight 57',       'single 57 on the cap, dry',          '@factory',    80, 16000, 1, 0.05, 0.05, 0, 0.12, 0.35, 0, 0, 3, 0, 0,   0,    0.35, 0, 0],
        ['57 + Ribbon',    'the classic pair, honest offset',     '@factory',    80, 16000, 1, 0.15, 0.05, 0, 0.12, 0.35, 0, 0, 3, 3, 0.3, 0.15, 0.40, 0, 0],
        ['Studio Pair',    'aligned 57 + ribbon, console chain',  '@factory',    80, 16000, 1, 0.10, 0.05, 0, 0.12, 0.35, 1, 3, 3, 0.2, 0.10, 0.35, 1, 0],
        ['Live Room Pair', '57 + far ribbon, live room',          '@factory',    80, 16000, 1, 0.20, 0.15, 1, 0.30, 0.60, 2, 0, 3, 3, 0.2, 0.50, 0.40, 0, 0],
        ['Chime Pair',     '57 + far condenser, small room',      '@vox2x12',    80, 16000, 1, 0.30, 0.20, 1, 0.15, 0.30, 2, 0, 3, 4, 0,   0.60, 0.35, 0, 0],
        ['Open-Back Air',  'backed off, roomy',                   '@american-ob',80, 16000, 1, 0.30, 0.35, 1, 0.20, 0.45, 2, 0, 3, 0, 0,   0,    0.35, 0, 0],
        ['Room',           'off-cap, small space',                '@greenback',  80, 16000, 1, 0.25, 0.20, 1, 0.18, 0.35, 2, 0, 3, 0, 0,   0,    0.35, 0, 0],
        ['Wall',           '57 + 421 aligned, dry',               '@hiwatt',     80, 16000, 1, 0.20, 0.10, 0, 0.12, 0.35, 0, 0, 3, 2, 0.2, 0.10, 0.35, 1, 0],
        ['Cave',           'off-axis, large room',                '@doom',       80, 16000, 1, 0.40, 0.30, 1, 0.25, 0.80, 2, 0, 3, 0, 0,   0,    0.35, 0, 0],
        ['Close',          '57 tight, dry',                       '@bass810',    40, 16000, 1, 0.10, 0.05, 0, 0.12, 0.35, 0, 0, 3, 0, 0,   0,    0.35, 0, 0],
        ['Room',           'backed off, room',                    '@bass115',    40, 16000, 1, 0.30, 0.20, 1, 0.15, 0.40, 2, 0, 3, 0, 0,   0,    0.35, 0, 0]
    ];
    function rigCabName(icon, ir) {
        var t = null;
        icon.find('[mod-role=input-parameter] [mod-role=enumeration-option]').each(function () {
            if (t == null && this.getAttribute('mod-parameter-value') == ir) t = (this.textContent || '').replace(/^\s+|\s+$/g, '');
        });
        return (t || ir).replace(/ \(.*\)$/, '');
    }
    function rigDisplayName(icon, r) { return rigCabName(icon, r[2]) + ' \u00b7 ' + r[0]; }
    function rigLabel(icon, name) { icon.find('[rata-role=rigname]').text(name); }
    function rigApply(icon, idx) {
        var r = RIGS[idx]; if (!r || !funcs || typeof funcs.set_port_value !== 'function') return;
        icon.data('cab_rig_busy', true);
        if (typeof funcs.patch_set === 'function') funcs.patch_set(RIG_IR_URI, 'p', r[2]);
        set_irfile(icon, r[2]);
        for (var i = 0; i < RIG_SYMS.length; ++i) { funcs.set_port_value(RIG_SYMS[i], r[3 + i]); syncSel(icon, RIG_SYMS[i], r[3 + i]); }
        icon.data('cab_micpos', r[6]); icon.data('cab_micdist', r[7]);
        icon.data('cab_m2pos', r[15]); icon.data('cab_m2dist', r[16]);
        micPadUpdate(icon);
        rigLabel(icon, rigDisplayName(icon, r));
        icon.find('[rata-role=riglist] > div').removeClass('hf-rig-on').eq(idx).addClass('hf-rig-on');
        setTimeout(function () { icon.data('cab_rig_busy', false); }, 250);
    }
    function rigBuild(icon) {
        var box = icon.find('[rata-role=rig]'); if (!box.length) return;
        var list = box.find('[rata-role=riglist]'); list.empty();
        RIGS.forEach(function (r, i) {
            $('<div/>').text(rigDisplayName(icon, r)).append($('<span/>').text(r[1])).appendTo(list)
                .on('click', function (e) { e.preventDefault(); e.stopPropagation(); box.removeClass('open'); rigApply(icon, i); });
        });
        box.find('[rata-role=rigname]').on('click', function (e) {
            e.preventDefault(); e.stopPropagation(); box.toggleClass('open');
        });
        $(document).on('click.hxcabrig', function () { box.removeClass('open'); });
    }
    if (event.type == 'start') {
        var icon = event.icon;
        buildSelMap(icon, event.ports);
        rigBuild(icon);
        // Show the loaded IR immediately (2026-07-23): mod-ui applies the patch write
        // when an option is clicked but does NOT reliably echo it back as a change
        // event, so the label sat on the old value. Update it ourselves on click,
        // and seed from the current parameter value at load.
        (event.parameters || []).forEach(function (pr) {
            if (pr.uri && pr.uri.indexOf('#irfile') >= 0) set_irfile(icon, pr.value);
        });
        icon.find('[mod-role=input-parameter] [mod-role=enumeration-option]').each(function () {
            var el = this;
            el.addEventListener('click', function () {
                set_irfile(icon, el.getAttribute('mod-parameter-value'));
            });
        });
        (event.ports || []).forEach(function (p) {
            if (p.symbol === 'mic_pos')   icon.data('cab_micpos',  parseFloat(p.value));
            if (p.symbol === 'mic_dist')  icon.data('cab_micdist', parseFloat(p.value));
            if (p.symbol === 'mic2_pos')  icon.data('cab_m2pos',   parseFloat(p.value));
            if (p.symbol === 'mic2_dist') icon.data('cab_m2dist',  parseFloat(p.value));
        });
        icon.find('[rata-role=mictabs] .hf-mp-tab').each(function () {
            var tab = this;
            tab.addEventListener('click', function (e) {
                e.preventDefault(); e.stopPropagation();
                icon.data('cab_mictab', tab.getAttribute('data-mic') === '2' ? 2 : 1);
                icon.find('[rata-role=mictabs] .hf-mp-tab').removeClass('on'); tab.classList.add('on');
                micPadUpdate(icon);
            });
        });
        var svg = icon.find('[rata-role=micsvg]')[0];
        if (svg) {
            var write = function (pos, dist) {
                var K = micKeys(icon);
                icon.data(K.pos, pos); icon.data(K.dist, dist);
                if (funcs && typeof funcs.set_port_value === 'function') {
                    funcs.set_port_value(K.posSym,  pos);
                    funcs.set_port_value(K.distSym, dist);
                }
                micPadUpdate(icon);
            };
            var apply = function (e) {
                var r = svg.getBoundingClientRect();
                var vx = (e.clientX - r.left) / r.width  * 140;
                var vy = (e.clientY - r.top)  / r.height * 150;
                var off  = 75 - vy;
                var dist = Math.max(0, Math.min(1, (vx - 28) / 94));
                var pos  = Math.max(0, Math.min(1, Math.abs(off) / 50));
                icon.data(micKeys(icon).side, off < 0 ? -1 : 1);
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
                icon.data(micKeys(icon).side, 1); write(0, 0);
                e.preventDefault(); e.stopPropagation();
            });
        }
        micPadUpdate(icon);
    } else if (event.type == 'change') {
        if (event.symbol) syncSel(event.icon, event.symbol, event.value);
        if ((event.symbol || event.uri) && !event.icon.data('cab_rig_busy')) rigLabel(event.icon, 'Custom');   // any hand edit = a custom rig
        if (event.uri == 'https://rpowell5064.github.io/guitaramp-suite/cab#irfile')
            set_irfile(event.icon, event.value);
        else if (event.symbol === 'mic_pos')  { event.icon.data('cab_micpos',  parseFloat(event.value)); micPadUpdate(event.icon); }
        else if (event.symbol === 'mic_dist') { event.icon.data('cab_micdist', parseFloat(event.value)); micPadUpdate(event.icon); }
        else if (event.symbol === 'mic2_pos')  { event.icon.data('cab_m2pos',  parseFloat(event.value)); micPadUpdate(event.icon); }
        else if (event.symbol === 'mic2_dist') { event.icon.data('cab_m2dist', parseFloat(event.value)); micPadUpdate(event.icon); }
    }
}
