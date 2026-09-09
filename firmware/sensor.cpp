#include "sensor.h"

void IrSensor::init() {
#if SIM_SENSOR
  latest = simCm;
#else
  analogSetPinAttenuation(IR_PIN, ADC_11db);   // 0~3.3V 量程,12bit(0~4095)
  latest = -1.0f;
#endif
}

float IrSensor::readCm() {
#if SIM_SENSOR
  latest = simCm;
  return latest;
#else
  /* 32 次均值滤波,压低供电纹波与 WiFi 邻近干扰 */
  uint32_t sum = 0;
  for (int i = 0; i < 32; i++) sum += analogRead(IR_PIN);
  float v = (sum / 32.0f) / 4095.0f * 3.3f;

  /* 两点标定换算:d = k/V + b(系数见 config.h IR_K/IR_B) */
  float d = IR_K / v + IR_B;
  if (v < 0.2f || d > IR_MAX_CM) d = -1.0f;    // 悬空/超量程
  latest = d;
  return d;
#endif
}
