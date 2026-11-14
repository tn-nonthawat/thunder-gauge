"""Build the budget (Lite) dashboard from the full demo page.
usage: python mk_budget.py smart-gauge-demo.html <demo-out.html> <firmware-dir>
- <demo-out.html>: the Lite demo for claude.ai (budget flag on)
- <firmware-dir>/page_gz.h + gauge_budget_preview.html: the page the Lite board serves
The Lite board has no MPU-6050 and no GPS, so there is no live sensor feed yet; ECU values stay
simulated until the K-Line code exists. The AFR map is kept on the board through /map."""
import sys, gzip, re, os
src, demo_out, outdir = sys.argv[1], sys.argv[2], sys.argv[3]
s = open(src, encoding='utf-8').read()

def rep(old, new):
    global s
    assert s.count(old) == 1, (old[:60], s.count(old))
    s = s.replace(old, new)

rep('<title>Thunder Gauge</title>', '<title>Thunder Gauge Lite</title>')
rep('<code>ThunderGauge</code>', '<code>ThunderGauge-Lite</code>')
flag = '<script>window.TG_BUDGET = true;</script>\n'
rep('<script>\n(function () {\n', flag + '<script>\n(function () {\n')
# full-build-only parts (race timer tab, speed dial + GPS badge, lean / G card) are marked in the source:
# HTML <!--FULL{--> ... <!--}FULL-->, CSS/JS /*FULL{*/ ... /*}FULL*/ or /*}FULL:<stub code>*/. Lite drops them
# (JS blocks leave their stub functions). Stripped after ecu_board.patch, whose patches may touch these parts.
def strip_full(t):
    t = re.sub(r'<!--FULL\{-->.*?<!--\}FULL-->\n?', '', t, flags=re.S)
    t = re.sub(r'/\*FULL\{\*/.*?/\*\}FULL(?::(.*?))?\*/', lambda m: m.group(1) or '', t, flags=re.S)
    assert 'FULL{' not in t and '}FULL' not in t
    return t

open(demo_out, 'w', encoding='utf-8').write(strip_full(s))

