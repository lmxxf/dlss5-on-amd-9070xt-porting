# fp8-sat-mode（2026-09-27）：FP8 饱和转换改用 MODE 位——不逐位，放弃；改成单条 med3——逐位，−0.3%

来源：mochizuki0323/DLSSNR-AMD（Vulkan 版）`fswin_t.comp` 注释：SPV_EXT_float8 饱和转换在 RADV 26 编成裸 `v_cvt_pk_fp8_f32` + MODE.FP16_OVFL，去掉每值 clamp，C32 −7%、其他级 3～7%。本实验在 c32-wave1（0.32 生产配方）上验证能否照搬。实验目录 `Development/HIP/experiments/fp8-sat-mode/`，9070 工作目录 `D:\DLSSNR-Lab\hip-backend\fp8-sat-mode`。

## 1. 语义探针（`probe.hip` / `probe.cpp`，gfx1201）

MODE = hwreg 1，FP16_OVFL = bit 23（`s_setreg_imm32_b32 hwreg(HW_REG_MODE, 23, 1), 1`；默认 MODE=0x010000f0）。

| 输入 | 现行 clamp+cvt | 裸 cvt，OVFL=0 | 裸 cvt，OVFL=1 | med3+cvt | f16 RNE，OVFL=0 / 1 |
|---|---|---|---|---|---|
| 有限值 ≤448 | 同 | 同 | 同 | 同 | — |
| 470 / 1000 / 1e30 | 0x7e | 0x7f（NaN） | 0x7e | 0x7e | 70000、1e30：0x7c00(Inf) / **0x7bff(65504)** |
| ±Inf | 0x7e / 0xfe | 0x7f / 0xff | **0x7f / 0xff** | 0x7e / 0xfe | 不变 |
| NaN | 0xfe（−448） | 0xff | **0xff** | 0xfe | 不变 |

- 指令级 `clamp` 修饰位：gfx12 汇编器拒绝（`v_cvt_pk_fp8_f32 ... clamp` invalid operand），不可用。
- OVFL=1 对有限值与 clamp 逐字节相同；但 ±Inf/NaN 变成 E4M3 NaN，且**核内所有 f16 RNE 转换溢出时饱和到 65504 而不是 Inf**（RTZ 转换本来就饱和，不受影响）。
- `__builtin_amdgcn_fmed3f(x,-448,448)` 对全部输入（含 NaN→−448、±Inf→±448）与现行 fminf/fmaxf 形式同字节。

## 2. 变体（宏 `HIP_FP8_SAT_MODE`，`hip/c32_fused_ffn_attention.hip` + `wave_owned_c32.inc` 入口 `fp8_sat_mode()`，默认 0）

变体 0 编出的 c32-wave1 与剑星现装 0.32 模块代码段相同（`hip/compare-modules.py`）。

| 变体 | 内容 | chain / finish / post / prefix 指令 | VGPR | 逐位（7 组 × 12 帧） |
|---|---|---|---|---|
| 0 | 生产 | 1775 / 2333 / 2260 / 2647 | 165～176 | — |
| 1 | 入口设 OVFL，F()/fp8() 去 clamp | 1581 / 2121 / 2056 / 2375（−8～−11%） | +4～+6 | 900/1080 静态、运动同；**720-motion 不同**（250 万值、最大差 0.18） |
| 2 | 只设 OVFL，clamp 保留（诊断） | — | — | **720-motion 不同** → 差异来自 OVFL 对 f16 RNE 溢出的副作用，不是去 clamp |
| 3 | 不动 MODE，clamp 写成 fmed3 | 1738 / 2283 / 2219 / 2598（−2%） | 不变 | **全部相同**（含 720、900/1080 历史） |

基线里 LLVM 已把大部分 clamp 折成 `v_med3_num_f32`（每核约 190 条），变体 1 的大头收益是连这些 med3 一起删掉；变体 3 只收掉没折成的 `v_max_num(x,x)`+`v_maxmin_num` 对。

720 档的数据里确有超出 f16 范围的中间值（padding 区或真实大值），现行链路靠 Inf→clamp 448 得到结果；OVFL 把它们改成 65504 后后续算术不同。mochizuki 的链路本来就不对齐 NVIDIA 的 f16，所以他们能接受；我们的逐位约束下不行。若要保留 MODE 路线，只能在每次 FP8 转换前后切换 MODE（RADV 就是这么做的，363 条 s_setreg），代价与收益需另测，未做。

## 3. 计时（变体 3，生产 host，1000 帧去前 200，ABBA 槽 0/3 基线、1/2 候选）

| 批次 | 900 | 1080 |
|---|---|---|
| 1 | 10.744 → 10.707（−0.037） | 15.009 → 14.964（−0.045） |
| 2 | 10.811 → 10.781（−0.029） | 15.045 → 15.010（−0.035） |

约 −0.3%，两批同向；四槽最终 hash 相同。

## 结论

- MODE.FP16_OVFL 路线：**不逐位，不采用**（720 档实测输出变化，原因已定位为 f16 溢出语义）。
- med3 写法（变体 3）：逐位，−0.03～−0.04ms，可作为下次 c32-wave1 重编的生产候选（改默认需把 `HIP_FP8_SAT_MODE 3` 加进 `build-modules.ps1` 的 c32-wave1 配方或改默认值）。未装游戏、未发包。
- 推广：C64～C256 wave-owned、deep_fast 里同样有 fminf/fmaxf 形式的 F()/fp8()（`multihead_fused_attention.hip`、`multihead_fast_padded.hip`、`deep_fast.hip`），按同法改成 fmed3 预计各自收益更小，待逐核看未折叠 clamp 数量再决定。
