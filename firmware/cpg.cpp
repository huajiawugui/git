#include "cpg.h"

/* —— 模型参数(与 web/index.html makeCPG 完全一致) —— */
static const float AR = 20.0f;      // 幅值收敛系数(临界阻尼,时间常数 ≈0.1s)
static const float AX = 20.0f;      // 偏置收敛系数
static const float W_IJ = 5.0f;     // 耦合权重 1/s

/* 相位偏置矩阵:phi[i][j] 为 j 对 i 的相位偏置
 * 前进:头领先,波向尾传播(与真实鱼类游动一致)
 * 后退:取反,波反向传播 */
static const float PHI_F[3][3] = {
  { 0.0f,  0.71f,  1.41f},
  {-0.71f, 0.0f,   0.71f},
  {-1.41f,-0.71f,  0.0f }
};
static const float PHI_B[3][3] = {
  { 0.0f, -0.71f, -1.41f},
  { 0.71f, 0.0f,  -0.71f},
  { 1.41f, 0.71f,  0.0f }
};

void Cpg::init() {
  W = 2.5f; ampDeg = 25.0f; biasDeg = 0.0f; pb = 0.7f;
  forward = true;
  for (int i = 0; i < 3; i++) {
    sA[i] = 0.0f; sr[i] = 0.1f; sx[i] = 0.0f; srD[i] = 0.0f; sxD[i] = 0.0f;
  }
}

void Cpg::step(float dt) {
  const float (*phi)[3] = forward ? PHI_F : PHI_B;
  const float wScale = forward ? 1.0f : pb;   // 后退时耦合减弱(pb 语义)
  const float cR = ampDeg * PI / 180.0f;
  const float cX = biasDeg * PI / 180.0f;

  float sAn[3], srn[3], sxn[3], srDn[3], sxDn[3];
  for (int i = 0; i < 3; i++) {
    /* 相位方程:sA_d[i] = W + Σ w[i][j]·sr[j]·sin(sA[j]-sA[i]-phi[i][j]) */
    float sA_d = W;
    for (int j = 0; j < 3; j++)
      sA_d += wScale * W_IJ * sr[j] * sinf(sA[j] - sA[i] - phi[i][j]);
    sAn[i] = sA[i] + sA_d * dt;

    /* 幅值二阶临界阻尼收敛到 cR */
    float sr_dd = AR * ((AR / 4.0f) * (cR - sr[i]) - srD[i]);
    srDn[i] = srD[i] + sr_dd * dt;
    srn[i]  = sr[i] + srD[i] * dt;

    /* 偏置同理收敛到 cX */
    float sx_dd = AX * ((AX / 4.0f) * (cX - sx[i]) - sxD[i]);
    sxDn[i] = sxD[i] + sx_dd * dt;
    sxn[i]  = sx[i] + sxD[i] * dt;
  }
  for (int i = 0; i < 3; i++) {
    sA[i] = sAn[i]; sr[i] = srn[i]; sx[i] = sxn[i];
    srD[i] = srDn[i]; sxD[i] = sxDn[i];
  }
}

void Cpg::out(float deg[3]) {
  for (int i = 0; i < 3; i++) {
    float d = (sx[i] + sr[i] * sinf(sA[i])) * 180.0f / PI;
    deg[i] = constrain(90.0f + roundf(d), SERVO_MIN, SERVO_MAX);
  }
}
