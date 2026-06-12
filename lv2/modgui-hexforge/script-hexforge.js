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
        show(icon, 'amp', '.c-amp-pa',   m !== 3 && m !== 5);
        show(icon, 'amp', '.c-amp-paman', m !== 3 && m !== 5 && !a);
        show(icon, 'amp', '.c-amp-nam',  m === 5);   // NAM file picker
        tileOf(icon, 'amp').find('[rata-role=lbl-amp_gain]').text(m === 3 ? 'Normal Vol' : (m === 5 ? 'Output' : 'Gain'));
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

    function setFile(icon, rata, value, empty) {
        var box = icon.find('[rata-role=' + rata + ']');
        if (value == null || value === 'None' || value === '') { box.text(empty); return; }
        var label = null;
        icon.find('[mod-role=enumeration-option]').each(function () {
            if (this.getAttribute('mod-parameter-value') == value)
                label = (this.textContent || '').replace(/^\s+|\s+$/g, '');
        });
        if (!label) { var s = '' + value; s = s.substring(s.lastIndexOf('/') + 1); s = s.substring(s.lastIndexOf('\\') + 1); label = s; }
        box.text(label);
    }
    // Empty / sentinel IR path == the always-available built-in Factory Cab.
    function setIr(icon, value) {
        if (value == null || value === 'None' || value === '' || value === '@factory')
            value = '@factory';
        setFile(icon, 'Ir', value, 'Factory Cab (built-in)');
    }

    // ── Presets: pulse command ports, render bank/slot/name + the 32-slot list ──
    var SW = ['sw_a', 'sw_b', 'sw_c', 'sw_d'];
    var PS_NAME_URI = 'https://rpowell5064.github.io/guitaramp-suite/hexforge#ps_name';
    function psPulse(fns, sym) {
        if (!fns || typeof fns.set_port_value !== 'function') return;
        fns.set_port_value(sym, 1);
        setTimeout(function () { fns.set_port_value(sym, 0); }, 40);   // clean rising edge
    }
    function psGoto(fns, flat) {
        if (!fns || typeof fns.set_port_value !== 'function') return;
        fns.set_port_value('ps_goto', flat);
        setTimeout(function () { fns.set_port_value('ps_goto', -1); }, 60);   // back to idle so a re-pick re-fires
    }
    function psBankLabel(icon) {
        var b = icon.data('ps_bank'); if (b == null) b = 0;
        var s = icon.data('ps_slot'); if (s == null) s = 0;
        icon.find('[rata-role=psbank]').text('BANK ' + (b + 1));
        icon.find('.hf-ps-slot').each(function () {
            this.classList.toggle('hf-ps-on', parseInt(this.getAttribute('data-slot'), 10) === s);
        });
    }
    function psRenderList(icon, fns) {
        var box = icon.find('[rata-role=pslist]'); if (!box.length) return;
        var names = icon.data('ps_names') || [];
        var ab = icon.data('ps_bank') || 0, as = icon.data('ps_slot') || 0;
        var html = '';
        for (var b = 0; b < 8; b++) {
            html += '<div class="hf-ps-bankrow"><span class="hf-ps-banknum">B' + (b + 1) + '</span>';
            for (var s = 0; s < 4; s++) {
                var flat = b * 4 + s, nm = names[flat] || '';
                var active = (b === ab && s === as) ? ' hf-ps-active' : '';
                var empty = nm ? '' : ' hf-ps-empty';
                html += '<button type="button" class="hf-ps-item' + active + empty + '" data-flat="' + flat + '">'
                      + '<b>' + 'ABCD'.charAt(s) + '</b> ' + (nm || '—') + '</button>';
            }
            html += '</div>';
        }
        box[0].innerHTML = html;
        box.find('.hf-ps-item').each(function () {
            var el = this;
            el.addEventListener('click', function (e) {
                e.stopPropagation();
                psGoto(fns, parseInt(el.getAttribute('data-flat'), 10));
                box.removeClass('hf-ps-open');
            });
        });
    }
    // Set the name input without clobbering what the user is currently typing.
    function psSetName(icon, nm) {
        var el = icon.find('[rata-role=psname]')[0];
        if (el && document.activeElement !== el) el.value = (nm == null ? '' : '' + nm);
    }
    // Replay the recalled snapshot ("sym=val;..") onto the host ports so the knobs,
    // tiles and conditional controls follow the preset. The block-order (_pos)
    // ports need care: writing them one-by-one through the normal change handler
    // would call moveToSlot for each and renumber the whole rack, scrambling the
    // permutation. So for _pos we set the tile's data-pos FIRST (which makes the
    // echoed change a recognised no-op), push the value, then resort once at the end.
    function psApply(fns, str, icon) {
        if (!fns || typeof fns.set_port_value !== 'function' || !str) return;
        var sawPos = false;
        ('' + str).split(';').forEach(function (kv) {
            var i = kv.indexOf('='); if (i < 0) return;
            var sym = kv.substring(0, i), val = parseFloat(kv.substring(i + 1));
            if (!sym || isNaN(val)) return;
            if (/_pos$/.test(sym)) {
                tileOf(icon, sym.replace(/_pos$/, '')).attr('data-pos', val);
                sawPos = true;
            }
            fns.set_port_value(sym, val);
        });
        if (sawPos) resort(icon);
    }

    if (event.type == 'start') {
        var icon = event.icon;
        icon.find('.hf-morebtn').each(function () {
            this.addEventListener('click', function (e) {
                var t = this; while (t && !(t.className && (' ' + t.className + ' ').indexOf(' hf-tile ') >= 0)) t = t.parentNode;
                if (t) {
                    t.classList.toggle('hf-open');
                    // Open the popup upward when the tile sits in the lower rows so it
                    // doesn't hang off the bottom of the Hex Forge frame.
                    var rack = t.parentNode, more = t.querySelector('.hf-more');
                    if (more) {
                        var up = rack && (t.offsetTop > rack.clientHeight * 0.4);
                        if (up) more.classList.add('hf-more-up'); else more.classList.remove('hf-more-up');
                    }
                }
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
        var drm = parseInt(map.dr_model || 0, 10);
        show(icon, 'dr', '.c-dr-oct', drm === 1);
        show(icon, 'dr', '.c-dr-nam', drm === 3);
        ['it'].concat(BLOCKS).forEach(function (b) {
            if ((b + '_enable') in map) tileOf(icon, b).toggleClass('hf-off', !(map[b + '_enable'] > 0.5));
            if ((b + '_pos') in map)    tileOf(icon, b).attr('data-pos', parseInt(map[b + '_pos'], 10));
        });
        resort(icon);

        // Preset strip: wire the buttons to pulse the command ports.
        function wire(sel, fn) { icon.find(sel).each(function () { var el = this;
            el.addEventListener('click', function (e) { e.stopPropagation(); fn(); }); }); }
        wire('.hf-ps-bankdn', function () { psPulse(funcs, 'ps_bank_dn'); });
        wire('.hf-ps-bankup', function () { psPulse(funcs, 'ps_bank_up'); });
        wire('.hf-ps-save',   function () { psPulse(funcs, 'ps_save'); });
        wire('.hf-ps-mvup',   function () { psPulse(funcs, 'ps_move_up'); });
        wire('.hf-ps-mvdn',   function () { psPulse(funcs, 'ps_move_dn'); });
        wire('.hf-ps-backup', function () { psPulse(funcs, 'ps_backup'); });
        wire('.hf-ps-restore',function () { psPulse(funcs, 'ps_restore'); });
        wire('.hf-ps-toggle', function () { icon.find('[rata-role=pslist]').toggleClass('hf-ps-open'); });
        icon.find('.hf-ps-slot').each(function () { var el = this;
            el.addEventListener('click', function (e) { e.stopPropagation();
                psPulse(funcs, SW[parseInt(el.getAttribute('data-slot'), 10)]); }); });
        // Name input → send the rename as an atom:String patch (valuetype 's').
        icon.find('.hf-ps-name').each(function () {
            var el = this;
            el.addEventListener('mousedown', function (e) { e.stopPropagation(); });   // focus, don't drag the pedal
            var commit = function () {
                if (funcs && typeof funcs.patch_set === 'function')
                    funcs.patch_set(PS_NAME_URI, 's', el.value.replace(/\|/g, ' '));
                el.blur();
            };
            el.addEventListener('change', commit);
            el.addEventListener('keydown', function (e) { if (e.keyCode === 13) { e.preventDefault(); commit(); } });
        });
        if ('ps_bank' in map) icon.data('ps_bank', parseInt(map.ps_bank, 10));
        if ('ps_slot' in map) icon.data('ps_slot', parseInt(map.ps_slot, 10));
        psBankLabel(icon); psRenderList(icon, funcs);
        // Re-sync each block's dimmed/active look from the actual on-off switch once
        // mod-ui has applied the initial port values (some builds don't pass them in
        // event.ports), so bypassed blocks always show dimmed and active ones bright.
        setTimeout(function () {
            ['it'].concat(BLOCKS).forEach(function (b) {
                var img = tileOf(icon, b).find('.hf-on-img');
                if (img.length) tileOf(icon, b).toggleClass('hf-off', img.hasClass('off'));
            });
        }, 350);
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
            var dm = parseInt(event.value, 10);
            show(icon, 'dr', '.c-dr-oct', dm === 1);
            show(icon, 'dr', '.c-dr-nam', dm === 3);
        } else if (s === 'dl_type') {
            icon.data('hf_dl_t', parseInt(event.value, 10)); applyDelay(icon);
        } else if (s === 'clip') {
            icon.find('.hf-clip').toggleClass('hf-clip-on', event.value > 0.5);
        } else if (s === 'ps_bank') {
            icon.data('ps_bank', parseInt(event.value, 10)); psBankLabel(icon); psRenderList(icon, funcs);
        } else if (s === 'ps_slot') {
            icon.data('ps_slot', parseInt(event.value, 10)); psBankLabel(icon); psRenderList(icon, funcs);
        } else if (event.uri && event.uri.indexOf('#irfile') >= 0) {
            setIr(icon, event.value);
        } else if (event.uri && event.uri.indexOf('#ampnam') >= 0) {
            setFile(icon, 'AmpNam', event.value, '-- choose a NAM file --');
        } else if (event.uri && event.uri.indexOf('#drnam') >= 0) {
            setFile(icon, 'DrNam', event.value, '-- choose a NAM file --');
        } else if (event.uri && event.uri.indexOf('#cabnam') >= 0) {
            setFile(icon, 'CabNam', event.value, '-- choose a NAM file --');
        } else if (event.uri && event.uri.indexOf('#ps_index') >= 0) {
            var parts = ('' + event.value).split('|');
            if (parts.length >= 2) {
                var pb = parseInt(parts[0], 10), ps = parseInt(parts[1], 10);
                icon.data('ps_bank', pb); icon.data('ps_slot', ps); icon.data('ps_names', parts.slice(2));
                psBankLabel(icon); psRenderList(icon, funcs);
                psSetName(icon, parts[2 + pb * 4 + ps]);
            }
        } else if (event.uri && event.uri.indexOf('#ps_name') >= 0) {
            psSetName(icon, event.value);
        } else if (event.uri && event.uri.indexOf('#ps_apply') >= 0) {
            psApply(funcs, event.value, icon);
        }
    }
}
