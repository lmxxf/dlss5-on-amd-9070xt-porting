# a9b13dd4 源码差异：组织、算术与供数

2026-10-23，第一阶段静态研究。源码锁：Mochizuki `a9b13dd4cdb5f4bf43db358492d72e6e5e2acd67`；我方阅读时 HEAD `c24e3a97cdf596d0c997cee7deda91c981d520ce`。本报告未执行 GPU/RTC，未改生产代码、游戏或发行包。完整锁版 tar 已下载，源码及 archive SHA 见 `source-lock.json`，选定管线宏见 `source-pipeline-defines.json`。计时结论由主报告提供；下面只解释实际代码差异和可检验机制。

下文 M 路径相对 `/home/lmxxf/work/dlss5-gap-source-20261023/DLSSNR-AMD-a9b13dd4cdb5f4bf43db358492d72e6e5e2acd67/`；H 路径相对本仓根。M 的固定链接前缀为 `https://github.com/mochizuki0323/DLSSNR-AMD/blob/a9b13dd4cdb5f4bf43db358492d72e6e5e2acd67/`。行号来自该锁版，未套 main 当前内容。

## 先说结论

1. ACO Mode 是 **Linux shader/宏 + 执行计划 + 权重排列 + 离线 ACO 机器码** 的组合。Windows 默认与 ACO 默认并非只换编译器。
2. `TCHAIN` 与“123 dispatches”是两件事。前者让相邻 dispatch 以 tile-counter 代替全局 barrier；真正把小通道多层压成一次派发的是 `NR_PERSIST`（并包含 DS-last / UPS-first）。
3. 我们已经收下 W16、字节流、成对 FP8、短 MODE 段、fmed3。再把这些名称列为“对手秘密”会重复 9–10 月工作。仍有实质差异的是 **N-pair 对 K-pair 的方向、每 wave 的 M/N 复用、更多完整 stage、不同 softmax 舍入/归约以及专门制造 VOPD 配对的源代码**。
4. 本轮已找到优先级更高的路由缺口：`InitCompactContract` / `InitCompactProjection` 限定 `H==1152`，1088 没有吃到全套 ViT compact/W16/byte 路径。先验证扩大到同640-token的1088资格，再研究新内核。其后最容易隔离的新候选是激活 FMA 操作数交换；TCHAIN 必须以新的 C512 边为对象，不能重跑已负的 ViT pair。
5. “AMD 不能像 NVIDIA 那样重叠 WMMA/VALU”即便成立，也只是两家实现共同面对的硬件约束，不能直接解释同一张 9070 XT 上 H 与 M 的差。需要证明 M 在这项约束下少发了哪些动态指令、少等了哪些依赖。

## A. 跨核与持久 stage

### A1. TCHAIN 到底做什么

M `windows/src/core/nr_graph.cpp:157–212,3751–3990,4507–4511`：

- Windows 默认 `NR_TCHAIN_KINDS=0x1`；有 ACO bundle 时默认 `0x1ff`。
- 已实现 pair 包括 C512 `attn→projection→ffwd3→projection→attn`；ViT `expand→contract→QKV`、`attention→projection→next expand`；decoder stage 间与 encoder DS→下一 stage。
- **QKV→全局 attention 仍保留 barrier**，因为 attention 要读全部 token。
- 宿主只有在实际核支持、输入输出 offset、tile geometry 等检查成功后设置 `g_tc_nobar[i]`。mask 不是实际移除 barrier 数。
- `runner.no_barrier_after` 只移除相邻派发间的 barrier，不删除任何一个派发。
- C512 projection 消费端强制 M-major (`remap=1`)：先让同一 token tile 的全部 N 片完成，再推进 token，匹配 producer 完成顺序。

M `linux/shaders/rdna4/include/nr_chain.glsl:71–166`：

