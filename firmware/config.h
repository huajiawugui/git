/* ================================================================
 * 全局配置:引脚 / 泳姿节拍 / 避障阈值 / WiFi / 标定系数 / 仿真开关
 * 所有可调参数集中于此,与 web/index.html 的 CFG 保持一致
 * ================================================================ */
#pragma once
#include <Arduino.h>

/* —— 舵机(顺序 [尾, 中, 头] = servo[0..2]) —— */
static const uint8_t SERVO_PIN[3] = {25, 26, 27};
static const float   SERVO_MIN   = 60.0f;   // 与网页 CFG.SERVO_MIN/MAX 一致
static const float   SERVO_MAX   = 120.0f;

/* —— 红外测距 GP2Y0A21YK0F —— */
static const uint8_t IR_PIN = 34;          // 必须用 ADC1 通道(WiFi 占用 ADC2)
static const float   IR_K   = 13.0f;       // 两点标定系数:d = k/V + b
static const float   IR_B   = 0.0f;        // 标定方法见 docs/标定记录.md,标定后修改此处
static const float   IR_MAX_CM = 80.0f;    // 超过量程返回 -1

/* —— 避障(与网页 CFG 一致) —— */
static const float    OBSTACLE_TRIGGER_CM = 5.0f;   // 触发距离
static const float    OBSTACLE_RELEASE_CM = 8.0f;   // 解除距离(滞回)
static const uint32_t AVOID_COOLDOWN_MS   = 2000;   // 避障后冷却期

/* —— 泳姿节拍 —— */
static const uint32_t STEP_MS_S    = 50;    // S 形每步时长
static const uint32_t STEP_MS_C    = 80;    // C 形/避障每步时长
static const uint32_t TICK_MS      = 50;    // 主循环节拍(20Hz)
static const uint32_t TELE_IDLE_MS = 200;   // 待机遥测周期(5Hz)

/* —— WiFi AP —— */
static const char* AP_SSID = "FishBuoy";
static const char* AP_PASS = "12345678";
static const IPAddress AP_IP(192, 168, 4, 1);
static const IPAddress AP_GW(192, 168, 4, 1);
static const IPAddress AP_MASK(255, 255, 255, 0);
static const uint16_t HTTP_PORT = 80;      // LittleFS 静态网页 + /api/*
static const uint16_t WS_PORT   = 81;      // WebSocket 遥测/指令

/* —— 调试 —— */
static const uint32_t SERIAL_BAUD = 115200;

/* 传感器模式:
 * 1 = 仿真模式(无硬件联调逻辑):距离由串口 `dist <cm>` 命令注入
 * 0 = 真实模式:GP2Y0A21 经 GPIO34(ADC1)读取
 * 接好真实传感器后改为 0,或用编译参数 -DSIM_SENSOR=0 覆盖 */
#ifndef SIM_SENSOR
#define SIM_SENSOR 1
#endif
