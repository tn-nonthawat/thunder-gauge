"""Build the budget (Lite) dashboard from the full demo page.
usage: python mk_budget.py smart-gauge-demo.html <demo-out.html> <firmware-dir> [<app-dir>]
- <demo-out.html>: the Lite demo for claude.ai (budget flag on)
- <firmware-dir>/page_gz.h + gauge_budget_preview.html: the page the Lite board serves
- <app-dir> (default ../app): the Lite phone app, served over https (GitHub Pages): the same board page, but it
  talks to the board over Bluetooth and takes road speed from the phone's GPS (source/app_ble.js, firmware
  ble_link.h). Files: index.html, setup.html (the board's wizard), update.html (firmware over Bluetooth),
  manifest.webmanifest, sw.js (offline copy), icon-192.png, icon-512.png.
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
# speed-only parts (speed dial + GPS badge, timer tab) are marked SPD the same way: the Wi-Fi Lite page has no speed
# source and drops them; the phone app keeps them (road speed from the phone's GPS)
def strip_spd(t):
    t = re.sub(r'<!--SPD\{-->.*?<!--\}SPD-->\n?', '', t, flags=re.S)
    t = re.sub(r'/\*SPD\{\*/.*?/\*\}SPD(?::(.*?))?\*/', lambda m: m.group(1) or '', t, flags=re.S)
    assert 'SPD{' not in t and '}SPD' not in t
    return t

open(demo_out, 'w', encoding='utf-8').write(strip_spd(strip_full(s)))

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
app_src = b                       # the phone app starts from the same font-less page (no internet on the road either)
b = b.replace(flag, flag + store + ui_sync, 1)

import ecu_board
b = strip_spd(strip_full(ecu_board.patch(b, lite=True)))
# Wi-Fi page: a card that points to the Bluetooth app (speed from the phone's GPS). The board has no internet, so the
# link opens only with mobile data or another Wi-Fi; the copy button falls back to execCommand (plain http page).
APP_URL = 'https://tn-nonthawat.github.io/thunder-gauge/app/'   # GitHub Pages of the public repo (the app/ folder)
# QR code of the app link (pip install qrcode). Without the package the card shows the link only.
try:
    import qrcode
    _q = qrcode.QRCode(border=2, error_correction=qrcode.constants.ERROR_CORRECT_M)
    _q.add_data(APP_URL); _q.make(fit=True)
    QR = _q.get_matrix()
except ImportError:
    QR = None
    print('note: no qrcode package, the Wi-Fi page shows the app link without a QR code')
def qr_svg(m):
    n = len(m)
    d = ''.join('M%d %dh1v1h-1z' % (x, y) for y in range(n) for x in range(n) if m[y][x])
    return ('<svg viewBox="0 0 %d %d" width="168" height="168" role="img" aria-label="QR" shape-rendering="crispEdges" '
            'style="display:block;margin:6px auto 2px;background:#fff;border-radius:8px"><path d="%s" fill="#000"/></svg>' % (n, n, d))
appcard = ('    <div class="card" id="app-card">\n'
           '      <h2>แอปบลูทูธ + ความเร็ว GPS</h2>\n'
           '      <p class="note">ใช้มือถือ Android กับ Chrome: แอปต่อเกจทางบลูทูธ และอ่านความเร็วจาก GPS ของมือถือ (ECU ไม่ส่งความเร็ว) เปิดลิงก์ตอนมีอินเทอร์เน็ต แล้วกด "เพิ่มลงหน้าจอหลัก" หลังจากนั้นแอปใช้ได้แม้ไม่มีเน็ต · ครั้งแรกไม่ต้องใส่ PIN (ถ้าตั้ง PIN ไว้แล้ว มือถือจะถามครั้งเดียว)</p>\n'
           + ('      ' + qr_svg(QR) + '\n' if QR else '') +
           '      <p class="note" style="text-align:center"><code id="app-url">' + APP_URL + '</code></p>\n'
           '      <div class="acts"><a class="btn go" href="' + APP_URL + '" target="_blank" rel="noopener">เปิดแอป</a>'
           '<button class="btn" type="button" id="app-copy">คัดลอกลิงก์</button></div>\n'
           '    </div>\n')
assert b.count('    <div class="card" id="setup-card">') == 1
b = b.replace('    <div class="card" id="setup-card">', appcard + '\n    <div class="card" id="setup-card">', 1)
b += ('\n<script>document.getElementById("app-copy").addEventListener("click",function(){var u=document.getElementById("app-url").textContent,b=this,'
      'ok=function(){b.textContent=document.documentElement.lang==="en"?"Copied":"คัดลอกแล้ว";};'
      'if(navigator.clipboard&&window.isSecureContext)navigator.clipboard.writeText(u).then(ok);'
      'else{var t=document.createElement("textarea");t.value=u;document.body.appendChild(t);t.select();try{document.execCommand("copy");ok();}catch(e){}t.remove();}});</script>')
# reminder while the board has no Wi-Fi password and/or Bluetooth PIN (a new board connects with neither)
b += ('\n<script>fetch("/api/setup/look",{cache:"no-store"}).then(function(r){return r.json();}).then(function(j){'
      'if(!j.done||!(j.nopass||j.nopin))return;var en=document.documentElement.lang==="en",'
      'what=j.nopass&&j.nopin?(en?"a Wi-Fi password and a Bluetooth PIN":"รหัส Wi-Fi และ PIN บลูทูธ"):j.nopass?(en?"a Wi-Fi password":"รหัส Wi-Fi"):(en?"a Bluetooth PIN":"PIN บลูทูธ"),'
      'd=document.createElement("div");d.setAttribute("role","alert");'
      'd.style.cssText="margin:10px 0;padding:10px 14px;border:1px solid var(--warn);border-radius:14px;background:var(--panel);font-size:13px;line-height:1.4";'
      'd.innerHTML="<b>"+(en?"Lock your gauge":"ตั้งรหัสของเกจ")+"</b><br>"+(en?"The gauge has no "+what+" yet, so anyone nearby can connect. ":"เกจยังไม่มี"+what+" คนแถวนั้นต่อเกจได้ ")'
      '+"<a href=\\"/setup\\" style=\\"color:var(--accent);font-weight:600\\">"+(en?"Set it":"ตั้งรหัส")+"</a>";'
      'var h=document.querySelector("header");if(h)h.after(d);}).catch(function(){});</script>')
assert b.count('"ภาษา": "Language", ') == 1
b = b.replace('"ภาษา": "Language", ', '"ภาษา": "Language", "แอปบลูทูธ + ความเร็ว GPS": "Bluetooth app + GPS speed", '
              '"ใช้มือถือ Android กับ Chrome: แอปต่อเกจทางบลูทูธ และอ่านความเร็วจาก GPS ของมือถือ (ECU ไม่ส่งความเร็ว) เปิดลิงก์ตอนมีอินเทอร์เน็ต แล้วกด \\"เพิ่มลงหน้าจอหลัก\\" หลังจากนั้นแอปใช้ได้แม้ไม่มีเน็ต · ครั้งแรกไม่ต้องใส่ PIN (ถ้าตั้ง PIN ไว้แล้ว มือถือจะถามครั้งเดียว)": '
              '"Android phone with Chrome: the app reaches the gauge over Bluetooth and reads road speed from the phone\'s GPS (the ECU sends none). Open the link while you have internet and tap \\"Add to home screen\\"; after that it works without internet · No PIN the first time (once a PIN is set, the phone asks for it once)", '
              '"เปิดแอป": "Open the app", "คัดลอกลิงก์": "Copy link", ', 1)
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

# ---------------------------------------------------------------- phone app (Bluetooth + phone GPS)
import hashlib, json, shutil
here = os.path.dirname(os.path.abspath(__file__))
appdir = sys.argv[4] if len(sys.argv) > 4 else os.path.join(here, '..', 'app')
os.makedirs(appdir, exist_ok=True)
fwdir = os.path.abspath(outdir)
a = app_src
aflag = '<script>window.TG_BUDGET = true; window.TG_GPS = true; window.TG_APP = true;</script>\n'
appjs = open(os.path.join(here, 'app_ble.js'), encoding='utf-8').read()
import time
appjs = appjs.replace('__APPVER__', time.strftime('%Y-%m-%d %H:%M'))   # shown on the app's Bluetooth card: which build a phone runs
a = a.replace(flag, aflag + store + '<script>\n' + appjs + '\n</script>\n', 1)
a = strip_full(ecu_board.patch(a, lite=True))      # SPD parts stay: speed dial, GPS badge, timer tab

def arep(old, new, n=1):
    global a
    assert a.count(old) == n, (old[:70], a.count(old))
    a = a.replace(old, new)
# timing tab: speed from the phone's GPS
arep('$("race-note").textContent = "กล่อง ECU ของ Wave 110i ไม่ส่งความเร็วรถ (ทดสอบขี่จริงแล้ว) การจับเวลาจึงใช้ไม่ได้กับรถรุ่นนี้ ต้องใช้รุ่นที่มี GPS";',
     '$("race-note").textContent = "จับเวลาจาก GPS ของมือถือ (ราว 1 ครั้งต่อวินาที) แม่นน้อยกว่าโมดูล GPS ใช้เทียบกับรถตัวเองได้ · ควรวิ่งในสนามหรือพื้นที่ปิดเท่านั้น";')
arep('(en ? "GPS not connected — lap timing needs the GPS build" : "ไม่ได้ต่อ GPS — จับเวลาไม่ได้ (ต้องใช้รุ่นที่มี GPS)")',
     '(en ? "No GPS from the phone yet — Settings → Bluetooth · phone GPS → Turn on GPS" : "ยังไม่มี GPS จากมือถือ — ไปที่ตั้งค่า → บลูทูธ · GPS มือถือ → เปิด GPS")')
arep('(en ? "GPS not connected" : "ไม่ได้ต่อ GPS")', '(en ? "No phone GPS" : "ยังไม่มี GPS มือถือ")')
# firmware update and setup wizard: over Bluetooth, in a frame over the gauge (keeps the Bluetooth link)
ota_th = 'อัปโปรแกรมใหม่ลงกล่องผ่าน Wi-Fi ได้เลย ไม่ต้องถอดกล่องหรือต่อสาย · ดาวน์โหลดไฟล์ .bin ล่าสุดจาก GitHub (Releases) ลงมือถือก่อน แล้วกดปุ่มด้านล่าง · เฟิร์มแวร์ที่ใช้อยู่:'
ota_en = 'Load new firmware onto the box over Wi-Fi, no need to remove it or run a cable · Download the latest .bin from GitHub (Releases) to your phone first, then tap the button below · Current firmware:'
arep(ota_th, ota_th.replace('ผ่าน Wi-Fi', 'ผ่านบลูทูธ'), 2)
arep(ota_en, ota_en.replace('over Wi-Fi', 'over Bluetooth'))
arep('<a class="btn go" href="/update">', '<a class="btn go" href="update.html" data-ov>')
arep('<a class="btn go" href="/setup">', '<a class="btn go" href="setup.html" data-ov>')
# system tab connection note
arep('<p class="note">บอร์ดปล่อย Wi-Fi ชื่อ <code>ThunderGauge-Lite</code> มือถือต่อแล้วเปิด <code>http://192.168.4.1</code></p>',
     '<p class="note">แอปนี้ต่อบอร์ดทางบลูทูธ ดูสถานะและเปิด Wi-Fi ของบอร์ดได้ที่ตั้งค่า → บลูทูธ · GPS มือถือ</p>')
arep('"ภาษา": "Language", ', '"ภาษา": "Language", '
     '"จับเวลาจาก GPS ของมือถือ (ราว 1 ครั้งต่อวินาที) แม่นน้อยกว่าโมดูล GPS ใช้เทียบกับรถตัวเองได้ · ควรวิ่งในสนามหรือพื้นที่ปิดเท่านั้น": "Timed from the phone\'s GPS (about once a second): less exact than a GPS module, fine for comparing your own bike · Only on a track or closed area", '
     '"แอปนี้ต่อบอร์ดทางบลูทูธ ดูสถานะและเปิด Wi-Fi ของบอร์ดได้ที่ตั้งค่า → บลูทูธ · GPS มือถือ": "This app reaches the board over Bluetooth. Status and the board\'s Wi-Fi: Settings → Bluetooth · phone GPS", ')
head = ('<!doctype html><html lang="th"><head><meta charset="utf-8">'
        '<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">'
        '<meta name="theme-color" content="#0b0e14"><link rel="manifest" href="manifest.webmanifest">'
        '<link rel="icon" href="icon-192.png"><link rel="apple-touch-icon" href="icon-192.png">'
        # a script error shows on screen (and goes to the PC mock's log as /__err when tested from it): a phone browser
        # that stops on some JavaScript otherwise just shows an empty page
        '<script>window.onerror=function(m,u,l,c,e){var t=m+" @"+l+":"+c+(e&&e.stack?"\\n"+e.stack:"");'
        'try{var x=new XMLHttpRequest();x.open("POST","/__err");x.send(navigator.userAgent+"\\n"+t);}catch(_){}'
        'var d=document.getElementById("tg-err");if(!d&&document.body){d=document.createElement("pre");d.id="tg-err";'
        'd.style.cssText="position:fixed;left:0;right:0;bottom:0;z-index:2147483647;margin:0;padding:8px;max-height:45vh;overflow:auto;'
        'white-space:pre-wrap;font:11px monospace;background:#300;color:#fdd";document.body.appendChild(d);}if(d)d.textContent+=t+"\\n";};'
        'window.addEventListener("unhandledrejection",function(e){window.onerror("promise: "+(e.reason&&(e.reason.stack||e.reason.message)||e.reason),"",0,0,null);});'
        # tested from the PC mock (port 8769): report what the page looks like once loaded
        'if(location.port==="8769")window.addEventListener("load",function(){setTimeout(function(){try{var b=document.body,cs=getComputedStyle(b),x=new XMLHttpRequest();'
        'x.open("POST","/__err");x.send("STATUS "+navigator.userAgent+"\\nsize "+innerWidth+"x"+innerHeight+" body kids "+b.children.length+" text "+b.innerText.length'
        '+" bg "+cs.backgroundColor+" color "+cs.color+" display "+cs.display+" opacity "+cs.opacity+" scrollH "+document.documentElement.scrollHeight'
        '+"\\nble "+!!navigator.bluetooth+" geo "+!!navigator.geolocation+" tg "+(window.TGBLE&&TGBLE.state)+" view "+document.querySelector(".view:not([hidden])")+"\\n"+b.innerText.slice(0,200));}catch(e){}},2500);});</script>'
        '</head><body>\n')
swreg = ("<script>if ('serviceWorker' in navigator && !/[?&]mock/.test(location.search)) "
         "navigator.serviceWorker.register('sw.js').catch(function () {});</script>")
app_page = head + a + '\n' + swreg + '\n</body></html>\n'
open(os.path.join(appdir, 'index.html'), 'w', encoding='utf-8').write(app_page)

# setup wizard: the board's own page (setup_wizard.h), Lite parts, with its requests going over the gauge's link
wz = open(os.path.join(fwdir, 'setup_wizard.h'), encoding='utf-8').read().split('R"HTML(')[1:]
wz = [x.split(')HTML"')[0] for x in wz]
wiz = wz[0] + wz[2]                                 # wz[1] = the full build's GPS/MPU rows
bridge = '''<script>
(function () {   // app frame: the wizard's requests use the gauge page's Bluetooth link (source/app_ble.js)
  var p = null; try { p = window.parent !== window && window.parent.TGBLE ? window.parent : null; } catch (e) {}
  if (!p) { location.replace("./"); return; }
  window.fetch = function (u, o) { return p.fetch(u, o); };
  window.TGBACK = function (r) { p.TGOV.close(r); };
  document.addEventListener("DOMContentLoaded", function () {
    var b = document.createElement("button"); b.type = "button"; b.textContent = "✕"; b.setAttribute("aria-label", "close");
    b.style.cssText = "position:fixed;top:10px;right:10px;width:38px;height:38px;border-radius:50%;border:1px solid #2a3346;background:#141924;color:#e8edf6;font:18px system-ui;cursor:pointer;z-index:9";
    b.onclick = function () { p.TGOV.close(false); }; document.body.appendChild(b);
  });
})();
</script>
'''
assert wiz.count('<title>') == 1
wiz = wiz.replace('<title>', bridge + '<title>', 1)
n = wiz.count("location.href='/'")
assert n >= 2, n
wiz = wiz.replace("location.href='/'", "TGBACK(true)")
open(os.path.join(appdir, 'setup.html'), 'w', encoding='utf-8').write(wiz)
shutil.copyfile(os.path.join(here, 'app_update.html'), os.path.join(appdir, 'update.html'))

# icons (the wizard's bolt on the app gradient) and manifest
def icon(px):
    from PIL import Image, ImageDraw
    k = 4; S = px * k
    im = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    g = Image.new('RGBA', (S, S))
    gd = ImageDraw.Draw(g)
    for i in range(S):
        t = i / S; gd.line([(0, i), (S, i)], fill=(255, int(174 - 84 * t), int(66 - 35 * t), 255))
    m = Image.new('L', (S, S), 0); ImageDraw.Draw(m).rounded_rectangle([0, 0, S - 1, S - 1], radius=int(S * .22), fill=255)
    im.paste(g, (0, 0), m)
    sc = S / 180.0
    bolt = [(19, 1.5), (4, 20), (12.5, 20), (9, 34.5), (26, 14), (17.5, 14), (21, 1.5)]
    pts = [((36 + x * 3.6) * sc, (25 + y * 3.6) * sc) for x, y in bolt]
    ImageDraw.Draw(im).polygon(pts, fill=(255, 255, 255, 255))
    return im.resize((px, px), Image.LANCZOS)
for px in (192, 512):
    icon(px).save(os.path.join(appdir, 'icon-%d.png' % px))
if QR:                                              # app/qr.png: the app link for the README and printed instructions
    from PIL import Image
    n = len(QR); k = 10
    im = Image.new('L', (n * k, n * k), 255)
    for y in range(n):
        for x in range(n):
            if QR[y][x]: im.paste(0, (x * k, y * k, x * k + k, y * k + k))
    im.save(os.path.join(appdir, 'qr.png'))
manifest = {
    'name': 'Thunder Gauge', 'short_name': 'Thunder Gauge', 'lang': 'th',
    'description': 'Smart gauge for the Honda Wave 110i: ECU data over Bluetooth, speed from the phone GPS',
    'start_url': './', 'scope': './', 'display': 'standalone', 'orientation': 'any',
    'background_color': '#0b0e14', 'theme_color': '#0b0e14',
    'icons': [{'src': 'icon-192.png', 'sizes': '192x192', 'type': 'image/png', 'purpose': 'any'},
              {'src': 'icon-512.png', 'sizes': '512x512', 'type': 'image/png', 'purpose': 'any'}]}
open(os.path.join(appdir, 'manifest.webmanifest'), 'w', encoding='utf-8').write(json.dumps(manifest, ensure_ascii=False, indent=1))
# service worker: the app works with no internet (on the road) from its offline copy; with internet it loads the
# newest version (3 s timeout, then the copy)
files = ['index.html', 'setup.html', 'update.html', 'manifest.webmanifest', 'icon-192.png', 'icon-512.png']
h = hashlib.sha1()
for f in files: h.update(open(os.path.join(appdir, f), 'rb').read())
sw = '''// Generated by source/mk_budget.py. Offline copy of the Thunder Gauge app.
const V = "tg-%s";
const FILES = ["./", %s];
self.addEventListener("install", e => { e.waitUntil(caches.open(V).then(c => c.addAll(FILES)).then(() => self.skipWaiting())); });
self.addEventListener("activate", e => { e.waitUntil(caches.keys().then(ks => Promise.all(ks.filter(k => k !== V).map(k => caches.delete(k)))).then(() => self.clients.claim())); });
self.addEventListener("fetch", e => {
  const r = e.request, u = new URL(r.url);
  if (r.method !== "GET" || u.origin !== location.origin) return;
  // newest version first (3 s to answer), the offline copy when there is no internet (on the road)
  e.respondWith(caches.open(V).then(c => new Promise(done => {
    let over = false;
    const fall = () => { if (over) return; over = true; c.match(r, { ignoreSearch: true }).then(hit => done(hit || fetch(r))); };
    const t = setTimeout(fall, 3000);
    fetch(r).then(res => { if (res.ok) c.put(r, res.clone()); if (!over) { over = true; clearTimeout(t); done(res); } }).catch(() => { clearTimeout(t); fall(); });
  })));
});
''' % (h.hexdigest()[:12], ', '.join('"%s"' % f for f in files))
open(os.path.join(appdir, 'sw.js'), 'w', encoding='utf-8').write(sw)
print('app', os.path.abspath(appdir), 'index', len(app_page), 'version', h.hexdigest()[:12])
