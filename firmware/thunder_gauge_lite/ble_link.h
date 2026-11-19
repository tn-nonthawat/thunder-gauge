// Bluetooth (BLE) link, Lite build only. The Thunder Gauge phone app (the gauge page opened from https in Chrome on
// Android, see app/) talks to the board over BLE instead of the board's Wi-Fi. Being on https lets the page use the
// phone's own GPS for road speed, which this ECU does not report.
//
// One GATT service. With a PIN set in the setup wizard (setup.txt pin=), every characteristic needs an encrypted,
// authenticated link: the phone pairs once with that 6-digit PIN, so nobody nearby can change settings or load
// firmware. A new board has no PIN: phones connect without pairing until the owner sets one (the app reminds).
//   LIVE (read, notify): newest ECU answer, binary, about 5 times a second:
//        [1, awake, age lo, age hi, live table, O2 table, n, d0..dn-1, n, o..., n, s...]   (age in ms, capped at 65535)
//   REQ  (write): a request in pieces [kind, id, seq lo, seq hi, data...]: kind 1 = more follows, 2 = last piece,
//        3 = ack of the reply (seq = next piece wanted), 4 = hello (no data, starts the pairing).
//        A request is "METHOD /path?query F\n" + body, F = 'f' url-encoded form or '-' raw body. It goes through
//        the same handlers as over Wi-Fi (TgServer::dispatch), so the ECU side stays read-only.
//   RES  (notify): the raw http reply (status line, headers, body) in pieces [id, last, seq lo, seq hi, data...].
//        8 pieces, then wait for the ack; no ack within 400 ms = send again from the last acked piece.
//   OTA  (write, read): firmware update [1, size u32] begin, [2, offset u32, data...] write, [3] finish, [4] cancel.
//        Read gives "ok <bytes written>" or "err <reason>". Same safety as /update: the new image only boots when
//        it arrived whole and its checksum verified, and it must start with the ESP32 image byte (0xE9).
// Requests are run from loop(), never from the BLE task, so the handlers see the same single-threaded world as
// over Wi-Fi. Only one phone at a time is connected over Bluetooth.
#pragma once
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLESecurity.h>
#include <BLE2902.h>
#include <Update.h>
#include <host/ble_hs.h>
#if !defined(CONFIG_NIMBLE_ENABLED)
#error "ble_link.h is written for the NimBLE stack (ESP32-C3)"
#endif

#define BL_SVC  "7e9c0001-3f2a-4c55-9d1e-5467617567e1"
#define BL_LIVE "7e9c0002-3f2a-4c55-9d1e-5467617567e1"
#define BL_REQ  "7e9c0003-3f2a-4c55-9d1e-5467617567e1"
#define BL_RES  "7e9c0004-3f2a-4c55-9d1e-5467617567e1"
#define BL_OTA  "7e9c0005-3f2a-4c55-9d1e-5467617567e1"

BLEServer* blSrv = nullptr;
BLECharacteristic *blLive = nullptr, *blReq = nullptr, *blRes = nullptr, *blOta = nullptr;
volatile bool blConn = false;
volatile bool blGreeted = false; volatile uint32_t blConnAt = 0; volatile uint8_t blEvt = 0;   // this link said hello; when it came; connect/disconnect seen (logged from loop())
volatile bool blPull = false;      // pull mode (hello byte 5 bit 0): the phone reads RES / LIVE itself, nothing is notified
volatile uint16_t blLiveMs = 100;   // shortest gap between live packets, asked by the phone in its hello (iPhone/Bluefy: 1000)
volatile bool blHello = false;   // a phone wrote hello (with a PIN set: only a paired one can): the .ino turns Wi-Fi off
volatile uint16_t blConnId = 0;
uint32_t blRestartAt = 0;