- producer 先 release 它的输出，再对该 tile counter 加一；consumer 只等自己所读 tile 的计数达到 `need×epoch`，随后 WG barrier + device acquire。
- counter 不逐帧清零；epoch 来自 frame tick，记录区和 error word 单独维护。
- wait 有 `1<<16` 次预算，超时记错；每个 record 带 magic 防 ABI 错位。
- table/record 放 weight buffer，push 只新增一个 word。源码解释：push 变大使 RADV 无法把参数留 SGPR，会让原本 uniform 参数变内存加载。这个 Vulkan ABI 优化不能原样套 HIP kernarg。
- 上游关于按 dispatch 顺序调度而不会饿死 producer 的论证是其 Vulkan 执行模型假设，**不能直接作为 HIP AnyOrder 队列的保证**。

我方已经有 PDL/SP/AnyOrder 研究。`DevHistory.md` 的 10-12 `vit-tile-ready` 实际证明了 producer 尚未结束时 consumer 已做第一条 MMA，但公平短 pair GPU `70.043→95.770µs`，STOP。新 C512 链如果继续，应先限定一个 **当前新 W16/byte 边**，核查 consumer tile 的真实依赖、release/acquire scope、超时、回绕和重用；不能只因 M 用 TCHAIN 就推断 HIP 有收益。

### A2. 123 的主要来源是完整 stage，不是少做网络

旧锁版实测 161 vs 123 的语义对齐保存在 `../dispatch-sequence-20261006/README.md`：小通道 encoder/decoder stage 合计多 28 次，ViT pack/reverse-gather 多 9 次，其余净多 1 次。10-14 我方在支持的生产资格下消掉 8 次 pack；但本次发现1088被compact资格挡住，**所以153不能直接充当本轮1088实测计数，旧M123也不是新版a9实测序列**。实际派发以本次trace为准。

M 构建 `windows/build/arch/rdna4.sh:9–18` 开启 `NR_PERSIST_DF=1`、`NR_DS_FUSE=15`、`NR_PERSIST_DS_MASK=7`、`NR_PERSIST_UPS_MASK=7`、`NR_REPACK_FOLD=1`。M `nr_graph.cpp:3280–3500` 将 C64/C128/C256 的多层组织为 DS-last / UPS-first 整段。当前 H 的 C256 stage 只包内层 `16..21`、`49..54`，还有 init/run/recover；边界 Body22/Down22、Up48/Body48 不在 SP 内。C64/C128 仍有多次单层派发。

M `fswin_t.comp:1693–1749,2753–3375` 持久实现的实质：

- 工作项为 `(layer,window)`；一个 consumer window 依赖前层至多四个 window。
- 按依赖组织的队列原子领取工作，已领取的 producer 有正在运行的 owner；记录 ready flag 和有界超时。
- **中间值仍写 global tile，再由下一层读**。一 dispatch 不等于寄存器穿越多层，不可用 tensor 分配数推 DRAM bytes。
- coherent/device-scope 访问保缓存可见性；producer 做 release。代价包括原子、轮询、L2/缓存可见性，而非“免费合核”。
- `NR_PERSIST_ONE` 可改为每工作项一个 WG，改变 oldest-first wave 仲裁中的年龄；`nr_graph.cpp:3427–3448` 按窗口数选择，并明确不同尺寸可能反向。H 的固定 worker/loop 结构与其不同。
- M 用 global word 广播 claim，源码说明若新加一 `shared uint`，LDS 按 1KB 分配粒度从 32768→33792，会少驻留一个 WG。这类 **LDS allocation cliff** 比“少几字节”更值得比较。

我方已失败的 Body22→Down、Up48→Body48 单边融合、C64/C128 内层持久化不能改名重开。完整 stage 若再做，必须有新证据解决三点：当前 FAST1 各层数学不同、普通 LLVM23 body 快于旧 COMGR SP body、边界 halo/skip 生命周期。只省两次 launch 而把六层换慢 recipe 会得不偿失。

## B. 算术、激活和 clamp

### B1. 两边都不是统一的“全 exact”或“全 float”

