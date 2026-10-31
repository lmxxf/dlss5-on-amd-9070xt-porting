# DLSS5（DLSSNR）→ AMD RX 9070 XT 移植：开发史

> **2026-10-06 压缩**：已完整读取原2005行，原文363,263字节逐字节备份至 [DevHistory-full-20261006.md](history/DevHistory-full-20261006.md)，SHA256 `f1fdd75daae23df753e51d3d9d8c8e630326dde58bf83fae58cdb1d068b98427`。本文保留结论、关键数字、现行约定、停止路线与未完成边界；逐刀SHA、失败日志、部署备份及统计原表去完整原文按日期/关键词查，再进入对应results。
> **2026-09-23压缩原文593KB**仍在 [DevHistory-full-20260923.md](history/DevHistory-full-20260923.md)（a300c20之前完整版）。两次原文都是历史快照，后续纠正看本文和新记录，不能把旧推断当当前事实。
> 本文件是开发/部署/运维历史入口；**实时状态和排队工作只在 [WorkingPlan.md](WorkingPlan.md)维护**，版本变化看CHANGELOG，中英文配置说明在scripts/CONFIGURATION{,.zh-CN}.md。

续写只在§12末尾按时间正序追加，写实际结果与改变结论的证据，不逐条记审稿/编译等待。长起来再整本备份压缩；实验数据留results、实验源码留HIP/experiments。共享分支禁止amend；commit不加Co-Authored-By。本文压缩只改文档、不改代码、玩家配置、已装载荷或发行包。

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

## 2. 逆向结论（原NVIDIA合同；当前fast并非完整exact）

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

**NGX 输入**：Color / Output（1080p RGBA16F）/ Depth / MVec；history（slot8）= 上一帧内部post工作域blend输出（不是API final），motion 在 slot10；`input_scale=1/32`，`rgb_mode=1`。

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

## 3. 当前状态

**不在这里维护。** 现状（当前版本、基线、各游戏现装、正在进行）只看 `Development/WorkingPlan.md`——两处锚点必然有一处过期（09-29 发现本节还停在 09-26）。版本改动看 `CHANGELOG.zh-CN.md`。

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
收益全部来自数据搬运：寄存器分块复用、FP8 格点上的中间量改用 f16 存、权重常驻 DEFAULT 堆、删 LDS 转存和 barrier、合批。该历史DX12快链的主要收益来自软件H/F和散写等标量尾巴；不能推广为当前整网已证明不受带宽/矩阵限制。逐刀表见原文 §3「fast 链每刀收益表」。

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

### 09-24～10-06 后续里程碑

|日期|落地变化与关键结果（都是当时同批口径，勿跨行累加）|
|---|---|
|09-24～25，0.30|MH全行写约−0.6/−0.7%；同流PDL六链900约−1.6%；双流不并发。修FSR上下文销毁双钩/切档自愈。2077别名输入必须ASYNC0，强度auto按exe选2077=1,0。|
|09-26，0.31/0.32|一头一wave与C32完整窗口共46块，完整Frame900/1080约−6.0/−6.6%；auto档允许超出≤10%往下缩；C512 mix/QKV M32约−1.0/−1.3%，ViT投影N64约−1.0/−1.8%；C32宽读/split约−0.8/−0.9%。共享导入池修73～93MiB/会话泄漏；RE9配置/ABI1,2兼容与新核落地。|
|09-27，0.33～0.35|成对FP8 PACK8约−9%整网；fmed3/固定短MODE段、C32去重复量化/RTZ配对/边界快路、MH字节输入/坐标优化。ViT AV字节+contract half流1080约−1.3%、900近中性。PDL uint32回绕drain清零；RE9释放绑定资源及flags平台化。鬼武者换执行队列修复。|
|09-28，0.36|float FMA经用户批准替旧fast激活，重新冻结gold；C256 wholeblock仅1152约−1.6%；C512 QKV-attn融合+MH权重复用约−3.6/−2.6%；Up56/62/66融合收尾约−2%。相对0.35实际组合约−8.5/−8.9%，并非逐位同0.35。|
|09-29，0.37前后|C512按有效raster点算约−2%；head pool+project融合、ViT score片段转置；C256两段6层init/run/recover约−1.9/−0.6%；ViT attention精确值域/成对编码/归约方向约−2.2/−1.5%；QKV W5共享权重入生产。|
|09-30，0.37后|900 C512省shift_pack约−1.1～1.4%；C32 skip/pre/down/post字节边、复合量化掩码、W16、尾向量化、倒数/冗余F清理、读等待外提等14刀通过。1088行作为有损可选，默认1152。|
|10-01，0.39|C512 QKV合趟/深流水、FFN寄存器合核及FP8 WMMA、ViT QKV F8W；小通道链尾half/byte边、QKV合趟、Up尾宽读；宿主负探测缓存及Style选项，锁基址消MinGW路径差，整套可复现重装。|
|10-02～03，0.40|两行LLVM23+调度配方收下；补裸LDS屏障栅栏。runtime直写输入和post_signal_query收下；全71+FAST1取代跳42/43/46，三层配置、多遍/F9/后续遍跳块；PRE_UPSCALE auto与ViT fast twin入main。|
|10-04，0.41准备|快速3x仅两遍真网络+预测第三遍；双feed首帧死锁修复；肤色保护观感未达标默认0。多遍直接RGBA、1440 C256路由、ViT已量化contract byte边、C512 directpack组合收下；Issue13原版双帧/原pre-down取证、Issue4当前device重绑、RE9强度入口。|
|10-05，0.41|三正式完整包发布。随后最终共享RGB直写、block4 pool→C64字节边、仅1440FAST3持久队列三刀安装双游戏；正式ZIP不变。用户1x剑星约57.6fps、快速3x约37；鬼武者900P快速3x约49。非同批FPS增益证明。|
|10-06|闪烁原history/RTZ取证、默认off regular 0.41-a实验；旧M完整资产/提交口径翻案和共同输入对账。H900与单timed marker本地生产接入且兼容闭环，未安装/发包。数学/供数/两条跨边界融合负账；固定工作图重放有效，但实际Auto产品替换首筛未过。|

## 6. 算力缺口与当前竞品对账

最初318研究：9070 XT微核FP8/FP16/FP32约405/204/49.9TF（约3.1GHz），整网有效矩阵约55TF；寄存器追加WMMA同读量150→299TF，说明供数/指令组织能限制微核，不是硬件峰值不足。325W功耗墙使FFN负载降频，局部并发收益不能相加；访存请求数、触及缓存行与数值指令都须核。

09-21～10-01的family/event/DUP/roofline是**历史诊断**：当时C32约35%、其它族约11～13%，计算下限900/1080约2.16/3.09ms、含WMMA/VALU组织模型4.19/6.01ms，均不是当前精确份额或硬件stall归因。逐派发event均摊42µs与DUP有系统扰动/丢重叠；全空C512消融改输出及下游，仅整网边际，不是核本体上界。不能用老地图断言某族已到极限或差距只剩数学。

**旧mochi锁账翻案**：实际测速0.0.2.5源码d1185d2514、原exe `8ad3ac1c…`，70资产本地/9070逐SHA同；glslang16.5+quad/direct1重编48/48网络SPV byte同。本地ACO旧4f62a8a不是测速版本。默认`--chunk=1`每帧submit+fence wait；“123 dispatches, one submit”是单pass打印，**撤回500帧一次submit与0.1～0.3ms归因猜额**。123实际steps.size且每step一dispatch，命令缓冲重放，vkCmdDispatch录制次数不等执行次数。

|锁定共同输入纯NN GPU均值|HIP FAST0|HIP FAST1|M|解释边界|
|---|---:|---:|---:|---|
|900首账|6.706866|6.628813|F0对照6.017463/F1对照6.028481|F1差0.600331ms；HIP400 vsM448token，非同token|
|1088首账/640|9.025781|8.880759|7.882941/7.867191|F1差1.013568ms；生产1152的额外行另列|
|fresh1088/640，60566|—|**8.931545125**|**7.885259375**|剩差**1.046285750ms**，未解释/未追平|

共同half-exact encoded gradient、Style1 seed0/full71/MP1/AE0/historyoff、逐帧提交同步，输入pad/模块/源/原M计划及宏锁定；CPUchrono与GPU时间分列。fresh共享42模块SHA同，H900在1088不启，Bridge-onlyPulse纯NN不存在。M只聚合GPU时间无逐帧p99；900几何不同、noise M预生成/HIP逐帧仍需区分。APP改善不能从这个pureNN差扣除。参考sync-network-gap、sync-network-gap1080、fresh-mochi1088、mochi-old-lock与mochizuki-gap-audit（均20261006）。

旧M实际ViT QK/AV FP32 cooperative accumulation、NR_ACC_F16=0，score先half RNE再halfFMA/位图；64key半分母按原固定顺序；出口ctx/inverse/product半化，**没有每AV chunk半截断**。M/我640 QK/AV矩阵次数同无尾padding。真实PAL `.text`身份映射：M g_attn182VGPR/9216LDS，VT114/0；我生产C512189不是旧误用256，不能仅VGPR推occupancy。详见vit-math-contract、mochi-driver-cache/map及production-resource纠错结果。

**161 vs123真实序列**：HIP89772首cold实际Fn161，M15597原--wiring图123，各自raw同finite。extra38=小通道encoder/decoder stage组织差28、ViT pack/反gather9、前C32/head+2、decoder39−1。SP每frame两stage、stock缺optional init_pair时各init/run/recover（recover总launch但正常内部重算0）。HIPsteady总161不证明cold逐名序列等steady；M跨层仍写global tile，allocation footprint不等实带宽。dispatch-sequence-20261006保阶段表/ABI与loadedSHA。

**RDTS窗口观察**（SPM+4SQTT，不是SPMonly/native性能）：双方同9070/driver/provider31defs/36IDs、interval4096；HIP start13042/count161→483，M9842/count123→369。各自reference rawbit0finite、资产不改、driver restore日志通过，未独立读实际频率。Budget1→3：HIP MemoryUnitBusy87.275→89.816%、stall19.784→20.349%；M90.236→90.205/12.944→12.922%；icache HIP99.381→99.344、M65.075→65.143%。MemoryUnitBusy是访存单元活跃且**包含stall**，分母commandprocessorbusycycles，不是GPU利用率，不能busy−stall当compute/bw或推1ms因果。Budget3样本17326/16496；SystemInfo100MHz只条件换算约29.897/28.582ms，不是native帧时/硬件锁频读数。globaloffset、完整帧函数映射、gfx12SQTT tokenreader、overflow/丢样仍未知，三budget不冒三完整帧。旧mixed捕获改输出的21.7%stall结论已撤回；旧热核68%只限热核。fullnn-spm/mochi-spm-20261006及独立可比性报告留全部定义/traceSHA。

## 7. 已停止 / 不要换名重复

完整逐候选负实验与未采纳/修复演进索引见 [failed-experiments.md](failed-experiments.md)；本节仍保留原压缩摘要，不代完整台账。

旧null只能在**实际profile/编译模块/数学/依赖发生变化**时说明新条件；零spill、少指令、少派发均不是自动收益证明。下列是具体候选STOP，不是所有可能实现的上界。

|范围|已测形态与裁决|查results关键词|
|---|---|---|
|旧布局/驻留|C32 no-unroll/VGPR封顶/LDS_VECTOR/尾转置；小C64/128 frag、post重复low读取、ViT GM2/4/8编号、地址skew/pool-hot无收益。RDNA4无global_load_lds，不能借CDNA指令。|c32-transposed-tail、c32-post-input、vit-group-order、infinity-cache、ideas-yami-ikaruga|
|C512|attention+proj一头一wave null；FFN M32/LDS共权、mix分N、深流水FFN、投影M32/WN4/DEEP、rawbyte残差、compact VT等未收。FFN_PROJ静态8wave融合两档慢。8无依赖stream慢保负账，**不代表所有队列严格上界**。|c512-wave、c512-round1、c512-proj-share、c512-ffn-lds、gap-map-evening、c512-xblock-queue、ideas-yami-ikaruga|
|ViT逐位组织|M32两16query链、TM2/TM4、W5T2/H2/BIG、decoderNT/expand尾宽写、gather折叠、KS、QKV尾宽写负。960 contract两wave共享权重171spill/688Bprivate静态止。|vit-1080-gap、deep-tail、gap-fusion、vit960-contract-m32|
|小通道/SP|C1284→3/C642→3持久化不赚；SP_ENDS新C512FFN基线1080慢；UP_DIRECT4xMMA、skipbyte/hidden tiles、C256 QKV_FUSE负。这些**不自动否定当前W16整stage+DS融合或不同directregpool**。|swin-persistent-c128-c64、c256-gap、c128-c64-inchain|
|交接/IO|HIP→D3D自旋曾TDR/饿死，双poll更慢；INPUT_POLL整Frame无收益；共队列无公开HIP入口。IO_FUSE均值快但900p99多次坏已封存；T8_NO_F32后FFN_ONE死宏。|handoff-poll、handoff-gpu、same-queue、input-slim、pending-review|
|编译|COMGR2/LLVM20慢、公开21无替换、VOPD前瞻无合格整网；LLVM22/23裸屏障正确性缺陷须先修，修后vit/mh仍慢，不整套换编译器。其它调度关VOPD/delay/occupancy sweep负。|compiler-versions、llvm-patch1、compiler-sweep、llvm23-vit|
|数值保护|入口一次MODE OVFL破f16溢出/黑块保护；固定短段需有限域和MODE恢复证据。directFP8删half存在0x3f880001反例；C32激活packedhalf bit2慢。|fp8-sat-mode、fmed3-ovfl、mochizuki-022、fast-tier|
|近期小刀|C512 activation LUT、C32固定几何入口、ViT960常量入口、小请求≤8MiB晚一batch池复用无收益（1440+0.061425ms）。|c512-activation-lut、c32-small、vit960、small-buffer-delay|
|ViT数学B/C/B2/C2|同16query有损scorehalf、halfden树及keyperm均无合格收益；B mean+.007611，C+.036425/p99+.028335；B2−.006105/p99+.054664；C2−.004771弱漂移。PSNR对A约52.20/52.63dB，非NV画质裁判。C62vs68VGPR均allocation72。B/C不支持则不D，不盲M32交互。|vit-math-stair、vit-score-halfclamp、vit-den-keyperm（20261006）|
|C32 feature直量化|只feature F32→FP8，residual仍half；gold捕获dynamic vector bug修复，不是生产helper坏。valid vsOLD51.1442dB非NV，GPU−.014088ms局部小收益，root止解释实验不生产。原NV半合同仍在。|c32-direct-feature/150cb252、98d6e633、70170|
|C512分母8→1|unit与wholeNN0；首pure GPU小快但实际APP1152 mean−.294µs/p99+54.17µs，实际APP已负，停止后续/formal。|c512-den（20261006）|
|VT硬件trload|AoS lane地址构造不改producer/数学，HWbyte0/soleVload 1/78函数改、wholeNNraw0；首GPU−.03505/p99+.03204、wall−.01045/p99+.02385ms，止不formal/集成。|vit-av-trload（20261006）|
|Body22→Down融合整线|实际普通Body22在SP21后；FAST3 rawF32skip3必须保，Down loaded normalhalf（缺fasttwin）独立数学。共享池LDS41216 raw/count161→160过但尾负；regcache private192静止；noescape private0/LDS32768/VGPR242 gold0但GPUmean约0、wall+.0112/尾+.03254。整线STOP不调。|body22-down-local/86300694、7b1faa96、3ed96f5e|
|Up48→普通Body48融合整线|SP49..54不动；Up独立normalRNEhalf merge→FP8add0，Body FAST每head32norm/byte residual。初post4错guard先于GPU修actual0；scope需actualactive/HasFn才sentinel。64key窗plane0够，W16不是16×16空间；135tile较128投影+5.47%。shadow/wholecount161→160/raw0，但wallmean+.003681/p99+.11964ms，STOP。|up48-body48-local/fbedb85e、up48-body48-audit|
|M反事实score|只1SPV halfFMA→F32非收缩map，其余半den/TR/AV/出口保；vsold52.7257dB，GPU+.00936ms弱/漂移/noM p99，止，不替原M基线。|mochi-score-f32/fb5c136f|
|paired提交事件|诊断pair mean快但1152第三正式p99+.08934ms，封存；query/匹配CPU delay负，不能由EventRecord效应断言必flush。旧single safe900 Record0实际fallback，不是性能负账。|framework-submit-pair、framework-single-event、submission-pacing|

旧§7“一切hipGraph收益为零”已被**同161工作重放**反证，取消普遍禁令；新范围/产品基线裁决见§12。负账不可用改名或加诊断事件刷收益；stop失败数据全留原文/results。

## 8. 工程教训（合并版）

**测量**
- 只信同批交错 ABBA，别跨批比较绝对值；GPU 会卡在降频态好几天，量之前先跑基线。
- 逐核 HIP event 在这套驱动上不可靠，会出现负值，全插反而把帧时拉长 80%；短段事件也不能当依据。wall/外层GPU跨度可作同批对照；dup/跳块只能作敏感性诊断，丢重叠/改变输出与下游，不能均摊精确贡献。
- 读回节奏会改变 GPU 频率：HLSL 全读回 26ms，只读首尾 16.7ms。性能测试一律只读首尾，正确性另做全帧检查。
- 单核隔离会把工作集留在缓存里，不能直接相加当整帧。
- `DLSS5_GAME_PROBE` 每帧 Flush，会破坏异步提交时序（地面变透明），不要和 ASYNC_SUBMIT 同开。
- 测帧率别开 Splashtop；先确认游戏有没有锁帧（剑星曾锁 30）。
- **游戏内帧率标准测试**（09-26 Zero 定）：《剑星》**1080P 窗口 + FSR 原生 AA**（渲染 1920×1080），**主菜单**读数，最简场景为辅。离 60 上限远、无缩放策略差异。flags 须 `NETWORK_HEIGHT=auto` 或 `1080`（固定 900 会把 1920 宽缩小）。基准：wave-owned 与 Daniel 0.4.0 均为主菜单 47～48 / 最简场景 51～52。 **按 F8 切 EXACT 再读**（剑星 flags 默认 `DLSS5_VIT_ADAPTIVE=1`，静止主菜单会复用 ViT 白赚约 3 帧；与 Daniel 对比必须 EXACT）。 09-27 Zero 重申：以后日常效能测试一律 1080P 窗口 + FSR 原生 AA；《匹诺曹的谎言》1080P 最低画质也贴 58～59（疑似 60 上限，未查），不作对比用；**日常只用剑星**（50 帧上下最敏感）。匹诺曹现装 0.32 + PACK8 模块，备份 `D:\DLSSNR-Lab\liesofp-fresh-032\backup-20260927-081126`。
- 上条09-26/27游戏标准是历史口径：09-28起1080P已触60Hz上限，改用**2K（2560×1440）原生AA、F8 EXACT**作游戏对比标尺（当时52～53fps）；当前必须同机位/同配置且避锁帧上限。
- 派生测试脚本后先 grep runner 名；对照组没变化先怀疑脚本（09-16 两个假 null 就是这么来的）。
- RGP 捕获会改时钟，旧mixed捕获曾改输出；新capture须各自raw门；RGP 里的 HLSL ELF 按占位哈希缓存，要先用 `dxilhash.py` 签名。

**数值**
- HLSL `round()` 是 RNE；`f32tof16` / D3D 驱动输出 surface 是朝零截断。
- FMA 收缩和上下文相关，凡要逐位的尾链两边都显式 `precise`。
- 单点候选能修一个反例，全幅上反而可能更差，别拿单点匹配去推广舍入规则。
- 单层高相关不等于多层稳定，必须做完整累计门。
- 硬件 E4M3 Cast 不饱和，所有无界值转换前都要 clamp ±448。

**GPU / D3D12 / HIP**
- 2 的幂行步长会让内存通道撞车；旧DX12实现单 command list 塞整网曾 DEVICE_HUNG，当时改分块提交加 fence（不能用作当前同161 HIP外层图已失败的证据）。
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

