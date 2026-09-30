function (event, funcs) {
    // Practice: a DAW-shaped editor for the 4-track looper and the drum
    // sequencer. Three surfaces the plugin cannot express as control ports —
    // the loop waveforms, the step grid, and MIDI import — travel over the
    // plugin's atom ports as JSON strings, the same mechanism the cabinet
    // already uses for its saved rigs. Everything else is an ordinary port.

    var BASE = 'https://rpowell5064.github.io/guitaramp-suite/practice';
    var PATTERN_URI  = BASE + '#pattern';
    var WAVEFORM_URI = BASE + '#waveform';
    var STATUS_URI   = BASE + '#status';

    // Instrument rows, in the order the plugin's Instrument enum defines them.
    // Laid out high-to-low the way a drummer reads a chart: cymbals on top,
    // kick at the bottom. The index is the enum value, NOT the row position —
    // those indices are baked into drumkit.dat and cannot be reordered here.
    var ROWS = [
        { i: 9,  n: 'Crash' },
        { i: 12, n: 'China' },
        { i: 13, n: 'Stack' },
        { i: 11, n: 'Ride Bell' },
        { i: 10, n: 'Ride' },
        { i: 8,  n: 'Hat Open' },
        { i: 7,  n: 'Hat Pedal' },
        { i: 6,  n: 'Hat Closed' },
        { i: 14, n: 'Rimshot' },
        { i: 2,  n: 'Sidestick' },
        { i: 1,  n: 'Snare' },
        { i: 3,  n: 'Tom Hi' },
        { i: 4,  n: 'Tom Mid' },
        { i: 5,  n: 'Tom Floor' },
        { i: 0,  n: 'Kick' }
    ];

    // The step alphabet the pattern table uses. Clicking a cell walks this
    // list, so the four states a drummer actually needs (silence, ghost,
    // normal, accent) are one click apart and nothing else is reachable.
    var CYCLE = ['.', '-', 'o', 'x', 'X'];

    // dB span shared by every fader port (see the note in the `start` handler).
    var FADER_MIN = -60, FADER_MAX = 12;
    // Tempo span, same deal -- checked against the TTL by practice_port_check.py.
    var TEMPO_MIN = 20, TEMPO_MAX = 300;

    // ── small helpers ────────────────────────────────────────────────────────
    function R(icon, role) { return icon.find('[rata-role=' + role + ']'); }
    function el(icon, role) { var q = R(icon, role); return q.length ? q[0] : null; }
    function clamp(v, a, b) { return v < a ? a : (v > b ? b : v); }

    function setPort(icon, sym, val) {
        // Write through mod-ui so the host, any bound MIDI control and the
        // widget all stay in step; a bare DOM change would update none of them.
        if (funcs && typeof funcs.set_port_value === 'function') funcs.set_port_value(sym, val);
        else icon.find('[mod-port-symbol=' + sym + ']').trigger('valuechange', val);
    }
    // The transport ports are lv2:trigger: the plugin edge-detects them, so a
    // press is a pulse, not a latch. Without the release a second press would
    // be no edge at all and the button would work exactly once.
    function pulse(icon, sym) {
        setPort(icon, sym, 1);
        setTimeout(function () { setPort(icon, sym, 0); }, 60);
    }

    function b64bytes(s) {
        if (!s) return null;
        var A = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
        var out = [], acc = 0, bits = 0;
        for (var i = 0; i < s.length; ++i) {
            var c = A.indexOf(s.charAt(i));
            if (c < 0) continue;                 // '=' padding and any stray whitespace
            acc = (acc << 6) | c; bits += 6;
            if (bits >= 8) { bits -= 8; out.push((acc >> bits) & 255); }
        }
        return out;
    }

    // ── waveform lanes ───────────────────────────────────────────────────────
    function drawWave(icon, trk) {
        var c = el(icon, 'wave' + trk); if (!c) return;
        var g = c.getContext('2d'); if (!g) return;
        // Size the bitmap to the box it is painted into. Left at its markup
        // size the browser rescales it to fit, which softens every edge --
        // and the lane is a different height at 1280 wide than it was at 980.
        var box = c.parentNode;
        var bw = (box && box.clientWidth) || 0, bh = (box && box.clientHeight) || 0;
        if (bw < 40) bw = icon.data('px_wavew') || 1000;      // hidden tab: keep the last good size
        if (bh < 20) bh = icon.data('px_waveh') || 86;
        icon.data('px_wavew', bw); icon.data('px_waveh', bh);
        if (c.width !== bw) c.width = bw;
        if (c.height !== bh) c.height = bh;
        var W = c.width, H = c.height, mid = H / 2;
        g.clearRect(0, 0, W, H);

        var W_ = icon.data('px_wave') || {};
        var pk = W_.t && W_.t[trk - 1];
        var bars = W_.bars || 0;

        // Bar rules first, so the waveform is drawn over them rather than
        // fighting them for the same pixels.
        if (bars > 1 && bars <= 64) {
            g.strokeStyle = 'rgba(255,255,255,.07)'; g.lineWidth = 1;
            for (var b = 1; b < bars; ++b) {
                var x = Math.round(W * b / bars) + 0.5;
                g.beginPath(); g.moveTo(x, 4); g.lineTo(x, H - 4); g.stroke();
            }
        }
        g.strokeStyle = 'rgba(255,255,255,.10)';
        g.beginPath(); g.moveTo(0, mid + 0.5); g.lineTo(W, mid + 0.5); g.stroke();

        R(icon, 'empty' + trk).css('display', (pk && pk.length) ? 'none' : '');
        if (pk && pk.length) {
            g.fillStyle = icon.data('px_sel') === trk ? '#f0a830' : '#7d8493';
            var n = pk.length;
            for (var i = 0; i < W; ++i) {
                var v = pk[Math.min(n - 1, Math.floor(i * n / W))] / 255;
                var h = Math.max(1, v * (mid - 3));
                g.fillRect(i, mid - h, 1, h * 2);
            }
        }

        // Playhead. Only meaningful while something is actually playing back.
        var prog = icon.data('px_prog');
        var st = icon.data('px_st' + trk) || 0;
        if (prog >= 0 && prog <= 1 && st >= 1 && st <= 3 && pk && pk.length) {
            var px = Math.round(W * prog) + 0.5;
            g.strokeStyle = '#ffffff'; g.lineWidth = 1;
            g.beginPath(); g.moveTo(px, 0); g.lineTo(px, H); g.stroke();
        }
    }
    function drawAllWaves(icon) { for (var t = 1; t <= 4; ++t) drawWave(icon, t); }

    function waveParse(icon, json) {
        var d = null;
        try { d = JSON.parse(json); } catch (e) { return; }
        if (!d || !d.t) return;
        var tracks = [];
        for (var i = 0; i < 4; ++i) tracks.push(b64bytes(d.t[i]));
        icon.data('px_wave', { v: d.v, bars: d.bars || 0, len: d.len || 0, t: tracks });
        drawAllWaves(icon);
    }

    // Track state, in the plugin's stateCode() order:
    //   0 empty · 1 recording · 2 overdubbing · 3 playing · 4 stopped.
    // Overdub counts as recording to the eye — the track is taking signal — so
    // both 1 and 2 light the red lamp.
    function laneState(icon, trk, st) {
        icon.data('px_st' + trk, st);
        var name = (st === 1 || st === 2) ? 'rec' : st === 3 ? 'play' : st === 4 ? 'stop' : '';
        R(icon, 'lamp' + trk).attr('data-st', name);
        // The Rec and Play buttons light for the SELECTED track only: they are
        // per-track commands, so a lit button has to mean "this track".
        if (icon.data('px_sel') === trk) {
            R(icon, 'btnrec').toggleClass('armed', st === 1 || st === 2);
            R(icon, 'btnplay').toggleClass('armed', st === 3);
        }
        drawWave(icon, trk);
    }

    function selectTrack(icon, trk, write) {
        icon.data('px_sel', trk);
        icon.find('.px-lane').each(function () {
            this.classList.toggle('sel', parseInt(this.getAttribute('data-trk'), 10) === trk);
        });
        if (write) setPort(icon, 'loop_track', trk);
        laneState(icon, trk, icon.data('px_st' + trk) || 0);
        drawAllWaves(icon);
    }

    // ── step grid ────────────────────────────────────────────────────────────
    var LBL_W = 96, CELL_H = 26, GAP = 2;

    // Hit strengths, weakest first. Height is what separates them at a glance —
    // four shades of amber in a 20px cell are not distinguishable, four bar
    // heights are. Colour carries the same order so the two agree.
    var HIT = {
        '-': { h: 0.26, fill: '#7a5c2b', edge: '#946f34' },   // ghost
        'o': { h: 0.48, fill: '#c08c2f', edge: '#d79c34' },   // soft
        'x': { h: 0.74, fill: '#f0a830', edge: '#ffc861' },   // normal
        'X': { h: 1.00, fill: '#ffd98a', edge: '#fff0c8' }    // accent
    };

    // The canvas is sized to the space available, so the grid never scrolls:
    // cells shrink instead. 5px is the floor at which a column is still a
    // visible target — a full 8-bar pattern is 128 steps and still fits.
    function gridFit(icon) {
        var c = el(icon, 'grid'); if (!c) return null;
        var box = c.parentNode;
        var w = (box && box.clientWidth) ? box.clientWidth - 16 : 0;
        // While the tab is hidden the container measures zero; keep the last
        // good width rather than collapsing the canvas to nothing.
        if (w < 200) w = icon.data('px_gridw') || 1100;
        icon.data('px_gridw', w);
        if (c.width !== w) c.width = w;
        return c;
    }

    function gridGeom(icon) {
        var p = icon.data('px_pat');
        var steps = p ? p.spb * p.bars : 16;
        var c = el(icon, 'grid');
        var avail = (c ? c.width : 1100) - LBL_W - 6;
        var cw = Math.max(5, Math.floor(avail / Math.max(1, steps)) - GAP);
        return { steps: steps, cw: cw, spb: p ? p.spb : 16 };
    }

    function drawGrid(icon) {
        var c = gridFit(icon); if (!c) return;
        var g = c.getContext('2d'); if (!g) return;
        var p = icon.data('px_pat');
        var G = gridGeom(icon);
        var H = ROWS.length * (CELL_H + GAP) + GAP;
        if (c.height !== H) c.height = H;
        // Trim the bitmap to the grid it actually draws. Cell widths are
        // floored to whole pixels, so the remainder would otherwise show as a
        // dark ragged gutter down the right-hand edge.
        var gridW = LBL_W + G.steps * (G.cw + GAP);
        if (gridW > 0 && gridW < c.width) c.width = gridW;
        g.clearRect(0, 0, c.width, c.height);
        if (!p) return;

        var beat = Math.max(1, Math.round(G.spb / 4));   // 16ths -> 4, triplet 8ths -> 3
        var barSteps = G.spb;
        var step = icon.data('px_step');

        // Bar bands first: alternate bars get a faint wash so a 4- or 8-bar
        // pattern reads as bars rather than one long ribbon of steps.
        var nBars = Math.max(1, Math.round(G.steps / barSteps));
        for (var bi = 1; bi < nBars; bi += 2) {
            g.fillStyle = 'rgba(255,255,255,.020)';
            g.fillRect(LBL_W + bi * barSteps * (G.cw + GAP), 0, barSteps * (G.cw + GAP), c.height);
        }

        for (var r = 0; r < ROWS.length; ++r) {
            var y = GAP + r * (CELL_H + GAP);
            var lane = p.lanes[ROWS[r].i] || '';

            g.fillStyle = (r % 2) ? 'rgba(255,255,255,.020)' : 'rgba(255,255,255,.042)';
            g.fillRect(0, y, gridW, CELL_H);

            g.fillStyle = '#9aa0ab';
            g.font = '600 10px "Helvetica Neue", Helvetica, Arial, sans-serif';
            g.textBaseline = 'middle';
            g.fillText(ROWS[r].n, 6, y + CELL_H / 2 + 1);

            for (var st = 0; st < G.steps; ++st) {
                var x = LBL_W + st * (G.cw + GAP);
                var ch = lane.charAt(st) || '.';
                var hit = HIT[ch];
                if (!hit) {
                    // Empty cell. The first step of each beat is a shade
                    // lighter so the bar keeps its shape when the lane is bare.
                    g.fillStyle = (st % beat === 0) ? 'rgba(255,255,255,.085)' : 'rgba(255,255,255,.035)';
                    g.fillRect(x, y + 3, G.cw, CELL_H - 6);
                    continue;
                }
                // A hit is a bar grown from the bottom of the cell: its HEIGHT
                // is the velocity, so strength is legible as a silhouette
                // rather than as a shade of amber.
                var full = CELL_H - 6;
                var h = Math.max(3, Math.round(full * hit.h));
                var top = y + 3 + (full - h);
                g.fillStyle = hit.fill;
                g.fillRect(x, top, G.cw, h);
                // A bright cap reads as the transient and keeps narrow cells
                // visible when the bar itself is only a few pixels wide.
                g.fillStyle = hit.edge;
                g.fillRect(x, top, G.cw, Math.min(2, h));
            }
        }

        // Beat hairlines, and a brighter rule on every bar line. At 16 steps the
        // cells are wide enough to count; at 64 or 128 they are not, and without
        // this the pattern loses its metre entirely.
        for (var t = beat; t < G.steps; t += beat) {
            var isBar = (t % barSteps) === 0;
            g.fillStyle = isBar ? 'rgba(255,255,255,.30)' : 'rgba(255,255,255,.11)';
            g.fillRect(LBL_W + t * (G.cw + GAP) - 2, 0, isBar ? 2 : 1, c.height);
        }
        // Close the label gutter with the same rule so the grid has an edge.
        g.fillStyle = 'rgba(255,255,255,.30)';
        g.fillRect(LBL_W - 2, 0, 2, c.height);

        // Playhead column, drawn last so it sits over the cells.
        if (step >= 0 && step < G.steps) {
            var hx = LBL_W + step * (G.cw + GAP);
            g.fillStyle = 'rgba(255,255,255,.15)';
            g.fillRect(hx, 0, G.cw, c.height);
        }
    }

    // Live panel state. It arrives on the atom channel rather than from the
    // output control ports because mod-host forwards those to a web GUI far too
    // sparsely to animate anything -- measured at one update in 2.5 s on the
    // device, against the four numbers in two seconds a count-in has to show.
    function statusParse(icon, json) {
        var d = null;
        try { d = JSON.parse(json); } catch (e) { return; }
        if (!d) return;
        if (d.st && d.st.length) {
            for (var t = 0; t < d.st.length && t < 4; ++t) laneState(icon, t + 1, d.st[t]);
        }
        if (typeof d.pr === 'number') portApply(icon, 'out_progress', d.pr / 400);
        if (typeof d.bars === 'number') icon.data('px_bars', d.bars);
        if (typeof d.step === 'number') portApply(icon, 'out_step', d.step);
        if (typeof d.undo === 'number') portApply(icon, 'out_undo_avail', d.undo);
        if (typeof d.ci === 'number') portApply(icon, 'out_countin', d.ci);
    }

    function patStatus(icon, msg) { R(icon, 'patstatus').text(msg || ''); }

    // ── dropdowns ────────────────────────────────────────────────────────────
    // Ours, not mod-ui's. Its custom-select widget never opened on this host,
    // so the groove could not be changed at all; and it does not reliably write
    // the selected label either, which left the box blank. Opening, labelling
    // and writing the port are all handled here, through set_port_value, which
    // is the one path on this stack that has proved dependable.
    function selClose(icon) {
        icon.find('.px-sel').each(function () { this.classList.remove('open'); });
    }
    function selLabel(icon, role, value) {
        var wrap = R(icon, role); if (!wrap.length) return;
        var want = Math.round(value), label = null;
        wrap.find('[mod-role=enumeration-option]').each(function () {
            if (Math.round(parseFloat(this.getAttribute('mod-parameter-value'))) === want)
                label = (this.textContent || '').replace(/^\s+|\s+$/g, '');
        });
        if (label) wrap.find('.mod-enumerated-selected').text(label);
    }
    function bindSelect(icon, role) {
        var wrap = R(icon, role); if (!wrap.length) return;
        var sym = wrap.attr('data-sym');
        wrap.find('.mod-enumerated-selected').on('click', function (e) {
            var was = wrap[0].classList.contains('open');
            selClose(icon);
            if (!was) wrap[0].classList.add('open');
            e.stopPropagation();
        });
        wrap.find('[mod-role=enumeration-option]').each(function () {
            var opt = this;
            $(opt).on('click', function (e) {
                var v = parseFloat(opt.getAttribute('mod-parameter-value'));
                if (!isNaN(v)) {
                    wrap.find('.mod-enumerated-selected').text(
                        (opt.textContent || '').replace(/^\s+|\s+$/g, ''));
                    setPort(icon, sym, v);
                }
                wrap[0].classList.remove('open');
                e.stopPropagation();
            });
        });
    }

    // Parse either shape the plugin sends: a factory groove (builtin:1) or the
    // user's own edited pattern echoed back.
    function patParse(icon, json) {
        var d = null;
        try { d = JSON.parse(json); } catch (e) { return; }
        if (!d || !d.lanes) return;
        var spb = clamp(d.spb || 16, 1, 32), bars = clamp(d.bars || 1, 1, 8);
        var lanes = {};
        for (var i = 0; i < d.lanes.length; ++i) {
            var L = d.lanes[i];
            if (L && typeof L.i === 'number') lanes[L.i] = '' + (L.s || '');
        }
        icon.data('px_pat', { spb: spb, bars: bars, lanes: lanes, builtin: !!d.builtin });
        patStatus(icon, d.builtin ? ('Factory groove — ' + (d.name || '')) : 'Edited pattern');
        // Label the picker from the PLUGIN's own message rather than waiting for
        // a port echo. mod-ui does not reliably echo a programmatic port write
        // back as an event (it did not here, on the device), but the pattern
        // push always arrives — it is what redrew this grid.
        if (d.builtin && d.name) {
            var box = R(icon, 'selgroove').find('.mod-enumerated-selected');
            if (box.length) box.text(d.name);
        }
        drawGrid(icon);
    }

    // Post the grid back to the plugin. Empty lanes are dropped: the plugin's
    // user-pattern slot has a fixed event budget and all-rest lanes spend it
    // on nothing.
    function patSend(icon) {
        var p = icon.data('px_pat'); if (!p) return;
        var steps = p.spb * p.bars, out = [];
        for (var k in p.lanes) {
            if (!p.lanes.hasOwnProperty(k)) continue;
            var s = p.lanes[k];
            if (!s || !/[^.]/.test(s)) continue;
            while (s.length < steps) s += '.';
            out.push({ i: parseInt(k, 10), s: s.substring(0, steps) });
        }
        if (funcs && typeof funcs.patch_set === 'function')
            funcs.patch_set(PATTERN_URI, 's', JSON.stringify({ spb: p.spb, bars: p.bars, lanes: out }));
        p.builtin = false;
        patStatus(icon, 'Edited pattern');
    }

    function gridHit(icon, ev, clear) {
        var c = el(icon, 'grid'); if (!c) return;
        var p = icon.data('px_pat'); if (!p) return;
        var rect = c.getBoundingClientRect();
        var x = (ev.clientX - rect.left) * (c.width / rect.width);
        var y = (ev.clientY - rect.top) * (c.height / rect.height);
        var G = gridGeom(icon);
        var row = Math.floor((y - GAP) / (CELL_H + GAP));
        if (row < 0 || row >= ROWS.length) return;
        var s = Math.floor((x - LBL_W) / (G.cw + GAP));
        if (s < 0 || s >= G.steps || x < LBL_W) return;

        var inst = ROWS[row].i, steps = G.steps;
        var lane = p.lanes[inst] || '';
        while (lane.length < steps) lane += '.';
        var cur = lane.charAt(s);
        var next;
        if (clear) next = '.';
        else {
            var at = CYCLE.indexOf(cur);
            next = CYCLE[(at < 0 ? 0 : at + 1) % CYCLE.length];
        }
        p.lanes[inst] = lane.substring(0, s) + next + lane.substring(s + 1);
        drawGrid(icon);
        patSend(icon);
    }

    // ── MIDI import ──────────────────────────────────────────────────────────
    // Parsed here rather than in the plugin: the browser already has the file,
    // and a General-MIDI drum track is just note numbers on a tick grid — far
    // less code in JS than a real-time-safe SMF reader in the audio plugin.
    var GM = {
        35: 0, 36: 0,                       // kicks
        38: 1, 40: 14,                      // snare, electric snare -> rimshot
        37: 2,                              // side stick
        41: 5, 43: 5, 45: 4, 47: 4, 48: 3, 50: 3,
        42: 6, 44: 7, 46: 8,                // hats: closed, pedal, open
        49: 9, 52: 12, 55: 9, 57: 9,        // crashes; 52 = china
        51: 10, 53: 11, 59: 10              // ride, bell, ride 2
    };

    function midiParse(buf) {
        var d = new DataView(buf), u8 = new Uint8Array(buf);
        function str(o, n) { var s = ''; for (var i = 0; i < n; ++i) s += String.fromCharCode(u8[o + i]); return s; }
        if (buf.byteLength < 14 || str(0, 4) !== 'MThd') return null;
        var ntrk = d.getUint16(10), div = d.getUint16(12);
        if (div & 0x8000) return null;                  // SMPTE timing: not a musical grid
        var ppq = div || 480;

        var notes = [], pos = 8 + d.getUint32(4);
        for (var t = 0; t < ntrk && pos + 8 <= buf.byteLength; ++t) {
            if (str(pos, 4) !== 'MTrk') break;
            var len = d.getUint32(pos + 4), p = pos + 8, end = Math.min(buf.byteLength, p + len);
            pos = p + len;
            var tick = 0, running = 0;
            while (p < end) {
                var v = 0, b;
                do { b = u8[p++]; v = (v << 7) | (b & 127); } while ((b & 128) && p < end);
                tick += v;
                var st = u8[p];
                if (st & 128) { p++; running = st; } else { st = running; }
                var hi = st & 0xf0;
                if (st === 0xff) { var ty = u8[p++]; var l = 0, c; do { c = u8[p++]; l = (l << 7) | (c & 127); } while (c & 128); p += l; }
                else if (st === 0xf0 || st === 0xf7) { var l2 = 0, c2; do { c2 = u8[p++]; l2 = (l2 << 7) | (c2 & 127); } while (c2 & 128); p += l2; }
                else if (hi === 0x80 || hi === 0x90 || hi === 0xa0 || hi === 0xb0 || hi === 0xe0) {
                    var n = u8[p], vel = u8[p + 1]; p += 2;
                    // Channel 10 is the GM drum channel, but plenty of exported
                    // loops sit on channel 1; accept any channel and let the GM
                    // note map decide what is a drum.
                    if (hi === 0x90 && vel > 0) notes.push({ t: tick, n: n, v: vel });
                } else if (hi === 0xc0 || hi === 0xd0) { p += 1; }
                else { p++; }
            }
        }
        if (!notes.length) return null;

        // Quantise to 16ths and keep whole bars. Anything finer than a 16th
        // would not survive the plugin's step grid anyway.
        var spb = 16, tickPerStep = ppq / 4;
        var maxStep = 0;
        for (var i = 0; i < notes.length; ++i) {
            notes[i].s = Math.round(notes[i].t / tickPerStep);
            if (notes[i].s > maxStep) maxStep = notes[i].s;
        }
        var bars = clamp(Math.ceil((maxStep + 1) / spb), 1, 8);
        var steps = spb * bars, lanes = {}, used = 0;
        for (var j = 0; j < notes.length; ++j) {
            var inst = GM[notes[j].n];
            if (inst === undefined) continue;
            var s2 = notes[j].s;
            if (s2 >= steps) continue;
            if (!lanes[inst]) { lanes[inst] = new Array(steps + 1).join('.'); }
            var ch = notes[j].v >= 112 ? 'X' : notes[j].v >= 92 ? 'x' : notes[j].v >= 64 ? 'o' : '-';
            lanes[inst] = lanes[inst].substring(0, s2) + ch + lanes[inst].substring(s2 + 1);
            used++;
        }
        if (!used) return null;
        return { spb: spb, bars: bars, lanes: lanes, builtin: false };
    }

    function midiLoad(icon, file) {
        if (!file || typeof FileReader === 'undefined') return;
        var rd = new FileReader();
        rd.onload = function () {
            var pat = null;
            try { pat = midiParse(rd.result); } catch (e) { pat = null; }
            if (!pat) { patStatus(icon, 'Could not read that MIDI file as a drum part.'); return; }
            icon.data('px_pat', pat);
            drawGrid(icon);
            patSend(icon);
            patStatus(icon, 'Imported ' + file.name + ' — ' + pat.bars + ' bar' + (pat.bars > 1 ? 's' : ''));
        };
        rd.onerror = function () { patStatus(icon, 'Could not read that file.'); };
        rd.readAsArrayBuffer(file);
    }

    // ── faders ───────────────────────────────────────────────────────────────
    // mod-ui binds the drag but paints nothing on a bare div, so the fill and
    // the cap are positioned here from the port's own value.
    function faderSet(icon, sym, val) {
        var map = icon.data('px_fad'); var f = map && map[sym]; if (!f) return;
        var frac = (f.max > f.min) ? clamp((val - f.min) / (f.max - f.min), 0, 1) : 0;
        // Positioned in PERCENT, never from a measured height: the MIX tab is
        // display:none when `start` fires, so every element there measures zero
        // and each fader was being pinned to the bottom of its track. calc()
        // resolves at paint time, when the tab is actually visible.
        var travel = '(100% - 14px) * ' + frac.toFixed(4);
        var $e = $(f.el);
        $e.find('.px-fad-fill').css('height', 'calc(' + travel + ')');
        $e.find('.px-fad-hand').css('bottom', 'calc(1px + ' + travel + ')');
    }

    // ── tempo drag ───────────────────────────────────────────────────────────
    // Driven here rather than by a mod-ui control widget: mod-ui's widget is a
    // "film" that reads its step count off a background sprite, so a plain
    // element with a CSS background is half-initialised and silently does
    // nothing. Dragging the number is the DAW idiom regardless.
    function tempoNow(icon) {
        var v = parseFloat(R(icon, 'tempofield').text());
        return isNaN(v) ? 120 : v;
    }
    function tempoSet(icon, v) {
        v = Math.round(clamp(v, TEMPO_MIN, TEMPO_MAX));
        R(icon, 'tempofield').text(String(v));
        setPort(icon, 'tempo', v);
        return v;
    }
    function bindTempo(icon) {
        var grab = R(icon, 'tempograb');
        if (!grab.length) return;
        var dragging = false, startY = 0, startV = 120;

        function synced() { return R(icon, 'topbar').hasClass('px-synced'); }

        grab.on('mousedown', function (e) {
            if (synced()) return;          // the host owns the tempo
            dragging = true;
            startY = e.clientY;
            startV = tempoNow(icon);
            icon.data('px_tempodrag', true);
            e.preventDefault();
        });
        $(document).on('mousemove', function (e) {
            if (!dragging) return;
            // 2 px per BPM: fine enough to land on a number, coarse enough to
            // cross the useful range without a marathon drag.
            tempoSet(icon, startV + (startY - e.clientY) * 0.5);
            e.preventDefault();
        });
        $(document).on('mouseup', function () {
            if (!dragging) return;
            dragging = false;
            icon.data('px_tempodrag', false);
        });
        grab.on('wheel', function (e) {
            if (synced()) return;
            tempoSet(icon, tempoNow(icon) + ((e.deltaY || 0) > 0 ? -1 : 1));
            e.preventDefault();
        });
    }

    // ── events ───────────────────────────────────────────────────────────────
    if (event.type == 'start') {
        var icon = event.icon;
        icon.data('px_sel', 1);
        icon.data('px_prog', -1);
        icon.data('px_step', -1);

        // Fader ranges are baked in, NOT read from the host. mod-ui's `start`
        // event carries only {symbol, value} per port -- it has no ranges in
        // any shape -- so anything taken from it is undefined and every fader
        // ends up pinned at zero. Every port drawn as a fader here is a dB
        // level over the same span; practice_port_check.py asserts these two
        // numbers against the TTL so they cannot drift apart.
        var fad = {};
        icon.find('.px-fader').each(function () {
            var sym = this.getAttribute('mod-port-symbol');
            if (sym) fad[sym] = { el: this, min: FADER_MIN, max: FADER_MAX };
        });
        icon.data('px_fad', fad);

        R(icon, 'pxtabs').find('.px-tab').each(function () {
            var self = this;
            $(self).on('click', function () {
                var name = self.getAttribute('data-pxtab');
                R(icon, 'pxview').attr('data-tab', name);
                R(icon, 'pxtabs').find('.px-tab').each(function () {
                    this.classList.toggle('on', this.getAttribute('data-pxtab') === name);
                });
                // Canvases laid out while hidden measure zero, so repaint on show.
                if (name === 'loops') drawAllWaves(icon); else if (name === 'drums') drawGrid(icon);
            });
        });

        icon.find('.px-lane').each(function () {
            var trk = parseInt(this.getAttribute('data-trk'), 10);
            $(this).on('click', function (e) {
                // The MUTE button lives inside the lane; clicking it must not
                // also re-arm the track.
                if ($(e.target).closest('.px-mini').length) return;
                selectTrack(icon, trk, true);
            });
        });
        for (var t = 1; t <= 4; ++t) (function (t) {
            R(icon, 'mute' + t).on('click', function () {
                var on = !$(this).hasClass('on');
                $(this).toggleClass('on', on);
                setPort(icon, 'trk' + t + '_mute', on ? 1 : 0);
            });
        })(t);

        R(icon, 'btnrec').on('click',   function () { pulse(icon, 'loop_rec'); });
        R(icon, 'btnplay').on('click',  function () { pulse(icon, 'loop_play'); });
        R(icon, 'btnstop').on('click',  function () { pulse(icon, 'loop_stop'); });
        R(icon, 'btnundo').on('click',  function () { pulse(icon, 'loop_undo'); });
        R(icon, 'btnclear').on('click', function () { pulse(icon, 'loop_clear'); });

        R(icon, 'countpill').on('click', function () {
            var on = !$(this).hasClass('on');
            $(this).toggleClass('on', on);
            setPort(icon, 'count_in', on ? 1 : 0);
        });
        R(icon, 'runpill').on('click', function () {
            var on = !$(this).hasClass('on');
            $(this).toggleClass('on', on);
            setPort(icon, 'run', on ? 1 : 0);
        });
        R(icon, 'syncpill').on('click', function () {
            var on = !$(this).hasClass('on');
            $(this).toggleClass('on', on);
            R(icon, 'topbar').toggleClass('px-synced', on);
            setPort(icon, 'tempo_sync', on ? 1 : 0);
        });

        var grid = el(icon, 'grid');
        if (grid) {
            $(grid).on('click', function (e) { gridHit(icon, e, false); });
            $(grid).on('contextmenu', function (e) { e.preventDefault(); gridHit(icon, e, true); });
        }
        R(icon, 'btnmidi').on('click', function () { var f = el(icon, 'midifile'); if (f) f.click(); });
        R(icon, 'midifile').on('change', function () {
            if (this.files && this.files[0]) midiLoad(icon, this.files[0]);
            this.value = '';                       // so the same file can be re-imported
        });
        R(icon, 'btnrevert').on('click', function () {
            // Hand the groove back to the factory table; the plugin answers
            // with the selected pattern, which repaints the grid.
            if (funcs && typeof funcs.patch_set === 'function')
                funcs.patch_set(PATTERN_URI, 's', JSON.stringify({ revert: 1 }));
            patStatus(icon, 'Reverted to the factory groove.');
        });
        R(icon, 'btnclearpat').on('click', function () {
            var p = icon.data('px_pat'); if (!p) return;
            p.lanes = {};
            drawGrid(icon);
            patSend(icon);
        });

        bindTempo(icon);
        bindSelect(icon, 'selgroove');
        bindSelect(icon, 'sellen');
        $(document).on('click', function () { selClose(icon); });
        selectTrack(icon, 1, false);
        drawGrid(icon);
        drawAllWaves(icon);

        // Saved state the host replays at load, then a full refresh request so
        // a freshly opened editor gets the waveforms and the live groove.
        if (event.ports) {
            for (var k = 0; k < event.ports.length; ++k) {
                var po = event.ports[k];
                var pv = (po.value !== undefined) ? po.value
                       : (po.ranges ? po.ranges.default : undefined);
                if (pv !== undefined) portApply(icon, po.symbol, parseFloat(pv));
            }
        }
        if (event.parameters) {
            for (var q = 0; q < event.parameters.length; ++q) {
                var pr = event.parameters[q];
                if (!pr.uri || !pr.value) continue;
                if (pr.uri.indexOf('#pattern') >= 0) patParse(icon, pr.value);
                else if (pr.uri.indexOf('#waveform') >= 0) waveParse(icon, pr.value);
                else if (pr.uri.indexOf('#status') >= 0) statusParse(icon, pr.value);
            }
        }
        if (funcs && typeof funcs.patch_get === 'function') {
            funcs.patch_get(WAVEFORM_URI);
            funcs.patch_get(PATTERN_URI);
            funcs.patch_get(STATUS_URI);
        }

    } else if (event.type == 'change') {
        // mod-ui has exactly ONE change event and it carries either a patch
        // property (uri) or a control port (symbol) -- there is no separate
        // port event. Handling only the uri form, as this did, silently drops
        // every port update after load: the transport lamps, the playhead, the
        // bar readout, the undo dimming and the tempo readout all froze at
        // their start values on the device while looking perfect offline.
        if (event.uri) {
            if (event.uri.indexOf('#waveform') >= 0) waveParse(event.icon, event.value);
            else if (event.uri.indexOf('#pattern') >= 0) patParse(event.icon, event.value);
            else if (event.uri.indexOf('#status') >= 0) statusParse(event.icon, event.value);
        } else if (event.symbol) {
            portApply(event.icon, event.symbol, parseFloat(event.value));
        }
    }

    function portApply(icon, sym, value) {
        if (!sym) return;
        faderSet(icon, sym, value);

        if (sym === 'pattern')   { selLabel(icon, 'selgroove', value); return; }
        if (sym === 'loop_bars') { selLabel(icon, 'sellen', value); return; }
        if (sym === 'loop_track') { selectTrack(icon, clamp(Math.round(value), 1, 4), false); return; }
        if (sym === 'run')        { R(icon, 'runpill').toggleClass('on', value > 0.5); return; }
        if (sym === 'tempo_sync') {
            var on = value > 0.5;
            R(icon, 'syncpill').toggleClass('on', on);
            R(icon, 'topbar').toggleClass('px-synced', on);
            return;
        }
        // Skip while the user is dragging, or an echoed value would fight
        // the number under their cursor.
        if (sym === 'tempo') {
            if (!icon.data('px_tempodrag')) R(icon, 'tempofield').text(String(Math.round(value)));
            return;
        }
        if (sym === 'host_bpm') { R(icon, 'hostbpm').text('host ' + Math.round(value)); return; }

        if (sym === 'out_progress') {
            icon.data('px_prog', value);
            // Only the lanes that are actually rolling need a repaint; redrawing
            // four canvases on every progress tick would burn the Pi's CPU for
            // three pictures that did not change.
            for (var t = 1; t <= 4; ++t) {
                var stt = icon.data('px_st' + t) || 0;
                if (stt >= 1 && stt <= 3) drawWave(icon, t);
            }
            var W = icon.data('px_wave');
            var bars = icon.data('px_bars') || (W && W.bars) || 0;
            if (bars > 0) {
                var pos = value * bars;
                R(icon, 'barsread').text((Math.floor(pos) + 1) + ' . ' + bars);
            }
            return;
        }
        if (sym === 'out_step') {
            var s = Math.round(value);
            if (s !== icon.data('px_step')) { icon.data('px_step', s); drawGrid(icon); }
            return;
        }
        if (sym === 'count_in') { R(icon, 'countpill').toggleClass('on', value > 0.5); return; }

        if (sym === 'beats_per_bar') { icon.data('px_bpb', Math.max(1, Math.round(value))); return; }

        if (sym === 'out_countin') {
            var raw = Math.round(value);
            var bpb = icon.data('px_bpb') || 4;
            // A press lands anywhere in a bar, so up to a further bar can pass
            // before the count itself starts. That wait is ARMED, not counting:
            // showing "8" would contradict the four-beat count you can hear.
            var beats = (raw > 0 && raw <= bpb) ? raw : 0;
            R(icon, 'btnrec').toggleClass('armed', raw > 0 && beats === 0);
            var on = beats > 0;
            R(icon, 'countin').toggleClass('on', on);
            R(icon, 'btnrec').toggleClass('counting', on);
            if (on) {
                // Re-pop the number only when it actually changes, or the
                // animation would restart on every block and just shimmer.
                if (beats !== icon.data('px_cibeat')) {
                    icon.data('px_cibeat', beats);
                    var num = R(icon, 'cinum');
                    num.text(String(beats));
                    // restart the CSS animation
                    num.css('animation', 'none');
                    if (num.length && num[0].offsetHeight) { /* reflow */ }
                    num.css('animation', '');
                    // The first value we see is the length of this count, so
                    // the dots match the meter without being told it.
                    var total = icon.data('px_citotal') || 0;
                    if (beats > total) { icon.data('px_citotal', beats); total = beats; }
                    var dots = R(icon, 'cidots');
                    if (dots.length && dots[0].childNodes.length !== total) {
                        var html = '';
                        for (var d = 0; d < total; ++d) html += '<span class="px-ci-dot"></span>';
                        dots[0].innerHTML = html;
                    }
                    if (dots.length) {
                        var kids = dots[0].childNodes;
                        for (var k = 0; k < kids.length; ++k)
                            kids[k].className = 'px-ci-dot' + (k < beats ? ' lit' : '');
                    }
                }
            } else {
                icon.data('px_cibeat', 0);
                icon.data('px_citotal', 0);
            }
            return;
        }

        if (sym === 'out_undo_avail') { R(icon, 'btnundo').toggleClass('dim', value < 0.5); return; }

        var m = /^out_trk([1-4])_state$/.exec(sym);
        if (m) { laneState(icon, parseInt(m[1], 10), Math.round(value)); return; }
        var mm = /^trk([1-4])_mute$/.exec(sym);
        if (mm) { R(icon, 'mute' + mm[1]).toggleClass('on', value > 0.5); return; }
    }
}
