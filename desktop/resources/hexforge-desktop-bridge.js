// ─────────────────────────────────────────────────────────────────────────────
// Hex Forge desktop — MOD modgui host shim (M2, hand-written).
//
// Reproduces enough of mod-ui's modgui runtime that the device's
// script-hexforge.js runs UNMODIFIED inside the JUCE webview:
//   * the widget layer: film-strip knobs/faders (default input-control-port),
//     switches, custom-select dropdowns, custom-select-path pickers (their
//     user-file list is replaced by a native Browse… dialog),
//     input-control-value readouts;
//   * the icon-script contract: iconFn(event, funcs) with 'start' (icon,
//     ports, parameters) and 'change' (symbol/value or uri/value) events,
//     funcs.set_port_value / funcs.patch_set;
//   * host echo: every port set is echoed back to the icon script as a
//     'change' event, matching mod-ui (the script is echo-hardened).
//
// Native transport (JUCE WebBrowserComponent, native integration on):
//   JS → C++ : window.__JUCE__.backend.emitEvent('hfMsg', {...})
//              {t:'ready'} {t:'set',sym,val} {t:'patch',uri,val}
//              {t:'paramset',uri,val} {t:'browse',uri}
//   C++ → JS : window.hfFromNative({...})
//              {t:'init',ports:[[sym,val]..],parameters:[[uri,path]..]}
//              {t:'port',sym,val}  {t:'param',uri,val}
// ─────────────────────────────────────────────────────────────────────────────
(function () {
'use strict';
var $ = window.jQuery;
var PORTS = window.HF_PORTS || {};
var icon = null;
var iconFn = null;
var values = {};                 // sym -> current value
var strips = [];                 // film-strip widgets: {el, sym, frames, ew}
var switchEls = {};              // sym -> [el]
var valueEls = {};               // sym -> [el] (readout spans)
var started = false;

function send(msg) {
    try {
        if (window.__JUCE__ && window.__JUCE__.backend)
            window.__JUCE__.backend.emitEvent('hfMsg', msg);
    } catch (e) {}
}

function cur(sym) {
    var v = values[sym];
    if (v !== undefined) return v;
    return PORTS[sym] ? PORTS[sym].df : 0;
}

// ── value readouts ────────────────────────────────────────────────────────────
function fmt(sym, v) {
    var m = PORTS[sym];
    if (!m) return '' + v;
    if (m.k === 't') return v > 0.5 ? 'ON' : 'OFF';
    if (m.sp)
        for (var i = 0; i < m.sp.length; ++i)
            if (Math.abs(m.sp[i][1] - v) < 0.001) return m.sp[i][0];
    if (m.k === 'i' || m.k === 'e') return '' + Math.round(v);
    var a = Math.abs(v);
    var s = a >= 100 ? v.toFixed(0) : a >= 10 ? v.toFixed(1) : v.toFixed(2);
    if (m.k === 'db') return s + ' dB';
    if (m.k === 'ms') return s + ' ms';
    if (m.k === 'hz') return s + ' Hz';
    return s;
}

// ── widget rendering ──────────────────────────────────────────────────────────
function stripRender(st) {
    if (!st.frames) return;
    var m = PORTS[st.sym];
    var norm = (cur(st.sym) - m.mn) / ((m.mx - m.mn) || 1);
    norm = Math.max(0, Math.min(1, norm));
    var idx = Math.round(norm * (st.frames - 1));
    st.el.style.backgroundPosition = (-idx * st.ew) + 'px 0px';
}
function renderSym(sym) {
    for (var i = 0; i < strips.length; ++i)
        if (strips[i].sym === sym) stripRender(strips[i]);
    (switchEls[sym] || []).forEach(function (el) {
        var on = cur(sym) > 0.5;
        el.classList.toggle('on', on);
        el.classList.toggle('off', !on);
    });
    (valueEls[sym] || []).forEach(function (el) { el.textContent = fmt(sym, cur(sym)); });
}
function renderAll() { Object.keys(PORTS).forEach(renderSym); }

// ── the two flows every value change funnels through ─────────────────────────
function fireChange(sym, val) {
    if (!started || !iconFn) return;
    try { iconFn({ type: 'change', icon: icon, symbol: sym, value: val }, funcs); }
    catch (e) { console.error('icon change handler failed for', sym, e); }
}
function fireParam(uri, val) {
    if (!started || !iconFn) return;
    try { iconFn({ type: 'change', icon: icon, uri: uri, value: val }, funcs); }
    catch (e) { console.error('icon param handler failed for', uri, e); }
}
// User / script initiated: update, tell native, echo back to the icon script.
function userSet(sym, val) {
    values[sym] = val;
    renderSym(sym);
    send({ t: 'set', sym: sym, val: val });
    setTimeout(function () { fireChange(sym, val); }, 0);   // host echo, async like mod-ui
}

var funcs = {
    set_port_value: function (sym, val) { userSet(sym, parseFloat(val)); },
    patch_set: function (uri, _type, val) { send({ t: 'patch', uri: uri, val: '' + val }); },
};

// ── widget wiring ─────────────────────────────────────────────────────────────
function wireStrip(el, sym) {
    var meta = PORTS[sym];
    if (!meta) return;
    var st = { el: el, sym: sym, frames: 0, ew: 0 };
    strips.push(st);
    var bg = getComputedStyle(el).backgroundImage;
    var mUrl = /url\(["']?([^"')]+)/.exec(bg || '');
    if (mUrl) {
        var img = new Image();
        img.onload = function () {
            var ew = el.offsetWidth || 40, eh = el.offsetHeight || 40;
            var frameNatW = img.naturalHeight * (ew / eh);   // frames share the element's aspect
            st.frames = Math.max(1, Math.round(img.naturalWidth / frameNatW));
            st.ew = ew;
            stripRender(st);
        };
        img.src = mUrl[1];
    }
    var range = function () { return (meta.mx - meta.mn) || 1; };
    var snap = function (v) {
        if (meta.k !== 'f' && meta.k !== 'db' && meta.k !== 'ms' && meta.k !== 'hz') v = Math.round(v);
        return Math.max(meta.mn, Math.min(meta.mx, v));
    };
    var dragging = false, startY = 0, startV = 0;
    el.style.cursor = 'ns-resize';
    el.style.touchAction = 'none';
    el.addEventListener('pointerdown', function (e) {
        dragging = true; startY = e.clientY; startV = cur(sym);
        el.setPointerCapture(e.pointerId);
        e.preventDefault(); e.stopPropagation();
    });
    el.addEventListener('pointermove', function (e) {
        if (!dragging) return;
        var v = snap(startV + (startY - e.clientY) * range() / (e.shiftKey ? 1600 : 160));
        if (v !== cur(sym)) userSet(sym, v);
        e.preventDefault();
    });
    el.addEventListener('pointerup', function () { dragging = false; });
    el.addEventListener('pointercancel', function () { dragging = false; });
    el.addEventListener('dblclick', function (e) {
        userSet(sym, meta.df); e.preventDefault(); e.stopPropagation();
    });
    el.addEventListener('wheel', function (e) {
        var fine = (meta.k === 'f' || meta.k === 'db' || meta.k === 'ms' || meta.k === 'hz');
        var step = fine ? range() / 50 : 1;
        userSet(sym, snap(cur(sym) + (e.deltaY < 0 ? step : -step)));
        e.preventDefault(); e.stopPropagation();
    }, { passive: false });
}

function wireSwitch(el, sym) {
    (switchEls[sym] = switchEls[sym] || []).push(el);
    var host = el.closest('.hf-sw') || el;   // whole labeled switch is the hit target
    host.style.cursor = 'pointer';
    host.addEventListener('click', function (e) {
        userSet(sym, cur(sym) > 0.5 ? 0 : 1);
        e.stopPropagation();
    });
}

var openList = null;
function closeLists() {
    if (openList) { openList.style.display = 'none'; openList = null; }
}
function wireSelect(el) {
    var sel = el.querySelector('.mod-enumerated-selected');
    var list = el.querySelector('.mod-enumerated-list');
    if (!sel || !list) return;
    sel.addEventListener('click', function (e) {
        var was = (openList === list);
        closeLists();
        if (!was) { list.style.display = 'block'; openList = list; }
        e.stopPropagation();
    });
    var isPath = el.getAttribute('mod-widget') === 'custom-select-path';
    var sym = el.getAttribute('mod-port-symbol');
    var uri = el.getAttribute('mod-parameter-uri');
    Array.prototype.forEach.call(el.querySelectorAll('[mod-role=enumeration-option]'), function (opt) {
        opt.addEventListener('click', function (e) {
            if (isPath) send({ t: 'paramset', uri: uri, val: opt.getAttribute('mod-parameter-value') || '' });
            else userSet(sym, parseFloat(opt.getAttribute('mod-port-value')));
            closeLists();
            e.stopPropagation();
        });
    });
    if (isPath) {   // native file dialog replaces mod-ui's server-side file list
        var browse = document.createElement('div');
        browse.textContent = 'Browse…';
        browse.style.borderTop = '1px solid rgba(255,255,255,.25)';
        browse.style.fontStyle = 'italic';
        browse.addEventListener('click', function (e) {
            send({ t: 'browse', uri: uri });
            closeLists();
            e.stopPropagation();
        });
        list.appendChild(browse);
    }
}

function wireWidgets(root) {
    root.querySelectorAll('[mod-role=input-control-port]').forEach(function (el) {
        var sym = el.getAttribute('mod-port-symbol');
        if (!sym) return;
        var w = el.getAttribute('mod-widget');
        if (w === 'switch') wireSwitch(el, sym);
        else if (w === 'custom-select') wireSelect(el);
        else wireStrip(el, sym);
    });
    root.querySelectorAll('[mod-role=input-parameter][mod-widget=custom-select-path]')
        .forEach(function (el) { wireSelect(el); });
    root.querySelectorAll('[mod-role=input-control-value]').forEach(function (el) {
        var sym = el.getAttribute('mod-port-symbol');
        if (sym) (valueEls[sym] = valueEls[sym] || []).push(el);
    });
    document.addEventListener('click', closeLists);
}

// ── native → JS ───────────────────────────────────────────────────────────────
window.hfFromNative = function (m) {
    if (!m || !m.t) return;
    if (m.t === 'init') {
        (m.ports || []).forEach(function (p) { values[p[0]] = p[1]; });
        renderAll();
        var portsArr = (m.ports || []).map(function (p) { return { symbol: p[0], value: p[1] }; });
        var paramsArr = (m.parameters || []).map(function (p) { return { uri: p[0], value: p[1] }; });
        if (!started) {
            started = true;
            try { iconFn({ type: 'start', icon: icon, ports: portsArr, parameters: paramsArr, data: {} }, funcs); }
            catch (e) { console.error('icon start failed', e); }
            send({ t: 'started' });
        }
    } else if (m.t === 'port') {
        values[m.sym] = m.val;
        renderSym(m.sym);
        fireChange(m.sym, m.val);
    } else if (m.t === 'param') {
        fireParam(m.uri, m.val);
    }
};

// ── boot ──────────────────────────────────────────────────────────────────────
function boot() {
    icon = $('.mod-pedal-guitaramp-hexforge').first();
    if (!icon.length) { console.error('hexforge icon root not found'); return; }
    wireWidgets(icon[0]);
    fetch('modgui/script-hexforge.js')
        .then(function (r) { return r.text(); })
        .then(function (src) {
            // The device script is an anonymous function EXPRESSION — same
            // eval mod-ui performs.
            iconFn = eval('(' + src + ')');   // eslint-disable-line no-eval
            send({ t: 'ready' });
        })
        .catch(function (e) { console.error('failed to load script-hexforge.js', e); });
}
if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', boot);
else boot();
})();