- **目录**：生产宿主 `src/`、生产内核 `hip/`（`build-modules.ps1`（早期24行，0.41为38行/架构；当前新增行查实际配方），默认双架构，`SHA256SUMS`）、DX12 shader `shaders/`、打包与配置 `scripts/`；实验在 `Development/HIP/experiments/`，结果在 `Development/results/`，RE9 前置宿主在 `Development/RE9/presr/`（只入库版本锁定 + 补丁 + 脚本）。
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

### 后续校准与强制守门

- GPU实验用户已授权agent自主执行，无需让用户手跑；主进程调度/审交账。所有GPU/远程RTC单队列原子gpu.lock，game-check实际检查、15秒看门狗、实际写盘空闲≥100GB；不杀/抢运行游戏。SSH MainWindowHandle常为0，game-check以启动60秒/显存≥200MB等兜底排僵尸。观察超时继续poll同handle，不能重启刷批。
- 逐位仅相对明确基线：exact0.01是NV裁判；09-28 floatFMA/10-03FAST1已改数学，新gold不等NVexact。全71/Style/geometry/seed/history/模块实际命中均须receipt，requested flag不等active。
- 新规撤销0.5%/0.1ms门槛：同批平均收益、p99与另一档无退化，再实际正确性/兼容；gfx1200只有编译通常不称真机证据。有损必须明确选项/误差与画质基准，用户定观感。
- O2 Windows benchmark steady_clock实际gettimeofday是**1µs量化**，不是QPC100ns。H900 R3 p99+.14µs为真实分位数插值、非浮点bug；控漂35–48µs、三轮pooled−74.58µs，root裁“分辨率内未分辨退步”后进兼容，原数不抹。不能用此放过1152+73.99µs或任意临时容差。
- NET timing首次GetTimings可lazy-enable。APP主测NET0禁GetTimings/GetStatus/Poll/SetTag及SPAN/GAME_PROBE/FRAME_STATS；**原post_signal_query、正常输入poll/同步策略保持**，不冒零HIP APIs。诊断需要真实frame/tag/ready一致；tag0不去重也不阻止刷新，最近完成值不必当帧，不能用不匹配NET尾解释wall因果。
- Fn cachemiss/moduleLoad/Upload若发生在HIP等尚未提交producer之后可能同步死锁。构造/安全热切预热资源，测试需真实未提交producer gate；多遍首帧双feed缺失已实证，不能再归moduleLoad猜测。
- ownerdevice/TLS明确：Bridge每Enq恢复hipSetDevice，cleanup/warm也选owner；真实SubmitPulseLease与HIPops故障注入测试，不以模型替实际helper。Create/Record失败局部关pulse继续旧NN尝试；已提交lease drain后才destroy；失败保整个owner不半释放。多HIP设备故障恢复未证。
- 性能只读首末；正确性逐帧另外跑。正常“19组”是14静态/动/history+AE CSV+4真实回绕，必须roll计数实际>0；宏DIAGNOSTICS0时env不能假开，ghost缺module/合法缺一个export需loadstatus0证明回落。
- 编译recipe必须含RowOpts/PrebuiltDir/compiler/l23defines，空opts与实际stock三sections核对；文件名/target标签不证明ISA。旧rtc实际gfx1200产gfx1201事故，0.41重新核76ELF；0.41-a修27错target不从现装盲复制。MinGW钉imagebase+去时间戳，cuid差需代码/metadata核不凭wholeSHA。
- 裸s_barrier不是内存栅栏：换LLVM22/23前HIP_BARRIER_FENCE1并barrier_scan，LDS写需wait/WG fence；当前已验活跃核与未派发fallback分开，不能只说加栅栏就更快。
- 配置优先级 **系统环境 > native-game-flags > custom-config > default-config > 内置默认**；同文件最后值覆盖，空值回内置默认。环境首次快照，升级不能覆盖custom/native。F9真实热路径写custom；native有MP同步改，环境有MP拒文件改。RE9需重启，white-list范围见当前配置文档。
- AE500ms墙钟idle reset会变决策CSV，harness停顿可误当数值差；验证idle两侧同钉。AE/EXACT只指复用，不表示NVexact/FAST0/真3。
- 队列/导入缓存不能猜内部driver机制；共享导入池与TrackedResource解绑已修，自动pool归还不等GPU完成，PDL/SP引用/代际必须审。PDL回绕已修，但旧consumeracquire/跨非对齐tile invalidate等协议边界保留，不宣所有PDL已形式证明。
- shared branch **禁止amend**：f7a实验曾被竞态amend，root CAS恢复、doc独立commit；不静改旧数据，不删失败轮。外层ai-theorys-study不动，只本仓local（push/部署须具体授权）。

## 11. 尚未闭环的事项

实时优先序看WorkingPlan；以下是截至压缩时必须携带的未完边界：

- 建筑房顶闪烁仍第一优先，网友场景本人无法复现；跳ViT消反光不等保反光抑闪。缺实际连续color/MV/depth/jitter/exposure及同场景最终输出；真实capture尚无硬件使用/身份完整证明，0.41-a等网友反馈。
- Issue13 test20 p95=23.315485%，注exact pre-down后16.327520%；缺源码/commit/effectiveflags/moduleSHA，不套旧79cb。旧双帧14.94%仅对当时exact-reference近NV，不能归全部闪烁原模型。等待原pre-down四tile比对；Issue4多HIPdevice1仅作者验证，9070本机device0不能冒双卡证明。
- gfx1200硬件、720几何对NV oracle偏差（约28dB）、自由分辨率与原版900plan/完整动态history规则、MV缺失后reset/exposure/jitter/depth仍待真实合同。RE9/卧龙/Forza auto适配与通用host补丁没有全部实机覆盖；PR12/TheAutomatic拆改等外部事项看WorkingPlan。
- 0.41发布、0.41-a临时包、双游戏现装、local未安装H/Pulse新core是不同载荷，禁止混作已发布默认。APP图外层固定scope尚非生产长期graph，MP/AE/history/resize/hotkey/故障恢复不自动支持。
- 同640 pureNN仍差1.046285750ms，已收APP小刀和离线图收益不能相扣；fresh公平目标包括900/1080平均/p99、确定画质/游戏验证及复现产物，不以单微核或有利小窗算追平。
- ReplayAuto 162节点强DAG（161 kernel + 1 EventRecord7，position=24 `C256Down -> EventRecord -> C512 firstFFN`）已于2026-10-14完成ROCm 7.0.2 CLR捕获补丁、12帧gold 0-diff及3轮ABBA实测（pooled Δmean=−17.25µs、Δp99=+4.00µs，R3双退步，结论STOP），hipGraph各代在RX 9070 XT上全线封板（详§12 2026-10-14节与 `results/graph0-replay-auto-20261014/`）。

## 附：原始历史与结果索引

|入口|内容|
|---|---|
|history/DevHistory-full-20261006.md|本次363263字节原文；含截至6af产品STOP全部逐刀、失败/纠正/部署与SHA|
|history/DevHistory-full-20260923.md|前次593KB完整版，08-31～09-23逐块移植/fast链|
|history/porting-worklog.md / reverse-engineering-notes.md|最初6276行流水（2976后倒序）及逆向原始结论|
|history/CURRENT-STATE.md / amd-port-plan.md / fast-path-plan.md / next-steps-plan*.md / PLAN.md|各早期阶段状态/计划，不作现状裁判|
|history/README.md / native-runtime-contract.md / local-patch-tool.md / optiscaler-intro.md|早期索引与历史方案|
|results/<机制>-<日期>/README.md|每轮有效范围/原CSV/帧hash/ISA/配方/失败轮；名称可从完整原文rg检索|
|HIP/experiments/、deployments/、tools/release-*-results.json|隔离实现与复现、安装/回滚、发行逐文件台账|

## 12. 近期结论（截至2026-10-06；后续流水追加于此）

### 正式版本与当前默认

0.41三完整ZIP已发布，大小342904511/373102226/427749567字节，台账results/package-041-20261005；镜像夸克https://pan.quark.cn/s/dbda3e470f8f、Gofile https://gofile.io/d/YAENU0ex（上传未重下载核验）。发布后10-05三刀已安装双游戏：最终共享RGB直写省.022/.022/.133ms，pool64字节边省.012/.020/.043，1440FAST3队列省.089ms；**未回写发行ZIP**。随后H900/Pulse044f只本地代码/产物，未安装/改现装/玩家配置；临时0.41-a只regular实验包，独立台账，不混正式0.41。

默认全71（SKIP_BLOCKS空）、FAST_NUMERIC1、MP1/PREDICT1/SKIN0，只有MP3才两遍+预测第三遍，显式PRED0真三遍。FAST0参考保；NETWORK1080默认1152/1088有损可选、FREE_RES0、VIT_ADAPTIVE依宿主模板（F8 EXACT仅禁复用）。三个当前模板/sources是唯一默认值来源，不根据历史“已装”读数改玩家配置。RE9 STRENGTH文件0..1且需重启，addon/Magpie0..3且部分热载；DIRECT_IO输入直写/defaultPOST_SIGNAL_QUERY1已收，实验INPUT_POLL0/IO_FUSE0保持。

### 画质、history与0.41-a

09-30同公开TombRaider/NV Style0单帧：Style不齐发布原样24.06dB；齐Style全71=47.43dB，跳42/43/46=44.26；FAST1全71=47.55；M公开45.56。这是一帧显示域数字，不代表全场景/时序优劣。Style1源于原NV剑星live .0078125，默认1/可0,1,2。

网友111.mp4确建筑高光亮暗反复，撤回最初只看衣服/草地“静物稳定”。31–38都是完整ViT块，非奇偶shift/37特殊；跳任何一块仍闪、分组跳可抑部分、全跳反光消失是未独立复验干预，有损而非根因证明。

原5090 Issue13独立两帧受控合同：p95 NV14.937534% vsHIP14.943205%，一帧全同/另一44half值差；后续test20显示额外前端因素，不能泛归原模型。原pre-down四8×8tile→4×4×32真实FP8两C16平面已取并回帖，寄存器前投影无独立输出不伪造。Issue13 API final full1152和内部post有效1080域不混。results/issue13-original-oracle、issue13-prefix-20261004。

5090原history两Eval实证1920×1080RGBA16F，每帧8294400half与内部post输出0diff/alpha1；与API final约619万RGB half码不同。第二帧只清post+58/+60保prefix/input/seed，RGB3077213half变化，证明历史含有效post blend；先读旧history→另postsurface→回存同history，不必要pingpong。有限正RGB原HALF surface **RTZ**，zero/+1pixel RNE496差/RTZ0、亚像素RNE576差/RTZ0。原MP1实验RNE错误已修，**不是当前游戏闪烁的已证直接bug**。row6 gate恢复、两K16头/SIG/warp独立gold已过；严格SIG用NV表，不把跨vendor3ULP泛原版等价。

real-sequence v1采集CPU实现b439/adbb：Mode2 FFX-only六资源burst/copy扰时序、无同期NR闪输出，source_identity/effectiveflags/contract未核仍false、seed null不补造。seal/recipe/转换receipt SHA绑定原raw/config/shader/exe；upscale0是contextmax未知不猜1曝光/方向；坐标directUV避免pixel往返误差。受控synthetic replay只工具链/finite重复门，缺真实连续source不无限加合成。

0.41-a regular默认off `TEMPORAL_HISTORY_EXPERIMENT`，只FFX pre-upscale/MP1/AE0/SKIN0/graph0/overlap0支持；`MV_UNJITTERED=1`是用户显式实验前提不是检测证明。内部blendRTZalpha1，UV8Bcarrier固定、reset/resize/exposure变/gap/missingMVcold、warm不发布history、F9到MP3旧Body seed0并invalidate/回MP1cold。RE9/Magpie不假支持；门控参考1080成本约**+5.03ms**，不承诺质量/FPS。临时regular有实际改变，不包装旧041冒修复；正式041 ZIP/tag/本机双游戏配置未改。history-contract、temporal-replay-contract、history-trial-041a、temporal-package-review-20261006留合同/包说明；是否已交付及反馈看WorkingPlan。

### 本地收下的H900与single marker（未安装/发包）

H仅原布局rsqrt(sum0)每lane一次，两WMMA半平方归约/原math不变；prefix1517→1480指令、rsq16→2、资源LDS4096/VGPR128/occupancy16同。转置T先拿错QKV轴失败已保；实际raw恢复及8原语+prefix/fullNN0后进APP。900正式3轮均值−.044929/−.049248/−.045163ms，R3p99+.14µs按1µs测量门裁未分辨退步、pooled−74.58µs；1152p99+73.99µs止。H900 core25c2唯一row，FAST1/1600×960/MP1/nongraph/nonexp/完整26ABI才启；19真正回绕、seed、正常25缺单feature整体fallback、MP2/3/FAST0/真F9 1H→3base→1H/raw0，正式host/模块机器码同已测。c32-norm-hoist-compat/stat-audit。

Pulse单timed event在**ordered C256 Down后、C512首块前**：累计pdl_calls不是pending，site普通Run/current_anyorderfalse才放行，900保持原PDL1、1152实际0。Create/Record失败关事件继续旧NN尝试，leaseowner/drain销毁有实际CPU注入门。旧safe900无Record只是fallback；旧O1NET1诊断与actualO2NET0不得互代。

true APP两档各3ABBA320弃80均值/p99门过：900三均值−.081840/−.122750/−.121227，1152−.090679/−.113254/−.105258ms；pooled900−.108606/p99−.15644，1152−.103064/−.04322。3840candidate Record成功/counters一致/raw0finite；不含H叠加。production044f core Auto2只gfx1201/WindowsRuntime7/driver32.0.31007.2048/actualcaps/实测full71 MP1 AE0 nongraph/noactivehistory/orderedsite，0关/1显式optin未测driver要日志，不放gfx1200。caps误必需optionalSPinitpair导致auto0(11516)修后6904真Record；10slot32413 exit1是stdout resize parser错、CPU30raw与900→1152→900资源证据闭账，保原exit1；MP1 PRED1实际inactive被误拒256修effective(MP3&&predict)，8696两5帧真AutoRecord5/raw0，MP2/3仍mask128。production-pulse结果锁source/产物，用户cfg/已装/包未动。

正式041原ZIP/tag523723fd、38模块+461codec/weights/HDR锁，当前044f四cell首实测：900MP1 H+P mean−.200425/p99−.16323；1152MP1 H0/P真Record160 −.18551875/−.13871；900/1152MP3 H/P均fallback −.04396875/−.03935与−.071875/−.03256ms。全raw0finite，**各仅一次首ABBA**，MP3弱/漂移不宣稳定；旧78164P0仅currentfallback数据保留，不偷当组合。combined-vs041-20261006/9b1eeaf3。

### 外层同工作图：有效证据与产品替换STOP

隔离opt.graphfalse原Enqueue、PDL0/pulse0/MP1/AE0/historyoff/seed0，不用生产旧Graph路线。捕前warm缓存/weights/pool/plans全持、拒新alloc/upload/Fn/planmiss和SP诊断/回绕；官方Params64B/type0，真实161Fn强唯一Kahn/160edges，不只multiset。失败EndCapture一次/IsCapturingNone，ownerdrain后销exec/graph，异常uncertainty直接_Exit不析半lease。

单72596 instantiate/replay1/poisonraw153ac0；bounded74023三same+一同GPUinputptr更改R棋盘，独立ReferenceNet raw7fe026同且异initial，CapturedNet捕后无Enqueue。这只是固定seed/shape/nongraphscope，不自动长期更新MP/AE/history/rollover。

pure同caller80warm160measure三必要轮4efbcf46各合并mean/p99正、pooledGPU−.090802/p99−.130112、wall−.100691/−.098590ms；每slotGraph241、161DAG/rawfinite。setup共eagerfirst+80warm+proof/捕获instantiate成本单列，**共3readbacks**（common1+selectedfirstlast2）全steady外。不能从M1.046差简单扣它。

APP原Frame first70977仅1seed0warm后NewGuard拒，新修≤80warm连续2无实际malloc/cache变/Fn同且warmOutReleased。**Runtime seed1Prepare并未在NativeFrame执行**，SPinitial160不是kernel数，实际NN仍161。修后19487稳定warm4（209→213→213→213malloc/pool23→27→27→27）、161DAG160、12HDRseq2/F16raw0finite、稳定mappedinput/output与originalcodec/interop；原prewait/外signal/query/FrameDrain图外，graph销毁先imports。

APP pulse0对pulse0 f7a48855三轮合并mean/p99过：pooledwall−.070848/p99−.09415ms、24F16raw0finite、Graph160。R2/R3 **B2自身p99退步保留**，candidate max11.745>control10.182，不宣全slot尾好/未来不卡顿。160total弃80、首末2readbacks计时外、原NET0/PDL0/codec/postSignalQuery不改、steady无Fnvector/lastFn/log/新getter。这是原limited off/off，不是产品Auto替换通过。

产品EagerAuto2 vsReplayOff0先12gold ee3919ba：Auto实际Create1/drivercaps1/RecordAccepted15=warm4+12−1/cleanup1，OffGraph12/事件0、F16同。quiet后验移setup/final保nativePulse原guard，原O2benchmark字节同。53360/6afdb3ba actualAuto163 vsOffGraph160、8rawsamefinite/计时有效；**Replay mean +.006975ms**、p99−.0451，controlmean drift+.013213。收益门未达**首筛STOP，不formal/重刷/defaultGraph**；漂移更大也不能断言普遍慢。pure/APP/marker旧数字不可相加兜mean。

当前ReplayAuto仅CPU组合候选（cd0534bb已O2编译，源审尚未完成，无GPU）：官方EventRecord=7/GetEvent存在，需设备导出/161kernel+1ownedEvent/162强DAG精确Down→Event→C512位置、事件handle真实lease匹配；graph/exec先销再leaseClose/import释放；每帧host资格/key检查不能靠捕获后staleDown字段。capture时CPU Record不算GPU marker执行N。sourcegold审后才另排probe，目前未GPU/收益/生产接受。详graph0-product-baseline-review/replay-auto-minimal-review与新source结果，旧替换失败保留。


### 2026-10-07 PR12 Fast History 默认关闭合入

锁定 TheAutomatic PR12 `3ef0b6e`，本地merge保作者祖先与现H900/Pulse/实验记录。新增 `DLSS5_FAST_HISTORY=0` 默认关闭，用户可在custom-config显式开启并重启；仅FFX pre/MP1/fullviewport/graph0/overlap0、真实无抖动MV及显式depth方向，reference实验互斥。额外row与匹配C32 logit exports必需，Magpie/RE9独立runtime不支持。F32反馈/有效性启发式是近似，不声称NV/041a逐位等价、游戏抑闪已修或借reference耗时。

修latest接口拓宽导致addon漏MP1 startup/F9防线（genericaux MP1/2/3保留），修PR普通RGBinput首次漏UAV→SRV默认回归；未知padding/display MV有效区failclosed。源policy/首状态检查与最终MinGW runtime77426通过。实际1392 MSVC/WARP/9070接口套件PASS、14793受影响addon补检PASS，codec默认输出对297b字节同；未做完整NN aux资产/真实游戏/性能/gfx1200硬件门。1392在最后RGB修前，14793单列不伪全套重跑。results/pr12-integration-20261007留receipt/日志。0.41/041a包、本机DLL与游戏配置不改。


### 2026-10-07 AMD低频时域模式与用户试装

独立实现SAOG0721/Magpie2fce算法规格（未copy GPL表达，非clean-room声称），codec编码域r=最终NR−同帧original，2×2半精低观察/颜色引导low，仅low做约80ms EMA/high保本帧，不回NN前端。统一TEMPORAL_MODE0/1/2默认0；1原TheAutomatic、2本算法；显式MODE含0优先旧FAST_HISTORY，reference诊断拒叠。未知MV为明确static颜色门控，不假有效零MV，不猜重复capture；初版同步owner安全优先，成本/观感未知可能拖影。

实际数学20385 WARP/9070与FFX无声明metadata连续Past1门通过；Runtime68326 mode0对旧634输出字节同、mode2cold/reset同/allfinite，稳定warm也same不伪效果。Addon PR12 wideifstream portability前置fail修c_str后52532exit0，final7383045e；Runtime0f3c3aec。用户授权22724备份后部署剑星/鬼武者Mode2 MP1 PRED0（根/storage两runtime读回），其余AE/strength/guide声明等保、default/78modules/SUMS不变，backup lowfreq-deploy-20261007/backups/20261007-234253。无正式ZIP/tag/push，等待实玩反馈，不宣抑闪已解决。owned结果目录留合同/日志/哈希。

