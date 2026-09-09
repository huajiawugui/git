/* ================================================================
 * CPG 中枢模式发生器(移植自 RoFish,与 web/index.html makeCPG() 同构)
 * 三个耦合相位振荡器分别驱动 尾/中/头 三段
 * ================================================================ */
#pragma once
#include <Arduino.h>
#include "config.h"

struct Cpg {
  float W;          // 摆动频率 rad/s
  float ampDeg;     // 摆动幅值(度)
  float biasDeg;    // 偏置(度,连续转向)
  float pb;         // 后退耦合比例 0~1(RoFish 语义:后退时耦合减弱)
  bool forward;     // 前进/后退

  float sA[3], sr[3], sx[3];     // 相位 / 幅值 / 偏置
  float srD[3], sxD[3];          // 其一阶导数

  void init();                   // 默认参数 + 状态清零
  void step(float dt);           // 积分一步(建议 dt=0.01s)
  void out(float deg[3]);        // 输出目标角 [尾,中,头],限幅 60~120°
};
