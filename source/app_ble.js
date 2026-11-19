// Thunder Gauge Lite phone app: Bluetooth link to the board + the phone's GPS for road speed.
// Built into app/index.html by mk_budget.py (the app copy of the Lite page). Runs before the page's own scripts.
// Why an app page: the board's own page is plain http (192.168.4.1), where Chrome blocks GPS and Bluetooth.
// The app is served over https (GitHub Pages), so it may use both; it then talks to the board over Bluetooth (BLE).
//
// Every request the page makes to the board ("/api/live", "/ui", "/map", "/api/setup/save" ...) goes through
// window.fetch, which is replaced here: requests to a path on this site ("/...") travel over BLE to the board
// (firmware ble_link.h) and come back as a normal Response, so the page code is the same as on the board's Wi-Fi.
// "/api/live" is answered from the board's live notifications (no round trip).
// Testing on a PC: ?mock in the URL sends the requests over http to the server the app came from
// (source/tools/mock_board.py serves the app at /app/), and the GPS speed comes from the mock's simulated ride.
(function () {
  "use strict";
  var SVC = "7e9c0001-3f2a-4c55-9d1e-5467617567e1", C_LIVE = "7e9c0002-3f2a-4c55-9d1e-5467617567e1",
      C_REQ = "7e9c0003-3f2a-4c55-9d1e-5467617567e1", C_RES = "7e9c0004-3f2a-4c55-9d1e-5467617567e1",
      C_OTA = "7e9c0005-3f2a-4c55-9d1e-5467617567e1";
  var qs = new URLSearchParams(location.search);
  var MOCK = qs.has("mock");
  var realFetch = window.fetch.bind(window);
  var en = function () { return document.documentElement.lang === "en"; };
  var L = function (th, e) { return en() ? e : th; };

  var IOS = /iP(hone|ad|od)/.test(navigator.userAgent) || (navigator.platform === "MacIntel" && navigator.maxTouchPoints > 1);
  var BLUEFY = "https://apps.apple.com/app/id1492822055";   // Bluefy – Web BLE Browser (free): Web Bluetooth on iPhone
  var T = window.TGBLE = { state: "off", name: "", mock: MOCK, supported: !!(navigator.bluetooth || MOCK), live: null, liveAt: 0, err: "" };
  var subs = [];
  T.on = function (f) { subs.push(f); };
  function setState(s, err) { T.state = s; if (err !== undefined) T.err = err; subs.forEach(function (f) { try { f(s); } catch (e) {} }); }

  // ---------------------------------------------------------------- GATT plumbing
  var dev = null, ch = {}, gq = Promise.resolve(), retryT = 0, wantOn = false;
  function gatt(fn) { var p = gq.then(fn); gq = p.catch(function () {}); return p; }   // one GATT operation at a time
  function u8(a) { return new Uint8Array(a); }
  // write with response; older Web Bluetooth (Bluefy on iPhone) only has writeValue, which also waits for the answer here
  function wr(c, v) { return c.writeValueWithResponse ? c.writeValueWithResponse(v) : c.writeValue(v); }

  function onLive(e) {
    var dv = e.target.value, i = 0;
    if (dv.byteLength < 9 || dv.getUint8(0) !== 1) return;
    i = 1;
    var awake = !!dv.getUint8(i++), age = dv.getUint16(i, true); i += 2;
    var lt = dv.getUint8(i++), ot = dv.getUint8(i++);
    function rd() { var n = dv.getUint8(i++), a = []; for (var k = 0; k < n && i < dv.byteLength; k++) a.push(dv.getUint8(i++)); return a; }
    var d = rd(), o = rd(), s = rd();
    T.live = { awake: awake, age: age, lt: lt, ot: ot, d: d, o: o, s: s }; T.liveAt = Date.now();
  }

  // replies: [id, last, seq lo, seq hi, data...]
  var cur = null;          // the request waiting for its reply
  function onRes(e) {
    var dv = e.target.value;
    if (!cur || dv.byteLength < 4) return;
    var id = dv.getUint8(0), last = dv.getUint8(1), seq = dv.getUint16(2, true);
    if (id !== cur.id) return;
    if (seq === cur.next) {
      cur.parts.push(new Uint8Array(dv.buffer.slice(dv.byteOffset + 4, dv.byteOffset + dv.byteLength)));
      cur.next++;
      if (last) { ack(cur.id, cur.next); finish(); return; }
      if (cur.next % 8 === 0) ack(cur.id, cur.next);
    } else if (seq > cur.next && Date.now() - (cur.nakAt || 0) > 150) {   // a piece went missing: ask again from there
      cur.nakAt = Date.now(); ack(cur.id, cur.next);
    }
  }
  function ack(id, next) {
    if (!ch.req) return;
    gatt(function () { return wr(ch.req, u8([3, id, next & 255, next >> 8])); }).catch(function () {});
  }
  function finish() {
    var c = cur; cur = null; clearTimeout(c.timer);
    var n = 0; c.parts.forEach(function (p) { n += p.length; });
    var all = new Uint8Array(n), o = 0; c.parts.forEach(function (p) { all.set(p, o); o += p.length; });
    // raw http reply: status line, headers, blank line, body
    var sep = -1;
    for (var i = 0; i + 3 < all.length; i++) if (all[i] === 13 && all[i + 1] === 10 && all[i + 2] === 13 && all[i + 3] === 10) { sep = i; break; }
    var head = new TextDecoder().decode(sep < 0 ? all : all.subarray(0, sep)), body = sep < 0 ? new Uint8Array(0) : all.subarray(sep + 4);
    var lines = head.split("\r\n"), m = /^HTTP\/\d\.\d (\d+)/.exec(lines[0] || ""), st = m ? +m[1] : 502;
    var h = new Headers();
    lines.slice(1).forEach(function (l) { var k = l.indexOf(":"); if (k > 0) { try { h.append(l.slice(0, k).trim(), l.slice(k + 1).trim()); } catch (e) {} } });
    h.delete("content-length");
    c.resolve(new Response(st === 204 || st === 304 ? null : body, { status: st < 200 || st > 599 ? 502 : st, headers: h }));
    pump();
  }

  // ---------------------------------------------------------------- request queue
  var queue = [], rid = 0;
  function pump() {
    if (cur || !queue.length || T.state !== "on" || T.otaBusy) return;
    var r = queue.shift();
    rid = (rid + 1) & 255;
    cur = { id: rid, parts: [], next: 0, resolve: r.resolve, reject: r.reject };
    cur.timer = setTimeout(function () { if (cur && cur.id === rid) { var c = cur; cur = null; c.reject(new TypeError("Bluetooth request timed out")); pump(); } }, r.wait);
    var data = new TextEncoder().encode(r.method + " " + r.url + " " + (r.form ? "f" : "-") + "\n" + r.body);
    var CH = 500, n = Math.max(1, Math.ceil(data.length / CH)), id = rid;
    var chain = Promise.resolve();
    for (var s = 0; s < n; s++) (function (s) {
      chain = chain.then(function () {
        var part = data.subarray(s * CH, (s + 1) * CH), pkt = new Uint8Array(4 + part.length);
        pkt.set([s + 1 === n ? 2 : 1, id, s & 255, s >> 8]); pkt.set(part, 4);
        return gatt(function () { return wr(ch.req, pkt); });
      });
    })(s);
    chain.catch(function () { if (cur && cur.id === id) { var c = cur; cur = null; clearTimeout(c.timer); c.reject(new TypeError("Bluetooth write failed")); pump(); } });
  }
  function waitFor(url) {
    return /^\/api\/scan(\?|$)/.test(url) ? 150000 : /^\/api\/(dtc|wake)/.test(url) ? 40000 : 20000;
  }

  function liveResponse() {
    var l = T.live;
    if (!l) return null;
    var o = { awake: l.awake, age: l.age + (Date.now() - T.liveAt), d: l.d, o: l.o, s: l.s, lt: l.lt, ot: l.ot };
    return new Response(JSON.stringify(o), { status: 200, headers: { "Content-Type": "application/json" } });
  }

  window.fetch = function (input, opt) {
    var url = typeof input === "string" ? input : input && input.url;
    if (typeof url !== "string" || url[0] !== "/" || url[1] === "/") return realFetch(input, opt);   // other sites, relative app files
    opt = opt || {};
    if (MOCK) return realFetch(input, opt);       // PC test: the mock board answers over http
    if (url === "/api/live" || url.indexOf("/api/live?") === 0) {
      if (T.state === "on") { var lr = liveResponse(); if (lr && Date.now() - T.liveAt < 3000) return Promise.resolve(lr); }
      return new Promise(function (res, rej) { setTimeout(function () { rej(new TypeError("no Bluetooth link")); }, 1000); });
    }
    var body = opt.body, form = false;
    if (Object.prototype.toString.call(body) === "[object URLSearchParams]") { body = body.toString(); form = true; }   // also from the wizard frame (another realm)
    else if (body == null) body = "";
    else if (typeof body !== "string") body = String(body);
    var method = (opt.method || "GET").toUpperCase();
    return new Promise(function (resolve, reject) {
      queue.push({ method: method, url: url, body: body, form: form, resolve: resolve, reject: reject, wait: waitFor(url) });
      pump();                                     // waits for the link: a request made before connecting is sent once it is up
    });
  };

  // ---------------------------------------------------------------- connect / reconnect
  function open() {
    if (!dev) return Promise.reject(new Error("no device"));
    setState(T.state === "on" ? "on" : (T.state === "lost" ? "lost" : "connecting"));
    return dev.gatt.connect().then(function (srv) { return srv.getPrimaryService(SVC); }).then(function (svc) {
      return Promise.all([svc.getCharacteristic(C_LIVE), svc.getCharacteristic(C_REQ), svc.getCharacteristic(C_RES), svc.getCharacteristic(C_OTA)]);
    }).then(function (c) {
      ch = { live: c[0], req: c[1], res: c[2], ota: c[3] };
      // hello first: the board only answers paired phones, so this is where Android asks for the PIN
      return gatt(function () { return wr(ch.req, u8([4, 0, 0, 0])); });
    }).then(function () {
      ch.res.addEventListener("characteristicvaluechanged", onRes);
      ch.live.addEventListener("characteristicvaluechanged", onLive);
      return sub(ch.res);
    }).then(function () { return sub(ch.live); })
      .then(function () { return gatt(function () { return ch.live.readValue(); }); })
      .then(function (v) { onLive({ target: { value: v } }); })
      .then(function () {
        T.name = dev.name || ""; try { localStorage.setItem("tg-bt-name", T.name); } catch (e) {}
        setState("on", ""); wake(); pump();
      });
  }
  // (re)subscribe: stop first so Chrome really writes the subscription again after a reconnect (it may think it is still on)
  function sub(c) {
    return gatt(function () { return c.stopNotifications().catch(function () {}); }).then(function () { return gatt(function () { return c.startNotifications(); }); });
  }
  function lost() {
    ch = {};
    if (cur) { var c = cur; cur = null; clearTimeout(c.timer); c.reject(new TypeError("Bluetooth link lost")); }
    if (!wantOn) { setState("off"); return; }
    setState("lost");
    clearTimeout(retryT);
    var tries = 0;
    (function again() {
      if (!wantOn || T.state === "on") return;
      open().catch(function () { tries++; retryT = setTimeout(again, Math.min(10000, 1500 + tries * 1500)); });
    })();
  }
  function useDevice(d) {
    if (dev && dev !== d) { try { dev.removeEventListener("gattserverdisconnected", lost); } catch (e) {} }
    dev = d; dev.addEventListener("gattserverdisconnected", lost);
  }
  T.connect = function () {                       // needs a tap (Chrome asks the user to pick the gauge)
    startGps();
    if (MOCK) return Promise.resolve();
    if (!navigator.bluetooth) { setState("off", L("เบราว์เซอร์นี้ใช้บลูทูธไม่ได้ ใช้ Chrome บน Android", "This browser has no Bluetooth. Use Chrome on Android.")); return Promise.resolve(); }
    wantOn = true;
    return navigator.bluetooth.requestDevice({ filters: [{ services: [SVC] }] }).then(function (d) { useDevice(d); return open(); })
      .catch(function (e) {
        var m = String(e && e.message || e);
        if (/cancel/i.test(m) || (e && e.name === "NotFoundError")) {   // closed the list, often because the gauge was not in it
          if (T.state !== "on") setState("off", L("ไม่เจอเกจในรายการ? บิดกุญแจเปิดอยู่ไหม · ต่อได้ทีละเครื่อง: ถ้ามือถืออีกเครื่องเปิดแอปอยู่ ให้กดตัดการเชื่อมต่อหรือปิดแอปบนเครื่องนั้นก่อน",
            "Gauge not in the list? Is the key on? · One phone at a time: if another phone has the app open, disconnect or close it there first"));
          return;
        }
        if (/auth|pair|security|insufficient/i.test(m)) m = L("จับคู่ไม่สำเร็จ ลองใหม่แล้วใส่รหัส PIN 6 หลักที่ตั้งไว้ในตัวช่วยตั้งค่า", "Pairing failed. Try again and enter the 6-digit PIN set in the setup wizard.");
        setState(T.state === "on" ? "on" : "off", m);
      });
  };
  T.disconnect = function () { wantOn = false; clearTimeout(retryT); if (dev && dev.gatt.connected) dev.gatt.disconnect(); else setState("off"); };
  // Chrome remembers the gauge picked before; reconnect without asking when it allows (no tap needed)
  function autoConnect() {
    if (MOCK) { setState("on", ""); T.name = "mock board"; return; }
    if (!navigator.bluetooth || !navigator.bluetooth.getDevices) return;
    navigator.bluetooth.getDevices().then(function (list) {
      var want = ""; try { want = localStorage.getItem("tg-bt-name") || ""; } catch (e) {}
      var d = list.filter(function (x) { return !want || x.name === want; })[0] || list[0];
      if (!d) return;
      useDevice(d); wantOn = true; setState("connecting");
      var go = function () { return open().catch(function () { lost(); }); };
      if (d.watchAdvertisements) {                // wait until the board is actually in range
        var done = false, onAdv = function () { if (done) return; done = true; d.removeEventListener("advertisementreceived", onAdv); try { d.unwatchAdvertisements && d.unwatchAdvertisements(); } catch (e) {} go(); };
        d.addEventListener("advertisementreceived", onAdv);
        d.watchAdvertisements().catch(go);
        setTimeout(function () { if (!done) { done = true; go(); } }, 4000);
      } else go();
    }).catch(function () {});
  }

  // keep the screen on while riding (connected)
  var lock = null;
  function wake() {
    if (!navigator.wakeLock || document.hidden || lock) return;
    navigator.wakeLock.request("screen").then(function (l) { lock = l; l.addEventListener("release", function () { lock = null; }); }).catch(function () {});
  }
  // The board takes one phone at a time. An app left open in the background (screen off, another app in front) lets go
  // of the link after 30 s so another phone can connect, and takes it again when it is on screen. While riding the
  // screen stays on (wake lock), so the link is kept. Not during a firmware upload.
  var hideT = 0, parked = false;
  document.addEventListener("visibilitychange", function () {
    clearTimeout(hideT);
    if (document.hidden) {
      if (T.state === "on" && !MOCK) hideT = setTimeout(function () {
        if (!document.hidden || T.otaBusy || T.state !== "on") return;
        parked = true; wantOn = false; clearTimeout(retryT);
        try { dev.gatt.disconnect(); } catch (e) {}
        setState("off", L("พักการเชื่อมต่อไว้ตอนแอปไม่ได้เปิดอยู่ (ให้มือถือเครื่องอื่นต่อได้)", "Link paused while the app was in the background (so another phone can connect)"));
      }, 30000);
      return;
    }
    if (parked && dev) { parked = false; wantOn = true; setState("connecting", ""); open().catch(function () { lost(); }); }
    else if (T.state === "on") wake();
  });

  // ---------------------------------------------------------------- firmware update over Bluetooth
  T.ota = function (buf, prog) {
    if (T.state !== "on" || !ch.ota) return Promise.reject(new Error(L("ยังไม่ได้เชื่อมต่อบลูทูธ", "Not connected over Bluetooth")));
    var data = new Uint8Array(buf), size = data.length, CH = 495, dec = new TextDecoder();
    if (data[0] !== 0xE9) return Promise.reject(new Error(L("ไฟล์ไม่ใช่เฟิร์มแวร์ ESP32", "Not an ESP32 firmware file")));
    T.otaBusy = true;
    var w = function (pkt) { return gatt(function () { return wr(ch.ota, pkt); }); };
    var st = function () { return gatt(function () { return ch.ota.readValue(); }).then(function (v) { return dec.decode(v); }); };
    var check = function () { return st().then(function (s) { if (s.indexOf("err") === 0) throw new Error(s.slice(4)); return s; }); };
    var u32 = function (x) { return [x & 255, (x >> 8) & 255, (x >> 16) & 255, (x >>> 24) & 255]; };
    var t0 = Date.now();
    return w(u8([1].concat(u32(size)))).then(check).then(function () {
      var off = 0, k = 0;
      function next() {
        if (off >= size) return Promise.resolve();
        var part = data.subarray(off, off + CH), pkt = new Uint8Array(5 + part.length);
        pkt[0] = 2; pkt.set(u32(off), 1); pkt.set(part, 5);
        return w(pkt).then(function () {
          off += part.length; k++;
          if (prog) prog(off / size, (Date.now() - t0) / 1000);
          return k % 64 === 0 ? check().then(next) : next();
        });
      }
      return next();
    }).then(function () { return w(u8([3])); }).then(check).then(function (s) {
      T.otaBusy = false; if (s.indexOf("ok done") !== 0) throw new Error(s); return s;
    }).catch(function (e) {
      T.otaBusy = false;
      if (ch.ota) w(u8([4])).catch(function () {});
      pump(); throw e;
    });
  };

  // ---------------------------------------------------------------- the phone's GPS = road speed
  // The board's page code reads window.LIVE (the full build's GPS/motion feed): on, kmh (-1 = none), fix, sats.
  var LIVE = window.LIVE = { on: false, mpu: false, kmh: -1, fix: -1, sats: -1, roll: 0, ax: 0, ay: 0, t: 0, acc: 0 };
  var G = window.TGGPS = { state: "off", err: "" };   // off | wait | ok | denied | none
  var gpsWatch = null, lastFix = null;
  function gpsSet(s, e) { G.state = s; if (e !== undefined) G.err = e; subs.forEach(function (f) { try { f(T.state); } catch (x) {} }); }
  function hav(a, b) {
    var R = 6371000, r = Math.PI / 180, dLa = (b.latitude - a.latitude) * r, dLo = (b.longitude - a.longitude) * r;
    var x = Math.sin(dLa / 2) * Math.sin(dLa / 2) + Math.cos(a.latitude * r) * Math.cos(b.latitude * r) * Math.sin(dLo / 2) * Math.sin(dLo / 2);
    return 2 * R * Math.atan2(Math.sqrt(x), Math.sqrt(1 - x));
  }
  function onFix(p) {
    var c = p.coords, now = Date.now(), v = null;
    if (c.speed != null && !isNaN(c.speed)) v = c.speed * 3.6;               // Doppler speed from the GPS chip: best
    else if (lastFix) { var dt = (p.timestamp - lastFix.t) / 1000; if (dt > 0.2 && dt < 4) v = hav(lastFix.c, c) / dt * 3.6; }
    lastFix = { c: c, t: p.timestamp };
    var good = c.accuracy <= 35;
    // Android often gives no speed while the phone stands still, and sends fixes only now and then: a position
    // with no speed counts as standing (0 km/h), so GPS shows as working from the first fix
    if (v == null) v = 0;
    LIVE.kmh = LIVE.kmh > 0 && v > 0 ? LIVE.kmh * 0.3 + v * 0.7 : v;
    LIVE.fix = good ? 1 : 0; LIVE.acc = c.accuracy; LIVE.on = true; LIVE.t = now;
    if (G.state !== (good ? "ok" : "wait")) gpsSet(good ? "ok" : "wait", "");
  }
  function onGpsErr(e) {
    if (e.code === 1) { gpsSet("denied", L("ไม่ได้อนุญาตตำแหน่ง: เปิดใน Chrome → การตั้งค่าเว็บไซต์ → ตำแหน่ง", "Location not allowed: Chrome → Site settings → Location")); stopGps(); }
    else if (e.code === 2) gpsSet("wait", L("มือถือยังหาตำแหน่งไม่ได้: เปิด Location ของมือถือ และให้แอป Chrome ใช้ตำแหน่งได้ (ตั้งค่า Android → แอป → Chrome → สิทธิ์ → ตำแหน่ง)", "The phone cannot get a position: turn Location on and let the Chrome app use it (Android settings → Apps → Chrome → Permissions → Location)"));
    else gpsSet("wait", L("รอตำแหน่งนานเกิน 15 วินาที ยังรออยู่ ลองออกไปกลางแจ้ง", "No position for 15 s, still waiting; try outdoors"));
  }
  function startGps() {
    if (MOCK) { mockGps(); return; }
    if (gpsWatch != null) return;
    if (!navigator.geolocation) { gpsSet("none", L("มือถือ/เบราว์เซอร์นี้ไม่มี GPS", "No GPS in this phone/browser")); return; }
    gpsSet("wait", "");
    gpsWatch = navigator.geolocation.watchPosition(onFix, onGpsErr, { enableHighAccuracy: true, maximumAge: 0, timeout: 15000 });
  }
  function stopGps() { if (gpsWatch != null) navigator.geolocation.clearWatch(gpsWatch); gpsWatch = null; }
  T.startGps = startGps;
  setInterval(function () {                     // no new fix: after 5 s speed 0 and "searching" (timing stops); after 60 s no GPS
    var age = Date.now() - LIVE.t;
    if (!LIVE.on || MOCK) return;
    if (age > 60000) { LIVE.on = false; LIVE.kmh = -1; LIVE.fix = -1; if (G.state === "ok") gpsSet("wait"); }
    else if (age > 5000 && LIVE.fix > 0) { LIVE.kmh = 0; LIVE.fix = 0; if (G.state === "ok") gpsSet("wait"); }
  }, 1000);
  var mockT = 0;
  function mockGps() {                          // PC test: speed from the mock board's simulated ride
    if (mockT) return;
    gpsSet("wait", "");
    mockT = setInterval(function () {
      realFetch("/data", { cache: "no-store" }).then(function (r) { return r.json(); }).then(function (d) {
        if (typeof d.kmh !== "number") return;
        LIVE.kmh = d.kmh; LIVE.fix = 1; LIVE.on = true; LIVE.t = Date.now(); LIVE.acc = 5;
        if (G.state !== "ok") gpsSet("ok", "");
      }).catch(function () {});
    }, 250);
  }
  // start at once (Chrome asks for location the first time); the app may reconnect Bluetooth by itself without a tap,
  // so GPS must not wait for the connect button
  if (MOCK) mockGps(); else startGps();

  // ---------------------------------------------------------------- shared screen setup over the link
  // Same job as the board page's uiSync (mk_budget.py): the look/layout keys live on the board in /ui.json so
  // every phone shows the same screen. The board page loads them synchronously before it starts; over Bluetooth
  // that is not possible, so the app starts with this phone's copy and, once connected, takes the board's copy
  // and reloads if it differs.
  (function uiSync() {   // named: ecu_board.py anchors on the page's own anonymous script
    var KEYS = ["tg-app", "tg-cardcfg", "tg-cards", "tg-face", "tg-gearst", "tg-layout", "tg-looks", "tg-shift", "tg-side", "tg-sidest", "tg-slst", "tg-style", "tg-theme", "tg-tiles", "tg-lang", "tg-pump-tile"];
    var ls; try { ls = window.localStorage; ls.getItem("tg-ui-ver"); } catch (e) { return; }
    var P = Storage.prototype, setI = P.setItem, remI = P.removeItem, ver = +(ls.getItem("tg-ui-ver") || 0), timer = 0, mute = false, last = "", synced = false;
    function snapOf(d) { var o = {}; KEYS.forEach(function (k) { if (typeof d[k] === "string") o[k] = d[k]; }); return o; }
    function snap() { var d = {}; KEYS.forEach(function (k) { var v = ls.getItem(k); if (v != null) d[k] = v; }); return d; }
    function apply(d) { last = JSON.stringify(snapOf(d)); mute = true; KEYS.forEach(function (k) { if (typeof d[k] === "string") setI.call(ls, k, d[k]); else remI.call(ls, k); }); mute = false; }
    function setVer(v) { ver = v; setI.call(ls, "tg-ui-ver", String(v)); }
    function push(retry) {
      timer = 0; if (!synced) return;
      var d = snap(), j = JSON.stringify(d); if (!Object.keys(d).length || (j === last && !retry)) return;
      window.fetch("/ui?v=" + ver, { method: "POST", headers: { "Content-Type": "text/plain" }, body: j }).then(function (r) {
        var v = +(r.headers.get("X-UI-Ver") || 0);
        return r.text().then(function (t) {
          v = v || +t || 0;
          if (r.status === 200) { setVer(v); last = j; uiSay(L("ส่งค่าหน้าจอไปบอร์ดแล้ว v", "Sent to the board, v") + v); }
          else if (r.status === 409 && !retry) { setVer(v); push(true); }
          else uiSay(L("ส่งค่าหน้าจอไม่สำเร็จ (", "Could not send the screen setup (") + r.status + ")");
        });
      }).catch(function (e) { uiSay(L("ส่งค่าหน้าจอไม่สำเร็จ: ", "Could not send the screen setup: ") + (e && e.message || e)); });
    }
    function uiSay(t) { T.uiInfo = t + " · " + new Date().toLocaleTimeString(); }
    function later() { if (!mute) { clearTimeout(timer); timer = setTimeout(push, 1500); } }
    P.setItem = function (k, v) { setI.call(this, k, v); if (this === ls && KEYS.indexOf(k) >= 0) later(); };
    P.removeItem = function (k) { remI.call(this, k); if (this === ls && KEYS.indexOf(k) >= 0) later(); };
    function pull(first) {
      return window.fetch("/ui", { cache: "no-store" }).then(function (r) {
        var bv = +(r.headers.get("X-UI-Ver") || 0);
        return r.json().then(function (d) {
          if (r.status !== 200) return;
          if (d && bv) {
            // the board's copy wins when it differs from this phone's: compare the content too, not only the version
            // (a BOOT reset starts the board's count at 0 again, so a phone's old number can match by chance)
            var theirs = JSON.stringify(snapOf(d)), mine = JSON.stringify(snapOf(snap()));
            if (theirs !== mine) { apply(d); setVer(bv); synced = true; uiSay(L("ได้ค่าหน้าจอจากบอร์ด v", "Took the board's screen setup, v") + bv); try { sessionStorage.setItem("tg-ui-reload", "1"); } catch (e) {} location.reload(); return; }
            setVer(bv); last = theirs; uiSay(L("หน้าจอตรงกับบอร์ดแล้ว v", "Same screen as the board, v") + bv);
          } else if (first) { setVer(0); synced = true; uiSay(L("บอร์ดยังไม่มีค่าหน้าจอ ส่งของเครื่องนี้ไป", "The board has no screen setup yet: sending this phone's")); setTimeout(push, 1500); return; }   // nothing on the board yet: this phone's screen becomes the shared one
          synced = true;
        });
      }).catch(function (e) { uiSay(L("ดึงค่าหน้าจอไม่สำเร็จ: ", "Could not get the screen setup: ") + (e && e.message || e) + L(" · ลองใหม่อัตโนมัติ", " · retrying")); });
    }
    var pulled = false;
    T.on(function (s) { if (s === "on" && !pulled) { pulled = true; pull(true); } if (s !== "on") pulled = false; });
    setInterval(function () {                    // another phone changed the screen: take it over
      if (timer || document.hidden || T.state !== "on") return;
      if (!synced) { pull(true); return; }        // the first read failed: try again (every 10 s while connected)
      window.fetch("/ui?q=1", { cache: "no-store" }).then(function (r) { return r.text(); }).then(function (t) { if (+t && +t !== ver && !timer) pull(false); }).catch(function () {});
    }, 10000);
  })();

  // ---------------------------------------------------------------- full-screen pages (setup wizard, firmware update)
  // They open in a frame over the gauge, so they share this page's Bluetooth link (a new page would have to pair again).
  window.TGOV = {
    open: function (url) {
      if (document.getElementById("tg-ov")) return;
      var f = document.createElement("iframe");
      f.id = "tg-ov"; f.src = url; f.setAttribute("title", "Thunder Gauge");
      f.style.cssText = "position:fixed;inset:0;width:100%;height:100%;border:0;z-index:2147483000;background:#0b0e14";
      document.body.appendChild(f);
      try { history.pushState({ tgov: 1 }, ""); } catch (e) {}
    },
    close: function (reload) {
      var f = document.getElementById("tg-ov"); if (f) f.remove();
      if (reload) location.reload();
    }
  };
  window.addEventListener("popstate", function () { if (document.getElementById("tg-ov")) window.TGOV.close(false); });
  document.addEventListener("click", function (e) {
    var a = e.target.closest && e.target.closest("a[data-ov]");
    if (!a) return;
    e.preventDefault(); window.TGOV.open(a.getAttribute("href"));
  });

  // ---------------------------------------------------------------- connection bar + settings card
  var CSS = "#tg-bt{position:fixed;left:50%;transform:translateX(-50%);bottom:calc(14px + env(safe-area-inset-bottom));z-index:900;display:flex;align-items:center;gap:10px;width:min(440px,calc(100% - 24px));box-sizing:border-box;border-radius:16px!important;" +
    "background:var(--panel,#141924);color:var(--text,#e8edf6);border:1px solid var(--line,#2a3346);border-radius:99px;padding:8px 8px 8px 14px;box-shadow:0 6px 24px rgba(0,0,0,.45);font:600 13px var(--font-body,system-ui)}" +
    "#tg-bt[hidden]{display:none}#tg-bt .m{flex:1;min-width:0;line-height:1.35}#tg-bt .m small{display:block;font-weight:400;color:var(--dim,#8d97ab);font-size:11.5px;white-space:normal}" +
    "#tg-bt button{flex:none;font:600 13px var(--font-body,system-ui);color:#fff;background:var(--accent,#ff3b30);border:0;border-radius:99px;padding:9px 14px;cursor:pointer}" +
    "#tg-bt .bb{display:flex;flex-direction:column;gap:6px;flex:none}#tg-bt .bb a{display:block;text-align:center;font:600 13px var(--font-body,system-ui);color:#fff;background:#2f6fe4;border-radius:99px;padding:9px 14px;text-decoration:none}#tg-bt .bb a[hidden],#tg-bt button[hidden]{display:none}" +
    "#tg-pin{position:fixed;left:50%;transform:translateX(-50%);top:calc(10px + env(safe-area-inset-top));z-index:901;display:flex;align-items:center;gap:10px;width:min(440px,calc(100% - 24px));box-sizing:border-box;background:var(--panel,#141924);color:var(--text,#e8edf6);border:1px solid var(--warn,#f2d24b);border-radius:16px;padding:10px 10px 10px 14px;box-shadow:0 6px 24px rgba(0,0,0,.45);font:600 13px var(--font-body,system-ui)}" +
    "#tg-pin .m{flex:1;min-width:0;line-height:1.35}#tg-pin small{display:block;font-weight:400;color:var(--dim,#8d97ab);font-size:11.5px}#tg-pin .bb{display:flex;flex-direction:column;gap:6px;flex:none}" +
    "#tg-pin button{font:600 13px var(--font-body,system-ui);border:0;border-radius:99px;padding:8px 14px;cursor:pointer;color:#fff;background:var(--accent,#ff3b30)}#tg-pin button.no{background:none;color:var(--dim,#8d97ab);border:1px solid var(--line,#2a3346)}" +
    "#tg-bt i{width:9px;height:9px;border-radius:50%;flex:none;background:var(--dim,#8d97ab)}#tg-bt i.w{background:var(--warn,#f2d24b);animation:tgbtp 1s infinite alternate}@keyframes tgbtp{to{opacity:.3}}" +
    "#bt-card .bt-kv{display:flex;justify-content:space-between;gap:10px;padding:6px 0;border-top:1px solid var(--line,#2a3346);font-size:13.5px}#bt-card .bt-kv:first-of-type{border-top:0}#bt-card .bt-kv span{color:var(--dim,#8d97ab)}#bt-card .bt-kv b{text-align:right}" +
    "#bt-card .acts{display:flex;flex-wrap:wrap;gap:8px;margin-top:10px}#bt-card .bt-err{color:var(--crit,#ff5a4e);font-size:13px}";
  function el(h) { var d = document.createElement("div"); d.innerHTML = h; return d.firstElementChild; }
  function btTxt() {
    var s = T.state;
    if (!T.supported && IOS) return [L("iPhone: เปิดแอปนี้ในแอป Bluefy", "iPhone: open this app in Bluefy"), L("Safari และ Chrome บน iPhone ใช้บลูทูธไม่ได้ · 1) ติดตั้ง Bluefy (ฟรี) 2) คัดลอกลิงก์ 3) เปิด Bluefy แล้ววางลิงก์ในช่องที่อยู่", "Safari and Chrome on iPhone have no Bluetooth · 1) Install Bluefy (free) 2) Copy the link 3) Open Bluefy and paste it in the address bar")];
    if (!T.supported) return [L("บลูทูธใช้ไม่ได้ในเบราว์เซอร์นี้", "No Bluetooth in this browser"), L("Android: เปิดใน Chrome · iPhone: เปิดในแอป Bluefy · หรือต่อ Wi-Fi ของเกจแล้วเปิด http://192.168.4.1", "Android: open in Chrome · iPhone: open in the Bluefy app · or join the gauge's Wi-Fi and open http://192.168.4.1")];
    if (s === "connecting") return [L("กำลังเชื่อมต่อเกจ…", "Connecting to the gauge…"), ""];
    if (s === "lost") return [L("สัญญาณบลูทูธหลุด กำลังต่อใหม่…", "Bluetooth link lost, reconnecting…"), L("บิดกุญแจเปิดอยู่ไหม", "Is the key on?")];
    return [L("ยังไม่ได้เชื่อมต่อเกจ", "Not connected to the gauge"), T.err || L("บิดกุญแจ แล้วกดเชื่อมต่อ เลือกเกจในรายการ · ครั้งแรกไม่ต้องใส่ PIN (ถ้ามือถือถาม ให้ใส่ PIN ที่ตั้งไว้)", "Key on, tap connect and pick the gauge · No PIN the first time (if the phone asks, enter the PIN you set)")];
  }
  function gpsTxt() {
    var g = G.state;
    return g === "ok" ? L("พร้อม", "Ready") + (LIVE.acc ? " · ±" + Math.round(LIVE.acc) + " m" : "") + (LIVE.kmh >= 0 ? " · " + LIVE.kmh.toFixed(0) + " km/h" : "")
      : g === "wait" ? L("กำลังหาตำแหน่ง…", "Getting a fix…") + (LIVE.acc ? " ±" + Math.round(LIVE.acc) + " m" : "")
      : g === "denied" ? L("ไม่ได้อนุญาต", "Not allowed") : g === "none" ? L("ไม่มี GPS", "No GPS") : L("ปิดอยู่", "Off");
  }
  var apInfo = null, copied = false;
  function apAsk(q) {
    if (T.state !== "on") return;
    window.fetch("/api/link" + (q || ""), { cache: "no-store" }).then(function (r) { return r.json(); }).then(function (j) { apInfo = j; paint(); }).catch(function () {});
  }
  function paint() {
    var bar = document.getElementById("tg-bt");
    if (bar) {
      var t = btTxt(), s = T.state;
      bar.hidden = s === "on";
      bar.querySelector("i").className = s === "connecting" || s === "lost" ? "w" : "";
      bar.querySelector(".m").innerHTML = t[0] + (t[1] ? "<small>" + t[1] + "</small>" : "");
      var b = bar.querySelector("button"), ios = !T.supported && IOS;
      b.hidden = (!T.supported && !ios) || s === "connecting";
      b.textContent = ios ? (copied ? L("คัดลอกแล้ว ✓", "Copied ✓") : L("คัดลอกลิงก์", "Copy link")) : s === "lost" ? L("เลือกใหม่", "Pick again") : L("เชื่อมต่อ", "Connect");
      var a = bar.querySelector("a"); a.hidden = !ios; a.textContent = L("ติดตั้ง Bluefy", "Get Bluefy");
    }
    var c = document.getElementById("bt-card");
    if (!c) return;
    var on = T.state === "on";
    c.querySelector("#bt-st").textContent = on ? L("เชื่อมต่อแล้ว", "Connected") + (T.name ? " · " + T.name : "") : btTxt()[0];
    c.querySelector("#bt-gps").textContent = gpsTxt();
    c.querySelector("#bt-ui").textContent = T.uiInfo || "–";
    c.querySelector("#bt-ap").textContent = !on || !apInfo ? "–" : apInfo.ap ? L("เปิดอยู่", "On") + " · " + apInfo.ssid + (apInfo.auto && apInfo.off > 0 ? " · " + L("ปิดเองใน ", "turns off in ") + Math.max(1, Math.round(apInfo.off / 60)) + L(" นาที", " min") : "") : L("ปิดอยู่", "Off");
    c.querySelector("#bt-go").hidden = on || !T.supported;
    c.querySelector("#bt-off").hidden = !on || MOCK;
    c.querySelector("#bt-gpsgo").hidden = G.state === "ok" || G.state === "wait";
    c.querySelector("#bt-apon").hidden = !on || !apInfo || apInfo.ap;
    c.querySelector("#bt-apoff").hidden = !on || !apInfo || !apInfo.ap;
    c.querySelector(".bt-err").textContent = (T.state !== "on" && T.err) || (G.state !== "ok" ? G.err : "");
  }
  var setupAsked = false;
  // reminder: the board has no Bluetooth PIN and/or no Wi-Fi password yet. "Later" hides it until the app is opened again.
  function pinWarn(on) {
    var w = document.getElementById("tg-pin");
    if (!on) { if (w) w.remove(); return; }
    if (w || !document.body) return;
    var both = on.nopin && on.nopass, what = both ? L("PIN บลูทูธ และรหัส Wi-Fi", "a Bluetooth PIN and a Wi-Fi password") : on.nopin ? L("PIN บลูทูธ", "a Bluetooth PIN") : L("รหัส Wi-Fi", "a Wi-Fi password");
    w = el('<div id="tg-pin" role="alert"><div class="m"><b>' + L("ตั้งรหัสของเกจ", "Lock your gauge") + '</b><small>' +
      L("เกจยังไม่มี " + what + " คนแถวนั้นต่อเกจได้ ตั้งรหัสของคุณเองในหน้าตั้งค่า", "The gauge has no " + what + " yet, so anyone nearby can connect. Set your own in the setup page") +
      '</small></div><div class="bb"><button type="button" class="go">' + L("ตั้งรหัส", "Set it") + '</button><button type="button" class="no">' + L("ทีหลัง", "Later") + '</button></div></div>');
    w.querySelector(".go").addEventListener("click", function () { pinWarn(false); window.TGOV.open("setup.html"); });
    w.querySelector(".no").addEventListener("click", function () { pinWarn(false); });
    document.body.appendChild(w);
  }
  T.on(function (s) {
    if (s === "on") {
      apAsk();
      if (!setupAsked) {                          // a board that was never set up: open the wizard, as the board's own page does
        setupAsked = true;
        window.fetch("/api/setup/look", { cache: "no-store" }).then(function (r) { return r.json(); }).then(function (j) {
          if (j && j.done === false) window.TGOV.open("setup.html");
          else if (j && (j.nopin || j.nopass)) pinWarn(j);   // no PIN / no Wi-Fi password yet: anyone nearby could connect
        }).catch(function () {});
      }
    }
    paint();
  });
  setInterval(function () { paint(); }, 1000);
  setInterval(function () { if (document.getElementById("bt-card") && !document.hidden) apAsk(); }, 15000);
  function ui() {
    var st = document.createElement("style"); st.textContent = CSS; document.head.appendChild(st);
    var bar = el('<div id="tg-bt" role="status"><i></i><div class="m"></div><div class="bb"><a href="' + BLUEFY + '" target="_blank" rel="noopener" hidden></a><button type="button">เชื่อมต่อ</button></div></div>');
    bar.querySelector("button").addEventListener("click", function () {
      if (T.supported || !IOS) return T.connect();
      var u = location.href.split("#")[0].split("?")[0], done = function () { copied = true; paint(); };   // the link to paste into Bluefy
      if (navigator.clipboard) navigator.clipboard.writeText(u).then(done).catch(function () { prompt(L("คัดลอกลิงก์นี้", "Copy this link"), u); });
      else prompt(L("คัดลอกลิงก์นี้", "Copy this link"), u);
    });
    document.body.appendChild(bar);
    var set = document.getElementById("v-set");
    if (set) {
      var card = el('<div class="card" id="bt-card"><h2>' + L("บลูทูธ · GPS มือถือ", "Bluetooth · phone GPS") + '</h2>' +
        '<div class="bt-kv"><span>' + L("เกจ", "Gauge") + '</span><b id="bt-st">–</b></div>' +
        '<div class="bt-kv"><span>' + L("GPS มือถือ (ความเร็ว)", "Phone GPS (speed)") + '</span><b id="bt-gps">–</b></div>' +
        '<div class="bt-kv"><span>' + L("Wi-Fi ของบอร์ด", "Board Wi-Fi") + '</span><b id="bt-ap">–</b></div>' +
        '<div class="bt-kv"><span>' + L("ซิงก์หน้าจอ", "Screen sync") + '</span><b id="bt-ui">–</b></div>' +
        '<p class="bt-err"></p><div class="acts">' +
        '<button class="btn go" type="button" id="bt-go">' + L("เชื่อมต่อเกจ", "Connect gauge") + '</button>' +
        '<button class="btn" type="button" id="bt-gpsgo">' + L("เปิด GPS", "Turn on GPS") + '</button>' +
        '<button class="btn" type="button" id="bt-apon">' + L("เปิด Wi-Fi ของบอร์ด", "Turn board Wi-Fi on") + '</button>' +
        '<button class="btn" type="button" id="bt-apoff">' + L("ปิด Wi-Fi ของบอร์ด", "Turn board Wi-Fi off") + '</button>' +
        '<button class="btn" type="button" id="bt-off">' + L("ตัดการเชื่อมต่อ", "Disconnect") + '</button></div>' +
        '<p class="note">' + L("ความเร็วมาจาก GPS ของมือถือ (ECU ของ Wave 110i ไม่ส่งความเร็ว) ได้ราว 1 ครั้งต่อวินาที วางมือถือให้เห็นท้องฟ้า · Wi-Fi ของบอร์ดใช้ตอนอยากเปิดหน้าเกจจากมือถือเครื่องอื่นหรือ iPhone (http://192.168.4.1) บอร์ดปิด Wi-Fi เองเมื่อแอปนี้ต่อบลูทูธ และปิดอีกครั้งเมื่อไม่มีใครต่อ 10 นาที",
          "Speed comes from the phone's GPS (the Wave 110i ECU sends none), about once a second; keep the phone where it sees the sky · The board's Wi-Fi is for opening the gauge on another phone or an iPhone (http://192.168.4.1); the board turns it off once this app connects over Bluetooth, and again after 10 minutes with nobody on it") + '</p><p class="note" style="opacity:.6">' + L("รุ่นแอป ", "App build ") + "__APPVER__" + '</p></div>');
      set.insertBefore(card, set.querySelector(".card"));
      card.querySelector("#bt-go").addEventListener("click", T.connect);
      card.querySelector("#bt-off").addEventListener("click", T.disconnect);
      card.querySelector("#bt-gpsgo").addEventListener("click", startGps);
      card.querySelector("#bt-apon").addEventListener("click", function () { apAsk("?ap=1"); });
      card.querySelector("#bt-apoff").addEventListener("click", function () { apAsk("?ap=0"); });
    }
    paint();
    autoConnect();
  }
  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", ui); else ui();
})();
