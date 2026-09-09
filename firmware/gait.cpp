#include "gait.h"

/* ================================================================
 * 泳姿数据库(与 web/index.html 的 GAIT_* 数值完全一致,勿单边修改)
 * 舵机顺序:[尾, 中, 头],90° 为中位。文档:docs/泳姿数据库.md
 * ================================================================ */

/* S 形前进:12 步 × 50ms = 600ms/周期
 * θi(k) = 90 + Ai·sin(2πk/12 + φi),A=[30,20,10],φ=[+120°,+60°,0°]
 * 头领先、波向尾传播 */
static const uint8_t PROGMEM T_S[12][3] = {
  {116,107,90},{105,110,95},{90,107,99},{75,100,100},{64,90,99},{60,80,95},
  {64,73,90},{75,70,85},{90,73,81},{105,80,80},{116,90,81},{120,100,85}
};

/* C 形左转:16 步 × 80ms = 1.28s(6 渐弯 + 4 保持 + 6 回正),一次性动作 */
static const uint8_t PROGMEM T_C_L[16][3] = {
  {90,90,90},{84,86,88},{78,82,86},{72,78,84},{66,74,82},{60,70,80},
  {60,70,80},{60,70,80},{60,70,80},{60,70,80},
  {66,74,82},{72,78,84},{78,82,86},{84,86,88},{90,90,90},{90,90,90}
};

/* C 形右转 */
static const uint8_t PROGMEM T_C_R[16][3] = {
  {90,90,90},{96,94,92},{102,98,94},{108,102,96},{114,106,98},{120,110,100},
  {120,110,100},{120,110,100},{120,110,100},{120,110,100},
  {114,106,98},{108,102,96},{102,98,94},{96,94,92},{90,90,90},{90,90,90}
};

static void pgmRow(const uint8_t (*t)[3], uint16_t idx, uint8_t out[3]) {
  for (int i = 0; i < 3; i++) out[i] = pgm_read_byte(&t[idx][i]);
}

const char* gaitName(uint8_t m) {
  switch (m) {
    case M_S:      return "S";
    case M_C_L:    return "C_L";
    case M_C_R:    return "C_R";
    case M_CPG:    return "CPG";
    case M_MANUAL: return "MANUAL";
    case M_AVOID:  return "AVOID";
    default:       return "IDLE";
  }
}

void GaitEngine::init() {
  mode = M_IDLE; resumeMode = M_IDLE;
  stepIndex = 0; nextStepMs = 0; stepMs = STEP_MS_S;
  avoidCooldownUntil = 0; avoidLatched = false;
  avoidTriggeredNow = false;
  cpg.init();
  servo[0] = servo[1] = servo[2] = 90.0f;
}

void GaitEngine::startGait(uint8_t m, uint32_t step) {
  mode = m; stepIndex = 0; stepMs = step; nextStepMs = 0;
}

void GaitEngine::command(const char* cmd) {
  if      (strcmp(cmd, "S")     == 0) startGait(M_S, STEP_MS_S);
  else if (strcmp(cmd, "C_L")   == 0) startGait(M_C_L, STEP_MS_C);
  else if (strcmp(cmd, "C_R")   == 0) startGait(M_C_R, STEP_MS_C);
  else if (strcmp(cmd, "CPG_F") == 0) { cpg.forward = true;  mode = M_CPG; }
  else if (strcmp(cmd, "CPG_B") == 0) { cpg.forward = false; mode = M_CPG; }
  else if (strcmp(cmd, "STOP")  == 0) {
    mode = M_IDLE; servo[0] = servo[1] = servo[2] = 90.0f;
  }
  else if (strcmp(cmd, "MANUAL") == 0) mode = M_MANUAL;
}

void GaitEngine::setCpg(float w, float amp, float bias, float p) {
  if (w > 0.0f) cpg.W = w;                     // w<=0 视为缺省,保持现值
  cpg.ampDeg = amp;
  cpg.biasDeg = bias;
  if (p >= 0.0f && p <= 1.0f) cpg.pb = p;
}

bool GaitEngine::manualSet(int id, float angle) {
  if (id < 0 || id > 2) return false;
  mode = M_MANUAL;
  servo[id] = constrain(angle, SERVO_MIN, SERVO_MAX);
  return true;
}

void GaitEngine::tick(uint32_t now, float distCm) {
  avoidTriggeredNow = false;
  const float d = distCm;

  /* —— 避障(滞回 + 冷却,与网页 Engine.update 一致) —— */
  if (mode != M_AVOID && d > 0.0f && d < OBSTACLE_TRIGGER_CM &&
      (int32_t)(now - avoidCooldownUntil) > 0 && !avoidLatched) {
    resumeMode = (mode == M_C_L || mode == M_C_R) ? M_IDLE : mode;
    startGait(M_AVOID, STEP_MS_C);
    avoidLatched = true;
    avoidTriggeredNow = true;
  }
  if (mode == M_AVOID && d > OBSTACLE_RELEASE_CM) avoidLatched = false;

  if (mode == M_S || mode == M_C_L || mode == M_C_R || mode == M_AVOID) {
    if ((int32_t)(now - nextStepMs) >= 0) {
      uint8_t row[3];
      if (mode == M_S)        pgmRow(T_S, stepIndex, row);
      else if (mode == M_C_R) pgmRow(T_C_R, stepIndex, row);
      else                    pgmRow(T_C_L, stepIndex, row);   // AVOID 用 C_LEFT
      servo[0] = row[0]; servo[1] = row[1]; servo[2] = row[2];
      stepIndex++;
      nextStepMs = now + stepMs;
      if (stepIndex >= (mode == M_S ? 12 : 16)) {
        stepIndex = 0;
        if (mode == M_C_L || mode == M_C_R) {
          mode = M_IDLE;                    // C 形转向为一次性动作
        } else if (mode == M_AVOID) {
          mode = resumeMode;                // 避障结束恢复原泳姿
          avoidCooldownUntil = now + AVOID_COOLDOWN_MS;
        }
      }
    }
  } else if (mode == M_CPG) {
    for (int k = 0; k < 5; k++) cpg.step(0.01f);   // 50ms 节拍内 5 次 10ms 积分
    cpg.out(servo);
  } else if (mode == M_IDLE) {
    servo[0] = servo[1] = servo[2] = 90.0f;
  }
  /* MANUAL:servo 由 manualSet 直接写入 */
}
