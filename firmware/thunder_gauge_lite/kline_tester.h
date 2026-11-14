// K-Line link to the Honda ECU, shared by the Lite build (thunder_gauge_lite) and the full build (gauge_live).
// Keep the two copies identical: edit firmware/thunder_gauge_lite/kline_tester.h and copy it over.
// The UI is the dashboard (/api/live) and the "ทดสอบ K-Line" card on the system tab (/api/* test calls).
// READ ONLY: sends only the Honda wake/init frames, "read table" requests (72 05 71 <table>) and
// "read fault code page" requests (72 05 73/74 <page>). Never writes, never clears codes.
// K-Line module: module RX <- GPIO21, module TX -> GPIO20, 10400 bps.
// The Lite build uses UART1; the full build uses UART0 (GPS has UART1): define KL_SERIAL before including.
// Backup wake: NPN 2N3904, base <- 4.7k <- GPIO8, collector -> K-Line, emitter -> GND.
// On the C3 SuperMini GPIO8 also drives the blue LED (LED on = transistor off).
#pragma once
#include <WebServer.h>
#include <LittleFS.h>
#ifndef KL_SERIAL
#define KL_SERIAL Serial1
#endif

extern WebServer server;
extern bool fsOk;

const int KL_RX = 20, KL_TX = 21, WAKE_NPN = 8;
const uint32_t KL_BAUD = 10400;
const char* LOG_FILE = "/kline_log.txt";
bool awake = false;
bool quiet = false;                 // background retries: keep the log readable
uint32_t bgTries = 0, lastApi = 0;
String logBuf;
uint8_t lastData[3][64]; int lastLen[3] = {0, 0, 0};   // last answer per table slot, to log only changes
// Engine data table: the Wave 110i ECU leaves 0x11 empty and sends its engine data in 0x17 (19 bytes, found
// 2026-11-09). Other Honda PGM-FI ECUs answer other tables (0x67, 0x10/0x11, 0x13...: see the decoder in the page,
// ecu_board.py). klModel comes from the setup wizard (setup.txt model=): "auto" asks the tables below after each
// wake and keeps the first that answers with data; "tXX" fixes table 0xXX. Read requests only, as everywhere here.
const uint8_t LIVE_CANDS[] = {0x17, 0x67, 0x10, 0x11, 0x13, 0x60, 0x61, 0x63};
const uint8_t O2_CANDS[] = {0x20, 0x70};
uint8_t LIVE_TABLE = 0x17;
String klModel = "auto";
uint8_t liveData[64]; int liveLen = 0; uint32_t liveAt = 0;  // newest LIVE_TABLE answer, served to the dashboard at /api/live
void keepLive(const uint8_t* d, int n) { memcpy(liveData, d, n); liveLen = n; liveAt = millis(); }
// Table 0x20 (3 bytes) on the Wave 110i: byte 0 swings fast at warm idle (O2 sensor), byte 1 sits at 128 cold
// and drifts 121-126 warm (fuel trim). Read alongside the main table and passed through raw.
uint8_t O2_TABLE = 0x20;
uint8_t o2Data[8]; int o2Len = 0;
// Table 0xD1 (6 bytes): digital status. Confirmed on the Wave 110i 2026-11-09: byte 0 = neutral switch
// (01 neutral, 00 in gear), byte 4 = fuel pump (01 running). Passed through raw; the page decodes it.
const uint8_t STATUS_TABLE = 0xD1;
uint8_t stData[8]; int stLen = 0;

// ---------------------------------------------------------------- helpers
void logLine(const String& s) {
  String l = String(millis() / 1000.0f, 1) + "s  " + s;
  Serial.println(l);
  if (quiet) return;
  logBuf += l + "\n";
  if (logBuf.length() > 9000) logBuf.remove(0, logBuf.length() - 7000);
  if (fsOk) {
    File f = LittleFS.open(LOG_FILE, "a");
    if (f) { if (f.size() < 65536) f.println(l); f.close(); }
  }
}
String hexs(const uint8_t* b, int n) {
  String s; char t[4];
  for (int i = 0; i < n; i++) { snprintf(t, sizeof t, "%02X ", b[i]); s += t; }
  s.trim(); return s;
}
uint8_t cks(const uint8_t* b, int n) { uint8_t s = 0; for (int i = 0; i < n; i++) s += b[i]; return (uint8_t)(0x100 - s); }
bool sumOk(const uint8_t* b, int n) { uint8_t s = 0; for (int i = 0; i < n; i++) s += b[i]; return s == 0; }
String jsonEsc(const String& s) {
  String o; for (char c : s) { if (c == '"' || c == '\\') { o += '\\'; o += c; } else if (c == '\n') o += "\\n"; else o += c; } return o;
}

