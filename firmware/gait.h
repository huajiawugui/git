/* ================================================================
 * 泳姿数据库 + 非阻塞状态机
 * S/C 步进表与避障逻辑与 web/index.html 的 Engine 完全同构
 * ================================================================ */
#pragma once
#include <Arduino.h>
#include "cpg.h"

enum GaitMode : uint8_t {
  M_IDLE = 0, M_S, M_C_L, M_C_R, M_CPG, M_MANUAL, M_AVOID
};

const char* gaitName(uint8_t m);   // 遥测 gait 字段

class GaitEngine {
public:
  void init();

  /* 泳姿指令:"S" / "C_L" / "C_R" / "CPG_F" / "CPG_B" / "STOP" / "MANUAL" */
  void command(const char* cmd);

  /* CPG 在线调参(缺省项由调用方决定,见 setCpg 实现) */
  void setCpg(float w, float amp, float bias, float p);

  /* 手动舵机(id 0尾/1中/2头),自动切入 MANUAL 模式 */
  bool manualSet(int id, float angle);

  /* 每 50ms 调用一次;distCm = 红外距离(-1 表示超量程) */
  void tick(uint32_t now, float distCm);

  uint8_t mode = M_IDLE, resumeMode = M_IDLE;
  bool avoidTriggeredNow = false;   // 本 tick 是否触发避障(用于 event 上报)
  float servo[3];                   // 目标角 [尾,中,头]
  Cpg cpg;

private:
  void startGait(uint8_t m, uint32_t stepMs);
  uint16_t stepIndex = 0;
  uint32_t nextStepMs = 0;
  uint32_t stepMs = STEP_MS_S;
  uint32_t avoidCooldownUntil = 0;
  bool avoidLatched = false;
};