| 区域 | M a9 实际配方 | 当前 H | 可解释的差异 |
|---|---|---|---|
| 小通道 FFN 激活 | `NR_ACT_F32=1, NR_ACT_HORNER=1` | `cw`/`w2` 已有 float FMA 多项式 | H 已收同类减转换，不能列新收益 |
| C512 grouped FFN | `NR_FFWD_F32` 默认 0，发布 `ffwd3` 未开；仍 `nr_swin_act2` half FMA | 要按实际 fast module / helper核；不是小通道相同配方 | M 也保留一些 half 语义，不能称其整网靠“更低精度” |
| Swin exp | 预烘 bias、float FMA→half，packed 位映射；部分层去上限 | H C32 score+bias→float affine/clamp→位截断映射；其他族按各自 helper | 不同舍入/归约，必须另立有损候选 |
| ViT exp | 输入 half、half FMA、half clamp、成对位变换 | float mul/add、float clamp→bit trunc；无假定 halfFMA | 老 score-only 控制已做，单此不足解释毫秒 |
| FP8 饱和 | Linux mode4 通过 `satfn` 模式位；Windows `NR_QUANT_EXPLICIT=1` | C32 已短段 MODE+4成对转换；其他已 fmed3/成对 | 不是所有 M 管线都无 clamp，也不是 H 仍全软件量化 |

M `include/swin_math.glsl:7–10` 原 activation 是 `x * (0.89453125 + t*(0.447265625 - 0.055908203125*abs(t)))`，`t=clamp(x,-4,4)`；**最后乘的是原 x，不是 t**。原 half FMA 中间舍入与 float 多项式不同。不能以代数同式当原版逐位等价。

### B2. 有新意的其实是 VOPD 寄存器配对

M `fswin_t.comp:2376–2445` 的 `NR_ACT_ALT=3` 将相邻分量的两次 FMA 交换乘数顺序，`NR_ACT_PAT` 按分量 index 的位奇偶决定哪些分量采用替代拼法。

其目标不是减少乘加次数，而是改变两个相邻值的 killed operand / 目标寄存器分配，满足 RDNA VOPD 的偶奇目标规则。发布 C32 有 ALT3，若干 prefix/post/persist 还有 PAT2/3/5/7；准确组合见 `source-pipeline-defines.json`。

H `hip/wave_owned_c32.inc:354–390` 的 float 路径全部分量使用同样的 `fma(abs(g),a,b)` / `fma(g,q,c)` 拼法；没有 ALT/PAT 等价开关。**这是可隔离、适合先做 ISA 门的新候选**：交换输入不改累加/舍入，但编译器可能 canonicalize 回同样机器码；必须先看 active kernel 的 `v_dual_fmac/fma/mul`、VGPR 和 wait 变化，再 Gold / 完整 NN ABBA。本轮ISA同事已在H chain看到8条dual_fmaak，说明H也有自然形成的VOPD；目标只能是增量配对改善，不能宣称H完全没双发射。若只是源码不同而ISA同，就立即止。

`NR_ACT_HS=2` 的 hard-swish 近似在相邻代码中存在，但发布 pipeline 没开，不能拿它解释 5.70ms；如果未来试它属于另换数学，不是等价追回。

### B3. 单边 clamp、预烘 bias、packed exp

M `include/swin_math.glsl:80–111` 在权重装载时预计算 `fma(bias,0.044921875,1.30078125)`，运行时只对 score 做一 FMA；这也跳过了原输入 half 舍入，源码明确为近似。结果 half 位图可用 `(packed_bits<<5)+0x7FF88000`，减少 mask/xor/独立两路转换。

M `nr_graph.cpp:1851–1863,4216–4230` 对实际 C32 bias/head scale 证明上限不可达，才选择 `nh` 孪生核。其上界取 `1.2*abs(scale)+0.05` 的保守量，再乘指数系数、加全部位置 bias。不是对任意模型删上限。

H **10-12 已做过同类七层 NoHi**：完整 Gold 过，Frame 仅 -5.269µs，控制漂移 +47.546µs，未证可靠收益，STOP。此处记录 M 做法以解释实现，**不把 NoHi 放进立即重跑清单**。

H C32 当前 float 位图可在 `wave_owned_c32.inc:443–453` 看见；M packed-half 路线若整体搬过来，需把预烘 bias、输入舍入、分母归约一起区分，不能只说“把两 scalar 换 packed 无损”。

### B4. FP8 clamp 并不是漏掉一个 med3 开关

