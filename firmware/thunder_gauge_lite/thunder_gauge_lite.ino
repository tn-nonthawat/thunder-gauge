// Thunder Gauge Lite - budget build (bench build, not the final firmware)
// Hardware: ESP32-C3 + K-Line module only. The C3 runs from the bike's USB socket; no MPU-6050, no GPS module
// (the Bluetooth phone app uses the phone's own GPS for road speed).
// Board opens its own Wi-Fi. Phone joins it and opens http://192.168.4.1 - or uses the app over Bluetooth.
// "/"    = the Lite dashboard (gzipped, from page_gz.h). ECU values stay simulated in the page
//          until the K-Line code is written (K-Line: UART on GPIO20 RX / GPIO21 TX, backup wake on GPIO8).
// "/api/*" = K-Line wake/read test (read only) behind the system tab's test card, see kline_tester.h
// "/update" = firmware upload over Wi-Fi (OTA), see ota_update.h
// "/map" = the AFR map, kept in LittleFS so it survives key-off (GET reads, POST replaces).
// Bluetooth: the phone app (app/, Chrome on Android) reaches every route above over BLE too, see ble_link.h.
// Wi-Fi turns itself off once a paired phone connects over Bluetooth (unless a phone is on the Wi-Fi), so nobody has
// to choose Wi-Fi or Bluetooth. It comes back at the next boot, from the app (/api/link?ap=1) or with a short press
// on BOOT, and goes off again after 10 minutes with no phone on it. "/api/link" = Wi-Fi state.
// Regenerate page_gz.h with mk_budget.py when the demo page changes.
// Partition table: partitions.csv in this folder (two 1.75 MB app slots, Bluetooth does not fit in the default 1.25 MB).
// Arduino IDE: Tools -> Partition Scheme -> "Minimal SPIFFS (1.9MB APP with OTA/128KB SPIFFS)" (only for the size check;
// the layout itself comes from partitions.csv). The first install with this table must go over USB.

#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include "page_gz.h"
#include "tg_server.h"
#define TG_SERVER_T TgServer   // shared headers: the server also answers Bluetooth requests
#define TG_BLE 1               // setup wizard: ask Wi-Fi or Bluetooth, and the Bluetooth PIN

const char* AP_SSID = "ThunderGauge-Lite";
const char* AP_PASS = "thunder123";   // full build default only; the Lite Wi-Fi is open until the owner sets a password

TgServer server(80);

// AFR map storage. Written at most about once a minute by the page; write to a temp file, then
// rename, so a key-off in the middle of a write leaves the previous map intact.
bool fsOk = false;
#include "kline_tester.h"
#include "ota_update.h"       // /update: firmware upload over Wi-Fi
#include "setup_wizard.h"     // /setup: first-run wizard (name + language, gauge Wi-Fi, wiring check)
#include "ble_link.h"         // Bluetooth link for the phone app
#include <nvs_flash.h>
const char* MAP_FILE = "/afrmap.json";
const char* MAP_TMP = "/afrmap.tmp";

void handleMapGet() {
  if (!fsOk || !LittleFS.exists(MAP_FILE)) { server.send(200, "application/json", "null"); return; }
  File f = LittleFS.open(MAP_FILE, "r");
  server.sendHeader("Cache-Control", "no-store");
  server.streamFile(f, "application/json");
  f.close();
}

void handleMapPost() {
  if (!fsOk) { server.send(503, "text/plain", "no fs"); return; }
  const String& body = server.arg("plain");
  if (body.length() < 2 || body.length() > 32768 || body[0] != '{') { server.send(400, "text/plain", "bad map"); return; }
  File f = LittleFS.open(MAP_TMP, "w");
  if (!f) { server.send(500, "text/plain", "open failed"); return; }
  size_t n = f.print(body);
  f.close();
  if (n != body.length()) { LittleFS.remove(MAP_TMP); server.send(500, "text/plain", "write failed"); return; }
  LittleFS.remove(MAP_FILE);
  LittleFS.rename(MAP_TMP, MAP_FILE);
  server.send(200, "text/plain", "ok");
}

// Screen setup shared by every phone (cards, tile looks, style, shift point, app icon...): one JSON object of
// the page's settings, kept in LittleFS so all phones see the same screen. The page sends it after a change
// (POST /ui?v=<version it started from>) and loads it at start (GET /ui; ?q=1 = version only, for polling).
// The version number goes in the X-UI-Ver header. A stale version gets 409 and the page retries on top.
const char* UI_FILE = "/ui.json";
const char* UI_TMP = "/ui.tmp";
const char* UI_VER = "/ui.ver";
uint32_t uiVer = 0;

void uiLoadVer() {
  if (!fsOk || !LittleFS.exists(UI_VER) || !LittleFS.exists(UI_FILE)) { uiVer = 0; return; }
  File f = LittleFS.open(UI_VER, "r");
  uiVer = f.readString().toInt();
  f.close();
}

void handleUiGet() {
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("X-UI-Ver", String(uiVer));
  if (server.hasArg("q")) { server.send(200, "text/plain", String(uiVer)); return; }
  logLine("หน้าจอ: มือถือขอค่า (มีอยู่ v" + String(uiVer) + ")");
  if (!fsOk || !uiVer || !LittleFS.exists(UI_FILE)) { server.send(200, "application/json", "null"); return; }
  File f = LittleFS.open(UI_FILE, "r");
  server.streamFile(f, "application/json");
  f.close();
}

