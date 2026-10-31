# 当前工作计划（WorkingPlan）

> 最新Phase2（2026-10-29）：三刀已全部验证并STOP，生产/80模块保持d78b688e对应状态；详文末及results/isa-asm-phase2-20261028/README.md。

> 最新汇编Phase1：三项独立手术均STOP，实际活跃映射/汇编修改-GPU闭环/分析报告已完成，见文末。生产仍b5875bfa。

> 最新：2026-10-28 backfeed四刀已结题。刀1限定shape采纳，刀2/3 STOP，刀4配置与最终构建完成；游戏/公开发行仍0.45。详文末与results/backfeed-20261028/knife4/receipt.json。

更新：2026-10-24（v0.45 毕业收官版：第一至第六阶段无损优化落地，剑星/鬼武者已装机校验通过，三包及公开镜像 [夸克](https://pan.quark.cn/s/0ccb3eff675b) / [Gofile](https://gofile.io/d/hmrcTjuS) 全部闭环）。

---

## 1. 当前基线与本机状态

### 1.1 当前发行与安装（0.45，2026-10-24，公开三包已交付）

- 正式三包在 `D:\給網友打包`，附SHA256；每包44shader/80模块/93Small资产/ZIP全文件读回，RE9 smoke通过。[发行回执](releases/0.45/README.md)。
- **0.45 公开镜像**：[夸克](https://pan.quark.cn/s/0ccb3eff675b) · [Gofile](https://gofile.io/d/hmrcTjuS)（0.44 旧镜像 [夸克](https://pan.quark.cn/s/ea87c0ffd257) / [Gofile](https://gofile.io/d/YCNT57JP) 与 0.43 旧镜像 [夸克](https://pan.quark.cn/s/3b19c0403e3f) / [Gofile](https://gofile.io/d/hz5cDOPF) 保留）。
- 双游戏已装 0.45 最终载荷（16份受保护配置记录逐字节保持，鬼武者 root 与 `_storage_` 两路 smoke exit0，备份与回滚脚本在 `D:\DLSSNR-Lab\game-install-20261024\rollback.ps1`）。[安装回执](results/game-install-20261024/README.md)。
- 当前 Small 0.5x 单帧：1080 **5.487ms** / free1088 **5.317ms** / 900 **4.245ms** / 720 **2.990ms**；Original 1x 单帧：1152 **9.092ms** / 1088 **8.794ms**（纯NN **8.306ms**）/ 900 **6.408ms**。原71块仍可F9选择1x/2x/3x。

### 1.1a 历史发行包基线（0.43，2026-10-17，公开三包已交付）
- 载荷源码 `10982cf4`；复用已验宿主/双架构80模块，不重跑模块编译或ABBA。
- `D:\給網友打包`：Magpie `345,297,302 B`（SHA `b3450315…`），通用OptiScaler `375,495,322 B`（SHA `e507a6b4…`），OptiScaler-REFramework `430,517,634 B`（SHA `0ac552c4…`），每个ZIP附 `.zip.sha256`。
- 每包44项shader、RE9包内smoke、ZIP逐文件回读及独立默认/80模块/宿主检查通过；回执见 [releases/0.43](releases/0.43/README.md)。
- **0.43 公开镜像**：[夸克](https://pan.quark.cn/s/3b19c0403e3f) · [Gofile](https://gofile.io/d/hz5cDOPF)（0.42 旧镜像 [夸克](https://pan.quark.cn/s/316704620069) / [Gofile](https://gofile.io/d/HNE20Ocp) 及旧ZIP保留）。
- **版本单帧耗时（`frame.exe` 稳态均值，RX 9070 XT）**：**1080 档（1920×1152）`9.121 ms`**（最佳单槽 `8.960 ms` mean / `8.945 ms` median；0.42 为 `~9.65 ms`），**900 档（1600×960）`6.535 ms`**。**Zero 2026-10-18 定规**：因游戏内 1x 帧率已顶满 60 FPS 上限看不出差距，以后直接记录每个版本的单帧耗时（`ms/frame`）。
- 继承0.42资源优化：已验范围内216MiB持有权重容量与显式无history会话33.75MiB共享buffer容量节省，非驻留峰值。

### 1.2 开发主线（10-14 七场战役 + 10-17 收下候选1/3）
在 0.42 之上完成 2026-10-14 全网 W16 权重分块、ISA 去分支与跨核 1B E4M3 字节流直连（1920×1152 Gold 首尾帧 SHA256 全程逐位锁定 `0900E489A397302B93074A878C694B9C3E7C3400CFE2DFDD3CC727ABE1050F4A`，`0 bit-diff`，`extra_vram_bytes=0`）：
1. **ReplayAuto 162 节点强 DAG 结题（STOP）**：修妥 ROCm 7.0.2 CLR `hipEventRecord` 流捕获缺陷并过 12 帧 Gold，但 3 轮 ABBA pooled `Δmean = -17.25 µs`、`Δp99 = +4.00 µs`（R3 双退步），hipGraph 全家桶在 RX 9070 XT 上彻底封板，生产保持 `EagerAuto`（`DLSS5_SUBMIT_PULSE=2, opt.graph=0`）。
2. **C512 三核 W16（`HIP_C512_W16=2`）**：`split_projection_frag_w16`、`c512_qkv_attention_compact_w16`、`mh_attention_project_frag_c512_w16_u4` 改 16B `global_load_b128`，3 轮 ABBA `-16.7 µs` mean。
3. **C512 Split-FFN W16（`HIP_C512_FFN_W16=1`）**：`split_ffn_one_w2f8_w16` 消除 256 条跨步 `global_load_b64` 为 128 条 100% 合并 `global_load_b128`，3 轮 ABBA **`-131.2 µs` mean / `-134.5 µs` median**。
4. **C32 Wave1 W16（`HIP_C32_W16=1`）**：`c32_wave1_*_w16` 将 88 条 `global_load_b64` 合为 30 条合并 `global_load_b128`，3 轮 ABBA **`-24.0 µs` mean / `-32.8 µs` median**。
5. **ViT 核心三算子 W16 + 外层分片禁展开（`HIP_VIT_W16=7`）**：Contract VGPR `211 → 115`（Occupancy `6 → 8`）、Projection VGPR `190 → 114`（Occupancy `6 → 10`），3 轮 ABBA **`-56.6 µs` mean / `-66.5 µs` median**。
6. **全网 ISA Epilogue 清理 + 二轮病灶审计**：消除 `mh_pool_project_group` 与 `mh_attention_project_frag_c512` 的 `v_movrels_b32` 及百余条 `s_cbranch`，统一无分支 `fmed3f(x+0.f, -448.f, 448.f)`，向量化 `c32_wave1_finish/post` LDS/全局写，两轮合计 **`-151.8 µs` mean / `-153.0 µs` median**。
7. **ViT + C512 + Downsample 1B E4M3 字节流直连（`HIP_VIT_INTER_BYTE=15`）**：消除全网全部 8 次 `vit_pack_input` 发射（每帧总发射数 **`161 → 153`**），每帧砍掉 **`681.45 MB`** 冗余 `float32 <-> uint8` 流量，池显存 `277.1 → 276.0 MiB`（`-1.1 MiB`），两轮合计 **`-217.6 µs` mean / `-213.6 µs` median**。
   - **1080p（`1920×1152`）整网 `frame.exe` 稳态耗时**：由 `9.6070 ms` 降至 **`9.0549 ms` pooled mean / `9.0485 ms` pooled median**（最佳单槽 **`8.9601 ms` mean / `8.9445 ms` median**，累计净降 **`-0.5812 ms` mean / `-0.6004 ms` median**）。
8. **`NativeLowFrequencyTemporal`（MODE 2/3）64-Slot Ring 化**：消除 MODE 2/3 每帧 `CreateDescriptorHeap` 与 2 次 `r.submit.Flush()`，异步提交下 `-0.070 ms/frame`。

**2026-10-17 本轮结题**：五项按序完成，收1/3，停2/4/5。干净默认构建的1080/900 Gold逐帧一致；每档3轮ABBA的均值全部改善，pooled mean分别−20.846/−34.487µs（**1080 档 `9.1422 → 9.1213 ms`，900 档 `6.5699 → 6.5354 ms`**），pooled p99−25.690/−32.351µs。1080 R2 p99+10.599µs原样留档，不称每轮尾都好。最终80模块逐SHA/实际ELF目标核验通过（旧RTC错误gfx1200标签已用当前源码工具修正，实测gfx1201字节未变）；gfx1200仅编译核验。addon/runtime构建与runtime API smoke通过，产物在 `D:\DLSSNR-Lab\five-final-20261017` 和本地 `/home/lmxxf/work/dlss5-five-build/final-payload/`。

**当前状态**：0.43 正式三包与网盘镜像已全部交付完毕。详 `releases/0.43/README.md`。

### 1.3 本机游戏现装（2026-10-17，鳩部署10982cf4）
- **《剑星》**：addon SHA `2cefc60d50906d9a5dcf542f1c890819fcaa01b2d712047423d303b8f0475e5b`，80模块；配置MODE0/MP1/PRED1/SKIN0，个人配置文件字节未改。用户实玩确认帧率已贴满上限看不出差异，以后直接记录各版本单帧耗时（`0.43`：1080 档 `9.121 ms` / 900 档 `6.535 ms`）。
- **《鬼武者》**：runtime根目录与_storage_均为 SHA `bc7b77049f828e1500bfe447d9f09c963b38b2c5584c104f65650e5127f8decd`，80模块；MODE1/MP1/PRED0/SKIN0，个人配置字节未改，API smoke exit0。
- **备份/回滚**：`D:\DLSSNR-Lab\game-install-20261017\backup-20261017-233600`；详 [安装回执](results/game-install-20261017/README.md)。

---

## 2. 新主线：0.5x Small（全档几何 / AE / MODE1已闭环，0.44交付）

Zero 2026-10-18 定名：310.9.16 的31块新网络叫 **0.5x**，`DLSS5_MULTI_PASS=0.5` 为新的源码/三模板默认；`1` / `2` / `3` 分别为旧71块单遍/叠层，第三遍预测选项保留。add-on F9按 **0.5 → 1 → 2 → 3 → 0.5** 切换（addon和runtime均支持；2x/3x暂挂MODE1，回到0.5x/1x自动恢复）。缺少Small资产时带requested/effective日志回退1x。

### 前阶段记录（1080阶段；后续进展见§6）

- [逆向报告](results/dlssnr-310-9-16-analysis-20261018/README.md)之后，实际提取转换与GPU回执见 **[small-model-20261018](results/small-model-20261018/README.md)**；该回执取代旧报告P1“保Original默认”的产品建议。
- 310.9.16 DLL SHA `2bc19bc0…`；5090通过原NGX分别跑Preset1/2及默认0。新Preset1=旧310.8，默认0=Preset2；合成及真实8678/8680重复稳定。只读内部Post捕获前后API输出SHA不变，输入捕获与上传值一致。
- 同一转换器重解HT的184个主图资产与现有HIP资产逐float bit一致。Small已导出93文件（92主图+64B history头），241214392B，独立`small/`目录及manifest，保留旧权重共存。blend `0x3A23` / 0.76708984375 与新history行6也已提取；**该阶段时序未接通，第三阶段已接通**。
- 31块执行图采用独立模型身份、legacy kernel-role资产映射与0/3→1/2 shift。1080固定1920×1152/640 token；不引入失败的C32 FP8 Up，不重编既有80个双架构算子模块。Small禁用旧图专用SP/Pulse/PDL提交；该阶段AE关闭，第三阶段已接通；切换drain并清理权重、SP计划引用和自适应状态。
- 原网络Gold仍为`0900E489…0F4A`，Small新Gold为`65D71486…8FCB8`；Gold和3轮ABBA全部首尾有限/同模型逐位一致。模式0.5→1→2→3→0.5真GPU往返Small bit-diff=0；Windows CPU热键写回/优先级/reload通过；缺Small实际回退与显式1x逐位一致。
- NVIDIA对账域是**内部Post FP16 surface展开的RGB**，不是API额外合成后的最终输出。真实8678：Small FAST0 **48.021703dB**、默认FAST1 **47.755055dB**；近似HIP算术不宣称NV 0 bit-diff，完整逐值指标在回执。
- **完整frame.exe 1080稳态单帧耗时：0.5x `5.553510 ms`，同批1x `9.156154 ms`，减耗时 `39.347%`，速度比1.649×**。3轮4-slot ABBA每槽240帧/丢前80，NET0/edges_only1；每轮正收益。Small median5.535300ms/p99 5.820500ms。历史0.43 1x9.121ms继续保留，比较结论优先用同批数据。
- add-on/runtime/frame宿主编译通过。正式0.43包与已装双游戏未动；这不是新的全分辨率发行包，也未做游戏实玩验收。

### 第三阶段任务状态

1. **已完成900/720/free**：逐节点ceil-half/round4、尾部填充与stride；模型/尺寸切换重建缓冲，runtime四尺寸回环通过。
2. **已完成Oracle/AE/MODE1/runtime F9**：900/720对账47.40/47.70dB，Small8个ViT gain，独立history gate和0x3A23；addon/runtime全模式回环通过。
3. **发行与部署**：0.44正式三包与双游戏备份部署按本轮明确授权执行；最终精确回执见§6和releases/0.44。

实验资产：Linux `/home/lmxxf/work/dlss5-small-build/small/`，9070 `D:\DLSSNR-Lab\small-model-20261018\assets\small\`；5090 Oracle在 `C:\DLSSNR-Small-20261018`。权重/原DLL/大原始输出留实验目录，不入Git。

---

## 3. 2026-10-17 候选与实测结论（已全部结题：收1/3，停2/4/5）

以下保留基于 `be506729` 的原始五项审计提案；是否成立以各节实测状态为准，不能把提案中的逐位/收益假设当成验收结果。汇总见 `results/five-final-20261017/`。

### 候选 1：Block 47 → `Up(48)`（`512 → 256`）1B E4M3 字节边接通 + `Up(39)`/`Up(48)` W16 FP8 WMMA（`HIP_DEC_F8_W16`）

**2026-10-17 已验收合入**：Gold 0 bit-diff；3轮ABBA均值全改善、p99全改善，pooled mean −17.398µs / p99 −23.962µs。默认 `DecoderF8W16Mode=3`；详 `results/decoder-f8-w16-20261017/`。下方原审计说明保留，下一项候选2。

1. **Block 47 → `Up(48)` 1B E4M3 字节边（零核改动或仅宿主接通）**：
   - **现状病灶**：`hip_reference_network.h:1025` 在 C512 解码段 `for (U b=40; b<=47; b++)` 仅对 `b < 47` 开启 `byte_out`，导致 Block 47（`post=0`，输出原本就是精确 E4M3 格点值 `F(Hrtz(acc))`）仍写出 `4.42 MB` 的 4B FP32；紧接着 `Up(48)`（`512 → 256`，`60×36 → 120×72`）在 16 个输出列分片上把这 `4.42 MB` FP32 重复读取 16 遍（**`70.78 MB/帧`**，每 wave 发出 `256` 条标量 `global_load_b32` 并转 `_Float16`）。
   - **现成基础**：`hip/deep_fast.hip:541` 的 `decoder_project2x_h16w_byteout_w_bin` **已经编译进了 `if (inputs==512 && outputs==256)` 分支**，仅因 `hip_reference_network.h:903` 的 `byte_in` 检查写死 `ic==1024&&oc==512` 且 `line 1025/1035` 未传 `byte_out/byte_in` 而未生效。
   - **物理收益**：接通后每帧直接消除 **`56.40 MB`** 全局显存流量（Block 47 少写 `3.32 MB` + `Up(48)` 少读 `53.08 MB`），`Up(48)` 输入加载由 `256` 条 `global_load_b32` 缩减为 `32` 条合并 `global_load_b64`，**0 字节额外显存、100% 逐位一致**。
2. **`Up(39)`（`1024 → 512`）与 `Up(48)`（`512 → 256`）W16 FP8 WMMA + 字节输入直喂**：
   - **现状病灶**：`decoder_project2x_h16w_wide_body`（`hip/deep_fast.hip:518`）即使在 `ByteInput=true` 时，仍先把 1B E4M3 输入逐字节解包（`v_bfe_u32` + `v_cvt_f32_fp8` + `v_cvt_f16_f32`，`Up(39)` 每 wave 白跑 **1,536 条 VALU 转换**，`Up(48)` **768 条**），再去读**未分块 row-major `_Float16` 权重 `w16+(row+rc())*inputs+k+gr()*8`**——相邻 lane `rc=0..15` 跨步达 **`2,048 B`（`Up(39)`）/ `1,024 B`（`Up(48)`）**，每条 `global_load_b128` 打散到 **16 条不同 cache line**！此前 `2026-10-01 §11` 测试 `HIP_DEC_F8W` 时既无 `ByteInput`（还在用 `dec_pack8` 现场把 FP16 转回 FP8），权重也是未分块 row-major。
   - **改造方案**：`decoder39-weights.f32` 与 `block48-weights.f32` 矩阵区已于 `2026-10-01 §9` 实测 100% 落在 E4M3 格点且 `wmma_fp8_fp8` 与 `wmma_f16` 19 组逐位一致。原地复用 `@decoder-f16r` 键将矩阵区打包为 `FragmentPackedMatrixW16` E4M3，核内直接用 1B E4M3 `ByteInput`（`i2 a0, a1`）与 16B 合并权重（`w16_i4 bw`）执行 `wmma_f32_16x16x16_fp8_fp8_w32_gfx12`：
     - 消除 `Up(39)`/`Up(48)` 全部 **1,536 / 768 条输入转换指令**；
     - 权重加载指令数减半（`128 → 64` / `64 → 32`）且从 16 路跨行散读变为 **100% 合并的 512B 全 wave 连续加载**。

### 候选 2：在写入端消除全网最后 2 次 `vit_gather_b8` 发射（发射数 `153 → 151`，释放 `5.0 MiB` `gather_maps` 显存）

**2026-10-17 已结题 STOP**：Gold 0 bit-diff；三轮ABBA均值全非改善，pooled +5.691µs，保留生产两次gather。详 `results/vit-write-gather-20261017/`。
- **现状病灶**：全网在 `Down(30)` 后与 `Block 38` 后各调 1 次 `vit_gather_b8`（共 2 次 launch），每次每个线程还要从显存读取 16B 的 `uint32_t indices[655,360]` 查找表（常驻占 **`5.0 MiB` VRAM**，每帧额外读 **`5.0 MiB`** 索引表 + **`1.31 MB`** 字节搬运）。
- **为什么 2026-09-30 的 `HIP_VIT_GATHER_FOLD_HOST` 在 900p 变慢**：当时把逆重排塞进了**读取端 `Up(39)`（`decoder_project2x_h16w_gin`）**——不仅丢掉了 `_w` 宽尾优化，而且 `Up(39)` 对每个输入 token 要跨 32 个输出列分片**重复读取 32 遍**，在读取端做 `vg_cinv` 通道重排把 32 遍连续向量读全打碎成了标量散读。
- **正确姿势（双写入端重排）**：
  1. **入口写入端 `Down(30)`**：`mh_pool_project_group_body`（`hip/multihead_fast_padded.hip:424`）**本来就有 `Gout=true` 分支**（`pg_tinv(q)*N + pg_cinv(c)`，仅 6 条位运算指令）！只需在 `c512_head_group.inc` 导出 `mh_pool_project_group_c512_gout_out8`（`Gout=true, ByteOut=true`），即可在 `Down(30)` 写出 `640 KB` 1B E4M3 时直接按 ViT 顺序落盘，省掉入口 `vit_gather_b8`。
  2. **出口写入端 `Block 38`**：在 `vit_stream_project_tiles`（`hip/vit_stream.inc:66`）新增 `Gout=true` 导出 `vit_stream_project_n64_bb_bout_gout_compactw_w16`，仅在 `block == 38` 写出 `640 KB` 1B E4M3 时按 `raster(t)*1024 + ch(c)` 落盘（每个字节只写 **1 次**，L2 内完成）。下游 `Up(39)` 原封不动继续走 `decoder_project2x_h16w_byteout_w_bin` 的连续 8B/16B 合并读取！
- **物理收益**：全网每帧 kernel 发射数 **`153 → 151`**，释放 **`5.0 MiB`** `gather_maps` 显存（`gather_maps_MiB: 5.0 → 0.0`），每帧再省 **`6.4 MiB`** 显存流量，100% 逐位无损。

### 候选 3：`Down(8)`、`Down(14)`、`Down(22)`、`Down(30)`（`mh_pool_project_group_c{64,128,256,512}`）W16 FP8 WMMA + 池化去分支

**2026-10-17 已验收合入**：Gold 0 bit-diff；三轮均值/p99全改善，pooled mean −20.704µs / p99 −27.539µs。池化有效性/顺序保留，采纳FP8字节LDS与W16矩阵路径。详 `results/down-f8-w16-20261017/`。
- **历史误判纠正**：`2026-10-01 §9` 普查曾验证 `block{8,14,22}-ds.f32` 与 `head-matrix.f32` 权重 **100% 在 E4M3 格点上**，但误以为激活输入是 `raw`（`Hrtz`）而标为“不在格点”。实查 `mh_pool_project_group_body`（`hip/multihead_fast_padded.hip:415`）：
  `v = F(H(H(top + bottom) * .25f)); pooled[k*S + c] = (_Float16)v;`
  2×2 池化均值在写入 LDS `pooled` 之前 **已经过 `F()` 量化到了精确 E4M3 格点（且 `-0.0f` 已转 `+0.0f`）**！
- **改造方案**：
  1. `pooled` 直接存 1B E4M3（LDS 占用减半：`c512` `16.6 KB → 8.4 KB`，`c256` `8.4 KB → 4.3 KB`），`@ds-frag` 原地改为 `FragmentPackedMatrixW16` E4M3 权重；
  2. 每个 K32 `chunk` 内用 1 条 `ds_load_b128` + 4 条合并 `global_load_b128` 喂入 `wmma_f32_16x16x16_fp8_fp8_w32_gfx12`，保留每 K32 步末尾的 `acc[j][e] = H(acc[j][e] + sum[j][e])` 舍入边界，**100% 逐位等价**；
  3. 顶部 `for (uint k=0; k<16; k++)` 池化循环对内部完整 tile（`first + 15 < M && !valid_width`）走无分支快路，消除现存的 16 个循环分支。

### 候选 4：`Down(4)`（`mh_pool_project_c32_b8_out8`，`480×288`）4 列分片合波（`34,560 → 8,640` waves）+ W16 权重

**2026-10-17 已结题 STOP**：Gold 0 bit-diff；三轮均值全部退步，pooled +5.546µs，保留生产旧四wave。详 `results/pool32-wave-w16-20261017/`。
- **现状病灶**：`mh_pool_project_c32_b8_out8`（`hip/multihead_fast_padded.hip:374`）每 wave 只算 16 列（`nt=4`），导致 `138,240` 个 token 发射多达 **`34,560` 个 wave**，每个 16-token 输入块被 4 个独立 wave 重复读取 4 遍，且权重 `(col+rc)*32 + half*16 + g*8` 是未分块 row-major（相邻 lane 跨 32B）；尾段还多做了一次无意义的 `acc[e] = Hrtz(0.f + sum[e])` 和双重 `q8(F(acc[e]))`（两次 `cvt_pk_fp8_f32`）。
- **改造方案**：对齐 `mh_pool_project_group`，改为每 wave 负责 16 tokens × 64 全列（`nt=1`，总 wave 数砍至 **`8,640`**），输入 `pooled` 每 wave 只读 1 次，`@ds-cast` 原地用 `FragmentPackedMatrixW16` 打包为 100% 合并 `global_load_b128`，尾段直接 `fmed3f(Hrtz(sum[e]) + 0.f, -448.f, 448.f)` + 单次 `cvt_pk_fp8_f32` 写出。

### 候选 5：融合上采样块 `UpBody`（Block 56 `C128`、Block 62 `C64`、Block 66 `C32`）上采样投影段 W16 FP8 化

**2026-10-17 已结题 STOP**：56/62 Gold同但pooled mean +3.555µs/p99 +20.987µs；独立派发发现66原路径缺字节low导出，补通65→66后完整Gold失败（2,768,436 half值差），未进入完整组合ABBA。生产保原路径，详 `results/upbody-f8-w16-20261017/`。
- **现状病灶**：`swin_wave2_body<Up=true>`（`hip/wave_owned_mh.inc:328`）与 `cw_body<Up=true>`（`hip/wave_owned_c32.inc:223`）在 `LowByte=true` 时，仍用 8 条标量字节加载 + `cvt_f32_fp8` + `cvt_f16_f32` 构造 `h8 a`，并从**未分块 row-major `_Float16` `upw`**（跨 16 条 cache line）加载 `b` 跑 `wmma_f16`。
- **改造方案**：`block{56,62,66}-weights.f32` 矩阵区同样 100% 在 E4M3 格点上。将 `in` 改为单条 8B `i2` 加载（零转换指令），`upw` 矩阵区原地按 `FragmentPackedMatrixW16` E4M3 排列并改用 `wmma_fp8_fp8`（尾部 `upw[IC*C + ...]` skip 缩放保持 f32 不变），消除标量拆字节与跨行散读。

---

## 4. 已封板边界（禁止换名重开）

- **hipGraph 全线封板**：旧 `opt.graph=1`、外层同工 `graph0`（`ReplayOff` 161 节点）与 2026-10-14 结题的 `ReplayAuto`（162 节点强 DAG，含 `EventRecord`）在 RX 9070 XT 上均无净收益或尾延迟退步，生产维持 `EagerAuto`（`DLSS5_SUBMIT_PULSE=2, opt.graph=0`）。
- **C256 `SpStage(16,6)` / `SpStage(49,6)` 边界不动**：首尾层（`15, 22, 48, 55`）因 `Block 22` `post=3` FP32 合同、1080p `W2_FAST_NUM=3 vs 0` 数值合同差异及 `c64-wave2-fast`（LLVM23 + `W2_QKV_FUSE=3` + `W2_FFN_QT_BATCH=4`）单层核快于 COMGR 持久核，不并入 `SpStage`；`sp_init_pair` 会禁用 `submit_pulse=auto`，不启用。
- **C256 Attention W16 / C64 & C128 FFN W16**：已实测在 4/8-wave WG 屏障下打平（`+0.8 µs` / `-1~-7 µs` 噪声），不合入。
- **Uniform-SGPR `m0` `v_movrel` 转三元选择树**：已于 `isa-audit2-20261014` 实测证伪（打断 VOPD 并生成 56 条 `v_cndmask_b32_e64`，退步 `+0.14 ms`），严禁替换 uniform SGPR 相对寻址。
- **C512 块内转置 WMMA FB8（`HIP_VIT_INTER_BYTE` bit 4 = `16`）**：已实现验证逐位一致且 3/3 全胜（`-37.6 µs`），但因转置 WMMA 下 `av8` 跨 512B 访问略弱于非转置 `mode=15`（`-47.2 µs`），保留为可选诊断位，默认维持 `HIP_VIT_INTER_BYTE=15`。
- **其余 370+ 历史负账**：动手前先检索 [failed-experiments.md](failed-experiments.md)。

---

## 5. 固定工程纪律

- **GPU 锁与护栏**：所有 GPU/远程 RTC 操作必须先做 `game-check`、进程检查、确认实验盘余量 `≥100 GB`、获取原子锁 `D:\DLSSNR-Lab\gpu.lock`，并挂 15 秒游戏看门狗；游戏运行期间严禁抢占 GPU、严禁替换 DLL/HSACO。
- **数值与验收门**：Original结构/布局/ISA优化必须对 `0900E489A397302B93074A878C694B9C3E7C3400CFE2DFDD3CC727ABE1050F4A` 逐位一致（`0 bit-diff`）；Small必须与310.9.16 Preset2另立gold，不拿训练差异强求旧0900，性能只认单进程同批交错 3 轮 4-slot ABBA（`edges_only=1, NET0`）。
- **版本与仓库卫生**：commit 不加 `Co-Authored-By`；共享分支禁止 `amend`；未经用户显式指令不改动已安装游戏文件或已发布 ZIP。

## 6. 2026-10-18 第三阶段验收

- Small：900档1600×1024 → C512 52×32 → ViT28×16；720档1344×768 → ViT24×12；free逐节点ceil-half/round4，跨模式/尺寸重建缓冲。
- Oracle：900 **47.4026 dB**，720 **47.7028 dB**；720抓取补UAV屏障，API输出hash未变。对内部Post层比较，不称NV逐位一致。
- 9070三轮4-slot ABBA：900 Original **6.558681** → Small **4.376946 ms/frame**（−33.2648%）；各960稳态样本，逐轮正收益。1080沿用前阶段 **5.553510 ms/frame**。
- Small AE从8个ViT块11..18的权重计算gain；F8保留。MODE1使用Small独立history-head和0x3A23=0.76708984375；时序重置及F9模式回环通过。
- 最终1080 Original Gold仍0900E489…，Small仍65D71486…；80模块逐SHA复用。详[第三阶段回执](results/small-complete-20261018/README.md)。
- **交付完成**：0.44正式三包与双游戏安装均通过；逐文件备份、回读、smoke、SHA和rollback回执见releases/0.44与results/game-install-20261018。下一步只待Zero实玩观感/是否上传公开镜像。

## 7. 2026-10-18 F9紧急修复（最终4764e4f1全部交付）

Zero实玩暴露剑星direct输出UAF、同尺寸重建与producer wait之后懒上传死锁，鬼武者WindowsApps只读配置导致F9被旧磁盘值覆盖。已修调用后重新取输出、保留SuppressFps/同尺寸资源、两模型权重分表常驻和wait前冷预热，F9内存立即切换/写盘best effort，鬼武者优先XboxGames镜像。CPU编译与只读配置测试通过。等待鬼武者PID27916退出后，严格空闲重跑GPU门，再重装/更新0.44三包。首次GPU测试包装器混入stdout误判busy已纠正并披露，初次回执不作正式空闲验收，详results/f9-hotfix-20261018。

### F9最终载荷4764e4f1

- 空闲-idle-r3的cold/warm/AE deferred、1080/900 direct输出与SuppressFps、Gold、runtime切换与XboxGames路径均通过；只读配置CPU即时/跨tick回归通过。
- 追加同尺寸MODE1的两模型history head缓存交换、blend更新和reset；GPU读回32系数逐项与相应模型一致，history输出与冷启动对照逐位一致。
- 已重装4764e4f1，安装smoke0，备份D:\DLSSNR-Lab\game-install-20261018-f9fix-r3\backup-20261018-102323。最终0.44三包压缩/回读进行中，旧ZIP已移history保留。

### F9交付完成

最终4764e4f1已补齐同尺寸MODE1 head/blend/reset并重跑-idle-r3全部回归；双游戏最终重装smoke0，Zero 实玩确认《剑星》与《鬼武者》内按 F9 切换均正常生效。正式三包更新完成（Magpie cf812fe7… / Opti40b4021a… / REFramework0f10b06a…），44shader/80模块/93Small权重/ZIP全文件回读与独立复验通过。备份game-install-20261018-f9fix-r3/backup-20261018-102323；旧正式包与6794中间包均留history。公开镜像已上传：[夸克](https://pan.quark.cn/s/ea87c0ffd257) · [Gofile](https://gofile.io/d/YCNT57JP)。详releases/0.44/README.md。

## 8. 2026-10-23 第一阶段研究完成：1x与Mochi差距刷新

本轮不改变0.44现装/发行状态。完整结论与第二阶段候选见 [gap-refresh-20261023](results/gap-refresh-20261023/README.md)。

- 同1088/640token三轮ABBA：当前1x **8.533ms** vs锁版旧M **7.867ms**，剩差 **0.666ms**；另旧HIP→当前同批 **8.923→8.554ms**，已追回 **0.369ms**。
- 新发现：1088被两处仅准1152的compact资格挡住，实际仍161dispatch。只扩这两处的隔离候选153dispatch，Gold0，三轮 **8.570→8.355ms**（省 **0.216ms**），均值/p99每轮改善；**未合生产，未做游戏/全产品兼容回归**。候选未与M同批，不机械扣除两个差额。
- a9源码/52个ACO记录已精确映射反汇编；新版ACO没有在本机运行。优先序：1088资格产品化 → C32 FMA_MIX/operand安排ISA筛选 → C512 N-pair供数 → 有具体新条件的C512链/stage。旧失败项照旧封存，非本轮授权自动重开。
- 后续实施以这份新实测和具体候选为起点；生产版本/游戏配置保持既有0.44。GPU锁已释放。

## 9. 2026-10-23 第二阶段（三刀按序）

- **刀1已采纳到生产源**：新增Original1088/900 compact资格；完整Frame1088省0.210ms、900省0.071ms，均三轮ABBA及每轮p99改善；1152中性且两轮尾反例留档。54+9帧Frame Gold、144帧缺导出/FAST0/MP热冷兼容通过。见 [knife1](results/gap-phase2-20261023/knife1/README.md)。已装0.44和发行包未改。
- **刀2开始**：C32首级FMA_MIX常量打包与ALT/PAT，先本地正式LLVM23 ISA、再Gold和三轮ABBA。
- **刀3排后**：刀2结题后做单个C512 N-pair供数核；不同时改变多刀基线。

### 第二阶段刀2结题，刀3开始

刀2 MIX/ALT机器码相同，仅测MIX；54帧Gold0，完整Frame三档各3轮ABBA无净收益（1088+10.902µs且p99+31.698µs，1152近零，900不稳定），**STOP，不合生产**。原40模块保持，刀3继续以刀1final为基线。刀3针对C512 projection的N-pair布局/消费循环；当前旧路已经A复用四个输出片，不预记“A读取减半”收益。

### 第二阶段三刀结题

- 刀1采纳：Original1088/900资格放宽，完整Frame分别−0.210ms/−0.071ms，Gold/回退/热切门通过。
- 刀2 STOP：C32 MIX没有跨档净收益，生产HIP不改。
- 刀3 STOP：66帧Gold同，1088三轮均值全退（合并+6.152µs/p99+21.655µs），900近零；1152−11.935µs但有尾反例和较大漂移，保原数据。没有继续扫其它N-pair变体。
- 最终生产差异仅刀1两资格；三刀全部原始CSV/ISA/补丁/身份记录在 [gap-phase2-20261023](results/gap-phase2-20261023/README.md)。正式宿主构建/最后独立核对收尾中，不安装游戏、不改0.44 ZIP。

第二阶段最终交付完成：三刀独立读审齐，生产仅刀1；正式addon/runtime构建通过，产物 `/home/lmxxf/work/dlss5-gap-phase2-20261023/final-payload/`，[构建回执](results/gap-phase2-20261023/final-build/receipt.json)。源、测试、产物可复现，GPU锁释放。没有后续自动部署/发包任务；当前游戏仍为既有0.44。

## 10. 2026-10-23 第三阶段：A→B→C按序

基线为第二阶段只采纳刀1的生产源码与原80模块。剩余旧M差“约0.45ms”仍是跨批估计，尚未更新为最终同批；不预先限定剩差仅由访存/跨核排空造成。

- **A进行中**：当前FAST1 ViT QT32双16query共享K/V；旧10-01 M32也是19组逐位通过后性能负，并非仅有损。此次明确真实半grid、400尾单链、当前FAST1；正式COMGR旧89Fn身份保持，新98VGPR/0LDS，源/ISA证明逻辑K/V请求减48%/50%。三档三图样Gold及缺导出回退已过，三档3轮ABBA进行中。
- **B待A结题**：仅C512 FFN→projection单边tile-counter，严格有界等待、epoch、实际重叠/错误路径与完整Gold，再ABBA。
- **C待B结题**：实际热点load/wait窗口审计，有具体安全可移动的预取才实验，不拿s_wait_loadcnt0本身判罪。
- 结果目录 [gap-phase3-20261023](results/gap-phase3-20261023/README.md)，GPU/RTC仍唯一持锁队列。

### 第三阶段A结题，B开始

A Gold60帧同且真实共享KV，但三档ABBA无收益：1088+0.097ms、1152+0.090ms、900+0.012ms；STOP。当前生产仍仅第二阶段刀1。B开始C512 FFN→projection单边有界tile-counter，需先证明依赖/实际重叠/错误路径再测收益，C继续排后。

### 第三阶段B结题，C开始

B已证明三档16边真计算重叠、ordered控制0，Gold/超时拒批/64bit边界门通过；但完整Frame九轮mean/p99全退（900+0.086ms、1088+0.125ms、1152+0.053ms），STOP。生产仍第二阶段刀1。C现在审当前热点实际load/wait依赖及prefetch机会；不把wait0本身当冗余，不重复旧D1/2/3扫描。

### 第三阶段C结题

C找到一个真实可移动尾窗口且正式ISA证实，只目标核顺序变，Gold54帧同；三档有效3轮（原r3因root文件准备干扰预先排除并补quiet r3）近零/不稳定，STOP。A/B/C全部不进生产，当前生产仍第二阶段刀1。最后补当前生产1x与锁定旧M同1088纯NN三轮对账，将约0.45估计换成实测；新版a9 ACO仍未本机运行。

### 第三阶段全部结题与最新差距

A/B/C全部STOP，源/ISA/Gold/三档ABBA与独立复核齐；生产无本阶段新增改动，仍第二阶段刀1。最后当前生产对锁旧M的同1088纯NN三轮实测 **8.366908 vs7.888277ms，剩差0.478631ms（6.0676%）**，取代估计0.45；各臂960帧、raw身份保持。新版a9 ACO未本机同口径测，剩差尚未因果拆完。见 [第三阶段结题](results/gap-phase3-20261023/README.md)；无自动后续实验/部署任务，GPU锁收尾释放。

## 11. 2026-10-23 第四阶段（两刀按序）

刀1进行中：用户指定head-major ViT V-fragment生产/消费配对，正式ISA确16个V u8读变1个b128，Q/K/容量/数学不变；独立GPU QKV逆排/AV/canary/reuse及CPU raw回读通过，三档54帧Gold通过，三档3轮ABBA运行。未测tile-major先导仅归档；pair首次物理.f32不存在而CPU拒绝，改用生产ReadWeights回落f16，未动数学。刀2待刀1闭环：Small900/720/1088 compact资格与实际token/选核。

### 第四阶段刀1已采纳，刀2开始

V-fragment head-major配对布局进生产默认，范围Original FAST1 MP1显式AE0三档；54FrameGold+39默认/回退门及GPU pair/CPU raw验通过。三档Frame省15.497/58.899/51.794µs（900/1088/1152），9mean全胜，900R3尾小反例留；1088纯NN同批追回36.265µs。80模块双架构SHA/ELF核齐，现装不动。刀2开始Small900/720/1088实际几何与compact/W16/字节链资格，不把1x V-fragment自动扩Small。

### 第四阶段刀2收益门通过，生产收尾

Small三个新scope：900/720/free1088，72FrameGold+24traceF32+108public兼容全过；每档3ABBA九轮mean/p99都改善，分别省64.446/54.133/245.105µs，活动weights各省120MiB。正在将集中资格helper合生产并做最后默认Gold/构建，80模块仅刀1已有四项变化，不扩VF Small/AE；现装/已发包仍不动。

### 第四阶段最终闭环

两刀均已合入生产。最终重编标准/free1072 Frame与已测候选SHA完全一致，7档默认回归21帧全部Gold 0-diff；完整addon/runtime正式构建成功，双架构80模块与生产SHA/ELF核同。证据及最终payload身份见 [第四阶段报告](results/gap-phase4-20261023/README.md) 与 final-build/receipt.json。所有GPU作业已结束、gpu.lock已释放。gfx1200仅编译核验；游戏安装及已发布ZIP未动。

## 12. 2026-10-23 第五阶段（进行中）

第四阶段完整生产为基线。刀1拆分C512成对FP8打包、permlanex16串行norm及组合三候选，先正式COMGR/Gold再三档ABBA和1088纯NN；w2仅独立验证，不偷混收益。刀2扩大Small四档及Original/Small AE的V-fragment资格，保reuse/anchor数学，另跑实际复用与四档ABBA。基线三档27帧Gold已过；生产尚未加入第五阶段候选。证据目录results/gap-phase5-20261023。

### 2026-10-23 第五阶段刀1：仅采纳C512成对FP8打包

正式COMGR pair-only：c512_qkv_attention_compact_w16的cvt144→72、总指令2483→2104、VALU1367→1025，VGPR188/LDS6144/private0不变；113导出仅两个compact核变化。基线/三候选三档三图样共108帧Gold、独立GPU norm/pack探针及CPU raw复核全通过；非W16三档paired18帧通过。

pair-only Frame900/1088/1152分别−23.331/−11.701/−18.815µs，9mean全改善，三档pooled p99改善，分轮尾反例留；同1088纯NN8.342063→8.326007ms（−16.055µs），3mean全改善、pooledp99−6.303µs。permlanex16单项1088+4.869µs，组合虽mean略好但pure pooledp99+184.606µs，均不进生产，serial_norm保持旧实现。正式pair-only源与双架构编译完成，gfx1201最终113函数码/资源全同已测pair；gfx1200只编译核验。证据results/gap-phase5-20261023/knife1。

### 2026-10-23 第五阶段刀2：V-fragment扩围Small/AE采纳

Small900/720/常规1080/free1088的V-fragment成对资格已合生产，保持精确shape/token绑定；Original既有三档与Small四档AE允许，AE策略/anchor/默认idle阈值未改。72帧SmallGold、84帧受控AE1、24帧默认500ms、48帧AE2/3共228帧跨臂0-diff、完整AECSV同。mode2实际全计算，mode3为0,1,1,1,0,1；不拿AE0性能当复用帧收益。

四档各3ABBA：9004.268004→4.244956ms（−23.049µs），7202.994176→2.989513（−4.663µs），11525.559334→5.499693（−59.642µs），free10885.382278→5.317102（−65.176µs）。11/12mean改善，12/12p99改善；720R2mean+2.378µs保留，该档仅微小收益。最终header/两个重编Frame复现实测候选身份；正式默认/off回归与完整构建收尾中。证据results/gap-phase5-20261023/knife2。

### 第五阶段最终闭环

生产最终默认/on/off 9组36帧跨臂输出与AE日志全部一致，两个Frame重编SHA复现实测候选。正式addon/runtime构建成功，双架构80模块逐SHA/ELF核验；最终payload位于`/home/lmxxf/work/dlss5-gap-phase5-20261023/final-payload`，身份见results/gap-phase5-20261023/final-build。刀1只收pair打包，刀2收Small/AE资格，permlane与combined明确STOP。gpu.lock已释放、无实验进程，剩余磁盘279784194048B；游戏安装与已发布ZIP未改。完整报告[第五阶段](results/gap-phase5-20261023/README.md)。

## 13. 2026-10-23 第六阶段（进行中）

第五阶段最终生产作基线。刀1新增QK head-major配对exports，原V-only导出不变；正式ISA producer b128与consumer入口Q/循环K,V b128均核实，VGPR consumer61→60，四tokenGPU/CPU逆排与210帧七档Gold/AE通过，84slot ABBA开始。刀2A准备Up wide成对转换独立宏，仅deep_fast-packed-fast启用；B的LDS ds_load_tr8硬件门初证gfx1250限定，gfx1201本地汇编拒绝，正式COMGR拒绝待归档，不重刷历史负收益VT。生产尚无本阶段变更。证据results/gap-phase6-20261023。

### 2026-10-23 第六阶段刀1：QK-fragment限三档采纳

新QK head-major配对：producer b128、consumer入口Q与循环K/V各b128，consumer VGPR61→60，producer78/LDS16K不增。四token GPU/CPU逆排、126帧Gold＋84AE通过；七档84slot ABBA及1088pure完成。仅默认开启Original900（−11.885µs）、Small900（−7.692µs）和Small1080（−11.083µs），pooledmean/p99均改善；所有分轮尾反例保留。其余四档继续V-only：Orig1088不稳、Orig1152和Smallfree1088尾退、Small720均值退。试验pure1088−13.577µs但p99+10.364µs，不能写成默认生产1088追回。限定默认/off/缺export33帧及三档AE18帧通过，4模块双架构更新，其他76保持。证据results/gap-phase6-20261023/knife1。刀2A Up成对打包继续测；B LDS TR本地/正式工具链均拒gfx1201，不发射不支持指令、不重刷历史负VT。

### 2026-10-24 第六阶段最终裁决

仅采纳QK-fragment的Original900、Small900、Small1080三个scope，分别省11.885/7.692/11.083µs；其余四档仍V-only。试验1088pure−13.577µs不是生产追回，pooled尾+10.364µs使该scope未收。刀2Up全开及最后FP16权重专用版均STOP：后者Small900mean近零、p99+22.444µs，未进一步加shape分支；控制组未执行改核，不算收益。C512 LDS TR本目标工具链不支持，CPU预期拒绝，无GPUlaunch。生产Up/recipe均与刀1后基线同，完整数据及最终80模块/宿主身份见[第六阶段报告](results/gap-phase6-20261023/README.md)。

第六阶段最终产物已封口：正式宿主与80模块身份核同，production无Up实验宏；仅QK三shape。GPU锁已释放，无残留实验进程，磁盘余261992218624B。回执：results/gap-phase6-20261023/final-build/receipt.json。

## 2026-10-24｜v0.45 毕业收官：双游戏安装与三包正式交付

明确授权后复用gap-phase6最终宿主和80模块，未重编宿主/HIP或重跑ABBA；仅纳入此前已采纳的第二/四/五阶段及第六阶段QK三shape，所有STOP候选不入生产。

《剑星》addon、《鬼武者》root/_storage_ runtime、两游戏双架构80模块及根/分架构SHA清单共169项全部SHA读回；完整备份1502文件2687928349B，rollback.ps1先于替换生成。六份default/custom/native从首次preflight到最终逐字相同，连同其他ini/cfg共16记录全部保持；两路安装runtime smoke exit0/15秒上限。安装器三次WinPS兼容问题及恢复证据保留，成功后再次核169/16；一个派生shader缓存纳入最终备份，不伪称失败恢复目录完全无新增。

D:\給網友打包三个0.45完整ZIP及.sha256已生成。每包44 shader编译，共132 PASS；REFramework包内runtime smoke exit0/15秒；所有ZIP条目含清单自身SHA读回。保留旧0.44 history原ZIP/原SHA。最后源码文档38→40修正只改两README，三包仅source tar/release.json/VERSION/SUMS四文件变化，全部重新封包读回，二进制/shader/配置不变。

源码归档SHA3bcfa93db081f139b39170ed2d9c23da35b34759fbfb5e5593c709da971c829c；明确git_base=c24e3a97cdf596d0c997cee7deda91c981d520ce上的dirty生产快照。未推送Git/外部网盘。最终锁释放、无实验进程，D余246590996480B。

- Magpie-DLSS5-AMD-0.45.zip：491900423B，SHA256 `004bcae073fb76efa7f7dd1e88a8d38a4dd3c9884419bfb29722c00e2f07c473`。
- OptiScaler-DLSS5-AMD-0.45.zip：522098432B，SHA256 `96c12292cb11c8b7da026c012166745e0ca330f86d9ff16267a55163d9c367c5`。
- OptiScaler-REFramework-DLSS5-AMD-0.45.zip：533387409B，SHA256 `a46fee43bfc67e8fe367e3a4593ddd71dfbcd8afb9c91c330effa012e8857604`。

回执：[双游戏安装](results/game-install-20261024/README.md)、[0.45发行](releases/0.45/README.md)。用户已提供 0.45 公开下载入口：[夸克](https://pan.quark.cn/s/0ccb3eff675b) · [Gofile](https://gofile.io/d/hmrcTjuS)。

### 2026-10-28 backfeed 刀1闭环；刀2～4待执行

Small900 C256全块融合采纳，87→83派发，三轮ABBA mean−87.598µs/p99−79.872µs；Small896×512自由小窗采用C256+400桶/V-fragment组合，mean−81.966µs/p99−90.334µs。720同样少四次派发但mean+0.405µs/p99+15.896µs，STOP并保留旧默认。默认仅限已测获益shape；Original保持原路由。六档及小窗Gold、裸网trace逐位通过，最终默认七档21帧Gold全部0diff。模块未重编，未安装/改发行包。细节results/backfeed-20261028/knife1；下一步刀2独立尾部向量化候选。

### 2026-10-28 backfeed 刀2→3→4全部闭环

刀2 ec91bd4a：contract/project实现LDS转置宽写但VGPR112→113，资源门STOP；qkv_vfrag资源不增、六档Gold全同，72slot三轮ABBA无稳定净收益，STOP。可选expand未执行。刀3 7fbba898：QT_SMALL_MASK本已3，仅补W16_SMALL=3，正式LLVM23基线整SHA同，新增导出零spill、七档Gold同；48slot四档ABBA中Small900 mean+8.466µs/p99+15.439µs，其余档也不稳定，STOP。生产优化仍为刀1a71f5d72的限定shape。

刀4：常规OptiScaler模板补DlssNr 1.0/1.0，实际打包合并函数验证通过。最终双架构80模块复用未变化的已验载荷，逐SHA/ELF核对hip/SHA256SUMS并生成分架构清单；正式O2 addon/runtime/frame构建通过，frame同刀1最终SHA。独立runtime smoke显式Small0.5/skip0/vit_stream3通过，前两次目录缺资源失败留档。交付目录Development/results/backfeed-20261028/final-payload，回执knife4/receipt.json。gfx1200仅ELF/构建核验；未装游戏、未改0.45ZIP、未push。全部四刀结题，无排队自动实验。

### 2026-10-28 gfx1201 汇编审计 Phase1：三项手术全STOP，闭环完成

基线b5875bfa。40模块1976导出完整拆分，六档新鲜派发覆盖56物理模块/函数对；完整.s不入Git。LLVM23 .hip→.s→cc1as(-real-true16)→lld闭环：64核代码/资源/完整metadata一致；真实C32 prefix shift→add等价.s修改后七档21帧loader/Gold通过，仅作闭环证明不进生产。C32/C64带DWARF行表重编201函数机器码/资源同，用内联调用链完成C32逐阶段构型统计。

C128/C256峰值在FFN Expand/Contract共存阶段，静态live234/240而metadata242；屏障处并无242全活。三项独立候选各七档Gold逐位同、四档三轮ABBA共144slot：upwide解码组织无稳定收益；decoder尾循环禁止展开3612→2648条但无稳定净收益；FFN批宽4→2令C128242→180VGPR（occupancy估计5→8）、C256242→216（5→7），四档mean反退20.5/19.6/39.4/22.6µs。全部STOP，生产代码/模块/已装游戏/ZIP不变。真实.s修改证明不冒充提速。报告与源码diff/ISA/曲线/统计/身份回执见results/isa-asm-audit-20261028/README.md；实验大文件清理、锁释放，无push。

### 2026-10-29 ISA Phase2 三刀闭环：均STOP

刀1 57c6533e：C512 V天然片段转置64ds_u8→0、188→187VGPR，Gold通过；局部1088原型小正，资格隔离/缓存环境后的真实产品before-after最终mean+12.284µs/p99+17.925µs，STOP。刀2 33a4d791：contract/project113/115VGPR超112资源门；expand77不增、490→407条、32b8→4b64，Gold通过但四档性能不稳，STOP。刀3 pool/split独立：pool4b64、C64 90→89其余88、LDS不增；split100VGPR、LDS1024→0、u8及F32散写消除，均七档Gold同，分别4档3ABBA无稳定净收益，STOP。

合计258帧Gold0diff/invalid0、216性能slot，实际模块路径/哈希/函数身份核验，原始分轮与中间反例保留。生产代码、双架构80模块与hip/SHA256SUMS复核未变，无新赢家需同步，未部署/改ZIP/push。报告与回执results/isa-asm-phase2-20261028。临时大文件清理、GPU锁释放，三刀已结题，无排队自动任务。


### 2026-10-29 ISA Phase3 四刀闭环

1f780a34（成对打包/预取）、cb5a59d2（保持B复用折叠/预取）、98ba6985（serial_norm缓存）均按四档3ABBA或资源门STOP，生产数学和核代码未采用新改动。全部80双架构模块实际重编、ELF/函数代码/资源复核，更新容器SHA；正式addon/runtime/frame O2构建、七档Gold0bit-diff/invalid0、七档3ABBA及runtime烟测完成。最新1088纯NN三轮NMMN：HIP 8.321360ms、锁版Mochizuki d1185d2 7.864895ms，差0.456466ms（+5.80%）；旧0.478631198ms为跨批历史，不能归因本轮。完整逐轮/负结果/配方身份见results/isa-asm-phase3-20261029/README.md。临时大文件清理、保留隔离payload，不部署、不改ZIP、不push。


### 2026-10-29 ACO hotspot Phase4 三阶段闭环

Step1 4a260061：新版v0.0.8 ACO在32.0.31007.2048原样pipeline-binary导入，53SPV/NRP匹配，无版本白名单绕过。三档3NMMN：Original1088 HIP8.301703/ACO5.670747ms（+46.40%），Small1080 5.005096/3.430510（+45.90%），Small900 3.841217/2.607886（+47.29%）；旧d1185的5.8%不代表新版。Small同帧GPU PP为0.242/0.157ms，Frame残差另含interop/CPU，探针开销单列。

Step2 a8103346：拒用负值/不闭合HIP短事件，改gfx1201硬件实时钟（100MHz设备属性、0.8µs邻接stamp校正），完整153/83业务dispatch，重构闭合0.7–1.0%。Original Top C1280.819/SP0.809/C32post0.728ms；Small C32post/prefix0.762/0.737ms。对手123/71 pass时间同列，未据此编造峰值利用率。

Step3：C128 3A Contract深预取保持B4，VGPR242→248但硬件分配桶不变、0spill；单核微正但整网不稳，STOP。3B严格安全的一条b64移动，136其他函数及metadata全同，单核/整网无稳定收益，STOP。各七档Gold42帧与四档3ABBA完成，不叠加/刷轮、不改生产80/SHA。全部报告/源码/ISA证明/逐轮数据见results/aco-hotspot-phase4-20261029/README.md。临时大文件清理、锁释放，无游戏/ZIP修改、无push；不宣称接近硬件上限或.s唯一路径。


### 2026-10-29 C32专项 Phase5 闭环

bc8cb62a权重共享/双qt均STOP；3b7d0a03资格Small1080 norm-hoist曾赢（mean−42.863µs/p99−43.602µs），最终集成合并p99+24.228µs（R1+65.243µs）撤销；0cc4a90a融合首档Gold不符STOP，8b8dbb7b寄存器Post七档Gold同但单核/整网退步STOP。双架构80模块实际重编、ELF/函数/metadata/SHA核验及正式O2 addon/runtime/frame/pure完成，最终七档Gold/七档3ABBA/三档新版v008纯NN3NMMN闭环。 最新同批Original1088 NN HIP8.299856/ACO5.694142ms（+45.76%），不宣称硬件上限。完整结果results/c32-stage-phase5-20261029/README.md；临时大文件清理、锁释放，无游戏/ZIP/上游源码修改，无push。


### 2026-10-29 C32 Phase5B 收网

修通finish→pool的跨文件Hrtz/FP8零边界，d510bb69独立7Gold42全同，四档合并mean/p99全赢；按Zero校准门进入组合。最终生产保留全档fast norm-hoist+OutProj外提+默认融合，七档Gold42帧0diff/invalid0，七档三轮ABBA mean/p99全部改善、mean各三轮均赢。 Original1152/1088 mean-101.146/-86.825µs，Small1080/900 -51.945/-62.368µs；C32+pool硬件时钟Orig/Small -92.903/-69.280µs。 双架构80模块实际重编、ELF/SHA/metadata核验及正式O2 addon/runtime/frame/pure、runtime-smoke完成。生产W16max158、全部private/spill0，旧7项非默认>160资源例外原样保留清单。 新版v008三轮NMMN Original1088 HIP8.214575/ACO5.685226ms（+44.49%）。 细节results/c32-stage-phase5b-20261029/README.md；已清理、锁释放，无游戏/ZIP/上游源码修改、无push。


### 2026-10-31 K-Search Phase 2～5 生产收网（`k6c3` 封存新 `1.00×` 基线）

定位并修复 `SubmitPulse` 门控（`PulseRejectMask`）遗漏 `Original 1088`（`1920×1088`）与 `Small` 家族（`small900`、`small720`、`smallwindow`）的瓶颈（K6），并证实开启 `SubmitPulse` 后非转置 `w16_fb8`（`split_projection_frag_w16_nof32_bskip` 省 `4.42 MB/帧` `ffn` 死写 + `mh_attention_project_frag_c512_w16_u4_fb8_bout` 93 VGPR，K2-Scope：`1600×960`、`1600×1024`、`Original 1920×1088`）在三档上全部释放纯正加速，结合 `c64/c128_wave2_up_lb_sh` 首 barrier 前 `qt` 循环折叠（砍 `-1,055/-977` 条指令）与 `c64_wave2` 全系压入 `189~191 VGPR`（8 waves/SIMD = 4 WGs/CU，K5）合入生产树。同时修复 `w16_fb8` 提前 `ffn8.reset()` 引发的多工作组同址读写竞态（保持 `if(!fb8&&!w16_fb8)ffn8.reset();` 异址分配）。

- **七档 42 帧 Gold**：`0 bit-diff`（`invalid=0`），首尾帧 SHA256 与基线 100% 一致。
- **七档 3 轮 4-slot Frame ABBA（每档 960 稳态帧/臂）**：
  - Step 4 `k6c3` 7/7 档 mean/p99 全负、20/21 轮胜出：`smallwindow` **`-165.487 µs (-8.81%, p99 -195.606 µs, 3/3)`**、`small720` **`-140.450 µs (-4.75%, p99 -205.720 µs, 3/3)`**、`original1088` **`-119.504 µs (-1.39%, p99 -163.074 µs, 3/3)`**、`small900` **`-88.182 µs (-2.14%, p99 -146.996 µs, 3/3)`**、`original960` **`-24.890 µs (-0.39%, p99 -4.838 µs, 3/3)`**、`small1080` **`-9.783 µs (-0.18%, p99 -48.095 µs, 3/3)`**、`original1152` **`-2.302 µs (-0.03%, p99 -28.669 µs, 2/3)`**。
  - Step 5 正式生产合入后（移除 `ffn8` 同址别名）复核：`smallwindow` **`-166.660 µs (-8.86%, p99 -211.988 µs, 3/3)`**、`original1088` **`-159.277 µs (-1.84%, p99 -180.595 µs, 3/3)`**、`small720` **`-136.781 µs (-4.64%, p99 -188.707 µs, 3/3)`**、`small900` **`-87.286 µs (-2.12%, p99 -169.082 µs, 3/3)`**、`original960` **`-24.445 µs (-0.38%, p99 -14.666 µs, 3/3)`**、`original1152` **`-4.633 µs (-0.05%, p99 -16.966 µs, 2/3)`**、`small1080` `+6.790 µs (+0.13%, 噪声带内)`。
- **最新生产 `pure.exe` vs 新版 `v0.0.8 ACO` 三档 3 轮 NMMN 纯 NN 对账（960 稳态帧/臂，全部 `raw_bitdiff=0`）**：
  - `original1088`：HIP **`8.096690 ms`** vs ACO **`5.646863 ms`**（差 `2.449827 ms`，**`+43.38%`**，较 Phase5B `8.214575 ms / +44.49%` 净快 `-117.885 µs`）。
  - `small1080`：HIP **`4.926222 ms`** vs ACO **`3.443128 ms`**（差 `1.483094 ms`，**`+43.07%`**，较 Phase5B `4.946637 ms / +43.36%` 净快 `-20.415 µs`）。
  - `small900`：HIP **`3.789783 ms`** vs ACO **`2.635953 ms`**（差 `1.153830 ms`，**`+43.77%`**，较 Phase5B `3.795411 ms` 净快 `-5.628 µs`）。
- **80 模块与正式宿主**：双架构 80 模块完整核验并更新 `hip/SHA256SUMS`（生产 C32 W16 max `158 VGPR`，C512 W16 max `97 VGPR`，`c64_wave2` max `191 VGPR`，全部生产与修改导出 `private=0, spill=0`）；正式 O2 `addon64`/`runtime`/`frame`/`pure` 与 `runtime-smoke`（`exit=0`）完成。K3（`c32_wave1_post` 外提 `scales`）与 K4（`sp_run256_w16` `W2_QKV_FUSE`）STOP 已入失败总账。详见 `Development/results/ksearch-step4-20261031/README.md`；临时大文件已清理、`gpu.lock` 已释放，未修改游戏/0.45 ZIP/上游源码，未 push。


### 2026-10-31 K-Search Round 2：`2×2` 探针定位与 R2-D（C32/C64/C128/C256/SP Softmax `ones` WMMA 分母倒数 `8 -> 1` 外提）生产收网

响应闇对 Round 1 评估报告的独立复核（`REVIEW-YAMI-ROUND1-20261031.md`），在 `e7b234e7` 基线上完成两组 `2×2` 破坏性敏感度探针（`probes-2x2-statistics.json`）：证实 C32 Box-Muller 噪声旁路（`Base - P0`）仅省 `+16.89 / +21.29 / +13.86 µs`，而 C32 Softmax 辅助算术与分母归约旁路（`Base - P1`）高达 **`+664.42 / +448.60 / +347.40 µs`**（全网最高敏感度靶点）。顺着 `P1` 审查 `hip/wave_owned_c32.inc:468` 与 `hip/wave_owned_mh.inc:692` 抓出并合入分支 **R2-D**：在 `wmma_f32_16x16x16_f16_w32_gfx12(ones, ...)` 中，因第一操作数 `ones` 为全 `1.0` 矩阵，同一 lane 的 `sums[0..7]` 100% 逐位恒等；将分母倒数外提为标量 `inv = norm_inverse(sums[0])` 与 `inv = w2_inverse(sums[0][0]+sums[1][0])` 后，全网 `c32-wave1*`、`c64-wave2*` 与 `swin-persistent*` 核内 Softmax `v_rcp_f32` 从 **`8 -> 1`（砍掉 87.5% 倒数超越函数，Original 全帧动态消除约 457 万条 `v_rcp_f32`）**。同时收窄 `C512W16Fb8ShapeEligible()` 宿主门禁（显式区分 `small_active` 并加上 `fast_numeric/multi_pass/AE/temporal/observer` 门禁），完成 `small1080` K5 隔离 ABBA（`k5_iso_s1080` 3/3 轮全胜 `-15.896 µs`）、同批交错 `8a9c60fd` vs 最新生产 HIP 三档 3 轮 ABBA `pure.exe` 纯 NN 测速以及 100MHz 硬件实时钟 152/82-dispatch 全网分解。

- **七档 42 帧 Gold**：`0 bit-diff`（`invalid=0`），首尾帧 SHA256 与基线 100% 一致。
- **增量四档 3 轮 Frame ABBA（`r2d_both` vs `e7b234e7`）**：`original1088` **`-22.802 µs (-0.27%, p99 -17.083 µs, 3/3)`**、`original960` **`-22.904 µs (-0.36%, p99 -51.789 µs, 3/3)`**、`small900` **`-7.459 µs (-0.18%, p99 -34.474 µs, 3/3)`**、`small1080` `+0.129 µs (p99 -5.326 µs, 2/3)`；100MHz 硬件实时钟对比 `e7b234e7` 全网净快 **`-25.207 µs`（`Orig1088`, 3/3）** 与 **`-16.137 µs`（`Small1080`, 3/3）**。
- **最终生产载荷 vs `8a9c60fd` 七档 3 轮 4-slot Frame ABBA（84 slots，7/7 档 mean 全负，20/21 轮赢）**：
  - `smallwindow`：**`-196.211 µs (-10.06%, p99 -228.470 µs, 3/3)`**
  - `small720`：**`-178.174 µs (-5.94%, p99 -182.480 µs, 3/3)`**
  - `original1088`：**`-137.372 µs (-1.60%, p99 -146.572 µs, 3/3)`**
  - `small900`：**`-131.231 µs (-3.15%, p99 -188.030 µs, 3/3)`**
  - `original960`：**`-33.746 µs (-0.53%, p99 -20.181 µs, 3/3)`**
  - `original1152`：**`-22.804 µs (-0.25%, p99 -8.443 µs, 3/3)`**
  - `small1080`：**`-4.385 µs (-0.08%, p99 +50.575 µs, 2/3)`**（且独立 `k5_iso_s1080` 3/3 轮 `-15.896 µs / p99 -26.979 µs`）
- **同批交错 `8a9c60fd` vs 最新生产 HIP 三档 3 轮 ABBA `pure.exe` 纯 NN 测速（3/3 档全胜，9/9 轮全胜）**：
  - `original1088`：`8.235997 -> 8.226979 ms`（**`-9.019 µs, 3/3 轮`**）；100MHz 硬件实时钟 152-dispatch 总和 `7917.90 -> 7847.36 µs`（**`-70.540 µs, 3/3 轮`**，其中 C512 `-35.02 µs`、C32 `-13.76 µs`、C64+C128 `-12.86 µs`、C256/SP `-8.09 µs`）。
  - `small1080`：`4.951770 -> 4.945876 ms`（**`-5.893 µs, p99 -9.334 µs, 3/3 轮`**）；100MHz 硬件实时钟 82-dispatch 总和 `4757.59 -> 4740.66 µs`（**`-16.927 µs, 3/3 轮`**，其中 C32 `-10.42 µs`、C64+C128 `-5.52 µs`）。
  - `small900`：`3.801556 -> 3.793241 ms`（**`-8.316 µs, p99 -10.109 µs, 3/3 轮`**）。
- **最新生产 vs 新版 `v0.0.8 ACO` 三档 3 轮 NMMN 纯 NN 对账**：`original1088` `8.219009 vs 5.681563 ms (+44.66%)`、`small1080` `4.944181 vs 3.450339 ms (+43.30%)`、`small900` `3.787933 vs 2.640502 ms (+43.46%)`。
- **80 模块实物与正式宿主**：双架构 80 模块完整实物保留于 `Development/results/ksearch-round2-20261031/payload/HIP/` 并更新 `hip/SHA256SUMS`；正式 O2 `addon64`/`runtime`/`frame`/`pure`/`final-profile` 与 `runtime-smoke`（`exit=0`）完成。R2-A（`r2a1/r2a2`）与 R2-B（`r2b` `c128_wave2` 240 VGPR，查明 16KiB LDS 锁死 4 waves/SIMD）STOP 已入失败总账。详见 `Development/results/ksearch-round2-20261031/README.md`；临时大文件已清理、`gpu.lock` 已释放，未修改游戏/0.45 ZIP/上游源码，未 push。


### 2026-10-31 K-Search Round 3：C32 Softmax 窄 `2×2` 子机制探针与 R3-A（`CW_FMED3_CLAMP=1` 消除 31 条 `v_cmp_gt_f32_e64` + 31 条 `v_cndmask_b32_e64` 与 14~25 SGPR 膨胀）生产收网

响应闇对 Round 2 评估报告的独立审查（`REVIEW-YAMI-ROUND2-20261031.md`），修正 RDNA4 硬件拓扑驻留表（`4 SIMD32/WGP`、`2 SIMD32/CU` 及单 wave WG 无 barrier 槽位限制），补齐非对称 `ones`-WMMA 原语实机验证（`r2d_primitive` 8 项断言 `0 bit-diff`）、11 组 62 文件全配置回归（`FAST0`、`AE=1/3`、`MODE1` 动态 history、F9 切换、free 几何 100% `0 bit-diff`）与 `C512W16Fb8ShapeEligible` 显式 history 门控（`!temporal_feature_tap_active && !pulse_frame_history && !native_post_output`），并在 `b1af6240` 上完成 C32 Softmax 窄 `2×2` 子机制拆分探针（`probes-sm-2x2-statistics.json`）：发现指数位映射/clamp + 概率 `cw_pack8` 量化子机制（`Base - P0`）高达 **`+518.32 / +340.03 / +245.24 µs`**（分母归约子机制 `Base - P1 = +261.75 / +113.30 / +82.62 µs`）。顺着 `P0` 审计 `c32-wave1-fast.hsaco` ISA 抓出并合入分支 **R3-A（`c32-wave1*` 全系开启 `CW_FMED3_CLAMP=1`）**：消除 LLVM 23 在 Softmax 32 次 `cw_clampf` 上生成的 31 条 `v_cmp_gt_f32_e64` + 31 条 `v_cndmask_b32_e64` + 16 条 `v_dual_max_num_f32`（统一合并为无条件码 `v_med3_num_f32`），每个生产 `c32_wave1_*_w16` 核静态指令再砍 **`-47 ~ -62` 条**、SGPR 从 **`42~58` 暴降至 `25~38`（减少 `14~25` 个 SGPR）**，Original1088 全帧动态消除 **2,649.5 万条** `v_cmp_gt_f32_e64 + v_cndmask_b32_e64`！

- **七档 42 帧 Gold 与 11 组 62 文件多配置回归**：全部 100% `0 bit-diff`（`invalid=0`）。
- **单刀 `R3-A` vs `b1af6240` 四档 3 轮 Frame ABBA（`r3a-statistics.json`）**：`small1080` **`-102.729 µs (-1.84%, p99 -479.706 µs, 3/3)`**、`original1088` **`-71.665 µs (-0.81%, p99 -53.318 µs, 3/3)`**、`original960` **`-71.646 µs (-1.10%, p99 -50.313 µs, 3/3)`**、`small900` **`-43.690 µs (-1.09%, 4.0133 -> 3.9696 ms 首次跌破 4.0ms, p99 -57.798 µs, 3/3)`**；100MHz 硬件实时钟对比 `b1af6240` 在 `Orig1088 / Small1080` 上 C32 阶段净快 **`-73.137 µs (3/3) / -70.277 µs (3/3)`**，全网净快 **`-63.953 µs (3/3) / -70.377 µs (3/3)`**。
- **最终生产载荷 vs `8a9c60fd` 七档 3 轮 4-slot Frame ABBA（84 slots，7/7 档 mean 与 pooled p99 100% 全负，21/21 轮 mean 与 21/21 轮 p99 100% 全胜）**：
  - `original1088`：**`-292.672 µs (-3.29%, p99 -221.811 µs, 3/3 mean, 3/3 p99)`**
  - `original1152`：**`-245.174 µs (-2.66%, p99 -167.007 µs, 3/3 mean, 3/3 p99)`**
  - `smallwindow`：**`-170.185 µs (-9.15%, p99 -169.186 µs, 3/3 mean, 3/3 p99)`**
  - `small720`：**`-120.305 µs (-4.16%, p99 -225.340 µs, 3/3 mean, 3/3 p99)`**
  - `original960`：**`-109.002 µs (-1.70%, p99 -86.257 µs, 3/3 mean, 3/3 p99)`**
  - `small900`：**`-108.985 µs (-2.66%, p99 -149.887 µs, 3/3 mean, 3/3 p99)`**（`4.0907 -> 3.9818 ms`）
  - `small1080`：**`-61.757 µs (-1.12%, p99 -55.697 µs, 3/3 mean, 3/3 p99)`**（彻底解决 `small1080` 尾延迟门！）
- **同批交错 `8a9c60fd` vs 最新生产 HIP 三档 3 轮 ABBA `pure.exe` 纯 NN 测速（3/3 档 mean 与 p99 全部 3/3 轮全胜）**：
  - `original1088`：`8.212547 -> 8.127218 ms`（**`-85.329 µs / -1.04%, p99 -63.941 µs, 3/3 轮`**）；100MHz 硬件实时钟 152-dispatch 总和 `7970.43 -> 7832.81 µs`（**`-137.620 µs, 3/3 轮`**，其中 C32 `-89.36 µs`、C512 `-33.73 µs`、C64+C128 `-8.49 µs`、C256/SP `-3.92 µs`）。
  - `small1080`：`4.931139 -> 4.858531 ms`（**`-72.608 µs / -1.47%, p99 -50.211 µs, 3/3 轮`**）；100MHz 硬件实时钟 82-dispatch 总和 `4757.77 -> 4678.54 µs`（**`-79.223 µs, 3/3 轮`**，其中 C32 `-77.24 µs`、C64+C128 `-6.03 µs`）。
  - `small900`：`3.789164 -> 3.722060 ms`（**`-67.103 µs / -1.77%, p99 -93.655 µs, 3/3 轮`**）。
- **最新生产 vs 新版 `v0.0.8 ACO` 三档 3 轮 NMMN 纯 NN 对账**：`original1088` `8.132934 vs 5.629724 ms (+44.46%)`、`small1080` `4.859123 vs 3.367579 ms (+44.29%)`、`small900` `3.740196 vs 2.572050 ms (+45.42%)`。
- **80 模块实物与正式宿主**：双架构 80 模块完整实物保留于 `Development/results/ksearch-round3-20261031/payload/HIP/` 并更新 `hip/SHA256SUMS`；正式 O2 `addon64`/`runtime`/`frame`/`pure`/`final-profile` 与 `runtime-smoke`（`exit=0`）完成。R3-B（ViT QKV 单 wave W16 去 LDS 屏障慢 `+365.58 / +337.65 µs`）与 R3-C（`c128_wave2` 零循环开销压至 `238 VGPR` 近中性 `-0.87 / +3.74 µs`）STOP 已入失败总账。详见 `Development/results/ksearch-round3-20261031/README.md`；临时大文件已清理、`gpu.lock` 已释放，未修改游戏/0.45 ZIP/上游源码，未 push。


### 2026-10-31 K-Search Round 4：回应闇 Round 3 审查、攻克 R4-A（`v_perm_b32(..., 0x06050201u) & 0x7fe07fe0u` Softmax 成对指数位映射合并）与全档大满贯生产收网

响应闇对 Round 3 评估报告的独立审查（`REVIEW-YAMI-ROUND3-20261031.md`），修正窄 `2×2` 探针交互项符号为《355 期》定义 `I = (Base - P0) + (Base - P1) - (Base - P2)`（Round 3 窄探针 `I = +153.703 / +32.457 / +23.651 µs`），修正 `compute-dynamic-rcp.py` 中 Block 70 的 `post_shift = 3`（`original1088` 窗口数 `33,017`，`original1152`/`small1080` 窗口数 `34,945`）并明确标注解析模型边界，补跑 `temporal=1` 下真实触发跨帧 ViT 复用的 `AE=1/3` 回归与 `adaptive.csv` 决策序列对账（`ae_reuse_reg` 32 对 `.f16` 与 `adaptive.csv` 100% 逐字节恒等），在 `gfx1201` 上运行 `r3a_r4a_primitive` 验证 `cw_clampf` 四区间 `4,785,484` 向量与 `R4-A` 全部 `4,513,793` 个位模式 100% `0 bit-diff`，并在 `1005424b`（`R3-A + K5` 固定）上完成 `R2-D` 四臂成分消融（证实 `small1080` 上 `A3_R2D_Both` 相对 `A0_NoR2D` 净快 `-22.61 µs / p99 -34.52 µs` 最优）与 C32 Softmax 窄 `2×2` 探针刷新（`Base - P0 = +452.26 / +276.68 / +202.26 µs`）。顺着刷新后 `P0` 抓出并合入分支 **R4-A（`__builtin_amdgcn_perm(bits(af1), bits(af0), 0x06050201u) & 0x7fe07fe0u`）**：利用 `affine` 在 `[1.03125f, 1.5693359375f]`（`[0x3f840000, 0x3fc8e000]`）内 `bit 23 == 1` 的恒等式，用单条 `v_perm_b32(..., 0x06050201u)` 同时完成成对右移 8 位与 16 位半精度打包，在 `c32-wave1*`、`c64-wave2*`、`swin-persistent*` 与 `c512-m32-mh` 的每个生产核内再精准净砍 **`-47 ~ -48` 条静态 VALU 指令**（消除全部 32 条 Softmax `v_lshrrev_b32` 与 16 条 `v_xor_b32`，`Original1088` 全帧动态再省 `8,835,984` 条 VALU 指令）！

- **七档 42 帧 Gold、`r3a_r4a_primitive` 穷举原语与 `ae_reuse_reg` 跨帧复用回归**：全部 100% `0 bit-diff`（`invalid=0`）。
- **单刀 `R4-A` vs `1005424b` 四档 3 轮 Frame ABBA（`r4a-statistics.json`）**：`original1088` **`-114.976 µs (-1.32%, p99 -70.006 µs, 3/3)`**、`original960` **`-69.891 µs (-1.11%, p99 -16.916 µs, 3/3)`**、`small1080` **`-68.486 µs (-1.28%, p99 -58.480 µs, 3/3)`**、`small900` **`-43.067 µs (-1.08%, p99 -26.696 µs, 3/3)`**；100MHz 硬件实时钟对比 `1005424b` 在 `Orig1088 / Small1080` 上全网净快 **`-81.560 µs (3/3) / -72.277 µs (3/3)`**（C32 `-58.78 / -52.11 µs`、C64+C128 `-23.81 / -15.48 µs`）。
- **最终生产载荷 vs `8a9c60fd` 七档 3 轮 4-slot Frame ABBA（84 slots，7/7 档 mean 与 pooled p99 100% 全负，21/21 轮 mean 与 21/21 轮 p99 100% 全胜）**：
  - `original1088`：**`-464.790 µs (-5.23%, p99 -421.979 µs, 3/3 mean, 3/3 p99)`**（`8.8898 -> 8.4250 ms`）
  - `original1152`：**`-351.489 µs (-3.78%, p99 -259.129 µs, 3/3 mean, 3/3 p99)`**（`9.2962 -> 8.9447 ms`）
  - `original960`：**`-200.781 µs (-3.11%, p99 -211.649 µs, 3/3 mean, 3/3 p99)`**（`6.4586 -> 6.2578 ms`）
  - `smallwindow`：**`-181.257 µs (-9.74%, p99 -144.918 µs, 3/3 mean, 3/3 p99)`**（`1.8610 -> 1.6797 ms`）
  - `small900`：**`-180.567 µs (-4.38%, p99 -287.167 µs, 3/3 mean, 3/3 p99)`**（`4.1272 -> 3.9466 ms`）
  - `small720`：**`-179.660 µs (-6.15%, p99 -309.420 µs, 3/3 mean, 3/3 p99)`**（`2.9218 -> 2.7421 ms`）
  - `small1080`：**`-136.189 µs (-2.48%, p99 -180.583 µs, 3/3 mean, 3/3 p99)`**（`5.4992 -> 5.3631 ms`）
- **同批交错 `8a9c60fd` vs 最新生产 HIP 三档 3 轮 ABBA `pure.exe` 纯 NN 测速（3/3 档 mean 与 p99 全部 3/3 轮全胜）**：
  - `original1088`：`8.175514 -> 7.983631 ms`（**`-191.883 µs / -2.35%, p99 -191.756 µs, 3/3 轮`**，首次突破 8.0ms 大关！）；100MHz 硬件实时钟 152-dispatch 总和 `7961.91 -> 7740.99 µs`（**`-220.920 µs, 3/3 轮`**，其中 C32 `-146.04 µs`、C512 `-34.83 µs`、C64+C128 `-30.98 µs`、C256/SP `-9.64 µs`）。
  - `small1080`：`4.915158 -> 4.794650 ms`（**`-120.507 µs / -2.45%, p99 -73.467 µs, 3/3 轮`**）；100MHz 硬件实时钟 82-dispatch 总和 `4754.56 -> 4612.40 µs`（**`-142.160 µs, 3/3 轮`**，其中 C32 `-121.28 µs`、C64+C128 `-18.42 µs`、C256 `-2.15 µs`、C512 `-2.00 µs`）。
  - `small900`：`3.790010 -> 3.683215 ms`（**`-106.795 µs / -2.82%, p99 -92.842 µs, 3/3 轮`**）。
- **最新生产 vs 新版 `v0.0.8 ACO` 三档 3 轮 NMMN 纯 NN 对账**：`original1088` `8.032311 vs 5.628856 ms (+42.70%)`、`small1080` `4.786000 vs 3.377634 ms (+41.70%)`、`small900` `3.674233 vs 2.571150 ms (+42.90%)`。
- **80 模块实物与正式宿主**：双架构 80 模块完整实物保留于 `Development/results/ksearch-round4-20261031/payload/HIP/` 并更新 `hip/SHA256SUMS`；正式 O2 `addon64`/`runtime`/`frame`/`pure`/`final-profile` 与 `runtime-smoke`（`exit=0`）完成。详见 `Development/results/ksearch-round4-20261031/README.md`；临时大文件已清理、`gpu.lock` 已释放，未修改游戏/0.45 ZIP/上游源码，未 push。

## 0.46 · 2026-10-31

以2f19eebc及本次发行文档快照为基础，采用Round5最终双架构80模块和已验证O2宿主。自0.45以来纳入Backfeed刀1的限定形状优化、C32 Phase 5B、K-Search Round1–4的已采纳无损路径，以及Round5仅norm900启用的带精确性保护四路dot2分母归约；失败候选不入包。保持Small 0.5x、MODE/AE和F9默认语义。0 bit-diff指各轮对应基线、已验证输入及配置，不表示跨模式或历史版本任意输出恒等，也未证明性能上限。

离线单帧耗时（不同验证批次范围，非游戏FPS）：Small 0.5x：1080档5.311–5.363ms、900档3.937–3.947ms、720档2.742ms、小窗1.680ms；Original 1x：1088档8.335–8.425ms（纯NN7.984ms）、1152档8.859–8.945ms、960档6.243ms。Round5同批Original960 mean下降25.60µs、p99下降30.55µs，三轮全胜；最终20配置138对文件0 bit-diff。数据不构成所有七档的同批区间保证。

三包与SHA回执：Development/releases/0.46/；双游戏备份安装回执：Development/results/game-install-20261031/。公开网盘尚未上传；0.45下载链接仍对应0.45。
