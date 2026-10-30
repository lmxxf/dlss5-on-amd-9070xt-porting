# DLSS 5 (RX 9070 XT `gfx1201`) 性能缺口分析评估报告（Round 1：2026-10-31）

- **主攻智能体**：枢木朱雀（鳩 / Ikaruga）
- **审稿智能体**：枢木朱雀（闇 / Yami，Codex @ `w4:p1`）
- **评估基线演进**：`8a9c60fd`（C32 Phase 5B 收网） $\to$ **`e7b234e7`（K-Search Round 1 收网新 `1.00×` 基线）**
- **证据与原始数据归档目录**：
  - Step 2（K2/K3 初筛与隔离）：`Development/results/ksearch-k2-k3-20261030/`
  - Step 3（K2-Fix/K2-Fix2 内存池隔离、K4、K5）：`Development/results/ksearch-step3-20261030/`
  - Step 4 & Step 5（K6 `SubmitPulse` 扩围 + K2-Scope + K5 组合收网与 NMMN 对账）：`Development/results/ksearch-step4-20261031/`

---

## 第一部分：De-anchoring 三张客观核心数据表（供审稿端先行独立核验）

> 按《355 期》第四步“先核数据、后看文字”审稿协议，以下三张表列出本轮（`8a9c60fd` $\to$ `e7b234e7`）全部静态 ISA/资源变化、同批交错 ABBA 实测统计（含分轮明细与反例）及 K-Search 分支状态。所有 JSON/CSV 原始回执均在对应 `Development/results/` 目录可查。

### 表 1：修改核静态 ISA 指令净差与硬件资源表（`gfx1201`，80 模块全量核验 `private=0, spill=0`）

| 所属模块 (`.hsaco`) | 核函数符号 | 编译器 | 基线 VGPR $\to$ 新 VGPR | 硬件分配桶 (Waves/SIMD) | SGPR | LDS (B) | Spill / Priv | 基线指令 $\to$ 新指令 ($\Delta$) | `global_load` $\to$ 新 | `global_store` $\to$ 新 | 关键 ISA 变更机制 |
| :--- | :--- | :---: | :---: | :---: | ---: | ---: | :---: | :---: | :---: | :---: | :--- |
| `deep_fast-packed-fast` | `split_projection_frag_w16_nof32_bskip` | COMGR | 98 (转置) $\to$ **97** (非转置) | 120 (12) | 30 | 1,024 | 0 / 0 | 630 (`bskip`) $\to$ **513 (`-117`)** | 60 $\to$ 60 | 34 $\to$ **2 (`-32`)** | K2：保持非转置 `A*B` 连续读，删 32 条 `global_store_b32` (`ffn` 4.42MB FP32 死写) 与 32 条 `cvt_f32_fp8` |
| `deep_fast-packed-fast` | `split_projection_frag_w16_nof32` | COMGR | 98 (转置) $\to$ **97** (非转置) | 120 (12) | 30 | 1,024 | 0 / 0 | 614 (`w16`) $\to$ **501 (`-113`)** | 60 $\to$ 60 | 34 $\to$ **2 (`-32`)** | K2：同上（FP32 skip 变体） |
| `multihead-fast-padded-wave-packed` | `mh_attention_project_frag_c512_w16_u4_fb8_bout` | COMGR | 101 (`u4_bout`) $\to$ **93 (`-8`)** | **120 $\to$ 96 (12 $\to$ 16 waves/SIMD)** | 36 | 0 | 0 / 0 | 756 $\to$ **749 (`-7`)** | 60 (`36×b32`) $\to$ **60 (`32×u8+4×b32`)** | 32 $\to$ 32 | K2：非转置直读 `ffn8` 1B E4M3 残差（流量 128B $\to$ 32B/lane）+ 尾部重算 `l_tail` 跨过 96 VGPR 悬崖 |
| `multihead-fast-padded-wave-packed` | `mh_attention_project_frag_c512_w16_u4_fb8` | COMGR | 101 (`u4`) $\to$ **93 (`-8`)** | **120 $\to$ 96 (12 $\to$ 16 waves/SIMD)** | 36 | 0 | 0 / 0 | 886 $\to$ **880 (`-6`)** | 60 (`36×b32`) $\to$ **60 (`32×u8+4×b32`)** | 32 $\to$ 32 | K2：同上（编码器/解码器尾 `post=3/0` FP32 输出变体） |
| `c64-wave2-fast` | `c64_wave2_bi_bo` (主力，5x/帧) | LLVM 23 | 199 $\to$ **191 (`-8`)** | **216 $\to$ 192 (7 $\to$ 8 waves/SIMD)** | 26 | 8,192 | 0 / 0 | 2,951 $\to$ **2,918 (`-33`)** | 156 $\to$ 156 | 2 $\to$ 2 | K5：在 `ht` 循环头重取 `ffn_tid`，斩断 LLVM 23 LICM 外提的 4 个 64 位 Contract 权重基址指针（7 VGPR），吃满 8KB LDS 决定的 4 WGs/CU 上限 |
| `c64-wave2-fast` | `c64_wave2_bi_ho` (编码尾，1x/帧) | LLVM 23 | 199 $\to$ **191 (`-8`)** | **216 $\to$ 192 (7 $\to$ 8 waves/SIMD)** | 25 | 8,192 | 0 / 0 | 2,910 $\to$ **2,878 (`-32`)** | 156 $\to$ 156 | 2 $\to$ 2 | K5：同上 |
| `c64-wave2-fast` | `c64_wave2_up_lb_sh` (解码 Up62) | LLVM 23 | 197 $\to$ **189 (`-8`)** | **216 $\to$ 192 (7 $\to$ 8 waves/SIMD)** | 27 | 8,192 | 0 / 0 | 4,526 $\to$ **3,471 (`-1055`, `-23.3%`)** | 182 $\to$ **176 (`-6`)** | 2 $\to$ 2 | K5：首 barrier 前 `qt=0..3` Up 采样循环 `unroll(disable)` 消除 4 倍展开膨胀 + 斩断 LICM 7 VGPR |
| `c64-wave2-fast` | `c128_wave2_up_lb_sh` (解码 Up56) | LLVM 23 | 238 $\to$ **240** | 240 (6 waves/SIMD，受 16KB LDS 限 4 WGs/CU) | 27 | 16,384 | 0 / 0 | 4,586 $\to$ **3,609 (`-977`, `-21.3%`)** | 284 $\to$ **278 (`-6`)** | 2 $\to$ 2 | K5：首 barrier 前 `qt=0..3` Up 采样循环 `unroll(disable)` 净砍 977 条指令 |

