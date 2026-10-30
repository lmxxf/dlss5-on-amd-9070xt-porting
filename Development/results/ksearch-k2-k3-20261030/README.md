# K-Search Phase 2：分支 K2 与 K3 实装、ISA 审计、硬件实时钟与 ABBA 门禁（2026-10-30）

**结论：两个候选分支（K2 非转置 `ffn8` 残差直读 + FP32 死写消除、K3 `c32_wave1_post` 跨 `qt` 外提 `scales[64]`）均通过七档 42 帧 Gold `0 bit-diff`（`invalid=0`），但在四档 3 轮 4-slot ABBA `frame.exe` 稳态门禁中均未获全局端到端净收益，双双判定 `STOP -> Eliminated`，生产基线保持 `8a9c60fd` 不变。**

---

## 1. 分支 K2：C512 块内非转置 `ffn` 4.42MB FP32 死写消除 + `mh_attention_project` 直读 `ffn8`

### 1.1 几何布局核对与 ISA 审计（修正向量化误判）
- **关键几何核对**：在非转置 WMMA（`Transposed=false`）下，`mh_attention_project_frag_c512_w16_body` 的每个 lane `l` 持有固定列 `c = col + n*16 + (l&15)`，而累加器下标 `e = 0..7` 对应的是 **8 个连续的 token 行** `pp = first + (l>>4)*8 + e`（而非转置 WMMA 下的 8 个连续通道）。在 `ffn8` 的 `[16 token × 32 channel]` 512B 瓦片布局中，相邻 token 行的步长为 **32 字节**。因此，保持 `Transposed=false`（以保证 `av8` 连续 `b64` 向量化读取）时，每个 lane 的 32 个残差元素在 `ffn8` 中是跨 32B 步长的字节，编译为 **32 条 `global_load_u8` + `v_cvt_f32_fp8`**（替代原 `ffn` 的 32 条 `global_load_b32`），不能在非转置布局下合并为 `global_load_b64`。
- **尾部重算跨过 96 VGPR 悬崖**：通过重算尾部 lane 索引切断跨 K 循环的活跃变量，同时省去 `split_projection_frag_w16_nof32[_bskip]` 的 32 条 `global_store_b32` 与 `v_cvt_f32_fp8`。
- **COMGR（gfx1201）ISA 对比**：

| 模块 / 核函数 | 基线 VGPR | 候选 VGPR | 基线指令数 | 候选指令数 | `global_load` | `global_store` | Spill |
|---|---:|---:|---:|---:|---:|---:|---:|
| `deep_fast-packed-fast` : `split_projection_frag_w16_bskip` (mode=15) | 100 | **97** | 630 | 642 | 60 (`32×u8+4×b32+16×b128+8×b64`) | 34 (`32×b32+2×b128`) | 0 |
| `deep_fast-packed-fast` : `split_projection_frag_w16_nof32_bskip` (mode=31) | 98 (转置) | **97** (非转置) | 511 | **513** | 60 (`32×u8+4×b32+16×b128+8×b64`) | **2** (`2×b128`，省 32×`b32`) | 0 |
| `mh_fast` : `mh_attention_project_frag_c512_w16_u4_bout` (mode=15) | 101 | **93** | 756 | 730 | 60 (`36×b32+16×b128+8×b64`) | 32 (`32×b8`) | 0 |
| `mh_fast` : `mh_attention_project_frag_c512_w16_u4_fb8_bout` (mode=31) | 94 (转置) | **93** (非转置) | 724 | **749** | 60 (`32×u8+4×b32+16×b128+8×b64`) | 32 (`32×b8`) | 0 |
| `mh_fast` : `mh_attention_project_frag_c512_w16_u4_fb8` (mode=31) | 94 (转置) | **93** (非转置) | 775 | **880** | 60 (`32×u8+4×b32+16×b128+8×b64`) | 32 (`32×b32`) | 0 |

### 1.2 硬件实时钟探针（3 轮 ABBA，`MSG_RTN_GET_REALTIME` 100MHz）
在单核与整网纯 HIP 流上，K2 消除了 16 块（Small 4 块）的 `4.42 MB` FP32 死写与重读，硬件实时钟 3/3 轮全部加速：

