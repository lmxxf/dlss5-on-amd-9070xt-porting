# DLSS5（DLSSNR）→ AMD RX 9070 XT 移植：开发史

> 本文件是本项目开发、部署、运维与后续工作的**唯一记录入口**。
> **2026-09-23 压缩**：原文 593KB（约 30 万 token）已整本移到 `Development/history/DevHistory-full-20260923.md`（git `a300c20` 之前的完整版）。本文只留结论、关键数字、现行约定和"别再做"的清单；要查某一刀的细节、SHA、日志路径，去原文 grep 日期或关键词，别整本读。
> 更早的逐刀原始记录在 `Development/history/` 其余文件（见文末索引）；每轮实验的数据在 `Development/results/<名字>-<日期>/`、`Development/HIP/experiments/<名字>/`。

**续写规则**：
- 新事件**直接追加在文件最末尾**（§12 流水，时间正序，`## 日期 时间：标题` + 正文），不要往 §4 的表里塞，也不要插到中间。
- 「§3 当前状态」「§11 待办」原地改；新的"不要重做"补进 §7，新教训补进 §8。
- §12 长到读不动时：每天压成 §4 的一行，结论并入 §5～§8，原文照样挪进 history/。

---

## 1. 目标与硬约束

把 NVIDIA 泄露样本 `nvngx_dlssnr.dll`（DLSS 5 神经渲染，内部名 DLSSNR）里的 71 块网络恢复出来，在 RX 9070 XT 上按原版数值执行，并在真实游戏里以可玩帧率替换 FSR 输出。

- 验收三级：离线复现 → AMD 跑通 → 游戏可用。后来 Zero 收紧为"9070 XT 走完 71 块出最终 RGB"，再到 1080p ≥10fps，再到 30fps。
- **exact 链**（tag 0.01，186ms）逐值等于原版，冻结当裁判；**fast / HIP 链**：结构改动必须逐位不变，算术改动看 PSNR，画面由 Zero 在游戏里按 F6 拍板。
- 帧率只信游戏内读数或同批 ABBA；绝对帧时跨批次留 ±0.3ms。

| 机器 | 用途 |
|---|---|
| `desktop-2026`（`ssh amd9070`），Win11 Insider 26H2，RX 9070 XT（RDNA4，gfx1201，16GB，128 SIMD / 32 WGP） | 靶机 |
| `NucBox_EVO-T1`（`ssh rtx5090`），RTX 5090 OCuLink | 标准答案机（原版 DLSSNR） |
| DGX Spark `spark-3a10`（GB10，sm_121） | 能直接加载 sm_120 CUBIN 当 oracle |

驱动：DX12 路线需要**预览驱动 32.0.31007.2048**（SM 6.10 / LinAlg，私有 Agility 721、开发人员模式）。HIP 路线（0.20 起）只需驱动自带 `amdhip64_7.dll`，不要预览接口；正式驱动有用户反馈可用。

---

## 2. 逆向结论（硬事实，不会再变）

**样本**：`nvngx_dlssnr.dll` 310.8.0.0，SHA-256 `e16bcf15…1fc8e`。资源 `WEIGHTS_HT` 147,695,410 字节，153 条记录（`name_length→name→body_span→body`），live 层对象 152 个（block70 的 blend_scale 与 layer 同属一个 Layer）。矩阵主体是 packed E4M3，偏置/skip/scale 是 FP16/f32；运行时 arena 按 512 字节对齐，共 147,719,680 字节，运行时不改写权重。DLL 内含 15 个 sm_120 CUBIN。

**网络**（构图函数 `0x180039780`，config `hnet-vigilant-squid` / variant `crazy-cuckoo`）：
```
0        PreBlock 1H / C32          1–3 Swin C32   4 ds 32→64
5–7      Swin 2H / C64              8 ds 64→128
9–13     Swin 4H / C128             14 ds 128→256
15–21    Swin 8H / C256             22 ds 256→512
23–30    split-Swin 16H / C512（30 带 ProjPool + FinalHead 512→1024）
31–38    ViT 1D / C1024（Expand→Contract→QKV→Attention→Projection）
39       DecInputUpsample 1024→512  40–47 split-Swin C512
48/56/62/66 upsample + Swin（C256/C128/C64/C32），49–55 / 57–61 / 63–65 / 67–69 同族
70       PostBlock C32 + RGB 头（blend_scale=0.73974609375）
```
跳接：`39←38+30`、`48←47+22`、`56←55+14`、`62←61+8`、`66←65+4`、`70←69+0`。

**1080p 几何**：处理区 1920×1152（底部 72 行镜像，`2*extent-coord-2`），ViT 20×32 = 640 token（有效 18×30）；post shift 3。decoder 移位 40–55 = 0/3/1/2 循环，56–61 = 1/2/0/3…，62–69 = 0/3/1/2（`native_runtime_shifts.h` 唯一）。900 档是我们自定的几何：1600×900 有效、**1600×960 处理**（09-17 定，原 1024 行版保留为 `900w`），ViT 25×16，第 16 行是零 token。

**NGX 输入**：Color / Output（1080p RGBA16F）/ Depth / MVec；history（slot8）= 上一帧网络输出，motion 在 slot10；`input_scale=1/32`，`rgb_mode=1`。

**算术**：
- FFN 激活 `x=clamp(x,-4,4); y=x*(0.89453125+x*(0.447265625-0.055908203125*|x|))`（half 多项式，不是 GELU）。
- 残差 skip-first：`H(input*skip)` 作为初始累加器，之后每 K32 做一次 half 舍入；FP8 是 E4M3 SATFINITE、RNE，次正规尾数允许进位到 8。
- 注意力：每 head 32 维，Q/K 归一化用半精度平方和，归约顺序固定；softmax 的 exp 是 half 仿射加移位的位映射（C32、ViT 系数不同）；分母求和树是固定的 key 顺序，不是平衡树。
- C64 三矩阵 FFN：64→256（分组扩展）→64（分组收缩，**每 32 输出通道只连 128 hidden，其余全零**）→64（混合）；C512 split FFN 是 512 混合→8 组 64→256→64。
- 输入混合按 WMMA 规则：两操作数指数和的最大值对齐，按 `2^(E-27)` 朝零截断后精确求和。随机场是 Box–Muller，用 MUFU 近似。
- 时序采样：UV 定点 21 位，五点十字核，MUFU.RCP 归一化。
- post70：输出合同是 `Color + 神经残差`；RGB 头两个 K16 的 27 位对齐整数规则。
- 原版 FP16 输出 surface 是**朝零截断**；AMD 预览驱动的 `f32tof16` 也是朝零截断。

---

## 3. 当前状态（2026-09-26）

**装机与发布**：《剑星》09-26 装 wave-owned（c32-wave1/c64-wave2，`DLSS5_HIP_WAVE_OWNED=1`）+ 06:43 auto-tier add-on f71f38a3（flags `NETWORK_HEIGHT=auto`，2K质量档自动走900）；2077仍prod8 / 插件0211a78a；0.30三包已发布，夸克与Google链接已填，清单 `Development/tools/release-030-results.json`。剑星900P→2K简单场景60～61fps，1080P→2K 47～48；2077质量41、平衡51～52（用户远程读数）。RE9机上为prod7内核。部署/备份详见末尾流水。

**地平线6**：0.30前置被同列表后续draw拒绝，手动 `DLSS5_PRE_UPSCALE=0` 后置可用，记录约44fps；首次黑屏未复现，dump脚本已备。下一版计划自动回落。

**黄金 hash**（HIP 900 档正确性回归）：900w 三道 `FEEA9EF3…`（40 帧）/ `22C171FC…`（每 8 帧 reset）/ `75B62D2F…`（seed123 history）；960 三道（900 漏派发修复后）`047c36e1…` / `b4f66e9d…` / `0e4afd83…`。HLSL 参考 `C7C2F49D…`。脚本 `validate-modules.ps1`、`validate-modules-960.ps1`。

**性能口径**：prod8完整NativeGameFrame旧回放约900 12ms / 1080 17ms；本轮纯HIP固定输入约11.7/16.7ms，不能跨口径直接比较。

**研究**：post输入三候选与ViT编号六候选均无稳定收益。新C32阶段账已完成，FFN主体和输入准备各约四分之一（插桩wave周期口径）。对角残差删零候选长槽约−0.026/−0.015ms，双架构编译和96帧候选RGB回归通过，未装机/未改发布版，详见 `results/c32-diag-zero-20260925`。

**其他待选**：非逐位6b约−1.1%、PSNR58dB，等Zero看动态画质；C256宽权重片段约−0.03ms待顺带（不是已启用的普通FRAG256路径）。下一研究入口 `WorkingPlan.md`。

---

## 4. 时间线（里程碑）

| 日期 | 事件 | 关键数字 |
|---|---|---|
| 08-31 | 权重解析、71 块构图恢复、5090 跑通原版、AMD 上传 arena | 153 条记录 |
| 09-01 | block0 出图、SASS 解出算术；row-major 假设被反证（矩阵按 tensor-core tile 排列） | |
| 09-02～03 | 在 5090 游戏 backend 里抓 live 中间层，用"下一层当裁判"逐段前移 | block70 RGB corr 0.945 |
| 09-04 | 动态游戏链 484s→10.6s；DirectML；1080p 单 DLL 进游戏 | 11.98fps |
| 09-05 | 像素审计翻案（此前根本没提交），8×8 网格 → 停追 FPS，改做 native 正确性 | |
| 09-06 | 原生逐值链 RGB512→0–70→RGB 全 exact；实机几何 640 token | 786,432 值 diff 0 |
| 09-07 | valid1080 整网 exact、时序 exact、进游戏出正确画面；闇夜战 4.0s→1.47s | 2fps |
| 09-08 | 闇→0.47s；光→0.186s（**tag 0.01 exact 终点**）；fast 链到 62.7ms（0.03） | 5→15fps |
| 09-09 | 33ms，GPU 饱和；显存之争（6.8→3.75GB）；闪烁靠输出平滑；0.04～0.06 | 29fps |
| 09-10 | fast38 34.0ms；0.07 包；浪人崛起 XeSS 出图；发现机器降频态（重启后 24.4ms） | 35fps |
| 09-11 | Magpie 路线打通；黑块根因 = 硬件 E4M3 Cast 不饱和 → NaN；全仓 43 处加饱和（0.10）；接管时间 22s→3.4s（0.11） | |
| 09-12 | 屏幕提示层（0.12）、FPS 显示 + XeSS FG（0.13）、0.14；C512 FFWD 换 FP8 无收益，"项目收尾" | Magpie 28→55 显示帧 |
| 09-13 | 小窗口 FIT_INPUT（0.15）；720 / 900 分支 | |
| 09-14 | **HIP 分支**：comgr 无 SDK 编译、D3D12↔HIP 桥接、512/900 逐位对上 oracle | |
| 09-15 | HIP 生产快路径 51→25ms；异步重绑竞态修复；读回节奏污染 HLSL 基准被识别 | 游戏 17→30fps |
| 09-16 | MH 分组收缩零结构、各家族 attention+投影融合、ViT 融合；"dup / 跳块"帧内成本法；权重预打包四连 | HIP 18.07 |
| 09-17 凌晨 | 读 `.s` 当 profiler：F() 去分支 + 16 load 连发（−0.68）、prefix 内联翻盘等 → **HIP 反超 HLSL** | 16.9→15.5ms，游戏 47→52fps |
| 09-17 | **0.20**（HIP，免预览驱动）；生产内核搬进 `hip/`；auto 选档（0.21）；900→960 行（0.22） | 900P 52～54 / 1080P 37～38 |
| 09-18 | 多 GPU 主机修复（0.23）；黑神话的钩子三道坎（未验通，已复原） | |
| 09-19 | OptiScaler 前置链（0.24）；主城掉帧修复；C32 寄存器复用 / 有界倒数；**900 解码尾部漏派发修复**；gfx1200 + gfx1201 双构建（0.25）；C256 frag；LOP 黑屏 = 打包漏 R11 shader（0.26） | |
| 09-20 | RE9 后置专用版（0.26.1）；从 AttExp 选入精确流式 attention / R3 自适应复用（默认关）；0.27 | 900P 56～57 |
| 09-21 | **算力缺口研究立项**（主线，见 §6）；C128/C256 零填充快路径、固定尺寸 ViT/decoder（−1.2/−1.7%） | |
| 09-22 | TheAutomatic PR5 设计独立实现分阶段桥接 → RE9 真正超分前接入 + 曝光修复（0.28、0.28.1）；合并 PR #7/#8；栅栏 local 化 + C32 CU 模式 | 58～59fps |
| 09-22 夜～09-23 | prod3～prod6：折叠 FFN、字节链、向量化 staging、mh 寄存器化、in16 别名、尾段转置 | 相对 prod2 −4.3/−4.5% |
| 09-23 | prod6 装机；DLSS5_FIT_LARGE（issue #6，>1080 输入降到 1080 层）剑星 + RE9 验通；**0.29 三包** | ~60fps；网友最低画质 73 |

---

## 5. 性能演进要点

### DX12 fast 链（09-08～09-12，186→约 24ms）
收益全部来自数据搬运：寄存器分块复用、FP8 格点上的中间量改用 f16 存、权重常驻 DEFAULT 堆、删 LDS 转存和 barrier、合批。**这台卡的核成本几乎全在逐元素标量尾巴**（软件 H/F、散写），不在带宽也不在矩阵乘。逐刀表见原文 §3「fast 链每刀收益表」。