> **未采纳候选的静态 ISA 对照（详见表 3 剪枝记录）**：
> - **K3（`c32_wave1_post_b8_w16` 外提 `scales[64]`）**：VGPR `148 -> 149`，指令 `1455 -> 1454`，拆散了循环内 `s_clause 0x3` 并跨 4 轮 `qt` 占用 16 个 VGPR。
> - **K4（`sp_run256_w16` 开启 `W2_QKV_FUSE=28, W2_HOIST_LOADS=3`）**：若保持内层 `kt` 展开（`W2_QKV_FUSE=12`）则触顶 `256 VGPR` 并产生 `11 spill / 48B private`；禁用 `kt` 展开（`W2_QKV_FUSE=28`）虽保住 `253 VGPR, 0 spill` 且指令 `5246 -> 3923`，但每轮 `kt` 仅剩 1 条 `ds_load` + 2 条 `global_load`，丢失跨 `kt` 的 `s_clause` 预取深度。

---

### 表 2：正确性 Gold 门禁、硬件实时钟探针、七档 Frame ABBA 与三档 `v0.0.8 ACO` 纯 NN 对账

#### 2.1 七档 42 帧 Gold 校验记录（`Development/results/ksearch-step4-20261031/final-gold-reference.json`）
- **结果**：7 个档位 × 两臂 × 3 帧 = **42 帧全部 `0 bit-diff`（`invalid_total = 0`）**：
  - `original1152`：`0900e489a397302b93074a878c694b9c3e7c3400cfe2dfdd3cc727abe1050f4a`（`submit_pulse accepted=4, reject_mask=0, vit_inter_byte_calls=108`）
  - `original1088`：`8a45a97480bfa91a8f07b76980275e46262173cb924c65d9d310289ea98546ab`（`submit_pulse accepted=4, reject_mask=0, vit_inter_byte_calls=172`）
  - `original960`：`5fe1ace60adb68d6e8f0dff8fdf72379c934587e3b4876a33c81e945103ad475`（`submit_pulse accepted=4, reject_mask=0, vit_inter_byte_calls=172`）
  - `small1080`：`65d71486636715598ee1994166d5186a52d5f53230dbd7f8f03e514e3ec8fcb8`（`submit_pulse accepted=0, vit_inter_byte_calls=87`）
  - `small900`：`d34685052d820fc24d77b7f35011e6b7aa3dc839fea08febb91d2f66dffea7c2`（`submit_pulse accepted=4, reject_mask=0, vit_inter_byte_calls=60`）
  - `small720`：`cb14850b0d3118d366107e70f7e630588cce8f3fc5703f32f96b006b11ca66f8`（`submit_pulse accepted=4, reject_mask=0, vit_inter_byte_calls=44`）
  - `smallwindow`：`2d89ecb26dbbaf7cec1abae3fdbbeb1da753a32e077170ad0e329c326363b9de`（`submit_pulse accepted=4, reject_mask=0, vit_inter_byte_calls=44`）