void uartOn() { KL_SERIAL.begin(KL_BAUD, SERIAL_8N1, KL_RX, KL_TX); }
void uartOff() { KL_SERIAL.end(); pinMode(KL_TX, OUTPUT); digitalWrite(KL_TX, HIGH); pinMode(KL_RX, INPUT); }
void drain() { while (KL_SERIAL.available()) KL_SERIAL.read(); }

// How much of the time the K-Line sits high (idle) - 100 = line powered and free, 0 = no 12V or shorted.
int lineHighPct() {
  uartOff();
  int h = 0; for (int i = 0; i < 200; i++) { h += digitalRead(KL_RX); delayMicroseconds(250); }
  uartOn();
  return h / 2;
}

// Send a frame and swallow our own echo (K-Line is one wire: everything we send comes back on RX).
bool sendFrame(const uint8_t* b, int n) {
  drain();
  KL_SERIAL.write(b, n); KL_SERIAL.flush();
  uint8_t e[32]; int got = 0; uint32_t t = millis();
  while (got < n && millis() - t < 80) if (KL_SERIAL.available()) e[got++] = KL_SERIAL.read();
  if (got < n) {
    logLine("  ส่ง " + hexs(b, n) + "  ✗ ไม่ได้ยินเสียงสะท้อน (" + String(got) + "/" + String(n) + " ไบต์) → เช็คสาย TX/RX หรือโมดูลไม่มีไฟ 12V");
    return false;
  }
  if (memcmp(e, b, n) != 0) { logLine("  ส่ง " + hexs(b, n) + "  ✗ เสียงสะท้อนเพี้ยน: " + hexs(e, n)); return false; }
  logLine("  ส่ง " + hexs(b, n) + "  ✓");
  return true;
}

// Honda answer: [type][length of whole frame][...][checksum]. Returns bytes read (negative = incomplete).
int readReply(uint8_t* r, int cap, uint32_t waitMs = 300) {
  int got = 0, need = 2; uint32_t t = millis();
  while (millis() - t < waitMs) {
    if (!KL_SERIAL.available()) continue;
    r[got++] = KL_SERIAL.read(); t = millis();
    if (got == 2) { need = r[1]; if (need < 3 || need > cap) return -got; }
    if (got >= 2 && got >= need) return got;
  }
  return -got;
}

// ---------------------------------------------------------------- wake
// Pull K-Line low for lowMs, release for highMs, then wake frame + init frame. ECU should answer 02 04 00 FA.
bool wakeOnce(bool npn, int lowMs, int highMs) {
  logLine(String("ปลุก ECU ") + (npn ? "ด้วยทรานซิสเตอร์ (GPIO8)" : "ผ่านโมดูล") + " · ดึงลง " + lowMs + " ms · ปล่อย " + highMs + " ms");
  uartOff();
  int heldMs = -1;                       // when the line came back up by itself (chip time-out)
  uint32_t t0 = millis();
  if (npn) digitalWrite(WAKE_NPN, HIGH); else digitalWrite(KL_TX, LOW);
  delay(2);
  while (millis() - t0 < (uint32_t)lowMs) if (heldMs < 0 && digitalRead(KL_RX)) heldMs = millis() - t0;
  if (npn) digitalWrite(WAKE_NPN, LOW); else digitalWrite(KL_TX, HIGH);
  if (heldMs >= 0 && heldMs <= 3) logLine(npn ? "  ✗ สายไม่ลงเลย → ยังไม่ได้ต่อทรานซิสเตอร์ หรือต่อผิดขา"
                                               : "  ✗ สายไม่ลงเลย → โมดูลไม่มีไฟ 12V, สาย TX ไม่ถึงโมดูล หรือขา EN ไม่ได้ต่อ 3V3");
  else if (heldMs > 3) logLine("  ⚠ สายถูกดึงลงได้แค่ ~" + String(heldMs) + " ms แล้วเด้งกลับ (ชิปตัดสัญญาณ) → ควรลองแบบทรานซิสเตอร์");
  else logLine("  ดึงสายลงได้ครบ " + String(lowMs) + " ms ✓");
  delay(highMs);
  uartOn(); drain();

  const uint8_t wake[] = {0xFE, 0x04, 0xFF, 0xFF};
  if (!sendFrame(wake, 4)) return false;
  delay(20);
  const uint8_t init[] = {0x72, 0x05, 0x00, 0xF0, 0x99};
  if (!sendFrame(init, 5)) return false;
  uint8_t r[64]; int n = readReply(r, sizeof r);
  if (n <= 0) { logLine("  ✗ ECU ไม่ตอบ" + String(n < 0 ? " (ได้มาบางส่วน " + hexs(r, -n) + ")" : "")); return false; }
  logLine("  รับ " + hexs(r, n) + (sumOk(r, n) ? "" : "  (checksum ผิด)"));
  if (n == 4 && r[0] == 0x02 && r[1] == 0x04 && r[2] == 0x00 && r[3] == 0xFA) { logLine("  ✓ ECU ตื่นแล้ว"); return true; }
  logLine("  ? ECU ตอบ แต่ไม่ใช่ 02 04 00 FA ที่คาดไว้ - ส่งผลนี้ให้ดูด้วย");
  return false;
}

