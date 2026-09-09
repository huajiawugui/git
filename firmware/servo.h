/* ================================================================
 * 3 通道舵机驱动(无需第三方库,直接用 ESP32 LEDC PWM)
 * 50Hz / 16bit;SG90 脉宽映射:0°=500us,180°=2500us;角度限幅 60~120°
 * 供电注意:舵机必须走独立 5V 电源轨并共地(见 README 接线表)
 * ================================================================ */
#pragma once
#include <Arduino.h>
#include "config.h"

class ServoDriver {
public:
  void init();                       // 3 通道全部回中
  void writeAll(const float deg[3]); // [尾,中,头] 目标角,内部限幅
};