#### 2.2 100MHz 硬件实时钟探针（`MSG_RTN_GET_REALTIME`，3 轮 ABBA）

| 被测分支 | 档位 | 目标阶段 / 核函数 | 基线 (µs) | 候选 (µs) | $\Delta$ (µs) | 赢轮数 | 全网纯 NN 实时钟 $\Delta$ (µs) | 结论 |
| :--- | :--- | :--- | ---: | ---: | ---: | :---: | ---: | :---: |
| **K2** (`w16_fb8` 非转置) | `original1088` | `c512_ffn_proj` + `c512_attn_proj` (32 calls) | 321.110 | 301.530 | **-19.580** | **3/3** | **-34.433 (3/3)** | **核加速成立** |
| **K2** (`w16_fb8` 非转置) | `small1080` | `c512_ffn_proj` + `c512_attn_proj` (8 calls) | 90.970 | 84.770 | **-6.200** | **3/3** | **-2.980 (2/3)** | **核加速成立** |
| **K5** (`c64/c128_wave2`) | `original1088` | `c64_wave2` + `c64_up` + `c128_up` (8 calls) | 1024.353 | 1018.397 | **-5.957** | **3/3** | -1.610 (2/3) | **核加速成立** |
| **K5** (`c64/c128_wave2`) | `small1080` | `c64_up` + `c128_up` (2 calls) | 246.663 | 245.103 | **-1.560** | **3/3** | **-4.233 (2/3)** | **核加速成立** |
| **K3** (`c32_post` `scales` 外提) | `original1088` | `c32_wave1_post_b8_w16` (1 call) | 710.257 | 711.217 | +0.960 | 2/3 | +2.547 (1/3) | 淘汰 |
| **K4** (`sp_run256` `QKV_FUSE=28`)| `original1088` | `sp_run256_w16` (2 calls) | 829.880 | 1086.293 | +256.413 | 0/3 | +214.120 (0/3) | 淘汰 |
| **K4c** (`sp_run256` `HOIST=3`) | `original1088` | `sp_run256_w16` (2 calls) | 826.143 | 826.110 | -0.033 | 1/3 | +7.563 (0/3) | 淘汰 |

#### 2.3 七档 3 轮 4-slot Frame ABBA 稳态评测（每 slot 240 帧弃 80 取 160，每臂 960 稳态帧，`edges_only=1, NET0`）

> 同時列出 Step 4 候选批（`k6c3-statistics.json`）与 Step 5 正式生产重构收口批（`final-statistics.json`，恢复 `if(!fb8&&!w16_fb8)ffn8.reset();` 保持 `ffn8` 与 `result` 异址以修复 `pure.exe` 连续 241 帧压测下的跨工作组读写别名竞争），不隐瞒任何批次或分轮波动：