// Auto: try a few timings, module first, then the transistor.
const int TIMINGS[][2] = {{70, 120}, {70, 130}, {70, 200}, {100, 200}, {50, 130}};
bool wakeAuto(bool allowNpn) {
  for (auto& t : TIMINGS) { if (wakeOnce(false, t[0], t[1])) return true; delay(300); }
  if (!allowNpn) { logLine("ปลุกผ่านโมดูลไม่ติดทุกจังหวะ"); return false; }
  logLine("ผ่านโมดูลไม่ติด → ลองทรานซิสเตอร์ (ถ้ายังไม่ได้ต่อ วงจรนี้จะไม่มีผล)");
  for (auto& t : TIMINGS) { if (wakeOnce(true, t[0], t[1])) return true; delay(300); }
  return false;
}

// ---------------------------------------------------------------- read a table (read-only request 0x72 .. 0x71 tbl)
int readTable(uint8_t tbl, uint8_t* data, int cap, String& err) {
  uint8_t q[5] = {0x72, 0x05, 0x71, tbl, 0}; q[4] = cks(q, 4);
  drain(); KL_SERIAL.write(q, 5); KL_SERIAL.flush();
  uint8_t e[5]; int got = 0; uint32_t t = millis();
  while (got < 5 && millis() - t < 80) if (KL_SERIAL.available()) e[got++] = KL_SERIAL.read();
  if (got < 5) { err = "ไม่ได้ยินเสียงสะท้อน"; return -1; }
  uint8_t r[80]; int n = readReply(r, sizeof r);
  if (n <= 0) { err = "ECU ไม่ตอบ"; return -1; }
  if (!sumOk(r, n)) { err = "checksum ผิด: " + hexs(r, n); return -1; }
  if (n < 5 || r[2] != 0x71 || r[3] != tbl) { err = "คำตอบแปลก: " + hexs(r, n); return -1; }
  int len = min(n - 5, cap); memcpy(data, r + 4, len);
  return len;
}

// ---------------------------------------------------------------- web


String tail() { return logBuf.length() > 4000 ? logBuf.substring(logBuf.length() - 4000) : logBuf; }
void sendJson(const String& body) { server.sendHeader("Cache-Control", "no-store"); server.send(200, "application/json", body); }

