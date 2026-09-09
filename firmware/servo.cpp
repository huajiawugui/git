#include "servo.h"

void ServoDriver::init() {
  for (int i = 0; i < 3; i++) {
    ledcSetup(i, 50, 16);                  // 通道 i:50Hz,16bit 分辨率
    ledcAttachPin(SERVO_PIN[i], i);
  }
  float c[3] = {90.0f, 90.0f, 90.0f};
  writeAll(c);
}

void ServoDriver::writeAll(const float deg[3]) {
  for (int i = 0; i < 3; i++) {
    float d = constrain(deg[i], SERVO_MIN, SERVO_MAX);
    /* SG90:0°=500us,180°=2500us → us = 500 + d*2000/180 */
    uint32_t us = 500 + (uint32_t)(d * 2000.0f / 180.0f);
    ledcWrite(i, (uint32_t)(us * 65535UL / 20000UL));   // 20ms 周期 → 占空比
  }
}