| 档位 | 模式与处理尺寸 | Step 5 基线 A (ms) | Step 5 生产 B `e7b234e7` (ms) | Step 5 mean $\Delta$ (µs) | Step 5 mean $\Delta$ (%) | Step 5 p99 $\Delta$ (µs) | Step 5 赢轮 (mean / p99) | Step 5 分轮 mean $\Delta$ (µs) (R1 / R2 / R3) | Step 4 `k6c3` 批 mean / p99 $\Delta$ (µs) |
| :--- | :--- | ---: | ---: | ---: | ---: | ---: | :---: | :--- | ---: |
| **`smallwindow`** | `896×512` Small | 1.880970 | **1.714310** | **-166.660** | **-8.86%** | **-211.988** | **3/3 / 3/3** | `-172.18 / -165.41 / -162.39` | `-165.49 / -195.61` (3/3) |
| **`original1088`**| `1920×1088` Orig | 8.672521 | **8.513244** | **-159.277** | **-1.84%** | **-180.595** | **3/3 / 3/3** | `-196.46 / -141.93 / -139.44` | `-119.50 / -163.07` (3/3) |
| **`small720`** | `1344×768` Small | 2.948854 | **2.812073** | **-136.781** | **-4.64%** | **-188.707** | **3/3 / 3/3** | `-139.03 / -131.62 / -139.69` | `-140.45 / -205.72` (3/3) |
| **`small900`** | `1600×1024` Small| 4.119069 | **4.031783** | **-87.286** | **-2.12%** | **-169.082** | **3/3 / 3/3** | `-92.60 / -77.12 / -92.14` | `-88.18 / -147.00` (3/3) |
| **`original960`** | `1600×960` Orig | 6.373642 | **6.349197** | **-24.445** | **-0.38%** | **-14.666** | **3/3 / 2/3** | `-19.37 / -31.78 / -22.19` | `-24.89 / -4.84` (3/3) |
| **`original1152`**| `1920×1152` Orig | 8.958572 | **8.953939** | **-4.633** | **-0.05%** | **-16.966** | **2/3 / 1/3** | `+0.94 / -9.09 / -5.75` | `-2.30 / -28.67` (2/3) |
| **`small1080`** | `1920×1152` Small| 5.367903 | **5.374693** | **+6.790** | **+0.13%** | **+29.501** | 0/3 / 0/3 | `+4.77 / +10.22 / +5.38` | **`-9.78 / -48.10` (3/3)** |

> **关于 `small1080` 的透明披露**：在 `small1080`（`1920×1152` Small）上，`SubmitPulse=0` 且 `C512W16Fb8ShapeEligible()=false`，因此 Step 4 `k6c3` 与 Step 5 `final` 在 `small1080` 上执行的 GPU 核与宿主路径 **100% 逐字节相同**（唯一变化仅为 K5 的 `c64-wave2-fast.hsaco`）。该路径在 Step 3 K5 独立 ABBA 中为 **`-4.514 µs`（3/3 轮全胜）**、在 Step 4 `k6c3` ABBA 中为 **`-9.783 µs`（p99 `-48.095 µs`，3/3 轮全胜）**、在硬件实时钟中为 **`-4.233 µs`**、在下方 NMMN 纯 NN 对账中比 Phase 5B 快 **`-20.415 µs`**，而在 Step 5 `final` 批次中出现 `+6.790 µs`（`+0.13%`）的跨批热态漂移，原样保留不刷轮。

#### 2.4 最新生产 `e7b234e7` vs 新版 `v0.0.8 ACO`：三档 3 轮 NMMN 纯 NN 对账（`final-nmmn-statistics.json`）

> 每 slot 80 warm + 160 measured = 240 帧（另首尾校验帧），三轮 NMMN 共 960 稳态帧/臂；全部 18 个 HIP slot `raw_bitdiff=0, output_finite=1`：

| 模型档位 | Phase 4 HIP NN (ms) | Phase 5B HIP NN (ms) | **本轮 `e7b234e7` HIP NN (ms)** | **本轮 `v0.0.8 ACO` NN (ms)** | **最新纯 NN 差值 (ms)** | **HIP 较 ACO 慢 %** | 本轮分轮 HIP / ACO (ms) (R1 / R2 / R3) |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | :--- |
| **`original1088`** | 8.301703 | 8.214575 | **8.096690** | **5.646863** | **+2.449827** | **+43.38%** | `8.0600/5.6306`, `8.1014/5.6472`, `8.1287/5.6628` |
| **`small1080`** | 5.005096 | 4.946637 | **4.926222** | **3.443128** | **+1.483094** | **+43.07%** | `4.9236/3.4414`, `4.9292/3.4431`, `4.9259/3.4448` |
| **`small900`** | 3.841217 | 3.795411 | **3.789783** | **2.635953** | **+1.153830** | **+43.77%** | `3.7944/2.6354`, `3.7833/2.6369`, `3.7916/2.6355` |

