// First-run setup wizard at /setup: name the gauge and pick the language, set the gauge's own Wi-Fi
// name/password, and check the wiring (K-Line line level, ECU answer, and on the full build GPS + MPU-6050)
// before the real gauge page opens. Colour theme and dial face are left to the gauge's Settings page.
// The page is sent in parts: the GPS + MPU rows (SETUP_ROWS_FULL) only go out on the full build.
// Shared by thunder_gauge_lite (Lite) and gauge_live (full) - keep the two copies identical:
// edit firmware/thunder_gauge_lite/setup_wizard.h and copy it over tests/gauge_live/setup_wizard.h.
//
// Until setup is finished, "/" redirects to /setup (call suGate() at the top of the "/" handler).
// Settings live in LittleFS /setup.txt (key=value lines). The gauge page reads them from /api/setup.
// Forgot the Wi-Fi password? Hold the BOOT button (GPIO9) for 3 s while the board is running:
// the settings (and the shared screen setup /ui.json) are wiped and the board restarts with the default Wi-Fi and the wizard.
// (Do not hold BOOT while powering up: that starts the ESP32 download mode instead.)
// READ ONLY toward the ECU: the wiring check only uses the existing /api/status and /api/wake.
//
// Depends on kline_tester.h (jsonEsc, sendJson, logLine) - include this AFTER it in the .ino.
#pragma once
#include <WiFi.h>
#include <LittleFS.h>
#include <WebServer.h>

extern WebServer server;
extern bool fsOk;
extern const char* AP_SSID;
extern const char* AP_PASS;

static const char* SETUP_FILE = "/setup.txt";
static const int SU_BTN = 9;                      // BOOT button on the C3 SuperMini
bool suDone = false;
bool suSubSet = false;                            // false = never set by the wizard (older setup.txt): the gauge keeps its own default line
String suName, suSub, suSsid, suPass, suTheme = "raceline", suFace = "classic", suLang = "th", suIcon = "bolt", suModel = "auto", suGear = "man";
uint32_t suVer = 0;                               // bumps on every save so phones re-sync the look
uint32_t suRestartAt = 0, suBtnSince = 0;

static String suClean(String v, unsigned maxLen) {
  String o; for (char c : v) if ((uint8_t)c >= 0x20 && c != 0x7f) o += c;   // no control chars in the file or JSON
  v = o; v.trim();
  if (v.length() > maxLen) v = v.substring(0, maxLen);
  return v;
}

void suLoad() {
  suSsid = AP_SSID; suPass = AP_PASS;
  if (!fsOk || !LittleFS.exists(SETUP_FILE)) return;
  File f = LittleFS.open(SETUP_FILE, "r"); if (!f) return;
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.replace("\r", "");
    int e = l.indexOf('='); if (e < 1) continue;
    String k = l.substring(0, e), v = l.substring(e + 1);
    if (k == "done") suDone = v == "1";
    else if (k == "name") suName = v;
    else if (k == "sub") { suSub = v; suSubSet = true; }
    else if (k == "ssid" && v.length()) suSsid = v;
    else if (k == "pass" && v.length() >= 8) suPass = v;
    else if (k == "theme") suTheme = v;
    else if (k == "face") suFace = v;
    else if (k == "lang") suLang = v;
    else if (k == "icon") suIcon = v;
    else if (k == "model") suModel = v;
    else if (k == "gear") suGear = v;
    else if (k == "ver") suVer = v.toInt();
  }
  f.close();
}
bool suSave() {
  if (!fsOk) return false;
  File f = LittleFS.open("/setup.tmp", "w"); if (!f) return false;
  f.printf("done=%d\nname=%s\n%s%s%sssid=%s\npass=%s\ntheme=%s\nface=%s\nlang=%s\nicon=%s\nmodel=%s\ngear=%s\nver=%lu\n", suDone ? 1 : 0,
           suName.c_str(), suSubSet ? "sub=" : "", suSubSet ? suSub.c_str() : "", suSubSet ? "\n" : "", suSsid.c_str(), suPass.c_str(), suTheme.c_str(), suFace.c_str(), suLang.c_str(), suIcon.c_str(), suModel.c_str(), suGear.c_str(),
           (unsigned long)suVer);
  f.close();
  LittleFS.remove(SETUP_FILE);
  return LittleFS.rename("/setup.tmp", SETUP_FILE);
}

// call first in the "/" handler: returns true when it sent the phone to the wizard instead
bool suGate() {
  if (suDone) return false;
  server.sendHeader("Location", "/setup");
  server.sendHeader("Cache-Control", "no-store");
  server.send(302, "text/plain", "setup");
  return true;
}