// request coming in (filled by the BLE task, taken by loop())
String blIn; uint8_t blInId = 0; uint16_t blInSeq = 0;
volatile bool blInReady = false;
// reply going out (loop() only, except the ack fields)
String blOut; uint8_t blOutId = 0; bool blOutBusy = false;
uint16_t blOutTotal = 0, blOutSent = 0, blOutAcked = 0, blChunk = 20; uint32_t blOutAt = 0; uint8_t blOutTries = 0;
volatile uint16_t blAckSeq = 0; volatile bool blAckNew = false; volatile uint8_t blAckId = 0;
// firmware upload over Bluetooth
uint32_t blOtaSize = 0, blOtaDone = 0; bool blOtaOn = false;

void blOtaStatus(const String& s) { if (blOta) blOta->setValue(s); }

// Notify the connected phone directly. The library's notify() only sends to phones it saw subscribe, and after a
// reconnect Chrome on Android may skip writing the subscription again (it thinks it is still on), so replies and
// live data would never arrive. ble_gatts_notify_custom sends to the connection whatever the subscription state.
// Pull mode, for iPhone browsers (Bluefy): notifications barely reach the page there, while reads always do. The phone
// then reads RES for each reply piece (the value is the piece it acked last) and reads LIVE about once a second.
static void blNotify(BLECharacteristic* c, const uint8_t* d, size_t n) {
  c->setValue(d, n);                             // also readable
  if (!blConn || blPull) return;
  os_mbuf* om = ble_hs_mbuf_from_flat(d, n);
  if (om) ble_gatts_notify_custom(blConnId, c->getHandle(), om);   // a lost piece is sent again by the reply window
}

class BlSrvCb : public BLEServerCallbacks {
  void onConnect(BLEServer* s, ble_gap_conn_desc* d) override { blConnId = d->conn_handle; blConnAt = millis(); blGreeted = false; blConn = true; blEvt |= 1; }
  void onDisconnect(BLEServer* s, ble_gap_conn_desc* d) override {
    blConn = false; blEvt |= 2;
    if (blOtaOn) { Update.abort(); blOtaOn = false; otaBusy = false; blOtaStatus("err หลุดการเชื่อมต่อ"); }
  }
};

// pairing result, logged from loop(): status 0 = paired; encrypted / authenticated (PIN) / bonded flags
volatile int blAuthStatus = 0; volatile uint8_t blAuthFlags = 0; volatile bool blAuthEvt = false;
class BlSecCb : public BLESecurityCallbacks {
  void onAuthenticationComplete(ble_gap_conn_desc* d, int status) override {
    blAuthStatus = status;
    blAuthFlags = (d && d->sec_state.encrypted ? 1 : 0) | (d && d->sec_state.authenticated ? 2 : 0) | (d && d->sec_state.bonded ? 4 : 0);
    blAuthEvt = true;
  }
};

class BlReqCb : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String v = c->getValue();
    const uint8_t* p = (const uint8_t*)v.c_str(); size_t n = v.length();
    if (n < 4) return;
    uint8_t kind = p[0], id = p[1]; uint16_t seq = p[2] | (p[3] << 8);
    if (kind == 3) { blAckId = id; blAckSeq = seq; blAckNew = true; return; }
    if (kind == 4) { blHello = true; blGreeted = true; blLiveMs = n >= 5 && p[4] ? p[4] * 100 : 100; blPull = n >= 6 && (p[5] & 1); return; }   // byte 4 = live interval x100 ms   // hello: the phone pairs here, before it subscribes
    if (kind != 1 && kind != 2) return;
    if (blInReady) return;                       // loop() has not taken the last one yet: the phone retries
    if (seq == 0) { blIn = ""; blInId = id; blInSeq = 0; }
    if (id != blInId || seq != blInSeq) return;  // a lost piece: drop the request, the phone times out and retries
    if (blIn.length() + n - 4 > 70000) { blIn = ""; return; }
    blIn.concat((const char*)p + 4, n - 4); blInSeq++;
    if (kind == 2) blInReady = true;
  }
};