void apiState() {   // cheap poll for the dashboard: is the ECU answering, and has the board tried yet
  sendJson(String("{\"awake\":") + (awake ? "true" : "false") + ",\"tried\":" + (bgTries ? "true" : "false") + "}");
}
void apiStatus() {
  lastApi = millis();
  int p = awake ? 100 : lineHighPct();   // never break a live session just to sample the line
  logLine("สาย K-Line อยู่สถานะสูง " + String(p) + "% ของเวลา" + (p >= 90 ? " ✓ มีไฟรอ" : p <= 10 ? " ✗ ไม่มีไฟ" : " (แกว่ง)"));
  sendJson("{\"line\":" + String(p) + ",\"awake\":" + (awake ? "true" : "false") + ",\"log\":\"" + jsonEsc(tail()) + "\"}");
}
void apiWake() {
  lastApi = millis();
  String m = server.arg("mode");
  if (m == "module") awake = wakeOnce(false, 70, 120);
  else if (m == "npn") awake = wakeOnce(true, 70, 120);
  else awake = wakeAuto(true);
  if (!awake) logLine("สรุป: ยังปลุกไม่ติด - กด 'คัดลอกผลทั้งหมด' แล้วส่งมาให้ดู");
  sendJson(String("{\"awake\":") + (awake ? "true" : "false") + ",\"log\":\"" + jsonEsc(tail()) + "\"}");
}
void apiRead() {
  lastApi = millis();
  uint8_t tbl = (uint8_t)strtol(server.arg("t").c_str(), nullptr, 16);
  uint8_t d[64]; String err;
  int n = readTable(tbl, d, sizeof d, err);
  int slot = tbl == 0x11 ? 0 : tbl == 0x10 ? 1 : 2;
  String body;
  if (n < 0) {
    static uint8_t fails = 0;
    logLine("อ่านตาราง 0x" + String(tbl, HEX) + " ✗ " + err);
    if (++fails >= 3) { awake = false; fails = 0; }
    body = String("{\"ok\":false,\"awake\":") + (awake ? "true" : "false");
  } else {
    awake = true;
    if (tbl == LIVE_TABLE) keepLive(d, n);
    if (n != lastLen[slot] || memcmp(d, lastData[slot], n) != 0) {           // log only when something changed
      logLine("ตาราง 0x" + String(tbl, HEX) + " (" + n + " ไบต์): " + hexs(d, n));
      memcpy(lastData[slot], d, n); lastLen[slot] = n;
    }
    body = "{\"ok\":true,\"awake\":true,\"data\":[";
    for (int i = 0; i < n; i++) { if (i) body += ','; body += d[i]; }
    body += "]";
  }
  body += ",\"log\":\"" + jsonEsc(tail()) + "\"}";
  sendJson(body);
}
// Fault codes, read only. Query 0x74 = current codes, 0x73 = stored codes, pages 1..11
// (frame layout per oJudgeStore/SmartGauge_for_Honda): request 72 05 <q> <page> <cs>,
// answer 02 <len> <q> <page> <status> <main sub> <main sub> <main sub> <cs>. main 00/FF = empty slot.
// Codes print as main-sub, e.g. 7-1. The clear command (0x60) is deliberately not implemented.
int readDtcPage(uint8_t q, uint8_t page, uint8_t* r, int cap) {
  uint8_t f[5] = {0x72, 0x05, q, page, 0}; f[4] = cks(f, 4);
  drain(); KL_SERIAL.write(f, 5); KL_SERIAL.flush();
  uint8_t e[5]; int got = 0; uint32_t t = millis();
  while (got < 5 && millis() - t < 80) if (KL_SERIAL.available()) e[got++] = KL_SERIAL.read();
  if (got < 5) return -1;
  int n = readReply(r, cap, 300);
  if (n < 5 || !sumOk(r, n) || r[2] != q || r[3] != page) return -1;
  return n;
}
void apiDtc() {   // one blocking call; it does not pause the background reads like the test buttons do
  if (!awake) awake = wakeAuto(false);
  String body = String("{\"awake\":") + (awake ? "true" : "false");
  if (!awake) { sendJson(body + ",\"ok\":false}"); return; }
  int answered = 0;
  for (int pass = 0; pass < 2; pass++) {
    uint8_t q = pass == 0 ? 0x74 : 0x73;
    body += pass == 0 ? ",\"cur\":[" : "],\"mem\":[";
    bool first = true;
    for (uint8_t page = 1; page <= 11; page++) {
      uint8_t r[32]; int n = readDtcPage(q, page, r, sizeof r);
      if (n < 0) continue;
      answered++;
      if (page == 1) logLine(String("รหัสปัญหา ") + (q == 0x74 ? "ปัจจุบัน" : "ในประวัติ") + " หน้า 1: " + hexs(r, n));
      for (int off = 5; off + 1 < n - 1; off += 2) {
        if (r[off] == 0x00 || r[off] == 0xFF) continue;
        body += String(first ? "" : ",") + "[" + r[off] + "," + r[off + 1] + "]"; first = false;
      }
    }
  }
  body += String("],\"ok\":") + (answered ? "true" : "false") + ",\"pages\":" + answered + "}";
  sendJson(body);
}

