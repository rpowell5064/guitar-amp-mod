function (event, funcs) {
    // Hex Forge tile rack: live reordering + per-block on/off + "More" panels +
    // cab IR name. Reordering is driven by each movable block's `<pfx>_pos` port
    // (slot 1..9). The numeric slot selector in every tile head writes that port
    // natively (always reliable); drag-and-drop is a convenience layer on top that
    // renumbers the slots and writes them via funcs.set_port_value when available.
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
        rack.append(tileOf(icon, 'it'));               // locked head
        orderedBlocks(icon).forEach(function (b) { rack.append(tileOf(icon, b)); });
    }

    // Apply a new visual order: drop dragged block in front of target, then
    // renumber every movable slot 1..9 and push to the ports.
    function applyDrop(icon, dragB, targetB) {
        if (!dragB || dragB === targetB || targetB === 'it') return;
        var ord = orderedBlocks(icon).filter(function (b) { return b !== dragB; });
        var ti = ord.indexOf(targetB);
        if (ti < 0) ti = ord.length;
        ord.splice(ti, 0, dragB);
        var canSet = funcs && typeof funcs.set_port_value === 'function';
        ord.forEach(function (b, i) {
            var pos = i + 1;
            tileOf(icon, b).attr('data-pos', pos);
            if (canSet) funcs.set_port_value(b + '_pos', pos);
        });
        resort(icon);
    }

    function setupDrag(icon) {
        var dragB = null;
        icon.find('.hf-tile').each(function () {
            var node = this, b = node.getAttribute('data-block');
            if (b === 'it') return;                    // locked
            node.setAttribute('draggable', 'true');
            node.addEventListener('dragstart', function (e) {
                dragB = b; node.classList.add('hf-drag');
                if (e.dataTransfer) { e.dataTransfer.effectAllowed = 'move'; try { e.dataTransfer.setData('text/plain', b); } catch (x) {} }
            });
            node.addEventListener('dragend', function () { node.classList.remove('hf-drag'); dragB = null; });
            node.addEventListener('dragover', function (e) { e.preventDefault(); if (e.dataTransfer) e.dataTransfer.dropEffect = 'move'; });
            node.addEventListener('drop', function (e) {
                e.preventDefault(); e.stopPropagation();
                applyDrop(icon, dragB, node.getAttribute('data-block'));
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
            this.addEventListener('click', function () {
                var t = this; while (t && !(t.className && (' ' + t.className + ' ').indexOf(' hf-tile ') >= 0)) t = t.parentNode;
                if (t) t.classList.toggle('hf-open');
            });
        });
        setupDrag(icon);
        resort(icon);
    } else if (event.type == 'change') {
        var icon = event.icon, s = event.symbol;
        if (s && /_pos$/.test(s)) {
            tileOf(icon, s.replace(/_pos$/, '')).attr('data-pos', parseInt(event.value, 10));
            resort(icon);
        } else if (s && /_enable$/.test(s)) {
            tileOf(icon, s.replace(/_enable$/, '')).toggleClass('hf-off', !(event.value > 0.5));
        } else if (event.uri && event.uri.indexOf('#irfile') >= 0) {
            setIr(icon, event.value);
        }
    }
}