### HIP 链（09-14～09-23，离线 900 档 51→12.1ms，全部逐位一致）
有效的几类刀，按类别记：
- **核融合**：各家族 FFN+QKV、attention+投影（C64/C128/C256）、prefix 进 block0、post RGB 头 / finish / 下采样进 C32 尾部。
- **权重预打包**：half / E4M3 / fragment 布局，一次 memcpy 取 B 片段；C512 QKV、ViT QKV / proj / contract、pool、decoder。
- **ISA 病灶**：F()/q8 的分支饱和改 fmin/fmax + cndmask；串行 load→wait 改连发；scale 读从循环里提出来。
- **结构零**：MH 收缩只遍历本组 128 K；C128/C256 全零 padding tile 直接写零。
- **固定尺寸**：编译期常量解锁展开与读取调度（ViT expand 一处 −26～36%）。
- **同步 / 驻留**：栅栏限定 LDS 地址空间（去掉 `global_inv`）、C32 用 CU 模式、in16 别名到 Scratch。
- **WMMA 操作数对调**（A/B 寄存器格式相同，对调得到 D^T）：折叠 FFN 让 hidden 不进 LDS、mh 的 ex/prob 留寄存器、ffn_fused 尾段转置。
- **请求合并**：C32 staging 把 16 条行读并成 b128（字节链 + 向量化）。

---

## 6. 算力缺口研究（318 期主线，09-21 起）

**问题**：9070 XT 独立程序实测 FP8 405T / FP16 204T / FP32 49.9T（与标称一致，时钟约 3.1GHz）；网络主矩阵有效吞吐只有约 55T。**判据是解释缺口，不是零碎提速**；负结果能排除原因也算数。总汇总 `results/network-cost-summary-20260921/README.md`，公众号稿 318.md。

已建立的认识：
1. **整网份额**（900/1080，稀疏事件 + 区段 ABBA）：C32 约 35%，C64 / C128 / C256 / C512 / ViT 各约 11～13%。局部机制证据**不能相加成整网解释百分比**。
2. **ViT 同 FLOPs 不同耗时**：expand/contract 的差异主要来自编译调度组织（固定尺寸解锁展开）；禁止 contract 展开慢 175%。
3. **计算密度探针**：保持读取量不变、只加寄存器内 WMMA，吞吐 150→299T，说明 150T 不是硬件上限，是供数和指令组织在限制。
4. **供数**：B 的工作集吞吐不单调；B tile 间距 +256B 后 span64 48→22μs；页内翻 bit10 同样有效 → 对地址低位敏感。真实 ViT 核在 RGP 里 memory stalled 68%（只代表那个热核状态）。
5. **规则**：读写成本 ≈ 指令数 + 触及的缓存行数，两项都算；lane 间仍连续时并宽才有效。排队按请求数算，不按字节数。
6. **分组 / 编号映射**：wave 总数固定时，组大小 1→2 或重排 logical_wave 编号都会慢 45%；HIP 驻留上限相同，原因未定。
7. **C32 周期账**（核内时间戳）：staging 31%、FFN 26%、QKV 11%、注意力 12%、投影 8%、尾部 9%、barrier 9%；矩阵只占 post 核约 15%。驻留贴着 LDS 上限。
8. **驻留**只在它是瓶颈时才值钱：VGPR 封到 96 反而慢；c256_attention 每个 launch 的尾巴 25～37% 是结构税。

仍未解：ViT 剩余约 50% 参考算力的具体限制因素；C32 余下部分的归因。

---

## 7. 已否定 / 不要重做（除非瓶颈变了，按"旧 null 要重测"原则说明理由再测）

- **占用率捷径**：C32 no-unroll（+0.64→+1.0 更慢）、waves_per_eu 提示（编译器不理）、LDS_SLIM、ffn_fused VGPR 封 96。
- **LDS 凑片**：C32 LDS_VECTOR（测了四次都是 null）、C32 / MH 的 V 转置、概率 DWORD 布局、AV 共读。
- **字节 / half 流**：MH 完整字节流当时慢（09-19 修漏派发后重测才通过，已进 0.25）；ViT byte/half stream、N2/N4 / M2 / M4、ViT FFN 融合（并行度不够）。
- **小通道 FFN 换布局**：C64/C128 tiled 或 frag（+0.3～0.4 更慢）。
- **C512**：mix 并进 FFN、FP8 展开（有逐位反例，只能收缩用 FP8）、attention+投影融合。
- **各种 hipGraph**：当前 GPU 时间把 CPU 提交全盖住了，收益为零。
- **C32 转置尾部并宽**（09-24：逐位同，慢 0.03ms；C32 卡读队列，写并宽不是瓶颈，转置反而让残差/缩放读变差）。
- **其他**：DX12 overlap（与游戏并发是双输）；VMM 稀疏映射（驱动只认"保留区 == 一个完整物理块"，封死）；buffer_load 32 位地址（正确，常量必须是 `0x31004000`，但无收益）；噪声缓存；多遍 NR（5090 上也不值）；C32 注意力寄存器化（压力抵消收益）；注意力投影输出转置（滚动循环别转置）。

---

## 8. 工程教训（合并版）

**测量**
- 只信同批交错 ABBA，别跨批比较绝对值；GPU 会卡在降频态好几天，量之前先跑基线。
- 逐核 HIP event 在这套驱动上不可靠，会出现负值，全插反而把帧时拉长 80%；短段事件也不能当依据。要么用 wall 加重复放大，要么用 dup / 跳块法（注意 HLSL 跳块带拷贝，会压低 HLSL 那边的家族成本）。
- 读回节奏会改变 GPU 频率：HLSL 全读回 26ms，只读首尾 16.7ms。性能测试一律只读首尾，正确性另做全帧检查。
- 单核隔离会把工作集留在缓存里，不能直接相加当整帧。
- `DLSS5_GAME_PROBE` 每帧 Flush，会破坏异步提交时序（地面变透明），不要和 ASYNC_SUBMIT 同开。
- 测帧率别开 Splashtop；先确认游戏有没有锁帧（剑星曾锁 30）。
- **游戏内帧率标准测试**（09-26 Zero 定）：《剑星》**1080P 窗口 + FSR 原生 AA**（渲染 1920×1080），**主菜单**读数，最简场景为辅。离 60 上限远、无缩放策略差异。flags 须 `NETWORK_HEIGHT=auto` 或 `1080`（固定 900 会把 1920 宽缩小）。基准：wave-owned 与 Daniel 0.4.0 均为主菜单 47～48 / 最简场景 51～52。 **按 F8 切 EXACT 再读**（剑星 flags 默认 `DLSS5_VIT_ADAPTIVE=1`，静止主菜单会复用 ViT 白赚约 3 帧；与 Daniel 对比必须 EXACT）。
- 派生测试脚本后先 grep runner 名；对照组没变化先怀疑脚本（09-16 两个假 null 就是这么来的）。
- RGP 捕获会改时钟和输出；RGP 里的 HLSL ELF 按占位哈希缓存，要先用 `dxilhash.py` 签名。

**数值**
- HLSL `round()` 是 RNE；`f32tof16` / D3D 驱动输出 surface 是朝零截断。
- FMA 收缩和上下文相关，凡要逐位的尾链两边都显式 `precise`。
- 单点候选能修一个反例，全幅上反而可能更差，别拿单点匹配去推广舍入规则。
- 单层高相关不等于多层稳定，必须做完整累计门。
- 硬件 E4M3 Cast 不饱和，所有无界值转换前都要 clamp ±448。

**GPU / D3D12 / HIP**
- 2 的幂行步长会让内存通道撞车；单 command list 塞整网会 DEVICE_HUNG，必须分块提交加 fence。
- D3D12 单轴 group 上限 65535；视图格式变量别复用（会建出 UNKNOWN 视图，device removed）；宿主贴图只能拷贝，不能建 UAV。
- ReShade immediate list 在 `_has_commands=false` 时直接 return，原生录的命令可能根本没提交。
- 派发网格按 token 和通道分别分块（900 档漏掉 4 个通道 tile 就是没这么做）。
- COMGR 确定性：同一文本两次编译字节一致，源码一变只改 `__hip_cuid_*`。校验标准 = 三道 hash + 去掉 cuid 后的 `.s` 对比。

**方法**
- 核内延迟问题先读 `.s`（数分支、数 load→wait、看重复 load），别凭结构直觉盲改。
- 跟 HLSL 并排逐段对"做同一件事但做法不同"的段。
- 旧 null 要重测：一刀的收益取决于当时的瓶颈。
- 静态指令数少不等于快；少 barrier 不等于快；少字节不等于快。

**运维**
- 游戏或 Magpie 开着时绝不换 DLL / HSACO，也不跑测试台。
- Windows Update 和 AMD Install Manager 会偷换驱动（已设 `ExcludeWUDriversInQualityUpdate=1`，计划任务已禁用）。
- 打包不能继承旧资产目录，要从仓库模板和当前源同步；配置唯一来源是 `scripts/*flags*.txt`（见 `scripts/CONFIGURATION.md`）。
- 远端 PowerShell 一律用 `-File`，别在 `-Command` 里 type 自己的输出文件（曾涨到 4.8GB）。
- commit 不加 Co-Authored-By；每做完一刀给 Zero 一句进度；时间只信 hook 时间戳。

---

## 9. 运维 / 构建 / 部署入口

