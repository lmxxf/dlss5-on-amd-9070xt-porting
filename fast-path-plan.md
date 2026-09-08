# 快速版执行方案（2026-09-08，光之朱雀，用户 09:04 拍板）

用户决定：不再追求与 5090 逐字节一致，优化贴合 9070 XT 自身特性。exact 链（186ms，15 帧 exact，`run_shared_scratch_network.ps1`）冻结为裁判，不再改。

## 不变的边界

- 原权重、71 块网络结构、输入依赖、层间数据流一律不动。
- 只允许硬件算术差异：累加精度/顺序、舍入方式、中间层存储格式。
- 不走"替代算法凑画面"（09-05 8×8 方格那次的教训）。
- 每一步都是新 flag + 新 release 目录 + 新 commit；exact 链的 flag 组合保留可回退。

## 验收标准（替代逐字节一致）

1. 每个候选跑同一套 15 帧 off/on/reset 测试，对 exact 链输出算：最终 RGB 的 max|Δ|、RMSE、PSNR，以及 ≥1/255 差异像素占比。
2. 数值只如实报，不设自动门槛；画面由用户在游戏里拍板。
3. 出现"单块看着小、整网放大"的情况（闇的 ViT 两 pass 反例）就退回该块。

## 技术改动，按收益/风险排序

### 阶段 1：GEMM 换硬件累加（预期 GEMM 60ms → 20ms 以下）

对象：Swin C64/128/256 FFN（blocked expand/contract）、两个投影、QKV GEMM、ViT expand/reduce/QKV/projection、split FFWD、decoder 入口/上采样、C32 FFN 与四 pass 注意力里的 qkv/projection。

改法：`for k32: acc=H(acc+MMA)` → 一次 `MMA` 跑完整 K 维，FP32 累加器，末尾一次 H()。去掉每 32 步的软件舍入和分块循环。先保持输入 f16 不变（一步一变量）。

### 阶段 2：输入换 FP8 矩阵档（吞吐翻倍，带宽减半）

A/B 操作数改 `ComponentType::F8_E4M3`（linalg.h 已声明），权重打包成 E4M3（原始 archive 本来就是 FP8/FP16 混合，能精确打包的直接打包，f16 权重保持 f16 档不混）。中间层活化从 f16 存改 E4M3 存（值本来就在 FP8 格点，无损）。先在一个 C64 块上验证 9070 的 F8 WMMA 精度行为，再推广。

### 阶段 3：注意力核里的标量段

不受矩阵档影响的部分：exp 位映射、分母树、F 量化、归一化。允许的改动：exp 改硬件 exp2 近似、分母树改 wave 归约（顺序不同）、H() 改硬件 f32tof16（注意闇实测该驱动的 f32tof16 是朝零截断，不是 RNE，需要 +1 ulp 补偿或接受偏差）。每一项单独量误差。

### 阶段 4：只有时序接上才有的收益

真 DLSS 有 history 后可评估是否减少每帧处理量。当前每帧 reset，不在快速版范围。

## 预期与止损

- 阶段 1+2 做完预期整帧 120～140ms（7～8fps）；注意力标量段约 100ms 是剩余大头。
- 阶段 3 每项收益小、误差风险大，画面不可接受就停。
- 10fps 不靠这条线单独达成；到 8fps 附近再决定要不要动时序。

## 工具

- 误差报告脚本：`compare_fast_output.py`（待写）读 `gpu-network70*.f32` 与 exact 链输出，输出上述指标到 `release/<dir>/fast-validation.json`。
- 测试入口沿用 `run_native_temporal_network70.ps1` 链，但 `analyze_native_network_profile.py` 的逐字节断言对快速版关闭，改调用误差脚本。
