/* ================================================================
 * 红外测距 GP2Y0A21YK0F:ADC1 读取 + 32 次均值滤波 + 两点标定换算
 * SIM_SENSOR=1 时由串口 dist 命令注入距离(无硬件逻辑联调)
 * ================================================================ */
#pragma once
#include <Arduino.h>
#include "config.h"

class IrSensor {
public:
  void init();
  float readCm();          // 返回距离 cm;超量程/无效返回 -1
  float latest = -1.0f;    // 最近一次结果(遥测用)
  float simCm = 40.0f;     // SIM_SENSOR=1 时的注入距离
};