---

### 表 3：K-Search 候选分支调度表（Round 1 终态）与逐 Stage 剩余差距账本

#### 3.1 K-Search 候选分支调度表

| 分支 ID | 状态 | 优化分支假设与精确靶点 | 硬件探针 / ABBA 实测证据 | 连续失败 `n` (`K=3`) |
| :--- | :---: | :--- | :--- | :---: |
| **K6** | **`Promoted ✅`** | 修复 `PulseRejectMask` 漏放行 `Original 1088` 与 `Small` (`small900/720/window`)，在 `ordered Down(22)` 后触发 `hipEventRecord` 提前发车 | 单刀 `k6b`：`orig1088` `-112.05 µs` (3/3)、`small900` `-72.36 µs` (3/3)；排除无 `sp_run256` 缓冲的 `small1080` (`+9.48 µs`) | `0` |
| **K2-Scope**| **`Promoted ✅`** | C512 块内非转置 `split_projection_frag_w16_nof32_bskip` (`97 VGPR`) + `mh_attention_project_frag_c512_w16_u4_fb8_bout` (`93 VGPR`, 16 waves/SIMD)，消除 `4.42 MB/块` FP32 死写，限定于 `orig1088/orig960/small900` + `InitC512Cache()` | 硬件实时钟 C512 双核 `-19.58 µs` (3/3)、纯 NN `-34.43 µs` (3/3)；在 `SubmitPulse=1` 下 `orig960` `-27.99 µs` (3/3)、`orig1088` 追加 `-25.4 µs`；异址 `ffn8` 保证 241 帧连续纯 NN `0 bit-diff` | `0` |
| **K5** | **`Promoted ✅`** | `c64/c128_wave2_up_lb_sh` 折叠首 barrier 前 `qt` 循环（砍 `-1055/-977` 指令）+ `c64_wave2` 斩断 LICM 7 个指针 VGPR 压入 `189~191 VGPR` (8 waves/SIMD) | 硬件实时钟 C64+C128 Up `-5.96 µs` (3/3)；独立四档 Frame ABBA 4/4 mean 全负（`-1.44 ~ -4.51 µs`） | `0` |
| **K3** | `Pruned ❌` | `c32_wave1_post_b8_w16` 跨 `qt` 外提 `scales[64]` | 256B `scales` 在 `qt=1..3` 已 100% 命中 L0 cache，外提占 16 VGPR 拆散 `s_clause 0x3`；硬件时钟 `+0.96 µs`，Frame ABBA `+3.58 / +15.55 µs` | `1 (Pruned)` |
| **K4** | `Pruned ❌` | `sp_run256_w16` 开启 `W2_QKV_FUSE=12/28` 与 `W2_HOIST_LOADS=3` | 展开 `kt` 触发 11 spill；禁 `kt` 展开 (`253 VGPR, 0 spill`) 丢失 `s_clause` 预取深度，硬件时钟 `+256.41 µs`；单开 `HOIST=3` 为 `-0.03 µs` | `2 (Pruned)` |
| **K1** | `Closed 🔒` | `VitQKFragmentShapeEligible` 在其余档位（`orig1088/1152`, `small720`）扩围 | 已于 Phase 6 刀 1（`gap-phase6-20261023/knife1`）在 153-dispatch 基线上完成 7 档 84-slot ABBA，仅 `orig900/small900/small1080` 三档正收益已开，其余四档尾退或均值退 | — |

#### 3.2 `e7b234e7` vs `v0.0.8 ACO` 逐家族剩余硬件时钟差距账本（Original 1088 与 Small 1080）

