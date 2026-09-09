/* ================================================================
 * 鱼群智联 · 仿生鱼固件(ESP32,Arduino 框架,零第三方库)
 *
 * 50ms 节拍调度,全程非阻塞(主循环零 delay):
 *   红外测距 → 泳姿引擎(S/C/CPG/避障)→ LEDC 舵机输出 → 串口/WS 遥测
 * 串口协议与 WebSocket 协议同构,见 docs/通信协议.md §8
 *
 * 串口命令(115200):
 *   gait S | C_L | C_R | CPG fwd|back     切换泳姿
 *   stop                                  停止回中
 *   servo <id> <angle>                    手动舵机(id 0尾/1中/2头)
 *   cpg <W> <amp> <bias> <pb>             在线调参
 *   dist <cm>                             注入距离(仅 SIM_SENSOR=1)
 *   status                                立即输出遥测
 * ================================================================ */
#include "config.h"
#include "servo.h"
#include "sensor.h"
#include "cpg.h"
#include "gait.h"
#include "wifi_server.h"

GaitEngine engine;
IrSensor sensor;
ServoDriver servos;
FishNet net;

static uint32_t lastTick = 0;
static uint32_t lastTele = 0;

/* ---------- 遥测构造(WS / HTTP /api/status 共用) ---------- */
static void buildTele(char* buf, size_t cap) {
  snprintf(buf, cap,
    "{\"t\":\"tele\",\"v\":1,\"ts\":%lu,\"servo\":[%d,%d,%d],"
    "\"dist_cm\":%.1f,\"gait\":\"%s\",\"obstacle\":%s}",
    (unsigned long)millis(),
    (int)lroundf(engine.servo[0]), (int)lroundf(engine.servo[1]), (int)lroundf(engine.servo[2]),
    sensor.latest, gaitName(engine.mode),
    engine.mode == M_AVOID ? "true" : "false");
}

/* ---------- 串口风格命令解析(与网页 JSON 指令共用语义) ---------- */
static void runCommand(const char* cmd) {
  char arg[16];
  if (strcmp(cmd, "stop") == 0)   { engine.command("STOP"); return; }
  if (strcmp(cmd, "status") == 0) { net.sendTele(); return; }

  if (sscanf(cmd, "gait %15s", arg) == 1) {
    if (strcmp(arg, "S") == 0)        engine.command("S");
    else if (strcmp(arg, "C_L") == 0) engine.command("C_L");
    else if (strcmp(arg, "C_R") == 0) engine.command("C_R");
    else if (strcmp(arg, "CPG") == 0) {
      char dir[8] = "";
      sscanf(cmd, "gait CPG %7s", dir);
      engine.command(strcmp(dir, "back") == 0 ? "CPG_B" : "CPG_F");
    }
    return;
  }

  int id; float ang;
  if (sscanf(cmd, "servo %d %f", &id, &ang) == 2) { engine.manualSet(id, ang); return; }

  float w, amp, bias, pb;
  if (sscanf(cmd, "cpg %f %f %f %f", &w, &amp, &bias, &pb) == 4) {
    engine.setCpg(w, amp, bias, pb);
    return;
  }

  float d;
  if (sscanf(cmd, "dist %f", &d) == 1) {
    sensor.simCm = d;
#if !SIM_SENSOR
    Serial.println("[warn] dist 命令仅在 SIM_SENSOR=1 时生效");
#endif
    return;
  }

  Serial.printf("[warn] 未知命令:%s\n", cmd);
}

/* ---------- WebSocket/HTTP 入口:JSON → 命令(极简字段提取) ---------- */
static bool jsonStr(const char* j, const char* key, char* out, size_t cap) {
  char pat[24];
  snprintf(pat, sizeof pat, "\"%s\":\"", key);
  const char* p = strstr(j, pat);
  if (!p) return false;
  p += strlen(pat);
  size_t n = 0;
  while (*p && *p != '"' && n + 1 < cap) out[n++] = *p++;
  out[n] = 0;
  return true;
}
static bool jsonNum(const char* j, const char* key, float* v) {
  char pat[24];
  snprintf(pat, sizeof pat, "\"%s\":", key);
  const char* p = strstr(j, pat);
  if (!p) return false;
  *v = atof(p + strlen(pat));
  return true;
}