// Table scan, behind the "add a bike model" button on the system tab: wakes the ECU if needed, then asks for
// every table 0x00-0xFF back to back (read requests 72 05 71 <tbl> only) and lists each answer raw.
// The last result is kept in /scan.txt (/api/scanlast).
void apiScan() {
  lastApi = millis();
  String out = "THUNDER GAUGE · สแกนตาราง ECU (อ่านอย่างเดียว)\n";
  if (!awake) awake = wakeAuto(false);
  out += String("ปลุก ECU: ") + (awake ? "ติด" : "ไม่ติด") + "\n";
  int found = 0, silent = 0, rewakes = 0;
  for (int t = 0; t < 256 && awake; t++) {
    uint8_t q[5] = {0x72, 0x05, 0x71, (uint8_t)t, 0}; q[4] = cks(q, 4);
    drain(); KL_SERIAL.write(q, 5); KL_SERIAL.flush();
    uint8_t e[5]; int got = 0; uint32_t t0 = millis();
    while (got < 5 && millis() - t0 < 60) if (KL_SERIAL.available()) e[got++] = KL_SERIAL.read();
    uint8_t r[80]; int n = readReply(r, sizeof r, 150);
    if (n <= 0) {
      if (++silent >= 6) {                         // the ECU dropped the session (or ignores this range)
        silent = 0;
        if (rewakes >= 4) { out += String("  (0x") + String(t - 5, HEX) + "–0x" + String(t, HEX) + " ไม่ตอบ ข้ามไป)\n"; continue; }
        rewakes++;
        out += String("  (ECU เงียบตั้งแต่ 0x") + String(t - 5, HEX) + " → ปลุกใหม่แล้วลองช่วงนี้ซ้ำ)\n";
        awake = wakeAuto(false); t -= 6; if (t < -1) t = -1;
        if (!awake) out += "ปลุกใหม่ไม่ติด หยุดสแกน\n";
      }
      continue;
    }
    silent = 0;
    char h[8]; snprintf(h, sizeof h, "0x%02X", t);
    int dlen = n - 5;
    if (dlen > 0) found++;
    out += String(h) + "  " + String(dlen) + " ไบต์ข้อมูล  ·  " + hexs(r, n) + (sumOk(r, n) ? "" : "  (checksum ผิด)") + "\n";
  }
  out += "ตารางที่มีข้อมูล: " + String(found) + "\n";
  if (fsOk) { File f = LittleFS.open("/scan.txt", "w"); if (f) { f.print(out); f.close(); } }
  lastApi = millis();
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/plain; charset=utf-8", out);
}
void apiScanLast() {
  server.sendHeader("Cache-Control", "no-store");
  if (fsOk && LittleFS.exists("/scan.txt")) { File f = LittleFS.open("/scan.txt", "r"); server.streamFile(f, "text/plain; charset=utf-8"); f.close(); }
  else server.send(200, "text/plain; charset=utf-8", "ยังไม่เคยสแกน");
}

// The dashboard polls this a few times a second. Raw bytes only: the page decodes them, so byte
// positions can be corrected in the page without touching the K-Line code.
void apiLive() {
  String b = String("{\"awake\":") + (awake ? "true" : "false") + ",\"age\":" + (liveLen ? millis() - liveAt : 999999) + ",\"d\":[";
  for (int i = 0; i < liveLen; i++) { if (i) b += ','; b += liveData[i]; }
  b += "],\"o\":[";
  for (int i = 0; i < o2Len; i++) { if (i) b += ','; b += o2Data[i]; }
  b += "],\"s\":[";
  for (int i = 0; i < stLen; i++) { if (i) b += ','; b += stData[i]; }
  sendJson(b + "],\"lt\":" + LIVE_TABLE + ",\"ot\":" + O2_TABLE + "}");   // lt/ot: the tables in use (the page decodes by table)
}
void apiLog() {
  server.sendHeader("Cache-Control", "no-store");
  if (fsOk && LittleFS.exists(LOG_FILE)) { File f = LittleFS.open(LOG_FILE, "r"); server.streamFile(f, "text/plain; charset=utf-8"); f.close(); }
  else server.send(200, "text/plain; charset=utf-8", logBuf);
}
void apiClear() {
  logBuf = ""; if (fsOk) LittleFS.remove(LOG_FILE);
  logLine("ล้างบันทึกแล้ว");
  sendJson(String("{\"awake\":") + (awake ? "true" : "false") + ",\"log\":\"" + jsonEsc(tail()) + "\"}");
}