class BlOtaCb : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {   // runs in the BLE task; the phone waits for each write to finish
    String v = c->getValue();
    const uint8_t* p = (const uint8_t*)v.c_str(); size_t n = v.length();
    if (!n) return;
    if (p[0] == 1 && n >= 5) {
      if (blOtaOn) Update.abort();
      blOtaSize = p[1] | (p[2] << 8) | (p[3] << 16) | ((uint32_t)p[4] << 24); blOtaDone = 0;
      otaBusy = true;                            // K-Line reads pause, as during a Wi-Fi upload
      if (!Update.begin(blOtaSize, U_FLASH)) { blOtaOn = false; otaBusy = false; blOtaStatus(String("err เริ่มไม่ได้: ") + Update.errorString()); return; }
      blOtaOn = true; blOtaStatus("ok 0");
      logLine("อัปเดตเฟิร์มแวร์ผ่านบลูทูธ: " + String(blOtaSize) + " ไบต์");
    } else if (p[0] == 2 && n > 5) {
      if (!blOtaOn) return;
      uint32_t off = p[1] | (p[2] << 8) | (p[3] << 16) | ((uint32_t)p[4] << 24);
      if (off != blOtaDone) { blOtaStatus("err ข้อมูลข้ามตำแหน่ง " + String(off) + " / " + String(blOtaDone)); return; }
      if (off == 0 && p[5] != 0xE9) { Update.abort(); blOtaOn = false; otaBusy = false; blOtaStatus("err ไฟล์ไม่ใช่เฟิร์มแวร์ ESP32"); return; }
      size_t len = n - 5;
      if (Update.write((uint8_t*)p + 5, len) != len) { blOtaStatus(String("err เขียนไม่ได้: ") + Update.errorString()); Update.abort(); blOtaOn = false; otaBusy = false; return; }
      blOtaDone += len; blOtaStatus("ok " + String(blOtaDone));
    } else if (p[0] == 3) {
      if (!blOtaOn) return;
      blOtaOn = false;
      if (blOtaDone != blOtaSize || !Update.end(true)) { otaBusy = false; blOtaStatus(String("err ตรวจไฟล์ไม่ผ่าน: ") + Update.errorString()); return; }
      blOtaStatus("ok done"); logLine("อัปเดตเฟิร์มแวร์ผ่านบลูทูธสำเร็จ รีสตาร์ท");
      blRestartAt = millis() + 1500;             // let the phone read the result first
    } else if (p[0] == 4) {
      if (blOtaOn) Update.abort();
      blOtaOn = false; otaBusy = false; blOtaStatus("err ยกเลิกแล้ว");
    }
  }
};

void bleBegin(const String& name, const String& pin) {
  String nm = name.length() ? name : String("ThunderGauge");
  if (nm.length() > 20) nm = nm.substring(0, 20);
  BLEDevice::init(nm);
  BLEDevice::setMTU(247);                        // 244-byte notifications when the phone agrees
  bool locked = pin.length() == 6;                // no PIN yet: open link, nothing to type on the first connection
  if (locked) {
    BLESecurity* sec = new BLESecurity();
    sec->setPassKey(true, (uint32_t)pin.toInt());  // static PIN: the board has no screen, the phone types it in
    sec->setCapability(ESP_IO_CAP_OUT);
    sec->setAuthenticationMode(true, true, true);  // bond, MITM (PIN prompt), LE secure connections
    BLEDevice::setSecurityCallbacks(new BlSecCb());
  }
  blSrv = BLEDevice::createServer();
  blSrv->setCallbacks(new BlSrvCb());
  blSrv->advertiseOnDisconnect(true);
  BLEService* svc = blSrv->createService(BL_SVC);
  const uint32_t RA = locked ? BLECharacteristic::PROPERTY_READ_AUTHEN : 0, WA = locked ? BLECharacteristic::PROPERTY_WRITE_AUTHEN : 0;
  blLive = svc->createCharacteristic(BL_LIVE, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY | RA);
  blReq = svc->createCharacteristic(BL_REQ, BLECharacteristic::PROPERTY_WRITE | WA);
  blRes = svc->createCharacteristic(BL_RES, BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ | RA);
  blOta = svc->createCharacteristic(BL_OTA, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_READ | WA | RA);
  blReq->setCallbacks(new BlReqCb());
  blOta->setCallbacks(new BlOtaCb());
  blOtaStatus("ok 0");
  svc->start();
  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(BL_SVC);
  adv->setScanResponse(true);                    // the name goes in the scan reply: the 128-bit service id fills the advert
  BLEDevice::startAdvertising();
  logLine("บลูทูธพร้อม: " + nm + (locked ? " (ต้องใช้ PIN)" : " (ยังไม่ตั้ง PIN)"));
}