| 家族阶段 | Orig 1088 HIP (µs) | Orig 1088 ACO (µs) | Orig 1088 $\Delta$ (µs) | Small 1080 HIP (µs) | Small 1080 ACO (µs) | Small 1080 $\Delta$ (µs) | 剩余差距的物理构成与约束边界 |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | :--- |
| **1. C32 家族** (`prefix`+`chain`+`finish_pool`+`up`+`post`) | 2,732.9 | 2,005.9 | **+727.0** | 2,184.7 | 1,612.3 | **+572.4** | ① `prefix`（`+180/+191 µs`）：我方每帧内联计算 PCG+Box-Muller 噪声，ACO 预生成噪声纹理；② `nh` 变体：ACO 预烘 attention exp bias 并使用 packed-half 位映射 softmax（有损/不同舍入合同）；③ `post`（`+171/+185 µs`）：Phase 5 已证伪寄存器 RGB 尾部（`8b8dbb7b` 慢 `+43 µs`），本轮 K3 证伪 `scales` 外提 |
| **2. C256 家族** (`sp_run256`+首尾 `wave2`+`up/down`) | 1,184.2 | 788.5 | **+395.7** | 367.3 | 236.3 | **+131.0** | ACO 将 8 层（Small 2 层）+ DS/UPS 合成单次 persistent dispatch（`fswinpds256`/`fswinpup256`）；我方因 `Block 22` `post=3` FP32 合同与 `W2_FAST_NUM=3 vs 0` 差异，首尾层 `15,22,48,55` 独立派发（已封板禁区），且本轮 K4 证伪 `sp_run256_w16` 内部 `W2_QKV_FUSE`/`HOIST` |
| **3. C64 + C128 家族** (`wave2`+`up`+`down`) | 1,958.1 | 1,323.5 | **+634.6** | 908.6 | 598.8 | **+309.8** | ACO 使用 DS-last / UPS-first 完整 stage 持久核（每 stage 1 次 dispatch，共 4 次）；我方为逐层独立 dispatch（Orig 22 次，Small 10 次）。本轮 K5 已将 `c64_wave2` 压入 191 VGPR (8 waves/SIMD) 并折叠 `up_lb_sh` 砍掉 ~1,000 条指令（追回 `-6.0 µs`）；`c128_wave2`（242 VGPR）因 `launder_coord` 仅降至 241 VGPR 未跨过 240 悬崖，且 Phase 1/Phase 4 已证伪 `B=2`（慢 `+20~39 µs`）与深预取 3A/3B |
| **4. C512 家族** (`QKV+Attn`+`AttnProj`+`SplitFFN`+`FFNProj`) | 1,020.7 | 718.1 | **+302.6** | 299.2 | 192.7 | **+106.4** | 本轮 K2 已消除 `orig1088/orig960/small900` 块内 `4.42 MB` FP32 `ffn` 死写并将 `mh_attention_project` 压入 93 VGPR (16 waves/SIMD)，追回 `-19.6 µs`（纯 NN `-34.4 µs`）。剩余差距在 `c512_qkv_attention_compact_w16`（`+75 µs`，Phase 2 刀 1 已证伪 V 转置消除 `ds_load_u8`、Phase 3 刀 3 证伪 `serial_norm` 缓存、Phase 5 刀 1 证伪 `permlanex16`）与 `split_ffn_one`（`+68 µs`，Phase 2 刀 3 证伪 LDS 消除） |
| **5. ViT 家族** (`Expand`+`Contract`+`QKV`+`Attn`+`Proj`+`Gather`) | 999.9 | 774.9 | **+225.0** | 995.5 | 767.9 | **+227.6** | 我方 ViT Attention 已反超 ACO（`-2.4 ~ -11.5 µs`）；剩余差距在 `QKV`（`+71 µs`，W5 经 LDS 共享权重含 6 次 barrier，Phase 1 backfeed 刀 2 已证伪 `qkv_vfrag` 尾宽写）、`Expand/Contract`（`+57/+49 µs`，Phase 2 刀 2 已证伪 `b64` 宽写因 VGPR `112->113/115` 超门禁或四档不稳）及 2 次独立 `vit_gather_b8`（`16.5 µs`，10-17 候选 2 已证伪双写入端折叠 `+5.69 µs`） |
| **6. 跨核调度与队列空泡差** | — | — | **~+165.0** | — | — | **~+135.9** | HIP 152/82 dispatches vs ACO 123/71 dispatches 的跨核 barrier 排空与 Vulkan command buffer 批处理差异（hipGraph 全家桶已于 10-14 `ReplayAuto` 封板；C512/ViT `tile-ready` any-order 链化已于 10-12 与 Phase 3B 实测净负封板） |

---

## 第二部分：第一轮收敛评估与提请闇（Codex）审稿的核心问题

综合上述四步实验（Step 2 $\to$ Step 5）与 380+ 项历史负账（`Development/failed-experiments.md`）：

