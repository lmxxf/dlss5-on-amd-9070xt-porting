# 0.02 之后的改进计划（2026-09-08 18:05，光之朱雀，Zero 定稿）

起点：tag 0.02。游戏内快速版 + 时序约 8fps（网络约 112ms + 时序约 30ms + 回写）。exact 链冻结当裁判（tag 0.01，186ms）。验收方式不变：测试台量数（`compare_fast_output.py`，时序帧用 gpu-network70-temporal.f32），画面 Zero 在游戏里按 F6 判。所有改动加 flag、独立 release 目录、独立 commit。

## 第一步：时序两个 pass 去 double（预期 30 → 5ms，约 9.5fps；半小时）

- 对象：`native_temporal_coordinates.hlsl`、`native_temporal_sample.hlsl`。现状为逐位复现 NVIDIA：UV 定点到 1/2097152、权重定点到 1/256、double 累加、五点重建、倒数表归一化；RDNA4 的 FP64 是 FP32 的 1/16，这就是 30ms 的来源。
- 改法：加编译期宏 `NATIVE_FAST_TEMPORAL`，坐标 pass 用 float 双线性取运动向量；采样 pass 用 float 双线性（或保留五点但 float），去掉倒数表。宿主 `native_temporal_coordinates.h` / `native_temporal_sample.h` 用 `DLSS5_FAST_TEMPORAL=1` 选宏；`native_temporal_feed.hlsl` 不变。
- 验收：测试台时序帧对 exact-temporal 的 PSNR（现快速版 42.2dB），以及运动向量探针的对齐误差（`release/temporal-probe-48648/`，管线 warp 现为 0.0541，不该明显变差）。
- 风险：五点重建换双线性，history 略软。可先只改坐标 pass 看收益。

## 第二步：块内 dispatch 两两融合（剩余最大项，几天）

- 现状每个多头 Swin 块约 10 个 dispatch，每段落显存：pack → wave QKV → normalize → attention → 注意力投影 → FP8 pack → expand → contract → FFN 投影。38 块约 50ms；C32 族（preblock/后 4 段/post70）另约 35ms。
- 融合顺序按收益：(a) QKV GEMM + normalize（normalize 只是每 token 一个 wave 的归一化，可以做成 QKV 核的尾段，省一次 3C 宽的写读）；(b) expand + contract（隐藏层 4C 宽，最大的中间张量，在 LDS 内分块完成）；(c) attention + 注意力投影（投影是单 K32，可以在 AV 之后直接做）。C32 四 pass 同理。
- 每融一对：exact 模式下必须仍逐字节 exact（融合不改数值），先在 exact 链验证再开快速版。

## 第三步：FP8 中间层扩到注意力（一天，小头）

- 现在 FFN 和 ViT 的中间层已用 E4M3 存；注意力的 normalize 输出（Q/K/V）和 AV 输出还是 f16。全 FP8 后带宽再减半、F() 调用合并。仅快速版。

## 第四步：H()/F() 剩余调用清理（半天，小头）

- 快速版里剩余的末尾 H() 对精度不敏感处可直接用硬件 f32tof16（朝零截断），FP8 转换只在存储时做一次。

## 不做 / 等 Zero 的

- 原生 1080p 输入：Zero 认为暂无影响（原生渲染更慢），不动。
- LDS/带宽/算力：硬件。
- jitter：未用，DLSS 的 jitter 语义未追；画面正常先放着。

## 到 10fps 的账

网络 112 + 时序 5（第一步后）+ 回写约 5 ≈ 122ms → 8.2fps；第二步做完网络若到 80 → 90ms → 11fps。10fps 主要靠第二步。