// ---------------------------------------------------------------- loop side
static HTTPMethod blMethod(const String& m) {
  if (m == "POST") return HTTP_POST;
  if (m == "PUT") return HTTP_PUT;
  if (m == "DELETE") return HTTP_DELETE;
  return HTTP_GET;
}

void blSendPiece(uint16_t seq);
void blTakeRequest() {
  String r = blIn; uint8_t id = blInId;
  blIn = ""; blInReady = false;
  int nl = r.indexOf('\n'); if (nl < 0) nl = r.length();
  String head = r.substring(0, nl), body = nl < (int)r.length() ? r.substring(nl + 1) : String();
  int s1 = head.indexOf(' '), s2 = head.lastIndexOf(' ');
  if (s1 < 0) return;
  if (s2 <= s1) s2 = head.length();
  String method = head.substring(0, s1), url = head.substring(s1 + 1, s2);
  bool form = s2 < (int)head.length() && head.substring(s2 + 1) == "f";
  int qm = url.indexOf('?');
  String path = qm < 0 ? url : url.substring(0, qm), query = qm < 0 ? String() : url.substring(qm + 1);
  blOut = "";
  if (path == "/update" && method != "GET") blOut = "HTTP/1.0 404 Not Found\r\nContent-Type: text/plain\r\n\r\nuse the Bluetooth firmware update";
  else server.dispatch(blMethod(method), path, query, body, form, blOut);
  if (!blOut.length()) blOut = "HTTP/1.0 500 Internal Server Error\r\nContent-Type: text/plain\r\n\r\nno reply";
  uint16_t mtu = blSrv->getPeerMTU(blConnId);
  blChunk = mtu > 30 ? min(240, mtu - 3 - 4) : 16;
  blOutId = id; blOutTotal = (blOut.length() + blChunk - 1) / blChunk; blOutSent = blOutAcked = 0; blOutTries = 0;
  blOutAt = millis(); blOutBusy = true; blAckNew = false;
  if (blPull) blSendPiece(0);                    // pull mode: the first piece waits in RES for the phone to read
}

void blSendPiece(uint16_t seq) {
  uint8_t buf[4 + 240];
  size_t off = (size_t)seq * blChunk, len = min((size_t)blChunk, blOut.length() - off);
  buf[0] = blOutId; buf[1] = seq + 1 == blOutTotal ? 1 : 0; buf[2] = seq & 255; buf[3] = seq >> 8;
  memcpy(buf + 4, blOut.c_str() + off, len);
  blNotify(blRes, buf, 4 + len);
}

void blSendTick() {
  if (!blOutBusy) return;
  if (!blConn) { blOutBusy = false; blOut = ""; logLine("บลูทูธ: หลุดระหว่างส่งคำตอบ"); return; }
  uint32_t now = millis();
  if (blAckNew) {
    blAckNew = false;
    uint16_t a = blAckSeq;
    if (blAckId == blOutId && a == 0xFFFF) { blOutBusy = false; blOut = ""; return; }   // the phone no longer wants this reply
    if (blAckId == blOutId && a >= blOutAcked && a <= blOutTotal) {
      if (a > blOutAcked) blOutTries = 0;
      blOutAcked = a; blOutAt = now;
      if (a < blOutSent) blOutSent = a;        // the phone missed a piece: send again from there
      if (a >= blOutTotal) { blOutBusy = false; blOut = ""; return; }
      if (blPull) blSendPiece(a);              // pull mode: put the next piece in RES for the phone's next read
    }
  }
  if (blPull) {                                  // pull mode: no windows, the phone reads at its own pace
    if (now - blOutAt > 8000) { blOutBusy = false; blOut = ""; logLine("บลูทูธ: แอปไม่มาอ่านคำตอบ เลิกส่ง (ได้ " + String(blOutAcked) + "/" + String(blOutTotal) + " ชิ้น)"); }
    return;
  }
  if (blOutSent > blOutAcked && now - blOutAt > 400) {   // no ack: send the window again
    if (++blOutTries > 20) { blOutBusy = false; blOut = ""; logLine("บลูทูธ: แอปไม่ตอบรับคำตอบ เลิกส่ง (ได้ " + String(blOutAcked) + "/" + String(blOutTotal) + " ชิ้น)"); return; }
    blOutSent = blOutAcked;
  }
  while (blOutSent < blOutTotal && blOutSent < blOutAcked + 8) { blSendPiece(blOutSent++); blOutAt = now; delay(2); }
}