static void onWsCommand(const char* json) {
  char cmd[12] = "";
  if (!jsonStr(json, "cmd", cmd, sizeof cmd)) return;

  if (strcmp(cmd, "gait") == 0) {
    char g[8] = "";
    if (!jsonStr(json, "gait", g, sizeof g)) return;
    if (strcmp(g, "CPG") == 0) {
      char dir[8] = "";
      jsonStr(json, "dir", dir, sizeof dir);
      engine.command(strcmp(dir, "back") == 0 ? "CPG_B" : "CPG_F");
    } else {
      engine.command(g);   // S / C_L / C_R
    }
  } else if (strcmp(cmd, "stop") == 0) {
    engine.command("STOP");
  } else if (strcmp(cmd, "servo") == 0) {
    float id = -1, ang = 90;
    jsonNum(json, "id", &id);
    jsonNum(json, "angle", &ang);
    if (id >= 0) engine.manualSet((int)id, ang);
  } else if (strcmp(cmd, "cpg") == 0) {
    float v;   // 字段可缺省,只改传入项
    if (jsonNum(json, "W", &v))    engine.setCpg(v, engine.cpg.ampDeg, engine.cpg.biasDeg, engine.cpg.pb);
    if (jsonNum(json, "amp", &v))  engine.setCpg(engine.cpg.W, v, engine.cpg.biasDeg, engine.cpg.pb);
    if (jsonNum(json, "bias", &v)) engine.setCpg(engine.cpg.W, engine.cpg.ampDeg, v, engine.cpg.pb);
    if (jsonNum(json, "pb", &v))   engine.setCpg(engine.cpg.W, engine.cpg.ampDeg, engine.cpg.biasDeg, v);
  } else if (strcmp(cmd, "status") == 0) {
    net.sendTele();
  }
}

/* ---------- 串口非阻塞接收 ---------- */
static char serBuf[64];
static uint8_t serLen = 0;
static void handleSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (serLen > 0) { serBuf[serLen] = 0; serLen = 0; runCommand(serBuf); }
    } else if (serLen < 63) {
      serBuf[serLen++] = c;
    }
  }
}

/* ---------- 主程序 ---------- */
void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(200);                          // 等串口稳定(仅 setup,主循环零 delay)
  Serial.println("\n[FishBuoy] 鱼群智联固件启动");

  servos.init();
  sensor.init();
  engine.init();
  net.setCmdHandler(onWsCommand);
  net.setTeleBuilder(buildTele);
  net.init();

  Serial.printf("[net] AP 已开启:SSID=%s IP=%s HTTP=%u WS=%u\n",
                AP_SSID, AP_IP.toString().c_str(), HTTP_PORT, WS_PORT);
#if SIM_SENSOR
  Serial.println("[sensor] SIM 仿真模式:用 `dist <cm>` 命令注入距离");
#else
  Serial.println("[sensor] 真实模式:GP2Y0A21 经 GPIO34(ADC1)读取");
#endif
  Serial.println("[help] gait S|C_L|C_R|CPG fwd|back / stop / servo id ang / cpg W amp bias pb / dist cm / status");
}

void loop() {
  uint32_t now = millis();
  net.loop(now);
  handleSerial();

  if (now - lastTick >= TICK_MS) {
    lastTick = now;
    float d = sensor.readCm();
    engine.tick(now, d);
    servos.writeAll(engine.servo);

    /* 遥测:泳姿中 20Hz,待机 5Hz */
    uint32_t period = (engine.mode == M_IDLE) ? TELE_IDLE_MS : TICK_MS;
    if (net.wsConnected() && now - lastTele >= period) {
      lastTele = now;
      net.sendTele();
    }

    /* 避障触发事件 */
    if (engine.avoidTriggeredNow) {
      net.sendWs("{\"t\":\"event\",\"v\":1,\"evt\":\"obstacle\"}");
    }

    /* 串口遥测:S=<尾>,<中>,<头> D=<距离> G=<泳姿> */
    Serial.printf("S=%d,%d,%d D=%.1f G=%s\n",
      (int)lroundf(engine.servo[0]), (int)lroundf(engine.servo[1]), (int)lroundf(engine.servo[2]),
      d, gaitName(engine.mode));
  }
}
