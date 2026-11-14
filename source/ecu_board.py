"""Shared board-page patches: real ECU data from the Thunder Gauge box (K-Line, /api/live, /api/dtc).
Used by mk_budget.py (Lite: no GPS/MPU) and mk_live.py (full: GPS speed + MPU motion come from LIVE).
Never simulates: with no ECU answer every ECU reading shows 0 or "–"."""
import os
NL = chr(10)
ANCHOR = '<script>' + NL + '(function () {' + NL
here = os.path.dirname(os.path.abspath(__file__))

def patch(b, lite):
    # real ECU values only: the box reads table 0x11 about 5 times a second and serves the raw bytes at
    # /api/live. The board page never simulates: no ECU answer = every reading shows 0.
    # Decoded with the usual Honda/Keihin table 0x11 layout; positions are a best guess until checked on
    # the Wave 110i, and a value outside its possible range shows 0 instead of a made-up number.
    ecu = r"""<script>
    window.ECU = { on: false, d: [], o: [], s: [], t: 0, age: 0 };
    (function () {
      function poll() {
        fetch('/api/live', { cache: 'no-store' }).then(function (r) { return r.json(); }).then(function (j) {
          if (j.awake && j.age < 1500 && j.d.length >= 14) { if (!ECU.dtcDone && window.__dtcRead) { ECU.dtcDone = true; setTimeout(window.__dtcRead, 300); } ECU.d = j.d; ECU.o = j.o || []; ECU.s = j.s || []; ECU.lt = j.lt || 0x17; ECU.ot = j.ot || 0x20; ECU.t = Date.now(); ECU.age = j.age; ECU.on = true; }
          else ECU.on = false;
        }).catch(function () { ECU.on = false; }).then(function () { setTimeout(poll, 200); });
      }
      poll();
      setInterval(function () {
        if (ECU.on && Date.now() - ECU.t > 1500) ECU.on = false;
        var c = document.getElementById('conn'), n = document.querySelector('.demo-note');
        var en = document.documentElement.lang === 'en';
        if (c) c.textContent = ECU.on ? (en ? 'ECU online' : 'ECU ตอบกลับ') : (en ? 'No ECU' : 'ECU ไม่ตอบ');
        // the note box shows only while the ECU is silent; with live data it is hidden
        if (n) n.style.display = ECU.on ? 'none' : '';
        if (n && !ECU.on) n.innerHTML = (en ? '<strong>No data:</strong> the ECU is not answering. Turn the key on.'
                : '<strong>ไม่มีข้อมูล:</strong> ECU ยังไม่ตอบ บิดกุญแจเปิด');
      }, 500);
    })();
    </script>
    """
    assert b.count(ANCHOR) == 1
    b = b.replace(ANCHOR, ecu + ANCHOR, 1)
    NL = chr(10)

    def brep(old, new_):
        nonlocal b
        assert b.count(old) == 1, (old[:70], b.count(old))
        b = b.replace(old, new_)

    # no sample runs, no sample fault codes (codes are not read from the ECU yet), no "clear codes" (read-only box)
    i = b.index('  let history = ['); k = b.index('  ];', i) + 4
    b = b[:i] + '  let history = [];' + b[k:]
    i = b.index('  let codes = ['); k = b.index('  ];', i) + 4
    # fault codes come from the box (/api/dtc, read-only: current 0x74 + stored 0x73). Meanings are the usual
    # Honda PGM-FI ones; anything else points to the service manual.
    dtc_js = """  let codes = [], curCodes = [], dtcState = "wait";
  const DTC_TXT = {
    "1-1": "เซนเซอร์ MAP แรงดันต่ำผิดปกติ", "1-2": "เซนเซอร์ MAP แรงดันสูงผิดปกติ",
    "7-1": "เซนเซอร์อุณหภูมิเครื่อง (EOT/ECT) แรงดันต่ำผิดปกติ", "7-2": "เซนเซอร์อุณหภูมิเครื่อง (EOT/ECT) แรงดันสูงผิดปกติ",
    "8-1": "เซนเซอร์คันเร่ง (TPS) แรงดันต่ำผิดปกติ", "8-2": "เซนเซอร์คันเร่ง (TPS) แรงดันสูงผิดปกติ",
    "9-1": "เซนเซอร์อุณหภูมิไอดี (IAT) แรงดันต่ำผิดปกติ", "9-2": "เซนเซอร์อุณหภูมิไอดี (IAT) แรงดันสูงผิดปกติ",
    "12-1": "วงจรหัวฉีด", "21-1": "เซนเซอร์ O2", "23-1": "ฮีตเตอร์ของเซนเซอร์ O2",
    "29-1": "วาล์วควบคุมรอบเดินเบา (IACV)", "33-2": "หน่วยความจำ ECU (EEPROM)",
    "54-1": "เซนเซอร์มุมเอียง แรงดันต่ำ", "54-2": "เซนเซอร์มุมเอียง แรงดันสูง"
  };
  const dtcRow = x => { const c = x[0] + "-" + x[1]; return { c, t: DTC_TXT[c] || "ไม่มีความหมายในตาราง ดูคู่มือซ่อม", w: "" }; };
  const dtcEmpty = () => lang === "en"
    ? (dtcState === "ok" ? "No stored codes" : dtcState === "fail" ? "Could not read codes. Turn the engine off (key on) and read again." : "Reading codes from the ECU…")
    : (dtcState === "ok" ? "ไม่มีรหัสในประวัติ" : dtcState === "fail" ? "อ่านรหัสไม่ได้ ลองดับเครื่อง (เปิดกุญแจไว้) แล้วกดอ่านอีกครั้ง" : "กำลังอ่านรหัสจาก ECU…");
  function paintMil() {
    const lamp = document.querySelector(".mil .lamp"), b = $("dtc-mil"), sp = $("dtc-cur");
    if (!b) return;
    const on = curCodes.length > 0;
    lamp.style.borderColor = on ? "var(--warn)" : ""; lamp.style.color = on ? "var(--warn)" : "";
    b.textContent = dtcState !== "ok" ? (lang === "en" ? "Check engine light" : "ไฟเตือนเครื่อง") : on ? (lang === "en" ? "Check engine light on" : "ไฟเตือนเครื่องติด") : (lang === "en" ? "Check engine light off" : "ไฟเตือนเครื่องดับ");
    sp.textContent = dtcState !== "ok" ? dtcEmpty() : on ? curCodes.map(x => x.c + " " + x.t).join(" · ") : (lang === "en" ? "No current fault codes" : "ไม่มีรหัสปัญหาปัจจุบัน");
  }
  window.__dtcRead = function () {
    dtcState = "wait"; renderCodes(); paintMil();
    fetch("/api/dtc", { cache: "no-store" }).then(r => r.json()).then(j => {
      if (!j.ok) { dtcState = "fail"; codes = []; curCodes = []; }
      else { dtcState = "ok"; codes = (j.mem || []).map(dtcRow); curCodes = (j.cur || []).map(dtcRow); }
    }).catch(() => { dtcState = "fail"; }).then(() => { renderCodes(); paintMil(); });
  };"""
    b = b[:i] + dtc_js + b[k:]
    brep("""'<li style="display:block;color:var(--dim)">' + T("ไม่มีรหัสในประวัติ") + '</li>'""",
         """'<li style="display:block;color:var(--dim)">' + dtcEmpty() + '</li>'""")
    brep('  function clearBtn() { $("clear-area").innerHTML = `',
         '  function clearBtn() { $("clear-area").innerHTML = `<button class="btn" id="dtc-read" type="button">${lang === "en" ? "Read codes again" : "อ่านรหัสอีกครั้ง"}</button>`; return; $("clear-area").innerHTML = `')
    brep('    else if (id === "clear-no") clearBtn();', '    else if (id === "clear-no") clearBtn();' + NL + '    else if (id === "dtc-read") window.__dtcRead();')
    brep('<p class="note">ความหมายรหัสเป็นแนวทางจากระบบ PGM-FI ของ Honda ควรจดรหัสและแก้ต้นเหตุก่อนลบ</p>',
         '<p class="note">ความหมายรหัสเป็นแนวทางจากระบบ PGM-FI ของ Honda · กล่องนี้อ่านอย่างเดียว ไม่ลบรหัส ให้จดรหัสแล้วแก้ต้นเหตุ</p>')

    # step(): the board version runs liveStep() instead of the simulation.
    tail_a = b.index('    // peaks' + NL); tail_z = b.index('    if (rec.on) recRow();' + NL, tail_a)
    tail = b[tail_a:tail_z]
    live = """  // full build only: GPS speed and MPU lean/G from the LIVE feed (the Lite page has no LIVE)
      function liveMotion() {
        const L = window.LIVE;
        if (!(L && L.on)) return false;
        if (L.kmh >= 0) S.speed = L.kmh < 2 ? 0 : L.kmh;
        if (L.mpu) { S.lean = clamp(L.roll, -60, 60); S.glat = clamp(L.ay, -1.5, 1.5); S.glong = clamp(L.ax, -1.5, 1.5); }
        return !!L.mpu;
      }
      const ZERO = ["speed", "rpm", "tps", "map", "eot", "iat", "batt", "inj", "igt", "afr", "o2", "lean", "glat", "glong"];
      function liveStep() {
        ticks++;
        if (!(window.ECU && ECU.on)) {
          for (const k of ZERO) S[k] = 0;
          liveMotion();
          S.gear = 0; S.loop = "closed";
          if (R.state === "run") { R.state = "idle"; raceButtons(); }
          if (ticks % 2 === 0) for (const k of GK) { G[k].push(0); if (G[k].length > 150) G[k].shift(); }
          if (ticks % 5 === 0) for (const k of HK) { H[k].push(0); if (H[k].length > 48) H[k].shift(); }
          return;
        }
        const vPrev = S.speed;
        // Wave 110i table 0x17 (19 data bytes), worked out on the bike 2026-11-09 from key-on / idle / ~4,100 rpm:
        // 0-1 rpm, 2 TPS volts, 3 throttle % (x/1.6), 4 ECT volts, 5 engine temp (x-40), 6-9 FF (IAT/MAP slots in the Honda layout: the bike has the MAP/IAT/TPS sensor unit, but this table sends FF),
        // 10 battery (x0.1 V), 11-12 injection (/200 ms), 13 ignition (x/2 - 64), 14 fast-idle/mode byte (not shown).
        // Battery and injection follow oJudgeStore/SmartGauge_for_Honda (Honda PGM-FI layout). Our ride log agrees on
        // the battery: byte 10 sits at 12.2 V engine off and levels off at ~13.2 V from 2,000 rpm (the regulator).
        // No MAP, intake temp or speed in this table: they show 0 / "–". MAP/IAT may sit in another table (source/tools/scan_map_iat.md).
        const d = ECU.d, u16 = i => d[i] * 256 + d[i + 1];
        // Other Honda PGM-FI ECUs (firmware picks the table, ECU.lt): the first 10 bytes follow the same Honda pattern
        // (rpm, TPS V/%, temperature V/°C, intake temp V/°C, MAP V/kPa; FF = no sensor); battery, injection and ignition
        // move. 0x17/0x67 as on the Wave 110i (checked on the bike); 0x13/0x63 and 0x10/0x11/0x60/0x61 from public
        // projects (oJudgeStore/SmartGauge_for_Honda, andreibaw/Honda_K-Line_KWP2000), not checked on a bike yet.
        const lt = ECU.lt || 0x17, ff = i => d[i] === undefined || d[i] === 255, w17 = lt === 0x17 || lt === 0x67;
        const L = w17 || lt === 0x13 || lt === 0x63 ? { b: 10, i: 11, g: 13 } : { b: 12, i: 14, g: 16 };
        let eot = d[5] - 40; if (ff(5) && !ff(9) && w17) eot = d[9] - 40;   // Air Blade 125 (table 0x17): its coolant temperature is in byte 9
        const v = { rpm: u16(0), tps: d[3] / 1.6, eot, inj: u16(L.i) / 200, igt: d[L.g] / 2 - 64, batt: d[L.b] / 10, map: !w17 && !ff(9) ? d[9] : 0, iat: ff(7) ? 0 : d[7] - 40, speed: 0 };
        const lim = { rpm: [0, 12000], tps: [0, 100.5], eot: [-30, 150], iat: [-30, 120], map: [0, 120], batt: [6, 16.5], speed: [0, 0], inj: [0, 25], igt: [-20, 60] };
        for (const k in v) S[k] = v[k] >= lim[k][0] && v[k] <= lim[k][1] ? (k === "tps" ? Math.min(100, v[k]) : v[k]) : 0;
        S.lean = 0; S.glat = 0;
        const mpu = liveMotion();
        const sd = ECU.s || []; S.pump = sd.length >= 5 && sd[4] ? 1 : 0;      // table D1 byte 4 = fuel pump
        // Table 0x20 byte 0 = stock O2 sensor, x/51 V (5 V / 255): a 14-minute ride on 2026-11-09 gave ~0.9 V at full
        // throttle and ~0 V on decel fuel cut. Byte 1 = fuel trim: exactly 128 in open loop (full throttle, cold start).
        // Above 1.2 V (byte > 61) is not a narrowband reading: key-on with a cold sensor gave 3.82 V, so O2/AFR show '–'.
        // Narrowband only says rich or lean, so AFR gets a nominal rich/lean value for the rich/lean wording only.
        const o = ECU.o || [];
        if (o.length >= 2 && o[0] <= 61) { S.o2 = o[0] / 51; S.afr = S.o2 >= .45 ? 14.2 : 15.0; } else { S.o2 = 0; S.afr = 0; }
        if (!mpu) S.glong = clamp((S.speed - vPrev) / 3.6 / DT / 9.81, -1.2, 1.2);
        if (S.speed < 2 || S.rpm < 500) S.gear = 0;
        else { const r = S.speed * 1000 / S.rpm; let g = 1; for (let i = 2; i <= 4; i++) if (Math.abs(KPR[i] - r) < Math.abs(KPR[g] - r)) g = i; S.gear = g; }
        const wot = S.tps > 70, decel = S.tps < 3 && S.rpm > 3000;
        S.loop = decel ? "cut" : o.length >= 2 ? (o[1] === 128 ? "open" : "closed") : wot ? "open" : "closed";
        // fuel trim % for the map (oJudge: (byte1 - 128) * 100 / 128); only meaningful in closed loop with the engine running
        S.trim = S.loop === "closed" && S.rpm > 500 && o.length >= 2 && o[1] > 0 ? (o[1] - 128) * 100 / 128 : null;
        // timing: armed + standing still, the run starts the moment the bike moves
        if (R.state === "armed") { R.armT += DT; if (vPrev === 0 && S.speed > 0) { R.state = "run"; R.t = 0; R.d = 0; R.res = {}; } }
        const racing = R.state === "run";
        if (racing) {
          const v0 = vPrev, d0 = R.d;
          R.t += DT; R.d += (v0 + S.speed) / 2 / 3.6 * DT;
          const vs = S.speed * (1 + cal / 100), v0s = v0 * (1 + cal / 100);
          for (const s of SPLITS) {
            if (R.res[s.k] !== undefined) continue;
            const prev = s.type === "v" ? v0s : d0, cur = s.type === "v" ? vs : R.d;
            if (cur >= s.th) { R.res[s.k] = R.t - DT + (s.th - prev) / (cur - prev || 1) * DT; if (s.type === "d") R.res["s" + s.th] = vs; }
          }
          if (R.d >= 402) finishRun();
        }
    """ + tail.replace('if (afrSrc === "wb") {', 'if (afrSrc === "wb" && S.afr > 0) {') + """    if (rec.on) recRow();
      }
    """
    brep('  function step() {' + NL, live + '  function step() {' + NL + '    if (BOARD) return liveStep();' + NL)
    brep('  const S = { speed: 0,', '  const BOARD = true;' + NL + '  const S = { speed: 0,')

    # tiles: no ECU (or a value the box does not read yet: AFR, O2, injection mode) = no status chip, no verdict text
    brep('      el.querySelector(".v").textContent = T(t.f());' + NL + '      if (t.st) {',
         '      const o2ok = ECU.on && ECU.o && ECU.o.length >= 2 && ECU.o[0] <= 61 && afrSrc !== "wb";' + NL +
         '      const nd = BOARD && (!ECU.on || ((k === "afr" || k === "o2") && !o2ok) || (k === "loop" && !(ECU.o && ECU.o.length >= 2)) || (k === "pump" && !(ECU.s && ECU.s.length >= 5)) || k === "map" || k === "iat");' + NL +
         '      el.querySelector(".v").textContent = nd && (k === "afr" || k === "o2" || k === "loop" || k === "map" || k === "iat" || k === "pump") ? "–" : T(t.f());' + NL +
         '      if (t.st && nd) { const c = el.querySelector(".chip"); c.className = "chip dim"; c.textContent = "–"; }' + NL +
         '      else if (t.st) {')
    brep('      const sb = el.querySelector(".sub"); if (t.sub && sb) sb.textContent = T(t.sub());',
         '      const sb = el.querySelector(".sub"); if (t.sub && sb) sb.textContent = nd ? "" : T(t.sub());')
    brep('<b>ไฟเตือนเครื่องดับ</b><span>ไม่มีรหัสปัญหาปัจจุบัน</span>', '<b id="dtc-mil">ไฟเตือนเครื่อง</b><span id="dtc-cur">กำลังอ่านรหัสจาก ECU…</span>')
    brep('<div id="clear-area"><button class="btn danger" id="clear" type="button">ลบรหัสในประวัติ</button></div>', '<div id="clear-area"><button class="btn" id="dtc-read" type="button">อ่านรหัสอีกครั้ง</button></div>')
    # ride tab AFR card: the box does not read O2/AFR yet, so no marker and no rich/lean verdict
    brep('      $("afr-src").textContent = wb ? "Wideband" : T("O2 เดิม · ประมาณ");',
         '      $("afr-src").textContent = wb ? "Wideband" : T("O2 เดิม · ประมาณ");' + NL +
         '      if (BOARD) $("afr-mk").style.visibility = "";' + NL + '      if (BOARD && !(ECU.on && ECU.o && ECU.o.length >= 2 && ECU.o[0] <= 61 && afrSrc !== "wb")) { $("afr-v").textContent = "–"; $("afr-chip").className = "chip dim"; $("afr-chip").textContent = "–"; $("afr-mk").style.visibility = "hidden"; }')
    # first paint, before the first poll: no claim that the ECU answers or that numbers are samples
    brep('<span id="conn">ECU ตอบกลับ</span>', '<span id="conn">ECU ไม่ตอบ</span>')
    brep('<p class="demo-note"><strong>ตัวอย่าง:</strong> ตัวเลขทั้งหมดเป็นข้อมูลจำลอง ยังไม่ได้อ่านจากรถจริง</p>',
         '<p class="demo-note"><strong>ไม่มีข้อมูล:</strong> ECU ยังไม่ตอบ บิดกุญแจเปิด</p>')
    brep('const REQ = [0x72, 0x05, 0x71, 0x11];', 'const REQ = [0x72, 0x05, 0x71, 0x17];')
    brep('<span class="chip dim">ตาราง 0x11</span>', '<span class="chip dim">ตาราง 0x17</span>')
    brep('<p class="note">ส่งคำขอทุก 100 ms ที่ 10400 bps ไบต์สีทองคือ checksum ตำแหน่งข้อมูลในตารางต้องยืนยันกับรถจริงก่อน</p>',
         '<p class="note">ส่งคำขอทุก 200 ms ที่ 10400 bps ไบต์สีทองคือ checksum · Wave 110i เก็บค่าเครื่องยนต์ไว้ในตาราง 0x17 (ตาราง 0x11 ว่าง)</p>')
    brep('"ตาราง 0x11": "Table 0x11",', '"ตาราง 0x11": "Table 0x11", "ตาราง 0x17": "Table 0x17", "ส่งคำขอทุก 200 ms ที่ 10400 bps ไบต์สีทองคือ checksum · Wave 110i เก็บค่าเครื่องยนต์ไว้ในตาราง 0x17 (ตาราง 0x11 ว่าง)": "Request every 200 ms at 10400 bps. The gold byte is the checksum. The Wave 110i keeps its engine data in table 0x17 (table 0x11 is empty).",')
    # recording: the raw ECU bytes go into the CSV too (table 0x17 bytes 0-18, table 0x20 bytes 0-2), so one ride
    # shows which byte follows road speed or anything else not decoded yet
    brep('  if (BUDGET) REC_COLS.length -= 3;   // lean_deg, g_long, g_lat',
         '  if (BUDGET) REC_COLS.length -= 3;   // lean_deg, g_long, g_lat' + NL +
         '  for (let i = 0; i < 19; i++) REC_COLS.push("x17_" + String(i).padStart(2, "0"));' + NL +
         '  for (let i = 0; i < 3; i++) REC_COLS.push("x20_" + i);' + NL +
         '  for (let i = 0; i < 6; i++) REC_COLS.push("xD1_" + i);')
    brep('      ...(BUDGET ? [] : [S.lean.toFixed(1), S.glong.toFixed(2), S.glat.toFixed(2)])].join(",");',
         '      ...(BUDGET ? [] : [S.lean.toFixed(1), S.glong.toFixed(2), S.glat.toFixed(2)]),' + NL +
         '      ...Array.from({ length: 19 }, (_, i) => ECU.on && ECU.d[i] !== undefined ? ECU.d[i] : ""),' + NL +
         '      ...Array.from({ length: 3 }, (_, i) => ECU.on && ECU.o && ECU.o[i] !== undefined ? ECU.o[i] : ""),' + NL +
         '      ...Array.from({ length: 6 }, (_, i) => ECU.on && ECU.s && ECU.s[i] !== undefined ? ECU.s[i] : "")].join(",");')
    # timing tab: the Wave 110i ECU does not report road speed (bytes 15-18 of table 0x17 stayed 0 over a
    # 14-minute ride), so timing cannot work on this bike without GPS
    if lite: brep('$("race-note").textContent = "จับเวลาจากความเร็วของ ECU (รุ่นนี้ไม่มี GPS) แม่นน้อยกว่ารุ่นมี GPS ใช้เทียบกับรถตัวเองได้ · ควรวิ่งในสนามหรือพื้นที่ปิดเท่านั้น";',
         '$("race-note").textContent = "กล่อง ECU ของ Wave 110i ไม่ส่งความเร็วรถ (ทดสอบขี่จริงแล้ว) การจับเวลาจึงใช้ไม่ได้กับรถรุ่นนี้ ต้องใช้รุ่นที่มี GPS";')
    brep('"Wave 110i · K-Line · รุ่นประหยัด": "Wave 110i · K-Line · Lite",',
         '"Wave 110i · K-Line · รุ่นประหยัด": "Wave 110i · K-Line · Lite", "กล่อง ECU ของ Wave 110i ไม่ส่งความเร็วรถ (ทดสอบขี่จริงแล้ว) การจับเวลาจึงใช้ไม่ได้กับรถรุ่นนี้ ต้องใช้รุ่นที่มี GPS": "The Wave 110i ECU does not report road speed (checked on a real ride), so timing cannot work on this bike. It needs the GPS build.",')
    # gear needs road speed (gear = rpm / speed ratio) and this ECU has neither speed nor a gear signal: show "–"
    brep('      $("gear").textContent = S.gear ? (lang === "en" ? "In gear" : "เข้าเกียร์") : "N"; $("gear").style.color = S.gear ? "" : "var(--ok)"; $("gear").style.fontSize = S.gear ? "18px" : "";',
         '      if (BOARD && window.TG_GEARBOX === "auto") { const sd = ECU.on && ECU.s && ECU.s.length ? ECU.s : null, st = sd && (sd[0] & 2);' + NL +   # automatic: no neutral switch
         '        $("gear").textContent = !sd ? "–" : st ? (lang === "en" ? "Stand down" : "ขาตั้งลง") : "D"; $("gear").style.color = !sd ? "var(--dim)" : st ? "var(--warn)" : "var(--ok)"; $("gear").style.fontSize = st ? "18px" : ""; }' + NL +
         '      else if (BOARD) { const sd = ECU.on && ECU.s && ECU.s.length ? ECU.s : null, n = sd && sd[0] === 1;' + NL +
         '        $("gear").textContent = !sd ? "–" : n ? "N" : S.gear ? S.gear : (lang === "en" ? "In gear" : "เข้าเกียร์"); $("gear").style.color = !sd ? "var(--dim)" : n ? "var(--ok)" : "";' + NL +
         '        $("gear").style.fontSize = sd && !n && !S.gear ? "18px" : ""; }' + NL +
         '      else { $("gear").textContent = S.gear ? S.gear : "N"; $("gear").style.color = S.gear ? "" : "var(--ok)"; }')
    # system tab: table D1 (digital status) shown live for testing: byte 0 = switches (neutral?), byte 4 = fuel pump?
    brep('      <div class="hex" id="hex-res"></div>',
         '      <div class="hex" id="hex-res"></div>' + NL +
         '      <div class="hex" id="hex-d1"></div>' + NL +
         '      <p class="note" id="d1-note">ตาราง D1 (สถานะสวิตช์ · กำลังทดสอบ): เปิดกุญแจรอปั๊มหยุด แล้วเข้าเกียร์ออกเกียร์ ดูว่าไบต์ไหนเปลี่ยน</p>')
    # fuel pump tile (table D1 byte 4: 01 running, 00 stopped), added to the sensor tab
    brep('    loop: { label: "โหมดหัวฉีด",',
         '    pump: { label: "ปั๊มติ๊ก", unit: "", f: () => S.pump ? "ทำงาน" : "หยุด", nospark: true },' + NL + '    loop: { label: "โหมดหัวฉีด",')
    brep('"eot", "batt", "loop"] };', '"eot", "batt", "loop", "pump"] };')
    # Lite default layout, set by the user on the bike 2026-11-10 (no speed / MAP / IAT tiles: this ECU has none)
    # updated 2026-11-14 to the owner's screen: ride tab IGT, INJ, injection mode, pump (then the engine overview card)
    if lite: brep('  const DEF = { r: ["igt", "inj", "loop", "pump"], t: ["rpm", "tps", "igt", "inj", "afr", "o2", "eot", "batt", "loop", "pump"] };',
                  '  const DEF = { r: ["igt", "inj", "loop", "pump"], t: ["rpm", "tps", "igt", "inj", "o2", "eot", "batt", "loop", "afr", "pump"] };')
    brep('  const saveLay = () => {',
         '  try { if (!localStorage.getItem("tg-pump-tile")) { if (!LAY.t.includes("pump")) LAY.t.push("pump"); localStorage.setItem("tg-pump-tile", "1"); localStorage.setItem("tg-layout", JSON.stringify(LAY)); } } catch (e) {}' + NL +
         '  const saveLay = () => {')
    brep('"ตาราง 0x17": "Table 0x17",', '"ตาราง 0x17": "Table 0x17", "ทำงาน": "Running", "หยุด": "Stopped", "เข้าเกียร์": "In gear",')
    # system tab: real raw bytes and data age instead of the demo frame
    brep('      $("lat").textContent = Math.round(rnd(32, 48)) + " ms";',
         '      $("lat").textContent = ECU.on ? Math.round(ECU.age) + " ms" : "–";' + NL +
         '      if (ECU.on && ECU.lt && REQ[3] !== ECU.lt) { REQ.length = 3; REQ.push(ECU.lt); REQ.push(ck(REQ)); paintReq(); }   // the table this ECU answers' + NL +
         '      if (ECU.on) { const res = [0x02, ECU.d.length + 5, 0x71, ECU.lt || 0x17, ...ECU.d]; res.push(ck(res)); $("hex-res").innerHTML = hexRow(T("รับ"), res); }' + NL +
              '      else $("hex-res").innerHTML = "<span>" + T("รับ") + "</span>–";' + NL +
         '      const sd = ECU.s || []; $("hex-d1").innerHTML = ECU.on && sd.length ? "<span>D1</span>" + sd.map(hx).join(" ") + " · " + (lang === "en" ? "switch " : "สวิตช์ ") + sd[0] + " · " + (lang === "en" ? "pump? " : "ปั๊ม? ") + (sd[4] ? (lang === "en" ? "on" : "ทำงาน") : (lang === "en" ? "off" : "หยุด")) : "<span>D1</span>–";' + NL +
         '    } else if (false) {')
    # status light: grey when there is no ECU (zeros would otherwise read as a flat battery)
    brep('    $("led").style.background = ledC;',
         '    if (!ECU.on) { $("led").style.background = "var(--dim)"; $("led").style.boxShadow = "none"; return; }' + NL + '    $("led").style.background = ledC;')
    # the board's system tab gets a K-Line wake/read test card
    # (it talks to /api/* on the box; it does not exist on the claude.ai demo page)
    # English for the board-only text (firmware card, K-Line test card, system notes)
    brep('"ภาษา": "Language",', '"ภาษา": "Language", ' + '"อัปเดตเฟิร์มแวร์": "Firmware update", "อัปโปรแกรมใหม่ลงกล่องผ่าน Wi-Fi ได้เลย ไม่ต้องถอดกล่องหรือต่อสาย · ดาวน์โหลดไฟล์ .bin ล่าสุดจาก GitHub (Releases) ลงมือถือก่อน แล้วกดปุ่มด้านล่าง · เฟิร์มแวร์ที่ใช้อยู่:": "Load new firmware onto the box over Wi-Fi, no need to remove it or run a cable · Download the latest .bin from GitHub (Releases) to your phone first, then tap the button below · Current firmware:", "ตั้งค่าเกจ": "Gauge setup", "ชื่อเกจ ภาษา ชื่อและรหัสผ่าน Wi-Fi ของกล่อง และเช็คการต่อสาย K-Line · ค่าพวกนี้เก็บไว้ในกล่อง ใช้กับทุกมือถือ · ถ้าเปลี่ยน Wi-Fi กล่องจะรีสตาร์ท ให้มือถือต่อ Wi-Fi ใหม่": "Gauge name, language, Wi-Fi name and password of the box, and a K-Line wiring check · Stored in the box, so they apply to every phone · Changing the Wi-Fi restarts the box; reconnect your phone to the new Wi-Fi", "เปิดตัวช่วยตั้งค่า": "Open setup wizard", "ความหมายรหัสเป็นแนวทางจากระบบ PGM-FI ของ Honda · กล่องนี้อ่านอย่างเดียว ไม่ลบรหัส ให้จดรหัสแล้วแก้ต้นเหตุ": "Code meanings follow Honda PGM-FI as a guide · This gauge only reads, it never clears codes. Note the code and fix the cause.", "ตาราง D1 (สถานะสวิตช์ · กำลังทดสอบ): เปิดกุญแจรอปั๊มหยุด แล้วเข้าเกียร์ออกเกียร์ ดูว่าไบต์ไหนเปลี่ยน": "Table D1 (switch status · testing): key on, wait for the pump to stop, then shift in and out of neutral and see which byte changes", "ทดสอบ K-Line": "K-Line test", "อ่านอย่างเดียว": "Read only", "การ์ดนี้ขึ้นเพราะบอร์ดปลุก ECU ไม่ติด ถ้า ECU ตอบปกติ การ์ดจะไม่แสดง": "This card shows because the board could not wake the ECU. When the ECU answers it stays hidden.", "บิดกุญแจ ยังไม่ต้องสตาร์ท แล้วกด \\"ปลุก ECU\\" · ส่งแค่คำสั่งปลุกและขออ่านข้อมูล ไม่เขียนอะไรลงกล่อง": "Turn the key on without starting, then tap \\"Wake ECU\\" · Only wake and read requests are sent, nothing is written to the ECU", "สาย K-Line": "K-Line wire", "ยังไม่ได้ปลุก": "Not woken yet", "ปลุก ECU": "Wake ECU", "ผ่านโมดูล": "Via module", "ทรานซิสเตอร์": "Transistor", "ตารางที่จะอ่าน": "Table to read", "0x17 · ค่าหลัก (Wave 110i)": "0x17 · main values (Wave 110i)", "0x00 · รหัสรุ่นกล่อง": "0x00 · ECU model code", "อ่าน 1 ครั้ง": "Read once", "อ่านต่อเนื่อง": "Read continuously", "ลองบิดคันเร่งหรือสตาร์ทเครื่อง ไบต์ที่เปลี่ยนจะเป็นสีเหลือง": "Try the throttle or start the engine; bytes that change turn yellow", "ไบต์": "Byte", "คู่ ×256": "Pair ×256", "คัดลอกผลทั้งหมด": "Copy all results", "เช็คสายใหม่": "Check wire again", "ล้างบันทึก": "Clear log", ')
    card = open(os.path.join(here, 'kline_card.html'), encoding='utf-8').read()
    kjs = open(os.path.join(here, 'kline_card.js'), encoding='utf-8').read()
    NL = chr(10)
    conn = '    <div class="card">' + NL + '      <h2>การเชื่อมต่อ</h2>'
    end = NL + '    </div>' + NL
    assert b.count(conn) == 1
    i = b.index(conn); k = b.index(end, i) + len(end)
    # firmware update over Wi-Fi (the box serves /update, see tests/*/ota_update.h)
    otacard = ('    <div class="card" id="ota-card">' + NL +
               '      <h2>อัปเดตเฟิร์มแวร์</h2>' + NL +
               '      <p class="note">อัปโปรแกรมใหม่ลงกล่องผ่าน Wi-Fi ได้เลย ไม่ต้องถอดกล่องหรือต่อสาย · ดาวน์โหลดไฟล์ .bin ล่าสุดจาก GitHub (Releases) ลงมือถือก่อน แล้วกดปุ่มด้านล่าง · เฟิร์มแวร์ที่ใช้อยู่: <b id="fw-build">–</b></p>' + NL +
               '      <div class="acts"><a class="btn go" href="/update">อัปเดตเฟิร์มแวร์</a></div>' + NL +
               '    </div>' + NL)
    b = b[:k] + NL + card + b[k:]
    # the box's own setup (first-run wizard at /setup, tests/*/setup_wizard.h): name, language, Wi-Fi, wiring check
    setupcard = ('    <div class="card" id="setup-card">' + NL +
               '      <h2>ตั้งค่าเกจ</h2>' + NL +
               '      <p class="note">ชื่อเกจ ภาษา ชื่อและรหัสผ่าน Wi-Fi ของกล่อง และเช็คการต่อสาย K-Line · ค่าพวกนี้เก็บไว้ในกล่อง ใช้กับทุกมือถือ · ถ้าเปลี่ยน Wi-Fi กล่องจะรีสตาร์ท ให้มือถือต่อ Wi-Fi ใหม่</p>' + NL +
               '      <div class="acts"><a class="btn go" href="/setup">เปิดตัวช่วยตั้งค่า</a></div>' + NL +
               '    </div>' + NL)
    # setup + firmware cards go last on the settings page
    si = b.index('<section class="view" id="v-set"'); se = b.index('  </section>', si)
    b = b[:se] + setupcard + NL + otacard + NL + b[se:]
    b = b + NL + "<script>fetch('/api/version',{cache:'no-store'}).then(r=>r.json()).then(j=>{const e=document.getElementById('fw-build');if(e)e.textContent=j.build}).catch(()=>{});</script>"
    b = b + NL + kjs
    # first-run wizard (tests/*/setup_wizard.h): show the owner's gauge name (unless this phone set its own
    # app name in Settings, window.TG_APPNAME), and when the wizard was
    # saved again (ver changed) copy its language and app icon into this phone's settings once (theme + dial face are set in the app)
    b = b + NL + ("<script>fetch('/api/setup/look',{cache:'no-store'}).then(r=>r.json()).then(j=>{"
                  "window.TG_GEARBOX=j.gear==='auto'?'auto':'man';"
                  "if(j.name&&!window.TG_APPNAME){const e=document.querySelector('.brand .txt b');if(e)e.textContent=j.name;document.title=j.name;}"
                  "if(j.done&&j.sub!=null){window.TG_BOARDSUB=j.sub;if(!window.TG_APPSUB){const e=document.getElementById('brand-sub');if(e)e.textContent=j.sub;}}"   # wizard left it empty = no line under the name; null = never set (older setup)
                  "if(!j.done)return;const v=String(j.ver);let cur=null;try{cur=localStorage.getItem('tg-setup-v');}catch(e){return;}"
                  "if(cur===v)return;try{"
                  "localStorage.setItem('tg-lang',j.lang);"
                  "}catch(e){return;}"
                  "const fin=img=>{try{if(['bolt','thunder','dial','letter','custom'].includes(j.icon)&&(j.icon!=='custom'||img)){let a={};try{a=JSON.parse(localStorage.getItem('tg-app')||'{}')||{};}catch(e){}a.icon=j.icon;a.img=img||'';localStorage.setItem('tg-app',JSON.stringify(a));}"   # app icon picked in the wizard (custom = its picture)
                  "localStorage.setItem('tg-setup-v',v);}catch(e){return;}location.reload();};"
                  "if(j.icon==='custom')fetch('/api/setup/icon',{cache:'no-store'}).then(r=>r.text()).then(t=>fin(t.startsWith('data:image/')?t:'')).catch(()=>fin(''));else fin('');"
                  "}).catch(()=>{});</script>")

    return b
