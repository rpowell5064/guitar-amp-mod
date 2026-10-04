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
    var SEEK_URI     = BASE + '#seek';

    // Instrument rows, in the order the plugin's Instrument enum defines them.
    // Laid out high-to-low the way a drummer reads a chart: cymbals on top,
    // kick at the bottom. The index is the enum value, NOT the row position —
    // those indices are baked into drumkit.dat and cannot be reordered here.
    // Track colours, matching the CSS custom properties. Kept here as literals
    // because a canvas cannot read a CSS variable without a getComputedStyle
    // per frame, and these are drawn on every playhead tick.
    var TRACK_COL = ['#f0a830', '#45c8d8', '#a07bff', '#58cc82'];

    var ROWS = [
        { i: 9,  n: 'Crash', f: 'cym' },
        { i: 12, n: 'China', f: 'cym' },
        { i: 13, n: 'Stack', f: 'cym' },
        { i: 11, n: 'Ride Bell', f: 'cym' },
        { i: 10, n: 'Ride', f: 'cym' },
        { i: 8,  n: 'Hat Open', f: 'hat' },
        { i: 7,  n: 'Hat Pedal', f: 'hat' },
        { i: 6,  n: 'Hat Closed', f: 'hat' },
        { i: 14, n: 'Rimshot', f: 'snare' },
        { i: 2,  n: 'Sidestick', f: 'snare' },
        { i: 1,  n: 'Snare', f: 'snare' },
        { i: 3,  n: 'Tom Hi', f: 'tom' },
        { i: 4,  n: 'Tom Mid', f: 'tom' },
        { i: 5,  n: 'Tom Floor', f: 'tom' },
        { i: 0,  n: 'Kick', f: 'kick' }
    ];

    // The step alphabet the pattern table uses. Clicking a cell walks this
    // list, so the four states a drummer actually needs (silence, ghost,
    // normal, accent) are one click apart and nothing else is reachable.
    var CYCLE = ['.', '-', 'o', 'x', 'X'];

    // Drum families, coloured the way a kit is grouped by ear rather than by
    // MIDI number. Velocity is still the bar's HEIGHT; hue says what the
    // instrument IS, so a glance at the grid reads as a kit and not as a
    // spreadsheet. Four tints per family, weakest first.
    var FAMILY = {
        kick:  ['#8a6a2a', '#c49a3c', '#ffd166', '#ffe4a3'],
        snare: ['#7a5c2b', '#c08c2f', '#f0a830', '#ffd98a'],
        tom:   ['#8a4530', '#c76544', '#ff8a5c', '#ffb79a'],
        hat:   ['#24666e', '#2f949f', '#45c8d8', '#92e2ec'],
        cym:   ['#4f3d80', '#7257c0', '#a07bff', '#c8b2ff']
    };
    var HIT_LEVEL = { '-': 0, 'o': 1, 'x': 2, 'X': 3 };
    var HIT_H     = { '-': 0.26, 'o': 0.48, 'x': 0.74, 'X': 1.00 };

    // dB span shared by every fader port (see the note in the `start` handler).
    var FADER_MIN = -60, FADER_MAX = 12;
    // Tempo span, same deal -- checked against the TTL by practice_port_check.py.
    var TEMPO_MIN = 20, TEMPO_MAX = 300;


    // ── Scales ──────────────────────────────────────────────────────────────
    // Ported from the user's own `modes` project so the two agree note for
    // note: same eleven modes, same spellings (Eb/Ab/Bb, not sharps), same
    // diatonic degrees and numerals. This tab makes no sound and holds no
    // plugin state -- it is a reference you can leave open while playing.
    var FB_NOTES = ['C', 'C#', 'D', 'Eb', 'E', 'F', 'F#', 'G', 'Ab', 'A', 'Bb', 'B'];

    var FB_MODES = [
        { n: 'Ionian',           iv: [0, 2, 4, 5, 7, 9, 11] },
        { n: 'Dorian',           iv: [0, 2, 3, 5, 7, 9, 10] },
        { n: 'Phrygian',         iv: [0, 1, 3, 5, 7, 8, 10] },
        { n: 'Lydian',           iv: [0, 2, 4, 6, 7, 9, 11] },
        { n: 'Mixolydian',       iv: [0, 2, 4, 5, 7, 9, 10] },
        { n: 'Aeolian',          iv: [0, 2, 3, 5, 7, 8, 10] },
        { n: 'Locrian',          iv: [0, 1, 3, 5, 6, 8, 10] },
        { n: 'Major Pentatonic', iv: [0, 2, 4, 7, 9] },
        { n: 'Minor Pentatonic', iv: [0, 3, 5, 7, 10] },
        { n: 'Blues Pentatonic', iv: [0, 3, 5, 6, 7, 10] },
        { n: 'Harmonic Minor',   iv: [0, 2, 3, 5, 7, 8, 11] }
    ];

    // [semitones from the key root, quality, numeral] -- one row per mode above.
    var FB_DEGREES = [
        [[0,'maj','I'],[2,'min','ii'],[4,'min','iii'],[5,'maj','IV'],[7,'maj','V'],[9,'min','vi'],[11,'dim','vii\u00B0']],
        [[0,'min','i'],[2,'min','ii'],[3,'maj','III'],[5,'maj','IV'],[7,'min','v'],[9,'dim','vi\u00B0'],[10,'maj','VII']],
        [[0,'min','i'],[1,'maj','II'],[3,'maj','III'],[5,'min','iv'],[7,'dim','v\u00B0'],[8,'maj','VI'],[10,'min','vii']],
        [[0,'maj','I'],[2,'maj','II'],[4,'min','iii'],[6,'dim','#iv\u00B0'],[7,'maj','V'],[9,'min','vi'],[11,'min','vii']],
        [[0,'maj','I'],[2,'min','ii'],[4,'dim','iii\u00B0'],[5,'maj','IV'],[7,'min','v'],[9,'min','vi'],[10,'maj','VII']],
        [[0,'min','i'],[2,'dim','ii\u00B0'],[3,'maj','III'],[5,'min','iv'],[7,'min','v'],[8,'maj','VI'],[10,'maj','VII']],
        [[0,'dim','i\u00B0'],[1,'maj','II'],[3,'min','iii'],[5,'min','iv'],[6,'maj','V'],[8,'maj','VI'],[10,'min','vii']],
        [[0,'maj','I'],[2,'min','ii'],[4,'min','iii'],[7,'maj','V'],[9,'min','vi']],
        [[0,'min','i'],[3,'maj','III'],[5,'min','iv'],[7,'min','v'],[10,'maj','VII']],
        [[0,'min','i'],[3,'maj','III'],[5,'min','iv'],[7,'min','v'],[10,'maj','VII']],
        [[0,'min','i'],[2,'dim','ii\u00B0'],[3,'aug','III+'],[5,'min','iv'],[7,'maj','V'],[8,'maj','VI'],[11,'dim','vii\u00B0']]
    ];
    var FB_QUAL = { maj: [0,4,7], min: [0,3,7], dim: [0,3,6], aug: [0,4,8] };
    var FB_SUFFIX = { maj: '', min: 'm', dim: '\u00B0', aug: '+' };

    // Open strings, LOW to HIGH, as MIDI numbers. Sevens and eights add low
    // strings; a six-string bass adds a high C as well as the low B.
    var FB_TUNING = {
        guitar: { 6: [40,45,50,55,59,64], 7: [35,40,45,50,55,59,64], 8: [30,35,40,45,50,55,59,64] },
        bass:   { 4: [28,33,38,43], 5: [23,28,33,38,43], 6: [23,28,33,38,43,48] }
    };
    var FB_STRINGS = { guitar: [6, 7, 8], bass: [4, 5, 6] };

    function fbState(icon) {
        var st = icon.data('px_fb');
        if (!st) {
            st = { inst: 'guitar', strings: 6, key: 0, mode: 0 };
            icon.data('px_fb', st);
        }
        return st;
    }

    function fbPitchClass(n) { return ((n % 12) + 12) % 12; }

    // Which scale degree a pitch class is, or -1. Returns the INTERVAL so the
    // caller can colour roots, thirds and fifths differently -- those three are
    // what anyone is actually looking for on a fretboard.
    function fbDegree(st, pc) {
        var iv = FB_MODES[st.mode].iv;
        for (var i = 0; i < iv.length; ++i)
            if (fbPitchClass(st.key + iv[i]) === pc) return iv[i];
        return -1;
    }

    function fbDegColour(interval) {
        if (interval === 0) return '#ff5c8a';                       // root
        if (interval === 3 || interval === 4) return '#ffc247';     // third
        if (interval === 7) return '#49d6ff';                       // fifth
        return '#8d93a3';
    }

    function fbSeg(icon, role, items, current, onPick) {
        var box = el(icon, role);
        if (!box) return;
        var html = '';
        for (var i = 0; i < items.length; ++i) {
            html += '<div class="px-segb' + (items[i].v === current ? ' on' : '') +
                    '" data-v="' + items[i].v + '">' + items[i].t + '</div>';
        }
        box.innerHTML = html;
        $(box).find('.px-segb').each(function () {
            var b = this;
            $(b).on('click', function () {
                onPick(b.getAttribute('data-v'));
            });
        });
    }

    function fbBuildControls(icon) {
        var st = fbState(icon);
        fbSeg(icon, 'fbinst',
              [{ v: 'guitar', t: 'Guitar' }, { v: 'bass', t: 'Bass' }], st.inst,
              function (v) {
                  st.inst = v;
                  // Keep a sensible string count: a 6-string bass and a 6-string
                  // guitar are different instruments, so snap to the default.
                  st.strings = FB_STRINGS[v][0];
                  fbBuildControls(icon); fbDraw(icon);
              });
        fbSeg(icon, 'fbstrings',
              FB_STRINGS[st.inst].map(function (n) { return { v: String(n), t: n + ' string' }; }),
              String(st.strings),
              function (v) { st.strings = parseInt(v, 10); fbBuildControls(icon); fbDraw(icon); });
        fbSeg(icon, 'fbkey',
              FB_NOTES.map(function (n, i) { return { v: String(i), t: n }; }), String(st.key),
              function (v) { st.key = parseInt(v, 10); fbBuildControls(icon); fbDraw(icon); });
        fbSeg(icon, 'fbscale',
              FB_MODES.map(function (m, i) { return { v: String(i), t: m.n }; }), String(st.mode),
              function (v) { st.mode = parseInt(v, 10); fbBuildControls(icon); fbDraw(icon); });
    }

    function fbDraw(icon) {
        var st = fbState(icon);
        var tuning = FB_TUNING[st.inst][st.strings];
        if (!tuning) return;

        R(icon, 'fbtitle').text(FB_NOTES[st.key] + ' ' + FB_MODES[st.mode].n);

        // The notes of the key, in order, coloured by degree.
        var iv = FB_MODES[st.mode].iv, nhtml = '';
        for (var i = 0; i < iv.length; ++i) {
            var cls = iv[i] === 0 ? ' root'
                    : (iv[i] === 3 || iv[i] === 4) ? ' third'
                    : iv[i] === 7 ? ' fifth' : '';
            nhtml += '<span class="px-fbnote' + cls + '">' +
                     FB_NOTES[fbPitchClass(st.key + iv[i])] + '</span>';
        }
        var nb = el(icon, 'fbnotes');
        if (nb) nb.innerHTML = nhtml;

        var c = el(icon, 'fretboard');
        if (!c) return;
        var g = c.getContext('2d');
        if (!g) return;

        var FRETS = 15;
        var nStr = tuning.length;
        var W = c.width, H = c.height;
        var padL = 54, padR = 16, padT = 26, padB = 20;
        var boardW = W - padL - padR, boardH = H - padT - padB;
        var rowH = boardH / (nStr - 1);
        var fretW = boardW / (FRETS + 1);        // +1 leaves the open-string column

        g.clearRect(0, 0, W, H);

        // Fret positions, and the dot markers a player navigates by.
        var MARK = { 3: 1, 5: 1, 7: 1, 9: 1, 15: 1 }, DBL = { 12: 1 };
        g.textAlign = 'center';
        g.textBaseline = 'middle';
        for (var f = 0; f <= FRETS; ++f) {
            var x = padL + fretW * (f + 0.5);
            if (MARK[f] || DBL[f]) {
                g.fillStyle = 'rgba(255,255,255,.045)';
                g.fillRect(padL + fretW * f, padT - 6, fretW, boardH + 12);
            }
            g.fillStyle = 'rgba(255,255,255,.30)';
            g.font = '600 9px ui-sans-serif,system-ui,sans-serif';
            g.fillText(String(f), x, padT - 14);
        }

        // Frets, then strings over them.
        g.strokeStyle = 'rgba(255,255,255,.16)';
        g.lineWidth = 1;
        for (var fl = 1; fl <= FRETS + 1; ++fl) {
            var fx = Math.round(padL + fretW * fl) + 0.5;
            g.beginPath(); g.moveTo(fx, padT); g.lineTo(fx, padT + boardH); g.stroke();
        }
        // The nut is the thick one.
        g.strokeStyle = 'rgba(255,255,255,.55)'; g.lineWidth = 3;
        var nx = Math.round(padL + fretW) + 0.5;
        g.beginPath(); g.moveTo(nx, padT - 4); g.lineTo(nx, padT + boardH + 4); g.stroke();

        for (var sI = 0; sI < nStr; ++sI) {
            var y = Math.round(padT + rowH * (nStr - 1 - sI)) + 0.5;
            // Thicker low strings, as they look on the instrument.
            g.lineWidth = 1 + (nStr - 1 - sI) * 0.22;
            g.strokeStyle = 'rgba(255,255,255,.22)';
            g.beginPath(); g.moveTo(padL, y); g.lineTo(padL + boardW, y); g.stroke();

            g.fillStyle = 'rgba(255,255,255,.45)';
            g.font = '700 10px ui-sans-serif,system-ui,sans-serif';
            g.textAlign = 'right';
            g.fillText(FB_NOTES[fbPitchClass(tuning[sI])], padL - 10, y);
            g.textAlign = 'center';
        }

        // The notes themselves.
        for (var s2 = 0; s2 < nStr; ++s2) {
            var yy = padT + rowH * (nStr - 1 - s2);
            for (var fr = 0; fr <= FRETS; ++fr) {
                var pc = fbPitchClass(tuning[s2] + fr);
                var deg = fbDegree(st, pc);
                if (deg < 0) continue;
                var cx = padL + fretW * (fr + 0.5);
                var col = fbDegColour(deg);
                var r = deg === 0 ? 11 : 9;
                g.beginPath(); g.arc(cx, yy, r, 0, Math.PI * 2);
                g.fillStyle = col; g.fill();
                if (deg === 0) {
                    g.lineWidth = 2; g.strokeStyle = 'rgba(255,255,255,.75)'; g.stroke();
                }
                g.fillStyle = (deg === 0 || deg === 3 || deg === 4 || deg === 7) ? '#12141a' : '#0d0f14';
                g.font = '700 ' + (deg === 0 ? 10 : 9) + 'px ui-sans-serif,system-ui,sans-serif';
                g.fillText(FB_NOTES[pc], cx, yy + 0.5);
            }
        }

        fbChords(icon, st, tuning);
    }

    // Diatonic chords of the selected key and scale, each with a small grid
    // showing one playable shape.
    function fbChords(icon, st, tuning) {
        var box = el(icon, 'fbchords');
        if (!box) return;
        var degs = FB_DEGREES[st.mode] || [];
        var html = '';
        for (var i = 0; i < degs.length; ++i) {
            var rootPc = fbPitchClass(st.key + degs[i][0]);
            html += '<div class="px-chord' + (degs[i][0] === 0 ? ' tonic' : '') + '">' +
                    '<div class="px-chord-n">' + FB_NOTES[rootPc] + FB_SUFFIX[degs[i][1]] + '</div>' +
                    '<div class="px-chord-r">' + degs[i][2] + '</div>' +
                    '<canvas data-ci="' + i + '" width="74" height="86"></canvas>' +
                    '</div>';
        }
        box.innerHTML = html;
        var cans = box.querySelectorAll('canvas');
        for (var k = 0; k < cans.length; ++k) {
            var idx = parseInt(cans[k].getAttribute('data-ci'), 10);
            fbChordShape(cans[k], fbPitchClass(st.key + degs[idx][0]), degs[idx][1], tuning);
        }
    }

    // Pick a shape: scan window positions and keep the one covering the most
    // strings with the root lowest. Not a chord dictionary -- enough to show
    // the player where the chord lives.
    function fbVoicing(rootPc, quality, tuning) {
        var want = FB_QUAL[quality].map(function (i) { return fbPitchClass(rootPc + i); });
        var best = null, bestScore = -1;
        for (var ws = 0; ws <= 12; ++ws) {
            var we = (ws === 0) ? 4 : ws + 3;
            var v = [], score = 0, lowest = -1;
            for (var si = 0; si < tuning.length; ++si) {
                var put = null;
                for (var fr = ws; fr <= we; ++fr) {
                    if (want.indexOf(fbPitchClass(tuning[si] + fr)) >= 0) { put = fr; break; }
                }
                v.push(put);
                if (put !== null) {
                    score += 1;
                    if (lowest < 0) lowest = fbPitchClass(tuning[si] + put);
                }
            }
            if (lowest === rootPc) score += 2;      // root in the bass reads as the chord
            if (score > bestScore) { bestScore = score; best = v; }
        }
        return best || tuning.map(function () { return null; });
    }

    function fbChordShape(canvas, rootPc, quality, tuning) {
        var g = canvas.getContext('2d');
        if (!g) return;
        var v = fbVoicing(rootPc, quality, tuning);
        var played = v.filter(function (x) { return x !== null && x > 0; });
        var minF = played.length ? Math.min.apply(null, played) : 1;
        var base = Math.max(1, minF - (played.length ? 0 : 0));
        if (played.length && Math.max.apply(null, played) - base > 3) base = minF;

        var W = canvas.width, H = canvas.height;
        var n = tuning.length;
        var padL = 9, padR = 9, padT = 14, padB = 10;
        var colW = (W - padL - padR) / Math.max(1, n - 1);
        var rows = 4, rowH = (H - padT - padB) / rows;

        g.clearRect(0, 0, W, H);
        g.strokeStyle = 'rgba(255,255,255,.22)';
        g.lineWidth = 1;
        for (var i = 0; i < n; ++i) {
            var x = Math.round(padL + colW * i) + 0.5;
            g.beginPath(); g.moveTo(x, padT); g.lineTo(x, padT + rows * rowH); g.stroke();
        }
        for (var r = 0; r <= rows; ++r) {
            var y = Math.round(padT + rowH * r) + 0.5;
            g.lineWidth = (r === 0 && base === 1) ? 2.5 : 1;
            g.strokeStyle = (r === 0 && base === 1) ? 'rgba(255,255,255,.6)' : 'rgba(255,255,255,.18)';
            g.beginPath(); g.moveTo(padL, y); g.lineTo(padL + colW * (n - 1), y); g.stroke();
        }
        if (base > 1) {
            g.fillStyle = 'rgba(255,255,255,.45)';
            g.font = '700 8px ui-sans-serif,system-ui,sans-serif';
            g.textAlign = 'left'; g.textBaseline = 'middle';
            g.fillText(String(base), 1, padT + rowH * 0.5);
        }

        g.textAlign = 'center'; g.textBaseline = 'middle';
        for (var si2 = 0; si2 < n; ++si2) {
            var cx = padL + colW * si2;
            var f = v[si2];
            if (f === null) {
                g.strokeStyle = 'rgba(255,255,255,.3)'; g.lineWidth = 1.4;
                g.beginPath();
                g.moveTo(cx - 3, padT - 9); g.lineTo(cx + 3, padT - 3);
                g.moveTo(cx + 3, padT - 9); g.lineTo(cx - 3, padT - 3);
                g.stroke();
                continue;
            }
            if (f === 0) {
                g.strokeStyle = 'rgba(255,255,255,.55)'; g.lineWidth = 1.2;
                g.beginPath(); g.arc(cx, padT - 6, 3, 0, Math.PI * 2); g.stroke();
                continue;
            }
            var row = f - base;
            if (row < 0 || row >= rows) continue;
            var cy = padT + rowH * (row + 0.5);
            var isRoot = fbPitchClass(tuning[si2] + f) === rootPc;
            g.beginPath(); g.arc(cx, cy, 6, 0, Math.PI * 2);
            g.fillStyle = isRoot ? '#ff5c8a' : '#e9edf5';
            g.fill();
        }
    }

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
        // fighting them for the same pixels. The CURRENT bar is shaded and
        // numbered: rules alone tell you a bar line went past, which is not the
        // same as knowing you are in bar 3 of 4 -- and that is what a player
        // counts. Numbers are drawn only when they have room to be legible.
        var curBar = icon.data('px_bar') || 0;
        if (bars > 0 && bars <= 64) {
            var bwPx = W / bars;
            if (curBar >= 1 && curBar <= bars) {
                g.fillStyle = 'rgba(255,255,255,.055)';
                g.fillRect(Math.round((curBar - 1) * bwPx), 0, Math.round(bwPx), H);
            }
            g.strokeStyle = 'rgba(255,255,255,.13)'; g.lineWidth = 1;
            for (var b = 1; b < bars; ++b) {
                var x = Math.round(bwPx * b) + 0.5;
                g.beginPath(); g.moveTo(x, 2); g.lineTo(x, H - 2); g.stroke();
            }
            if (bwPx >= 26) {
                g.font = '600 9px ui-sans-serif,system-ui,sans-serif';
                g.textBaseline = 'top';
                for (var bn = 1; bn <= bars; ++bn) {
                    g.fillStyle = (bn === curBar) ? 'rgba(255,255,255,.72)'
                                                  : 'rgba(255,255,255,.26)';
                    g.fillText(String(bn), Math.round((bn - 1) * bwPx) + 4, 3);
                }
            }
        }
        g.strokeStyle = 'rgba(255,255,255,.10)';
        g.beginPath(); g.moveTo(0, mid + 0.5); g.lineTo(W, mid + 0.5); g.stroke();

        R(icon, 'empty' + trk).css('display', (pk && pk.length) ? 'none' : '');
        if (pk && pk.length) {
            // The track's own colour, full strength when it is the armed one and
            // dimmed when it is not -- so "which track is this" and "which am I
            // about to record onto" are two different questions the lane answers
            // at once, without a label for either.
            var tcol = TRACK_COL[(trk - 1) % TRACK_COL.length];
            g.fillStyle = (icon.data('px_sel') === trk) ? tcol : hexFade(tcol, 0.42);
            var n = pk.length;
            for (var i = 0; i < W; ++i) {
                var v = pk[Math.min(n - 1, Math.floor(i * n / W))] / 255;
                var h = Math.max(1, v * (mid - 3));
                g.fillRect(i, mid - h, 1, h * 2);
            }
        }

        // Trim window. Drawn before the playhead so the playhead stays on top.
        var ti = icon.data('px_trimIn' + trk);
        var to = icon.data('px_trimOut' + trk);
        if (ti === undefined) ti = 0;
        if (to === undefined) to = 1;
        if (pk && pk.length && (ti > 0.0005 || to < 0.9995)) {
            // Shade what has been trimmed away rather than hiding it: you need
            // to see what you are about to bring back.
            g.fillStyle = 'rgba(5,6,9,.72)';
            if (ti > 0)  g.fillRect(0, 0, Math.round(W * ti), H);
            if (to < 1)  g.fillRect(Math.round(W * to), 0, W - Math.round(W * to), H);
        }
        if (pk && pk.length) {
            // Handles. Always drawn, so the loop advertises that it can be
            // trimmed at all -- a hidden affordance is one nobody finds.
            var sel = icon.data('px_sel') === trk;
            [[ti, 1], [to, -1]].forEach(function (h) {
                var hx = Math.round(W * h[0]);
                hx = Math.max(1, Math.min(W - 2, hx));
                g.fillStyle = sel ? '#f0a830' : 'rgba(200,205,215,.55)';
                g.fillRect(hx - 1, 0, 3, H);
                // A grip tab on the inside edge, the way a DAW marks a region
                // boundary you can drag.
                g.beginPath();
                g.moveTo(hx + (h[1] > 0 ? 1 : -1), 0);
                g.lineTo(hx + h[1] * 9, 0);
                g.lineTo(hx + (h[1] > 0 ? 1 : -1), 11);
                g.closePath();
                g.fill();
                g.beginPath();
                g.moveTo(hx + (h[1] > 0 ? 1 : -1), H);
                g.lineTo(hx + h[1] * 9, H);
                g.lineTo(hx + (h[1] > 0 ? 1 : -1), H - 11);
                g.closePath();
                g.fill();
            });
        }

        // Playhead. Only meaningful while something is actually playing back.
        var prog = icon.data('px_prog');
        var st = icon.data('px_st' + trk) || 0;
        if (prog >= 0 && prog <= 1 && st >= 1 && st <= 3 && pk && pk.length) {
            var px = Math.round(W * prog);
            // Shade what has already played. A bare hairline is easy to lose
            // against a busy waveform; a moving edge is not.
            g.fillStyle = 'rgba(0,0,0,.34)';
            g.fillRect(0, 0, px, H);
            // The line itself, in the accent with a glow so it reads at the
            // 50% zoom the pedalboard draws blocks at.
            g.fillStyle = 'rgba(255,255,255,.30)';
            g.fillRect(px - 2, 0, 5, H);
            g.fillStyle = '#ffffff';
            g.fillRect(px, 0, 2, H);
            // A tab at the top edge, the way a DAW marks the position.
            g.fillStyle = '#ffffff';
            g.beginPath();
            g.moveTo(px - 4, 0); g.lineTo(px + 6, 0); g.lineTo(px + 1, 7);
            g.closePath(); g.fill();
        }
    }
    function drawAllWaves(icon) { for (var t = 1; t <= 4; ++t) drawWave(icon, t); }

    // Mix a hex colour toward the panel background. Canvas has no notion of
    // opacity on a fill style without rgba, and the lanes are drawn over a known
    // dark ground, so blending is cheaper and more predictable than alpha.
    function hexFade(hex, k) {
        var r = parseInt(hex.substr(1, 2), 16), gg = parseInt(hex.substr(3, 2), 16),
            b = parseInt(hex.substr(5, 2), 16);
        r  = Math.round(r  * k + 10 * (1 - k));
        gg = Math.round(gg * k + 11 * (1 - k));
        b  = Math.round(b  * k + 14 * (1 - k));
        return 'rgb(' + r + ',' + gg + ',' + b + ')';
    }

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
        // The whole lane says it is recording, not just a 10px lamp.
        icon.find('.px-lane[data-trk="' + trk + '"]')
            .toggleClass('recording', st === 1 || st === 2);
        // The Rec and Play buttons light for the SELECTED track only: they are
        // per-track commands, so a lit button has to mean "this track".
        if (icon.data('px_sel') === trk) {
            R(icon, 'btnrec').toggleClass('armed', st === 1 || st === 2);
            R(icon, 'btnplay').toggleClass('armed', st === 3);
        }
        drawWave(icon, trk);
        // A lane that has just started recording has to switch its readout
        // from "BAR 3/4" to a count-up in the same instant the lamp changes.
        laneBars(icon);
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

            g.fillStyle = hexFade((FAMILY[ROWS[r].f] || FAMILY.snare)[2], 0.80);
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
                // Two dimensions, both doing work: the bar's HEIGHT is how hard
                // the hit is, its HUE is which part of the kit it belongs to.
                // That makes the grid read as a drum kit grouped the way it
                // sounds, rather than a spreadsheet of identical cells.
                var fam = FAMILY[ROWS[r].f] || FAMILY.snare;
                var lvl = HIT_LEVEL[ch];
                var full = CELL_H - 6;
                var h = Math.max(3, Math.round(full * HIT_H[ch]));
                var top = y + 3 + (full - h);
                g.fillStyle = fam[lvl];
                g.fillRect(x, top, G.cw, h);
                // A brighter cap reads as the transient and keeps narrow cells
                // visible when the bar itself is only a few pixels wide.
                g.fillStyle = fam[Math.min(3, lvl + 1)];
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
        if (typeof d.tb === 'number' && d.tb !== icon.data('px_tbars')) {
            icon.data('px_tbars', d.tb);
            laneBars(icon);
        }
        if (typeof d.bar === 'number' && d.bar !== icon.data('px_bar')) {
            icon.data('px_bar', d.bar);
            laneBars(icon);
            // The shaded bar lives on the waveform, so the lanes have to be
            // repainted when it moves -- once per bar, not once per block.
            for (var w = 1; w <= 4; ++w) drawWave(icon, w);
        }
        if (typeof d.step === 'number') portApply(icon, 'out_step', d.step);
        if (typeof d.undo === 'number') portApply(icon, 'out_undo_avail', d.undo);
        // 'tr' is whether the transport is MOVING, which Play and Record start
        // on their own. It is not the Run switch -- that is the drummer's
        // on/off and the pill already follows its port.
        if (typeof d.tr === 'number') icon.data('px_running', d.tr > 0.5);
        if (typeof d.ci === 'number') portApply(icon, 'out_countin', d.ci);
    }

    // Per-lane bar readout. A track that is RECORDING counts up ("BAR 3"),
    // because a take still defining the loop has no total to count against;
    // everything else reads "BAR 3/4". Empty lanes say nothing rather than
    // showing a count that belongs to somebody else's take.
    function laneBars(icon) {
        var bar   = icon.data('px_bar')  || 0;
        var total = icon.data('px_bars') || 0;
        for (var t = 1; t <= 4; ++t) {
            var st  = icon.data('px_st' + t) || 0;
            var out = R(icon, 'bar' + t);
            if (!out.length) continue;
            var recording = (st === 1 || st === 2);
            if (!bar || st === 0) { out.text(''); out.toggleClass('rec', false); continue; }
            // While RECORDING the loop has no length yet, so count against the
            // length the take is heading for. "Bar 2" on its own says nothing
            // about when the take will close, which is the one thing a player
            // laying down a loop is waiting to know.
            var tgt = recording ? (icon.data('px_tbars') || 0) : total;
            out.text(tgt > 0 ? ('BAR ' + bar + '/' + tgt) : ('BAR ' + bar));
            out.toggleClass('rec', recording);
        }
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

    // ── trim handles ─────────────────────────────────────────────────────────
    // Non-destructive: these only move the window the plugin plays through, so
    // dragging a handle back brings the audio with it. Snapped to bar lines by
    // default because the loop is bar-locked and anything else fights the
    // drums; hold Shift for a free drag when you really do want to cut into a
    // bar (chopping off a count-in, say).
    // Grab zone in SCREEN pixels, not canvas pixels. The pedalboard draws the
    // block at 50%, so an 11-canvas-pixel zone was about five pixels under the
    // cursor -- which is most of why trimming felt like it did not work.
    var kTrimGrabPx = 16;

    function trimOf(icon, trk) {
        var i = icon.data('px_trimIn' + trk), o = icon.data('px_trimOut' + trk);
        return { in: (i === undefined ? 0 : i), out: (o === undefined ? 1 : o) };
    }
    function trimSnap(icon, frac, free) {
        if (free) return clamp(frac, 0, 1);
        var bars = icon.data('px_bars') || 0;
        var W = icon.data('px_wave');
        if (!bars && W) bars = W.bars || 0;
        if (!bars || bars < 1) return clamp(frac, 0, 1);
        // Snap to the BEAT, not the bar. Bars were too blunt for the job people
        // actually use this for -- shaving a count-in or a dead bar off the
        // front -- and on a two-bar loop they offered only three places to put
        // a handle. Shift still gives a free drag.
        var div = bars * (icon.data('px_bpb') || 4);
        return clamp(Math.round(frac * div) / div, 0, 1);
    }
    function trimWrite(icon, trk, t) {
        icon.data('px_trimIn' + trk, t.in);
        icon.data('px_trimOut' + trk, t.out);
        setPort(icon, 'trk' + trk + '_trim_in',  t.in);
        setPort(icon, 'trk' + trk + '_trim_out', t.out);
        drawWave(icon, trk);
    }
    function trimHit(icon, trk, c, ev) {
        // Measured against the element as DRAWN, so the zone is the same size
        // under the cursor whatever zoom the pedalboard is at.
        var r = c.getBoundingClientRect();
        if (r.width <= 0) return null;
        var x = ev.clientX - r.left;
        var t = trimOf(icon, trk);
        var dIn  = Math.abs(x - r.width * t.in);
        var dOut = Math.abs(x - r.width * t.out);
        if (dIn <= kTrimGrabPx && dIn <= dOut) return 'in';
        if (dOut <= kTrimGrabPx) return 'out';
        return null;
    }
    // Play from a point in the loop. Goes over the atom channel as a float
    // property: the control ports were appended only this morning and each
    // append is a port-count change every saved pedalboard notices, while this
    // channel is already open and already carries the waveforms.
    function seekTo(icon, frac) {
        if (!funcs || typeof funcs.patch_set !== 'function') return;
        funcs.patch_set(SEEK_URI, 'f', frac);
        // Move the drawn playhead immediately rather than waiting for the next
        // status push, so the click feels like it did something.
        icon.data('px_seekPending', frac);
        portApply(icon, 'out_progress', frac);
    }

    function bindTrim(icon) {
        var drag = null;   // {trk, edge}
        for (var t = 1; t <= 4; ++t) (function (trk) {
            var c = el(icon, 'wave' + trk);
            if (!c) return;
            $(c).on('mousedown', function (ev) {
                var edge = trimHit(icon, trk, c, ev);
                if (!edge) {
                    // Not a trim handle, so it is a position: play from here.
                    // Only meaningful once there IS a loop -- before that the
                    // click just arms the lane, as it always did.
                    if (icon.data('px_bars') > 0) {
                        var rr = c.getBoundingClientRect();
                        seekTo(icon, clamp((ev.clientX - rr.left) / rr.width, 0, 0.9999));
                    }
                    return;
                }
                drag = { trk: trk, edge: edge };
                ev.preventDefault();
                ev.stopPropagation();
            });
            // Show the handle is grabbable before it is grabbed.
            $(c).on('mousemove', function (ev) {
                if (drag) return;
                c.style.cursor = trimHit(icon, trk, c, ev) ? 'ew-resize' : 'pointer';
            });
        })(t);

        $(document).on('mousemove', function (ev) {
            if (!drag) return;
            var c = el(icon, 'wave' + drag.trk);
            if (!c) return;
            var r = c.getBoundingClientRect();
            var frac = trimSnap(icon, (ev.clientX - r.left) / r.width, ev.shiftKey);
            var t = trimOf(icon, drag.trk);
            // Never let the edges cross or meet: a zero-width window is a track
            // that has silently disappeared with no way to see why.
            var minGap = 0.02;
            if (drag.edge === 'in') t.in = Math.min(frac, t.out - minGap);
            else                    t.out = Math.max(frac, t.in + minGap);
            t.in = clamp(t.in, 0, 1); t.out = clamp(t.out, 0, 1);
            trimWrite(icon, drag.trk, t);
            ev.preventDefault();
        });
        $(document).on('mouseup', function () { drag = null; });
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

        // The Scales tab is self-contained: no ports, no plugin state, so it is
        // built once here and only redrawn when something on it is clicked.
        fbBuildControls(icon);
        fbDraw(icon);

        R(icon, 'pxtabs').find('.px-tab').each(function () {
            var self = this;
            $(self).on('click', function () {
                var name = self.getAttribute('data-pxtab');
                R(icon, 'pxview').attr('data-tab', name);
                R(icon, 'pxtabs').find('.px-tab').each(function () {
                    this.classList.toggle('on', this.getAttribute('data-pxtab') === name);
                });
                // Canvases laid out while hidden measure zero, so repaint on show.
                if (name === 'loops') drawAllWaves(icon);
                else if (name === 'drums') drawGrid(icon);
                else if (name === 'scales') fbDraw(icon);
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
        R(icon, 'monopill').on('click', function () {
            var on = !$(this).hasClass('on');
            $(this).toggleClass('on', on);
            setPort(icon, 'mono_sum', on ? 1 : 0);
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
        bindTrim(icon);
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
        if (sym === 'mono_sum')  { R(icon, 'monopill').toggleClass('on', value > 0.5); return; }

        if (sym === 'beats_per_bar') { icon.data('px_bpb', Math.max(1, Math.round(value))); return; }

        if (sym === 'out_countin') {
            var beats = Math.round(value);
            // Show the WHOLE count. This used to hide everything until the
            // final bar, so a press mid-bar left the panel blank for up to
            // three beats -- the "count-in takes forever to show up" that the
            // scheduling fix alone did not cure.
            var on = beats > 0;
            // The moment the count ends is the moment to come in, so say so
            // rather than just vanishing.
            // PLAY is shown whenever a count that WAS running ends. Keying it
            // on the last number still being on screen missed it whenever the
            // final beat and the take landed close enough together, which is
            // most of the time -- the cue the player is actually waiting for
            // was the one thing that did not reliably appear.
            if (on) icon.data('px_wascount', 1);
            if (!on && icon.data('px_wascount')) {
                icon.data('px_wascount', 0);
                icon.data('px_cibeat', 0);
                var card = R(icon, 'cinum');
                card.text('PLAY');
                R(icon, 'countin').toggleClass('on', true).toggleClass('go', true);
                R(icon, 'btnrec').toggleClass('counting', false);
                clearTimeout(icon.data('px_citimer') || 0);
                icon.data('px_citimer', setTimeout(function () {
                    R(icon, 'countin').toggleClass('on', false).toggleClass('go', false);
                }, 700));
                icon.data('px_citotal', 0);
                return;
            }
            // Kill any pending PLAY-cue hide. The cue hides itself 700 ms after
            // a count ends, and nothing used to cancel that: press record again
            // within those 700 ms -- which is exactly what you do when stacking
            // takes -- and the timer fired in the middle of the NEW count and
            // blanked it. That was the "count-in doesn't always show".
            if (on) {
                clearTimeout(icon.data('px_citimer') || 0);
                icon.data('px_citimer', 0);
            }
            R(icon, 'countin').toggleClass('go', false);
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
            }
            return;
        }

        if (sym === 'out_undo_avail') { R(icon, 'btnundo').toggleClass('dim', value < 0.5); return; }

        var m = /^out_trk([1-4])_state$/.exec(sym);
        if (m) { laneState(icon, parseInt(m[1], 10), Math.round(value)); return; }
        var mt = /^trk([1-4])_trim_(in|out)$/.exec(sym);
        if (mt) {
            var tk = parseInt(mt[1], 10);
            icon.data('px_trim' + (mt[2] === 'in' ? 'In' : 'Out') + tk, clamp(value, 0, 1));
            drawWave(icon, tk);
            return;
        }
        var mm = /^trk([1-4])_mute$/.exec(sym);
        if (mm) { R(icon, 'mute' + mm[1]).toggleClass('on', value > 0.5); return; }
    }
}
