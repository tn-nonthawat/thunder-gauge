// Firmware update over Wi-Fi (OTA), shared by thunder_gauge_lite and gauge_live - keep the two copies identical.
// Phone joins the gauge's Wi-Fi, opens http://192.168.4.1/update, picks the .bin and uploads it.
// The new firmware goes into the other app slot (app0/app1, 1.25 MB each); the ESP32 only switches to it
// when the whole image arrived and its checksum verified, so a dropped upload leaves the old firmware running.
// Only accepts an ESP32 app image (first byte 0xE9). K-Line reads pause while an upload runs.
#pragma once
#include <WebServer.h>
#include <Update.h>

extern WebServer server;
bool otaBusy = false;
bool otaSawFirst = false;      // have we checked the magic byte on the real first data chunk yet?
String otaErr;
const char* FW_BUILD = __DATE__ " " __TIME__;

static const char OTA_PAGE[] PROGMEM = R"HTML(<!doctype html><html lang="th"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>อัปเดตเฟิร์มแวร์</title>
<style>body{margin:0;font:15px system-ui,sans-serif;background:#0c0708;color:#e6edf2;padding:20px}
.c{max-width:440px;margin:auto;background:#170e10;border:1px solid #3a2027;border-radius:14px;padding:18px}
h1{font-size:19px;margin:0 0 6px}p{color:#9aa7b4;font-size:13.5px;line-height:1.5}
input{width:100%;margin:10px 0;color:#e6edf2}button{width:100%;padding:12px;font:600 15px system-ui;border-radius:10px;border:1px solid #ff7a1a;background:#3a1a12;color:#fff}
button:disabled{opacity:.5}.bar{height:10px;background:#21151a;border-radius:6px;overflow:hidden;margin:12px 0}
.bar i{display:block;height:100%;width:0;background:#ff7a1a}#m{font-weight:600}.ok{color:#4cc38a}.bad{color:#ff5a4e}a{color:#ffc531}</style></head>
<body><div class="c"><h1>อัปเดตเฟิร์มแวร์</h1>
<p>เฟิร์มแวร์ที่ใช้อยู่: <b>%BUILD%</b><br>เลือกไฟล์ <b>.bin</b> ของรุ่นนี้ แล้วกดอัปโหลด ระหว่างอัปให้เปิดหน้านี้ค้างไว้ เสร็จแล้วบอร์ดจะรีสตาร์ทเอง
ถ้าอัปไม่ครบ บอร์ดจะใช้เฟิร์มแวร์เดิมต่อ ไม่เสีย</p>
<input type="file" id="f" accept=".bin"><button id="b">อัปโหลด</button>
<div class="bar"><i id="p"></i></div><div id="m"></div><p><a href="/">กลับหน้าเกจ</a></p></div>
<script>
const f=document.getElementById('f'),b=document.getElementById('b'),p=document.getElementById('p'),m=document.getElementById('m');
b.onclick=()=>{const x=f.files[0];if(!x){m.textContent='ยังไม่ได้เลือกไฟล์';m.className='bad';return}
if(!/\.bin$/i.test(x.name)){m.textContent='ต้องเป็นไฟล์ .bin';m.className='bad';return}
b.disabled=true;m.className='';m.textContent='กำลังอัป… อย่าปิดหน้านี้';
const d=new FormData();d.append('fw',x,x.name);const r=new XMLHttpRequest();
r.upload.onprogress=e=>{if(e.lengthComputable){const q=Math.round(e.loaded*100/e.total);p.style.width=q+'%';m.textContent='กำลังอัป '+q+'%'}};
r.onload=()=>{if(r.status==200){p.style.width='100%';m.className='ok';m.textContent='สำเร็จ ✓ บอร์ดกำลังรีสตาร์ท รอ 10 วินาทีแล้วต่อ Wi-Fi ใหม่';setTimeout(()=>location.href='/',12000)}
else{m.className='bad';m.textContent='ไม่สำเร็จ: '+r.responseText+' · เฟิร์มแวร์เดิมยังใช้ได้';b.disabled=false}};
r.onerror=()=>{m.className='bad';m.textContent='การเชื่อมต่อหลุด · ลองใหม่ (เฟิร์มแวร์เดิมยังใช้ได้)';b.disabled=false};
r.open('POST','/update');r.send(d)};
</script></body></html>)HTML";

void otaPage() {
  String h = FPSTR(OTA_PAGE); h.replace("%BUILD%", FW_BUILD);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html; charset=utf-8", h);
}

void otaUpload() {
  HTTPUpload& u = server.upload();
  if (u.status == UPLOAD_FILE_START) {
    otaErr = ""; otaBusy = true; otaSawFirst = false;
    if (!u.filename.endsWith(".bin")) { otaErr = "ไม่ใช่ไฟล์ .bin"; return; }
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) otaErr = String("เริ่มไม่ได้: ") + Update.errorString();
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (otaErr.length()) return;
    // Check the ESP32 magic byte only on the TRUE first data chunk. Do NOT use Update.progress()==0
    // for this: progress() only advances once a full 4 KB flash sector is committed, so it stays 0
    // across the first few 1436-byte chunks and the check would wrongly re-run on later chunks (whose
    // first byte is not 0xE9), rejecting every real (multi-chunk) firmware. A one-shot flag is correct.
    if (!otaSawFirst && u.currentSize > 0) {
      otaSawFirst = true;
      if (u.buf[0] != 0xE9) { otaErr = "ไฟล์ไม่ใช่เฟิร์มแวร์ ESP32"; Update.abort(); return; }
    }
    if (Update.write(u.buf, u.currentSize) != u.currentSize) { otaErr = String("เขียนไม่ได้: ") + Update.errorString(); Update.abort(); }
  } else if (u.status == UPLOAD_FILE_END) {
    if (!otaErr.length() && !Update.end(true)) otaErr = String("ตรวจไฟล์ไม่ผ่าน: ") + Update.errorString();
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    Update.abort(); otaErr = "อัปโหลดถูกยกเลิก"; otaBusy = false;
  }
}

void otaDone() {
  if (otaErr.length() || Update.hasError()) {
    otaBusy = false;
    server.send(500, "text/plain; charset=utf-8", otaErr.length() ? otaErr : String(Update.errorString()));
    return;
  }
  server.send(200, "text/plain; charset=utf-8", "ok");
  delay(600);
  ESP.restart();
}

void otaBegin() {
  server.on("/update", HTTP_GET, otaPage);
  server.on("/update", HTTP_POST, otaDone, otaUpload);
  server.on("/api/version", HTTP_GET, []() { server.send(200, "application/json", String("{\"build\":\"") + FW_BUILD + "\"}"); });
}
