function (event, funcs) {
    // Hex Forge node-chain UI: a horizontal signal strip of clickable nodes (one per
    // block). Click a node to edit it in the detail panel below; click its power dot
    // to bypass it (greyed, dry, settings kept); drag to reorder; "+ ADD" pulls a
    // removed effect back from the palette; REMOVE sends it there. Each movable block
    // owns <pfx>_pos (slot 1..11), <pfx>_enable (chain membership) and <pfx>_bypass
    // (active/bypassed). The DSP sorts by pos and runs a block iff enable && !bypass.
    // Input Trim is locked first; its dot toggles it_enable (it has no bypass port).
    var BLOCKS = ['gt','cp','fz','dr','amp','cab','md','dl','rv','wh','oc'];

    // Node subtitle labels for model-bearing blocks — MUST mirror gen_hexforge.py's
    // scalePoints (the source of truth). Scalar blocks bind their value via mod-role in
    // the HTML instead; these are the ones we can't (enumerated → would show a number).
    var NV = {
        amp: ['Clean Meanie','Crunchy McCrunchFace','Gainzilla','Doom Daddy','Tangerang','Neural','Beardo BE','Hi-Volt','Chime Thirty','Backline Plus'],
        dr:  ['Green Man','New Dawn','Dear Rodent Boy','Neural','Grunge DS','Gilded Horse','Super Nova'],
        fz:  ['Italian Hero','I Know It','Octavia'],
        md:  ['Lush-2','Uni-Verse','Phaser','Flanger','Tremolo','Rotary','Nevermind Chorus'],
        dl:  ['Digital','Tape','Echo Wreck','Seraph']
    };
    function setNodeVal(icon, pfx, txt) { icon.find('[rata-role=nv-' + pfx + ']').text(txt == null ? '' : txt); }
    function setModelVal(icon, pfx, idx) { var a = NV[pfx]; setNodeVal(icon, pfx, (a && a[idx]) || ''); }

    function nodeOf(icon, b)  { return icon.find('.hf-node[data-block="' + b + '"]'); }
    function panelOf(icon, b) { return icon.find('.hf-detail-panel[data-block="' + b + '"]'); }
    // ── Strobe tuner: note name + a disc that spins by cents (still + green = in tune) ──
    var NOTE_NAMES = ['C','C♯','D','D♯','E','F','F♯','G','G♯','A','A♯','B'];
    function tunerNote(icon, v) {
        var n = parseInt(v, 10), t = icon.find('[rata-role=tuner]');
        if (n < 0 || isNaN(n)) { icon.find('[rata-role=tunernote]').text('–'); t.removeClass('hf-tuner-lit hf-tuner-intune'); icon.find('[rata-role=tunercents]').text('no signal'); }
        else { icon.find('[rata-role=tunernote]').text(NOTE_NAMES[n]); t.addClass('hf-tuner-lit'); }
    }
    function tunerCents(icon, v) {
        var c = parseFloat(v); if (isNaN(c)) return;
        var t = icon.find('[rata-role=tuner]'), ring = icon.find('[rata-role=tunerdisc]')[0];
        var inTune = Math.abs(c) <= 3.5;
        t.toggleClass('hf-tuner-intune', inTune);
        icon.find('[rata-role=tunercents]').text((c > 0 ? '+' : '') + c.toFixed(0) + ' cents' + (inTune ? ' • in tune' : (c > 0 ? ' • sharp' : ' • flat')));
        if (!ring) return;
        if (inTune) { ring.style.animationPlayState = 'paused'; }
        else {
            var dur = Math.max(0.12, 3.0 / Math.min(50, Math.abs(c)));   // faster spin = further out
            ring.style.animationDuration = dur.toFixed(2) + 's';
            ring.style.animationDirection = c > 0 ? 'normal' : 'reverse';
            ring.style.animationPlayState = 'running';
        }
    }
    function posOf(icon, b)   { var p = parseInt(nodeOf(icon, b).attr('data-pos'), 10); return isNaN(p) ? 99 : p; }
    function inChain(icon, b) { return nodeOf(icon, b).parent().hasClass('hf-nodes'); }

    // movable blocks currently in the chain, sorted by slot; and the removed ones
    function chainOrder(icon) {
        var a = BLOCKS.filter(function (b) { return inChain(icon, b); })
                      .map(function (b) { return { b: b, p: posOf(icon, b) }; });
        a.sort(function (x, y) { return (x.p - y.p) || (BLOCKS.indexOf(x.b) - BLOCKS.indexOf(y.b)); });
        return a.map(function (o) { return o.b; });
    }
    function removedOrder(icon) {
        var a = BLOCKS.filter(function (b) { return !inChain(icon, b); })
                      .map(function (b) { return { b: b, p: posOf(icon, b) }; });
        a.sort(function (x, y) { return (x.p - y.p) || (BLOCKS.indexOf(x.b) - BLOCKS.indexOf(y.b)); });
        return a.map(function (o) { return o.b; });
    }
    // Assign pos 1..11 across all movable blocks (chain first, removed after), pushing
    // data-pos BEFORE the ports so the echoed change events read as no-ops.
    function writePos(icon, fns, order) {
        order.forEach(function (b, i) { nodeOf(icon, b).attr('data-pos', i + 1); });
        if (fns && typeof fns.set_port_value === 'function')
            order.forEach(function (b, i) { fns.set_port_value(b + '_pos', i + 1); });
    }
    // Re-flow the chain DOM by slot (Input Trim always first); removed nodes live in
    // the palette and are left there.
    function resort(icon) {
        var nodes = icon.find('.hf-nodes');
        nodes.append(nodeOf(icon, 'it'));
        chainOrder(icon).forEach(function (b) { nodes.append(nodeOf(icon, b)); });
    }
    // Move `moveB` to visual slot `want` among the chain blocks; renumber everything.
    function moveToSlot(icon, fns, moveB, want) {
        var chain = chainOrder(icon).filter(function (b) { return b !== moveB; });
        var idx = want - 1; if (idx < 0) idx = 0; if (idx > chain.length) idx = chain.length;
        chain.splice(idx, 0, moveB);
        writePos(icon, fns, chain.concat(removedOrder(icon)));
        resort(icon);
    }

    function selectNode(icon, b) {
        if (!b || !nodeOf(icon, b).length) b = inChain(icon, 'amp') ? 'amp' : 'it';
        icon.data('hf_sel', b);
        icon.find('.hf-node').removeClass('hf-sel');
        nodeOf(icon, b).addClass('hf-sel');
        icon.find('.hf-detail-panel').removeClass('hf-sel');
        panelOf(icon, b).addClass('hf-sel');
    }

    function renderPalette(icon) {
        var pal = icon.find('.hf-palette');
        pal.find('.hf-palette-empty').remove();
        if (!pal.find('.hf-node').length)
            pal.append('<div class="hf-palette-empty">All effects are already in the chain.</div>');
    }

    function addBlock(icon, fns, b) {
        if (b === 'it' || !fns || inChain(icon, b)) return;
        fns.set_port_value(b + '_enable', 1);
        nodeOf(icon, b).attr('data-pos', 99);                 // drop in at the end of the chain
        icon.find('.hf-nodes').append(nodeOf(icon, b));
        writePos(icon, fns, chainOrder(icon).concat(removedOrder(icon)));
        resort(icon); renderPalette(icon);
        icon.find('.hf-palette').removeClass('hf-open');
        selectNode(icon, b);
    }
    function removeBlock(icon, fns, b) {
        if (b === 'it' || !fns || !inChain(icon, b)) return;
        fns.set_port_value(b + '_enable', 0);
        icon.find('.hf-palette').append(nodeOf(icon, b));
        writePos(icon, fns, chainOrder(icon).concat(removedOrder(icon)));
        resort(icon); renderPalette(icon);
        if (icon.data('hf_sel') === b) selectNode(icon, 'it');
    }
    function setBypass(icon, fns, b, byp) {
        if (!fns) return;
        var sym = (b === 'it') ? 'it_enable' : (b + '_bypass');
        var portVal = (b === 'it') ? (byp ? 0 : 1) : (byp ? 1 : 0);   // IT: it_enable 1=active
        fns.set_port_value(sym, portVal);
        nodeOf(icon, b).toggleClass('hf-byp', byp);
    }

    function setupNodes(icon, fns) {
        var dragB = null;
        icon.find('.hf-node').each(function () {
            var nd = this, b = nd.getAttribute('data-block');
            // click node body = select; click the dot = bypass; a palette node = add
            nd.addEventListener('click', function (e) {
                var t = e.target;
                while (t && t !== nd) {
                    if (t.className && (' ' + t.className + ' ').indexOf(' hf-node-dot ') >= 0) {
                        setBypass(icon, fns, b, !nd.classList.contains('hf-byp'));
                        e.stopPropagation(); return;
                    }
                    t = t.parentNode;
                }
                if (nd.parentNode && nd.parentNode.className.indexOf('hf-palette') >= 0) addBlock(icon, fns, b);
                else selectNode(icon, b);
                e.stopPropagation();
            });
            if (b === 'it') return;
            nd.setAttribute('draggable', 'true');
            nd.addEventListener('dragstart', function (e) {
                if (nd.parentNode && nd.parentNode.className.indexOf('hf-palette') >= 0) { e.preventDefault(); return; }
                dragB = b; nd.classList.add('hf-drag');
                if (e.dataTransfer) { e.dataTransfer.effectAllowed = 'move'; try { e.dataTransfer.setData('text/plain', b); } catch (x) {} }
                e.stopPropagation();
            });
            nd.addEventListener('dragend',  function (e) { nd.classList.remove('hf-drag'); dragB = null; e.stopPropagation(); });
            nd.addEventListener('dragover', function (e) { e.preventDefault(); if (e.dataTransfer) e.dataTransfer.dropEffect = 'move'; });
            nd.addEventListener('drop', function (e) {
                e.preventDefault(); e.stopPropagation();
                var targetB = nd.getAttribute('data-block');
                if (!dragB || dragB === targetB || targetB === 'it' || !inChain(icon, dragB)) return;
                moveToSlot(icon, fns, dragB, posOf(icon, targetB));
            });
        });
        // REMOVE buttons in each detail panel
        icon.find('.hf-dremove').each(function () {
            var el = this;
            el.addEventListener('click', function (e) {
                e.stopPropagation();
                var pn = el; while (pn && !(pn.className && (' ' + pn.className + ' ').indexOf(' hf-detail-panel ') >= 0)) pn = pn.parentNode;
                if (pn) removeBlock(icon, fns, pn.getAttribute('data-block'));
            });
        });
    }

    // ── Amp faceplate: per-model skin class (hf-face-mN) + big parody badge ──
    function applyAmpFace(icon, m) {
        var face = panelOf(icon, 'amp').find('.hf-amp-face');
        if (!face.length) return;
        var el = face[0];
        el.className = el.className.replace(/\bhf-face-m\d+\b/g, '').replace(/\s+/g, ' ').replace(/\s+$/, '');
        el.className += ' hf-face-m' + m;
        face.find('[rata-role=amp-badge]').text((NV.amp && NV.amp[m]) || '');
    }
    // ── Conditional control visibility, scoped to the block's DETAIL PANEL ──
    function show(icon, b, sel, on) { panelOf(icon, b).find(sel).toggleClass('mod-hidden', !on); }
    function applyAmp(icon) {
        var m = icon.data('hf_amp_m'); if (m == null) m = 1;
        var a = icon.data('hf_amp_auto'); if (a == null) a = true;
        show(icon, 'amp', '.c-amp-sunn', m === 3);
        show(icon, 'amp', '.c-amp-chan', m === 2 || m === 4);
        show(icon, 'amp', '.c-amp-be',   m === 6);
        show(icon, 'amp', '.c-amp-reso', m === 2);
        show(icon, 'amp', '.c-amp-pa',   m !== 3 && m !== 5);
        show(icon, 'amp', '.c-amp-paman', m !== 3 && m !== 5 && !a);
        show(icon, 'amp', '.c-amp-nam',  m === 5);
        panelOf(icon, 'amp').find('[rata-role=lbl-amp_gain]').text(m === 3 ? 'Normal Vol' : (m === 5 ? 'Output' : 'Gain'));
        setModelVal(icon, 'amp', m);
        applyAmpFace(icon, m);
    }
    function applyFuzz(icon) {
        var p = icon.data('hf_fz_p'); if (p == null) p = 0;
        var tb = (p === 1);
        show(icon, 'fz', '.c-fz-ih', !tb);
        show(icon, 'fz', '.c-fz-tb', tb);
        var pn = panelOf(icon, 'fz');
        pn.find('[rata-role=lbl-fz_sustain]').text(tb ? 'Attack' : 'Sustain');
        pn.find('[rata-role=lbl-fz_volume]').text(tb ? 'Level' : 'Volume');
        setModelVal(icon, 'fz', p);
    }
    function applyDelay(icon) {
        var t = icon.data('hf_dl_t'); if (t == null) t = 0;
        show(icon, 'dl', '.c-dl-tape', t === 1 || t === 2);
        show(icon, 'dl', '.c-dl-heads', t === 2);
        show(icon, 'dl', '.c-dl-seraph', t === 3);
        setModelVal(icon, 'dl', t);
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
    var CAB_NAMES = { '@factory':'Factory Cab (V30 4x12)', '@vox2x12':'Chime 2x12 (Vox)',
                      '@american-ob':'American Open-Back 2x12', '@greenback':'Greenback 4x12',
                      '@hiwatt':'Hi-Volt 4x12 (Fane)', '@doom':'Doom 4x12' };
    function setIr(icon, value) {
        if (value == null || value === 'None' || value === '') value = '@factory';
        if (CAB_NAMES[value]) {                      // built-in synthetic cab
            setFile(icon, 'Ir', value, CAB_NAMES[value]);
            setNodeVal(icon, 'cab', CAB_NAMES[value].replace(/ \(.*\)$/, ''));
            return;
        }
        setFile(icon, 'Ir', value, null);            // user .wav → basename
        setNodeVal(icon, 'cab', icon.find('[rata-role=Ir]').first().text());
    }
    // Level meters: the plugin sends in_meter/out_meter as 0..1 (dB-scaled); set the bar width.
    // Hot path (~14 Hz) — cache the raw DOM node (no jQuery .find() per tick) and skip sub-1%
    // changes so steady-state levels cost zero repaints. This is what kept the browser CPU busy.
    function setMeter(icon, rata, v) {
        var key = 'hf_m_' + rata;
        var el = icon.data(key);
        if (!el || !el.isConnected) {
            var f = icon.find('[rata-role=' + rata + ']');
            if (!f.length) return;
            el = f[0]; icon.data(key, el);
        }
        var pct = (v < 0 ? 0 : (v > 1 ? 1 : v)) * 100;
        if (el._hfPct != null && Math.abs(el._hfPct - pct) < 1) return;
        el._hfPct = pct;
        el.style.width = pct.toFixed(1) + '%';
    }

    // ── Presets: pulse command ports, render bank/slot/name + the 32-slot list ──
    var SW = ['sw_a', 'sw_b', 'sw_c', 'sw_d'];
    var PS_NAME_URI = 'https://rpowell5064.github.io/guitaramp-suite/hexforge#ps_name';
    function psPulse(fns, sym) {
        if (!fns || typeof fns.set_port_value !== 'function') return;
        fns.set_port_value(sym, 1);
        setTimeout(function () { fns.set_port_value(sym, 0); }, 40);
    }
    function psGoto(fns, flat) {
        if (!fns || typeof fns.set_port_value !== 'function') return;
        fns.set_port_value('ps_goto', flat);
        setTimeout(function () { fns.set_port_value('ps_goto', -1); }, 60);
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
        // Show every bank that has a preset, PLUS one empty "new" bank at the bottom —
        // saving into it reveals the next, so the user grows banks on demand (up to 32).
        var maxB = 0;
        for (var i = 0; i < names.length; i++) if (names[i]) maxB = Math.floor(i / 4);
        var showBanks = Math.min(Math.max(maxB + 2, ab + 2), 32);
        var html = '';
        for (var b = 0; b < showBanks; b++) {
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
                icon.find('[rata-role=psmenu]').removeClass('hf-ps-open');
            });
        });
    }
    function psSetName(icon, nm) {
        var el = icon.find('[rata-role=psname]')[0];
        if (el && document.activeElement !== el) el.value = (nm == null ? '' : '' + nm);
    }
    // Replay the recalled snapshot ("sym=val;..") onto the host ports so the nodes,
    // detail controls, membership/bypass and chain order all follow the preset.
    function psApply(fns, str, icon) {
        if (!fns || typeof fns.set_port_value !== 'function' || !str) return;
        var sawPos = false, drm = null, membership = false;
        ('' + str).split(';').forEach(function (kv) {
            var i = kv.indexOf('='); if (i < 0) return;
            var sym = kv.substring(0, i), val = parseFloat(kv.substring(i + 1));
            if (!sym || isNaN(val)) return;
            if (/_pos$/.test(sym)) {
                nodeOf(icon, sym.replace(/_pos$/, '')).attr('data-pos', val); sawPos = true;
            } else if (/_bypass$/.test(sym)) {
                nodeOf(icon, sym.replace(/_bypass$/, '')).toggleClass('hf-byp', val > 0.5);
            } else if (/_enable$/.test(sym)) {
                var b = sym.replace(/_enable$/, '');
                if (b === 'it') nodeOf(icon, 'it').toggleClass('hf-byp', !(val > 0.5));
                else {
                    var want = val > 0.5;
                    if (want && !inChain(icon, b)) { nodeOf(icon, b).removeClass('hf-byp'); icon.find('.hf-nodes').append(nodeOf(icon, b)); membership = true; }
                    else if (!want && inChain(icon, b)) { icon.find('.hf-palette').append(nodeOf(icon, b)); membership = true; }
                }
            } else if (sym === 'amp_model')        icon.data('hf_amp_m', parseInt(val, 10));
            else if (sym === 'amp_pamp_auto')      icon.data('hf_amp_auto', val > 0.5);
            else if (sym === 'fz_pedal')           icon.data('hf_fz_p', parseInt(val, 10));
            else if (sym === 'dl_type')            icon.data('hf_dl_t', parseInt(val, 10));
            else if (sym === 'md_type')            setModelVal(icon, 'md', parseInt(val, 10));
            else if (sym === 'dr_model')           drm = parseInt(val, 10);
            fns.set_port_value(sym, val);
        });
        if (sawPos || membership) resort(icon);
        if (membership) renderPalette(icon);
        applyAmp(icon); applyFuzz(icon); applyDelay(icon);
        if (drm != null) { show(icon, 'dr', '.c-dr-oct', drm === 1); show(icon, 'dr', '.c-dr-nam', drm === 3); setModelVal(icon, 'dr', drm); }
        selectNode(icon, icon.data('hf_sel'));   // keep selection valid + refresh the panel
    }

    if (event.type == 'start') {
        var icon = event.icon;
        setupNodes(icon, funcs);
        // (Node connector is now a cheap CSS-scrolled baked strip — no JS animation loop.)
        // "+ ADD" palette toggle
        icon.find('.hf-add').each(function () {
            var el = this;
            el.addEventListener('click', function (e) { e.stopPropagation(); icon.find('.hf-palette').toggleClass('hf-open'); });
        });

        // Seed conditional visibility, membership, bypass + slot order from START values.
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
        setModelVal(icon, 'dr', drm);
        setModelVal(icon, 'md', parseInt(map.md_type || 0, 10));
        setNodeVal(icon, 'cab', 'Factory Cab');   // updated by setIr once the IR path arrives
        // Input Trim: dot reflects it_enable (1=active)
        if ('it_enable' in map) nodeOf(icon, 'it').toggleClass('hf-byp', !(map.it_enable > 0.5));
        // Movable blocks: membership (enable) → chain vs palette; bypass → grey; pos
        BLOCKS.forEach(function (b) {
            if ((b + '_pos') in map) nodeOf(icon, b).attr('data-pos', parseInt(map[b + '_pos'], 10));
            if ((b + '_bypass') in map) nodeOf(icon, b).toggleClass('hf-byp', map[b + '_bypass'] > 0.5);
            if ((b + '_enable') in map && !(map[b + '_enable'] > 0.5)) icon.find('.hf-palette').append(nodeOf(icon, b));
        });
        resort(icon); renderPalette(icon);
        selectNode(icon, 'amp');

        // Preset strip wiring.
        function wire(sel, fn) { icon.find(sel).each(function () { var el = this;
            el.addEventListener('click', function (e) { e.stopPropagation(); fn(); }); }); }
        wire('.hf-ps-bankdn', function () { psPulse(funcs, 'ps_bank_dn'); });
        wire('.hf-ps-bankup', function () { psPulse(funcs, 'ps_bank_up'); });
        wire('.hf-ps-save',   function () { psPulse(funcs, 'ps_save'); });
        wire('.hf-ps-mvup',   function () { psPulse(funcs, 'ps_move_up'); });
        wire('.hf-ps-mvdn',   function () { psPulse(funcs, 'ps_move_dn'); });
        wire('.hf-ps-backup', function () { psPulse(funcs, 'ps_backup'); });
        wire('.hf-ps-restore',function () { psPulse(funcs, 'ps_restore'); });
        wire('.hf-ps-toggle', function () { icon.find('[rata-role=psmenu]').toggleClass('hf-ps-open'); });
        icon.find('.hf-ps-slot').each(function () { var el = this;
            el.addEventListener('click', function (e) { e.stopPropagation();
                psPulse(funcs, SW[parseInt(el.getAttribute('data-slot'), 10)]); }); });
        icon.find('.hf-ps-name').each(function () {
            var el = this;
            el.addEventListener('mousedown', function (e) { e.stopPropagation(); });
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
    } else if (event.type == 'change') {
        var icon = event.icon, s = event.symbol;
        if (s && /_pos$/.test(s)) {
            var b = s.replace(/_pos$/, ''), want = parseInt(event.value, 10), cur = posOf(icon, b);
            if (want === cur) { resort(icon); return; }
            if (inChain(icon, b)) moveToSlot(icon, funcs, b, want);
            else nodeOf(icon, b).attr('data-pos', want);
        } else if (s && /_bypass$/.test(s)) {
            nodeOf(icon, s.replace(/_bypass$/, '')).toggleClass('hf-byp', event.value > 0.5);
        } else if (s && /_enable$/.test(s)) {
            var eb = s.replace(/_enable$/, '');
            if (eb === 'it') { nodeOf(icon, 'it').toggleClass('hf-byp', !(event.value > 0.5)); }
            else {
                var on = event.value > 0.5, isin = inChain(icon, eb);
                if (on && !isin) { nodeOf(icon, eb).removeClass('hf-byp'); icon.find('.hf-nodes').append(nodeOf(icon, eb)); resort(icon); renderPalette(icon); }
                else if (!on && isin) { icon.find('.hf-palette').append(nodeOf(icon, eb)); resort(icon); renderPalette(icon); if (icon.data('hf_sel') === eb) selectNode(icon, 'it'); }
            }
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
            setModelVal(icon, 'dr', dm);
        } else if (s === 'md_type') {
            setModelVal(icon, 'md', parseInt(event.value, 10));
        } else if (s === 'dl_type') {
            icon.data('hf_dl_t', parseInt(event.value, 10)); applyDelay(icon);
        } else if (s === 'clip') {
            icon.find('.hf-clip').toggleClass('hf-clip-on', event.value > 0.5);
        } else if (s === 'tuner_on') {
            icon.find('[rata-role=tuner]').toggleClass('mod-hidden', !(event.value > 0.5));
        } else if (s === 'tuner_note') {
            tunerNote(icon, event.value);
        } else if (s === 'tuner_cents') {
            tunerCents(icon, event.value);
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
        } else if (event.uri && event.uri.indexOf('#meters') >= 0) {
            var mv = ('' + event.value).split('|');
            if (mv.length >= 2) { setMeter(icon, 'imeter', parseFloat(mv[0])); setMeter(icon, 'ometer', parseFloat(mv[1])); }
        } else if (event.uri && event.uri.indexOf('#tuner') >= 0) {
            var tv = ('' + event.value).split('|');
            if (tv.length >= 2) { tunerNote(icon, parseInt(tv[0], 10)); tunerCents(icon, parseFloat(tv[1])); }
        }
    }
}