| 模型 | 指标 | 基线 (mode=15) µs | 候选 K2 (mode=31) µs | 均值 Δµs | 赢轮数 |
|---|---|---:|---:|---:|---:|
| `original1088` (16 块) | `c512_ffn_proj` 合计 | 149.577 | 133.317 | **-16.260** | 3/3 |
| `original1088` (16 块) | `c512_attn_proj` 合计 | 171.533 | 168.213 | **-3.320** | 3/3 |
| `original1088` (16 块) | **C512 双核合计** | **321.110** | **301.530** | **-19.580** | **3/3** |
| `original1088` (16 块) | 纯 NN 全网总计 | 7903.450 | 7869.017 | **-34.433** | 3/3 |
| `small1080` (4 块) | `c512_ffn_proj` 合计 | 41.363 | 37.673 | **-3.690** | 3/3 |
| `small1080` (4 块) | `c512_attn_proj` 合计 | 49.607 | 47.097 | **-2.510** | 3/3 |
| `small1080` (4 块) | **C512 双核合计** | **90.970** | **84.770** | **-6.200** | **3/3** |
| `small1080` (4 块) | 纯 NN 全网总计 | 4753.803 | 4750.823 | **-2.980** | 2/3 |

### 1.3 七档 Gold 与四档 Frame 三轮 ABBA 稳态评测（及根因隔离实验）
- **七档 42 帧 Gold**：`0 bit-diff`，`invalid=0`，输出哈希与基线 100% 相同。
- **四档 Frame 三轮 ABBA（240 帧弃 80 取 160，960 稳态帧/臂）**：

| 档位 | 基线 A (ms) | 候选 K2 (ms) | mean Δµs | mean Δ% | p99 Δµs | mean 赢轮数 | p99 赢轮数 |
|---|---:|---:|---:|---:|---:|---:|---:|
| `original960` | 6.372378 | 6.346736 | **-25.642** | -0.40% | **-15.811** | 3/3 | 3/3 |
| `original1088` | 8.600840 | 8.839009 | **+238.169** | +2.77% | **+257.031** | 0/3 | 0/3 |
| `small1080` | 5.365602 | 5.422772 | **+57.169** | +1.07% | **+46.876** | 0/3 | 0/3 |
| `small900` | 4.130014 | 4.131390 | **+1.376** | +0.03% | **+55.457** | 1/3 | 0/3 |

- **异常回退的根因隔离实验（`k2b` / `k2c` / `k2d`）**：
  为什么 `original960` 稳定快 `-25.64 µs`（3/3 轮），而 `original1088` 反慢 `+238.17 µs`、`small1080` 反慢 `+57.17 µs`？我们做了三组对照实验：
  1. **`k2b`（宿主侧省掉 `CompactC512Body` 中 `mixed`、`contract`、`ffn` 的 `New()` 池分配）**：`original1088` 仍慢 `+248.7 µs`，`small1080` 仍慢 `+72.4 µs`。排除池分配器变化。
  2. **`k2c`（使用原封不动的基线 `.hsaco` 模块，仅设置环境变量 `NR_VIT_INTER_BYTE=31`）**：`original1088` 同样慢 **`+269.1 µs`**，`small1080` 同样慢 **`+56.3 µs`**！
  3. **`k2d`（使用 K2 新 `.hsaco` 模块，保持默认 `mode=15` 不开 `w16_fb8`）**：`original1088` 与基线仅差 `+6.5 µs`（噪声内），`small1080` 仅差 `+3.9 µs`。
  - **真相大白**：在 `CompactC512Body`（`hip_reference_network.h:788`）中，开启 `w16_fb8`（`NR_VIT_INTER_BYTE=31`）必须在每个 C512 块（Original 16 次/帧，Small 4 次/帧）额外调用两次 `HasFn("deep_fast", "split_projection_frag_w16_nof32...")` 和 `HasFn("mh_fast", "mh_attention_project_frag_c512_w16_u4_fb8...")`（共 32 次 `std::string` 构造 + `std::map<std::string, Handle>` 查找/每帧）！在 `original960`（`submit_pulse=1` 激活，C512 之前已切走提交批次）中，CPU 入队延迟被 GPU 执行完全掩盖，因此 GPU 端省下的 `-25.6 µs` 直接体现为帧时间下降；而在 `original1088` 和 `small1080`（`submit_pulse=0`，帧尾一次性信号提交）中，这 32 次/帧的 `HasFn` 字符串拼接与 `std::map` 查找延迟推迟了 CPU 端的 `hipSignalExternalSemaphoresAsync` 和 `post_query`，在 Windows HIP + D3D12 共享围栏跨队列唤醒上触发了约 `~15 µs/块`（16 块共 `+240 µs`，4 块共 `+60 µs`）的提交延迟放大！这与历史账本 `2026-10-01 rebuild-baseline` 中记载的“每帧 `HasFn` 字符串拼接查询导致 ABBA 全慢”机制完全吻合。
  - 按门禁纪律，K2 本轮 `STOP -> Eliminated`，不合入生产。