void handleUiPost() {
  if (!fsOk) { server.send(503, "text/plain", "no fs"); return; }
  if ((uint32_t)server.arg("v").toInt() != uiVer) { logLine("หน้าจอ: มือถือส่งค่าจาก v" + server.arg("v") + " แต่บอร์ดมี v" + String(uiVer) + " → ให้ลองใหม่"); server.sendHeader("X-UI-Ver", String(uiVer)); server.send(409, "text/plain", String(uiVer)); return; }
  const String& body = server.arg("plain");
  if (body.length() < 2 || body.length() > 65536 || body[0] != '{') { server.send(400, "text/plain", "bad ui"); return; }
  File f = LittleFS.open(UI_TMP, "w");
  if (!f) { server.send(500, "text/plain", "open failed"); return; }
  size_t n = f.print(body);
  f.close();
  if (n != body.length()) { LittleFS.remove(UI_TMP); server.send(500, "text/plain", "write failed"); return; }
  LittleFS.remove(UI_FILE);
  LittleFS.rename(UI_TMP, UI_FILE);
  uiVer++;
  logLine("หน้าจอ: บันทึกค่าใหม่ v" + String(uiVer) + " (" + String(body.length()) + " ไบต์)");
  File v = LittleFS.open(UI_VER, "w");
  if (v) { v.print(uiVer); v.close(); }
  server.sendHeader("X-UI-Ver", String(uiVer));
  server.send(200, "text/plain", String(uiVer));
}

// ---------------------------------------------------------------- Wi-Fi on demand
bool apOn = false, btUser = false; uint32_t apSince = 0;   // btUser: a paired phone has connected over Bluetooth since boot
void apStart(const char* why) {
  apSince = millis();
  if (apOn) return;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(suSsid.c_str(), suPass.length() ? suPass.c_str() : nullptr);   // no password yet = open Wi-Fi
  WiFi.setSleep(false);                 // no power-save naps between packets = lower latency
  WiFi.setTxPower(WIFI_POWER_11dBm);    // C3 super-minis misbehave at full power
  apOn = true;
  logLine(String("เปิด Wi-Fi ") + suSsid + " (" + why + ")");
}
void apStop(const char* why) {
  if (!apOn) return;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  apOn = false;
  logLine(String("ปิด Wi-Fi (") + why + ")");
}
void apLoop() {
  if (blHello) {                        // a paired phone said hello over Bluetooth
    blHello = false;
    if (!btUser) { btUser = true; if (apOn && WiFi.softAPgetStationNum() == 0) apStop("มือถือต่อบลูทูธแล้ว"); }
  }
  if (!apOn || !btUser) return;         // Bluetooth in use: Wi-Fi turned on again goes off after 10 minutes with no phone on it
  if (WiFi.softAPgetStationNum() > 0) apSince = millis();
  else if (millis() - apSince > 600000UL) apStop("ไม่มีมือถือต่อ 10 นาที");
}
void suShortPress() { apStart("กดปุ่ม BOOT"); }
void suFactoryHook() { nvs_flash_erase(); }   // BOOT reset also forgets the phones paired over Bluetooth
void handleLink() {                     // GET /api/link[?ap=1|0]: Wi-Fi state (the app's connection card)
  if (server.hasArg("ap")) { if (server.arg("ap") == "1") apStart("สั่งจากแอป"); else apStop("สั่งจากแอป"); }
  sendJson(String("{\"auto\":") + (btUser ? "true" : "false") + ",\"ap\":" + (apOn ? "true" : "false") + ",\"sta\":" + (apOn ? WiFi.softAPgetStationNum() : 0) +
           ",\"ssid\":\"" + jsonEsc(suSsid) + "\",\"bt\":" + (blConn ? "true" : "false") + ",\"off\":" + (apOn ? (600 - (millis() - apSince) / 1000) : 0) + "}");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  fsOk = LittleFS.begin(true);          // formats the data partition on first boot

  suBegin();                            // load the wizard settings (gauge Wi-Fi name/password, Bluetooth PIN) from LittleFS
  apStart("บูต");                       // access point only (OTA at 192.168.4.1); the web server needs the network stack up once
  server.on("/", []() {
    if (suGate()) return;               // first run: send the phone to /setup
    server.sendHeader("Content-Encoding", "gzip");
    server.sendHeader("Cache-Control", "no-store");   // no-store, or Chrome serves a stale offline copy on this internet-less AP
    server.send_P(200, "text/html; charset=utf-8", (const char*)PAGE_GZ, PAGE_GZ_LEN);
  });
  klineBegin();
  server.on("/map", HTTP_GET, handleMapGet);
  server.on("/map", HTTP_POST, handleMapPost);
  uiLoadVer();
  server.on("/ui", HTTP_GET, handleUiGet);
  server.on("/ui", HTTP_POST, handleUiPost);
  otaBegin();
  suRoutes();
  server.on("/api/link", handleLink);
  server.begin();
  bleBegin(suSsid, suPin);
  logLine("---- บูต · Wi-Fi " + suSsid + (apOn ? " เปิด" : " ปิด") + " · บลูทูธ · LittleFS " + (fsOk ? "OK" : "ใช้ไม่ได้"));
}

void loop() {
  server.handleClient();
  if (!otaBusy) klineLoop();         // pause K-Line while a firmware upload runs
  suLoop();                          // BOOT held 3 s = factory Wi-Fi; restart after a Wi-Fi change
  bleLoop();                         // Bluetooth requests from the phone app, live data, firmware upload
  apLoop();
  delay(1);
}