static const char SETUP_PAGE_A[] PROGMEM = R"HTML(<!doctype html><html lang="th"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover"><meta name="theme-color" content="#0b0e14">
<title>ตั้งค่าเกจ</title><style>
:root{--bg:#0b0e14;--p:#141924;--p2:#1b2130;--ln:#2a3346;--fg:#e8edf6;--mu:#8d97ab;--a:#ff3b30;--a2:#ff7a1a;--ok:#3ccf7a;--wn:#f2d24b;--cr:#ff5a4e}
*{box-sizing:border-box}html,body{background:var(--bg);margin:0}
body{color:var(--fg);font:15px/1.5 "Leelawadee UI",Tahoma,system-ui,sans-serif;padding:20px max(16px,env(safe-area-inset-left)) 40px}
.w{max-width:460px;margin:0 auto}h1{font-size:22px;margin:4px 0 2px}h2{font-size:17px;margin:0 0 12px}
.sub{color:var(--mu);margin:0 0 16px;font-size:13px}
.dots{display:flex;gap:6px;margin:0 0 16px}.dots i{flex:1;height:4px;border-radius:4px;background:var(--ln)}.dots i.on{background:linear-gradient(90deg,var(--a),var(--a2))}
.c{background:var(--p);border:1px solid var(--ln);border-radius:14px;padding:16px;margin-bottom:14px}
label{display:block;font-size:13px;color:var(--mu);margin:12px 0 4px}
select{width:100%;font:inherit;color:var(--fg);background:var(--p2);border:1px solid var(--ln);border-radius:10px;padding:10px 12px}
input{width:100%;font:inherit;color:var(--fg);background:var(--p2);border:1px solid var(--ln);border-radius:10px;padding:10px 12px}
input:focus{outline:2px solid var(--a2);border-color:transparent}
.g{display:grid;grid-template-columns:repeat(auto-fill,minmax(120px,1fr));gap:8px}
.o{font:inherit;color:var(--fg);background:var(--p2);border:1px solid var(--ln);border-radius:10px;padding:10px;cursor:pointer;text-align:left;display:flex;align-items:center;gap:8px}
.o[aria-pressed=true]{border-color:var(--a2);background:color-mix(in srgb,var(--a2) 20%,var(--p2))}
.o i{width:18px;height:18px;border-radius:50%;flex:none}
.nav{display:flex;gap:10px;margin-top:4px}
.b{flex:1;font-family:inherit;font-weight:600;font-size:15px;color:var(--fg);background:var(--p2);border:1px solid var(--ln);border-radius:10px;padding:12px;cursor:pointer}
.b.go{border:0;background:linear-gradient(90deg,var(--a),var(--a2));color:#fff}.b:disabled{opacity:.5}
.r{display:flex;justify-content:space-between;align-items:center;gap:10px;padding:10px 0;border-top:1px solid var(--ln)}.r:first-of-type{border-top:0}
.r small{display:block;color:var(--mu);font-size:12px}.st{font-weight:600;white-space:nowrap}
.ok{color:var(--ok)}.wn{color:var(--wn)}.cr{color:var(--cr)}.mu{color:var(--mu)}
.err{color:var(--cr);font-size:13px;min-height:1.2em;margin:8px 0 0}.note{color:var(--mu);font-size:13px}
.sm{font-family:inherit;font-weight:600;font-size:13px;color:var(--fg);background:var(--p2);border:1px solid var(--ln);border-radius:8px;padding:6px 10px;cursor:pointer}
.g.ic{grid-template-columns:repeat(5,1fr);gap:6px}.icu img,.icu svg{width:44px;height:44px;border-radius:10px;display:block;object-fit:cover}.ic .o{flex-direction:column;justify-content:center;text-align:center;gap:6px;padding:10px 4px;font-size:12px}.ic svg{width:44px;height:44px;border-radius:10px;display:block}
[hidden]{display:none!important}
</style></head><body><div class="w">
<h1 data-t="ตั้งค่าเกจครั้งแรก">ตั้งค่าเกจครั้งแรก</h1>
<p class="sub" data-t="ตั้งค่าครั้งเดียว แก้ภายหลังได้ที่หน้าตั้งค่า (ปุ่มเฟือง)">ตั้งค่าครั้งเดียว แก้ภายหลังได้ที่หน้าตั้งค่า (ปุ่มเฟือง)</p>
<div class="dots" id="dots"><i></i><i></i><i></i></div>

<section class="c" data-s="0"><h2 data-t="1 · ชื่อเกจและภาษา">1 · ชื่อเกจและภาษา</h2>
<div class="g" id="lang"><button class="o" type="button" data-v="th">ไทย</button><button class="o" type="button" data-v="en">English</button></div>
<label for="nm" data-t="ชื่อสมาร์ทเกจของคุณ (แสดงบนหัวหน้าเกจ)">ชื่อสมาร์ทเกจของคุณ (แสดงบนหัวหน้าเกจ)</label>
<input id="nm" maxlength="24" placeholder="THUNDER GAUGE" autocomplete="off">
<label for="sb" data-t="คำใต้ชื่อ (เว้นว่างได้ ถ้าไม่ใส่จะไม่แสดง)">คำใต้ชื่อ (เว้นว่างได้ ถ้าไม่ใส่จะไม่แสดง)</label>
<input id="sb" maxlength="40" autocomplete="off">
<label for="md" data-t="รุ่นรถ / ตารางข้อมูลของกล่อง ECU">รุ่นรถ / ตารางข้อมูลของกล่อง ECU</label>
<select id="md"><option value="auto" data-t="ตรวจหาอัตโนมัติ (แนะนำ)">ตรวจหาอัตโนมัติ (แนะนำ)</option><option value="t17" data-t="Wave 110i / 125i · ตาราง 0x17">Wave 110i / 125i · ตาราง 0x17</option><option value="t67" data-t="ตาราง 0x67 · ทดลอง">ตาราง 0x67 · ทดลอง</option><option value="t10" data-t="ตาราง 0x10 (CBR และรุ่นใหญ่) · ทดลอง">ตาราง 0x10 (CBR และรุ่นใหญ่) · ทดลอง</option><option value="t11" data-t="ตาราง 0x11 · ทดลอง">ตาราง 0x11 · ทดลอง</option><option value="t13" data-t="ตาราง 0x13 · ทดลอง">ตาราง 0x13 · ทดลอง</option></select>
<p class="note" data-t="อัตโนมัติ: บอร์ดลองถามกล่องทีละตารางแล้วใช้ตารางแรกที่ตอบ ทดสอบกับรถจริงแล้วเฉพาะ Wave 110i รุ่นอื่นค่าบางตัวอาจไม่ตรง">อัตโนมัติ: บอร์ดลองถามกล่องทีละตารางแล้วใช้ตารางแรกที่ตอบ ทดสอบกับรถจริงแล้วเฉพาะ Wave 110i รุ่นอื่นค่าบางตัวอาจไม่ตรง</p>
<label data-t="ระบบเกียร์">ระบบเกียร์</label>
<div class="g" id="gear"><button class="o" type="button" data-v="man" data-t="เกียร์ธรรมดา / กึ่งออโต้ (มีเกียร์ว่าง N)">เกียร์ธรรมดา / กึ่งออโต้ (มีเกียร์ว่าง N)</button><button class="o" type="button" data-v="auto" data-t="เกียร์ออโต้ (สกู๊ตเตอร์ ไม่มีเกียร์ว่าง)">เกียร์ออโต้ (สกู๊ตเตอร์ ไม่มีเกียร์ว่าง)</button></div>
<label data-t="ไอคอนแอป (บนหัวหน้าเกจ และตอนเพิ่มลงหน้าจอโฮม)">ไอคอนแอป (บนหัวหน้าเกจ และตอนเพิ่มลงหน้าจอโฮม)</label>
<div class="g ic" id="icon"><button class="o" type="button" data-v="bolt"><svg viewBox="0 0 180 180"><defs><linearGradient id="wg" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#ffae42"/><stop offset="1" stop-color="#ff5a1f"/></linearGradient></defs><rect width="180" height="180" rx="40" fill="url(#wg)"/><g transform="translate(36 25) scale(3.6)"><path d="M19 1.5 4 20h8.5L9 34.5 26 14h-8.8L21.5 1.5z" fill="#fff" stroke="#fff" stroke-width="2" stroke-linejoin="round"/></g></svg><span data-t="สายฟ้าส้ม">สายฟ้าส้ม</span></button><button class="o" type="button" data-v="thunder"><svg viewBox="0 0 180 180"><rect width="180" height="180" rx="40" fill="#0b1530"/><g transform="translate(36 25) scale(3.6)"><path d="M19 1.5 4 20h8.5L9 34.5 26 14h-8.8L21.5 1.5z" fill="#0d1f4a" stroke="#3d9bff" stroke-width="2" stroke-linejoin="round"/><path d="M12.5 20 15.5 14" stroke="#2ec4b0" stroke-width="1.4" stroke-linecap="round"/><circle cx="15.5" cy="14" r="1.6" fill="#2ec4b0"/><circle cx="12.5" cy="20" r="1.3" fill="#f0a868"/></g></svg><span data-t="ธันเดอร์">ธันเดอร์</span></button><button class="o" type="button" data-v="dial"><svg viewBox="0 0 180 180"><rect width="180" height="180" rx="40" fill="#10161e"/><path d="M49 141A58 58 0 1 1 131 141" fill="none" stroke="#2a3a4c" stroke-width="14" stroke-linecap="round"/><path d="M148 100A58 58 0 0 1 131 141" fill="none" stroke="#ff5a4e" stroke-width="14" stroke-linecap="round"/><path d="M49 141A58 58 0 0 1 119 50" fill="none" stroke="#5fa8ff" stroke-width="14" stroke-linecap="round"/><line x1="90" y1="100" x2="117" y2="57" stroke="#e6edf2" stroke-width="7" stroke-linecap="round"/><circle cx="90" cy="100" r="10" fill="#e6edf2"/></svg><span data-t="หน้าปัด">หน้าปัด</span></button><button class="o" type="button" data-v="letter"><svg viewBox="0 0 180 180"><rect width="180" height="180" rx="40" fill="#3d9bff"/><text id="icl" x="90" y="126" text-anchor="middle" font-family="sans-serif" font-weight="700" font-size="104" fill="#fff">T</text></svg><span data-t="ตัวอักษร">ตัวอักษร</span></button><button class="o" type="button" data-v="custom"><span class="icu" id="icu"><svg viewBox="0 0 180 180"><rect width="180" height="180" rx="40" fill="#1b2130" stroke="#2a3346" stroke-width="6"/><path d="M90 52v76M52 90h76" stroke="#8d97ab" stroke-width="12" stroke-linecap="round"/></svg></span><span data-t="รูปของคุณ">รูปของคุณ</span></button></div>
<input type="file" id="icf" accept="image/*" hidden>
</section>

<section class="c" data-s="1" hidden><h2 data-t="2 · Wi-Fi ของเกจ">2 · Wi-Fi ของเกจ</h2>
<p class="note" data-t="ชื่อและรหัสที่มือถือใช้ต่อเข้าเกจ เปลี่ยนจากค่าเริ่มต้นเพื่อไม่ให้คนอื่นเข้าได้">ชื่อและรหัสที่มือถือใช้ต่อเข้าเกจ เปลี่ยนจากค่าเริ่มต้นเพื่อไม่ให้คนอื่นเข้าได้</p>
<label for="ss" data-t="ชื่อ Wi-Fi">ชื่อ Wi-Fi</label><input id="ss" maxlength="32" autocomplete="off" autocapitalize="off">
<label for="pw" data-t="รหัสผ่านใหม่ (8 ตัวขึ้นไป)">รหัสผ่านใหม่ (8 ตัวขึ้นไป)</label><input id="pw" maxlength="63" autocomplete="new-password" autocapitalize="off">
<p class="note" id="pwk" hidden data-t="เว้นว่างไว้ = ใช้รหัสเดิม">เว้นว่างไว้ = ใช้รหัสเดิม</p>
<p class="err" id="e1"></p>
</section>

<section class="c" data-s="2" hidden><h2 data-t="3 · เช็คการต่อสาย">3 · เช็คการต่อสาย</h2>
<p class="note" data-t="บิดกุญแจ ON (ไม่ต้องติดเครื่อง) แล้วรอสักครู่">บิดกุญแจ ON (ไม่ต้องติดเครื่อง) แล้วรอสักครู่</p>
<div class="r"><div><span data-t="สาย K-Line">สาย K-Line</span><small id="klh"></small></div><span class="st mu" id="kl">…</span></div>
<div class="r"><div><span data-t="กล่อง ECU">กล่อง ECU</span><small id="ech"></small></div><span class="st mu" id="ec">…</span></div>
)HTML";
#ifdef TG_FULL_BUILD
static const char SETUP_ROWS_FULL[] PROGMEM = R"HTML(<div class="r" id="r-mpu"><div><span data-t="เซ็นเซอร์เอียง MPU-6050">เซ็นเซอร์เอียง MPU-6050</span><small id="mph"></small></div><span class="st mu" id="mp">…</span></div>
<div class="r" id="r-gps"><div><span>GPS</span><small id="gph"></small></div><span class="st mu" id="gp">…</span></div>
)HTML";
#endif
static const char SETUP_PAGE_B[] PROGMEM = R"HTML(<div class="nav" style="margin-top:10px"><button class="sm" type="button" id="wake" data-t="ลองปลุก ECU อีกครั้ง">ลองปลุก ECU อีกครั้ง</button></div>
<p class="note" data-t="ข้ามขั้นนี้ได้ถ้ายังไม่ได้ต่อเข้ารถ">ข้ามขั้นนี้ได้ถ้ายังไม่ได้ต่อเข้ารถ</p>
</section>

