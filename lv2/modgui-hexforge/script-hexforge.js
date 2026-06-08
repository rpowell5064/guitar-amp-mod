function (event, funcs) {
    // Hex Forge tile rack: live reordering + per-block on/off + "More" panels +
    // cab IR name. Each movable block owns a `<pfx>_pos` port (slot 1..9). Changing
    // a tile's slot selector MOVES that block to the chosen slot and renumbers the
    // rest (a clean permutation), writing every affected port via set_port_value,
    // then re-flows the DOM immediately. Drag-and-drop does the same on drop. The
    // DSP independently sorts blocks by pos every cycle, so audio follows the ports.
    var BLOCKS = ['gt','cp','fz','dr','amp','cab','md','dl','rv'];

    function tileOf(icon, b) { return icon.find('.hf-tile[data-block="' + b + '"]'); }
    function posOf(icon, b)  { var p = parseInt(tileOf(icon, b).attr('data-pos'), 10); return isNaN(p) ? 99 : p; }

    function orderedBlocks(icon) {
        var a = BLOCKS.slice().map(function (b) { return { b: b, p: posOf(icon, b) }; });
        a.sort(function (x, y) { return (x.p - y.p) || (BLOCKS.indexOf(x.b) - BLOCKS.indexOf(y.b)); });
        return a.map(function (o) { return o.b; });
    }

    // Re-flow the DOM so tiles appear in slot order (Input Trim always first).
    function resort(icon) {
        var rack = icon.find('.hf-rack');
        rack.append(tileOf(icon, 'it'));
        orderedBlocks(icon).forEach(function (b) { rack.append(tileOf(icon, b)); });
    }

    // Place `moveB` at slot `want`, renumber every movable block 1..9, push all the
    // pos ports, and re-flow. data-pos is set BEFORE writing ports so the echoed
    // change events (value === current data-pos) are recognised as no-ops.
    function moveToSlot(icon, fns, moveB, want) {
        var ord = orderedBlocks(icon).filter(function (b) { return b !== moveB; });
        var idx = want - 1; if (idx < 0) idx = 0; if (idx > ord.length) idx = ord.length;
        ord.splice(idx, 0, moveB);
        ord.forEach(function (b, i) { tileOf(icon, b).attr('data-pos', i + 1); });
        if (fns && typeof fns.set_port_value === 'function')
            ord.forEach(function (b, i) { fns.set_port_value(b + '_pos', i + 1); });
        resort(icon);
    }

    function setupDrag(icon, fns) {
        var dragB = null;
        icon.find('.hf-tile').each(function () {
            var node = this, b = node.getAttribute('data-block');
            if (b === 'it') return;
            node.setAttribute('draggable', 'true');
            node.addEventListener('dragstart', function (e) {
                dragB = b; node.classList.add('hf-drag');
                if (e.dataTransfer) { e.dataTransfer.effectAllowed = 'move'; try { e.dataTransfer.setData('text/plain', b); } catch (x) {} }
                e.stopPropagation();
            });
            node.addEventListener('dragend', function (e) { node.classList.remove('hf-drag'); dragB = null; e.stopPropagation(); });
            node.addEventListener('dragover', function (e) { e.preventDefault(); if (e.dataTransfer) e.dataTransfer.dropEffect = 'move'; });
            node.addEventListener('drop', function (e) {
                e.preventDefault(); e.stopPropagation();
                var targetB = node.getAttribute('data-block');
                if (!dragB || dragB === targetB || targetB === 'it') return;
                moveToSlot(icon, fns, dragB, posOf(icon, targetB));
            });
        });
    }

    // ── Conditional control visibility (only show what the selection uses) ──
    function show(icon, b, sel, on) { tileOf(icon, b).find(sel).toggleClass('mod-hidden', !on); }

    function applyAmp(icon) {
        var m = icon.data('hf_amp_m'); if (m == null) m = 1;
        var a = icon.data('hf_amp_auto'); if (a == null) a = true;
        show(icon, 'amp', '.c-amp-sunn', m === 3);
        show(icon, 'amp', '.c-amp-chan', m === 2 || m === 4);
        show(icon, 'amp', '.c-amp-reso', m === 2);
        show(icon, 'amp', '.c-amp-pa',   m !== 3);
        show(icon, 'amp', '.c-amp-paman', m !== 3 && !a);
        tileOf(icon, 'amp').find('[rata-role=lbl-amp_gain]').text(m === 3 ? 'Normal Vol' : 'Gain');
    }
    function applyFuzz(icon) {
        var p = icon.data('hf_fz_p'); if (p == null) p = 0;
        var tb = (p === 1);
        show(icon, 'fz', '.c-fz-ih', !tb);
        show(icon, 'fz', '.c-fz-tb', tb);
        var t = tileOf(icon, 'fz');
        t.find('[rata-role=lbl-fz_sustain]').text(tb ? 'Attack' : 'Sustain');
        t.find('[rata-role=lbl-fz_volume]').text(tb ? 'Level' : 'Volume');
    }
    function applyDelay(icon) {
        var t = icon.data('hf_dl_t'); if (t == null) t = 0;
        show(icon, 'dl', '.c-dl-tape', t === 1 || t === 2);
        show(icon, 'dl', '.c-dl-heads', t === 2);
    }

    function setIr(icon, value) {
        var box = icon.find('[rata-role=Ir]');
        if (value == null || value === 'None' || value === '') { box.text('-- choose an IR file --'); return; }
        var label = null;
        icon.find('[mod-role=enumeration-option]').each(function () {
            if (this.getAttribute('mod-parameter-value') == value)
                label = (this.textContent || '').replace(/^\s+|\s+$/g, '');
        });
        if (!label) { var s = '' + value; s = s.substring(s.lastIndexOf('/') + 1); s = s.substring(s.lastIndexOf('\\') + 1); label = s; }
        box.text(label);
    }

    if (event.type == 'start') {
        var icon = event.icon;
        icon.find('.hf-morebtn').each(function () {
            this.addEventListener('click', function (e) {
                var t = this; while (t && !(t.className && (' ' + t.className + ' ').indexOf(' hf-tile ') >= 0)) t = t.parentNode;
                if (t) t.classList.toggle('hf-open');
                e.stopPropagation();
            });
        });
        setupDrag(icon, funcs);
        // Initialize conditional visibility + slot order from the START port values.
        // (Some mod-ui builds don't emit initial 'change' events, so relying on
        // those alone leaves every conditional control visible at load — which is
        // why the Sunn/power-amp controls showed up under a non-Sunn amp.)
        var map = {};
        (event.ports || []).forEach(function (p) { map[p.symbol] = p.value; });
        if ('amp_model' in map)     icon.data('hf_amp_m', parseInt(map.amp_model, 10));
        if ('amp_pamp_auto' in map) icon.data('hf_amp_auto', map.amp_pamp_auto > 0.5);
        if ('fz_pedal' in map)      icon.data('hf_fz_p', parseInt(map.fz_pedal, 10));
        if ('dl_type' in map)       icon.data('hf_dl_t', parseInt(map.dl_type, 10));
        applyAmp(icon); applyFuzz(icon); applyDelay(icon);
        show(icon, 'dr', '.c-dr-oct', parseInt(map.dr_model || 0, 10) === 1);
        BLOCKS.forEach(function (b) { if ((b + '_pos') in map) tileOf(icon, b).attr('data-pos', parseInt(map[b + '_pos'], 10)); });
        resort(icon);
    } else if (event.type == 'change') {
        var icon = event.icon, s = event.symbol;
        if (s && /_pos$/.test(s)) {
            var b = s.replace(/_pos$/, ''), want = parseInt(event.value, 10), cur = posOf(icon, b);
            if (want === cur) { resort(icon); return; }   // echo of our own write / already there
            moveToSlot(icon, funcs, b, want);
        } else if (s && /_enable$/.test(s)) {
            tileOf(icon, s.replace(/_enable$/, '')).toggleClass('hf-off', !(event.value > 0.5));
        } else if (s === 'amp_model') {
            icon.data('hf_amp_m', parseInt(event.value, 10)); applyAmp(icon);
        } else if (s === 'amp_pamp_auto') {
            icon.data('hf_amp_auto', event.value > 0.5); applyAmp(icon);
        } else if (s === 'fz_pedal') {
            icon.data('hf_fz_p', parseInt(event.value, 10)); applyFuzz(icon);
        } else if (s === 'dr_model') {
            show(icon, 'dr', '.c-dr-oct', parseInt(event.value, 10) === 1);
        } else if (s === 'dl_type') {
            icon.data('hf_dl_t', parseInt(event.value, 10)); applyDelay(icon);
        } else if (event.uri && event.uri.indexOf('#irfile') >= 0) {
            setIr(icon, event.value);
        }
    }
}