### 2026-10-08 仅剑星模式3试装

用户明确授权仅《剑星》试用模式3。依据源码e8fa0661、数学fixture90050 WARP/9070 PASS与正式O2 addon5768，安装前实查游戏/RTC未运行、D盘余量约383GB，原子锁下备份原模式1后更新addon为4ceef29f，并设置MODE3 / ENHANCE_STRENGTH1 / MP1 / PRED0，FAST_HISTORY与reference保持0。模式3单NN后增强编码域低/高残差，冷帧也增强，新增增量软压缩与统一RGB余量保护；不是两遍NN等价或更近NV的证据。

真实载荷/配置读回通过：剑星custom SHA4bdb8c3a，default181171BA与native2226F311逐字保持；AE/SKIN/strength/geometry及全部79模块、SUMSC08125C2、row77c745保持。不写MV/depth未知声明，未声明时明确静态回退。鬼武者完全未改：根/_storage_ runtime仍3eaed204、MODE1 / MP1 / PRED0，配置及SUMS原SHA同。备份与rollback：D:\DLSSNR-Lab\mode3-stellar-deploy-20261008\backups\20261008-112330。

安装命令实际exit0/LOCK_RELEASED；未代启动游戏、未测FPS或观感，也未先跑完整NN模式3门（receipt明确数学fixture范围）。末只读核验时用户已自行启动剑星PID19776，之后未写文件/取锁/跑GPU干扰。正式0.41包/tag不动。完整安装前后manifest/模块身份/脚本见results/mode3-stellar-deploy-20261008。

### 2026-10-09 原生D3D12有界四阶段原型

复用旧NativeVitBlock的expand/contract/QKV/attention，明确fusedFFN0、原weights/immutableactual640×1024F32输入。输入012df8bc来自controlled encoded1152 gradient跑旧full71→NativeVitGather/vit0.Input，不是真游戏/HDR菜单。GPU15399实际4个list对1个list四stage，4/4hostRecordStage计数、outputbit0/finite，654815/655360非零、范围[-18,18]（内部域）。53213仅补单pair计时后GPU76655exit0，warm/timed两路仍对原goldc6e798bit0，读取在计时外。separate GPUouter.57816/wall.659ms；batch GPUouter.52192/wall.788ms：GPU-.05624但wall+.129，单pair加对称两markerlist，不判收益/不刷/不相加外推fullNN或FPS。保stage内部barrier/fence与异常drain前资源持有，不绕wholeRecordUnsubmitted。

APP -DCOMPARE_HLSL私有caller21603CPU过，源57NNshader匹配旧manifest、8codec/UI/temporal有差异仅实际codec私有覆盖；VitFP8当前DXC同recipe重编CSO4a28227a与旧全文件同。旧prefix root实际选择bug已在私有copy包括prefix_wave修，未修改生产main。APP未GPU跑，不重复主pure已发现的missingnoise/1088tailguard失败。旧HLSL数学不同HIPFAST1，预览驱动/Agility环境支持不等可发布后端提速。原型/receipt见results/nr-dx12-fourstage-20261009与nr-dx12-legacy-app-20261009，未安装/玩家配置/发行默认变动。

### 2026-10-09 legacyD3D12完整APP首帧集成通过

独立APP29430 actualexit0/LOCK_RELEASED，只一帧旧真实HDR菜单FA3C1873（1296×720 RGBA16F），358dc私有rootfixed原COMPARE_HLSL caller，actualgeometry1920×1152/valid1920×1080、Network70VitTokens参数640。完整71/MP1AE0history0Style1seed0，private5codec/input源覆盖+缺失noise补齐，source选NativeActualNetwork70而非HIP类；没有采集processmodule枚举，不伪“actualNoHIPDLLloaded”。首RGBA16F输出7464960B，SHA740c534212ce0be989d38d764608c98e520e5fa08734034f9373347a67dd004f，全finite、RGB2796326非零/2799360、range[-0,29.9375]。首wall51.414ms是冷集成数据，不作为速度、不formal/复刷。只证端到端能跑，不证画质或加速；旧HLSL数学/noise组织与当前HIP不同，主purelegacy约23.39ms/freshHIP约9.17ms不能称纯backend提升。脚本/input/raw身份及日志见results/nr-dx12-legacy-app-20261009/app-receipt.json。未安装/改游戏/执行Daniel；GPU归还主队列，原raw不入git。

### 2026-10-11 剑星 1x 模式3试用与F9修复部署

用户明确要求本机剑星试用1x高低频增强。只读确认游戏/RTC均退出、无GPU锁，已安装仍为旧4ceef29f；custom/native有效配置原本就是MODE3/ENHANCE1/MP1/PRED0，User/Machine/Process未发现DLSS5环境覆盖。ea11历史O2产物窄查未找到，故用生产main a9b7a521、现有third_party和正式scripts/build-addon.sh --hip重编；8521 exit0，新addon SHA9413682f3185d1e9327ce7ce76be96ee88aad794f528799152ebaa3f2a7f85f3。三个F9 guard源码SHA与已验temporal-hotkey-guard-20261008完全一致：MODE1/2/3消费F9按键边沿但在改配置前拒绝切多遍。

82751实际部署exit0/LOCK_RELEASED，只替换剑星dlss5-amd.addon64并对必要键做幂等写入；有效读回TEMPORAL_MODE3、TEMPORAL_ENHANCE_STRENGTH1、MULTI_PASS1、PREDICT0、FAST_HISTORY0、旧TEMPORAL_HISTORY_EXPERIMENT0。三个配置文件最终逐字节未变，因此玩家AE/基础strength/几何等原样保留；79个hsaco、SUMS与64B history row逐SHA保留，未写鬼武者或Runtime DLL。未代启游戏、未跑GPU性能/视觉实验、未发布ZIP/tag/push。Mode3已部署不等于已观察到实际处理帧或画质提升，等玩家启动反馈。

备份/回滚：D:\DLSSNR-Lab\mode3-stellar-1x-trial-20261011\backups\20261011-114301\rollback.ps1（退出游戏后执行，校验原SHA并持atomic锁）。本地完整receipt/manifest/after/effective-config/modules与回滚脚本见results/mode3-stellar-1x-trial-20261011。

### 2026-10-11：MODE3/s1 与普通NN输出的客观图像对照

复核旧真实HDR菜单的同一次NN前后、同decoder配对raw（历史30243/314c573c），当前低频host/shader、输入/codec源码逐字同，当前installed79 HIP模块逐SHA同。编码残差MAE .01868645→.02713170（约+45.2%）；共同预览映射RGB MAE约4.51/255。正常全图相近，局部脸/发更暗、边缘/衣料反差和色彩变化可辨；不证明新细节或更好画质。当前剑星NR成功/MP1日志另行确认，旧菜单不冒本次2560×1440实玩抓帧。仅CPU统计与真实图，无新GPU/游戏改动；报告与四局部图见 results/mode3-objective-20261011/README.md。

### 2026-10-11 剑星恢复普通MODE0作帧率对照

用户感到MODE3帧率约54，要求恢复默认再试。a14232实际配置操作exit0/LOCK_RELEASED，写时游戏已退出，无需代kill。只将custom-config.txt的DLSS5_TEMPORAL_MODE=3改为0（本地before/after SHA证明仅1字节），default/native配置SHA不变；有效MP1/PRED0/FAST_HISTORY0/旧history实验0保持。增强强度1保留，MODE0不执行增强。DLL9413682f、全部内核和玩家AE/基础strength/几何不改，鬼武者不动；未执行任何GPU/帧率实验或发布。实际帧率待用户启动反馈，不把54归因为本次已测差值。完整小receipt与回滚路径见results/stellar-mode0-restore-20261011；远端备份D:\DLSSNR-Lab\stellar-mode0-restore-20261011\backups\20261011-144303。


## 2026-10-12 ViT contract→QKV tile-ready：矩阵重叠成立，短 pair 净负，止

隔离 `vit-tile-ready` 从30040d4c派生，不改生产数学/AoS/权重/默认路径。每16token的16个contract列块agent release/acq_rel发布，QKV真实hipExtAnyOrderLaunch消费；fresh counters、agent acquire、统一WG失败合流、boundedwait。40675固定640token独立双原参考/顺序ready/双anyorder完整contract655360B与QKV1966080B均byte0/finite0/status0。77445用firstWMMA结果实际store完成后全局realtime标记，两anyorder有36/157WG在producer尾发布前已完成第一MMA，ordered为0，证明有效重叠。

traceoff pair ABBA10warm+20measure/slot：原72359因窗外D2H不对称仅保原证；唯一公平修正31969两variant共同小读回、候选必需reset计入GPU/wall，8边缘gold0。GPU均值70.043→95.770µs（+25.727µs/+36.7%），wall106.475→141.603µs；两B均慢于两A，控制漂移−13.198µs不足改变方向。样本仅40/variant，不据p99作稳态尾结论。不拆分归因reset/原子/缓存失效/等待成本。STOP，不进整Frame、不追加变体、不装机/push。初始不支持旧时钟builtin的编译失败与0.643ms诊断值保留。源码、终态、SHA、原CSV及独立审见 `HIP/experiments/vit-tile-ready/README.md`；仅实验文件及研究文档进入main，无发行功能变化。

### 2026-10-12：上界证明与权重紧凑存储结题

A C32 NoHi 实际七层获界、三层保旧；84738 Half原语、51888 mapped、72895完整Frame和25355 ABBA数值/路由门全部通过。Frame wall9.6336771→9.6284083ms仅−5.269µs，小于控制漂移47.546µs；正确但没有可靠性能收益，停止，不进生产。证明/ISA/异常域与原日志保留 `HIP/experiments/c32-score-nohi/`。

B 八个已知ViT expand原16MiB容量只读4MiB packed前缀。77951单位与21471完整Frame字节0，真实八key权重容量省96MiB；48281四子进程全0，wrapper末JSON嵌套误判经只读PS/Python43模块SHA核同闭合，原exit1保留未刷。wall9.633050→9.6346917ms无稳定加速。集成狭域默认开启loader，未知布局/已有legacy cache回旧；不改kernel/ABI/精度，不改玩家DLL或发行包。容量不冒驻留峰值，详见 `HIP/experiments/packed-weight-compact/RESULTS.md`。

## 2026-10-12｜剑星紧凑权重本机试装（非发版）

用户授权后，以 `1b4c898f` 正式 `scripts/build-addon.sh --hip` O2 构建 addon `64dc04286c22d5d3…`；实际部署 `57883` exit0/锁释放，替换旧 `9413682f`。仅带已采纳的8个ViT expand紧凑权重路径；C32 NoHi未带入。写前游戏/RTC空，配置全部字节保持，实际MODE0/MP1/PRED0/SKIN0；79模块、SUMS、row和Runtime不变，鬼武者不动。备份与rollback、小回执见 `Development/results/stellar-compact-trial-20261012/`。未代启游戏、未测新帧率、未推送或发布。

### 2026-10-12 12:54｜用户剑星试用反馈

用户原话：“有提升~ 我通过splashtop剑星2k AA(1080P dlss5) exact 静态画面稳定在58.2帧”。对应已部署 addon `64dc04286c22d5d3…`（部署记录 `5574d8db`），普通MODE0/1x设置未改。这是用户实玩观察与主观提升反馈；旧版没有同场景精确对照，不能计算精确增幅；AA/Exact按用户原称记录，不推断实际GPU字段。未另改配置或运行实验。

### 2026-10-12：资源后续第一项，显式无history会话

Bridge公共接口unknown保旧，NativeFrame仅Off且无temporal config/rgb/experimental显式声明NeverHistory；DirectHistory/实验/其它历史会话保原路径，全temporal入口拒违反，不热分配。CPU九拒/空析构与正式O2通过。43364两freshprocess三帧gold0900/finite/Auto3/moduleidentity全过，history真实Width35389440→0，candidate创建/导入2、poolhit0；这是33.75MiB会话容量，不是假释放旧共享池/驻留峰值。36250完整FrameABBA wall9.6426208→9.6462167ms (+3.596µs)，p99−8.420µs，控制漂移26.392µs；无稳定加速/可辨明显回归，采纳资源改动，不部署。证据 `HIP/experiments/history-elision/` 与 `HIP/experiments/resource-followups/`。第一项结题后才开始C512分支未用tensor；第三项尚未实施。

### 2026-10-12：资源后续第二项止负

只将CompactC512Body未用mixed/contract New挪fallback，26032完整Framegold0且48calls/fallback0证明生效；但actual best-fit pool290571128→292742008B反增2.07MiB，driverfree仅+64KiB，未省资源。按门停止、不跑ABBA/不改池，撤回生产候选，仅保实验源码/receipt与失败总账；第一项NeverHistory保留。第三项在本次checkpoint后再开始。

### 2026-10-12：第三项contract活尾紧凑化采纳资源收益

只将原FP8矩阵后F32 scale尾从16MiB偏移搬到4MiB后，新export/带tag buffer成对，缺新Fn/legacy cache/未知selector保旧；单矩阵22077双old双new全gold0。79561八实例完整Frame0900/Auto3/44moduleidentity通过，weights423438120→322774824B净省96MiB，pool290571128/gather5242880同，driverfree+96MiB，非驻留峰值。67984单次ABBA480/arm mean9.65121875→9.642650ms(−8.569µs)，p99−42.690µs，控制漂移+46.1125µs；单max9.924→10.182ms保留，不宣稳定加速。作为资源优化采纳，实验source与原CSV持久，最终生产paired接口/recipe随后统一集成；contract checkpoint后才启动projection。

### 2026-10-12：projection及正式资源后续交付完成，未部署

projection仅FP8矩阵1MiB后scale尾从原4MiB地址搬到紧随矩阵，新export/ptr-layout配对；92358单矩阵4gold/finite/guard0。11819八实例完整Framegold0，在已开contract基线上weights322774824→297609000B实省24MiB，pool/gather/noise同；driverfree另录+32.5MiB，不把额外8.5MiB解释为峰值收益。30592一次ABBA mean9.658091667→9.656335417ms，p99−11.470µs、control drift+26.492µs，max10.068→10.255ms；无可靠加速，按资源收益采纳。

最终生产自然并入既有vit_stream源/recipe，两内部macro默认1、gfx1201/9070XT冷资格、未知module/selector/legacy cache成对旧路；无永久侧车/NR实验开关。joint94285模块da1b4ae9...旧86/候选87原始函数字节全同。正式Frame99541与addon30670 O2均0，正式路径66725无NR、两43模块旧877e/新da1b各3帧0900/finite/repeat/Auto3全过；新weights297609000对旧423438120净省120MiB。阶段2共享两New已恢复，既有expand96MiB保留。installed64dc与游戏MODE0/1x未动，本轮未部署/未push，0.41不改；后续是否试用由用户另指令。

### 2026-10-12：保真实Auto2的host timing有限结题

基于99fc33ba只生成私有宿主11个QPC点，原GPU命令/同步和Auto guard不改，coreNetwork头逐字同；43joint模块/current资源/0900/数值门95980通过。唯一31627四角色320弃80 A/A真实0/released，各Autoaccepted320/reject0/PDLcalls0。off9.6242125→on9.647700ms(+23.4875µs)，p99+4.75µs，控制漂移28.725µs，两B均高于两A，不能宣零开销或几微秒可靠归因，不重刷。

marked on480只报告主线程API经过时间：input录交.034ms、Network::Enqueue调用.354ms、output录交.020ms、原emptySubmit+Flush完成动作9.188ms；末者不是可删除CPU开销，也不等GPU执行/空档。无当前已验证且校时的外部GPU trace，内部stage/队列空泡不可辨；停止扩展，只建议后续先核实际GPU调度trace可行性，不据此派新优化。实验/CSV/身份/双审见 `HIP/experiments/auto-timeline/`。正式实现、installed64dc/游戏配置均未改，未部署/push。

### 2026-10-12：V末rendezvous候选正确，筛选漂移止损

只使WG-uniform part2/V跳过QKV末weight→norm复用rendezvous；COMGR8218/actualda86 ISA数据wait/MMA/资源与旧88Fn身份通过，94277完整1,966,080B双old双new gold/finite/canary0。唯一662786 QKV-only10warm20measure ABBA各GPU均值113.300/36.850/32.250/34.295µs，首末control漂移−79.005µs盖过pooled−39.2475µs；收益不可归因，STOP、保原样本、不改warm/刷轮/整Frame，不证明候选慢。研究源码不进产品；封存后才第二项prefix主输出cachehint。

### 2026-10-12：prefix main TH_STORE_NT正确，完整Frame收益未证

第二项只将arg4 main八store的TH置NT，arg5 down/width/scope/waits/math/资源全保；761984模块的唯一style PC重定位已解析同默认global1/128。18221完整main70,778,880B/down17,694,720B双old双new各SHA/gold/finite/guard0。初次68401私有host资格将Style1的默认sentinel−1误拒（candidate_calls0），原wrapper1保；仅host20047修精确默认资格，79532实际new3/stock0与baseline0/3、0900/Auto3/current资源门全过。

唯一完整生产Auto Frame89008四槽320弃80、480/arm：wall9.656520833→9.655660417ms（−0.8604µs），p99 9.88847→9.86799ms，max9.947→9.926ms；control漂移+26.408µs、B漂移+31.129µs，后B比后A慢1.5µs。改善不可与漂移分离，STOP未证稳定收益，不扫hint/重刷/正式集成。8firstlast0900、Autoaccepted320/reject0/PDLcalls0、实际NT320/stock320和44模块身份均通过。两候选均封存，产品源/已装64dc未变、未部署/push。证据 `HIP/experiments/two-kernel-trials/`。


### 2026-10-13：正式 0.42 三包交付与下载入口

按用户要求发布日常三包：`Magpie-DLSS5-AMD-0.42.zip`、`OptiScaler-DLSS5-AMD-0.42.zip`、`OptiScaler-REFramework-DLSS5-AMD-0.42.zip`，位于 `D:\給網友打包`，各附 `.zip.sha256`。载荷源码 `456b7e16`，构建及独立验收归档 `88472e96`；0.41 原包保留，0.41-a 仍只是临时测试包。

正式包纳入0.41后已采纳的数据流、限定范围的范数复用与Auto提交优化，以及可选MODE1/2/3、history资产和F9准入修复。默认MODE0/MP1/SKIN0，PRED1只在3x时使用两真实遍加预测；Magpie未接入新增时域消费者，具体支持范围见配置说明。已验范围内紧凑权重合计省216MiB分配容量（expand96+contract96+projection24），显式无history会话另省33.75MiB共享buffer；不是驻留峰值或新增FPS保证。V末屏障、prefix NT、NoHi、C512闲置分配及QPC诊断未进产品，D3D12研究不作为发行后端。

官方配方重新构建80模块（gfx1200/gfx1201各40，LLVM12+COMGR68），新tail导出进入正式vit-stream，无实验侧车。正式addon/runtime O2、gfx1201新40模块对旧43模块完整Frame逐字节0900/finite/Auto及资源门、各包44项shader编译、REFramework运行库smoke和ZIP逐文件回读全部通过，独立审计通过。gfx1200仅构建与结构核验，不宣实机性能；新tail主机资格仍保守回退。三包大小分别344087793、374285768、429238298字节；完整SHA、源/编译器清单与回执见 [0.42发布记录](releases/0.42/README.md) 和 `releases/0.42/results.json`。