---

## 2. 分支 K3：`c32_wave1_post_b8_w16` 跨 `qt` 外提 `scales[64]`

### 2.1 ISA 与资源审计（LLVM 23）
- 在 `hip/wave_owned_c32.inc` 中，将 `Post && CW_VEC_INPUT` 下的 `scales` 读取外提到第一个 `for(uint qt=0; qt<4; qt++)` 循环之前（存入 `post_s0[2][8], post_s1[2][8]`）。
- **LLVM 23 编译审计（`c32-wave1-fast` / `c32-wave1-fast-norm900`）**：
  - `c32_wave1_post_b8_w16`：VGPR 从 `148 -> 149`（仍处于同一 `<=160` 门禁内，0 spill，private=0），静态指令数 `1455 -> 1454`。
  - **关键发现（为什么静态 `global_load` 仍是 69 条且动态无收益）**：反汇编对比证明，外提后 `qt` 循环内的 4 条 `global_load_b128` 确实消除了（动态减少 12 条 `global_load_b128`），但由于 `post_s0/s1`（16 个 float）在入口处一次性加载并跨 4 轮 `qt` 保活，LLVM 23 在入口处发出了 **8 条 `global_load_b128`（`s_clause 0x7`）**（因 `ci=0,1` 展开且地址计算被拆分），且原本在 `qt` 循环内紧挨 `in`（`low`）和 `skip` 的 `s_clause 0x3` 合并加载被拆散，跨迭代占用的 16 个 VGPR 挤压了循环内 FFN 累加器的调度空间。

### 2.2 硬件实时钟与四档 Frame 三轮 ABBA
- **七档 42 帧 Gold**：`0 bit-diff`，`invalid=0`。
- **硬件实时钟（3 轮 ABBA）**：
  - `original1088`：`c32_wave1_post_b8_w16` `710.257 -> 711.217 µs`（**`+0.960 µs`**，2/3 轮改善，第 3 轮 `+8.28 µs`）；C32 阶段 `-1.040 µs`；整网 `+2.547 µs`。
  - `small1080`：`c32_wave1_post_b8_w16` `744.510 -> 741.690 µs`（**`-2.820 µs`**，3/3 轮改善）；C32 阶段 `-6.127 µs`；整网 `-10.807 µs`。
- **四档 Frame 三轮 ABBA（960 稳态帧/臂）**：

| 档位 | 基线 A (ms) | 候选 K3 (ms) | mean Δµs | mean Δ% | p99 Δµs | mean 赢轮数 | p99 赢轮数 |
|---|---:|---:|---:|---:|---:|---:|---:|
| `original1088` | 8.599572 | 8.603152 | **+3.580** | +0.04% | -10.362 | 1/3 | 1/3 |
| `original960` | 6.373953 | 6.389508 | **+15.555** | +0.24% | +253.601 | 2/3 | 1/3 |
| `small1080` | 5.349708 | 5.347707 | **-2.000** | -0.04% | -16.447 | 1/3 | 1/3 |
| `small900` | 4.103837 | 4.103849 | **+0.012** | +0.00% | +7.503 | 2/3 | 2/3 |

- **判定**：`scales[64]` 仅 256 字节（2 条 128B cache line），在 `qt=0` 载入后 `qt=1..3` 100% 命中 L0/L1 cache；外提省掉的是 L0 命中读取，但付出了跨整个 FFN+QKV 循环保活 16 个 VGPR 的寄存器压力代价，硬件时钟（`original1088` `+0.96 µs`）与四档 ABBA（3 档 mean 正向退化或持平）均无净收益，判定 `STOP -> Eliminated`。

---

## 3. 护栏与清理核验
- 远端 `amd9070` 全程遵守 `game-check`、进程检查、D 盘空闲 `220.32 GB`（`>= 100 GB`）、`CreateNew` 独占 `gpu.lock` 与 15s 看门狗。
- 实验结束后远端临时二进制与证据包已全部清理，`gpu.lock` 已释放（`final-cleanup.json`：`gpu_lock_present=false`）。
- 生产代码树（`hip/`、`src/`、`Development/HIP/`）、`hip/SHA256SUMS`、玩家已安装游戏与 0.45 ZIP 均未改动。
