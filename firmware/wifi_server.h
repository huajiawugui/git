/* ================================================================
 * 网络服务:AP + HTTP(80,LittleFS 静态 + /api/*)+ 手写 WebSocket(81)
 * - HTTP 用核心自带 WebServer.h(无需第三方库)
 * - WebSocket 手写实现(握手 + 文本帧收发 + ping/pong + 15s 超时),
 *   只处理网页指令这种小文本帧,免装 arduinoWebSockets 库
 * 协议见 docs/通信协议.md;命令解析由 firmware.ino 注入(串口/WS 共用)
 * ================================================================ */
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include "config.h"

class FishNet {
public:
  FishNet();

  void init();                       // AP + LittleFS + 路由 + WS 监听
  void loop(uint32_t now);           // 非阻塞:HTTP handleClient + WS 收发 + 超时
  bool wsConnected() const { return wsHandshaken; }
  bool sendWs(const char* text);     // 文本帧
  void sendTele();                   // 用 teleFn 构造 tele 报文并发送

  /* 回调注入:firmware.ino 提供命令解析与遥测构造 */
  void setCmdHandler(void (*fn)(const char* json)) { cmdFn = fn; }
  void setTeleBuilder(void (*fn)(char* buf, size_t cap)) { teleFn = fn; }

private:
  WebServer http;
  WiFiServer wsSrv;
  WiFiClient ws;
  bool wsHandshaken = false;
  uint32_t wsLastRx = 0;             // 15s 无帧判断连
  char frag[512]; uint16_t fragLen = 0;   // 分片帧累积缓冲
  String hsBuf; uint32_t hsT0 = 0;        // 握手头累积与超时

  void (*cmdFn)(const char* json) = nullptr;
  void (*teleFn)(char* buf, size_t cap) = nullptr;

  void serveFile(const char* path, const char* ct);
  void handleRoot();
  void handleJs();
  void handleStatus();
  void handleCmd();

  void wsHandshake(uint32_t now);
  void wsReceive(uint32_t now);
  bool wsReadExact(uint8_t* buf, size_t n);
  bool sendFrame(uint8_t op, const char* data, size_t len);
  static void sha1B64(const char* key, char out[32]);
};