<section class="c" data-s="3" hidden><h2 id="dh" data-t="เสร็จแล้ว">เสร็จแล้ว</h2><p id="dm"></p></section>

<p class="err" id="e9"></p>
<div class="nav" id="nav"><button class="b" type="button" id="bk" data-t="ย้อนกลับ">ย้อนกลับ</button><button class="b go" type="button" id="nx" data-t="ถัดไป">ถัดไป</button></div>
</div><script>
(function(){
const $=id=>document.getElementById(id);
const EN={"ระบบเกียร์":"Gearbox","เกียร์ธรรมดา / กึ่งออโต้ (มีเกียร์ว่าง N)":"Manual / semi-auto (has neutral N)","เกียร์ออโต้ (สกู๊ตเตอร์ ไม่มีเกียร์ว่าง)":"Automatic (scooter, no neutral)","รุ่นรถ / ตารางข้อมูลของกล่อง ECU":"Bike model / ECU data table","อัตโนมัติ: บอร์ดลองถามกล่องทีละตารางแล้วใช้ตารางแรกที่ตอบ ทดสอบกับรถจริงแล้วเฉพาะ Wave 110i รุ่นอื่นค่าบางตัวอาจไม่ตรง":"Auto: the board asks the ECU table by table and uses the first that answers. Only the Wave 110i is tested on a real bike; on other models some readings may be off.","ตรวจหาอัตโนมัติ (แนะนำ)":"Detect automatically (recommended)","ตาราง 0x67 · ทดลอง":"Table 0x67 · experimental","ตาราง 0x10 (CBR และรุ่นใหญ่) · ทดลอง":"Table 0x10 (CBR, big bikes) · experimental","ตาราง 0x11 · ทดลอง":"Table 0x11 · experimental","ตาราง 0x13 · ทดลอง":"Table 0x13 · experimental","Wave 110i / 125i · ตาราง 0x17":"Wave 110i / 125i · table 0x17","ไอคอนแอป (บนหัวหน้าเกจ และตอนเพิ่มลงหน้าจอโฮม)":"App icon (gauge header and home-screen shortcut)","สายฟ้าส้ม":"Orange bolt","ธันเดอร์":"Thunder","หน้าปัด":"Dial","ตัวอักษร":"Letter","รูปของคุณ":"Your picture","ตั้งค่าเกจครั้งแรก":"First-time setup","ตั้งค่าครั้งเดียว แก้ภายหลังได้ที่หน้าตั้งค่า (ปุ่มเฟือง)":"Set once. Change it later in Settings (gear button).","1 · ชื่อเกจและภาษา":"1 · Name and language","ชื่อสมาร์ทเกจของคุณ (แสดงบนหัวหน้าเกจ)":"Your gauge's name (shown at the top of the gauge)","คำใต้ชื่อ (เว้นว่างได้ ถ้าไม่ใส่จะไม่แสดง)":"Line under the name (optional, left empty = none)","2 · Wi-Fi ของเกจ":"2 · Gauge Wi-Fi","ชื่อและรหัสที่มือถือใช้ต่อเข้าเกจ เปลี่ยนจากค่าเริ่มต้นเพื่อไม่ให้คนอื่นเข้าได้":"The name and password your phone uses to join the gauge. Change the default so others can't join.","ชื่อ Wi-Fi":"Wi-Fi name","รหัสผ่านใหม่ (8 ตัวขึ้นไป)":"New password (8+ characters)","เว้นว่างไว้ = ใช้รหัสเดิม":"Leave empty to keep the current one","3 · เช็คการต่อสาย":"3 · Wiring check","บิดกุญแจ ON (ไม่ต้องติดเครื่อง) แล้วรอสักครู่":"Turn the key ON (engine off) and wait a moment.","สาย K-Line":"K-Line wire","กล่อง ECU":"ECU","เซ็นเซอร์เอียง MPU-6050":"MPU-6050 lean sensor","ลองปลุก ECU อีกครั้ง":"Wake the ECU again","ข้ามขั้นนี้ได้ถ้ายังไม่ได้ต่อเข้ารถ":"You can skip this if it's not on the bike yet.","เสร็จแล้ว":"All set","ย้อนกลับ":"Back","ถัดไป":"Next","ข้าม":"Skip","บันทึกและเริ่มใช้":"Save and start","เปิดหน้าเกจ":"Open the gauge","ใส่ชื่อ Wi-Fi":"Enter a Wi-Fi name","รหัสต้องมี 8–63 ตัว":"The password needs 8–63 characters","บันทึกไม่สำเร็จ ลองอีกครั้ง":"Saving failed, try again","ปกติ ✓":"OK ✓","ไม่มีไฟ ✗":"No signal ✗","แกว่ง":"Unstable","ตอบกลับ ✓":"Answering ✓","ยังไม่ตอบ":"No answer","กำลังปลุก…":"Waking…","ไม่พบ ✗":"Not found ✗","จับได้ ✓":"Fix ✓","รอดาวเทียม":"Searching","มีไฟรอที่สาย ต่อถูกแล้ว":"Line is powered, wired right","ไม่มีไฟ: เช็คไฟเลี้ยงโมดูล K-Line และสายเข้า ECU":"No power: check the K-Line module supply and the ECU wire","ไฟไม่นิ่ง: เช็คสายกราวด์และขั้วต่อ":"Unsteady: check the ground and connectors","อ่านค่าได้ พร้อมใช้":"Reading data, ready","บิดกุญแจ ON แล้วกดลองปลุกอีกครั้ง":"Key ON, then tap Wake again","เช็คสาย SDA→GPIO4, SCL→GPIO10":"Check SDA→GPIO4, SCL→GPIO10","ต้องอยู่กลางแจ้ง อาจใช้เวลา 1–5 นาที":"Needs open sky, may take 1–5 minutes","ดวง":"sats"};
let lang="th";const T=s=>lang==="en"&&EN[s]?EN[s]:s;
const cfg={name:"",sub:"",ssid:"",pass:"",lang:"th",icon:"bolt",img:"",model:"auto",gear:"man"};let orig={},full=!!$("r-gps"),step=0,timer=0;   // theme + dial face: set later in the gauge's Settings
function icl(){const c=(Array.from($("nm").value||"").find(c=>!/[\sเแโใไ<&>"]/.test(c))||"T").toUpperCase();$("icl").textContent=c;}
function icImg(u){$("icu").innerHTML='<img alt="" src="'+u+'">';}
$("icf").addEventListener("change",e=>{const f=e.target.files&&e.target.files[0];e.target.value="";if(!f)return;const u=URL.createObjectURL(f),im=new Image();im.onload=()=>{const c=document.createElement("canvas"),z=Math.min(im.naturalWidth,im.naturalHeight),x=c.getContext("2d");c.width=c.height=180;x.fillStyle="#10161e";x.fillRect(0,0,180,180);x.drawImage(im,(im.naturalWidth-z)/2,(im.naturalHeight-z)/2,z,z,0,0,180,180);URL.revokeObjectURL(u);cfg.img=c.toDataURL("image/jpeg",.85);icImg(cfg.img);pick("icon","custom");};im.src=u;});
function pick(g,v){document.querySelectorAll("#"+g+" .o").forEach(b=>b.setAttribute("aria-pressed",b.dataset.v===v));cfg[g]=v;}
["lang","icon","gear"].forEach(g=>$(g).addEventListener("click",e=>{const b=e.target.closest(".o");if(!b)return;if(g==="icon"&&b.dataset.v==="custom"&&(!cfg.img&&!orig.hasImg||e.target.closest(".icu"))){$("icf").click();return;}pick(g,b.dataset.v);if(g==="lang"){lang=b.dataset.v;tr();}}));
function tr(){document.documentElement.lang=lang;document.querySelectorAll("[data-t]").forEach(e=>e.textContent=T(e.dataset.t));nav();}
function nav(){document.querySelectorAll("section[data-s]").forEach(s=>s.hidden=+s.dataset.s!==step);
document.querySelectorAll("#dots i").forEach((d,i)=>d.className=i<=step?"on":"");
$("bk").style.visibility=step>0&&step<3?"visible":"hidden";$("nav").hidden=step===3;
$("nx").textContent=T(step===2?"บันทึกและเริ่มใช้":"ถัดไป");
clearInterval(timer);if(step===2){check();timer=setInterval(check,1500);}}
function set(id,cls,txt,hint){const e=$(id);e.className="st "+cls;e.textContent=T(txt);if(hint!=null)$(id+"h").textContent=T(hint);}
const J=u=>fetch(u,{cache:"no-store"}).then(r=>{if(!r.ok)throw 0;return r.json();});
function check(){
J("/api/status").then(j=>{const p=j.line;p>=90?set("kl","ok","ปกติ ✓","มีไฟรอที่สาย ต่อถูกแล้ว"):p<=10?set("kl","cr","ไม่มีไฟ ✗","ไม่มีไฟ: เช็คไฟเลี้ยงโมดูล K-Line และสายเข้า ECU"):set("kl","wn","แกว่ง","ไฟไม่นิ่ง: เช็คสายกราวด์และขั้วต่อ");
j.awake?set("ec","ok","ตอบกลับ ✓","อ่านค่าได้ พร้อมใช้"):$("ec").textContent!==T("กำลังปลุก…")&&set("ec","wn","ยังไม่ตอบ","บิดกุญแจ ON แล้วกดลองปลุกอีกครั้ง");}).catch(()=>{});
if(full)J("/data").then(d=>{d.mpu?set("mp","ok","ปกติ ✓",""):set("mp","cr","ไม่พบ ✗","เช็คสาย SDA→GPIO4, SCL→GPIO10");
d.fix?set("gp","ok","จับได้ ✓",d.sats+" "+T("ดวง")):set("gp","wn","รอดาวเทียม",T("ต้องอยู่กลางแจ้ง อาจใช้เวลา 1–5 นาที")+" · "+d.sats+" "+T("ดวง"));}).catch(()=>{});}
$("wake").onclick=()=>{set("ec","mu","กำลังปลุก…");$("wake").disabled=true;J("/api/wake").then(j=>j.awake?set("ec","ok","ตอบกลับ ✓","อ่านค่าได้ พร้อมใช้"):set("ec","wn","ยังไม่ตอบ","บิดกุญแจ ON แล้วกดลองปลุกอีกครั้ง")).catch(()=>set("ec","wn","ยังไม่ตอบ")).then(()=>$("wake").disabled=false);};
$("bk").onclick=()=>{step--;nav();};
$("nx").onclick=()=>{
if(step===1){const s=$("ss").value.trim(),p=$("pw").value;if(!s){$("e1").textContent=T("ใส่ชื่อ Wi-Fi");return;}
if((p.length||!orig.done)&&(p.length<8||p.length>63)){$("e1").textContent=T("รหัสต้องมี 8–63 ตัว");return;}$("e1").textContent="";}
if(step<2){step++;nav();return;}save();};
function save(){$("e9").textContent="";cfg.model=$("md").value;cfg.name=$("nm").value.trim();cfg.sub=$("sb").value.trim();cfg.ssid=$("ss").value.trim();cfg.pass=$("pw").value;$("nx").disabled=true;
fetch("/api/setup/save",{method:"POST",body:new URLSearchParams(cfg.icon==="custom"?cfg:Object.assign({},cfg,{img:""}))}).then(r=>r.json()).then(j=>{if(!j.ok)throw 0;
try{localStorage.setItem("tg-lang",cfg.lang);}catch(e){}
step=3;nav();clearInterval(timer);
if(j.restart){$("dm").innerHTML=(lang==="en"?"The gauge is restarting with the new Wi-Fi. On your phone, join <b>":"เกจกำลังรีสตาร์ทด้วย Wi-Fi ใหม่ ให้มือถือต่อ Wi-Fi ชื่อ <b>")+esc(cfg.ssid)+(lang==="en"?"</b>, then open <b>http://192.168.4.1</b>":"</b> แล้วเปิด <b>http://192.168.4.1</b>")+`<br><br><button class="b go" type="button" onclick="location.href='/'">${T("เปิดหน้าเกจ")}</button>`;}
else{$("dm").innerHTML=`<button class="b go" type="button" onclick="location.href='/'">${T("เปิดหน้าเกจ")}</button>`;}
}).catch(()=>{$("nx").disabled=false;$("e9").textContent=T("บันทึกไม่สำเร็จ ลองอีกครั้ง");});}
const esc=s=>s.replace(/[&<>"]/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;"}[c]));
$("nm").addEventListener("input",icl);
J("/api/setup").then(j=>{orig=j;$("nm").value=j.name||"";icl();orig.hasImg=j.icon==="custom";if(j.model)$("md").value=j.model;pick("gear",j.gear==="auto"?"auto":"man");pick("icon",["bolt","thunder","dial","letter","custom"].includes(j.icon)?j.icon:"bolt");if(j.icon==="custom")fetch("/api/setup/icon").then(r=>r.text()).then(t=>{if(t.startsWith("data:image/"))icImg(t);}).catch(()=>{});$("sb").value=j.sub||"";$("ss").value=j.ssid||"";$("pwk").hidden=!j.done;
lang=j.lang==="en"?"en":"th";pick("lang",lang);tr();}).catch(()=>{pick("lang","th");pick("icon","bolt");pick("gear","man");tr();});
})();
</script></body></html>)HTML";

void suPage() {
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html; charset=utf-8", "");
  server.sendContent_P(SETUP_PAGE_A);
#ifdef TG_FULL_BUILD
  server.sendContent_P(SETUP_ROWS_FULL);        // GPS + MPU-6050 rows: the Lite board has neither
#endif
  server.sendContent_P(SETUP_PAGE_B);
  server.sendContent("");                        // end of the chunked reply
}

// Never sends the password back.
void suApiGet() {
#ifdef TG_FULL_BUILD
  const char* full = "true";
#else
  const char* full = "false";
#endif
  sendJson(String("{\"done\":") + (suDone ? "true" : "false") + ",\"full\":" + full +
           ",\"name\":\"" + jsonEsc(suName) + "\",\"sub\":" + (suSubSet ? String("\"") + jsonEsc(suSub) + "\"" : String("null")) + ",\"ssid\":\"" + jsonEsc(suSsid) +
           "\",\"theme\":\"" + jsonEsc(suTheme) + "\",\"face\":\"" + jsonEsc(suFace) + "\",\"lang\":\"" + jsonEsc(suLang) + "\",\"icon\":\"" + jsonEsc(suIcon) + "\",\"model\":\"" + jsonEsc(suModel) + "\",\"gear\":\"" + jsonEsc(suGear) +
           "\",\"ver\":" + String(suVer) + "}");
}
// Called from the gauge page: name + look only, never the password.
void suApiLook() {
  sendJson(String("{\"done\":") + (suDone ? "true" : "false") + ",\"name\":\"" + jsonEsc(suName) +
           "\",\"sub\":" + (suSubSet ? String("\"") + jsonEsc(suSub) + "\"" : String("null")) + ",\"theme\":\"" + jsonEsc(suTheme) + "\",\"face\":\"" + jsonEsc(suFace) + "\",\"lang\":\"" + jsonEsc(suLang) + "\",\"icon\":\"" + jsonEsc(suIcon) + "\",\"model\":\"" + jsonEsc(suModel) + "\",\"gear\":\"" + jsonEsc(suGear) +
           "\",\"ver\":" + String(suVer) + "}");
}

void suApiSave() {
  String ss = suClean(server.arg("ssid"), 32), pw = server.arg("pass");
  { String o; for (char c : pw) if ((uint8_t)c >= 0x20 && c != 0x7f) o += c; pw = o; }
  if (!pw.length() && suDone) pw = suPass;          // re-run of the wizard: empty = keep the current password
  if (!ss.length() || pw.length() < 8 || pw.length() > 63) { sendJson("{\"ok\":false,\"err\":\"wifi\"}"); return; }
  String th = suClean(server.arg("theme"), 12), fc = suClean(server.arg("face"), 10), lg = suClean(server.arg("lang"), 2);   // the wizard no longer sends theme/face: kept as they are
  bool wifiChanged = ss != suSsid || pw != suPass;
  suName = suClean(server.arg("name"), 96);         // the page caps it at 24 characters (Thai = 3 bytes each)
  suSub = suClean(server.arg("sub"), 160); suSubSet = true;          // line under the name, page caps it at 40 characters
  suSsid = ss; suPass = pw;
  if (th.length()) suTheme = th;
  if (fc == "sport" || fc == "classic" || fc == "mini") suFace = fc;
  if (lg == "th" || lg == "en") suLang = lg;
  String md = suClean(server.arg("model"), 6);
  if (md == "auto" || md == "t17" || md == "t67" || md == "t10" || md == "t11" || md == "t13") { suModel = md; klModel = md; awake = false; }   // re-pick the tables on the next wake
  String gb = suClean(server.arg("gear"), 4);
  if (gb == "man" || gb == "auto") suGear = gb;
  String ic = suClean(server.arg("icon"), 8);
  if (ic == "bolt" || ic == "thunder" || ic == "dial" || ic == "letter") suIcon = ic;
  if (ic == "custom" && fsOk) {                     // own picture: a new one comes with the form, an old one stays in /icon.txt
    const String& im = server.arg("img");
    if (im.startsWith("data:image/") && im.length() < 60000) { File f = LittleFS.open("/icon.txt", "w"); if (f) { f.print(im); f.close(); } }
    if (LittleFS.exists("/icon.txt")) suIcon = ic;
  }
  suDone = true; suVer++;
  if (!suSave()) { suDone = false; sendJson("{\"ok\":false,\"err\":\"fs\"}"); return; }
  logLine("ตั้งค่าเกจแล้ว: " + suName + " · Wi-Fi " + suSsid);
  sendJson(String("{\"ok\":true,\"ver\":") + suVer + ",\"restart\":" + (wifiChanged ? "true" : "false") + "}");
  if (wifiChanged) suRestartAt = millis() + 1500;  // let the answer reach the phone first
}

// call in setup() right after LittleFS.begin() and before WiFi.softAP(suSsid, suPass)
void suBegin() {
  pinMode(SU_BTN, INPUT_PULLUP);
  suLoad();
  klModel = suModel;
}
// call after the other server.on() routes
void suApiIcon() {                                  // the wizard's own picture icon (data: URL), "" if none
  server.sendHeader("Cache-Control", "no-store");
  if (fsOk && LittleFS.exists("/icon.txt")) { File f = LittleFS.open("/icon.txt", "r"); server.streamFile(f, "text/plain"); f.close(); }
  else server.send(200, "text/plain", "");
}
void suRoutes() {
  server.on("/setup", HTTP_GET, suPage);
  server.on("/api/setup", HTTP_GET, suApiGet);
  server.on("/api/setup/look", HTTP_GET, suApiLook);
  server.on("/api/setup/icon", HTTP_GET, suApiIcon);
  server.on("/api/setup/save", HTTP_POST, suApiSave);
}
void suLoop() {
  if (suRestartAt && (int32_t)(millis() - suRestartAt) >= 0) ESP.restart();
  if (digitalRead(SU_BTN) == LOW) {                 // BOOT held 3 s = back to factory Wi-Fi + wizard
    if (!suBtnSince) suBtnSince = millis();
    else if (millis() - suBtnSince > 3000) {
      if (fsOk) { LittleFS.remove(SETUP_FILE); LittleFS.remove("/ui.json"); LittleFS.remove("/ui.ver"); LittleFS.remove("/icon.txt"); }   // also the shared screen setup
      logLine("กด BOOT ค้าง: ล้างค่าตั้งเกจ กลับเป็นค่าเริ่มต้น");
      delay(200); ESP.restart();
    }
  } else suBtnSince = 0;
}
