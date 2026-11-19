// WebServer that can also answer a request that came in over Bluetooth (ble_link.h), Lite build only.
// The phone app sends the same requests it would send over Wi-Fi ("GET /api/live", "POST /ui?v=3" ...); dispatch()
// runs them through the normal server.on() handlers and catches the raw http reply (status line, headers, body)
// in a String instead of a Wi-Fi socket. So every route works the same over Wi-Fi and Bluetooth, and the ECU side
// stays exactly as read-only as it is over Wi-Fi (same handlers, nothing new sent to the ECU).
// The shared headers declare "extern TG_SERVER_T server": the .ino sets TG_SERVER_T to TgServer before including them.
#pragma once
#include <WebServer.h>

class TgServer : public WebServer {
public:
  using WebServer::WebServer;

  // Run one request through the handlers. form = body is url-encoded form fields (the wizard's save), else it is the
  // raw body (server.arg("plain")). out gets the whole http reply. Uploads (/update) are not handled here:
  // Bluetooth firmware updates have their own characteristic in ble_link.h.
  void dispatch(HTTPMethod method, const String& uri, const String& query, const String& body, bool form, String& out) {
    out = "";
    _clearResponseHeaders();                   // the http loop clears these per request; without it headers pile up reply after reply
    _currentMethod = method;
    _currentUri = uri;
    _currentVersion = 0;                       // HTTP/1.0 style: no chunked encoding in the caught reply
    _contentLength = CONTENT_LENGTH_NOT_SET;
    _currentHandler = nullptr;
    for (RequestHandler* h = _firstHandler; h; h = h->next())
      if (h->canHandle(*this, method, uri)) { _currentHandler = h; break; }
    String q = query;
    if (form && body.length()) { if (q.length()) q += '&'; q += body; }
    _parseArguments(q);                        // allocates one spare slot for "plain", as the http parser does
    if (!form && body.length() && _currentArgs) {
      RequestArgument& a = _currentArgs[_currentArgCount++];
      a.key = F("plain");
      a.value = body;
    }
    cap = &out;
    _handleRequest();
    cap = nullptr;
    _contentLength = CONTENT_LENGTH_NOT_SET;
    _clearResponseHeaders();
  }

  // streamFile() writes straight to the socket in the base class; while catching a reply, copy the file instead.
  template<typename T> size_t streamFile(T& file, const String& contentType, const int code = 200) {
    if (!cap) return WebServer::streamFile(file, contentType, code);
    _streamFileCore(file.size(), file.name(), contentType, code);
    uint8_t buf[512]; size_t n, total = 0;
    while ((n = file.read(buf, sizeof buf)) > 0) { cap->concat((const char*)buf, n); total += n; }
    return total;
  }

protected:
  String* cap = nullptr;                       // set while dispatch() runs: the reply goes here
  size_t _currentClientWrite(const char* b, size_t l) override {
    if (cap) { cap->concat(b, l); return l; }
    return WebServer::_currentClientWrite(b, l);
  }
  size_t _currentClientWrite_P(PGM_P b, size_t l) override {
    if (cap) { cap->concat(b, l); return l; }    // PROGMEM is plain memory on the ESP32
    return WebServer::_currentClientWrite_P(b, l);
  }
};
