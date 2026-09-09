/* 泳姿核心逻辑验证(与浏览器无关,node gait_test.js 运行)
 * 从 index.html 提取 0~3 段(CFG/泳姿表/CPG/Engine)拼接执行
 */
var fs = require("fs");
var path = require("path");

var html = fs.readFileSync(path.join(__dirname, "..", "index.html"), "utf8");
var js = html.split(/<script>/)[1].split("</script>")[0];          // 内联主脚本(three.min.js 的标签带 src,不匹配字面 <script>)
var start = js.indexOf("/* ================= 0. 配置 ================= */");
var end = js.indexOf("/* ================= 4. three.js 场景 ================= */");
var core = js.slice(start, end);

var passed = 0, failed = 0;
function assert(cond, msg) {
  if (cond) { passed++; console.log("PASS: " + msg); }
  else { failed++; console.error("FAIL: " + msg); }
}
function clamp(x, lo, hi) { return x < lo ? lo : (x > hi ? hi : x); }
var WSC = { live: false, telemetry: null };   // 测试环境桩

eval(core);   // 载入 CFG / GAIT_* / makeCPG / Engine

/* 1. S 泳姿表:尾摆幅最大、头小幅摆动 */
var tailMin = 999, tailMax = -999, headMin = 999, headMax = -999;
GAIT_S_MOVE.forEach(function (s) {
  tailMin = Math.min(tailMin, s[0]); tailMax = Math.max(tailMax, s[0]);
  headMin = Math.min(headMin, s[2]); headMax = Math.max(headMax, s[2]);
});
assert(tailMax - tailMin >= 55, "S 泳姿尾鳍摆幅约 60 度(实际 " + (tailMax - tailMin) + ")");
assert(headMax - headMin <= 25, "S 泳姿头部小幅摆动(实际 " + (headMax - headMin) + ")");
var kTailMax = GAIT_S_MOVE.findIndex(function (s) { return s[0] === tailMax; });
assert(GAIT_S_MOVE[kTailMax][2] < 90, "尾摆到最右时头已回摆(头领先尾 120°),头=" + GAIT_S_MOVE[kTailMax][2]);

/* 2. S 泳姿状态机持续循环 */
Engine.command("S");
var seq = [];
for (var t = 0; t < 1200; t += 50) { Engine.update(t, 0.05); seq.push(Engine.servo.join(",")); }
assert(Engine.mode === "S", "S 泳姿持续循环,模式保持");
assert(new Set(seq).size > 10, "S 泳姿产生连续变化的舵机序列(" + new Set(seq).size + " 个不同帧)");

/* 3. C 形左转一次性完成 */
Engine.command("C_L");
var steps = 0;
for (t = 0; t < 3000 && Engine.mode !== "IDLE"; t += 80) { Engine.update(t, 0.08); steps++; }
assert(Engine.mode === "IDLE", "C 形左转序列结束后回到待机(" + steps + " 步)");

/* 4. 避障:5cm 触发 → 序列结束恢复原泳姿;2s 冷却 */
Engine.command("S");
Engine.distCm = 4;
Engine.update(100000, 0.05);
assert(Engine.mode === "AVOID", "距离 4cm 触发自动避障");
Engine.distCm = 20;
var guard = 0;
while (Engine.mode === "AVOID" && guard < 100) { Engine.update(100000 + guard * 80, 0.08); guard++; }
assert(Engine.mode === "S", "避障结束后恢复 S 形泳姿(实际 " + Engine.mode + ")");
Engine.distCm = 4;
Engine.update(101000, 0.05);
assert(Engine.mode === "S", "冷却期内不重复触发避障");
Engine.distCm = 40;

/* 5. CPG 前进:幅值收敛 ~25 度、频率 ~2.5 rad/s、尾领先 */
Engine.command("CPG_F");
Engine.cpg.W = 2.5; Engine.cpg.ampDeg = 25; Engine.cpg.biasDeg = 0;
var samples = [];
for (t = 0; t < 20000; t += 10) { Engine.update(300000 + t, 0.01); samples.push(Engine.servo.slice()); }
var tailArr = samples.slice(1000).map(function (s) { return s[0]; });
var ampT = (Math.max.apply(null, tailArr) - Math.min.apply(null, tailArr)) / 2;
assert(ampT > 18 && ampT < 32, "CPG 尾鳍幅值收敛到 ~25 度(实际 " + ampT.toFixed(1) + ")");
var dedup = [];
tailArr.forEach(function (v) {
  if (dedup.length === 0 || dedup[dedup.length - 1] !== v) dedup.push(v);
});
var peaks = 0;
for (var i = 1; i < dedup.length - 1; i++) {
  if (dedup[i] > dedup[i - 1] && dedup[i] >= dedup[i + 1]) peaks++;
}
assert(peaks >= 3 && peaks <= 6, "CPG 频率 ≈2.5 rad/s,10 秒约 4 个周期(峰值 " + peaks + " 个)");
/* 尾领先于中段:corr(中段[q]·尾[q+lag]) 应在 lag≈280ms 处最大 */
function corrLag(lagMax, from, to) {
  var c = [];
  for (var lag = 0; lag <= lagMax; lag += 10) {
    var s = 0;
    for (var q = from; q < to; q++) s += samples[q][1] * samples[q + lag][0];
    c.push(s);
  }
  var m = Math.max.apply(null, c), idx = c.indexOf(m) * 10;
  return { peak: idx, vals: c };
}
var rF = corrLag(500, 1000, 1500);
assert(rF.peak > 150 && rF.peak < 450, "CPG 前进:中段滞后尾鳍约 280ms(实测 " + rF.peak + "ms)");

/* 6. CPG 后退:相位关系反转——交换序列后 corr(尾[q]·中[q+lag]) 峰值回到 ~284ms */
Engine.command("CPG_B");
samples = [];
for (t = 0; t < 20000; t += 10) { Engine.update(400000 + t, 0.01); samples.push(Engine.servo.slice()); }
function corrSwapLag(lagMax, from, to) {
  var c = [];
  for (var lag = 0; lag <= lagMax; lag += 10) {
    var s = 0;
    for (var q = from; q < to; q++) s += samples[q][0] * samples[q + lag][1];  // 尾[q]·中[q+lag]
    c.push(s);
  }
  return c.indexOf(Math.max.apply(null, c)) * 10;
}
var backPeak = corrSwapLag(500, 1000, 1500);
assert(backPeak > 150 && backPeak < 450, "CPG 后退:相位矩阵反转生效,尾[q]·中[q+lag] 峰值 ≈284ms(实测 " + backPeak + "ms)");

/* 7. CPG 偏置(转向)输出 */
Engine.command("CPG_F");
Engine.cpg.biasDeg = 15;
var biasOut = [];
for (t = 0; t < 10000; t += 10) { Engine.update(500000 + t, 0.01); biasOut.push(Engine.servo[0]); }
var mean = biasOut.reduce(function (a, b) { return a + b; }, 0) / biasOut.length;
assert(Math.abs(mean - 105) < 6, "CPG 偏置 15 度:尾鳍均值偏向 105 度(实测 " + mean.toFixed(1) + ")");
Engine.cpg.biasDeg = 0;

console.log("\n===== 结果:" + passed + " 通过 / " + failed + " 失败 =====");
process.exit(failed > 0 ? 1 : 0);