1. **本轮已兑现的实质突破（`8a9c60fd` $\to$ `e7b234e7`）**：
   - 通过 **K6（修复 `PulseRejectMask` 漏放行 `Original 1088` 与 `Small 900/720/window`）+ K2-Scope（非转置 `w16_fb8` 消除 C512 块内 `4.42 MB` FP32 死写并将 `mh_attention_project` 压入 93 VGPR 满驻留档）+ K5（折叠 `c64/c128_wave2_up_lb_sh` 首 barrier 前 `qt` 循环砍 `-1,055/-977` 指令并将 `c64_wave2` 全系压入 `189~191 VGPR` = 8 waves/SIMD）**，在严格满足 **七档 42 帧 Gold `0 bit-diff`（`invalid=0`）** 与 **80 模块 0 spill** 的前提下：
   - 将端到端 `frame.exe` 稳态耗时在 `smallwindow` 上压降 **`-166.66 µs (-8.86%)`**、`original1088` 压降 **`-159.28 µs (-1.84%)`**、`small720` 压降 **`-136.78 µs (-4.64%)`**、`small900` 压降 **`-87.29 µs (-2.12%)`**、`original960` 压降 **`-24.45 µs (-0.38%)`**；
   - 将同批 `original1088` 纯 NN 耗时从 `8.2146 ms` 压进 **`8.0967 ms`（净降 `-117.89 µs`，距离 8.0 ms 关口仅 96.7 µs）**，`small1080` 纯 NN 压进 **`4.9262 ms`**，`small900` 纯 NN 压进 **`3.7898 ms`**。

2. **当前在严格 `0 bit-diff` + 现有架构边界下继续深挖面临的硬约束**：
   - **C32（差 `+727 / +572 µs`）**：Phase 5B 已收网 `CW_NORM_HOIST=1`、`CW_OUTPROJ_HOIST=1` 与 `finish_dcrop_pool` 融合（`-92.9 µs`）；Phase 2 刀 2 证伪 `FMA_MIX`、Phase 5 刀 4 证伪 Post 寄存器 RGB 尾部、本轮 K3 证伪 Post `scales` 外提；剩余差额主要来自我方每帧内联实时计算 Box-Muller 噪声（ACO 预生成噪声表）及 ACO 的 `nh` 预烘 bias / 位映射 softmax（改变数学合同）。
   - **C256 / SP（差 `+396 / +131 µs`）**：首尾层 `15, 22, 48, 55` 因 `Block 22` `post=3` FP32 合同及 `W2_FAST_NUM` 差异无法并入 `SpStage`（已封板）；本轮 K4 证实 `sp_run256_w16` 开启 `W2_QKV_FUSE` 无论展开（spill）还是循环（丢 `s_clause` 预取，`+256 µs`）均负，单开 `W2_HOIST_LOADS=3` 持平（`-0.03 µs`）。
   - **C64 / C128（差 `+635 / +310 µs`）**：本轮 K5 已兑现 `c64_wave2` 199 $\to$ 191 VGPR（8 waves/SIMD）与 `up_lb_sh` 循环折叠；`c128_wave2_bi_bo`（242 VGPR）因 `B=4, T=2` 累加器保活下限卡在 241~242 VGPR，而改 `B=2`（Phase 1）或深预取（Phase 4 3A/3B）均已实测为负。
   - **C512 & ViT（差 `+303 / +225 µs`）**：本轮 K2 已收网块内 `w16_fb8` 1B 残差流；其余 ViT W16、字节流、`_vfrag`/`_qkfrag`、C512 W16、成对 FP8 打包均已合入，而 V 转置、N-pair、QT32 共享 KV、tile-counter 链化、宽写等十余个变体均已在 Phase 1~3 中实测证伪。

3. **提请闇（Codex）逐行审查与挑刺**：
   - 请独立核查上述三张数据表、`e7b234e7` 提交代码及 `Development/results/ksearch-step4-20261031/` 中的证据文件；
   - 检查本报告的数据归因、实验设计（如 `small1080` 上 K5 的批次波动、或 `1920×1152` 上未启用 `w16_fb8` 的原因、或 `c128_wave2` 242 $\to$ 240 VGPR 仅差 1~2 个寄存器的壓降手段、或任何尚未被 `failed-experiments.md` 真正封死的算子/宿主路径）是否存在逻辑漏洞或未测试的可行突破口。若发现任何漏洞或新候选，请直接指出，主攻端立即立项进入 Round 2 实机攻关！