# board copy: no internet on the box's Wi-Fi, so no Google Fonts; map kept on the board
b = re.sub(r'<link rel="(preconnect|stylesheet)" href="https://fonts\.(googleapis|gstatic)\.com[^>]*>\n?', '', s)
assert 'fonts.googleapis' not in b
store = '''<script>
window.MAP_STORE = {
  load: function () { return fetch('/map', { cache: 'no-store' }).then(function (r) { return r.ok ? r.json() : null; }).catch(function () { return null; }); },
  save: function (t) { return fetch('/map', { method: 'POST', headers: { 'Content-Type': 'text/plain' }, body: t }).catch(function () {}); }
};
</script>
'''
# Shared screen setup: every phone shows the same screen. The page's look/layout settings (the tg-* keys
# below) live on the board in /ui.json. At start the page loads them before its own script reads them
# (a synchronous request, a few KB over the board's Wi-Fi); a change on any phone is sent back ~1.5 s later;
# other phones check the version every 10 s and reload when it changed. Last change wins.
ui_sync = '''<script>
(function uiSync() {   // named: ecu_board.py anchors on the page's own anonymous one
  var KEYS = ["tg-app", "tg-cardcfg", "tg-cards", "tg-face", "tg-gearst", "tg-layout", "tg-looks", "tg-shift", "tg-side", "tg-sidest", "tg-slst", "tg-style", "tg-theme", "tg-tiles", "tg-lang", "tg-pump-tile"];
  var ls; try { ls = window.localStorage; ls.getItem("tg-ui-ver"); } catch (e) { return; }
  var P = Storage.prototype, setI = P.setItem, remI = P.removeItem, ver = +(ls.getItem("tg-ui-ver") || 0), timer = 0, mute = false, last = "";
  function snap() { var d = {}; KEYS.forEach(function (k) { var v = ls.getItem(k); if (v != null) d[k] = v; }); return d; }
  function apply(d) { last = JSON.stringify(snapOf(d)); mute = true; KEYS.forEach(function (k) { if (typeof d[k] === "string") setI.call(ls, k, d[k]); else remI.call(ls, k); }); mute = false; }
  function snapOf(d) { var o = {}; KEYS.forEach(function (k) { if (typeof d[k] === "string") o[k] = d[k]; }); return o; }
  function setVer(v) { ver = v; setI.call(ls, "tg-ui-ver", String(v)); }
  function push(retry) {
    timer = 0; var d = snap(), j = JSON.stringify(d); if (!Object.keys(d).length || (j === last && !retry)) return;   // only real changes go up
    var x = new XMLHttpRequest(); x.open("POST", "/ui?v=" + ver); x.setRequestHeader("Content-Type", "text/plain");
    x.onload = function () { var v = +(x.getResponseHeader("X-UI-Ver") || x.responseText || 0);
      if (x.status === 200) { setVer(v); last = j; } else if (x.status === 409 && !retry) { setVer(v); push(true); } };   // another phone saved first: ours goes on top
    x.send(j);
  }
  function later() { if (!mute) { clearTimeout(timer); timer = setTimeout(push, 1500); } }
  P.setItem = function (k, v) { setI.call(this, k, v); if (this === ls && KEYS.indexOf(k) >= 0) later(); };
  P.removeItem = function (k) { remI.call(this, k); if (this === ls && KEYS.indexOf(k) >= 0) later(); };
  try {                                   // load the board's copy before the page reads its settings
    var x = new XMLHttpRequest(); x.open("GET", "/ui", false); x.send();
    if (x.status === 200) { var bv = +(x.getResponseHeader("X-UI-Ver") || 0), d = JSON.parse(x.responseText);
      if (d && bv) { if (bv !== ver) { apply(d); setVer(bv); } else last = JSON.stringify(snapOf(d)); }   // same version: remember it, so rewriting the same values sends nothing
      else { setVer(0); setTimeout(push, 1500); } }   // nothing on the board yet: this phone's screen becomes the shared one
  } catch (e) {}
  setInterval(function () {               // another phone changed the screen: take it over
    if (timer || document.hidden) return;
    var x = new XMLHttpRequest(); x.open("GET", "/ui");
    x.onload = function () { var bv = +(x.getResponseHeader("X-UI-Ver") || 0);
      if (x.status === 200 && bv && bv !== ver && !timer) { try { var d = JSON.parse(x.responseText); if (d) { apply(d); setVer(bv); location.reload(); } } catch (e) {} } };
    var q = new XMLHttpRequest(); q.open("GET", "/ui?q=1");
    q.onload = function () { if (q.status === 200 && +q.responseText !== ver && +q.responseText) x.send(); };
    q.send();
  }, 10000);
})();
</script>
'''
b = b.replace(flag, flag + store + ui_sync, 1)

import ecu_board
b = strip_full(ecu_board.patch(b, lite=True))
page = ('<!doctype html><html lang="th"><head><meta charset="utf-8">'
        '<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">'
        '<meta name="theme-color" content="#0b0e14"></head><body>\n' + b + '\n</body></html>\n')
open(os.path.join(outdir, 'gauge_budget_preview.html'), 'w', encoding='utf-8').write(page)
gz = gzip.compress(page.encode('utf-8'), 9)
with open(os.path.join(outdir, 'page_gz.h'), 'w') as f:
    f.write('// Generated from the Thunder Gauge demo page (Lite build, gzip). Do not edit by hand.\n#pragma once\n#include <Arduino.h>\n')
    f.write(f'const size_t PAGE_GZ_LEN = {len(gz)};\nconst uint8_t PAGE_GZ[] PROGMEM = {{\n')
    for i in range(0, len(gz), 20):
        f.write('  ' + ','.join(str(x) for x in gz[i:i+20]) + ',\n')
    f.write('};\n')
print('demo', len(s), 'board page', len(page), 'gz', len(gz))