用户随后提供公开下载入口：[夸克](https://pan.quark.cn/s/316704620069) · [Gofile](https://gofile.io/d/HNE20Ocp)，以 `490a1b5a` 补入中英文README/changelog；网盘内容未独立下载核验。`82227299` 将已发布细节归入唯一0.42节并恢复版本表旧→新顺序，`46e6c28c` 新增 [0.42更新与使用说明](../0.42-更新与使用说明.md)，后续使用说明修订 `f5e66999`。这些文档修订未重打已上传ZIP，其二进制和SHA保持不变。本轮没有替换用户游戏安装；最后确认的剑星仍为64dc/MODE0/1x。打包与文档任务未执行GitHub release/tag/push，不把网盘交付等同GitHub发布。

2026-10-14补记：用户发现开发史漏记，核对上述实际回执后补齐此条，并同步WorkingPlan当前发布状态；未重跑构建或修改发行包。

### 2026-10-14：ReplayAuto 162节点强DAG闭环与ABBA结题（STOP）

对 2026-10-06 留置的 CPU 隔离候选 `ReplayAuto`（`cd0534bb`，161 kernel + 1 timed EventRecord = 162 节点强 DAG）完成源码审计、ROCm 7.0.2 CLR 捕获修复、12 帧 gold 验证与 3 轮 4-slot ABBA 实测闭环（证据与补丁归档于 `HIP/experiments/graph0-replay-auto-20261006/` 与 [ReplayAuto结题报告](results/graph0-replay-auto-20261014/README.md)）：

1. **ROCm 7.0.2 CLR 流捕获 EventRecord 缺陷定位与修复**：
   - 未修复的 `cd0534bb`（`279984df...`）首次上机捕获时在 `if(n!=eager.size()+1)` 门禁触发 `REPLAY_AUTO_NATIVE_FAILURE reason=APP graph dynamic node count`（实际返回 `n=161`）。根因：`amdhip64_7.dll`（ROCm 7.0.2 CLR `hip_event.cpp:365-413`）在 stream capture 期间对默认 `hipEventRecord(event, stream)`（`flags=0`）直接走底层流记录并返回 `hipSuccess`，不会向捕获图插入 `GraphEventRecordNode`；而 `hipEventRecordWithFlags(..., hipEventRecordExternal=1)` 存在上游栈指针 `amd::GraphNode* pGraphNode;` 未初始化即传入 `addEventRecordNode`（检查 `*node != nullptr` 报 `hipErrorInvalidValue=1`）的 bug。
   - 在 `submit_pulse_hip.h::record_capture` 中通过 `hipEventRecord` + `hipStreamGetCaptureInfo_v2` + `hipGraphAddEventRecordNode(&evNode, cgraph, deps, 1, handle)`（零初始化 `evNode`）+ `hipStreamUpdateCaptureDependencies(*stream, &evNode, 1, hipStreamSetCaptureDependencies=1u)` 显式构建 `type=7` 节点并更新捕获依赖前沿。修复后编译产物 `graph0-replay-auto.exe` SHA256 为 `70a61794cd7a5566e02792ba2b990064cbeb5974d960992bec285e96ae7b3c6a`。
2. **162 节点唯一 Kahn 拓扑与 12 帧 HDR 序列 2 Gold（PASS）**：
   - 实机捕获验证通过：`nodes=162 edges=161 event_record_nodes=1 external_interop_nodes=0`，唯一拓扑序在 `position=24` 处满足 `mh_fast:mh_pool_project_group_c256 -> EventRecord(type=7, owned_event) -> c512_m32_deep:split_ffn_one_w2f8`，且 `hipGraphEventRecordNodeGetEvent` 返回句柄与 `pulse_lease` 私有单句柄严格一致。
   - 12 帧 `DLSS5_RESIDUAL_SEQUENCE=2` 成对 gold：`EagerAuto`（`warm=4, record_ok=15, accepted=15`）与 `ReplayAuto`（`warm=4, capture_record_calls=1, graph_launch_api_calls=12, host_RecordAccepted=5, valid=1, before_lease_destroy=1`）全部 12 帧 `.f16` 逐位完全一致（`0 bit-diff`，`nan_inf=0`）。
3. **3 轮 4-slot ABBA 稳态性能评估（STOP）**：
   - 160 帧/slot、弃前 80 帧、取后 80 帧稳态（每轮 160 帧/arm，3 轮共 480 帧/arm，`edges_only=1`，`NET0`）：
     - **Round 1**：`EagerAuto` mean `9.509006 ms` vs `ReplayAuto` mean `9.491825 ms`（`Δmean = -0.017181 ms` / `-17.18 µs`），`Δp99 = -0.000040 ms`（`-0.04 µs`），落在同臂 slot 漂移（`+11.84` / `+17.20 µs`）噪声带内。
     - **Round 2**：`EagerAuto` mean `9.553869 ms` vs `ReplayAuto` mean `9.518381 ms`（`Δmean = -0.035488 ms` / `-35.49 µs`），`Δp99 = -0.001740 ms`（`-1.74 µs`）；但 `B2`（`9.544663 ms`）慢于 `A1`（`9.538263 ms`），control/candidate 漂移达 `+31.21` / `+52.56 µs`。
     - **Round 3**：`EagerAuto` mean `9.561094 ms` vs `ReplayAuto` mean `9.562006 ms`（`Δmean = +0.000913 ms` / `+0.91 µs`），`EagerAuto` p99 `9.732920 ms` vs `ReplayAuto` p99 `9.761560 ms`（`Δp99 = +0.028640 ms` / `+28.64 µs`），均值与尾延迟双退步触发 **STOP**。
     - **3 轮合并（480 帧/arm）**：`EagerAuto` mean `9.541323 ms` → `ReplayAuto` mean `9.524071 ms`（**`Δmean = -0.017252 ms` / `-17.25 µs`，`-0.18%`**），`EagerAuto` p99 `9.732100 ms` → `ReplayAuto` p99 `9.736100 ms`（**`Δp99 = +0.004000 ms` / `+4.00 µs`**）。
4. **物理结论与封板**：将帧内 timed EventRecord 脉冲编入图第 24 号节点确实消除了无脉冲 `ReplayOff` 相对 `EagerAuto` 的 `+38 µs` 退步，但因 ROCm 7.0.2 `hipGraphLaunch` 在主机侧仍逐节点遍历发射 AQL 包且附加图执行包装开销，相对 `EagerAuto` 净均值仅差 `-17.25 µs`（处于 12～53 µs 漂移噪声内）且 p99 退步 `+4.00 µs`（R3 `+28.64 µs`）。判定 **STOP**，hipGraph 全家桶（旧 `opt.graph=1`、同工 `graph0`、`ReplayOff`、`ReplayAuto` 162 节点）在 RX 9070 XT 上彻底结题，生产默认维持 `EagerAuto`（`DLSS5_SUBMIT_PULSE=2, opt.graph=0`）。

### 2026-10-14：C512 W16（16B `global_load_b128` 权重分块）零额外显存采纳与 ViT / C256 边界审计

对 16 个 C512 块（`23..30` 与 `40..47`）除 `split_ffn_one_w2f8` 外的其余三个权重 fragment 消费核（`split_projection_frag`、`c512_qkv_attention_compact`、`mh_attention_project_frag_c512`）完成 `FragmentPackedMatrixW16`（16B `global_load_b128`）改造、零额外显存合并及 3 轮 4-slot ABBA 验证（详见 [C512 W16 报告](results/c512-w16-20261014/README.md)）：

1. **机制与零额外显存实现**：
   - 将每个 512B 权重块（`16 rows × 32 k`）由 `[h=0..1][gr=0..1][lane16][8B]` 改为 `FragmentPackedMatrixW16`（`[gr=0..1][lane16][h=0..1][8B]`），使同一线程相邻两个 16-k WMMA 的 8B B-fragment 连续排列为 16B，每 32-k 步由 2 条 `global_load_b64` 合为 1 条 `global_load_b128`（FFNProj/AttnProj 每 wave 权重 VMEM load `64 → 32`，QKV `192 → 96`），WMMA 累加顺序（`k=0, 16, ..., 496`）与舍入完全不变。
   - `PackedSplitProjectionFrag(name, w16)` 与 `PackedMhWeightQkvFrag(name, 512, w16)` 原地复用既有 `@proj-frag` 与 `@qkv-frag` 键，**零额外显存分配（`weights_count=184, weights_bytes=297,609,000 B`）**。
   - 默认启用 `HIP_C512_W16=2`（`u4`：`split_projection_frag_w16` 100 VGPR、`c512_qkv_attention_compact_w16` 187 VGPR、`mh_attention_project_frag_c512_w16_u4` 97 VGPR，均 `0 spill`；`HIP_C512_W16=1` 为 `u2` 81 VGPR，`DLSS5_HIP_C512_W16=0` 可回退旧核）。`gfx1200`/`gfx1201` 涉及的 12 个 `.hsaco` 模块已同步重编，保持 80 模块完整。
2. **完整 Frame Gold 与 3 轮 4-slot ABBA（PASS）**：
   - `1920×1152` 完整 Frame Gold（3 帧重复，`Auto` 脉冲 `record_ok=3 accepted=3 reject_mask=0`）：`baseline(0)`、`u2(1)`、`u4(2)` 首尾帧 SHA256 全为 `0900E489A397302B93074A878C694B9C3E7C3400CFE2DFDD3CC727ABE1050F4A`（`0 bit-diff`）。
   - `benchmark.exe` 独立 NN 微基准 `u4` 净降 `-0.088 ms`。完整 `frame.exe` 3 轮 4-slot ABBA（240 帧/slot，弃 80 取 160 稳态，960 帧/arm）：
     - **`u4`（`HIP_C512_W16=2`，默认）**：3 轮均值差分别为 `-14.1 µs`、`-31.1 µs`、`-4.9 µs`，**6/6 对偶比较全负（`< 0`）**；960 帧合并 mean `9.6070 → 9.5903 ms`（**`-16.7 µs`**）、median **`-14.5 µs`**、p90 **`-17.0 µs`**、p99 **`-5.0 µs`**。
     - **`u2`（`HIP_C512_W16=1`）**：3 轮均值差分别为 `-24.1 µs`、`-7.5 µs`、`-12.8 µs`（5/6 对偶为负）；960 帧合并 mean `9.5902 → 9.5754 ms`（**`-14.8 µs`**）、median **`-13.0 µs`**、p90 **`-27.0 µs`**、p99 **`-16.0 µs`**。
3. **伴随边界排查（不采纳，已记入 `failed-experiments.md`）**：
   - **ViT `compactw` W16**：`vit-stream-fast` 下 `vit_stream_contract_frag_bout_compactw_w16` VGPR `211 → 251`（逼近 256 上限），`vit_stream_project_n64_bb_compactw_w16` VGPR `161 → 220`（wave 占用由 3 waves/SIMD 掉至 2 waves/SIMD），且 ViT 最大的 QKV（`vit_stream_qkv_f8w_bin`）在 0.42 已是 W16，不合入。
   - **C256 `SpStage` 首尾层（`15, 22, 48, 55`）与 `sp_init`/`sp_recover`**：Encoder `15` 输入为 FP32（`Down(14)`）、`22` 输出需保 FP32 `Hrtz`（`PostMode=3` 供 `skips[3]` 与 `Down(22)`）；Decoder `48/55` 走 `c64-wave2-fast.hsaco`（LLVM 23，`W2_QKV_FUSE=3, W2_FFN_QT_BATCH=4, W2_HOIST_LOADS=15, W2_FAST_NUM=3`），而 1080p `SpStage(49,6)` 走 `swin-persistent.hsaco`（COMGR，`W2_QKV_FUSE=0, W2_FFN_QT_BATCH=2, W2_HOIST_LOADS=0, W2_FAST_NUM=0`），并入 `SpStage` 会改变算术合同（破坏 `0900E489...` Gold）、退化到更慢的单层 recipe 并多占 `4.42 MB` 持久缓冲；`sp_init_pair` 被 `CheckPulseCapabilities` 显式禁止（会关闭 `submit_pulse=auto`），`sp_recover256_w16` 是替代 CPU `hipStreamSynchronize` 的 `<1.5 µs` 异步超时守卫，保持不动。

### 2026-10-14：`NativeLowFrequencyTemporal`（MODE 2/3）64-Slot Ring 化与移除每帧 2 次 `r.submit.Flush()`

对 `DLSS5_TEMPORAL_MODE=2/3`（`NativeLowFrequencyTemporal`）的 D3D12 资源生命周期与队列同步进行审计并完成 64-slot ring 改造（详见 [low-temporal-ring 报告](results/low-temporal-ring-20261014/README.md)）：

1. **根因审计与 64-Slot Ring 设计**：
   - 此前 `NativeLowFrequencyTemporal` 仅持有一份 256B `control` 上传缓冲、一份 1-SRV `heap` 描述符堆与一份 `motionPin`，每次 `Prepare()` 在 CPU 侧执行 `heap.Reset(); CreateDescriptorHeap(...)` 并 `Map` 覆写 `control`，导致 `NativeGameFrame::ProcessSubmittedFrame` 在 `r.low_temporal.enabled` 时必须在 `Prepare()` 前与 `Submit()` 后各调一次 `r.submit.Flush()`（每帧 2 次全队列排空 + 1 次 D3D12 描述符堆销毁重建）。
   - GPU 侧中间缓冲（`difference`、`observed`、`observationGuide`）与乒乓历史（`band[0..1]`、`guide[0..1]`）均为同一 `r.submit.Queue()` 上按序执行并由显式 `NON_PIXEL_SHADER_RESOURCE <-> UNORDERED_ACCESS` barrier 保护的 `DEFAULT` 堆 UAV，无需跨槽复制。
   - 对齐 `NativeAddonFastHistory`（MODE 1），在 `src/native_low_frequency_temporal.h` 中引入 `std::array<Slot, 64> slots`（每槽独立持有 `control`、`motionPin`、`heap` 与 `UINT64 fence`，且槽内 `heap` 与 `control` 首次创建后持久复用，彻底消除每帧 `CreateDescriptorHeap`），新增 `Prepare(Submission& submit, ...)`（仅在 `s.fence > submit.Completed()` 时按需 `submit.Flush()`）与 `Submitted(UINT64 fenceValue)`，并在 `src/native_game_frame.h` 中移除 `r.low_temporal.enabled` 路径下的 2 次 `r.submit.Flush()`（支持 `NR_LOW_TEMPORAL_RING=0` 回退旧路径对比）。保留原 `Prepare(ID3D12CommandQueue*, ...)` / `Submitted(ID3D12CommandQueue*)` 重载供独立单元测试与 `LmxxfNrRuntime.cpp` 使用。
2. **单元测试、12 帧多帧 Gold 与 3 轮 4-slot ABBA 验证（PASS）**：
   - `test_low_frequency.exe 1`（MODE 2）与 `test_temporal_enhancement.exe 1`（MODE 3）全部 `PASS`。
   - 12 帧 `DLSS5_RESIDUAL_SEQUENCE=2` 多帧时序 Gold（`NR_LOW_TEMPORAL_FIXED_DT=0.016` 锁定帧间 `dt`）：在默认同步提交与 `DLSS5_TEST_ASYNC_SUBMIT=1` 延迟 ring 提交下，`NR_LOW_TEMPORAL_RING=0` 与 `NR_LOW_TEMPORAL_RING=1` 的 `MODE 2`（首帧 `0900E489...`、末帧 `30F1313D...`）和 `MODE 3`（首帧 `A7B0DDB8...`、末帧 `5008EF28...`）全部 **逐位一致（`0 bit-diff`）**。
   - 3 轮 4-slot ABBA（`MODE 3`，24 帧/slot，取后 20 帧稳态）：默认同步提交下 `ring=0` `10.779 ms` vs `ring=1` `10.776 ms`（`-0.003 ms`）；在延迟 ring 提交（`DLSS5_TEST_ASYNC_SUBMIT=1`）下消除每帧额外 `Flush()` + `CreateDescriptorHeap` 后，3 轮均值分别为 `-0.072 ms`、`-0.060 ms`、`-0.077 ms`（**3/3 轮全胜**），6-slot 稳态中位数均值由 `10.910 ms` 降至 `10.840 ms`（**`-0.070 ms / frame`**）。

### 2026-10-14：C512 Split-FFN W16（`split_ffn_one_w2f8_w16`，128 条合并 `global_load_b128` 替代 256 条跨步 `global_load_b64`）零额外显存采纳

对 `c512-m32-deep.hsaco` 中的 `split_ffn_one_w2f8` 进行 ISA 反汇编审计时发现，前序 `c512-w16-20261014` 误判该核已使用 16B W16 权重加载，实则仅其 FP32 输入 `ar` 使用了 `global_load_b128`，而 `PackedSplitFfnWeightMixFp8` 的三个 E4M3 权重区（`Mix [512][512]`、`Expand 8×[256][64]`、`Contract 8×[64][256]`）仍为未分块的 row-major 布局，每 wave 发出 **256 条跨步非合并 `global_load_b64`**（Mix 相邻 lane `rc()` 跨 `512B` 打散到 16 条不同 cache line，Expand 跨 `64B`，Contract 跨 `256B`）。完成 `FragmentPackedMatrixW16` 改造及 3 轮 4-slot ABBA 实机验证（详见 [C512 Split-FFN W16 报告](results/c512-ffn-w16-20261014/README.md)）：

1. **机制、ISA 与零额外显存实现**：
   - `PackedSplitFfnWeightMixFp8(name, w16)` 原地复用既有 `@split-mix-fp8` 缓存键，在 `w16=true` 时对 Mix（`FragmentPackedMatrixW16(v, 0, 512, 512)`）、Expand（`FragmentPackedMatrixW16(v, 262144, 2048, 64)`）和 Contract（`FragmentPackedMatrixW16(v, 393216, 512, 256)`）原地执行 W16 排列，**零额外显存分配（`weights_count=184, weights_bytes=297,609,000 B`）**。
   - 新增 `split_ffn_one_w2f8_w16`（`HIP_C512_FFN_W16=1`，默认开启）：Stage 1（Mix）按 `gs=0..15`（32-k 步）每 `j=0..3` 发 1 条 512B 全 wave 合并 `global_load_b128` 覆盖相邻两步 WMMA；Stage 2/3（Expand + Contract）按 `h2=0..7` 成对处理 `hb=2*h2, 2*h2+1`，预取 4 条 Contract `global_load_b128`（低 8B 供 `2*h2`、高 8B 供 `2*h2+1`），内层每 `hb` 发 2 条 Expand `global_load_b128`，WMMA 操作数与 K 累加顺序（`k=0, 16, ..., 496` 及 `hb=0..15`）完全不变。
   - `gfx1201` ISA 对比：`split_ffn_one_w2f8`（112 VGPR，256 条跨步 `global_load_b64` 权重加载）→ `split_ffn_one_w2f8_w16`（124 VGPR，Occupancy 10，`0 spill`，`0 LDS`，**0 条 `global_load_b64`**，**128 条 100% 合并 `global_load_b128` 权重加载**）。同步测试 LDS 合并写出的 `split_ffn_one_w2f8_w16_tvec`（`NR_C512_FFN_W16=2`，126 VGPR，2048B LDS）。
2. **完整 Frame Gold 与 3 轮 4-slot ABBA（PASS，`-0.1312 ms / frame`）**：
   - `1920×1152` 完整 Frame Gold（3 帧重复，`Auto` 脉冲 `record_ok=3 accepted=3 reject_mask=0`）：`baseline(0)`、`w16(1)`、`w16_tvec(2)` 首尾帧 SHA256 全为 `0900E489A397302B93074A878C694B9C3E7C3400CFE2DFDD3CC727ABE1050F4A`（`0 bit-diff`，`extra_vram_bytes=0`）。
   - 在已开启 `HIP_C512_W16=2` 的基线（`~9.59 ms`）之上执行完整 `frame.exe` 3 轮 4-slot ABBA（240 帧/slot，弃 80 取 160 稳态，960 帧/arm）：
     - **`w16`（`HIP_C512_FFN_W16=1`，默认）**：3 轮均值差分别为 **`-134.8 µs`**（`9.5473 → 9.4126 ms`）、**`-131.8 µs`**（`9.6127 → 9.4810 ms`）、**`-127.2 µs`**（`9.6179 → 9.4907 ms`），**3/3 轮全胜**；960 帧合并 mean `9.5926 → 9.4614 ms`（**`-0.1312 ms` / `-131.2 µs`，`-1.37%`**），median **`-0.1345 ms`**。
     - **`w16_tvec`（`NR_C512_FFN_W16=2`）**：3 轮均值差分别为 `-123.5 µs`、`-124.1 µs`、`-115.0 µs`（3/3 全胜，960 帧合并 **`-0.1209 ms`**），因 `32 × ds_store_b8 + wave_barrier + 2 × ds_load_b128` 额外开销比纯 `w16` 慢约 `10.3 µs`，保留为可选模式 `2`，生产默认采用 `HIP_C512_FFN_W16=1`。

### 2026-10-14：C32 Wave1 W16（`c32_wave1_*_w16`，30 条合并 `global_load_b128` 替代 88 条跨步/窄 `global_load_b64`）零额外显存采纳与 C256 Attention W16 边界排查

继续沿“消除未合并跨步 `global_load_b64` 权重加载”脉络对 C32 `wave1`（每帧 10 次发射、`8,100 ~ 32,400` WG、`0.78 ms`）与 C256 Swin Attention 进行审计与实机验证（详见 [C32 Wave1 W16 报告](results/c32-w16-20261014/README.md)）：

1. **机制、ISA 与零额外显存实现**：
   - `PackedC32Weight` 原先仅将 C32 FFN（`Expand [128][32]`、`Contract [32][128]`）与 Attention（`QKV [96][32]`、`OutProj [32][32]`）量化为未分块 row-major E4M3，导致 `cw_matrix8` 每 wave 发出 **76 条跨 32B/128B 的非合并 `global_load_b64`** 加 12 条对角残差 `global_load_b64`（共 88 条 `global_load_b64`）。
   - 在 `HIP_C32_W16=1`（默认开启，支持 `NR_C32_W16` / `DLSS5_HIP_C32_W16` 覆写）下，`PackedC32Weight` 原地复用既有 `@c32fp8` 缓存键对 4 个 FP8 权重矩阵执行 `FragmentPackedMatrixW16` 并通过 `AppendC32ResidualDiagonalsW16` 将每个 `(part, ci)` 的 `kt=0,1` 对角片合并为单个 512B W16 tile，**零额外显存分配（`weights_count=184, weights_bytes=297,609,000 B`）**。
   - `hip/wave_owned_c32.inc` 新增 `cw_matrix16` 与 `W16=1`（`c32_wave1_*_w16`）：FFN Expand + Contract 按 `h2=0..3` 成对步进（常数立即数偏移 `offset:2048/2560/18432/20480`），对角残差、QKV 与 OutProj 全部改为 512B 全 wave 100% 合并 `global_load_b128`，WMMA 累加顺序完全不变。`c32_wave1_chain`（126 VGPR，88 条 `global_load_b64`）→ `c32_wave1_chain_w16`（136 VGPR，`0 spill`，**0 条 `global_load_b64`**，**30 条合并 `global_load_b128` 权重加载**）。保留原 26 个导出符号供 `CheckPulseCapabilities()` 校验。
2. **完整 Frame Gold 与 3 轮 4-slot ABBA（PASS，`-0.0240 ms` mean / `-0.0328 ms` median）**：
   - `1920×1152` 完整 Frame Gold（3 帧重复，`Auto` 脉冲 `record_ok=3 accepted=3 reject_mask=0`）：`baseline(0)`、`w16(1)`、`w16h(2)` 首尾帧 SHA256 全为 `0900E489A397302B93074A878C694B9C3E7C3400CFE2DFDD3CC727ABE1050F4A`（`0 bit-diff`，`extra_vram_bytes=0`）。
   - 在已开启 `HIP_C512_W16=2` 与 `HIP_C512_FFN_W16=1` 的基线（`~9.47 ms`）之上执行完整 `frame.exe` 3 轮 4-slot ABBA（240 帧/slot，弃 80 取 160 稳态，960 帧/arm）：
     - **`w16`（`HIP_C32_W16=1`，默认）**：3 轮均值差分别为 **`-27.4 µs`**（`9.4440 → 9.4166 ms`，median `-42.0 µs`）、**`-19.9 µs`**（`9.4712 → 9.4513 ms`，median `-22.5 µs`）、**`-24.6 µs`**（`9.4999 → 9.4753 ms`，median `-34.0 µs`），**3/3 轮全胜**；960 帧合并 mean `9.4717 → 9.4477 ms`（**`-0.0240 ms` / `-24.0 µs`**），median `9.4707 → 9.4378 ms`（**`-0.0328 ms` / `-32.8 µs`**，单槽 median 最低达 `9.3900 ms`）。与同日 C512 Split-FFN W16（`-0.1312 ms` mean / `-0.1345 ms` median）合计净降 **`-0.1552 ms` mean / `-0.1673 ms` median**。
     - **`w16h`（`NR_C32_W16=2`，Contract 保 8B Frag）**：单轮 ABBA 退步 `+28.5 µs`（因在 `ht=0..7` 循环内索引 8B Contract 引入 128 条动态移位 ALU 指令与 64 条 `global_load_b64`），不采用。
3. **伴随 C256 Swin Attention W16 实测排查（中性，不合入）**：
   - 将 `WaveOwnedAttentionWeight(name, 256)` 由 8B `FragmentPackedMatrix` 改为 16B `FragmentPackedMatrixW16`（`c256_wave2*_w16a`、`sp_run256_w16a`，`vgpr=184~189, scratch=0, b64: 433 → 17, b128: 46 → 254`），1920×1152 Gold 逐位一致（`0900E489...`），但 3 轮 4-slot ABBA 均值差为 `-12.0 µs`、`-3.6 µs`、`+18.0 µs`（3 轮合并 `+0.8 µs`，打平）：因 C256 已有 8B `FragmentPackedMatrix`（256B 半合并读取）且 8-wave WG 受 `w2_sync()` 跨 wave LDS 屏障主导，16B 合并未能缩短关键路径，已回退不合入。

### 2026-10-14：ViT 核心三算子 W16 + 外层分片循环禁展开（`HIP_VIT_W16=7`，Contract/Proj VGPR 削减 `-96`/`-76` 跨档提 Occupancy）零额外显存采纳与 C64/C128/C512-QKV 审计

对 C64/C128 `wave2`、`c512_qkv_attention_compact_w16` 与 ViT 核心算子（blocks `31..38`，共 `1.204 ms`）执行 ISA 与寄存器/驻留悬崖全面审计并完成实机验证（详见 [ViT W16 报告](results/vit-w16-20261014/README.md)）：

1. **前置审计结论（C64/C128 `wave2` 与 `c512_qkv_attention_compact_w16`）**：
   - **C64/C128 `wave2`（`c64-wave2-fast.hsaco`）**：`W2_QKV_FUSE=3`、`W2_FFN_QT_BATCH=4`、`W2_UP_LOW_BYTES=1`、`W2_DOWN_HALF=1`、`W2_UP_VEC=1` 已全开；`W2_FFN_W16_SMALL` 先前已在 `w16-c64-c128-20260930` 与 `bitexact-pm-20261001` 实测证伪（`-1 ~ -7 µs` 噪声，因 `64 KB / 256 KB` 权重常驻 L2/MALL 且被 4-wave WG 屏障掩盖）。
   - **`c512_qkv_attention_compact_w16`（`187 VGPR`）**：在 `gfx1201` 上 `161..192 VGPR` 同属 `8 waves/SIMD` 档，且每 wave `3072 B` LDS（`6144 B / 2-wave WG`）已将硬件驻留硬封顶在 `5 waves/SIMD`（`20 waves/CU`）；调小预取深度 `D=3, 2, 1` 的 VGPR 为 `183, 179, 171`，不改变 VGPR 档位与 LDS 封顶，仅减少预取深度。
   - **ViT 核心算子根因定位**：`vit_stream_qkv_frag_bin_w5f8` 已用 16B `vit_frag_matrix_w16`（`114 VGPR`，`10 waves/SIMD`）；但 `vit_expand_blocked_fp8_frag_bytein`（每帧 24 次调用，每 wave 128 条 `global_load_b64`）、`vit_stream_contract_frag_bout_compactw`（每帧 8 次调用，每 wave 256 条 `global_load_b64`，**`211 VGPR` = `6 waves/SIMD` 悬崖**）与 `vit_stream_project_n64_bb_compactw`（每帧 1 次调用，每 wave 128 条 `global_load_b64`，**`190 VGPR` = `6 waves/SIMD` 悬崖**）仍为 8B `FragmentPackedMatrix`。此前 `failed-experiments.md:2657` 尝试 ViT W16 退步 `+32 µs` 的真因是 Clang 将外层 `for (uint part = 0; part < 4u; ++part)` / `for (uint p = 0; p < 4u; ++p)` 完全展开，导致 VGPR 暴涨至 `251` 与 `220`。
2. **机制、ISA 与零额外显存实现（`HIP_VIT_W16=7`）**：
   - `Development/HIP/hip_reference_network.h` 新增 `HIP_VIT_W16`（默认 `7`，支持 `NR_VIT_W16` / `DLSS5_HIP_VIT_W16` 位掩码覆写：`1`=Expand，`2`=Contract，`4`=Projection），在既有 `@vit-frag@compact-expand`、`@vit-frag@compact-contract`、`@vit-proj-frag@compact-projection` 键下原地执行 `FragmentPackedMatrixW16`，**零额外显存分配（`weights_count=184, weights_bytes=297,609,000 B`）**。
   - `hip/deep_fast.hip` 与 `hip/vit_stream.inc` 新增 `vit_expand_blocked_fp8_frag_bytein_w16`、`vit_stream_contract_frag_bout_compactw_w16`、`vit_stream_project_n64_bb_compactw_w16`：在 Contract 与 Projection 的外层 4 分片循环加 `_Pragma("clang loop unroll(disable)")`、内层 `k += 32u` W16 循环加 `_Pragma("unroll 2")`（Project 将 `p==0` 残差读取外提到循环前），保持 K 累加顺序与舍入 100% 不变。
   - `gfx1201` ISA 质变：
     - `vit_expand_blocked_fp8_frag_bytein_w16`：`77 VGPR`，SGPR 由 `25 → 13`（`-12`），`128` 条 `global_load_b64` → **`64` 条合并 `global_load_b128`**（`16 waves/SIMD`）。
     - `vit_stream_contract_frag_bout_compactw_w16`：VGPR 由 **`211 → 115`（`-96 VGPR`！）**，`256` 条 `global_load_b64` → **`128` 条合并 `global_load_b128`**，Occupancy 由 **`6 → 8 waves/SIMD`**（达 `4096 B` LDS 硬上限）！
     - `vit_stream_project_n64_bb_compactw_w16`：VGPR 由 **`190 → 114`（`-76 VGPR`！）**，`128` 条 `global_load_b64` → **`64` 条合并 `global_load_b128`**，Occupancy 由 **`6 → 10 waves/SIMD`（`+67%`）**！
3. **完整 Frame Gold 与 3 轮 4-slot ABBA（PASS，`-0.0566 ms` mean / `-0.0665 ms` median）**：
   - `1920×1152` 完整 Frame Gold（3 帧重复，`Auto` 脉冲 `record_ok=3 accepted=3 reject_mask=0`）：`baseline(0)`、`expand(1)`、`contract+proj(6)`、`all(7)` 四角色首尾帧 SHA256 全为 `0900E489A397302B93074A878C694B9C3E7C3400CFE2DFDD3CC727ABE1050F4A`（`0 bit-diff`，`extra_vram_bytes=0`）。
   - 在已开启 `HIP_C512_W16=2`、`HIP_C512_FFN_W16=1` 与 `HIP_C32_W16=1` 的基线之上执行完整 `frame.exe` 3 轮 4-slot ABBA（240 帧/slot，弃 80 取 160 稳态，960 帧/arm）：
     - **`NR_VIT_W16=7`（默认）**：3 轮均值差分别为 **`-47.8 µs`**（`9.4323 → 9.3846 ms`，median `-65.0 µs`）、**`-59.3 µs`**（`9.4787 → 9.4194 ms`，median `-68.5 µs`）、**`-62.9 µs`**（`9.5087 → 9.4458 ms`，median `-66.0 µs`），**3/3 轮全胜**；960 帧合并 mean `9.4732 → 9.4166 ms`（**`-0.0566 ms` / `-56.6 µs`，`-0.60%`**），median `9.4708 → 9.4043 ms`（**`-0.0665 ms` / `-66.5 µs`**，单槽 median 最低达 **`9.3620 ms`**）。
     - 与今日前两轮 W16 战役（C512 Split-FFN W16 `-0.1312 ms` + C32 Wave1 W16 `-0.0240 ms`）合计，三场 W16 战役累计净降 **`-0.2118 ms` mean / `-0.2338 ms` median**（`9.5926 ms → 9.4166 ms` mean，单槽 median 低至 `9.3620 ms`）。

### 2026-10-14：全网 Epilogue 无分支化、`v_movrels` 消除与 `+0.f` `fmed3f` FP8 截断清理（`isa-epilogue-clean-20261014`，`-0.1462 ms` mean / `-0.1435 ms` median）

对全网活跃模块执行逐核 ISA 反汇编扫描，定位并消除 6 处 Epilogue 分支风暴、`v_movrels_b32` 动态寄存器索引循环与 `-0.0f` 冗余 `v_max_num_f32`/`v_cndmask_b32`（详见 [ISA Epilogue Clean 报告](results/isa-epilogue-clean-20261014/README.md)）：

1. **核心病灶与无分支 ISA 改造**：
   - **`mh_attention_project_frag_c512_w16_u4` & `_w16`（`hip/multihead_fast_padded.hip`，每帧 16 次）**：原 Epilogue 为 `for(n=0..3) for(e=0..7)` 且对每个元素执行 `p>=tokens`、`post==3/4`、`cropw` 分支跳转，导致编译器未能展开内层 `e` 循环并生成 **`4` 条 `v_movrels_b32`** 动态寄存器索引与 **`58` 条 `s_cbranch`**。改为外层 `_Pragma("unroll") for(uint e=0;e<8;e++)`（每 `e` 仅 1 次 `if(p<valid_tokens)`）、内层 `_Pragma("unroll") for(uint n=0;n<4;n++)` 无分支计算 `hc=__builtin_amdgcn_fmed3f(hi+0.f,-448.f,448.f)` + `cvt_pk_fp8_f32` + `cvt_f32_fp8`（IEEE-754 RNE 下 `-0.0f + 0.0f = +0.0f`，天然满足 `±0.0f -> +0.0f` 合同）：`v_movrels_b32` 由 **`4 → 0`**，`s_cbranch` 由 **`58 → 17`（砍掉 41 条分支）**，总指令数 `1029 → 952`（`u4`），每 `e` 连续发射 4 条 `global_store_b32`。
   - **`split_projection_frag_w16` & `split_projection_frag`（`hip/deep_fast.hip`，每帧 16 次）**：原 Epilogue 调用 `F(VIT_FAST_H(acc[j][e]))` 后再执行 `cvt_pk_fp8_f32(v)`，在每个元素上生成 `v_max_num_f32` + `v_cmp_neq_f32` + `v_cndmask_b32` 及两次 `cvt_pk_fp8_f32`。改为单次 `xc=fmed3f(x+0.f,-448.f,448.f)` → `b8=cvt_pk_fp8_f32(xc)` → `v=cvt_f32_fp8(b8,0)`：`deep_fast-packed-fast.hsaco` 中 `split_projection_frag_w16` 指令数由 **`799 → 648`（`-151` 条，`-18.9%`）**，`v_max_num_f32` `31 → 0`，`v_cndmask_b32` `32 → 0`，VGPR `100 → 99`；`vit-stream-fast.hsaco` 中 `split_projection_frag_w16` 指令数由 **`904 → 745`（`-159` 条）**，`split_projection_frag` 由 **`936 → 716`（`-220` 条）**。
   - **`c512_qkv_attention_compact_w16`（`hip/c512_qkv_attention_compact.inc`，每帧 16 次）**：`c5c_fp8(float x)` 由 `fminf(fmaxf(x,-448.f),448.f)` 改为 `__builtin_amdgcn_fmed3f(x,-448.f,448.f)`，`v_max_num_f32` 由 `16 → 8`（`c512_qkv_attention_compact` 指令数 `2762 → 2706`）。
   - **`swin_wave2_body` float 输出 Epilogue（`hip/wave_owned_mh.inc`）**：非 `ByteOut` / 非 `W2HalfTail` 路径（`c64_wave2_bi`、`c128_wave2_bi`、`c256_wave2_bi_w16` 等）将嵌套三元 `post==3?Hrtz(v):post==0?F(Hrtz(v)):F(v)` 改为无分支 `fmed3f(hi+0.f,-448.f,448.f)` 计算，`c64-wave2-fast.hsaco` 中每核 `s_cbranch` 由 **`35 → 6`（`-29` 条分支）**，总指令数净减 **`166 ~ 177` 条/核**。
   - **`mh_pool_project_group_body`（`hip/multihead_fast_padded.hip`，Downsample Pool+Project）**：Epilogue 循环由外 `j` 内 `e` 改为外 `e=0..7`（每 `e` 1 次 `if(q<M)`）、内 `j=0..3` 无分支 `fmed3f(a+0.f,-448.f,448.f)`：`multihead-fast-padded-wave-packed.hsaco` 中 `mh_pool_project_group_c64/c128/c256/c512` 的 `s_cbranch` 由 **`94~95 → 22~23`（砍掉 72 条分支）**，总指令数分别由 **`2397 → 1000`（`-1397` 条，`-58.3%`）**、**`2635 → 1238`（`-1397` 条）**、**`2344 → 948`（`-1396` 条）**、**`2406 → 1012`（`-1394` 条）**。
   - **`c32_wave1_finish_b8*` 下采样 f32 路径（`hip/wave_owned_c32.inc`）**：将 `F(Hrtz(Hrtz(top+bottom)*.25f))` 改为 `fmed3f(dv+0.f,-448.f,448.f)` + `cvt_pk_fp8_f32` + `cvt_f32_fp8`，`c32_wave1_finish_b8_w16` 减少 `41` 条指令（`v_cndmask_b32` `16 → 0`）。
2. **完整 Frame Gold 与 3 轮 4-slot ABBA（PASS，`-0.1462 ms` mean / `-0.1435 ms` median）**：
   - `1920×1152` 完整 Frame Gold（3 帧重复，`Auto` 脉冲 `record_ok=3 accepted=3 reject_mask=0`）：`baseline`（`vit-w16-20261014`）与 `candidate`（重编 18 个受影响模块）首尾帧 SHA256 全为 `0900E489A397302B93074A878C694B9C3E7C3400CFE2DFDD3CC727ABE1050F4A`（`0 bit-diff`，`extra_vram_bytes=0`）。
   - 在已开启全部 W16 特性（`NR_C512_W16=2`、`NR_C512_FFN_W16=1`、`NR_C32_W16=1`、`NR_VIT_W16=7`）的基线之上执行完整 `frame.exe` 3 轮 4-slot ABBA（240 帧/slot，弃 80 取 160 稳态，960 帧/arm）：
     - **Round 1**：Baseline `9.3896 ms`（`9.3549` / `9.4243`）vs Candidate `9.2412 ms`（`9.2388` / `9.2436`），$\Delta\text{Mean} = \mathbf{-0.1484\text{ ms}}$（`-148.4 µs`），$\Delta\text{Median} = -141.5\text{ µs}$，$\Delta\text{p90} = -157.0\text{ µs}$，$\Delta\text{p99} = -155.0\text{ µs}$。
     - **Round 2**：Baseline `9.4233 ms`（`9.4044` / `9.4421`）vs Candidate `9.2925 ms`（`9.2731` / `9.3119`），$\Delta\text{Mean} = \mathbf{-0.1308\text{ ms}}$（`-130.8 µs`），$\Delta\text{Median} = -132.5\text{ µs}$，$\Delta\text{p90} = -147.0\text{ µs}$，$\Delta\text{p99} = -84.0\text{ µs}$。
     - **Round 3**：Baseline `9.4660 ms`（`9.4555` / `9.4765`）vs Candidate `9.3067 ms`（`9.2966` / `9.3167`），$\Delta\text{Mean} = \mathbf{-0.1593\text{ ms}}$（`-159.3 µs`），$\Delta\text{Median} = -155.0\text{ µs}$，$\Delta\text{p90} = -166.0\text{ µs}$，$\Delta\text{p99} = -180.0\text{ µs}$。
     - **3 轮合并（960 帧/arm，3/3 轮全胜、6/6 对偶全负）**：mean 由 **`9.4263 ms → 9.2801 ms`（`-0.1462 ms` / `-146.2 µs`，`-1.55%`）**，median 由 **`9.4190 ms → 9.2755 ms`（`-0.1435 ms` / `-143.5 µs`）**，p90 由 `9.5580 → 9.4020 ms`（`-156.0 µs`），p99 由 `9.6450 → 9.4990 ms`（`-146.0 µs`），单槽 mean/median 最低达 **`9.2388 ms` / `9.2380 ms`**。
     - 结合今日前四场 W16 战役，单日整网 Frame 时间已从 `9.6070 ms` 降至 **`9.2801 ms`**（单日累计净降 **`-0.3747 ms` mean**）。

### 2026-10-14：二轮全网 ISA 病灶审计与清理（`isa-audit2-20261014`，`F(x)`/`vit_contract_F_byte`/`c64_wave2_up` `fmed3f` 化 + `c32_wave1_post/finish` 向量化与去分支，`-5.6 µs` mean / `-9.5 µs` median）

在 `d5b72614`（`isa-epilogue-clean-20261014`）之上使用 LLVM 23 `llvm-objdump`（正确识别单行 `v_dual_*` VOPD 指令）对全部 18 个编译模块执行二轮逐核 ISA 病灶排查（详见 [ISA Audit2 报告](results/isa-audit2-20261014/README.md)）：

1. **核心 ISA 清理与负向耦合边界识别**：
   - **`F(x)` 与 `vit_contract_F_byte(x)`（`hip/deep_fast.hip`）**：将 `clampf(x,-448.f,448.f)` + `(bits(x)&0x7fffffffu)==0?0.f:y` 替换为 `__builtin_amdgcn_fmed3f(x+0.f,-448.f,448.f)` + `cvt_pk_fp8_f32`（同时保留 `clampf(a,lo,hi) = minf(maxf(a,lo),hi)` 以免破坏 `c512-m32-deep` 中 `vit_expand_blocked_fp8_frag_bytein_w16` 的 `46` 条 `v_dual_mov_b32` VOPD 配对，`400` 条指令零退步）。`vit-stream-fast.hsaco` 中 `vit_stream_contract_frag_bout_compactw_w16` 由 **`581 → 472`（`-109` 条，`-18.8%`）**（`v_max_num_f32` `23 → 0`、`v_cndmask_b32` `28 → 0`），`vit_stream_project_n64_bb_compactw_w16` 由 **`689 → 595`（`-94` 条，`-13.6%`）**（`v_max_num_f32` `27 → 0`、`v_cndmask_b32` `29 → 0`），`vit_stream_qkv_frag_bin_w5f8` 由 **`575 → 521`（`-54` 条）**，`vit_stream_qkv_frag_hin_w5f8` 由 **`711 → 665`（`-46` 条）**；`c512-m32-deep.hsaco` 中 `split_ffn_fused_fp8_t8` 由 **`692 → 669`（`-23` 条）**，`decoder_project2x_h16w_byteout` 由 **`5618 → 5425`（`-193` 条）**。
   - **`c32_wave1_finish*` 存储谓词外提与 `down` 向量化（`hip/wave_owned_c32.inc`）**：放开 `CW_FINISH_TAIL_VEC && ByteOut && !ByteDown` 的 `!Crop` 限制，使 `c32_wave1_finish_dcrop_b8_w16` 也走 `ds_load_b128` + `global_store_b128` 向量化下采样输出路径（消除 `134` 条标量 `ds_load_u16` 与 `31` 条 `global_store_b32`），并将 `t0`/`t1` 打包计算外提到 `if(!Crop)` 分支之外（仅谓词化 `global_store_b128`），同时将 `cw_up_F` 改为 `fmed3f(x+0.f,-448.f,448.f)`。`c32-wave1-fast.hsaco` 中 `c32_wave1_finish_b8_w16` 由 **`2116 → 1589`（`-527` 条，`-24.9%`）**（`s_cbranch` `25 → 16`、`ds_load_u16` `64 → 0`、`global_store_b32` `16 → 0`），`c32_wave1_finish_dcrop_b8_w16` 由 **`2557 → 1621`（`-936` 条，`-36.6%`）**（`s_cbranch` `37 → 18`、`ds_load_u16` `134 → 0`），`c32_wave1_finish_dcrop_b8d_w16` 由 **`2203 → 1591`（`-612` 条，`-27.8%`）**。
   - **`c32_wave1_post*` `ds_load_b128` 向量化（`hip/wave_owned_c32.inc`）**：将 `CW_POST_TAIL_VEC && ByteOut && W16` 尾部每 lane 8 次标量 `ds_load_u16`（`sh_u16[l*8+e]`）合并为 1 条 `ds_load_b128`（`reinterpret_cast<const u4*>(sh_u16)[l]`）寄存器解包，`c32_wave1_post_b8_w16` 由 **`1545 → 1532`（`-13` 条）**、`c32_wave1_post_b8_logit_w16` 由 **`1664 → 1658`（`-6` 条）**，`ds_load_u16` `8 → 0`、`s_wait_dscnt` `14 → 7`。
   - **`c64_wave2_up_lb_sh` 与 C512 QKV 辅助函数（`hip/wave_owned_mh.inc`、`hip/multihead_fast_padded.hip`、`hip/c512_qkv_attention_fused.inc`）**：将 `w2_z`、`w2_up_F`、`w2_skip_byte` 改为 `fmed3f(x+0.f,-448.f,448.f)` + `cvt_pk_fp8_f32`，`c64_wave2_up_lb_sh` 由 **`4770 → 4541`（`-229` 条）**（`v_cndmask_b32` `175 → 47`）；`c512_qkv_attention_fused` 由 **`1759 → 1614`（`-145` 条）**，`mh_qkv_normalize_frag_c512_m32` 由 **`803 → 745`（`-58` 条）**。
   - **Uniform-SGPR `m0` `v_movrel` vs `v_cndmask_b32` 选择树实测证伪**：实测将 `wave_owned_c32.inc`（`c32_wave1_chain_w16`，每帧 22 次调用）与 `wave_owned_mh.inc`（`c128_wave2_bi_bo` / `sp_run256_w16`）中 uniform SGPR 循环变量 `qt=0..3` 的数组索引替换为三元选择树 `(qt&2)?((qt&1)?w3:w2):((qt&1)?w1:w0)`，虽然表面消除了 `16`/`4` 条 `v_movrel`，但因 uniform `m0` 相对寻址每 word 仅需 1 条 VALU 指令且不拉长单元素活跃区间，三元树反而生成 `56` 条 `v_cndmask_b32_e64` 并打断 VOPD 双发射，导致 `c32_wave1_chain_w16` 暴涨 `+108` 条指令（`1003 → 1111`）、`c128_wave2_bi_bo` `+54` 条、`sp_run256_w16` `+61` 条，实机 ABBA 退步 `+0.14 ms`。已回退 `v_movrel` 三元替换，确保全网所有活跃 kernel 0 退步。
2. **完整 Frame Gold 与 3 轮 4-slot ABBA（PASS，`-0.0056 ms` mean / `-0.0095 ms` median）**：
   - `1920×1152` 完整 Frame Gold（3 帧重复，`Auto` 脉冲 `record_ok=3 accepted=3 reject_mask=0`）：`baseline`（`d5b72614`）与 `candidate` 首尾帧 SHA256 全为 `0900E489A397302B93074A878C694B9C3E7C3400CFE2DFDD3CC727ABE1050F4A`（`0 bit-diff`，`extra_vram_bytes=0`）。
   - 3 轮 4-slot ABBA（960 帧/arm，5/6 对偶为负）：mean `9.2894 → 9.2838 ms`（**`-0.0056 ms` / `-5.6 µs`**），median `9.2860 → 9.2765 ms`（**`-0.0095 ms` / `-9.5 µs`**），p90 `9.4190 → 9.4120 ms`（`-7.0 µs`），p99 `9.4980 → 9.4980 ms`（`+0.0 µs`）。

### 2026-10-14：ViT 块间字节流直连（`HIP_VIT_INTER_BYTE=1`，消除 7 次 `vit_pack_input` 发射与 49.28 MB/帧 DRAM 往返）+ `vit-stream` `HIP_BYTE_F_ADD0` 与 QKV 归一化 Epilogue 外提（`vit-inter-byte-20261014`，`-0.1704 ms` mean / `-0.1611 ms` median）

对比 Mochizuki RDNA4 ViT 执行图与我方 ViT Stage（blocks `31..38`，`n=1380`，`C=256`）跨核边界，定位并消除了 ViT 块间 `float32 -> uint8` 的冗余 DRAM 往返与 7 次独立 `vit_pack_input` 发射，同时清理了 `vit-stream` 的两处 Epilogue 病灶（详见 [ViT Inter-Block Byte Stream 报告](results/vit-inter-byte-20261014/README.md)）：

1. **跨核结构与 ISA 改造**：
   - **ViT 块间字节流直连（`HIP_VIT_INTER_BYTE=1` 默认开启，`hip/vit_stream.inc` + `Development/HIP/hip_reference_network.h`）**：
     - 此前 ViT Stage 共 8 个 block（`31..38`）：每个 block 先发射独立 kernel `vit_pack_input`（读取 `1.41 MB` FP32 `input` 并写出 `0.35 MB` E4M3 `in8`），随后 `vit_stream_contract_frag_bout_compactw_w16` 一边读 `0.35 MB` 1B `in8` 做矩阵乘、一边从 `in_skip` 再读一遍 `1.41 MB` 4B FP32 `input` 做残差相加，最后 `vit_stream_project_n64_bb_compactw_w16` 在寄存器内已经将累加结果截断到 FP8 E4M3 网格（`F(VIT_FAST_H(total))`）的情况下，又将其转回 `1.41 MB` 4B FP32 `out` 写回全局显存，供下一 block 的 `vit_pack_input` 重新读回压缩为 1B E4M3。
     - 为 `vit_stream_project_tiles` 增加 `bool ByteOut=false` 模板参数，导出 `vit_stream_project_n64_bb_bout_compactw_w16`（`ByteOut=true` 直接写出 `byte_F(VIT_FAST_H(total))`，每 lane 8 次 4B `global_store_b32` 缩减为 8 次 1B `global_store_b8`，`global_store_b32: 8 → 0`）与 `vit_stream_contract_frag_bstream_bout_compactw_w16`（`ByteIn=true, ByteStream=true, ByteOut=true, CompactWeights=true, W16=true`，残差路径直接读取 1B E4M3 `in8_skip` 并以 `fp8_to_f32` 还原，`global_load_b32: 10 → 2`）。
     - 在 `HipReferenceNetwork::Vit()` 与 `Run()` 中，blocks `31..37`（前 7 个 ViT block）直接分配并写出 `n * 256` 字节的 1B E4M3 `out`（`0.353 MB` 替代 `1.413 MB`），blocks `32..38`（后 7 个 ViT block）直接复用上一 block 的 1B `out` 作为 `in8` 与 `in8_skip`——**彻底消除 blocks `32..38` 的 7 次 `vit_pack_input` kernel 发射（全网每帧总发射数由 `161 → 154`）**，并在 `Gather()` 消费完 `input` 后立即 `input.Reset()` 归还池内存（`pool_MiB=277.1 (27 tensors)`、`owned_MiB=565.9`，**0 字节额外显存**）。
     - 每帧消除 **`7 × 7.04 MB = 49.28 MB` 全局显存往返流量**，且因 `F(VIT_FAST_H(total))` 原本就已投影到精确的 FP8 E4M3 数值网格，1B 存储与 `fp8_to_f32` 还原在数学上 **100% 逐位等价**。
   - **`vit-stream` 开启 `HIP_BYTE_F_ADD0=1` 与 `vit_stream_qkv_frag_bin_w5f8` 归一化外提（`hip/build-modules.ps1` + `hip/vit_stream.inc`）**：
     - `build-modules.ps1` 中 `vit-stream-fast` 与 `vit-stream` 此前漏加 `'HIP_BYTE_F_ADD0 1'`，导致 `byte_F(float)` 走旧的 4 指令分支路径（`v_cmp_gt_f32` + `v_cndmask_b32` + `v_max_num_f32` + `v_cvt_pk_fp8_f32`）。补齐 `'HIP_BYTE_F_ADD0 1'` 后统一走 2 指令无分支 `fmed3f(x + 0.f, -448.f, 448.f)` + `v_cvt_pk_fp8_f32`。
     - `vit_stream_qkv_frag_bin_w5f8`（每帧 8 次调用）与 `vit_stream_qkv_frag_hin_w5f8` 此前在 `for (e=0..7)` 循环内对 `part < 2`（Q/K）和 `part == 2`（V）分两条 `global_store_b8` 分支，导致编译器对全部 3 个 part 都无条件执行平方和规约、`__syncthreads()`、`s_scale` 加载、8 次 `v_rsq_f32` 与 16 次 `v_mul_f32`，且生成两份 `byte_F` 转换（48 条 `v_cvt`）。将 `if (part < 2)` 整体外提到规约与 `s_scale` 加载之前，合并为单一 `global_store_b8` 循环，并将 `maxf` 替换为无 NaN 检查的 `__builtin_fmaxf`：`vit_stream_qkv_frag_bin_w5f8` 总指令数由 **`521 → 455`（`-66` 条，VALU `233 → 168` 净减 `-65` 条，`v_cvt` `48 → 16` 净减 `-32` 条）**，且 `part == 2`（V 分支，占 1/3 线程块）直接跳过 LDS 规约、scale 加载与 8 次 `v_rsq_f32`。
2. **完整 Frame Gold 与 3 轮 4-slot ABBA（PASS，`-0.1704 ms` mean / `-0.1611 ms` median）**：
   - `1920×1152` 完整 Frame Gold（3 帧重复，`Auto` 脉冲 `record_ok=3 accepted=3 reject_mask=0`）：`baseline`（`NR_VIT_INTER_BYTE=0`）、`candidate-qkv-only`（`NR_VIT_INTER_BYTE=0`）与 `candidate-inter-byte`（`NR_VIT_INTER_BYTE=1`，`vit_inter_byte_calls=21`）首尾帧 SHA256 全为 `0900E489A397302B93074A878C694B9C3E7C3400CFE2DFDD3CC727ABE1050F4A`（**`0 bit-diff`**，`pool_MiB=277.1 (27 tensors)`，`extra_vram_bytes=0`）。
   - 在已开启全部前序优化（`9241808a`）的基线之上执行完整 `frame.exe` 3 轮 4-slot ABBA（240 帧/slot，弃 80 取 160 稳态，960 帧/arm）：
     - **Round 1**：Baseline `9.2416 ms`（`9.2172` / `9.2659`，med `9.2140` / `9.2670`）vs Candidate `9.0620 ms`（`9.0424` / `9.0815`，med `9.0372` / `9.0805`），$\Delta\text{Mean} = \mathbf{-0.1796\text{ ms}}$（`-179.6 µs`），$\Delta\text{Median} = \mathbf{-0.1816\text{ ms}}$（`-181.6 µs`）。
     - **Round 2**：Baseline `9.2970 ms`（`9.2776` / `9.3164`，med `9.2651` / `9.3114`）vs Candidate `9.1225 ms`（`9.1218` / `9.1232`，med `9.1358` / `9.1247`），$\Delta\text{Mean} = \mathbf{-0.1745\text{ ms}}$（`-174.5 µs`），$\Delta\text{Median} = \mathbf{-0.1580\text{ ms}}$（`-158.0 µs`）。
     - **Round 3**：Baseline `9.3036 ms`（`9.3033` / `9.3039`，med `9.2920` / `9.2917`）vs Candidate `9.1466 ms`（`9.1415` / `9.1516`，med `9.1475` / `9.1488`），$\Delta\text{Mean} = \mathbf{-0.1571\text{ ms}}$（`-157.1 µs`），$\Delta\text{Median} = \mathbf{-0.1437\text{ ms}}$（`-143.7 µs`）。
     - **3 轮合并（960 帧/arm，3/3 轮全胜、6/6 对偶全负）**：mean 由 **`9.2807 ms → 9.1103 ms`（`-0.1704 ms` / `-170.4 µs`，`-1.84%`）**，median 由 **`9.2735 ms → 9.1124 ms`（`-0.1611 ms` / `-161.1 µs`）**，单槽 mean/median 最低达 **`9.0424 ms` / `9.0372 ms`**，逼近 9.0 ms 大关！
     - 结合今日前五场战役，单日整网 Frame 时间已从 `9.6070 ms` 降至 **`9.1103 ms`**（单日六场战役累计净降 **`-0.5340 ms` mean / `-0.5479 ms` median**）。

### 2026-10-14：C512 16-Block 块间 1B E4M3 字节流直连 + ViT 进出边字节化（消除最后 1 次 `vit_pack_input` 发射）+ Downsample C64/C128 字节边（`HIP_VIT_INTER_BYTE=15`，`c512-byte-stream-20261014`，`-0.0472 ms` mean / `-0.0525 ms` median，单槽突破 9.0 ms 大关达 `8.9601 ms` mean / `8.9445 ms` median）

在 `vit-inter-byte-20261014`（`HIP_VIT_INTER_BYTE=1`）消除了 ViT 内部 7 次 `vit_pack_input` 发射后，对全网剩余跨核边界开展地毯式排查，定位并消除了三处“生产者在寄存器内已量化到精确 FP8 E4M3 网格（`F(Hrtz(acc))`）、却展开为 4B FP32 写回显存、消费者立即重读并重新 `v_cvt_pk_fp8_f32` 打包为 1B E4M3”的冗余流量与最后 1 次 `vit_pack_input` 发射（详见 [C512 Byte Stream 报告](results/c512-byte-stream-20261014/README.md)）：

1. **三项跨核 1B E4M3 字节流扩展（`HIP_VIT_INTER_BYTE=15` 默认开启，位掩码 `1|2|4|8`）**：
   - **Bit 1（`2`）：Downsample C64/C128 → Block 9 / Block 15 字节边（`hip/multihead_fast_padded.hip` + `Development/HIP/hip_reference_network.h`）**：
     - `mh_pool_project_group_c64_hin`（`480×288×128`）与 `mh_pool_project_group_c128_hin`（`240×144×256`）原本已算出 `byte_F(Hrtz(acc))` 又转回 4B FP32 写出，而消费者 Block 9（`c128_wave2`）与 Block 15（`c256_wave2`）早已具备 `*_bi_bo[_w16]` 1B 输入变体。导出 `mh_pool_project_group_c64_hin_out8` 与 `mh_pool_project_group_c128_hin_out8`（`global_store_b32: 32 → 0`、`global_store_b8: 0 → 32`、`v_cvt_f32_fp8: 33 → 1`），每帧消除 **`59.72 MB`** 显存往返。
   - **Bit 2（`4`）：ViT 入口（`Down(30) → Gather → Block 31`）与出口（`Block 38 → Gather → Up(39)`）字节边（`hip/c512_head_group.inc` + `hip/deep_fast.hip`）**：
     - 入口处导出 `mh_pool_project_group_c512_out8` 与 `vit_gather_b8`（7 VGPR，直接搬运 1B E4M3 token），使 Block 31 直接消费 `vit_gather_b8` 产出的 1B `vit_in8`——**彻底消除全网最后 1 次 `vit_pack_input` kernel 发射（全网每帧总发射数由 `154 → 153`，8 个 ViT block 的 `vit_pack_input` 8/8 全部清零）**。
     - 出口处令 Block 38 也走 `vit_stream_project_n64_bb_bout_compactw_w16` 直接写出 1B E4M3，经 `vit_gather_b8` 还原空间网格后，由新增导出的 `decoder_project2x_h16w_w_bin` / `decoder_project2x_h16w_byteout_w_bin` 直接以 `global_load_b64` 读取 8B E4M3 并在寄存器内解包，将 `Up(39)` 对每个输入 token 重复 32 次的全局读取带宽砍掉 4 倍（`45.16 MB → 11.29 MB`），每帧合计消除 **`41.61 MB`** 显存流量与 `Up(39)` 每 wave 128 条 `v_cvt_pk_fp8_f32`。
   - **Bit 3（`8`）：C512 16-Block 块间 1B E4M3 字节流直连（`Down(22) → Blocks 23..30` 与 `Up(39) → Blocks 40..47`）**：
     - 此前 16 个 C512 block（`60×36 = 2,160` tokens，`C=512`）中，每个 block 的 `split_ffn_one_w2f8_w16` 被 8 个通道组 workgroup 重复读取 8 遍 4B FP32 输入（`8 × 4.42 MB = 35.39 MB/block`，16 个 block 高达 **`566.23 MB/帧`**），且每 wave 执行 64 条 `v_cvt_pk_fp8_f32`（`c5d_pack8f`）将原本就落在 E4M3 网格上的 FP32 重新打包回 E4M3 字节；同时 `split_projection_frag_w16` 再读 1 遍 `4.42 MB` FP32 skip（`70.78 MB/帧`），`mh_attention_project_frag_c512_w16_u4` 写出 `4.42 MB` FP32（`70.78 MB/帧`）。
     - 导出 `mh_pool_project_group_c256_out8`、`mh_attention_project_frag_c512_w16_u4_bout`（101 VGPR，0 spill，`global_store_b32: 32 → 0`、`global_store_b8: 0 → 32`、`v_cvt_f32_fp8: 32 → 0`）、`split_ffn_one_w2f8_w16_bin`（通过 `_Pragma("clang loop unroll_count(2)")` 抑制 Clang 因输入加载缩小而触发的 16x 完全展开，保持 **124 VGPR、Occupancy 10、0 spill**，`v_cvt_pk_fp8_f32: 80 → 64` 消除全部 64 条输入重打包转换）、`split_projection_frag_w16_bskip`（99 VGPR），并放开 `decoder_project2x_h16w_byteout_w[_bin]` 对 `1024 → 512`（`Up(39)`）的支持，使 `Blocks 23..30` 与 `Blocks 40..47` 全程保持 1B E4M3 块间流（仅 Block 30 因 `post=3` 保留 FP32 输出供 `skips[4]`/`Down(30)` 消费，Block 47 保留 FP32 输出供 `Up(48)` 消费）。
     - 每帧消除 **`530.84 MB` L2/DRAM 流量**（三项合计消除 **`632.17 MB/帧`**），且因 16 个 C512 块间张量由 `4.42 MB` 缩减为 `1.11 MB`，运行时池显存由 **`277.1 MiB` 降至 `276.0 MiB`（净省 `1.1 MiB` VRAM，`owned_MiB: 565.9 → 564.9 MiB`）**。
   - **Bit 4（`16`，可选开关）：C512 块内转置 WMMA FB8（`split_projection_frag_w16_nof32_bskip` + `mh_attention_project_frag_c512_w16_u4_fb8_bout`）**：
     - 同步实现了通过转置 WMMA（`B^T * A^T`）直接从 `ffn8` E4M3 tile 读取残差、省去块内 `4.42 MB` FP32 `ffn` 张量读写（`141.56 MB/帧`）的变体（96 / 94 VGPR，0 LDS，0 spill），Gold 100% 逐位一致；实测 3 轮 ABBA 同样 3/3 全胜（`-37.6 µs` mean / `-42.0 µs` median），但因转置 WMMA 下相邻 lane 索引 16 个 token（`av8` 步长 512B）弱于非转置的 16 列连续访问，净收益略低于保留块内 `ffn` 的 `mode=15`（`-47.2 µs` mean / `-52.5 µs` median），故默认设为 `HIP_VIT_INTER_BYTE=15`，Bit 4 留作可选位。
2. **完整 Frame Gold 与 3 轮 4-slot ABBA（PASS，`-0.0472 ms` mean / `-0.0525 ms` median，单槽突破 `9.0 ms` 达 `8.9601 ms` mean / `8.9445 ms` median）**：
   - `1920×1152` 完整 Frame Gold（3 帧重复，`Auto` 脉冲 `record_ok=3 accepted=3 reject_mask=0`）：`NR_VIT_INTER_BYTE = 1, 3, 7, 9, 15, 31` 共 6 组模式首尾帧 SHA256 全为 `0900E489A397302B93074A878C694B9C3E7C3400CFE2DFDD3CC727ABE1050F4A`（**`0 bit-diff`**，`pool_MiB=276.0 (27 tensors)` 净省 `1.1 MiB`，`extra_vram_bytes=0`）。
   - 在已开启全部前六场战役（`e337af3d`，`NR_VIT_INTER_BYTE=1`）的基线之上执行完整 `frame.exe` 3 轮 4-slot ABBA（240 帧/slot，弃 80 取 160 稳态，960 帧/arm）：
     - **Round 1**：Baseline `9.0662 ms`（`9.0426` / `9.0897`，med `9.0480` / `9.1005`）vs Candidate `9.0208 ms`（`9.0814` / **`8.9601`**，med `9.0690` / **`8.9445`**），$\Delta\text{Mean} = \mathbf{-0.0454\text{ ms}}$（`-45.4 µs`），$\Delta\text{Median} = \mathbf{-0.0650\text{ ms}}$（`-65.0 µs`）。其中 **`candidate-B2` 单槽 160 帧稳态 mean 达 `8.9601 ms`、median 达 `8.9445 ms`，正式突破 `9.0 ms` 大关！**
     - **Round 2**：Baseline `9.1032 ms`（`9.0935` / `9.1129`，med `9.0885` / `9.1060`）vs Candidate `9.0595 ms`（`9.0766` / `9.0424`，med `9.0695` / `9.0350`），$\Delta\text{Mean} = \mathbf{-0.0437\text{ ms}}$（`-43.7 µs`），$\Delta\text{Median} = \mathbf{-0.0385\text{ ms}}$（`-38.5 µs`）。
     - **Round 3**：Baseline `9.1369 ms`（`9.1240` / `9.1498`，med `9.1285` / `9.1545`）vs Candidate `9.0845 ms`（`9.0888` / `9.0803`，med `9.0775` / `9.0640`），$\Delta\text{Mean} = \mathbf{-0.0524\text{ ms}}$（`-52.4 µs`），$\Delta\text{Median} = \mathbf{-0.0685\text{ ms}}$（`-68.5 µs`）。
     - **3 轮合并（960 帧/arm，3/3 轮全胜）**：mean 由 **`9.1021 ms → 9.0549 ms`（`-0.0472 ms` / `-47.2 µs`）**，median 由 **`9.1010 ms → 9.0485 ms`（`-0.0525 ms` / `-52.5 µs`）**，p90 由 `9.2160 → 9.1830 ms`（`-33.0 µs`），p99 由 `9.3120 → 9.2730 ms`（`-39.0 µs`）。
     - **2026-10-14 单日七场战役汇总**：1080p（`1920×1152`）整网 Frame 时间已从早晨基线 `9.6070 ms` 一路降至 **`9.0549 ms` pooled mean / `9.0485 ms` pooled median（最佳单槽 `8.9601 ms` mean / `8.9445 ms` median）**，单日七场战役 100% 逐位无损、零额外显存（且池显存净省 `1.1 MiB`），累计净降 **`-0.5812 ms` mean / `-0.6004 ms` median**！


### 2026-10-17：候选1 decoder字节边与W16 FP8采纳

接通Block47→Up48 E4M3字节边，Up39/48直接读取字节输入与FragmentPackedMatrixW16权重，K16累加及Up39四段归并/skip尾保持。复用原权重key、容量不增；mode3默认，mode0旧路，缺export成对回退。1920×1152完整Frame Gold首尾0900E489...0F4A逐位一致；3轮ABBA(240弃80/slot，960/arm) mean分别−22.087/−12.966/−17.142µs，p99分别−29.038/−8.255/−29.330µs；pooled mean9.168759792→9.151361354ms(−17.398µs)，p99−23.962µs。R2控制漂移−103.6µs照留，不宣全部对偶赢。远端首次旧模块Gold仅fallback，RTC路径/null ExitCode失败及正式COMGR Gold分开记录。测试遵game-check/原子锁/15秒游戏看门狗，游戏期间仅CPU准备。游戏安装与发行包未改。证据results/decoder-f8-w16-20261017。

### 2026-10-17：候选2双写入端gather折叠止负

Down30写ViT序、Block38写decoder序，避免旧gin读取端打散。完整Frame Gold0900逐位同，但3轮ABBA mean全部非改善，pooled9.128203125→9.133894583ms(+5.691µs)，p99 pooled−3.487µs不能抵均值负。STOP，不合入；候选3从已接受候选1继续。结果results/vit-write-gather-20261017，失败总账已追加；游戏和发行包未改。

### 2026-10-17：候选3 grouped Down FP8 W16采纳

Down8/14/22/30使用字节LDS池化值与W16 FP8权重，保K32后H(acc+sum)及原padding/池化顺序，缺export回half；复用@ds-frag容量。Gold0900全同；3轮ABBA mean−21.775/−17.083/−23.255µs，p99−34.692/−47.005/−10.743µs，pooled9.153535833→9.132831563ms(−20.704µs)，p99−27.539µs。候选2关闭、候选1为共同基线；gfx1200仅编译，双架构80模块manifest后续最终干净重构统一封存。详results/down-f8-w16-20261017，游戏与正式包未改。

### 2026-10-17：候选4 C32 pool四列合波止负

Gold0900逐位一致；3轮ABBA均值全部退步，pooled9.117309583→9.122855833ms(+5.546µs)。p99改善4.099µs不足通过均值门，STOP，生产保原四wave。详results/pool32-wave-w16-20261017与failed-experiments；候选5从已采纳1+3继续，不带候选2/4。

### 2026-10-17：候选5融合Up W16 FP8结题，保原路径

56/62新核Gold同但三轮pooled mean+3.555µs/p99+20.987µs。派发记录确认66未命中，CW_UP_LOW_BYTES0使原计划low-byte前提不成立。补齐65→66字节边后66新导出实际运行，full Gold首帧2,768,436 half值不同，停止且不跑性能。失败原数据全留，不当NV合同/FP8硬件反证。五项逐项结题：收1/3，停2/4/5；开始干净主线80模块重编及最终默认路径Gold。详results/upbody-f8-w16-20261017。

### 2026-10-17：五项优化最终干净产物与双档验收完成

生产只收候选1/3（载荷源码10982cf4），2/4/5已留负账，不把孤立收益相加。1/3新布局使用独立tag缓存键，防half回落布局混读；最终固定模式权重数量/容量未增加。正式O2 Frame默认路径不带NR_*覆盖，1920×1152与1600×960另跑每帧读回的3帧Gold：1080首尾0900E489...0F4A、两档全部3帧finite/repeat/对照逐位同。

干净80模块同批各3轮4-slot ABBA（240弃80，960/arm/tier）：1080 mean9.142189167→9.121343438ms（−20.845729µs）、pooled p99−25.690µs；900 mean6.569911146→6.535424271ms（−34.486875µs）、pooled p99−32.351µs。六轮均值全改善；1080 R2 p99+10.599µs保留，不称每轮/每slot尾都好，不刷轮消反例，不从其它批次绝对值算FPS。

全部80个SHA及实际ELF目标核验中抓到远端旧D:\DLSSNR-Lab\rtc_compile.exe忽略gfx1200参数，初始COMGR gfx1200标签实际为gfx1201。用当前hip/rtc_compile.cpp在独立目录编译工具，只重构gfx1200；修后40+40实际ELF架构全部正确，已实测gfx1201的40个文件逐SHA未变。旧错target快照及失败台账保留，本轮未替换全局工具。gfx1200仅编译/ELF核验，不冒真机证据。hip/SHA256SUMS同步最终80项。

正式addon/runtime构建完成，runtime API smoke先因测试目录缺native_codec_encode.hlsl失败，补齐当前shader测试资产后exit0；原失败日志仍留。最终产物本地/home/lmxxf/work/dlss5-five-build/final-payload，远端D:\DLSSNR-Lab\five-final-20261017。原始CSV/raw、模块/宿主SHA、source receipt、构建/修复与smoke日志见results/five-final-20261017。所有GPU/RTC持原子gpu.lock、game-check/≥100GB磁盘/15秒游戏看门狗，锁已释放。游戏安装、玩家配置、正式0.42 ZIP未动，未推送/发布；五项任务结题，待用户指定新版号后打包。

### 2026-10-17：10982cf4双游戏安装回执入库与正式0.43本地三包

鳩先行将10982cf4装入剑星/鬼武者：备份backup-20261017-233600、两宿主和双架构80模块逐SHA读回，鬼武者根/_storage_ runtime一致，个人default/custom/native配置字节未改，runtime-smoke exit0；回执results/game-install-20261017本次一并入库。剑星此前60fps属于10-14载荷历史观察，不作新版本FPS数据。

用户授权正式0.43三包后，以已验证0.42完整包为底，复用five-final-20261017宿主/80模块，不重编内核、不复跑ABBA。每包44shader变体（共3组）、包内RE9 runtime-smoke（PrepareFrame/HIP rc0、errors0）与ZIP全文件SHA回读通过，独立核默认配置、80模块身份及源码附件/sidecar通过。正式默认MODE0/1x/SKIN0，PRED1仅3x生效；Magpie不冒新增时域消费者支持。GPL宿主保持原二进制/源码，源码附件刷新MIT核心与当前文档，源代码标识10982cf4。

D:\給網友打包三包：Magpie345297302B SHA b3450315de6bf23bfbbd550a56cc82d7ed461e31b6a2c402e6baf1fefe3160ac；OptiScaler375495322B SHA e507a6b4fb4e8a80c92334f7509d40f81907095eb6b281190d8c09756e4e512e；OptiScaler-REFramework430517634B SHA 0ac552c469f7d4b88dbb0e3d7aa484e462a5fd4967d8f52b6d29adbc5257203f，均附.zip.sha256，载荷文件781/599/608个（另加总SHA清单）。

准备阶段配置重复展开、PowerShell5无BOM中文路径解析失败在成包前修正并留原日志；metadata输出明确UTF8。打包GPU阶段持gpu.lock、game-check、≥100GB余量与15秒游戏看门狗，最终锁释放；本轮没有再次安装/修改玩家配置，没有改旧0.42 ZIP。用户随后提供 0.43 公开下载入口：[夸克](https://pan.quark.cn/s/3b19c0403e3f) · [Gofile](https://gofile.io/d/hz5cDOPF)，README/双语CHANGELOG/六份package-notes/WorkingPlan已同步更新；完整产物与验证回执见Development/releases/0.43。

**2026-10-18 测量口径更新（Zero 定）**：因《剑星》2K AA（1080P DLSS5）1x 实玩已顶满 60 FPS 上限，游戏内帧率已无法分辨亚毫秒级收益，**以后不再用游戏内帧率衡量，直接记录每个版本的单帧耗时（`frame.exe` 稳态 `ms/frame`）**：
- `0.42`（`456b7e16`）：1080 档 `~9.65 ms`
- `10-14`（`be506729`，七场战役）：1080 档 `9.055 ms` mean / `9.049 ms` median（最佳单槽 `8.960 ms` mean / `8.945 ms` median）
- `0.43`（`10982cf4`，含候选 1+3）：1080 档 **`9.121 ms`**（同批对照 `9.142 → 9.121 ms`，`-20.85 µs`）、900 档 **`6.535 ms`**（同批对照 `6.570 → 6.535 ms`，`-34.49 µs`）。



### 2026-10-18：0.5x Small 的1080阶段接通（后续900/720未完成）

Zero定名新31块模型为0.5x并作为默认，配置`DLSS5_MULTI_PASS=0.5`，F9顺序0.5→1→2→3→0.5；1/2/3保留旧71块单遍/叠层。模式与整数遍数分开，Small独立资产目录；缺资产/未支持几何或history合同记录requested/effective并回退1x。不是把0.5截断为0遍，也不让测试静默测成旧网。

同一exact布局转换器先重解HT：184主图文件逐float bit对上既有资产，再导出Small 93文件/241214392B（92主图+history行6的64B），blend 0x3A23在manifest。5090真实NGX新Preset1=旧310.8、默认0=新Preset2，两张真实输入与合成重复稳定。加只读观察/原内部Post捕获后，API最终输出SHA仍不变。首轮把HIP网络RGB与API额外合成后的输出混比约25dB，改为内部Post FP16存储后展开的RGB比较；真实8678 Small FAST0 48.021703dB、FAST1 47.755055dB，不宣称NV逐位相同。

HIP固定1080图31块/640 token，复用既有算子、改拓扑/shift/skip与模型资产身份，SP/Pulse/PDL/AE旧图资格不套Small。模型切换先drain，连SP计划的旧权重裸指针也一起清理，防止旧图重入用到已释放权重。9070实际0.5→1→2→3→0.5输出往返Small bit0，CPU F9配置写回及reload通过；缺Small回退与显式1x逐位同。Original首尾Gold `0900E489…0F4A`未变，新Small `65D71486…8FCB8`，Gold+3轮4-slot ABBA全同模型首尾逐位稳定/有限。

完整frame.exe NET0/edges_only1，每槽240帧丢前80：同批1x **9.156154ms → 0.5x 5.553510ms/frame**（mean−39.347%，1.649×），Small median5.535300ms/p99 5.820500ms；三轮均正收益。0.43历史1x9.121ms保留，不把跨批差额当旧路径性能回归。驻留上传权重297609000→154793464B，不称整进程显存峰值。

宿主add-on/runtime/frame已编译，既有80个双架构HSACO未改，未装游戏、未换0.43包、未发新版；gfx1200无Small真机声明。900/720/free逐层几何、跨尺寸热切、Small原生history/AE及游戏体验仍待下一阶段；runtime仍启动读配置，F9/热重载目前仅add-on，统一运行时热键也未结题。所有GPU实验守game-check、实验盘≥100GB、原子gpu.lock与15秒看门狗；5090 D盘余63GB，实验改放C盘（约777GB空闲），锁位置不变。完整源码/Oracle/CSV/Gold/模式切换回执见 [small-model-20261018](results/small-model-20261018/README.md)，当前排队只看WorkingPlan§2。

## 2026-10-18 Small第三阶段：900/720/free、AE与MODE1

接通Small逐节点ceil-half/round4及输入X补边、输出stride和跨模式重建。900=1600×1024/448token，720=1344×768/288token。5090内部Post对账90047.4026dB、72047.7028dB；720裁判抓取UAV同步修正后无零块，API输出SHA未变。

9070三轮4-slot ABBA：900 Small **4.376946ms/frame**（median4.36715/p994.6548），同批Original6.558681，−33.2648%，每轮正收益；1080阶段二5.553510ms保留。正式宿主回归1080 Gold旧0900/Small65D7均逐位保持。Small AE按8个ViT层算gain；MODE1用独立history-head与0x3A23，GPU解析例、720时序reset、F9全模式回环、AE/free完整帧通过。runtime支持配置热键和切模式重建，2x/3x暂挂history再恢复。93 Small权重和旧模型共存，80模块复用。

详[完整回执](results/small-complete-20261018/README.md)。用户本轮明确授权双游戏部署并把《剑星》custom MULTI_PASS改0.5；发行准备0.44三包，完成后追加安装/发行验收。

### 2026-10-18 05:02 第三阶段交付：0.44三包与双游戏

正式Magpie447,953,583B（57e6bd49…）、OptiScaler478,151,601B（45253828…）、REFramework533,119,059B（e501bbd3…）均在D:\給網友打包，附zip.sha256。每包44 shader编译、80模块/93Small权重、ZIP全文件回读及独立Small解压流SHA通过；RE9包smoke exit0，源包当前代码与压缩SHA校验通过。载荷源码b8f76238；公开镜像未上传。

持锁+15秒看门狗部署双游戏。剑星addon0f4bcf62…；鬼武者根及_storage_runtime ced2df19…；各80模块、93Small权重与shader读回通过。custom/native两层MP0.5/HOT1/F9，剑星MODE0、鬼武者MODE1保留；原配置和全部文件逐位备份，rollback.ps1齐全。已安装鬼武者runtime smoke实际Small31块、exit0。备份D:\DLSSNR-Lab\game-install-20261018\backup-20261018-050250，GPU锁已释放。

[正式发行回执](releases/0.44/README.md) · [双游戏安装/回滚](results/game-install-20261018/README.md)。没有代Zero启动游戏实玩。

## 2026-10-18 F9实机回归缺口与紧急修复

鳩按实机日志定位：剑星OneShot提前取DirectOutput，Process重建后返回悬垂纹理；同尺寸不应重建，权重清空又导致延迟producer wait之后同步上传死锁。鬼武者WindowsApps只读路径导致F9落盘失败/轮询覆盖内存。已按上述机制修复，补cold/warm/AE的未完成producer栅栏回归、1080/900 direct输出与overlay保留测试，以及只读配置即时/跨tick和外部编辑覆盖测试。

工程失误另记：本次新GPU脚本Idle把game-check文字+bool作为返回数组，busy误判true，首轮GPU回归在后台鬼武者PID27916仍运行时误执行；没有修改游戏目录，作业已结束、锁已释放。已修Out-Host分离输出、实际busy验证WAIT且不拿锁，向Zero/鳩明确披露；初轮不计正式验收，等完全退出后-idle-r2重跑。

### F9空闲正式回归与最终部署4764e4f1

后台鬼武者退出后，-idle-r2严格重验通过；补齐同尺寸MODE1需同时切换head/blend后，4764e4f1再跑-idle-r3全门通过。受控producer栅栏验证返回时producer未完成；预热两模型的首次0.5→1与回环Upload增量0。1080保留D3D输出，900重建后新取DirectOutput并实际复制/读回；Small回环逐位一致，Overlay抑制保持。

新增history-row-cache实际GPU32系数匹配、1080 MODE1 0.5→1→0.5与冷启动seed1输出逐位一致，blend从0.76708984375→0.73974609375→0.76708984375。最终Original0900/Small65D7 Gold不变，runtime模式循环与Onimusha镜像配置路径通过。已重装4764e4f1并smoke0，备份game-install-20261018-f9fix-r3/backup-20261018-102323，三包最终回读中。

### 0.44 F9最终交付4764e4f1

同尺寸MODE1需一起换head和blend，已补缓存两模型head、切换系数并reset，GPU32系数与冷启动history图像逐位回环通过。最终-idle-r3 cold/warm/AE/deferred/direct/history/Gold/runtime/Xbox路径全部通过，源码4764e4f1；addon4dc97d06…、runtimec10d827f…。最终双游戏重装与smoke0，Zero 实玩确认《剑星》与《鬼武者》内按 F9 切换均正常生效；备份game-install-20261018-f9fix-r3/backup-20261018-102323，rollback齐全。

最终三包447,952,711B（cf812fe7…）、478,150,706B（40b4021a…）、533,121,864B（0f10b06a…），附zip.sha256；每包44shader、80模块/93Small资产、ZIP全文件回读及独立复验、RE9smoke0和源包核对通过。旧包与中间构建保留history，GPU锁释放。用户随后上传 0.44 公开镜像：[夸克](https://pan.quark.cn/s/ea87c0ffd257) · [Gofile](https://gofile.io/d/YCNT57JP)，README/CHANGELOG/releases/WorkingPlan已同步更新；详releases/0.44/README.md。初轮门禁错误已经披露修复，最终验收不采纳busy首轮数据。

### 2026-10-23：1x同1088纯NN差距刷新与a9 ACO解剖，发现compact资格漏覆盖

第一阶段只研究，生产/游戏/驱动/发行包未改。当前HEAD c24e3a97生成纯NN O2 host，gfx1201 40模块逐SHA同10-17最终与当前SUMS；旧HIP 044f功能基线/旧M d1185d2及SPV/plan全部锁定。1920×1088/640token、同encoded half-exact gradient、全71/MP1/FAST1/Style1/seed0/AE0/historyoff，80warm+160timed/slot：三轮NMMN当前8.533050354 vs旧M7.867127083ms，剩差0.665923271；另三轮ONNO旧H8.923111219→当前8.554250771ms，同批追回0.368860448。新旧H全输出仍153ac…47f1；M仍624d…a612且finite。旧M无逐帧p99，不编造；旧10-06绝对值不与本轮相减作优化证据。

发现InitCompactContract/Projection只准1920×1152，1088仍640token却没接通compact W16/ViT byte链；独立trace三帧证当前实际161次（非原假设153），8次vit_pack_input仍在。隔离候选只将两处H门扩1088，模块不变，三帧trace153次/pack0/compact1/entryexit1，全部Gold同；三轮NCCN 8.570213292→8.354502188ms（−0.215711104ms/−2.517%），每轮均值/p99都改善，pooled p99 8.816156→8.5707569。尚未合生产/做产品兼容，候选未与M同批，不把两差直接相减宣布新剩差。PDL capability1但actual anyorder0/lifetime_pdl_calls0，SP六次launch都计入。

锁版a9b13dd4/v0.0.4-win-preview完整源码与52个SPV→.nrp逐一映射/反汇编完成，0 unknown。TCHAIN移barrier不减dispatch，少派发主要来自完整持久stage；N-pair配输出N片，我W16配K片；两边已有med3/成对FP8/VOPD，新ISA候选为C32首级FMA_MIX打包常量/操作数安排、N-pair供数等。ACO并非普遍更低VGPR，静态指令不能代动态成本；整核ACO→HIP需PAL/HSA ABI适配。a9作者7.21→5.70ms是组合模式发布值，本轮未实机运行新版，不冒最新ACO已追平。初次预检误用旧调研1152输入SHA当valid，被GPU前拦住并按原input.json纠正，原失败保留。全部GPU遵game-check/≥100GB/gpu.lock/15秒看门狗，最终释放锁。完整原CSV/命令/模块身份/独立复算/候选补丁及优先清单见 [gap-refresh-20261023](results/gap-refresh-20261023/README.md)。

### 2026-10-23 第二阶段刀1：1088/900 compact资格正式接通

两处InitCompact资格新增Original-only1920×1088与1600×960，原1152行为及其它资格保留。完整Frame三档各3轮ABBA(240弃80/960样本每arm)：900 6.548212→6.477252ms(−0.070960)，1088 9.090362→8.880001ms(−0.210362)；两档每轮mean/p99均改善。1152 9.167039→9.160298ms按中性，R2/R3p99+0.043854/+0.035559原样保留，pooled尾下降不冒每轮全胜。三图样54帧Gold同，最终Original资格收窄后另9帧Gold同。三档default/FAST0/冷MP2/热1→2→1及四类缺导出故障注入共144帧兼容trace全对照逐位同；缺导出是API边界注入，不冒实际删export。实际1088 161→153、900165→157，1152保持153。生产源已修改，final-frame/source身份封存；未装游戏/发包。详results/gap-phase2-20261023/knife1，刀1闭环后进入刀2。

### 2026-10-23 第二阶段刀2：C32 FMA_MIX数学通过，性能止负

首级Horner用一SGPR打包0xab28/0x3728两个精确half常量，g与结果F32不变；LLVM MC拒直接packed literal的失败保留。MIX+ALT与MIX两模块.text完全同，只测MIX。热点代码少64–72B，资源/VOPD/VALU/WMMA/wait不变；54帧三档三图样Gold全同。基于刀1最终host完整Frame各3轮ABBA：900−0.007787ms但R2退，1088+0.010902ms/p99+0.031698ms，1152−0.000474ms近零。STOP，不合生产，不扩大到其它通道，不刷轮；失败总账已记。初次系统音频trace-agent名称误匹配在GPU前拦住，查路径后收窄实验进程名，原日志留存且没杀系统进程。见results/gap-phase2-20261023/knife2，进入刀3。

### 2026-10-23 第二阶段刀3：C512 N-pair通过Gold，目标1088无收益止负

在独立host/模块实现projection普通/bskip N-pair，保持逐输出K16序/skip-first/末量化，独立缓存tag且两export齐才用；CPU穷尽矩阵索引/尾区保持通过。正式COMGR baseline整HSACO同生产，candidate旧89Fn代码/资源全同，仅新增2Fn，VGPR100→80。66帧Gold含普通F32skip和实际旧模块缺导出回退全同；三档各3轮FrameABBA同host/同candidate模块仅NR0/1，candidate每slot实际3856calls。900mean+0.000909ms近零，1088三轮均值全退/pooled+0.006152ms、p99+0.021655；1152−0.011935ms但R2p99+0.036063、部分漂移较大。整体STOP，不合生产、不扫参，保1152小正值不抹。三刀结题：仅刀1资格进生产源码，刀2/3负账齐；游戏/驱动/正式包未动。详results/gap-phase2-20261023。

第二阶段最终交付补记：三刀均经独立复算/源与产物身份审计，最终正式addon/runtime按仓库脚本CPU构建通过，仅含刀1生产资格改动。addon SHA e1b75a99…9b71a9，runtime c77cb78c…c1a8ca，产物/home/lmxxf/work/dlss5-gap-phase2-20261023/final-payload；源清单/日志见results/gap-phase2-20261023/final-build。原80 HSACO未重编，未另做最终DLL API smoke，完整Frame/兼容GPU门见各刀回执；未安装/发布，gpu.lock最终释放。

### 2026-10-23 第三阶段A：精确QT32共享K/V，当前条件仍负

纠正前提：旧10-01 M32也19 SAME，并非只限有损。本轮依用户明确授权保当前FAST1数学，真实ceil(n/32)grid、400尾编译期单链；正式COMGR旧89Fn同，新98VGPR/0LDS/private0。源/loopISA证明K/V逻辑请求−48%/−50%、WMMA同（不冒DRAM字节）。60帧Gold/缺导出回退同；三档各3轮FrameABBA，900+0.012039ms、1088+0.097277ms、1152+0.090150ms；1088/1152每轮mean/p99都退。STOP，不合生产，不扫变体，完整证据results/gap-phase3-20261023/A；开始B单边tile-counter。

### 2026-10-23 第三阶段B：C512单边链化有真重叠，但九轮净负

隔离FFN→Projection16条边、64bit累计counter、有界acquire/epoch极限关闭。timeout不读未完成数据、完整零写+sticky最终拒批；候选独有hold/释放计入Frame，NR0保生产。复现发现实际COMGR producer空opts才整SHA同生产，脚本可选max-ilp并非现载荷实际选项；错误组保留未上GPU。首次缺池在externalwait后被保护拦下，修NR1冷预热最多8次/连续2cycle稳定，不清counter/status，原失败保留。

独立超时canary/CPU复读过、84主Gold/diagnostic帧同、carry32/64极限关闭通过，强制完整Frame错误status1/exit1正确拒批。trace三档16边全部存在consumer真WMMA完成早于末producer发布，ordered对照0；计时关trace。完整Frame三档各3轮ABBA：900+0.086410ms、1088+0.124866ms、1152+0.052815ms，九轮mean/p99全退。STOP不进生产，不单归因某种同步成本。详results/gap-phase3-20261023/B；继续C实际load/wait审计。

### 2026-10-23 第三阶段C：依赖安全的末尾等待窗口，整网近零止负

查C512两处/C128十一处/C256十八处wait0均真RAW，不直接放宽。仅C512 W16最后两K16步用compiler barrier将7条独立WMMA置wait0前，最后依赖两条仍受保护。正式COMGR仅目标Fn变、其它112原样，188VGPR/25SGPR/6144LDS/private0及大小/全部opcode计数同；54帧Gold同。原第三轮期间root准备后续对账文件可能CPU/I/O干扰，统计前记录排除、保原轮，仅补quiet r3。有效3轮FrameABBA900+0.004076ms、1088−0.004563ms（不稳且有尾反例）、1152+0.000788ms，STOP不合生产。证据results/gap-phase3-20261023/C；三项均已独立实现与实测，最终生产仍第二阶段刀1，正在收尾旧M同批差距刷新。

第三阶段最终差距刷新：实际生产仅含第二阶段刀1，以final-pure543a、原40模块，和锁旧M d1185d2做同1088/640tokens、Style1/seed0/FAST1/full71/MP1/AE0/historyoff的三轮NMMN（80warm+160timed，每arm960）。生产8.366908281ms、M7.888277083ms，实测差0.478631198ms（6.0676%）；三轮差0.471143/0.477641/0.487110ms。各自raw同历史153ac/624d，M资产前后未变、无M逐帧p99。独立复算通过，取代约0.45跨批估计；新a9仍未本机同口径测。A/B/C均不合生产，最后源码仍仅刀1两条件；完整结题results/gap-phase3-20261023。

### 2026-10-23 第四阶段刀1：ViT V-fragment成对布局采纳

按用户head-major布局，producer V尾每lane16B原地写，consumer每K16直接b128读，QK/波数/数学/容量不变。先导tile-major只CPU/编译未上GPU；最终正式ISA旧89/101Fn保持，consumer loop123→91指令、84→62VALU、WMMA5同，16V-u8→1b128，producerV单b128。真实GPU QKV逆排/AV/canary/reuse门与独立CPU raw回读过，54帧三档三图样Gold同。Frame三档各3ABBA：900−0.015497ms、1088−0.058899、1152−0.051794，9mean全胜/pooled尾均好，900R3p99+4.693µs原留。默认/禁用/优先级/不对称缺export/FAST0/MP2/AE回退39帧通过。生产默认1（NR诊断优先，DLSS5_HIP_VIT_VFRAG可禁），保Original FAST1 MP1显式AE0三档，不暗扩Small/AE。

1088纯NN真实旧生产vs新默认三轮：8.341779→8.305514ms，追回36.265µs，三轮mean/p99均改善，raw仍153ac。双架构80模块完整，清单仅换4项、实际ELF架构全核，gfx1200无实机。首次probe误以为物理f32存在，在CPU读文件拒绝，改用生产ReadWeights回落f16（无数学改动）并保旧日志。完整证据results/gap-phase4-20261023/knife1；游戏/发行包未改，开始Small资格刀2。

### 2026-10-23 第四阶段刀2：Small900/720/free1088 compact资格采纳

集中helper新增Small1600×1024/448、1344×768/288、1920×1088/640，保原Original/1152/设备/其它门，VF Small不扩。常规Small1080_ROWS1088仍重算1152；实跑1088用free输入1920×1072。72FrameGold、24帧bareRGB32F trace同：900/72095→87、free108891→83，8pack清零，attention实际640/400/640，Up39走byte-input/F32-output；每新档活动weights容量精确省120MiB。

108帧public模式/direct重建/AE/静态MODE1历史兼容通过。MODE1首冷>250ms导致双方序列001011，源码规则与逐帧SHA核同，没有改历史行为。完整Frame三档各3ABBA，9004.350586→4.286140ms（−64.446µs）、7203.061927→3.007794（−54.133µs）、free10885.587080→5.341975（−245.105µs），九轮mean/p99全胜。两个初始化VF统计误门由真实预热日志纠正，原子进程exit0数据留档；不把预热Original8次算Small。采纳host资格，80模块复用刀1；最终默认回归/宿主构建随后封存，结果gap-phase4-20261023/knife2。

### 第四阶段最终闭环

两刀均已合入生产。最终重编标准/free1072 Frame与已测候选SHA完全一致，7档默认回归21帧全部Gold 0-diff；完整addon/runtime正式构建成功，双架构80模块与生产SHA/ELF核同。证据及最终payload身份见 [第四阶段报告](results/gap-phase4-20261023/README.md) 与 final-build/receipt.json。所有GPU作业已结束、gpu.lock已释放。gfx1200仅编译核验；游戏安装及已发布ZIP未动。

### 2026-10-23 第五阶段刀1：仅采纳C512成对FP8打包

正式COMGR pair-only：c512_qkv_attention_compact_w16的cvt144→72、总指令2483→2104、VALU1367→1025，VGPR188/LDS6144/private0不变；113导出仅两个compact核变化。基线/三候选三档三图样共108帧Gold、独立GPU norm/pack探针及CPU raw复核全通过；非W16三档paired18帧通过。

pair-only Frame900/1088/1152分别−23.331/−11.701/−18.815µs，9mean全改善，三档pooled p99改善，分轮尾反例留；同1088纯NN8.342063→8.326007ms（−16.055µs），3mean全改善、pooledp99−6.303µs。permlanex16单项1088+4.869µs，组合虽mean略好但pure pooledp99+184.606µs，均不进生产，serial_norm保持旧实现。正式pair-only源与双架构编译完成，gfx1201最终113函数码/资源全同已测pair；gfx1200只编译核验。证据results/gap-phase5-20261023/knife1。

### 2026-10-23 第五阶段刀2：V-fragment扩围Small/AE采纳

Small900/720/常规1080/free1088的V-fragment成对资格已合生产，保持精确shape/token绑定；Original既有三档与Small四档AE允许，AE策略/anchor/默认idle阈值未改。72帧SmallGold、84帧受控AE1、24帧默认500ms、48帧AE2/3共228帧跨臂0-diff、完整AECSV同。mode2实际全计算，mode3为0,1,1,1,0,1；不拿AE0性能当复用帧收益。

四档各3ABBA：9004.268004→4.244956ms（−23.049µs），7202.994176→2.989513（−4.663µs），11525.559334→5.499693（−59.642µs），free10885.382278→5.317102（−65.176µs）。11/12mean改善，12/12p99改善；720R2mean+2.378µs保留，该档仅微小收益。最终header/两个重编Frame复现实测候选身份；正式默认/off回归与完整构建收尾中。证据results/gap-phase5-20261023/knife2。

### 第五阶段最终闭环

生产最终默认/on/off 9组36帧跨臂输出与AE日志全部一致，两个Frame重编SHA复现实测候选。正式addon/runtime构建成功，双架构80模块逐SHA/ELF核验；最终payload位于`/home/lmxxf/work/dlss5-gap-phase5-20261023/final-payload`，身份见results/gap-phase5-20261023/final-build。刀1只收pair打包，刀2收Small/AE资格，permlane与combined明确STOP。gpu.lock已释放、无实验进程，剩余磁盘279784194048B；游戏安装与已发布ZIP未改。完整报告[第五阶段](results/gap-phase5-20261023/README.md)。

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

回执：[双游戏安装](results/game-install-20261024/README.md)、[0.45发行](releases/0.45/README.md)。用户已提供 0.45 公开下载入口：[夸克](https://pan.quark.cn/s/0ccb3eff675b) · [Gofile](https://gofile.io/d/hmrcTjuS)，README/CHANGELOG/releases/WorkingPlan已同步更新。

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


### 2026-10-31 闇 Round 5：独立实机追查与 norm900 无损分母归约

完整报告：`Development/results/yami-round5-20261031/GAP-EVALUATION-ROUND5-20261031.md`。以 `b60e6a8c` 为基线，当前会话亲自执行，无子代理。

- ViT 成对指数映射 Gold 失败已定位为 COMGR 对 half 向量分量左值直接 bit_cast 取错元素；整数半字提取修复通过七档 Gold。100MHz attention 阶段 Original1088/Small1080 分别快 10.200/11.103µs，但最终整帧门槛未稳定通过，未合入。
- 当前基线 R2-D 安静交错复核支持保留 Both；MH-only 在 Original1088/1152 三轮均值均回退。直接修复 Round4 动态计数 kt/qt 因子12→48及 SP 网格；Original1088 R4 目标 lshr/xor 消除计数改为35,343,936，不能当作包含新增指令的净 VALU 计数。旧历史条目的8,835,984口径由此更正。
- 实测推进 C32 四种带精确性保护的归约、C64/C128/C256 单 LDS 平面、C512 clamp/倒数、ViT 编译/宿主绑定等分支。四个较大最终组合均未通过预登记长批门槛，失败结果完整保留。LDS occupancy API 的 blocks/MP 不能当作 waves/SIMD 或动态驻留；旧 Round2 相关断言撤回。
- **仅整合 gfx1201 现有 c32-wave1-fast-norm900 分支的 guarded 四路 dot2 分母归约**：独立三轮 ABBA、800帧丢160、每臂3840样本，Original960 mean 6.268203→6.242602ms（−25.601µs/−0.408%），p99 −30.551µs，三轮 mean/p99 全胜。保持原 host dispatch 门；其他标准档位执行代码不变。
- 七档 Gold、最终20配置138对输出文件全部0 bit-diff。双架构80对象与当前源码/清单匹配；保存于该轮 `payload/HIP/`。六个正式宿主沿用 b60 并校验 SHA。补齐 C512 deep 分架构编译选项，避免重建偏离基线。
- 远端最终核验1298文件无漂移、无遗留任务、gpu.lock 已释放。源码及 hip/SHA256SUMS 已在工作区整合，未 commit/push/游戏部署。**未证明当前架构的性能上限，不认可“所有无损收益已抠干”。**