M `include/coopmm.glsl:360–405` 借 NIR 特定 `isinf` 形状匹配 `f2e4m3fn_satfn`，ACO 使用 `MODE.FP16_OVFL`。有限溢出饱和，但 Inf→NaN，并非 NVIDIA 全输入 satfinite 合同。其历史注释记录很多 MODE toggles，是否还存在须看本次实际 .nrp ISA。

H `hip/build-modules.ps1:63–71` 已给 C32 设 `CW_PACK_MODE_MASK=127`；`wave_owned_c32.inc:144–160` 在一个不可拆 asm 块里开 MODE、做四条 FP8 pair 转换、恢复 MODE。其它路径已有 `fmed3f`。这方面应核 **模式切换数量和转换覆盖范围**，不能因为 M 原文称“零 clamp”就主张 H 全局开启 MODE（旧全局 MODE 会改变 half 溢出语义，已有负账）。

### B5. ViT 的大差异是 query 复用与归约组织组合

M `vit_attn.comp:124–169` 以及发布宏：`NR_QT=32, NR_VWAVES=1, NR_KC=64`。每 wave 两个 16-query tile，共享 K/V 读取；full chunk 与 tail 分开编译；K 行置换把指定 key-pair 放进同一 lane，packed half 归约后将概率恢复自然顺序供 AV。见 `include/vit_attn_vt_chunk.glsl:6–34,35–110`。

H 当前 `deep_fast.hip:281–341`：每 wave 16 query，K16 循环，分母用 half operands / FP32 accumulator 的 WMMA，加到自己的固定次序；V 在 AoS 里按 key/channel gathering。两边 QK/AV 总矩阵乘工作量可相同，但 **K/V读取复用、额外分母WMMA、half归约和寄存器压力** 不同。

H 已试过 M32、多种 half-score/half-den/key permutation，见 `failed-experiments.md` 与 `../mochi-vit-score-control-20261006/README.md`。仅把 M exp 改为 H float score 的对照约 +9.36µs、低于漂移，不能给剩余差额分摊因果。若下一阶段再做 ViT，必须以本次实际 ISA 显示的供数重复/资源 cliff 为新依据；不能原样重跑旧 M32。

## C. N-pair 与我方 W16：都是16B，但配对方向不同

M `nr_graph.cpp:3203–3249` `pack_matrix` 按 **两个相邻输出 N16 片** 配对：一个 lane 一次16B得到同一 K16 对两个 N16 的两份8B权重。N-pair 支持一个输入 fragment 服务多个输出 accumulator；ACO C512 attention 单独转 QKV `1536×512`，native Windows attention 则保原布局。选 native shell 时必须同时回权重布局，不能仅换 shader 文件。

H `Development/HIP/packed_weights.h:65–80` `FragmentPackedMatrixW16` 按 **同一 N16 的两个相邻 K16 步** 配对：一个 lane 一次16B得到 K32 的两份 B fragment。`hip_reference_network.h:107–116,362–363,427` 已给 C512/ViT/C32 用它。没有原生 N-pair 同布局。

两者都是 lane 连续16B、整 wave 512B，但有不同取舍：

| 比较 | M N-pair | H K-pair W16 |
|---|---|---|
| 16B 包含 | N0/K0 + N1/K0 | N0/K0 + N0/K1 |
| 直接支持 | 一份 A 同时更新两个 N accumulator | 同一 accumulator 连续两个 K 步 |
| 常见代价 | 更多 N 活跃累加器/更长生命周期 | 对多 N 输出可能重复 A 读取 |
| 可移植性 | HIP 能按纯排列实现，但生产者/消费者须成对变更 | 已生产使用 |
| 数学要求 | 同一输出仍按原 K 顺序累加 | 同左 |

M `gemm1x1.comp:1256–1346` 将 lane base、chunk base、K immediate 分离，双缓冲 fragment 提前一个 K 步发 load；N-pair 每偶数 j 一次读四个 vec4，分给 j/j+1。不能把 `coopMatLoad` 当硬件异步搬运：它最终仍是普通 load/DS/WMMA，只是 compiler 选择 fragment map。

