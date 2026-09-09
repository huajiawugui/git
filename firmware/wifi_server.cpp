#include "wifi_server.h"
#include <mbedtls/sha1.h>
#include <mbedtls/base64.h>

FishNet::FishNet() : http(HTTP_PORT), wsSrv(WS_PORT) {}

void FishNet::init() {
  LittleFS.begin(true);

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_GW, AP_MASK);
  WiFi.softAP(AP_SSID, AP_PASS);

  http.on("/", [this]() { handleRoot(); });
  http.on("/index.html", [this]() { handleRoot(); });
  http.on("/three.min.js", [this]() { handleJs(); });
  http.on("/api/status", [this]() { handleStatus(); });
  http.on("/api/cmd", [this]() { handleCmd(); });
  http.onNotFound([this]() { http.send(404, "text/plain", "404 not found"); });
  http.begin();

  wsSrv.begin();
  wsSrv.setNoDelay(true);
}

void FishNet::loop(uint32_t now) {
  http.handleClient();

  if (!ws.connected()) {
    if (wsHandshaken) { wsHandshaken = false; fragLen = 0; }   // 旧客户端断开
    ws = wsSrv.available();                                    // 接受新连接(无则返回空 client)
    if (!ws.connected()) return;
    wsHandshaken = false;
    fragLen = 0;
    hsBuf = "";
    hsT0 = now;
  }
  if (wsHandshaken) wsReceive(now);
  else wsHandshake(now);
}

/* ---------- HTTP 路由 ---------- */

void FishNet::serveFile(const char* path, const char* ct) {
  if (!LittleFS.exists(path)) { http.send(404, "text/plain", "404 not found"); return; }
  File f = LittleFS.open(path, "r");
  if (!f) { http.send(404, "text/plain", "404 not found"); return; }
  /* 分块发送(不用 streamFile,兼容 core 2.x / 3.x 的 API 差异) */
  http.setContentLength(f.size());
  http.send(200, ct, "");
  uint8_t buf[1024];
  size_t n;
  while ((n = f.read(buf, sizeof buf)) > 0) http.sendContent((const char*)buf, n);
  http.sendContent("");
  f.close();
}

void FishNet::handleRoot()   { serveFile("/index.html", "text/html; charset=utf-8"); }
void FishNet::handleJs()     { serveFile("/three.min.js", "application/javascript"); }

void FishNet::handleStatus() {
  char buf[160];
  if (teleFn) teleFn(buf, sizeof buf); else buf[0] = 0;
  http.send(200, "application/json", buf);
}

void FishNet::handleCmd() {
  String c = http.arg("cmd");          // WebServer 已做 URL 解码
  if (c.length() > 0 && cmdFn) cmdFn(c.c_str());
  http.send(200, "application/json", "{\"t\":\"ack\",\"v\":1,\"ok\":true}");
}

/* ---------- WebSocket(手写实现) ---------- */

void FishNet::wsHandshake(uint32_t now) {
  if (now - hsT0 > 2000) { ws.stop(); return; }   // 2s 未完成握手,断开

  while (ws.available()) {
    char c = (char)ws.read();
    if (hsBuf.length() < 1024) hsBuf += c;
    if (hsBuf.endsWith("\r\n\r\n")) {
      if (hsBuf.indexOf("Upgrade: websocket") < 0) { ws.stop(); return; }
      int k = hsBuf.indexOf("Sec-WebSocket-Key: ");
      if (k < 0) { ws.stop(); return; }
      String key = hsBuf.substring(k + 19);
      key.trim();
      char acc[32];
      sha1B64(key.c_str(), acc);
      ws.print("HTTP/1.1 101 Switching Protocols\r\n"
               "Upgrade: websocket\r\n"
               "Connection: Upgrade\r\n"
               "Sec-WebSocket-Accept: ");
      ws.print(acc);
      ws.print("\r\n\r\n");
      wsHandshaken = true;
      wsLastRx = now;
      return;
    }
  }
}

void FishNet::wsReceive(uint32_t now) {
  if (now - wsLastRx > 15000) { ws.stop(); wsHandshaken = false; return; }  // 15s 超时

  while (ws.available()) {
    uint8_t h[2];
    if (!wsReadExact(h, 2)) return;
    const bool fin = h[0] & 0x80;
    const uint8_t op = h[0] & 0x0F;
    const bool masked = h[1] & 0x80;
    uint64_t len = h[1] & 0x7F;
    if (len == 126) {
      uint8_t e[2];
      if (!wsReadExact(e, 2)) return;
      len = ((uint64_t)e[0] << 8) | e[1];
    } else if (len == 127) {
      uint8_t e[8];
      if (!wsReadExact(e, 8)) return;
      len = 0;
      for (int i = 0; i < 8; i++) len = (len << 8) | e[i];
    }
    if (!masked) { ws.stop(); wsHandshaken = false; return; }  // 客户端帧必须掩码
    uint8_t mk[4];
    if (!wsReadExact(mk, 4)) return;
    if (len > 511) { ws.stop(); wsHandshaken = false; return; } // 指令帧很小,超长视为异常
    char buf[512];
    if (!wsReadExact((uint8_t*)buf, (size_t)len)) return;
    for (uint64_t i = 0; i < len; i++) buf[i] ^= mk[i & 3];
    wsLastRx = now;

    if (op == 0x8) { ws.stop(); wsHandshaken = false; return; }  // close
    if (op == 0x9) { sendFrame(0xA, buf, (size_t)len); continue; } // ping → pong
    if (op == 0x1 || op == 0x0) {                                 // text / continuation
      if (fragLen + len > 511) { ws.stop(); wsHandshaken = false; return; }
      memcpy(frag + fragLen, buf, (size_t)len);
      fragLen += (uint16_t)len;
      if (fin) {
        frag[fragLen] = 0;
        fragLen = 0;
        if (op == 0x1 && cmdFn) cmdFn(frag);
      }
    }
    /* binary / pong 忽略 */
  }
}

/* 非阻塞读:当前缓冲不足 n 字节时返回 false,下次 loop 再试 */
bool FishNet::wsReadExact(uint8_t* buf, size_t n) {
  if ((size_t)ws.available() < n) return false;
  for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)ws.read();
  return true;
}

bool FishNet::sendFrame(uint8_t op, const char* data, size_t len) {
  if (!wsHandshaken || !ws.connected()) return false;
  uint8_t h[10];
  size_t hl;
  h[0] = 0x80 | op;
  if (len < 126) { h[1] = (uint8_t)len; hl = 2; }
  else {
    h[1] = 126; h[2] = (uint8_t)(len >> 8); h[3] = (uint8_t)len; hl = 4;
  }
  if (ws.write(h, hl) != hl) return false;
  return ws.write((const uint8_t*)data, len) == len;
}

bool FishNet::sendWs(const char* text) {
  return sendFrame(0x1, text, strlen(text));
}

void FishNet::sendTele() {
  if (!teleFn || !wsConnected()) return;
  char buf[160];
  teleFn(buf, sizeof buf);
  sendWs(buf);
}

/* Sec-WebSocket-Accept = base64(SHA1(key + GUID)) */
void FishNet::sha1B64(const char* key, char out[32]) {
  char in[128];
  snprintf(in, sizeof in, "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", key);
  uint8_t d[20];
  mbedtls_sha1((const unsigned char*)in, strlen(in), d);
  size_t olen = 0;
  mbedtls_base64_encode((unsigned char*)out, 32, &olen, d, 20);
  out[olen] = 0;
}
