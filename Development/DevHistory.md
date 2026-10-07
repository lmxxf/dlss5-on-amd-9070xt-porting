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
- ReplayAuto新组合仅CPU待源/接口节点与owner合同审（事件GetEvent7、162节点/DAG、资格每帧复核/图先lease销）。未GPU/输出/收益，当前STOP的ReplayOff不能悄替换产品Auto。

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
