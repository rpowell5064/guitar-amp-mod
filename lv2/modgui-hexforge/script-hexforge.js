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
        resort(icon);
    } else if (event.type == 'change') {
        var icon = event.icon, s = event.symbol;
        if (s && /_pos$/.test(s)) {
            var b = s.replace(/_pos$/, ''), want = parseInt(event.value, 10), cur = posOf(icon, b);
            if (want === cur) { resort(icon); return; }   // echo of our own write / already there
            moveToSlot(icon, funcs, b, want);
        } else if (s && /_enable$/.test(s)) {
            tileOf(icon, s.replace(/_enable$/, '')).toggleClass('hf-off', !(event.value > 0.5));
        } else if (event.uri && event.uri.indexOf('#irfile') >= 0) {
            setIr(icon, event.value);
        }
    }
}