void klineBegin() {
  pinMode(WAKE_NPN, OUTPUT); digitalWrite(WAKE_NPN, LOW);   // transistor off (SuperMini LED on)
  uartOn();
  server.on("/api/state", apiState);
  server.on("/api/status", apiStatus);
  server.on("/api/wake", apiWake);
  server.on("/api/read", apiRead);
  server.on("/api/live", apiLive);
  server.on("/api/dtc", apiDtc);
  server.on("/api/scan", apiScan);
  server.on("/api/scanlast", apiScanLast);
  server.on("/api/log", apiLog);
  server.on("/api/clear", apiClear);
}

// Runs from loop(): wake the ECU by itself at key-on, keep it awake, and retry while it stays silent.
// The dashboard only shows the test card when this has tried and the ECU still isn't answering.
// After a wake: pick the engine data table (and the O2 table) this ECU answers. A table "answers" when it sends
// at least 8 data bytes that are not all 0xFF; a header-only reply means the ECU does not have it.
bool tblHasData(uint8_t t, int minLen) {
  uint8_t d[64]; String err;
  quiet = true; int n = readTable(t, d, sizeof d, err); quiet = false;
  if (n < minLen) return false;
  for (int i = 0; i < n; i++) if (d[i] != 0xFF) return true;
  return false;
}
void klPickTables() {
  if (klModel.length() == 3 && klModel[0] == 't') { LIVE_TABLE = (uint8_t)strtol(klModel.c_str() + 1, nullptr, 16); }
  else {
    for (uint8_t t : LIVE_CANDS) if (tblHasData(t, 8)) { LIVE_TABLE = t; break; }
  }
  for (uint8_t t : O2_CANDS) if (tblHasData(t, 2)) { O2_TABLE = t; break; }
  char m[64]; snprintf(m, sizeof m, "ตารางข้อมูลเครื่องยนต์ 0x%02X · O2 0x%02X (%s)", LIVE_TABLE, O2_TABLE, klModel == "auto" ? "ตรวจหาอัตโนมัติ" : "ตั้งเอง");
  logLine(m);
}

void klineLoop() {
  static uint32_t lastKeep = 0, lastTry = 0; static uint8_t keepFails = 0;
  uint32_t now = millis();
  if (now - lastApi < 30000) return;              // someone is testing by hand: stay out of the way
  if (awake) {
    if (now - lastKeep < 200) return;           // ~5 readings a second for the dashboard
    lastKeep = now;
    uint8_t d[64]; String err;
    quiet = true; int n = readTable(LIVE_TABLE, d, sizeof d, err); quiet = false;
    if (n >= 0) {
      keepFails = 0; keepLive(d, n);
      uint8_t o[16]; String e2;
      quiet = true; int m = readTable(O2_TABLE, o, sizeof o, e2); quiet = false;
      if (m >= 0) { memcpy(o2Data, o, min(m, 8)); o2Len = min(m, 8); }
      quiet = true; m = readTable(STATUS_TABLE, o, sizeof o, e2); quiet = false;
      if (m >= 0) { memcpy(stData, o, min(m, 8)); stLen = min(m, 8); }
      return;
    }
    if (++keepFails >= 3) { awake = false; keepFails = 0; lastTry = now; logLine("ECU หยุดตอบ (" + err + ") → จะลองปลุกใหม่ทุก 5 วินาที"); }
    return;
  }
  if (now < 1500 || (bgTries && now - lastTry < 5000)) return;   // give the ECU a moment after key-on
  lastTry = now; bgTries++;
  quiet = bgTries > 1;                            // log the first attempt in full, later ones only on success
  bool ok = wakeOnce(false, 70, 120) || wakeOnce(true, 70, 120);
  quiet = false;
  if (ok) { awake = true; logLine("✓ ปลุก ECU อัตโนมัติติด (ครั้งที่ " + String(bgTries) + ")"); klPickTables(); }
  else if (bgTries == 1) logLine("ปลุกอัตโนมัติตอนเปิดเครื่องไม่ติด → จะลองซ้ำทุก 5 วินาที · ใช้การ์ดทดสอบ K-Line ในแท็บระบบได้");
}