M tiled activation 是 `(token/16,channel/16,token%16,channel%16)`；对齐坐标简化为 `base + token*C + 16*channel`，`vit_attn.comp:66–89` 有明确消除地址运算依据。H W16 权重已经对齐，但 ViT V 的 AoS gathering 与 M tile load 不同；要比较真实 VMEM 指令、cacheline 分散和重排代价，不用名义字节数代替。

M `gemmprojc` 还交换 A/B 得到转置 accumulator；`nr_graph.cpp:3091–3104` 明确仅选 C512，并记录 native Windows 编译器对此资源很差。H 已做过转置 FFN/attention、FB8 C512块内变体（略弱于默认）；因此“转置WMMA”本身不是新候选，**N-pair + 当前 shape + fragment复用** 才是可区别的设计。

## D. 源码层候选优先序（无虚构毫秒预测）

这里按“潜在覆盖范围 / 实施成本 / 是否已有负账”排序；所有收益均待新基线实测，不把作者旧测量当 H 预期值。

| 优先级 | 候选 | 适用门 | 数值属性 | 新条件/停止条件 |
|---|---|---|---|---|
| 0（本轮已验证） | 放开1088的ViT compact contract/projection资格 | `hip_reference_network.h:478,495` 当前只准H1152；1088仍640token | 路由/布局应逐位，实际Gold裁决 | 隔离候选三帧Gold同，161→153；3轮ABBA省0.215711ms，尚未合生产 |
| 1 | C32 激活 ALT/PAT 交换FMA乘数，争取 VOPD | 先仅 active C32 W16 fast 核 | 有限域等数学；Gold裁决 | 新明确源码机制；若 LLVM canonicalize、dual数/资源无改善则止 |
| 2 | C512 QKV/投影 N-pair 对当前 K-pair，按实际 reuse 重排循环 | ISA先查A重复读取与活跃acc | 可保持K次序逐位 | 不是重新“做W16”；先一个热点，不能全家一换 |
| 3 | 分量/片段地址表达式与载入调度，消重复SALU/VALU | 精确PC/loop证据 | 结构等价 | 需当前机器码证明地址重算或load→wait短链；不盲扫调度参数 |
| 4 | 一条 C512 tile-counter 跨核边，M-major消费顺序 | 当前新布局，保安全旧路 | 结构等价但同步需实证 | 与已负ViT pair不同；full-frame非改善立即止 |
| 5 | C64/C128完整DS-last/UPS-first stage | 先证明当前recipe不退化 | 边界数学/skip必须保 | 实现量大；旧内层SP及单边融合负账保留，不能只因少launch开工 |
| 条件研究 | ViT 32-query共享K/V + tile/den组织 | 当前ISA找到实际重复供数后 | 数学与组织需拆开验 | 旧M32/half-den/keyperm多次负，不先凭M默认就重开 |
| 不重跑 | 全局MODE、NoHi七层、原版gather写端折叠、hipGraph、旧Body22/Up48融合 | 已有清晰负账 | — | 无新编译/数学/硬件证据就保持STOP |

## E. 对“为什么比 M 慢”的当前可说范围

本轮刷新见主报告/`statistics.json`：当前1088 H/M同批 `8.533050354 / 7.867127083ms`，差 `0.665923271ms`；独立旧H/新H同批 `8.923111219→8.554250771ms`，追回 `0.368860448ms`，三轮皆正。两组ABBA的H绝对值不相减混用。

源码足以证明 **有差异**，不足以证明每项的时间份额。当前最强的结构事实是 H 多层 stage 组织更碎、H/M 片段复用方向不同、softmax 处理链不同、M 有专门 VOPD 寄存器配对表达式。当前最重要的反证是 H 已经把旧版几个显眼损耗清掉，旧 1.046ms 不能继续原额归因。

下一步若要把原因拆成毫秒，应在锁版 M 同一 shader/数学下分别测 TCHAIN0/1、native shader / ACO shader计划，再做 H 一核一个候选；若换 compiler 同时换 shader/plan 就只能叫组合收益。单纯 `153-123=30` 乘一个launch常数、或把静态VALU数量按峰值除，均不能得到真实差额。