- **目录**：生产宿主 `src/`、生产内核 `hip/`（`build-modules.ps1` 24 行配方，默认双架构，`SHA256SUMS`）、DX12 shader `shaders/`、打包与配置 `scripts/`；实验在 `Development/HIP/experiments/`，结果在 `Development/results/`，RE9 前置宿主在 `Development/RE9/presr/`（只入库版本锁定 + 补丁 + 脚本）。
- **AMD 机**：实验根 `D:\DLSSNR-Lab`（`hip-backend\` 放模块目录和 benchmark）；《剑星》在 `C:\Program Files (x86)\Steam\steamapps\common\StellarBlade\SB\Binaries\Win64`；成品在 `D:\給網友打包`。5090 的《剑星》在 `D:\SteamLibrary\…\Win64`。
- **构建**：`scripts/build-addon.sh --hip`（DX12 用 `--tiled`）；`hip/build-modules.ps1`。
- **部署**：每轮一个 `Development/deployments/<名字>/` 或 `D:\DLSSNR-Lab\stellar-<名字>\install.ps1`，流程是源 hash 校验 → 备份 → 替换 → 读回，并确认宿主 / INI / flags 前后不变，失败回滚。
- **打包**：`Development/tools/package-0xx.ps1`，底包逐文件核对、44 个 shader 变体编译、ZIP 读回校验，flags 从仓库模板复制。
- **远程 UI**：交互计划任务 `dlss5game` / `dlss5magpie` / `dlss5toggle` / `dlss5shot` / `dlss5rgp` / `dlss5toolbar` / `dlss5profiler`；Magpie 热键 **Alt+Shift+A**。
- **HIP 选项**：默认组合在 `src/native_hip_network.h` 的 HIP_FAST；诊断开关（dup / skip / memory / span probe）全部默认关。

---

## 10. 游戏适配要点

- **《剑星》**：FFX `ffxDispatch` 钩子（ReShade addon）；现走 OptiScaler 0.9.4 前置链，必须 `Dx12Upscaler=fsr31`（写 `ffx` 会静默落到 FSR2.1）；`GameUserSettings.ini` 里 AA=OFF 时 FSR 根本不创建。
- **Magpie**：输入是 8 位 sRGB 成品图，需要 `CODEC_SRGB=1`；光流在暗部会出垃圾向量，是已知限制（止血用 `MOTION_MAX_PX=64`）；包内带 XeSS FG ZeroMV。
- **浪人崛起**：XeSS 路径，运动向量符号为 −1。
- **匹诺曹的谎言**：R11G11B10 输出，需要 R11 写回 shader（0.26 修）。
- **RE9**：同一列表后面还有游戏自己的 draw（51～53 次），普通前置被安全检查拒绝。走 TheAutomatic PR5 设计的分阶段桥接 + GPL 命令列表代理，在超分前接入；曝光纹理必须传入（否则褪色）；尺寸越界要先检查、失败要回滚（0.28.1）。Xbox 版《鬼武者》用 0.26.1 后置包可用。
- **黑神话**：FSR3 静态链进 exe，未验通，已复原。
- **FIT_INPUT / FIT_LARGE**：≤1080 的小窗口 letterbox；>1080 的输入降采样到 1080 层，按亮度比调制原图。

---

## 11. 待办

- README 补三点：默认跳过 42/43/46 三块（40.66dB，清空 `DLSS5_SKIP_BLOCKS` 即全跑，约 +1ms）；ViT 自适应复用默认关、开了有损；精确流式注意力常开且逐位。
- 0.30已发布、双网盘链接已填；issue #6回复状态另核。
- 6b（wave 内归一化）画质等 Zero 看。
- 算力缺口主线：ViT 剩余缺口的限制因素、C32 余下部分的归因。
- 9060 / XT（gfx1200）至今只做了编译验证，没有实机测过。

---

## 附：history/ 文件索引（流水在本表之后）

| 文件 | 内容 |
|---|---|
| `DevHistory-full-20260923.md` | **本文压缩前的完整版**（08-31～09-23 每一刀的数字、SHA、日志路径） |
| `porting-worklog.md` | 08-31～09-08 逐块移植流水（6276 行；第 2976 行之后倒序） |
| `reverse-engineering-notes.md` | 逆向结论原始记录 |
| `CURRENT-STATE.md` | 09-07～09-10 fast 链逐刀状态（最新在上） |
| `amd-port-plan.md` / `fast-path-plan.md` / `next-steps-plan*.md` / `PLAN.md` | 各阶段计划 |
| `README.md`、`native-runtime-contract.md`、`local-patch-tool.md`、`optiscaler-intro.md` | 早期索引 / 已过时的方案 |

---

## 12. 流水（新事件追加在本节末尾，时间正序）

## 2026-09-23 16:00：DevHistory 压缩

原文 593KB（约 30 万 token，新 session 读不动）按主题重组为本文件（约 22KB）：逆向事实、当前状态、里程碑、性能演进、算力缺口认识、不要重做清单、合并教训、运维入口、游戏适配、待办。完整原文 `git mv` 到 `Development/history/DevHistory-full-20260923.md`（提交 e0824bf）。续写规则改为新事件只追加在文件最末尾。

## 2026-09-23 23:00～09-24 00:05：《卧龙 2》Alpha Demo 试装（网友反馈"用不了"）

游戏在 9070 机 `C:\Program Files (x86)\Steam\steamapps\common\Wo Long 2 Wings of Ember Alpha Demo`（Katana 引擎，Streamline 2.9 接 DLSS，自带 FSR）。装 0.29 常规 OptiScaler 包（脚本 `Development/deployments/wolong2-20260923/install.ps1`，覆盖游戏自带 libxess/libxell 时备份到 `_dlss5_backup`），逐层排查：

1. ReShade 菜单出、无 DLSS5：游戏选 DLSS 后 OptiScaler 建了 fsr31 特征，每帧 Evaluate/Dispatch 都在跑，但 addon 钩的 `amd_fidelityfx_dx12.dll::ffxDispatch` 一次没触发。改钩 `amd_fidelityfx_upscaler_dx12.dll`（26KB 的 dx12.dll 只是转发壳）仍无触发。
2. 根因（读 OptiScaler 源码 FfxApi_Proxy.h）：游戏自己先加载了 upscaler dll，OptiScaler 把它当"游戏加载的 FFX 模块"用 Detours 钩其五个导出做输入捕获，自己的派发走 Detours 跳板，不经导出入口。`[Inputs] EnableFfxInputs=false` 后钩子每帧触发，网络初始化成功（1580×888 → 900 档）。
3. 常规前置随即被自家检查拒绝：`UNSAFE: draw/dispatch after deferred upscaler in same list`——和 RE9 同类，FSR 之后同列表还有 draw。
4. 换 RE9 宿主（`-Variant re9`，不装 REFramework dinput8.dll）：`PrepareFrame: render input metadata/texture size mismatch`（游戏开着动态分辨率，纹理按最大分配）+ 宿主 300ms/2 帧稳定判定永远过不了。关掉游戏内动态分辨率后两者消失（1664×935 = 1664×935）。
5. 之后神经路径生效：画面变灰、帧率不变（60）。日志：曝光扫描 >64 个候选（RE9 定制的过滤在 Katana 上失效，曝光未识别，FP16 线性场景色按曝光 1 归一化）；`prior job not yet submitted; original SR` 反复出现（Submitted 钩子对队列/时机的假设是 RE9 的，卧龙提交路径不同，多数帧绕过）。

结论：卧龙 2 不是"装不上"，是三层都要适配：EnableFfxInputs、动态分辨率关、以及 RE9 宿主的曝光发现 + 提交观察两处 Katana 化。当前机上装的是 RE9 变体 + `LmxxfDiagnostic=off`。代码改动：`src/native_submission_order_probe.cpp` 优先钩 upscaler dll 并在 hook_status 行记模块名/导出地址、派发入口前 8 次记类型（剑星回归未做，未发包）。未记入 0.29。

## 2026-09-24 00:30：C32 转置尾部（out 并宽）null

WorkingPlan 的"C32 产出端并宽"试完：FFN 收缩 + prefix + 投影全部转置，ffn8/scratch.ex/out/pre16 的窄写并宽。逐位同，但 1080/900 六组配对全部慢 0.03ms（0.2～0.3%）。原因：残差初始化和缩放访问改成 lane=行后 LDS 读变差，投影缩放 2→16 读，chain/finish VGPR +15。开关 `HIP_C32_TRANSPOSED_TAIL` 留 0，进"不要重做"。详见 `results/c32-transposed-tail-20260924`。

## 2026-09-24 01:00：探索 1 关闭——RDNA4 没有直达 LDS 的读取

318 账本里 staging 31% 想用 `global_load_lds`（显存直落共享内存）省掉寄存器往返和 LDS 写指令。comgr 报 `__builtin_amdgcn_global_load_lds` 需要 target feature `vmem-to-lds-load-insts`，gfx1201 没有；汇编器也不认 `global_load_lds_b128`。RDNA4 上这条指令不存在（CDNA/gfx9 有），硬件没铺路。探索 1 关，改看频率账本（探索 3，`Development/HIP/experiments/clock-ledger`）。

## 2026-09-24 01:05：频率账本——功耗墙，FFN 最费电，ViT 最省电

探索 3 做完（`results/clock-ledger-20260924`）：核族 dup×8 占满一帧逐族量时钟功耗。板功耗每个配置都钉在 325～328 W，时钟随负载变：C64～C256 FFN 占满时 2.51～2.55 GHz（比基线低 8～9%），C32 低 1～1.5%，C512/C256 注意力持平，ViT 反而高 1～2%（2.78～2.85）。整网 2.75 是加权结果，峰值测试 3.1 是纯寄存器矩阵省电。318"ViT 单测 2.5 GHz"是孤立微基准的读数，要改口。新杠杆：让 FFN 核族省电能抬全帧时钟，上界 1～2%；用户侧功耗上限 +10% 值得实测。dup 开关在 flags 文件里不在环境变量（第一遍白跑，当基线重复性用了）。

## 2026-09-24 01:15：FFN 核族的电烧在全局窄写；并宽指令不省电，减少触及行数才省

FFN 占满一帧换 mh_fast 模块看时钟（`results/clock-ledger-20260924/ffn-tail`、`ffn-stores`）：串行求和、LDS 交换 + barrier 都不耗电；去掉 norm 全局写 +4% 时钟，norm 和 out 都去 +7%。prod6 把 norm 写 24 条 b8 并成 3 条 b64，时间 −2.6ms 但时钟不动——转置后每条写仍触及 16 行，功耗跟触及行数走。下一把刀（逐位）：out 从已有的 qfeature LDS 暂存整行写出，norm 按 part 暂存 4 KB 后整行写出，每行只写一次；预期 FFN 占满时钟 +7%、整帧 1～2%。

## 2026-09-24 01:30：mh_fast 全行写（HIP_FFN_LINE_STORES）——逐位，−0.6/−0.7%，prod7 候选回归通过

功耗账本指出的那把刀做完：out 从 qfeature LDS 暂存整行写出，norm 三个 part 暂存后整行写出（多一个 barrier，LDS 每组 +8.4 KB，驻留仍由 VGPR 定）。模块集 ABBA 三孪生逐位全 0，1080 −0.10/−0.10/−0.08ms，900 −0.08/−0.09/−0.09ms；FFN 占满一帧时钟 +1.2%（2526→2554 MHz）。写出字节没变所以没到"去掉写"的 +7%，省的是部分写的开销。prod7 候选（只换 mh_fast 两架构）回归：12 帧 RGB 哈希 2 序列 × 2 档全部与 prod2 基线一致，四组额外对照一致；1000 帧计时 900 12.01/12.03、1080 16.91/16.92（基线 prod2 12.62/12.72、17.80/17.88）。`results/mhfast-line-stores-20260924`、`deployments/stellar-prod7-20260924`。未装机，等用户关游戏。

## 2026-09-24 07:13：prod7 装进剑星

用户确认游戏关闭后 `stellar-prod7-20260924\install.ps1`：2 个 mh_fast 模块（gfx1200/gfx1201）哈希核对后替换，宿主/INI/flags 未动，备份 `D:\DLSSNR-Lab\stellar-prod7-20260924\backups\20260924-071340`（`-RestoreBackup` 回滚）。等用户实玩反馈。

## 2026-09-24 07:19：用户反馈 prod7

《剑星》900P 中画质拉伸 2K，常测场景稳定 60 帧（prod6 时"最快接近 60"）。prod7 通过实玩，进 0.30。

## 2026-09-24 07:30～18:30：《赛博朋克 2077》2.31 试装——三层坑，最后一层是别名瞬态资源

常规 OptiScaler 0.29 包装进 `bin\x64`（`deployments/cyberpunk-20260924/install.ps1`，覆盖游戏自带的 5 个 xess/ffx dll，备份 `_dlss5_backup`）。网友"用不了"逐层：
1. 和卧龙一样：游戏自带 FSR dll，OptiScaler 走 Detours 跳板 → `[Inputs] EnableFfxInputs=false` + 优先钩 upscaler dll 的 addon。
2. `terminal resource state not representable by FFX`：Reverse() 只认 8 种状态。补齐深度态和任意只读组合态，并把撞到的状态值写进日志（`src/native_pre_upscale.h`）。
3. **网络跑起来但画面只有色调变化。** dump 网络输入发现每帧都是同一幅"天空 + 灰地"的静止环境（第 600 帧和第 3000 帧统计四位全同，字节不同——云在动），而用户看的是街景。根因：`DLSS5_PRE_UPSCALE_ASYNC=1` 的延后提交让我们拷贝颜色纹理的命令排到游戏下一帧的早期通道之后，REDengine 的颜色缓冲是瞬态别名资源，那时那块显存装的是天空探针。剑星的颜色纹理持久，赛跑读到上一帧也看不出。**`DLSS5_PRE_UPSCALE_ASYNC=0` 后 dump 是真场景**（min 0.04 / 中位 0.17 / p99 0.64 / max 9.8，纸白 1 正好）。
中途把纸白猜成 8 是错的（那是天空探针的量级），已改回 1；顺手加了 `DLSS5_PAPER_WHITE`（Record 的门从 {0.5,1,2} 放宽到有限正数）和 `DLSS5_DUMP_FRAME`（每帧路径第 n 帧 dump 游戏颜色输入，配 DLSS5_DEBUG_DUMPS）。dump 统计脚本在 /tmp/f16stats.py 一类的临时件，结论在此。用户实机确认待做（ASYNC=0 + 纸白 1 的组合还没看过）。
教训：**同队列不等于同时序——延后提交遇到别名瞬态资源就读到别人的内容；游戏适配先关 ASYNC。**

## 2026-09-24 18:34：赛博朋克 2077 用户确认

ASYNC=0 + 纸白 1 后用户实机："材质明显差异了"。赛博朋克通过。机上 flags 已清掉 DEBUG_DUMPS/DUMP_FRAME/PAPER_WHITE，保留 EnableFfxInputs=false（OptiScaler.ini）与 DLSS5_PRE_UPSCALE_ASYNC=0；addon 是探针版（钩 upscaler dll + 状态表补齐 + 两个诊断开关），进 0.30 前要过剑星回归。

## 2026-09-24 19:10：细节量化——"只有光影"还是"有纹理"，离线一算就知道

赛博朋克那次教训：色调变了不等于网络起了作用。做了个客观指标（`/tmp` 临时脚本，方法记这儿）：输入与网络输出同尺寸，亮度进 log2(1+64L) 的显示域，减高斯低通得高频，比 RMS 与相关系数；再比梯度幅值。
- **赛博朋克（坏的那次，天空探针输入）**：σ=3 高频相关 0.9997、拉普拉斯能量比 1.04——纯色调，判"没起作用"。
- **剑星凍结菜单帧（prod7 网络输出 vs live-menu-before 输入，900 档）**：σ=1 高频 RMS +11%、梯度幅值 +13%、相关 0.887（细节是新合成的，不是拷贝）；σ=4 比 0.95/相关 0.92（中频基本保留）；低频色调变化 std 0.27（色调也变，但不只色调）。裁图 `deployments/stellar-addon030-20260924/detail-crop-in-vs-out.png`：皮肤出毛孔级纹理、布料出织纹、背景金属出颗粒，边缘有彩色微噪。
判据可复用：高频相关 >0.99 且能量比 ≈1 = 只改了色调；相关 <0.95 且 σ=1 能量比 >1.05 = 有新细节。用户实机（19:06）：双钩 addon 在剑星画面/60 帧照旧。
- **赛博朋克第 3000 帧（ASYNC=0 后的真场景，中央裁 1296×720 离线跑 prod7 900 档）**：σ=1 高频比 1.001、相关 0.973；σ=2/4 比 1.03/1.04；梯度幅值 +13%。比剑星温和：它的输入本身已经很锐（高频 RMS 0.152 对剑星 0.131），网络主要是把已有的划痕/磨损纹理强化、边缘高光提亮，不像剑星那样在光滑皮肤上合成新纹理。裁图 `deployments/cyberpunk-20260924/detail-crop-in-vs-out.png`。用户主观"材质明显差异"与此一致。

## 2026-09-24 19:16：剑星双钩 addon 帧率（用户）

900P 拉 2K 最简单场景 60～61 帧，经 Splashtop 远程测（远程 UI 约吃 1～2 帧，本机应 61～63）。双钩 addon（c7f68e60…）在剑星画面/帧率与 prod7 一致，剑星侧通过。赛博朋克帧率待用户报。

## 2026-09-24 19:29：赛博朋克帧率（用户）

低画质 900P 拉 2K 约 50～51 帧（Splashtop 远程）。用户主观：材质有差异但"不够真实"；屏幕上黄色分辨率/帧率字样在 2077 里没显示。

## 2026-09-24 19:40：prod7 内核装进 RE9

RE9 目录里的内核比 prod6 还旧（0.28.1 那版，c32/mh_fast 哈希都对不上）。runtime 从 HIP\gfx1201 加载（LUID 选子目录），SHA256SUMS 只查文件名。`deployments/re9-prod7-20260924/install.ps1`：两架构各覆盖生产模块（10 个换掉，参考/实验模块不动），备份 `D:\DLSSNR-Lab\re9-prod7-20260924\backup`（-Restore）。gfx1201 mh_fast 98faa6d4、gfx1200 36ad568f = prod7。等用户看效果。

## 2026-09-24 19:50：RE9 装 prod7 后用户"绝对只是亮度变化"——抓帧量化说不是

用 RE9 宿主的 capture-colour.request（runtime 从 _storage_ 加载，请求放 _storage_）抓同帧：input RGB9E5 1512×848、proxy/neural 1600×900 FP16、result FP16。
- 网络域 proxy→neural：细纹理能量比 1.03、相关 0.979、**梯度幅值 +19.5%**。
- 游戏域 input→result：能量比 1.08/1.11/1.13（σ=1/2/4）、相关 0.99/0.986/0.982、梯度 +19%；平均亮度 4.108→4.077（几乎不变）。
判据：色调-only 是相关 0.9997、比 1.0、梯度 1.0；RE9 明显不是。裁图 `deployments/re9-prod7-20260924/detail-crop-in-vs-out.png`：面板缝、划痕、黑色金属边缘都更硬。
另外 prod7 与 0.28.1 内核逐位同（回归对 prod2 基线 12 帧哈希全同），换内核不可能改画面，只可能改速度；用户印象里的"之前不一样"不是内核造成的。发出去的 0.29 REFramework 包（prod6 内核 + fitlarge runtime）同理不受影响。本机 D: 上 0.29 包文件夹和 zip 已不在（用户上传后删了），无法重新哈希。

## 2026-09-24 20:00～20:50：强度外推试看、Google Drive 链接、网友"统一宿主"补丁审查

- 用户要看"最高强度"：`DLSS5_STRENGTH` 上限放到 3（>1 沿 lerp 外推，纯诊断），剑星/2077 flags 临时写 2,1；用户看完 21:07 已删回默认。
- 0.29 三包补了 Google Drive 镜像（给没有中国手机号的用户），README 两处链接；以后发布 = 夸克 + Google Drive。
- git：用户定"只推 297，别动外层 ai-theorys-study"。
- 网友的 OptiScaler 通用宿主补丁（统一 RE9 与常规包）审完，归档 `Development/RE9/presr/contrib/generic-host-20260924/REVIEW.md`：查询记账（切点无未闭合查询即可切分）与提前包裹（返回地址判断游戏 exe 建列表）两处可用；backend 补丁是我们 prepare-host.py 旧快照的翻版且缺曝光 ABI v2 两行；runtime 编译脚本不打我们的 runtime 补丁（退回 0.26 时代）；宿主 dxgi.dll 需 MSVC 未提；无测试证据。等用户问到网友在哪个游戏跑通、帧率多少再定要不要合进 prepare-host 实测。

## 2026-09-24 21:10～21:50：2077 三方对比（默认 / OP / Magpie 0.29）——红偏来自颜色转移，改成按 exe 查表 1,0

用户同机位截四张（机位微差，逐像素对不上，看全局统计 + 等倍裁片；图在 `deployments/addon030-strength-20260924/`）：

| | 平均 R/G/B | 高通细节 RMS（σ=2，log 亮度） |
|---|---|---|
| 默认 FSR（F6 关） | .107/.112/.059 | 0.159 |
| OP 1,1（847p 前置） | .122/.094/.049 | 0.193（+22%） |
| OP 1,0（只转亮度） | .120/.121/.072 | 0.189（+19%） |
| Magpie 0.29（1080p 后置，OSD 30 帧 33 ms） | .105/.111/.055 | 0.212（+34%） |

- 第一轮误判：先拿到的 op/magpie 两张机位差太多，看成"OP 动得轻"；补了默认那张才看清：OP 1,1 不是轻，是**色相转了方向**——R +14%、G −16%，绿色霓虹环境光压成灰褐；Magpie 和默认色调几乎一样，只加细节。
- 原因：前置路线把色调映射**之前**的线性缓冲交给网络，解码 `Hue(neural×ratio, neural)` 在线性域取神经色相，再过 2077 的 tonemapper + LUT，色相就跑了；Magpie 吃的是 sRGB 成品，LUT 已定型。
- 只转亮度（1,0）：色相比例回到游戏自己的，细节增益基本没丢（+19% 对 +22%），脸/报纸/红衣服都比默认清楚。
- 落地：`native_game_codec.h` `LegacyParameters` 里 `DLSS5_STRENGTH` 缺省或 `auto` → 按 exe 查表（Cyberpunk2077.exe → 1,0，其他 1,1），写 `event=strength` 到 oneshot 日志；模板 `hip-game-flags.txt` 加 `DLSS5_STRENGTH=auto`，CONFIGURATION.md 一行。addon a569ed6f…（`deployments/addon030-strength-20260924`，build-addon-oneclick.sh --hip），21:50 两游戏都关着，已装进剑星和 2077（backup-stellar / backup-cyberpunk，`install.ps1 -Restore`），两处 flags 的显式 STRENGTH 行删掉由表决定。剑星行为不变（1,1）。
- 细节上限：Magpie 在 1080p 上跑网络所以细节比 OP 多一截，但 30 帧对 51 帧；这是前置路线用 847p 换帧率的结构代价，不是 bug。
- RE9 宿主路线（LmxxfNrRuntime.cpp 自己读 `DLSS5_STRENGTH`，上限 1）不受影响；RE9 是后置 sRGB 域，色相问题不适用。

## 2026-09-24 23:00～25 01:30：launch 尾巴——双流死路，同流任意序 + tile 旗子成刀（900 −1.6%）

主线第 4 条。先量驱动（`HIP/experiments/stream-overlap`）：两条 stream 在这块 Windows HIP 上**完全不并发**（只有一条硬件队列，`GPU_MAX_HW_QUEUES` 无效），跨流事件一对 110～180 μs；同流 `hipExtModuleLaunchKernel` + `hipExtAnyOrderLaunch`（去 AQL barrier 位）能让下一个 launch 在上一个收尾时派发（2 wave/SIMD 时每 launch 198 → 31 μs）。第一版 spin 核用 `readsteadycounter` 计时，那条 `s_sendmsg_rtn` 全 GPU 串行，量出来全是消息拥塞——换成 `SHADER_CYCLES` + `s_sleep` 才对。
任意序没有"部分依赖"，只能把依赖搬进核里：土法 programmatic dependent launch（`HIP/experiments/pdl-chain`，`results/pdl-chain-20260925`）——C64/C128/C256 六条链上 FFN/QKV 核与窗口注意力核各发 `_pdl` 孪生，入口按 token 坐标等上游 tile 的计数器（FFN 组等上一块注意力的 ≤6 个窗口，注意力组等本块 FFN 的 ≤8 个 tile），出口每 wave `fence(release)` 后 +1；host 链头普通提交、其余任意序，计数器按（核种、通道、格子宽高）各一份、永不清零、目标为累计 wave 数；最近几块张量攥住不还 pool。逐位：18 个槽 2880 帧零差异。
拆账（1080）：协议纯成本 +0.25～0.29 ms（发旗 0.10，等旗 0.15～0.19：组开头一次 L2 往返 + barrier，没法和核开头重叠），调度捡回 0.23（正好是 launch-occupancy 预测的尾巴）；组屏障发旗版净零，改每 wave 发计数器 + 去掉 per-group acquire 后净 −0.1（−0.6%）；**900：11.74 → 11.55，−0.18 ms，−1.6%**，三次重跑 −0.16～−0.19。只开 FFN 或只开注意力都是零，两头要一起。
坑：(1) 环形槽位混用不同尺寸格子 → 累计目标追不上 → 死锁一次；(2) helper 里加空指针提前返回那版在任意序下三跑三报 `hipErrorLaunchFailure`，普通提交正常，撤掉后两跑两过，原因未明；(3) 时钟随功耗漂，只看相邻 A/B。
生产化：两份 hip 源加 `HIP_PDL_KERNELS` 孪生（原核不动）、host `opt.pdl`（`DLSS5_HIP_PDL`，模板 =1，Magpie 模板同）、插件重编 5be18ac3…；`deployments/stellar-prod8-20260925`（build/regression/payload/install）。regression-prod8（01:02）：900/1080 各两序列 12 帧哈希对 prod2 逐位全同；1000 帧 ABBA 对 prod2 基线：900 −0.80 ms（−6.3%，prod7 同口径 −0.65/−5.1%，即 prod8 比 prod7 约 −1.2%），1080 −1.02 ms（−5.7%，prod7 −0.92/−5.2%，约 −0.5%）。日志 `deployments/stellar-prod8-20260925/regression-prod8.log`。06:40 装进剑星（5 文件 + flags 一行，备份 backups\20260925-064001）。

## 2026-09-25 06:47～07:20：剑星切 DLSS 档位后 DLSS5 消失——钉死的 FSR 上下文

用户装 prod8 后从"平衡"切"质量"/"DLAA"，效果没了，切回也没了（进程内永久失效）。日志：06:47:00 切 DLAA 时 OptiScaler 建了新 FSR 上下文（1000002）并释放旧的（1000001），我们的 pre-upscale 日志从那一刻起一行都没有——不是回归失败，是 `dispatch_core` 在 `DLSS5_FIT_INPUT` 分支里把 `fit_context` 钉在第一个上下文上，"直到它被销毁"，而销毁钩子只挂在 upscaler dll 的 `ffxDestroyContext` 上；剑星的宿主经 shim 调用，我们钉住的是 shim 级句柄，provider 级销毁钩子里的指针对不上，永远清不掉——后面每个 dispatch（新上下文，任何档位）都静默透传。`restarts>=8` 也是个隐患：每次切分辨率的 session reset 都吃预算，切八次就死。
修：(1) shim 的 `ffxDestroyContext` 也钩（同 dispatch 的双钩子），命中记 `fit_context_destroyed via=shim/provider`；(2) 自愈：钉住的上下文连续 120 次没派发而别的上下文在派发，视为已死，改钉新的（记 `fit_context_reassigned`），FSR4 跟随者交替派发时每次命中都清零计数，钉不丢；(3) restart 预算只在失败（phase 5）时消耗，几何/队列变化无限次。插件 0211a78a…（`deployments/stellar-prod8-20260925` payload 已更新），等用户关游戏装。与 PDL 无关（flags 曾临时改 0，装机脚本写回 1）。
07:03 装剑星（用户切档位来回确认恢复）。根因追到 09-24 `1a3aa01`（卧龙 2 那次把主钩子挪到 upscaler dll、dispatch 补了 shim 双钩子而 destroy 没补）；0.29 发包时钩子还在 shim 上，不受影响。用户实测剑星：900P→2K 简单场景 60～61，1080P→2K 47～48。07:08 同一 payload 装进 2077（install.ps1 加 -Game）。
用户实测 2077（Splashtop 远程，简单场景）：低画质 2K 质量档（1707×961，1080 网络）41 帧，平衡档 51～52（昨天 prod7 50～51）。黄字仍不显示（老问题，低优先级）。

## 2026-09-25 07:20～08:20：C512 链旗子（不采用）与 0.30 打包

用户定"这一轮做完就打包"。C512 链 7 个核各发 `_pdl` 孪生（`HIP/experiments/pdl-c512`，四个模块），逐位；900：只 C512 −0.16 ms、只 C64-256 −0.19、两者都开 −0.15/−0.16；1080：−0.11 / −0.12 / −0.09。不相加——板功耗钉 325 W，填了空隙就掉时钟，两族像共用一份"时钟预算"。**不采用**，实验和结论归档 `results/pdl-c512-20260925`；用户加功耗上限后可复测叠加。坑：实验 host 打在生产头文件上，生产 ctor 只在 opt.pdl 时分配旗子缓冲，timeline 的 flags.txt 没写 PDL=1 → 对空指针算偏移 launch 失败。
0.30 打包：`Development/tools/package-030.ps1`（0.29 复制：三包基线 0.29，模块 prod8 两架构与剑星机上一致，插件 0211a78a，flags 模板查 PDL=1 / ASYNC=auto / STRENGTH=auto，常规包 OptiScaler.ini 叠加 `scripts/optiscaler-regular.ini`，RE9 宿主/runtime 与 0.29 同哈希，codec hlsl 与 0.29 同哈希）；package-notes 六份改 0.30；README 两处"当前版本"与更新记录行（链接待上传）。

07:53 三包打完（`Development/tools/release-030-results.json`，D:\給網友打包）：Magpie 336,377,155 B sha256 2b467532…、OptiScaler 366,576,181 B 8b3f1ae3…、OptiScaler-REFramework 421,034,771 B ae445772…；每包 SHA256SUMS 逐项核对、44 个 shader 变体编过、RE9 runtime 冒烟通过。等用户上传夸克 + Google 后填 README 链接。
08:08 用户上传完成：夸克 https://pan.quark.cn/s/80a735ab9f88（三包一个分享）、Google Drive https://drive.google.com/drive/folders/1pKZpLosgJXxUOZTMg_m0sbCipX9Q3WYo；README 两处链接已填。0.30 发布。

19:55 《极限竞速：地平线 6》（Xbox 商店版，`C:\XboxGames\Forza Horizon 6\Content`，实际运行路径在 WindowsApps 下）装 0.30 常规包（`deployments/forza6-030-20260925/install.ps1`，覆盖了游戏自带 `amd_fidelityfx_upscaler_dx12.dll`/`libxess.dll`，有备份可 `-Restore`）。第一次启动黑屏卡死（OptiScaler/ReShade 日志在第三次 D3D12CreateDevice 后同秒断掉，进程活着，插件一行未写——它在 30 帧 Present 之前不动手），关插件后正常，再开插件却不再复现，原因未抓到；`dump.ps1`（dbghelp MiniDumpWriteDump）备着，再黑就抓。真正的问题：链路通了（DLSS→OptiScaler→FSR，`render=1508x848 upscale=2560x1440`），但前置路线第一帧 `UNSAFE: draw/dispatch after deferred upscaler in same list` → fatal 透传，F6 无反应——和卧龙 2 同一种列表布局。`DLSS5_PRE_UPSCALE=0` 后置路线可用：1440 缩 1080 档，稳态 22.5 ms/帧≈44fps，覆盖 5362/6300 帧，F6 切换有日志，STRENGTH auto→1,1，黄字不显示（同 2077）。0.30 包不动，README 分游戏段写手动改法；下一版把 PRE_UPSCALE 做成 auto（首帧探测列表尾部有无后续工作，有则落后置）。网友说的"运行不了"多半就是这种"装了跟没装一样"。


## 2026-09-25 20:09 起：mapped/post 数据流审计与 post70 输入复用候选（Yami，待 GPU 实测）

按更新后的 WorkingPlan 开始追生产者/消费者。发现旧计划的前提过时：mapped/post 的 staging 在 09-23 已改为每 lane 4 次 b128，不是仍在 16 次 b32；当前特征生产者也是 HIP 而不是 HLSL 编码。block1 ← block0 down；block66 ← decoder_project2x_h16w(oc=32)；post70 low ← block69 main，skip ← block0 main8。每 token 的通道本已连续，改 tile 顺序不保证减少缓存行请求，不能直接承诺读等待减半。

先做较小候选 `HIP/experiments/c32-post-input`：post70 每 wave 两行像素共享同一低分辨率行，k=2/3 的 low 读取复用 k=0/1 寄存器；skip 与所有数值运算不变，奇数 sy/height 回落原读法。保留原核、同体异名 control、reuse 三个导出；新 runner 从现行 host 提取选项，prod8 模块底包、PDL=1、adaptive/graph/dup 关，两档整网 ABBA，计时外槽首尾逐位比较，并检查 post 每帧确实调用一次。生产源码和游戏安装均未改。

本机地址/有效性检查 59,904 对通过，MinGW Windows host 编译通过；已传 `D:\DLSSNR-Lab\hip-backend\c32-post-input`，入口 `start.ps1`（检查空闲→GPU 编译→900/1080 对照）。此时 GPU 编译、原核/control ISA 对照、GPU 逐位和性能结果均待执行，不宣称提速。原始依据和操作见实验 README；布局改造仍保留，先看这个重复读候选值不值得。


## 2026-09-25 22:21 起：自主跑完 post70 三种输入候选，均不采用

Zero 明确授权：DLSS5 优化的耗时编译、实验、回归、分析由 agent 自行执行，不再让用户手动跑命令。已记 WorkingPlan；仍遵守机器空闲检查。

`HIP/experiments/c32-post-input` 在 gfx1201 跑三类候选，两档各三轮 ABBA，并有同体异名 control。通用纵向低分辨率复用：900/1080 平均 +0.0265/+0.0278ms；将偶数几何提升到 host 检查后：+0.0084/−0.0028ms；block69 main→post70 low 用 FP16 精确传值：+0.0071/+0.0056ms。control 单次噪声约 ±0.028ms；后两者无稳定收益，不采用。每槽首尾 RGB 逐位检查全同，计时内无读回，post 调用计数确认每帧一次；这是固定输入筛选，不是全时序回归。

ELF 比对确认实验模块内 10 个原核机器码与 prod8 全同，control 与原 post 全同。偶数特化实际少两条 b128；FP16 post 改为 4 条 b64 低分辨率读取，post VGPR 都为 158，block69 VGPR 153 未变。不是假 null，也不能因此宣布整个 staging 已到极限。结果和代码身份归档 `results/c32-post-input-20260925/README.md`。生产源码、游戏安装、发布包未改。

同时补读 ViT 旧 page/pitch/groups 记录：完整 4MiB 权重的 padding/xor 收益很小，随机 stride 重排和多 wave 合组更慢。下一轮若做调度，限定真实核一 wave 一组的小范围二维 tile 编号重排，验证 A/B 复用取舍，不重追特殊 span64。


## 2026-09-25 22:33 起：ViT 局部二维工作组编号，六候选无稳定收益

`HIP/experiments/vit-group-order`：真实展开/收缩各做 GM2/4/8，保持每组一 wave、原累加顺序和输出地址，只让相邻 token tile 接连访问同一列权重。区别于旧随机 stride 和多 wave 合组。26,973 个 tile 映射检查无漏无重，覆盖 900 档25行的尾组。

两档各三组 ABBA（每槽20预热+160计时帧）。展开三候选的900均值 +0.006～+0.008ms，1080约0～+0.013ms；收缩900约−0.002～+0.005ms，1080约+0.002～+0.019ms。同期同体异名对照单次约−0.011～+0.036ms，未获稳定收益，六个均不采用。各槽首尾RGB逐位同，调用计数确认命中；这是固定输入筛选，不是全时序回归。

两次模块内76个原核函数体对prod8逐字节全同，展开/收缩control也全同。展开VGPR77→78/77/77，收缩205→211/212/212，地址计算改变编译调度，不能把耗时差全归给缓存。数据、源码身份、ISA核对归档 `results/vit-group-order-20260925`。生产、游戏和发布包未改。

下一步改为重测prod8 C32阶段账：旧c32-phase-trace逐核同步+Memcpy更新全局trace指针，host替换锚点还没适配PDL；先改成参数传trace区域、预分配、少量阶段/稀疏采样，并确认计时ISA和插桩扰动，别直接沿旧阶段份额猜刀。WorkingPlan已更新。


## 2026-09-25 22:56 起：重建 C32 阶段账，得到一把约0.1～0.2%的逐位小刀

`c32-phase-current` 去掉旧工具逐核CPU同步/Memcpy，预分配trace缓冲并经参数传指针，显式本地20位SHADER_CYCLES，不用REALTIME消息。每次测一个阶段与整段、每32窗口抽一个、8帧轮换余数。两档粗分+FFN细分全部RGB逐位同，插桩约+0.05～0.14ms（不到整帧1%）；部分核VGPR增加，故只以稳定阶段排序指导实验，不冒充未插桩精确账。新观测：输入准备约24%，FFN整体38%；其中主体25%、残差4%、入口同步2.6%、FFN发布6.8%。主体包括权重读取/量化，不是纯WMMA份额。结果 `results/c32-phase-current-20260925`。

沿残差检查发现非对角K16半块恒零，`c32-diag-zero` 去掉每wave六条乘零WMMA。六组真实权重×全部有限FP8编码，49,152个float32结果逐位同；短槽3轮、800帧长槽2轮，两档十组候选对照均快。长槽900 −0.0256ms（约0.2%）、1080 −0.0148ms（约0.09%），收益很小，留下一次合包，不单独装游戏。

标准导出候选收窄为只改chain/chain_finish_dcrop/chain_finish三核：三个函数体与实测孪生相同，其余七核对prod8字节不变。双架构编译通过，gfx1201正式NativeGameFrame回放8条件×12帧，96个候选RGB帧与prod8逐帧同哈希（含720、900/1080移动、history和浮点路径）；gfx1200仅编译。最终C32 hash gfx1201 e41c5c0b… / gfx1200 fafe5fe1…，候选在 `D:\DLSSNR-Lab\hip-backend\c32-diag-zero\candidate-modules` / `candidate-gfx1200`。完整结果、每帧hash和复现代码见 `results/c32-diag-zero-20260925`。生产源码和游戏/发布包未换。

下一刀查C32 FFN权重的片段预排，尤其32×128收缩矩阵的跨行读取；只改排列与对应寻址，不同时动同步/输出转置。WorkingPlan已更新，§3当前状态也从prod6/0.29旧摘要同步到prod8/0.30。


## 2026-09-26 01:55 起：闭源v0.4.0汇编调查，单wave/寄存器路线；偏淡对应默认LocalTone=0

用户给桌面下载目录dlssnr_on_amd_setup.exe，SHA 2d37453e…与官方v0.4.0资产digest一致（发布说明：比0.3.3性能提升42%）。另下载官方0.3.3（af5b9bbe…）对照。静态提取DLL与HIP fat bundle，用libLLVM20反汇编gfx1201：旧44、新86个函数，未知指令0；未运行安装器或闭源核、未改游戏。

C32的32线程/2KB LDS/零barrier寄存器快核在0.3.3已有；0.4.0新增42个核，其中24个C64/128/256专用MH核，另有reg1d/reg_vit与512布局转换。C64新核64线程/4KB LDS/7个静态barrier，对照旧通用核256线程/15616B/17个；有真实spill，不能只看零barrier。C32入口/出口四次128位读写，坐标除4、tile步长512B，支持4×4片段组织推断。packed-half归约与我们的求和路线不同，未证明逐位等价。C256 chain是显式DLSSNR_CHAIN开启，默认不能归功给它。

02:10用户补充网友截图偏淡、灯光/色彩不浓。进一步找到安装包完整INI模板：LocalTone=0、LocalStructure=1、ToneChannels=0、ToneLift=0、Style=0、ToneCurve=reinhard。UI明确LocalTone管大范围光照/色彩，宿主也将默认0送入控制参数；这与反馈相符，但未拿网友INI/做同帧A/B，不能定死为根因，更不能说它关闭光照计算换帧率。PreUpscale默认1，所以1080输出也不自动等于1080网络输入。

报告/身份/资源/默认INI在 `results/closed-v040-20260926`，复现工具 `tools/closed-inspect`，原二进制与完整反汇编只留/tmp。WorkingPlan把独立C32单wave窗口原型提到优先位，保留我们原数值与视觉控制；权重片段预排转后续小刀。单窗口四wave不是算法必付成本，这条认知需修正。


## 2026-09-26 02:21：从新增核名单收窄到关键派发与数据归属

Zero指出Daniel可能是一个关键改法带来40%跃升，不能把42个导出理解成42项小优化。进一步对齐宿主：0.3.3在0x1800363a3先要求C==32才进寄存器快路；0.4.0在0x180039c67检查同类flag后，0x18007a480表把C32/64/128/256都分派到快核。默认flag条件旧版已有，变化是覆盖范围与实现，不是简单把默认0改1。

旧通用路径每窗口显式global workspace为C32/C64 8KiB、C128 16KiB、C256 32KiB，新快分支绕过分配。旧C64 GPU入口从参数0xa0读取这块指针，加窗口号×8192，0x95c7c即有准备后输入的global_store_b32；还没完整标注其全部生命周期，不只叫“概率表”。旧swin_var本来也是融合核，真正关键候选是一个head的完整窗口归同一wave，减少中间状态落global/LDS，而不只是“多融合一次”。

自家旧c64_window_fused.hip已核：256线程，head=alltid/128，first=awave*16，nrm/feat放LDS，仍一头四wave；其null没有否定一头一wave。主线改为C64受控原型，原色彩/分辨率/数值路线不动；42%因果份额仍待实验。证据追加closed-v040报告、dispatch-delta.json/txt，WorkingPlan同步。


## 2026-09-26：一头一wave的C64首版逐位跑通，继续追整网质变

按Daniel版本差异线索独立实现c64-wave2：两wave覆盖完整窗口的两个head，两块4KiB LDS原生片段平面按生命周期复用，Q/K/V驻留寄存器，V用相反WMMA方向直接生成B片段。32项f32归一化用4次wave部分和传递保持原顺序，softmax保留现有WMMA归约，不改颜色/分辨率/跳层。4个barrier、VGPR190/191、零private spill。

900/1080各三组200帧ABBA，最终RGB槽首尾逐位同；替换8个C64块后平均−0.149/−0.257ms，约1.3～1.5%。模块74ed8686…，工具与结果分别HIP/experiments/c64-wave2、results/c64-wave2-20260926。只是可行节点，目标未完成；下一步扩C128/C256分账，查片段供数与寄存器生命周期。未改生产和装机，完整多帧历史回归待稳定候选。

## 2026-09-26：一头一wave扩展与寄存器调度，混合候选整网约3%

C128/C256独立实现均通过固定输入RGB逐位。初始C256有private spill；每个QKV tile重新引入不透明lane索引，限制跨tile地址/片段缓存生命周期后清零spill。编译调度栅栏进一步降VGPR，滚动query循环用uniform寄存器索引缩小代码；FFN两路展开优于四路，但C256全融合仍慢。没有改FP8/FP16舍入、颜色控制或分辨率。

最终筛选组合frag+launder+sched+roll+ht2，只替换C64+C128共20块，C256维持prod8：900/1080各三组200帧ABBA，配对均值−0.29591/−0.50451ms，约2.5%/3.0%耗时下降。全部槽首尾bitdiff=0，目标调用36/帧、实际替换20/帧。C256单独短筛+0.21068ms，全部替换−0.16194ms；不能因为零spill就判定C256问题已解决。

源码、全部分支筛选CSV/日志及校验脚本已归档c64-wave2；候选module SHA256=601f93af77afb3ac376ef5fdbe6cad8e9e2865f005988d4dc381a13fcacfe8bd。下一步保留生产FFN/QKV，仅验证C256一头一wave attention/projection。完整历史回归、gfx1200、游戏FPS仍待做；未部署。Daniel关键结构线索已转化为小幅实测收益，42%的因果份额与本项目质变目标尚未完成。

## 2026-09-26：C256保留生产前段，单换一头一wave注意力获得小幅收益

新增mode 6，独立核读取生产normalized FP8 [pixel][Q,K,V][channel]，用共享平面把V转成B片段后复用为AV，attention/projection段由已验证完整原型生成。原FFN/QKV与输入feature保持不变，输出PDL每窗口16wave计数改为8，前段计数与槽复用方式不变。现有PDL可见性/复用待审问题仍保留，测试通过不代替协议证明。

900/1080各三组200帧ABBA，配对均值−0.09923/−0.12809ms，所有槽首尾bitdiff=0，16块/帧替换计数正确。C256全融合+0.21068ms回退可通过保留生产前段避开；仍非大幅提升。gfx1201 float/byte输出151/142VGPR、零spill、32KiB LDS，module cc8166b3…；mode 7组合随后完成两档各三组200帧ABBA：900 −0.39283ms、1080 −0.67259ms（约3.3%/4.0%耗时下降），所有槽首尾逐位同、36块/帧替换正确。结果目录c64-wave2-20260926，下一步组合分账及feature直接读取省LDS。

## 2026-09-26：C256 attention残差直接读取，LDS减半但新增收益未定

实验增加DirectFeature开关：feature只在最终对角残差计算时从原FP8输入读，省掉16KiB plane0；V转置/AV复用仍用plane1。gfx1201模块6a0c6426…，LDS32768→16384B，float/byte输出VGPR151/142→152/144，零spill。1080 mode 6三组200帧ABBA全部槽首尾逐位，配对−0.15867/−0.17082/−0.14146ms，均值−0.15698ms。旧attention-only的−0.12809ms不在同批，不能把差值直接算新增收益；开关默认关，mode 7最佳已验证组合不变。

源、manifest与日志归档c64-wave2。下一主线移到C32完整窗口单wave原型，先中间chain，再prefix/post生产者布局；不把其收益混入Daniel版本差异解释。

## 2026-09-26：C32完整窗口单wave链式原型逐位跑通

独立额外模块c32-wave1替换block2/3/67/68四个非finish chain。保留输入输出窗口FP8、shift/crop补零、三段残差对角、C32专用FP16平方和与四个K16顺序softmax归约。Q/K/V寄存器驻留，FFN展开转置直接收缩，AV直送projection；4KiB LDS只存FFN半精度残差，单wave无需组barrier。

首版两档各三组200帧ABBA、每槽首尾RGB逐位同，每帧4次替换计数正确：900 −0.05340ms、1080 −0.08415ms。VGPR240、零spill，module380a5da9…；收益仅约0.5%，不当成目标完成。跨tile地址隔离版本239VGPR、零spill，1080 −0.06430ms（同样三组逐位）；没有实测改善。FFN隐藏片段滚动循环对照也完成1080三组逐位：−0.10293ms，VGPR仍240，module efc870c3…；与首版差仅约0.019ms且跨批，不据此断言额外提升。后续优先扩大到mapped/finish，再查prefix/post布局。未改生产/部署，多头组合与完整历史回归尚待。

## 2026-09-26：C32扩到八块；滚动窗口降寄存器并扩大收益

mapped首块block1/66和finish block4/69加入单wave原型，分别保留mode0输入先转half再乘scale、裁切/F转化和2×2下采样原求和顺序。finish用原4KiB半精度残差平面复用输出，未增加全局临时缓冲。八块首版两档三组200帧ABBA逐位，900 −0.09231ms、1080 −0.13849ms；mapped256VGPR/private60B/14spill，chain240、finish244VGPR。

QKV子段坐标隔离+编译调度栅栏未降压力（chain246、mapped15spill），1080短筛−0.06934ms；单wave改WGP mode短筛−0.11416ms，均无改善证据。保留开关默认关。

关键变化是保留窗口四tile外层循环：Q/K/V从动态C++数组换为可统一索引ext_vector，避免全展开与数组溢出。RollHidden+RollWindow：chain/mapped165VGPR、finish174VGPR、全部零spill、4KiB LDS；模块e51696a7…。两档三组200帧ABBA，900配对−0.16490/−0.18124/−0.17031ms（均−0.17215），1080−0.24577/−0.26211/−0.26295ms（均−0.25694），所有槽首尾逐位、8块/帧计数正确。不能把跨批差额精确归因于一个编译设置，但资源变化和整网增益方向一致。

未部署，尚未与MH候选组合、未跑完整多帧历史。下一步prefix/post全分辨率两端，随后组合回归。C32旧版Daniel已有，不当成0.3→0.4新增原因；当前结果仍远未到质变目标。

## 2026-09-26：prefix/post完整单wave与MH组合，整网耗时下降6.0%/6.6%

C32加入post70和prefix0，完整10块：post保留低分辨率特征+FP8 skip合并的两次Hrtz、最后RGB32项顺序f32点积；prefix复用生产噪声/历史特征，用低16lane算特征、高16lane取后8项，保留两次FP16 WMMA（零K片段也保留），main8和下采样顺序不变。两端分别176/174VGPR、零spill、4KiB LDS。完整C32模块73a2dfdb…，两档三组200帧ABBA逐位：900 −0.29543ms、1080 −0.45633ms。

新增DynamicFrames，按fixture生成位移/增益/黑白输入、seed=123+17*f，首帧null history、后续前一帧输入作history。每帧先baseline后candidate，同步完成后比最终RGB；两档各16帧完整C32全逐位。它证明Network入口的数学/历史输入一致，不替代NativeGameFrame完整宿主生命周期。

wave-owned-combined生成host把C32和MH候选接到同一Network，mode0 prod8、1 MH36块、2 C3210块、3合用46块。MH模块cc8166b3…（C64/C128整块融合+C256 attention-only，非DirectFeature），C32模块73a2dfdb…。两档三组200帧ABBA：900配对−0.70055/−0.70633/−0.70033ms，均−0.702405ms，11.661035→10.958630ms，耗时−6.0235%；1080−1.10831/−1.10106/−1.09592ms，均−1.1017625ms，16.693508→15.591746ms，耗时−6.5999%。全部槽首尾逐位、46块/帧计数正确；组合也完成两档各16帧动态历史对照，全逐位。

所有source生成器、原始日志/CSV、module manifest与校验脚本归档。未部署、未宣称游戏FPS提高相同比例。下一步可选生产集成、gfx1200编译、NativeGameFrame完整回归，再测真实游戏；后续继续研究ViT/C512/布局。6.6%是阶段结果，尚未实现用户要求的质变，也未证明Daniel42%的精确贡献分配。

## 2026-09-26：完整NativeGameFrame回归与gfx1200编译通过，6.7%收益保留

prepare-runtime.py把已验证组合Network接进原NativeGameFrame/D3D12桥接与benchmark_vit_reuse.cpp，仅以进程flag DLSS5_HIP_WAVE_OWNED=0/1选择基线/候选，记录销毁时46块/帧的累计调用/替换数。尚未改生产头文件、游戏或发布包。gfx1200两额外模块用gfx1201同一源构建通过，完整module hash清单归档；没有gfx1200硬件运行证据。

900/1080静态与移动、720移动、900连续历史，共6对12帧。72个候选RGBA16F帧逐帧SHA256与基线相同，所有已检查帧无非有限值，累计替换数与实际帧数吻合。覆盖真实捕获HDR输入→D3D12 encode→HIP网络→history/decode→D3D12输出，补上前轮仅Network接口的缺口；不覆盖所有游戏宿主资源生命周期。

完整处理长测每槽1000帧，去前200帧，按基线/候选/候选/基线：900四槽11.94554/11.23668/11.28361/12.00644ms，均值11.97599→11.26015，−0.71585ms/−5.98%；1080四槽16.98308/15.85462/15.87311/17.01188ms，均值16.99748→15.86386，−1.13362ms/−6.67%。计时槽只检查抽样输出，不能称8000帧全部逐位；72帧正确性回归单独逐帧比较。

所有帧hash、flags、原始日志与CSV、校验脚本、host输入头文件hash归档wave-owned-combined。下一步生产可选路径集成、兼容条件/回落、双架构打包与游戏计时；继续研究Daniel新增ViT/C512寄存器路线，目标仍未完成。

## 2026-09-26：wave-owned进入可选生产路径，正式构建回归与回落通过

生产Options增加wave_owned，NativeGameFrame add-on解析DLSS5_HIP_WAVE_OWNED=0/1、默认关。启用且兼容时加载c32-wave1/c64-wave2两模块并选择46块新路径；不同布局、graph及被替换范围跳层保留prod8派发。首次计数回归抓到skip_blocks.empty()过严：正式配置本来跳ViT42/43/46，导致0块替换；修正为只限制相关范围，后续实际46块/帧并逐位通过。

四份内核实现原样迁至hip/wave_owned_*.inc，实验生成器共用；正式通用配方支持26模块，增量prepare_wave_owned.py输出相同内核。两架构编译通过。通用/增量初次文件hash不同，经完整汇编对比确认只有__hip_cuid编译单元符号差异，规范化此一项后所有指令与metadata一致。gfx1200只编译，gfx1201实机。

生产host的完整NativeGameFrame回归：720/900/1080静态/移动/历史6对12帧，72候选帧逐位相同；关闭MH byte stream和关闭byte feature两种配置，在没有新增模块的旧模块目录测试flag1回落，额外24帧逐位，计数0替换。性能使用无计数插桩的生产host：每槽1000帧去前200，900 11.98305→11.26020ms（−0.72284，−6.03%）；1080 16.99276→15.87261ms（−1.12014，−6.59%）。

add-on SHA256 3e3ca57a079b607405227a7b3b5b1c72b71ae0416438928e69bc3b65808e67a8；payload包括两架构四新模块和add-on，部署脚本提供prod8预检、游戏关闭检查、已存在/新增文件备份恢复、flags回滚。gfx1200实验基线只含5个旧更新模块，不能假装是完整24模块集；安装仅增补新pair，其他旧模块不改。已准备但未执行安装/发布。RE9/C API实例配置尚未接此环境开关，后续独立处理。下一步同场景真实游戏ABBA与继续研究Daniel剩余ViT/C512差异，目标未完成。

## 2026-09-26 04:55～05:49：wave-owned 剑星实机 A/B（闇采数，朱雀补记）

闇在最后一次提交后按 install.ps1 实装了一轮，并已自行回滚：04:55 prod8 拍 `baseline-a`，05:02 安装（备份 `D:\DLSSNR-Lab\hip-backend\wave-owned-production\deploy-stellar\backups\20260926-050241`），05:12/05:18 拍 `candidate-b1/b2`，之后回滚到 prod8（05:49 核对：addon 0211a78a、flags 无 WAVE_OWNED、无新模块）。每组 6 张 HUD 截图 + samples.json（约 500MB，未分析），目录 `wave-owned-production\game-evidence\`。
同场景《剑星》2K 质量档（1707×961 → 1080 网络，FSR 输出 2560×1440），HUD 读数：baseline 47/47/46，candidate b1 49/49/49、b2 49/49/49；05:49 用户在 prod8 下复读 47～48，补齐 ABBA 的第二个 A。**约 +2fps（+4%），帧时约 −0.7ms**；离线 1080 档网络 −1.12ms，游戏里兑现六成多（网络约占 21ms 帧的 17ms，外加功耗墙）。画面未见异常，逐位一致由离线回归保证。尚未正式装机，2077 未测。

## 2026-09-26 05:51～05:54：wave-owned 正式装进剑星

用户关游戏后 `install.ps1 -Game stellar`：5 文件哈希校验、flags 加 `DLSS5_HIP_WAVE_OWNED=1`、dxgi/OptiScaler.ini 不变，备份 `deploy-stellar\backups\20260926-055146`。用户同场景 2K 质量档实测 49～50fps（prod8 47～48）。`native-hip.txt` 当前进程 `wave_owned_requested=1 wave_owned_active=1`，未回落。2077 未装，发布包未改。

## 2026-09-26 06:02～06:10：Daniel v0.4.0 同场景实机——快在少算 35% 像素，按像素效率持平

手动代理装（`results/daniel-040-ingame-20260926/swap.ps1`，只换 dxgi.dll，已切回并核对 fbfb6676）。剑星 2K 质量档同场景：Daniel **56～57fps**，我们 wave-owned 49～50。其日志：网络直接跑渲染分辨率 1707×961（我们 FIT 到 1080 层，处理 1920×1152，像素多 35%），网络 GPU 11.7～12.2ms；我们 15.87ms 按像素折算 ≈11.8ms——**内核效率持平，差距全在网络尺寸**。其 0.3→0.4 的 42% 是补自家旧通用路径的课（与闇静态拆包结论一致）。另见 WMMA 统计：Daniel 全包仅 73 条 FP16 WMMA，我们 ViT QKV（130 条）与 decoder 投影为 FP16——原版权重即 float/half，逐位约束所致，全换 FP8 估计整网仅 2～4%，不是主因。
画质："浅"有依据——其默认 PreHistory=0（日志 `history off`，时序历史未喂网络）、LocalTone=0、961 行网络。
**新主线**：网络按渲染分辨率跑（只补齐到网络所需倍数，不再放大到 1080 几何），像素约 −20%，预期帧时 −3ms 级，保留时序历史与现有色彩控制。需新增任意尺寸几何（ViT token、decoder 移位、固定尺寸编译特化）。

## 2026-09-26 06:12～06:25：2K 质量档往下缩进 900 档——帧率撞 60 上限；auto 规则改为"超出 ≤10% 往下缩"

根因复盘：1707×961 在 auto 下超过 1600×900 → 选 1080 档，`NativeInputGeometry::Make` 按比例**双线性放大**到 1920×1080 再补到 1920×1152——网络多算的 35% 像素全是插值。剑星 flags 临时 `DLSS5_NETWORK_HEIGHT=auto→900`（备份 `D:\DLSSNR-Lab\daniel-040\sb-flags-before-900.txt`）：日志 `input=1707x961 network=1600x900 viewport=0,0,1599,900`、temporal armed、wave_owned_active=1。用户：同场景（去帧率限制/垂直同步）稳 60，日志 avg_ms_per_frame 16.6～16.7 = 仍有 60 上限（来源未查：驱动 Chill/FRTC、60Hz+强制垂直同步或 Splashtop），真实余量未测；主菜单 56～57（非上限）；立绘效果可见。对照：同档 1080 放大 49～50，Daniel 56～57。
代码：`native_network_geometry.h` `ForInput` 改为输入两轴都不超过某档 110% 时往下缩进该档（1707×961/1760×990→900，1761→1080，1366×768/1408×792→720，1920×1080 仍 1080）；本机单元小测通过。缩放与 FIT_LARGE 无关（只管 >1920×1080 的接收），走同一 `Make()` 双线性。RE9 宿主同样调用 auto。**未重编 add-on**，剑星当前靠 flags 固定 900；下一版打包时恢复 auto。画质损失 = 输入宽高各缩约 6%，待 Zero 在游戏内对比确认。

## 2026-09-26 06:30～06:38：标准对照（1080P 窗口 + FSR 原生 AA，主菜单）——与 Daniel 0.4.0 打平

Zero 定标准测试：1080P 窗口、FSR 原生 AA（渲染 1920×1080，两边网络同尺寸）、主菜单读数（最简场景为辅）。flags 临时 `NETWORK_HEIGHT=1080`。
- 我们 wave-owned：日志 `input=1920x1080 network=1920x1080`、wave_owned_active=1；**主菜单 47～48，最简场景 51～52**。
- Daniel 0.4.0（swap.ps1）：**主菜单 47～48，最简场景 51～52**，完全相同。其日志网络 14.0ms（200 帧均值，history off）、游戏队列自旋等待 14.1ms、present 周期 19.5ms。
我们离线 15.87ms 为 1920×1152（多 72 行补齐，+6.7%），按像素折 ≈14.9ms，另含时序历史一路；游戏内持平是因为我们 PRE_UPSCALE_ASYNC=1 与游戏渲染重叠，他是 inline 自旋。结论：**同尺寸打平**；此前 2K 质量档他领先 7 帧全因少算 35% 像素，已由 cd0fea6 往下缩进 900 档补齐。可做的小账：1080 档底部 72 行补齐（原版几何，逐位约束所致）。

## 2026-09-26 06:43～06:50：auto-tier add-on 装进剑星并三档验收

本机重编 add-on（cd0fea6 源码，`deployments/autotier-20260926`，f71f38a3），只换 addon、flags `NETWORK_HEIGHT=auto`，备份 `D:\DLSSNR-Lab\autotier-20260926\backups\20260926-064302`（`install.ps1 -RestoreBackup`）。注意：本机 mingw 13-posix 编译**不确定性**（同源连编两次哈希不同），且与闇装的 3e3ca57a（4,055,051 B，非本机编）大小不同；源码差异仅 `native_network_geometry.h`，以游戏内行为验收。
同进程切三档，日志与用户读数（主菜单 / 最简场景）：1080P AA 1920×1080→1080 档 47～48 / 51～52（与旧 add-on 同）；2K AA 2560×1440→1080（FIT_LARGE 缩）42～43 / 45～46；**2K 质量 1707×961→900 档 56～57 / 60**（与固定 900 一致）。auto 新规则通过，今后标准测试不必改 flags。

## 2026-09-26 07:04：wave-owned 生产状态逐族区段账——C32 仍 34%，C512 成了唯一没动的族

新工具 `HIP/experiments/family-ledger`（sparse-prefix 改动态拓扑：先录一帧 Stage 游标，按阶段名定 15 区段，16 轮 localABBA），结果 `results/family-ledger-wave-owned-20260926`。prod8+wave-owned 46 块、生产 flags、graph/adaptive 关，09-21 捕获帧；派发 234→214。900/1080 × PDL 1/0 四组，每组 18 次原始 FP32 逐位检查全 0；标记扰动 PDL1 ≤0.11%、PDL0 ≤0.24%，区段和对 GPU 跨度 +1.2～1.8%，以 PDL=1 为准。
1080（ms / 份额，括号为 09-21 旧账）：C32 5.243 / 34.1%（6.444 / 35.5%）、C64 1.828 / 11.9%（2.363）、C128 1.787 / 11.6%（2.214）、C256 1.860 / 12.1%（2.254）、**C512 2.231 / 14.5%（2.179 / 12.0%）**、ViT 2.232 / 14.5%（2.471）、transitions 0.182。900：C32 3.623 / 33.5%、C512 1.768 / 16.4%（第二大）。
判断：C64/C128/C256 降约 20%，C32 同比例降但排位不变；C512 是唯一未做一头一 wave 的注意力族（92 派发不变），Daniel 0.4.0 对应有 reg1d / 512 布局转换核。下一刀推荐 C512 一头一 wave（按 MH 比例估 1080 −0.4～0.5ms）；C32 剩余为输入准备与 FFN 主体，排第二。未改生产源码/游戏/发布包。

## 2026-09-26 07:05～07:28：C512 一头一 wave（attention/projection-only）——逐位但 null，不采用

逐族账里 C512 是唯一没动的族，照 C64/C128 做法试一头一 wave。`c512_attn_wave`（`HIP/experiments/c512-wave`）：一窗口一 workgroup、16 wave 各一头，替换 `mh_attention_fused_fp8_out` + `mh_attention_project_frag_c512`/`..._scalar_fp8`；吃生产的 FP8 normalized、f32 feature、`PackedMhWeightQkvFrag`，AV 留 32 KiB LDS，投影保持生产操作数方向/K 顺序/残差初值/尾部。124 VGPR、0 spill。900/1080 各 3 组 200 帧 ABBA（对照 = prod8 + wave-owned 46 + PDL=1）全部槽首尾逐位、16 帧动态历史逐位、13 次/帧替换（含 identity 块）：**900 −0.017 ms、1080 −0.019 ms，噪声内，null**。两 wave 共一头（1024 线程）撞 192 VGPR 上限溢出 110，+0.30 ms。
重复派发量被替换两核边际成本：attention +0.165/+0.252 ms、projection +0.221/+0.331 ms（900/1080）——奖金约 0.4～0.6 ms，但融合核花掉同样多。原因：C512 只有 104/135 个窗口，一窗口一 16-wave 组、每 WGP 约 3 个常驻，排出第二轮尾巴且每 wave 串行 4 个 query tile；生产两核有上千个小组。投影要全部 16 头的 AV（K=512），拆 query 撞 VGPR、拆头要跨组，融合在这一层没空间。C512 的时间主要在 FFN 链（mix→split FFN→split projection→QKV），若再动 C512 应看那段。详见 `results/c512-wave-20260926`。生产、游戏、发布包未改。

## 2026-09-26 07:30～08:40：C512 FFN 链——QKV 与 mix 改 32 token/wave，逐位，900 −1.0% / 1080 −1.3%，进可选生产路径（未装）

重复派发分账（13 块/帧）：QKV 归一化 0.60/0.50ms 最贵，mix(h16w) 0.28/0.42，ffn_fused_t8 0.23/0.29，projection_frag 0.16/0.24。四核都是「一 wave = 16 token × 64 列」，每 16 token 重读整列权重（QKV 1080 单次 ~424MB L2 权重流量）。候选（`HIP/experiments/c512-ffn`，两档三组 200 帧 ABBA，全逐位+动态历史）：QKV 字节尾巴经 LDS 并成 b128 仅 −0.04；**QKV 32 token/wave −0.07～−0.10**；48/64 token 溢出反慢；投影 32 token null；**mix 32 token −0.04/−0.12**；**QKV+mix 合用 900 −0.122（−1.12%）、1080 −0.216（−1.40%）**。结论：C512 FFN 链受供数（每字节权重只喂 16 token）限制。
生产化：`hip/c512_m32_{mh,deep}.inc` + 两个独立新模块（配方 28 个，原模块源不变），开关 `DLSS5_HIP_C512_M32`（默认 0，`C512M32Compatible` 仅在生产 h16w/frag 配置激活，日志 `c512_m32_requested/active`）。生产配方机器码与实验逐条相同；双架构编译。NativeGameFrame 回归（`deployments/c512-m32-20260926`）：900/1080 静态+移动、720 移动、900/1080 历史全部逐帧同 SHA，每帧 26 次替换，回落用例 0 替换；生产 host 千帧长测 900 11.114→11.005（−0.99%）、1080 15.683→15.481（−1.29%）。add-on 3d8295db（含 auto-tier）+ 4 模块 payload 与 `install.ps1` 就绪于 `D:\DLSSNR-Lab\c512-m32-20260926`，**未安装未发包**。
⚠️ 回归中**现行生产基线**一次 900 历史用例第 8～11 帧与其余 20+ 次不同（非本改动），疑 PDL 可见性/复用，待高次数 PDL=1/0 对照。结果 `results/c512-ffn-20260926`。

## 2026-09-26 09:00～10:10：生产基线 900 历史偶发不一致——PDL 1/0 共 350 次零复现，不调默认值

c512-ffn 回归里那次"基线第 8～11 帧不一致"：回归的 `extra-900-history-False` 本身就是离群那次（多数结果 frame8 `F9FABBE4…`），异常时帧时抖到 18～23ms（有外部 GPU 负载），第 8 帧整幅微差、热点为网络 x≈770～1340 的全高纵带，后续帧由历史带下去。复现：900 历史 PDL=1/0 各 50、加并发 1080 回放干扰各 60、按回归原进程顺序（先 720 候选）30、1080 PDL=1 50——**全部 0 次不一致**。协议审计（RAW 等待覆盖、每 wave release 发布、累计目标分槽、`pdl_keep` 32 张量覆盖 C256 链、行对齐下依赖派发 acquire）未找到缺陷；理论空档记为：非对齐跨 tile 行出现时需补 gl0/gl1 invalidate。PDL 现值（仅 C256 链）：900 −0.081ms（−0.72%）、1080 −0.071ms（−0.45%）。结论：根因未定、不支持 PDL 竞态，**生产保持 PDL=1**，兜底 PDL=0 代价约 0.08ms。详见 `results/pdl-race-20260926`。

## 2026-09-26 09:12：C512 M32 装进剑星（待用户标准测试）

游戏关闭时执行 `deployments/c512-m32-20260926/install.ps1`：add-on 3d8295db（含 auto-tier）+ 双架构 c512-m32-mh/deep 模块，flags 加 `DLSS5_HIP_C512_M32=1`（auto / PDL=1 / WAVE_OWNED=1 保持）。备份 `D:\DLSSNR-Lab\c512-m32-20260926\backups\stellar-20260926-091206`。待 Zero 按标准测试（1080P 窗口 FSR 原生 AA 主菜单）读数，并核对日志替换生效。

## 2026-09-26 10:17：C512 M32 剑星实测

用户 2K 质量档（1707×961→900 档）：主菜单 57～58（C512 前 56～57），最简场景 60（上限）。日志 pid=29300：`c512_m32_active=1`、`wave_owned_active=1`、`network=1600x900`。当日 2K 质量档主菜单累计：prod8 约 47～48 → auto 分档 56～57 → +C512 57～58。

## 2026-09-26 09:15～10:40：m32-sweep——普查 16 token/wave 核，ViT project 加宽到 64 列（逐位，1080 −1.9%，900 −1.0%）

推广 c512-ffn 的"一 wave 多算、权重少读"（`HIP/experiments/m32-sweep`，`results/m32-sweep-20260926`；对照 = prod8 + wave-owned + C512_M32）。重复派发普查：C256 生产 FFN 最贵（16 次/帧，900/1080 边际 0.93/1.32 ms），其后 ViT attention 0.36/0.56、ViT QKV 0.34/0.54、decoder 投影 0.34/0.46、ViT project 0.24/0.41。
- **C256 FFN 32 token/组：null**（LINE_STORES 版 1080 +0.036）。按 FLOP 算它 ≈460 TF，已贴 FP8 峰值——算力瓶颈，少读权重没用，LDS 60 KB/181 VGPR 反拖驻留。**排序要看离峰值多远，不只看边际成本。**
- ViT QKV 三种加宽（32×64 / 32×32 / 16×64）两档不一致或变慢：wave 数不够藏延迟。decoder 16×64 变慢（2×2 上采样尾部每元素散写 4 处，加宽后串行）。
- **ViT project 16×64（每 K16 步 f32→E4M3 的 A 片段喂 4 个 WMMA）：三批 ABBA 900 −0.105/−0.112/−0.101，1080 −0.300/−0.289/−0.281 ms**，槽首尾与 16 帧动态历史逐位。手写特化版编出来不同（98 vs 160 VGPR）只剩 −0.04/−0.22，生产用实测模板原文，ISA 逐条相同。
- 接入：`hip/vit_wide_deep.inc`、模块 `vit-wide-deep`（配方 29 模块）、开关 `DLSS5_HIP_VIT_PROJ_N64`（默认 0，要求 vit_proj_frag）。NativeGameFrame 回归（900/1080 静态/移动、720 移动、900/1080 历史、关 frag 回落）全部逐帧同、计数每帧 8 次全替换。生产 host 千帧长测未跑（10:20 Zero 开了剑星，停手）。add-on 106ff3d0 + 2 模块 + install.ps1 在 `D:\DLSSNR-Lab\vit-proj-n64-20260926`，**未安装**。
- **配方缺口已补**：prod7/prod8 的 mh_fast 带 `HIP_FFN_LINE_STORES 1` 编，但 `hip/build-modules.ps1` 那行没写，按配方重编会丢 prod7 的 −0.6%。补后与 prod8 `mhfast.generated.hip` 逐字节相同。

## 2026-09-26 10:22～10:27：ViT project N64 千帧长测通过并装进剑星

`vit-proj-n64-production\regression.ps1 -TimingOnly`（生产 host，1000 帧去前 200，槽 0/3 基线、1/2 候选；两侧 WAVE_OWNED/PDL/C512_M32=1）：900 10.997→10.893ms（−0.104，−0.95%）；1080 15.511→15.232ms（−0.279，−1.80%），1080 四槽最终 rgb.f16 哈希相同。随即 `install.ps1`：add-on 106ff3d0 + 2 个 vit-wide-deep 模块，flags `DLSS5_HIP_VIT_PROJ_N64=1`，备份 `D:\DLSSNR-Lab\vit-proj-n64-20260926\backups\stellar-20260926-102640`。待用户实测。

## 2026-09-26 10:32：ViT project N64 剑星实测

2K AA（2560×1440→1080 档）：主菜单 43～44 / 最简场景 46～47（上午 42～43 / 45～46，+1 帧，与 1080 −1.8% 吻合）。2K 质量（900 档）：主菜单 57 / 最简场景 60（与 C512 后 57～58 持平，900 档只 −0.1ms 读不出）。

## 2026-09-26 10:35～10:45：剑星一直开着 ViT 自适应复用；修正"与 Daniel 打平"

核 flags 发现剑星早就是 `DLSS5_VIT_ADAPTIVE=1`（REUSE_HOTKEY=1，F8 切换；AE/EXACT 提示只附在 FPS 文字行，黄字行看不到——下次重编挪到黄字行）。`AdaptiveVitGroup` 在生产路径每帧调用。用户 2K AA 实测：AE 运动 44 / 静止 47；EXACT（F8）静止 44。**复用静止 +3 帧，运动无额外开销，保持开。**
**修正**：上午标准对照"我们 47～48 = Daniel 47～48"时我们开着 AE、主菜单静止，不公平；EXACT 下我们估计 44～45，比 Daniel 慢约 3 帧，与离线按像素折算（我们 ≈14.9ms 对其 14.0ms，约慢 6%）一致；其差额部分是时序历史（他关、我们开）的代价。今日所有游戏内读数均为 AE 状态（同状态之间的前后对比仍有效）。标准测试规则已加"F8 切 EXACT"。

## 2026-09-26 10:50～11:00：0.31 三包打完（待上传）

`Development/tools/package-031.ps1`（0.30 底包；只新增 5 个模块×双架构，取自剑星安装、每架构 29 个，其余 24 个与 0.30 相同——剑星上多出的非 packed `deep_fast.hsaco` 是旧实验残留、生产不加载，不入包；add-on 106ff3d0；模板 WAVE_OWNED/C512_M32/VIT_PROJ_N64=1、常规包 VIT_ADAPTIVE=1；源码提交 1c3950c）。产物 `D:\給網友打包`：Magpie 339,077,522 B `acd8e64b…`、OptiScaler 369,275,463 B `f53af436…`、OptiScaler-REFramework 423,711,009 B `c030bf36…`；底包逐文件核对、44 shader 变体编译、ZIP 回读、RE9 runtime 冒烟通过。清单 `Development/tools/release-031-results.json`。README 当前版本/更新记录已改好（本地未提交），等 Zero 上传夸克 + Google Drive 后填链接。

## 2026-09-26 11:19：0.31 发布

用户上传完成：夸克 https://pan.quark.cn/s/e76b8611e3cc（三包一个分享）、Google Drive https://drive.google.com/drive/folders/1xtBe_XhgF9eqBlrlIQMgWcEkzm0UKHIZ?usp=sharing；中英 README 当前版本与更新记录已填。

## 2026-09-26 10:50～12:30：ViT attention 四候选全 null；C32 prefix 分摊 1080 −0.03ms（宏并入、默认关）

`experiments/vit-attn-c32-input`（m32-sweep 派生，基线含 VIT_PROJ_N64），五候选均逐位 + 动态历史通过。ViT attention：m32 两 query tile 共用 K/V +0.08ms（并行度减半），V 转置写/8 字节读 −0.01，QK 操作数对调去 LDS 转置与 barrier ±0.03，二者叠加 ≈0——不卡访存量、V gather、同步链，卡在逐元素 exp/fp8 打包的 VALU 依赖延迟，逐位下无结构空间，不采用。C32 prefix：原来只有半 wave 算 16 个输入特征（Box–Muller 噪声），改两半 wave 同指令流分摊、g0 一次 bpermute：900 −0.01（噪声内）、1080 −0.034（6 组全负），以 `CW_PREFIX_SPLIT`（默认 0）并入 `hip/wave_owned_c32.inc`，配方未改、未进生产，下轮合包重编时打开并回归。结果 `results/vit-attn-c32-input-20260926`。

## 2026-09-26 11:20～12:40：单 wave C32 阶段账；输入宽读 + prefix split 进生产配方（−0.8～0.9%，待装）

`HIP/experiments/c32-wave-phase`（核内 SHADER_CYCLES，阶段变体与生产核同模块，生产核机器码逐条同；两档 177 次 VERIFY 全逐位）。wave 周期份额（两档一致）：输入 24.5%、FFN 37.7%、特征打包+QKV 11.6%、注意力+投影+写出 10.3%（loop 2 纯寄存器算术被编译器跨过计时点，只能合并看）、尾部 9.8%。按核：post 输入段 49%（折合全 C32 约 14%）、mapped 输入 35%。post 每 lane 每 tile 69×b32 + 16×u8 逐元素读 8 个连续通道（`if(valid)` + 4 字节对齐阻止合并）。
`CW_VEC_INPUT=1`：整行 b128/b64 读取，算术不变，VGPR 不变零溢出。`experiments/c32-vec-input` 三候选两档三组 200 帧 ABBA + 动态历史全逐位：宽读 −0.86/−0.82%，宽读 + `CW_PREFIX_SPLIT` −1.06/−0.94%（采用），仅 split −0.17/−0.21%。
生产：c32-wave1 配方（build-modules.ps1 + prepare_wave_owned.py）加两宏，跟 WAVE_OWNED 走、无新开关；双架构编译（gfx1201 7AC34418…、gfx1200 128BB82C…），gfx1201 模块 16 函数与实测候选逐条同；NativeGameFrame 7 用例 84 帧逐帧同；生产 host 千帧长测 900 −0.088ms（−0.81%）、1080 −0.140ms（−0.92%）。payload + install.ps1 在 `D:\DLSSNR-Lab\c32-vec-20260926`（前置哈希与剑星现装一致），**未安装未发包**。结果 `results/c32-wave-phase-20260926`。下一刀候选：finish/prefix 尾部（各自核 21～26%）。

## 2026-09-26 12:00：C32 vec-input 装进剑星

游戏关闭时执行 `D:\DLSSNR-Lab\c32-vec-20260926\install.ps1`（只替换 c32-wave1 双架构模块，WAVE_OWNED 路径内生效，add-on/flags 不变）。待用户实测（离线千帧 900 −0.81%、1080 −0.92%）。

## 2026-09-26 12:07：C32 vec-input 剑星实测

2K AA（→1080 档）：主菜单 44 / 最简场景 47（前 43～44 / 46～47，稳在上沿，与 −0.9% 吻合）。当日 2K AA 主菜单：42～43 → 44。

## 2026-09-26 12:05～12:20：C32 finish/prefix 尾部合并遍历——null

`CW_TAIL_QUAD`（`hip/wave_owned_c32.inc`，默认 0）：2×2 块一遍遍历，4 次 LDS 读同时供 main 与 down（原 64+64 次），写地址改 32 位偏移；ISA finish 2333→1777 行、64 位地址移位 18→0、零 scratch。两档各 3 组 200 帧 ABBA（对照 = 当前生产 c32-wave1 7AC34418；另设同源对照测噪声）：900/1080 均 −0.010ms（−0.10%/−0.07%），同源对照噪声带 −0.013～+0.024ms，**不采用**；动态历史 64 帧 bitdiff=0。尾部主体是写出量（每窗口 main 8KB + down 2KB，lane=通道 128B 连续已最优）。与 §7"转置尾部并宽"不同（那次改 lane 映射）。结果 `results/c32-tail-20260926`，实验 `HIP/experiments/c32-tail`。生产/游戏/发布包未改。

## 2026-09-26 17:00～18:10：3zwr1 AMDNR 0.3.3.2（c32w）实机对照——我们快约 9%

网友（Zero 转）：3z 称改了我们的代码、1080p 网络 17.8→15.3ms。静态看：OptiScaler 分支 + 我们 RE9 runtime（MIT 署名在）+ 加密 pak；c32w 为其声明的自有单 wave C32 核。runtime C API 计时对照卡在其 EnqueueHip 崩溃，改游戏内：其日志 `net=1920x1080 color_job=1705x960 c32w=on hist=on`，网络固定 1080 档、无复用。剑星 1080P 原生 AA 简单场景：3z 45～46，我们（晃动使复用失效）49～50；2K 质量主菜单 3z 40～41、我们 57。详见 `results/amdnr-0332-ingame-20260926`。

## 2026-09-26 18:30～18:45：HIP 导入的共享缓冲区永不归还——桥接层按档位池化

受 3z 更新日志启发实测：裸探针按桥接顺序导入/映射/释放 40 次，显存与私有内存各漏 ~3 GB，整套缓冲区一字节不还；真实 `D3D12Bridge` 40 会话切档旧行为约 73 MiB/次（1080 档 ≈93）。add-on（native_game_oneshot 每会话新帧）与 RE9 runtime（每会话 new D3D12Bridge）共用 `hip_d3d12_bridge.h`，一处修：进程级池（设备+字节+UAV），`Release` 归还不销毁，上限 ≈200 MiB；`DLSS5_HIP_SHARED_POOL=0` 回退。池化后第一轮三档后平台期（显存 ≈2530、私有 ≈310 MiB 不再涨）；fresh/复用输出哈希全同；NativeGameFrame 回归 112 个 f16 与此前逐字节同。add-on 5950fe20 部署包 `deployments/vram-pool-20260926`（未装）；RE9 runtime 可按 build-runtime.sh 重编。详见 `results/vram-leak-20260926`。

## 2026-09-26 19:14：显存池 add-on 装进剑星（待用户切档实测）

游戏关闭时 `D:\DLSSNR-Lab\vram-pool-20260926\install.ps1`：只换 add-on 为 5950fe20（0.31 源 + 共享缓冲区池 + PR #9 合并后的共享头），模块/flags 不变，备份 `backups\20260926-191447`。待 Zero 游戏内反复切分辨率/DLSS 档位看显存是否平台化。

## 2026-09-26 19:27：显存池剑星实测通过

同进程（pid 6804）网络会话 3→6（新增 3 次均 1080 档），性能计数器专用显存 11,203→11,240 MiB（+37，游戏自身波动），私有内存反降；修复前应漏 ≈280 MiB。池化在游戏内生效。

## 2026-09-26 19:50～20:30：RE9 runtime 可配置 + 接入 0.31 新核（A1）

TheAutomatic/ouco 反馈 RE9 包无法配置开关。以仓内 `src/LmxxfNrRuntime.cpp` 为唯一源：Create 时读一次 flags（白名单 DLSS5_HIP_*/SKIP_BLOCKS/FIT_LARGE/NETWORK_HEIGHT，环境优先），add-on 的 getenv 覆盖段原样搬进 `src/native_hip_env_options.h` 两边共用，默认值=0.31 模板，缺模块自动降级，状态行报告生效开关。兼容 RE9 包宿主：GetApi 收 ABI 1/2，ABI 2 给两参数 EnqueueHip 包装（PR #9 改三参数，老宿主会传垃圾队列指针）；shader/权重补 RE9 包目录布局。顺带修 PR #9 删 include 导致的 add-on 编译失败（`native_preblock_runtime.h` 补 `<cmath>`）；`hip-re9-flags.txt` NETWORK_HEIGHT 900→auto + 四新键。
验证（rt_bench 直调 C API）：旧/新/新关四组尺寸末帧哈希全同；ABBA 1920×1080 16.88→14.93 ms（−11.5%），1707×961 16.90→10.77 ms（900 档 + 新核）；24 次切档显存旧 +180/次、新 +35/次；runtime-smoke 过；部署脚本安装→回滚验证。部署包 `deployments/re9-runtime-flags-20260926`（AMD 同名 `\deploy`），未装、不发包。详见 `results/re9-runtime-flags-20260926`。

## 2026-09-26 21:00～21:17：可配置 RE9 runtime 装进 RE9 并实测

RE9 目录原为 0.29～0.31 同款宿主 0ef10229 + runtime 6e9974d7 + 旧 24 模块。先把 HIP 升到 0.31 RE9 包（每架构 29、SUMS 58 全核对；旧 HIP 备份 `D:\DLSSNR-Lab\re9-runtime-flags-20260926\pre-031-hip-backup`），再 `deploy\install.ps1 -GameDir <RE9>`（备份 `deploy\backups\20260926-210057`）。OptiScaler.log：`modules_ok=58 hip=1 ... wave_owned=1/1 c512_m32=1/1 vit_proj_n64=1/1 pdl=1 skip=3 | flags: ...`，老宿主两参数 EnqueueHip 兼容生效。缺陷：runtime 只在首帧打几何行，改设置后不再打印（下次补"尺寸变化即打印"）。
用户中画质 A/B（flags 三开关 1/0，同分档规则）：2K DLSS 高质量（≈1707×960→900 档）54 对 51～52；2K 原生 AA（→1080 档）38 对 36。新核约 +5%。flags 已改回 1。

## 2026-09-26 21:20～21:27：0.32 三包打完（待上传）

`Development/tools/package-032.ps1`（0.31 底包，取自 history；源码提交 e1d9bd3）：常规 add-on 5950fe20（显存池 + PR #9 共享头，剑星装机验证）；c32-wave1 宽读版双架构（剑星/RE9 装机同哈希 128bb82c/7ac34418）；RE9 runtime 2aedb521（读 flags、0.31 新核默认开、显存池、PR #9、老宿主 ABI-2 兼容，RE9 装机实测）+ 新 hip-re9-flags 模板；SOURCE-README 追加 runtime 源码提交。产物：Magpie 339,076,857 B `87eeae8e…`、OptiScaler 369,274,773 B `a26d1fab…`、REFramework 423,734,964 B `567b8ef9…`；底包逐文件、44 shader、ZIP 回读、RE9 runtime 冒烟通过。仓库 HEAD 的"env 选项搬家"版 add-on 未入包（待回归）。README 0.32 条目已写好（本地），待链接。

## 2026-09-26 21:51：0.32 发布

夸克 https://pan.quark.cn/s/b805e071405c ；镜像改用 Gofile https://gofile.io/d/CZ67LYIc （本版不是 Google Drive）。中英 README 当前版本与更新记录已填；tag 0.32 = e1d9bd3（打包源码提交）。同时剑星已整包解压 0.32 常规包做全新安装验证（备份 `D:\DLSSNR-Lab\stellar-fresh-032\backup-20260926-213704`，`install.ps1 -Restore`），待用户游戏内确认。

## 2026-09-26 21:55：0.32 常规包全新安装验证通过

剑星整包解压 0.32（DLSS5-AMD 目录全新、OptiScaler.ini 用包内模板）后用户进游戏：黄字正常、帧率与此前同；日志（全新 logs 目录，pid 29688）wave_owned/c512_m32/vit_proj_n64 均 requested=1 active=1。剑星现为 0.32 包原样安装（旧目录备份见上条）。

## 2026-09-27 03:00：fp8-sat-mode——MODE 饱和路线不逐位，med3 写法逐位 −0.3%

借鉴 mochizuki0323/DLSSNR-AMD（Vulkan）的"FP8 饱和转换用 MODE 位代替每值 clamp"。探针：gfx1201 上 MODE.FP16_OVFL（hwreg MODE bit 23）确使 `v_cvt_pk_fp8_f32` 对有限溢出饱和到 ±448，但 ±Inf/NaN 变 E4M3 NaN，且核内 f16 RNE 转换溢出改为 65504；gfx12 的 cvt 没有指令级 clamp 位。c32-wave1 入口设 OVFL + 去 clamp：指令 −8～−11%，900/1080 静态运动逐位，但 720-motion 输出变化；只设 OVFL 保留 clamp 同样变化 → 原因是 f16 溢出语义，不是去 clamp，**不采用**。改为不动 MODE、clamp 写成 `fmed3`（探针对 NaN/Inf 同字节）：指令 −2%，7 组 84 帧逐位，两批 ABBA 900 −0.037/−0.029ms、1080 −0.045/−0.035ms。宏 `HIP_FP8_SAT_MODE`（默认 0；3 = med3）在 `hip/c32_fused_ffn_attention.hip`，未改生产配方、未装游戏。详见 `results/fp8-sat-mode-20260927`。