void blLiveTick() {
  static uint32_t lastAt = 0, lastSend = 0; static bool lastAwake = false;
  uint32_t now = millis();
  // Live data waits while a reply is being sent: a slow phone browser (iPhone/Bluefy) passes Bluetooth data to the
  // page one by one, so a stream of live packets would make every reply wait behind it.
  if (blOutBusy && !blPull) return;
  if (now - lastSend < blLiveMs) return;
  if (liveAt == lastAt && awake == lastAwake && now - lastSend < max((uint16_t)500, (uint16_t)blLiveMs)) return;
  lastAt = liveAt; lastAwake = awake; lastSend = now;
  uint8_t b[8 + 64 + 8 + 8]; int n = 0;
  uint32_t age = liveLen ? now - liveAt : 65535; if (age > 65535) age = 65535;
  b[n++] = 1; b[n++] = awake ? 1 : 0; b[n++] = age & 255; b[n++] = age >> 8; b[n++] = LIVE_TABLE; b[n++] = O2_TABLE;
  int dl = min(liveLen, 64); b[n++] = dl; memcpy(b + n, liveData, dl); n += dl;
  int ol = min(o2Len, 8); b[n++] = ol; memcpy(b + n, o2Data, ol); n += ol;
  int sl = min(stLen, 8); b[n++] = sl; memcpy(b + n, stData, sl); n += sl;
  blNotify(blLive, b, n);
}

void bleLoop() {
  if (blRestartAt && (int32_t)(millis() - blRestartAt) >= 0) ESP.restart();
  if (blEvt) {                                   // connect / disconnect, logged here (not from the BLE task)
    uint8_t e = blEvt; blEvt = 0;
    if (e & 1) logLine("บลูทูธ: มีมือถือต่อเข้ามา");
    if ((e & 2) && !blConn) logLine("บลูทูธ: มือถือหลุด กลับไปรอให้ต่อ");
  }
  if (blAuthEvt) {
    blAuthEvt = false; uint8_t f = blAuthFlags;
    logLine(String("บลูทูธ: จับคู่") + (blAuthStatus == 0 ? "ผ่าน" : "ไม่ผ่าน (รหัส " + String(blAuthStatus) + ")") +
            " · เข้ารหัส " + (f & 1 ? "ใช่" : "ไม่") + " · ใช้ PIN " + (f & 2 ? "ใช่" : "ไม่") + " · จำเครื่อง " + (f & 4 ? "ใช่" : "ไม่"));
  }
  // A link that never says hello is not the app (e.g. the phone's own Bluetooth holding an old connection). Only one
  // phone can be connected, so it would lock the app out: drop it after 15 s and advertise again.
  if (blConn && !blGreeted && millis() - blConnAt > 15000) {
    blGreeted = true;
    logLine("บลูทูธ: เครื่องที่ต่อค้างไม่ได้เปิดแอป ตัดทิ้งให้เครื่องอื่นต่อได้");
    blSrv->disconnect(blConnId);
  }
  // A new request while an older reply is still unfinished: the phone has given up on the old one (it waits for the
  // newest), so drop that reply and answer the new request at once. Otherwise a slow link falls further and further behind.
  if (blInReady && (!blOutBusy || blInId != blOutId)) { blOutBusy = false; blOut = ""; blTakeRequest(); }
  blSendTick();
  blLiveTick();
}
