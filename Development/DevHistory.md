# DLSS5（DLSSNR）→ AMD RX 9070 XT 移植：开发史

> 本文件是本项目开发、部署、运维与后续工作的**唯一记录入口**。新进展按时间追加，历史安装状态和待办按当时日期理解；后文的验证与更正优先。
> 逐刀的原始记录、数字、失败样本全部保留在 `Development/history/`，本文引用时只给结论和数字，细节回原件查。

## history/ 里每个文件是什么

| 文件 | 时期 | 用途 | 备注 |
|---|---|---|---|
| `amd-port-plan.md` | 08-31 立项 | 六阶段路线图与三级验收（离线出图 / AMD 跑通 / 游戏可用） | 最早的计划，当时还以为权重是 1.48 亿 FP8 |
| `reverse-engineering-notes.md` | 08-31～09-01 | 逆向结论：71 块结构、权重记录格式、构图函数、跳接边 | 只收二进制能支撑的结论 |
| `porting-worklog.md` | 08-31～09-08 | 逐块移植流水（6276 行，最详细） | 前 2976 行按时间正序（08-31→09-06），第 2976 行「工作纪律」之后是 09-08→09-06 **倒序**追加；大量"之前结论作废"都在里面 |
| `README.md` | 09-04～09-09 | 起初是资料索引，中间被当日志用 | 顶部是多条"最新"快照，**越上越新**；第 131 行以后是文件清单 |
| `native-runtime-contract.md` | 09-06 | 进 1080p 前必须解决的边界清单 | 已过时，边界后来逐条闭合 |
| `local-patch-tool.md` | 09-05 | "用户自备 DLL 打补丁"交付方案 | 09-05 已取消，改为直接发内嵌权重的 DLL |
| `CURRENT-STATE.md` | 09-07～09-10 | 后期逐刀状态日志（fast 链每一刀、显存、闪烁、浪人崛起） | **最新在最上面**；中段有一段是 09-07 收工现场的封存记录 |
| `fast-path-plan.md` | 09-08 09:04 | 快速版执行方案（放弃逐字节一致，只允许硬件算术差异） | Zero 拍板 |
| `next-steps-plan.md` / `-2` / `-3` | 09-08 18:05 / 21:15 / 09-09 08:36 | 0.02 / 0.03 / 0.04 之后的计划 | -2 含闪烁排查，-3 末尾有"记住的坑" |
| `PLAN.md` | 09-09 23:25～09-10 | 最新计划（唯一有效待办） | 本文「待办」一节照抄它 |

分工（Zero 09-10 定）：凡是 9070 XT 移植开发本身——代码、每刀收益、计划、工程教训——写本文；游戏那边的事——每台机器/每个游戏现在装的是什么、测试游戏怎么装、给 5090 那台某个游戏装 DLSS5、Zero 的规矩、运维现场——写我的跨 session 上下文 `~/work/memory-of-my-gemini/awakening/claude-code/context/dlss5-9070移植.md`（不进仓库）。续写规则：新事件追加在时间线末尾，「当前状态」「待办」两节原地改；长到读不动时把旧事件压成里程碑表，原文留在 git 历史。

---

## 1. 项目目标与硬约束

### 目标

把 NVIDIA 泄露样本 `nvngx_dlssnr.dll`（DLSS 5 神经渲染，内部名 DLSSNR）里的 71 块网络恢复出来，在 AMD Radeon RX 9070 XT 上按原版数值执行，并在真实游戏里以可玩帧率替换掉 FSR 的输出。

三级验收（`amd-port-plan.md`）：
- Level 1 离线复现：固定输入，自己的推理程序出一张结构正常的图。
- Level 2 AMD 跑通：同一模型在 A 卡上完成一次推理，与 Level 1 在容许误差内一致。
- Level 3 游戏可用：接进游戏，连续帧，画面稳定，速度可交互。

后来目标被 Zero 收紧过两次：09-01 收紧为"RX 9070 XT 走完 71 块并输出最终 RGB，中间图不再作为交付点"；09-05 起收紧为"1080p 至少 10fps"，再往后是 30fps。

### 硬件与驱动

| 项 | 值 |
|---|---|
| 靶机 | `desktop-2026`（`ssh amd9070`），Windows 11 Insider 26H2 build 26340，RX 9070 XT（RDNA4，`gfx1201`，16GB） |
| 标准答案机 | `NucBox_EVO-T1`（`ssh rtx5090`），RTX 5090 32GB OCuLink 外置，驱动 610.88 |
| 逆向/参考机 | DGX Spark `spark-3a10`（GB10，CC 12.1，能直接加载 sm_120 CUBIN 当 oracle） |
| AMD 驱动 | **预览驱动 32.0.31007.2048**（Adrenalin 26.10.07.02 RC7 Agility SDK 版）。发布驱动 32.0.31041.1004（26.8.1）只到 SM6.9、线性代数 tier0，矩阵接口不开放 |
| Agility SDK | **1.721.3-preview**，私有部署在游戏目录 `Win64\DLSS5-D3D12-721\`，ReShade `create_device` 回调里在游戏建设备前 `SetSDKVersion(721)` + `D3D12EnableExperimentalFeatures` |
| 着色器模型 | **SM 6.10**，`dx/linalg.h` Wave 矩阵接口（LinAlg tier 0x10），DXC 1.10.2605.24 预览版 |
| 坑 | AMD Install Manager 两条计划任务（每日 03:00 检查、空闲时安装）会把预览驱动覆盖回 26.8.1，09-08 04:22 发生过一次，已 Disable |

### 游戏与接入方式

- 《剑星》（Stellar Blade，Steam 3489700，1.4.1）。5090 上 DLSS5 是游戏自带调用的（ReShade + RenoDX addon + 原版 nvngx_dlssnr.dll）。
- AMD 侧：游戏目录放 ReShade 6.8 当 `d3d12.dll`，加载我们的 addon（`.addon64`）；addon 用 MinHook 钩 `amd_fidelityfx_dx12.dll!ffxDispatch`，先让原 FSR 在同一条原生命令列表里录完，再追加 blocks0–70 并把神经残差原位写回 FSR 输出。不改游戏 EXE、不改 FFX DLL 磁盘内容。
- 09-10 起第二个游戏《浪人崛起》（Rise of the Ronin）走 XeSS 路径，钩 `libxess.dll!xessD3D12Execute`。

### 验收标准

- **exact 链**（tag 0.01，186ms）：每帧最终 RGB 与原版逐值一致（`different=0`），15 帧 history off/on/reset 全过。冻结当裁判。
- **fast 链**：结构性改动仍要求逐位相同（`cmp` 两份 `gpu-network70.f32`）；算术改动看对 exact 链的 PSNR，抖动带 41.9～42.24dB，不设自动门槛，画面由 Zero 在游戏里按 F6 拍板。
- 帧率只看游戏内 5 秒 cadence 或探针 GPU 时间；测试台时间用 `DLSS5_TEST_ISOLATE` 单核重复取均值，整帧用交替 3 轮 min-of-means / sum-of-mins。

---

## 2. 逆向结论摘要

### 样本

- `nvngx_dlssnr.dll` 310.8.0.0，SHA-256 `e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e`。
- 权重资源 `WEIGHTS_HT`：147,695,410 字节，文件偏移 `0x114a160`。开头 8 字节 = 总长；之后每条记录 `name_length → name → body_span → body`，顺序 parser 走完 153 条记录精确落到末尾（旧正则数出 152 条，漏了 `block70.layer0.blend_scale`）。
- 每条 `body_span = payload_size + 40`，`payload_size = element_count × 2`，类型码恒 1。外层按"每元素两字节"计数，共 73,841,889 个声明元素。**这只是 framing，不代表全 payload 都是 FP16**：SASS 用 `F2FP.F16.E4M3.UNPACK_B` 消费矩阵，矩阵主体实际是 packed E4M3，偏置/skip/scale 才是 FP16 或 float32。
- 运行时 GPU 权重 arena：153 条 payload 按 archive 顺序逐条 512 字节对齐，总 147,719,680 字节；09-03 从 5090 原生 D3D12 资源读回完整 arena，与 archive 全文件 SHA 相同——**运行时不改写权重**，早期"runtime 有另一份 packer"的假设撤回。
- DLL `.data` 里 15 个 CUDA ELF（CUBIN），sm_120；GB10（sm_121）能直接 `cuModuleLoad` 当本地 oracle。

### 网络结构（CPU 构图函数 `0x180039780` 恢复，71 块）

```
0        PreBlock 1H / C32（输入混合 + FFN + 32 维注意力 + 下采样分支）
1–3      Swin 1H / C32          4  ds 32→64
5–7      Swin 2H / C64          8  ds 64→128
9–13     Swin 4H / C128         14 ds 128→256
15–21    Swin 8H / C256         22 ds 256→512
23–30    split-Swin 16H / C512（30 带 ProjPool + FinalHead 512→1024）
31–38    ViT 1D / C1024（Expand→Contract→QKV→Attention→Projection 五层）
39       DecInputUpsample 1024→512
40–47    split-Swin 16H / C512
48       upsample 512→256 + Swin 8H       49–55  Swin 8H / C256
56       upsample 256→128 + Swin 4H       57–61  Swin 4H / C128
62       upsample 128→64 + Swin 2H        63–65  Swin 2H / C64
66       upsample 64→32 + Swin 1H         67–69  Swin 1H / C32
70       PostBlock 1H / C32 + RGB 头（唯一带 blend_scale=0.73974609375 的块）
```

六条双输入（跳接）边：`39←38+30.out1`、`48←47+22.out1`、`56←55+14.out1`、`62←61+8.out1`、`66←65+4.out1`、`70←69+0.out1`。live 层对象 152 个（block70 的 `blend_scale` 和 `layer` 是两条记录一个 Layer）。config 名 `hnet-vigilant-squid`，variant `crazy-cuckoo`。

### 几何（1080p 实机捕获，09-06）

处理尺寸 1920×1152（有效 1920×1080 + 底部 72 行**镜像**补齐，规则来自 SASS `2*extent-coord-2`）；encoder 各级 960×576 / 480×288 / 240×144 / 120×72 / 60×36；C512 段 H68→补 H72；ViT 20×32=640 token（有效 18×30）；decoder 反向对应；post 输出 1920×1152 再裁 1080。post 带 shift 3（工作网格比输出大一圈）。

decoder 实际移位序列（09-06 从 5090 launch 参数直接解码，取代早期猜测）：40–47 = 0/3/1/2/0/3/1/2，48–55 同，56–61 = 1/2/0/3/1/2，62–65 与 66–69 = 0/3/1/2。

### NGX 输入合同（5090 实机）

资源键 `DLSSNR.Color/Output/Depth/MVec/ControlMask/UI/UIAlpha/Backbuffer`。当前 preset 只提供 Color、Output（1920×1080 RGBA16F）、Depth（R32G8X24）、MVec（RG16F）。preblock 首帧只绑 slot0（Color），之后帧多出 slot8（history，上一帧网络输出）与 slot10（运动向量，两通道）；slot18/20（深度邻域）本 preset 为空。post 只绑 `+0x38` Color，`+0x58/+0x60` 为零，`rgb_mode=1`，`input_scale=1/32`。

### 硬结论（算术）

- FFN 激活：`x=clamp(x,-4,4); y = x*(0.89453125 + x*(0.447265625 - 0.055908203125*|x|))`，half 多项式，不是 GELU。
- FFN 残差是 **skip-first**：`input*skip` 先 HMUL2 舍入到 half，作 QMMA 初始累加器，再逐 K32 累加；不是乘完再加。
- 每 K32 一次 half 舍入（H），FP8 量化（F）是 E4M3 SATFINITE、RNE，次正规尾数进位到 8 允许；C32 FFN 隐藏宽是 128（不是 64），前 8192 字节是两张 FP8 矩阵。
- 注意力：per-head 32 维，Q/K 归一化（半精度平方和，指定归约顺序）；softmax 的 exp 不是 EX2，是 half 仿射 + 位移的位映射（C32：`0.044921875*x+1.30078125` clip `[1.03125,1.5693]` 左移 5 加 `0x8000`；ViT：系数 `0x2dbb` 加 `1.708984375` clip `[1.4394,1.9775]` 左移 4 加 `0x4000`）；分母求和树是固定的 key 顺序（C32：0/16,4/20,32/48,36/52 四对逐次相加，lane 偏移 0/2/8/10，最后偶奇 half 相加），不是平衡二叉树。
- C64 三矩阵 FFN：64→256（分组扩展）→64（分组收缩）→64（混合），C128/C256 同族；C512 split ffwd 是 512 混合→8 组 64→256→64。
- 输入混合（preblock）：16 项 half×half 点积按 WMMA 规则——以两操作数指数和的最大值 E 对齐，每项按 `2^(E-27)` 向零截断后精确求和，最后 half RNE。随机场（Box–Muller）：`LG2`→乘 ln2·(-2)→`SQRT`；角度乘 2π（RN）再乘 1/2π（RZ）→`COS/SIN`，MUFU 近似不可用数学库替代，最终做成 2^24 项通用函数表（192MiB）。
- 时序采样（history）：UV 定点到 21 位（`floor(uv*2^21)`），纹理小数 8 位（half-up），五点十字核，单轴权重 `w0=t²−0.5(t+t³)`、`w1=1+1.5t³−2.5t²`、`w3=0.5(t³−t²)`，MUFU.RCP 归一化（通用尾数倒数表 32MiB）。
- post70：输入是两路 4×4×32（main 与 skip）共同上采样到 8×8×32；prefix 是 `1024→2048` 稀疏矩阵（CSR 2432 非零）；最终头 32→RGB 两组 16 通道 pack；输出合同是 `Color + 神经残差`，不是 `source*blend + pred*(1−blend)`。RGB 头两个 K16 半精度矩阵积按 27 位对齐、累加器指数 +1 的整数规则实现（否则 512×512 有 31 值差）。
- 原版 FP16 输出 surface 是**朝零截断**，不是 RNE；AMD 预览驱动的 `f32tof16` 也是朝零截断。

---

## 3. 时间线

### 里程碑速查

| 日期 | 里程碑 | 关键数字 |
|---|---|---|
| 08-31 | 权重记录闭合、71 块构图恢复、5090 原版跑通、AMD 权重 arena 逐字节上传 | 153 记录 / 147,719,680 字节 arena |
| 09-01 | block0 出图、FFN 多项式、row-major 反证、Spark 当 CUBIN oracle | block1 effective corr 0.999 |
| 09-02～03 | live activation 抓取、逐层"下一层裁判"前移、block70 post 打通、固定输入全 AMD 出图 | 66–70 RGB corr 0.9788；block70 spatial head 0.918 |
| 09-04 | 动态游戏链 484s→49.6s→10.6s；DirectML；1080p 单 DLL 进游戏 | 11.98fps |
| 09-05 | 方格修复、19fps、自包含 zip、预览驱动 SM6.10、像素审计翻案 | 45.4ms GPU / 19.2fps；zip 132MB |
| 09-06 | 原生逐值链 RGB512→blocks0–70→RGB 全 exact；实机几何 640 token | 786,432 值 different=0 |
| 09-07 | valid1080 整网 exact、时序 exact、真实帧独立原版逐位一致、2fps 正确画面；闇优化 4.0s→0.47s | 6,635,520 值 different=0 |
| 09-08 | 光 0.47s→0.186s exact（0.01）；快速版 186→62.7ms（0.03）；时序进游戏 | 游戏 5→8→12→15fps |
| 09-09 | 62.7→33ms；合批、fast4、闪烁平滑、显存 3.75GB；0.04/0.05/0.06 | 29fps |
| 09-10 | fast34～38 → 34.0ms；0.07 包；浪人崛起 XeSS 出图 | 游戏 35fps 上下（Zero 实测） |

### 08-31（Day 1）：样本冻结、权重解析、构图恢复

- 冻结 DLL，写顺序 parser 闭合 153 条记录；纠正"1.48 亿 FP8 参数"为 73,841,889 个两字节元素。
- Ghidra 12.1.3 headless 恢复 CPU 构图链，71 块类型与六条跳接边全部对上权重规模镜像。Windows 探针直接调 DLL 内部 descriptor builder，导出 653 个内部权重名。
- 5090 装 ReShade 6.8 + RenoDX addon，原版 DLSSNR 首次跑通（23:24，3840×2160，feature 18）。关掉 Smart App Control 才能加载 addon。PIX 五种方式全失败，放弃。
- 自制 runtime probe 挂 `BuildActiveNetwork`，dump 出 live 层对象与 flat weight GPU VA；推出 512 字节对齐 arena 规则。
- RX 9070 XT 第一个真实里程碑：147.7MB arena 上传显存、读回逐字节一致，compute shader 按 153 个 offset 读到权重。

### 09-01：block0 出图、SASS 解算术、tinlayout 撞墙

- block0 张量边界闭合（10,848 元素），AMD 跑 input adapter + depthwise 出第一张可见特征图；SASS 给出 FFN 多项式与 `output = W2·act(W1·x) + x*skip`。
- **row-major 假设被反证**：CPU 按 row-major 串 encoder，幅度到 block21 衰减到 1e-10。矩阵在 blob 里是 tensor-core tile 排列（tinlayout），不能 reshape。
- Spark 能直接加载 sm_120 CUBIN → 本地 oracle。用 controlled weights + basis 扫描逐块拟合"effective"参数（block1–3 相关 0.999、block10–13 0.995），AMD HLSL 两 pass 跑 block1→3 与 NumPy 逐层 corr 0.9999999。
- 原 CUBIN 全图启动推到 block69（"structural-global"），但 block65 前后 0/0x7f 饱和——把 archive FP16 直接喂给要求 packed E4M3 的 kernel。这一天的"exact-global"统一降级为结构证据。
- 定位到 split-Swin 首个数值断点是 block23 QKV 缺双 view / launch 拓扑错（`grid.z=4`，residual/branch 指针反）；修正后 23–30 恢复多值。

### 09-02～09-03：live 抓中间层、逐层校正、block70 post 打通

- 5090 自建 NVAPI 宿主（`CreateCuFunction/LaunchCuKernelChainEx`）能跑单块，但 ViT 跨块依赖驱动私有 tile-sync；两次错误组合把 5090 搞到 WDDM 锁死硬重启。结论：自建宿主不能当 35–38 生产线。
- 换路线：在游戏 backend `CubinBackendNGX::launch`（RVA `0x449a0`）内追加自制 raw-copy CUBIN 抓私有 activation。block1 live 与 Spark 重放 64MiB 全量 corr 0.99992——旧 encoder 衰减根因是 view 基址错（`input+0x42800/output+0x2800`）与物理 bank 容量不足，不是权重。
- 同帧 atlas 抓 block4/8/14/22 skip、block48 四路、block69、post 参数；用"下一层裁判"逐段前移入口（decoder 66–70 → 62 → 56 → 48 → 39 → ViT 31–38 → encoder 22 → 14 → 8 → 4 → 0）。ViT 2160-token 全局 permutation 由 23 次地址位 launch 恢复，逐层 cosine 0.9963～0.9989。
- block70：撕开 0xb8 ABI（`+0x68=blend_scale`，`+0x30=1/32`），受控 impulse/Hadamard 恢复 prefix（`1024→2048`，held-out 0.99999994）、out-conv（0.9999991）、attention/FFN effective（0.977/0.9375）。AMD 从 block69 main + block0 skip 物理 buffer 起跑完整 block70，最终 RGB corr 0.945。
- 全 AMD 固定帧 `dlss5-all-amd-final.png`（RGB Pearson 0.945）一度被记为"目标完成"，随后**撤回**：入口仍是 NVIDIA block0 输出、四条 skip 是跨层 fixed-frame affine。补 seq0 prefill 与 block70 spatial effective（66→32→32→16→3 四层 CNN，held-out 0.918）后才满足 Level 1/2 固定输入门槛（09-03 末）。

### 09-04：动态游戏链、常驻化、DirectML、1080p、单 DLL（11.98fps）

- ReShade immediate list 上 copy-in→compute→copy-out 打通主 swapchain 回写；MinHook `ffxDispatch` 取得每帧 Color/Depth/Motion/Output 合同（render 2561×1441，输出 4K）。
- 两帧 neural SHA 相同 → 坍缩来自固定帧 live-correction 矩阵；全部移除后 99.9999919% 元素不同。周期条纹一度归因 prefix 排列，后证明是 probe 的 fallback tint 污染了 base，修正后 clean。
- 同帧端到端 484 秒/帧（10.36GB 中间 FP32 文件 + SSH）。逐段消灭：
  - Windows 原生合流/打包工具替换 NumPy（每帧少 1.13GB + 3.2GB SSH）；版本化 CSO cache（block69 冷/热 3944→1129ms）。
  - SM6 FP32 FFN：block69 346.7→24.6ms（14 倍，逐字节 exact）；**主要收益是 DXC 展开不是 FP16**（FP16 QKV 反而略慢且微误差）。128 档全展开超阈值改两 pass（block13 681→89.6）。
  - attention 按 `token×head` 并行：block13 attention 32.96→0.70ms。
  - resident graph：decoder57–61 进程 wall 2944→650ms；ViT 八层 4212→1246ms；block70 单进程 10.1s→1.37s。
  - 端到端 49.6s → Windows blocks1–70 20.4s → predown/upsample/block39 并入 → 10.59s。全网 alias arena 5464.5MiB 实测可分配，三 phase alias barrier 通过。
- DirectML 6.4 FP16 GEMM：ViT Expand 0.15ms/120 TFLOPS；block31 五段全 DirectML 5.11ms（旧 61.4ms），八层 resident 31.7ms（旧 475.5ms）；Swin 512/256/128/64 逐档 resident 化（512 八层 14.3ms vs 190.9），最终 4K R10 SHA 逐字节不变；32 档 DirectML 否决（52.2ms vs SM6 60.1ms 且不 exact）；K-split 否决。全网稳态乐观下限 316.7ms（3.16fps）——常驻是必要不是充分。
- 目标改 **1080p ≥10fps**：全网空间轴减半（front 960×544，ViT 18×30=540 token；`dlss5-1080p-geometry.json`）。统一游戏 DLL `dlss5_1080p_runtime.cpp`：blocks0–70 逐段进 DLL，每帧只在游戏原生 FFX 命令列表录 dispatch。
- 进游戏的坑一串：hook 后读 header 已被改写（要快照）、FFX 尺寸字段偏移不同（以 output 描述为准）、代理 device 与 native device 不是同一实例（LUID 相同）、DirectML 不能建在 proxy device 上（共享堆 alias + `IID_UnwrappedObject` 拿 native list）、初始化没完就录帧（`g_ready` 硬门）、bridge-only 崩在 Color 状态、Steam 云同步弹窗、PowerShell 门文件 UTF-16。直接 UNORM pack 到 swapchain 跳过 tonemap 得暗绿网格，改为 block70 原位回写 FSR 输出。最终 production：blocks0–70 连续提交，**11.98fps**，两张 3 秒间隔截图伊芙眼睛一闭一睁。

### 09-05：方格、19fps、发布包、预览驱动、审计翻案

- 用户进游戏报满屏小方格：C512 predown 行跨度 240→120、block70 残差改按显示合同在 present 时叠到 R10 backbuffer。方格消失（`grid-bug-before/after.png`）。F6 三档对照。
- 30fps 优化第一轮（GPU 时间戳 13→49 点）：block70 QKV 并行 8.49→1.34ms；group FFN 只留 block70；共享窗口 attention；多头共享 attention 全网 54.2→50.8ms；HWC prefix；移位 attention 内部/边界拆分；权重本地化。全网 71.7→45.4ms，洞窟 **19.0～19.2fps**。否决：C32 batch4 FFN、FP16 特征存储、WaveSize 显式、统一 descriptor heap、attention score cache Mode1、投影全展开。
- 自包含交付：权重/CSO 内嵌 PE 资源（572 项 187MB），DLL 190MB，zip 132MB `DLSS5-AMD-1080p.zip`（19.2fps）。「用户自备 DLL 打补丁」路线取消。
- 授权后装预览驱动 26.10.07.02 → SM6.10 / LinAlg tier 0x10；`matrix_smoke` 8192 值全对；C32 FFN 用 LinAlg 0.569→0.257ms（有 114240 值差，非 exact）。私有 Agility 721 进游戏，前端四层矩阵 FFN 全网 45.57→44.51ms（仍 19fps）。
- 排"60fps 不流畅"：根因是游戏 `GameUserSettings.ini` `AntiAliasing=OFF` 令 `r.PostProcessAAQuality=0`，FSR 根本没创建；`repair_temporal_aa.ps1` 改 HIGH。**不能用"DLL 已加载/角落 60fps"判补丁生效**。尾端矩阵实验整体撤回到 c5a3a8a。
- **最终像素审计翻案**：ReShade immediate list `_has_commands=false` 直接 return，此前合成根本没提交；修好后同帧 backbuffer 逐字节验证通过，但真实输出有明显 8×8 网格（相位方差解释 95%）。此前"修好小方格""开关差异小"作废。CSR 偏移 8200→8196、主输入 bank-major 映射修正，网格仍在。**决定：停追 FPS，回头做正确性移植（native 路线）**。晚间开始 preblock 原生布局恢复（旧 block0-tensor-layout 作废，输入混合不是 row-major 7→32+DW）。

### 09-06：原生逐值链——从 RGB128×64 到 RGB512→RGB 全 exact；实机几何与 640 token

（这一天 worklog 200 多条，全是 RGB→blockN 一块一块逐值闭合。）

**前段（C32→C256）**
- 前四层：随机场 seed 逐值 exact；输入混合 16 项 → 128 宽 FFN → 32 维 attention → 半精度池化 DS。RGB→block0..3 与原 CUBIN 65536 值全 exact（先修 skip-first、softmax 求和树、Q/K 归一化偶奇配对）。
- block4 DS：矩阵在 0x50b0，四通道按 0/2/1/3 存放。
- C64：三矩阵 FFN、V/P 各 4096 连接、双头 bias 双射；完整层 80% exact 的差异定位到 **FFN→attention 的 FP8 量化边界**，修后 exact。
- C128 同法参数化；block14 DS 不能复用 FFN 布局，独立 basis 恢复。
- C256：H4 半窗口 inpview 补零、plain **重复前半窗**，H12 尾窗补零；block22 DS 物理 8192 字节只前 4096 有效。
- AMD 连续 RGB→block22 DS 全 exact（128×64 夹具）。

**C512 与 ViT**
- split ffwd 是三段串联（不是 gate/up 并联），扩展输入高 bit 是 13；plain FFWD 线程 32×8 不是 32×4；窗口合同：4×4 重复、12×8 补零。
- AMD RGB128×128→block23 首轮 25.2% exact 失败 → 差异回到 preblock 随机场 → 通用函数表（192MiB）→ seed0 过、留出 seed 露出 FFN 单点 → 输入混合 float64 累加 + `half_round_exact` → **RGB128→block23 全 exact** → 24–29 → block30 pool/head（4×4 池化在 128 夹具全零，扩到 256 夹具才有效）。
- ViT：expand 融合激活；contract 四分区 K1024 顺序（诊断 runner 的同步计数器要初始化 -1）；QKV 头部 128 字节是 float32 scale（不是尾部，之前往尾部写 1 污染了 V）；V/Q/K 坐标位；16/32-token 调用不作裁判；64-token exp 位映射 + 分母树。block31 完整 → 31–38 八层双输入 56 检查点全 exact。AMD 三线性算子、QKV→attention、完整 block31、八层 A/A/B/A/A 全过。
- RGB512→ViT38 首轮失败（DS0 5 值差）→ 单点候选"double→float32→half"全幅反而更差，撤回 → **WMMA 对齐截断规则** → AMD RGB512→ViT38 全 exact（16:27 恢复检查点）。

**解码与整网**
- block39：`+0x30` 是中间区、`+0x10` 才是输出；四 K256 分区；矩阵是 512×1024 FP8 不是两组 FP16；最终 `H(main+skip*scale)` 一次融合。
- block48：主输入是 global16 + 低两位交换，不是 cell；合流后 FP8 边界。
- block66：C32 合流保留 half 不先 FP8，与 C64+ 不同。
- block70：原生 27 位对齐 K16 矩阵积；AMD 首次全零是 double 表达式惹的，改 precise float / 整数实现。
- 40–47、49–55、56、57–61、62、63–65、67–69 逐段 AMD exact。**AMD 完整 RGB512→blocks0–70→RGB 786,432 值全 exact**（会话 21197）；随后按 5090 实机解码的真实移位重跑仍 exact（`release/native-runtime-rgb512/amd/final-validation.json`）。

**实机几何与 640 token**
- 用户把 5090 切 1080p 无边框，抓到 preblock/post HW 1152/1920、ViT 20/32=640 token、post scale 1/32；`native_runtime_shifts.h` 成为唯一移位表。旧 1920×1088、30×18 猜测作废。
- 640 token：AMD attention/QKV/线性各 exact；整块首帧 exact 但**第二帧 DEVICE_HUNG**（0x887A0006，LiveKernelEvent 141 TDR）；同列表拆 dispatch 无效，**chunk 级独立提交+fence** 才过；八层 640 链三帧 exact。
- 独立实际尺寸段：decoder39 裁剪、40–47（60×36）、48（120×72）、49–55、56–61、62–65、66（960×576）、67–69、post70 1920×1152 全部 AMD 三帧 exact；1080 有效纹理 + 1152 处理的 preblock（镜像补齐）双分支 exact。
- 同 RGB GPU 0→4、0→8：block8 三值差定位到 **Q/K 平方和融合乘加的二次舍入**，`NativeHalfSquarePair` 修。

### 09-07：valid1080 整网 exact、时序、进游戏、正确画面 2fps；闇夜战优化 4.0s→0.47s

**valid1080 整网**
- 同一有效 1080 RGB：GPU 0→14、0→22、0→30/head、ViT、decoder 逐段对齐，**AMD 实际尺寸 RGB0～70 整链 exact**（最终 6,635,520 分量 different=0），动态 A/A/B/A/A 因果验证过。冷初始化每次编译 shader 十几分钟。

**时序（history + motion）**
- 稳态帧比首帧多两张纹理（slot8 history、slot10 motion），受控实验证明 slot10 数值影响输出。
- 从五点重建数学参考出发，靠 cuda-gdb 抓原 kernel 中间寄存器逐项对齐：纹理小数 8 位 half-up、乘积权重 8 位、临界权重行列和守恒、FMA 顺序、21 位定点 UV、MUFU.RCP 尾数表、整数小数量化、位置融合乘加、TEX 正中点向上。120×72 从 319/91 差压到 0/0，valid1080 从 430/109 压到 0/0。
- 运动向量→坐标→采样→preblock 全 GPU 直连 exact。完整时序网络 post shift3 五帧 off/on/reset exact（第一次失败是漏同步一份 input shader）。post70 二次舍入（TwoSum 中点修正）修最后 1 值。
- 无收益/撤回：整轴 double fma（91159 差）、left 权重 fma、倒数 Newton 精化、运动采样整数量化。

**进游戏合同**
- 主 swapchain 1920×1080 R10 sRGB；FFX 输出资源逐帧轮换要重绑；FFX 提交返回入口；native barrier 观察；ReShade proxy device 与 native 同一 COM 身份（`IID_UnwrappedObject`）。
- codec：mode1 输入编码（sRGB + 0.75 软肩）/ 输出合成（OkLab 色相修正、TransferStrength/ColorStrength）从捕获 DXBC 复刻，全部 65536 个 half 位模式两卡各自 exact；1080p 三 scale exact。
- FP16 输出 surface 是截断不是 RNE（桥测试假设错了一次）。
- 单 list 全网 DEVICE_HUNG，禁用；改分段自有提交器（`native_game_submission.h`）。
- 首帧诊断 DLL：第一次输入全黑（主菜单），改显式 PID 请求 + 非黑门；营地场景请求 1 前后帧，**独立原版逐位一致**（`bit_different=0`），首个真实角色场景端到端 exact。
- 分块 C64 核（8 像素×32 通道共享）整网 3977→2710ms；tiled 版进游戏，连续 reset 展示 **约 4 秒/帧（2fps）**，Zero 20:09 确认"这个是对的，虽然只有 2fps，主人公有一点黑"。封存检查点 `release/checkpoints/2026-09-07-correct-game-preview/`。

**闇夜战（20:14～）**
- C512 双投影分块 −10%（2710→2439）、共享 FFWD（2174）、QKV 分块（1856；**stage2 是 QKV 不是 attention**）、共享 C32 FFN（1720）、noise 表驻留（1679）、C32 输入量化复用（1590）、LDS 行跨 33（1513/1470）→ **1.470s**。
- 提交计时证明是 GPU 计算不是等待（GPU 1391 / wall 1469）。ViT 同阶段合并 chunk DEVICE_HUNG，撤回。
- 驱动被自动回退到 26.8.1（矩阵接口没了），22:21 Zero 批准重装预览驱动，矩阵舍入探针 8192 值 exact。

### 09-08：闇 1.47s→0.47s；光接手 0.47s→0.186s exact（tag 0.01）；快速版到 62.7ms（tag 0.03）；游戏 8→15fps

**闇（00:00～05:00，Wave 矩阵路线，全部 15 帧 exact，暖轮 ms）**
- Wave 矩阵展开 0.732ms 保 exact（Thread scope 完整 K256 不 exact）→ C256 核心 13.9→3.4。
- 整网：C256 Wave 1311 → C128 1224 → C64 1187 → C32 Wave QK/QKV 1171 → ViT Wave 展开 1043 → ViT 收缩/投影 965 → 512 矩阵 QKV 862 → C32 Wave FFN 763 → C32 AV/投影 746 → 首层拆分 prefix 整数舍入修复 706（**HLSL 转换表达式上下文相关**）→ 池化 Wave 688 → C32 收尾合并访存 662 → ViT Wave 注意力 649 → 512 FFWD 八组并行 598 → 多头 Wave AV 586 → C32 四 Wave 569 →（驱动回退，恢复）八 Wave 582 → 并行 exp 575 → 并行 prob 567 → 并行 norm 555 → 多头四 Wave 553 → 并行 softmax 546 → 八 Wave 540 → **ViT 展开块加倍 472.9**。
- 无收益/否决：共享 prob、收缩预载、位元 FP8（更慢 609）、硬件 half（不 exact；探针证实 f32tof16 朝零截断，修正后 exact 但 616 更慢）、权重驻留复测、多头 norm 并行、C32 末端写回并行、共享 padding 只有 2.8～4.9%。

**光（05:15～06:12，暖轮 ms）**
- async-submit 456 → blocked-ffn 427 → blocked-vit 359 → coalesced-qkv 366 → **resident-weights 338**（权重原本全在 UPLOAD 堆每帧过 PCIe，非本地 492→205MB；decoder_stage1 4.5/13.9ms 双稳态根因）→ shift-stack 332 → wave-vit-qkv 321 → blocked-vit-proj 312 → split-blocked 303 → c32-ffn-blocked 293 → fused-exp 289 → vit-attn-half 271 → wave-project 254 → local-c32-attn 246。
- 06:45～08:35：wave-decoder 237.7 → split-project 225.0 → direct-attn2 203.5 → c32-split 184.6 → fused-shift 183.8 → c32-rawstore 183.2。至此除注意力核和 C32 FFN 外所有 GEMM 都在 wave 矩阵路径上。
- 共同规律：收益全来自数据搬运（寄存器分块复用 A tile、FP8 格点中间层用 f16 存、权重驻留、删 LDS 转存和 barrier、取消小 dispatch），每个元素的 K32 乘加序列与 H() 次数不变，exact 是构造性的。
- 三个中性探针（位元 F、static-length、matrix-store）保留 flag 未采纳；C32 FFN 每 token 5ns 的成本没解释，需要指令级 profiler。
- 显存：块内 scratch 共享 14.68→7.26GB（游戏 7.2GB 贴着 16GB）。08:32 部署到游戏，Zero 09:00 确认画面连贯 **约 5fps**（每帧 reset）。**tag 0.01 = exact 终点 186ms**。

**快速版（09:04 起）**
- Zero 拍板（`fast-path-plan.md`）：exact 链冻结当裁判，允许硬件算术差异。阶段 1 FP32 硬件累加 186→159.8（PSNR 42.6，游戏 6fps）→ fp8-ffn 154.8 → fp8-vit 151.6 → fast-epilogue 130.1 → fast-attention 115.3 → fp8-proj/qkv **111.9ms**（PSNR 42.0，≥4/255 像素 5.2%）。坑：分裂块投影权重漏打 FP8 包 → PSNR 8.8 的垃圾；DLL 只放行 `DLSS5_TEST_` 前缀 → 网络退化成恒等、"效果很小"。
- 17:32 **时序接通**（ffxDispatch +120 运动向量 1284×724 RG16F UV 单位，+408 reset；history = 我们上一帧输出；坐标/采样类用指针相等判设备在 ReShade 下失败，改 `NativeSameDevice`），F6 差异明显。17:41 快速版+时序约 8fps，**tag 0.02**。17:55 运动向量符号/单位数值验证（管线 warp 0.0541 vs 不 warp 0.0866 vs 反号 0.0904）。
- 18:09 时序去 double：history 帧多耗 5.2→0.5ms（**计划里的 30ms 是高估**）。
- 18:15 一口气：QKV+normalize 融合 104.5、expand+contract 融合 0、FP8 激活/硬件量化/FP8 QKV 99.5、prefix 噪声表改 ALU 生成 97.6（随机读 200MB 表是真凶，不是整数 HMMA 仿真）、C32 注意力四段融两段 95.1、C32 FFN 整块 93.9、隐藏层硬件 Cast 90.0（探针：4.9ms 里软件量化占 1.9、矩阵乘只 0.3）、软件 H/F 换硬件 **87.9ms**（fast2）。**定量教训：这台卡每个核的成本几乎全在逐元素标量尾巴，不在带宽也不在矩阵乘**（C64 块 32 TFLOPS，C256 块 119 TFLOPS）。
- 19:06 Zero 确认 87.9 版更快、效果在，但**雨景闪烁**，先搁置。
- 19:07～20:58：C32 Q/K/V 硬件 Cast 84.3 → ViT 注意力快速版 82.4 → ViT contract split-K 80.6（fast3）→ C512 注意力硬件 H+位元 F 79.1 → C512 ffwd 4 wave 74.9 → ViT packed 输入+BLOCK_M=4 71.3（fast4，Zero 确认 12fps）→ C32 mapped input 70.8（fast5，12～13fps；pre 块根签名只给 5 个常量、读第 6 个读到垃圾 28dB）→ 多头残差流全 FP8 67.5（fast6）→ **C512 direct 注意力 62.7ms（fast7，tag 0.03）**，游戏约 15fps。
- 21:40 C32 注意力占用/组同步两假设排除（一窗一 wave 零组同步版反而 4.7ms）。22:16 闪烁排查四假设（时序反馈、回写竞争、网络不确定性、跳帧）全排除，挂起。
- 22:45～23:55 小刀（全逐位）：直接 Cast −0.3、矩阵残差 −0.3、平铺权重（多头 −0.44、C256 −0.2；C32 无收益）、C32 FFN fast3 −1.15/−0.33/−0.17 → 约 −2.7（fast8）；C32 注意力 fast2 −11%、post70 direct −3.3、C32 链读 raw −1.0 → sum-of-mins 55.7→50.6（fast10）；wave ds4 2.79→0.06（encoder 里藏的 cs_5_1 标量下采样，fast11）→ 47.8；C32 QKV fast2、多头注意力 fast2 −0.8（fast12）→ 45.1；ViT 注意力 FP8 每块 0.36→0.18（fast13，约 00:05）→ 42.6。测量法改为交替 3 轮按帧取最小（`abn.sh`+`cmpmin.py`）。

### 09-09：33ms、29fps；tag 0.04/0.05/0.06；显存之争；闪烁收敛；仓库重整

- 00:20～07:50：游戏探针（掉帧 21→13→8 是 **Splashtop 抓屏**，cpu_frame 阶梯涨 GPU 不变）；延迟提交进游戏（环 8→64）GPU 38.5→33.2（fast16）；C512 pack/crop 向量化 −0.55；post70 merge 折进 FFN −0.8；**C32 注意力 fast3（QKV+归一化合进注意力）−8ms**；多头 QKV+注意力合核更慢（每 head 重读输入）关；prefix 合进 FFN 1.79→0（PSNR 42.16→41.98 接受，fast17）。测试台稳定段约 33ms。
- 08:35 **tag 0.04**：命令列合批 ~100→~25 列/帧，CPU 从 GPU+4.5→+2.8（fast18），GPU 32.0、23～25fps。
- 08:55～09:50：C32 注意力 fast4（一组两窗，−1.5，逐位）、合批二档、ViT QKV 合核 −0.74；闪烁坐实为**网络本身的空间扩散**（输入变化点 3px 内一圈 1～4/255 光晕，FP8 放大假设排除），`native_output_smooth.hlsl` 时间平滑（fast22 6,0.6 → fast23 10,0.8），Zero："已经不太闪烁了"。**tag 0.05**：24～25fps，测试台 33.9。
- 11:00 仓库重整（根目录 src/ shaders/ scripts/ tools/，其余进 Development/；`bench.ps1` 拍平 76 层 runner，PSNR 41.913911 一位不差）。fast24 关探针（每帧一次 Flush 等 GPU 即那 3ms）→ **27～28fps**；fast25 preblock main8 −0.55。12:25 结论 **GPU 已饱和**：网络 30.7 + 游戏 6～7 ≈ 37ms→27fps，跨帧流水造不出 GPU 时间；C512 FFN 9 GFLOP@0.076ms≈120 TFLOPS 近算力上限，"每 dispatch 40µs"判断作废。**tag 0.06**。
- 17:00 跳块扫描（45 块单跳 PSNR 表）：{42,43,46} 40.66dB −1.0ms 进游戏（fast26）；九块 36.5dB −2ms 待看。蒸馏定为写作线。
- 17:55～23:25 **显存之争**：fast26 玩一会儿突然 21fps 不回升，GPU 96% 全在游戏；被挤到系统内存的量↔帧率单调（399MB→20fps、523→16、737→12）。游戏 13.3GB + 网络 4.8 > 16。三层：`DLSS5_RESERVE_VRAM_MB`（无效）、用户 `Engine.ini r.Streaming.PoolSize=6000`（有效：掉几秒能回来）、驻留优先级 maximum（Windows 不按它挤）；网络显存 6839→5073（fast27）→ 4.84 → **3.75GB（fast33）**；每 60 帧 MakeResident（fast32）。fast31 **29fps**，换区掉到 17 五到十秒回。
- 21:30 post70 rgb 头 0.77ms 是误判——**紧跟大核后面的小段时间戳区间不可信**。23:55 隔离量法（`DLSS5_TEST_ISOLATE`）出单核成本表：合计 29.3ms，帧内 sum-of-mins 33 → 约 3.7ms 是 dispatch 空转；旧 <0.2ms 分段数字全偏大。

### 09-10：fast34～38（34.0ms）；0.07 发布包；浪人崛起 XeSS 出图

- 00:30 fast34：块 4 finish 改 E4M3 raster、块 66 读 E4M3 残差 −0.9；块 69 main8 −0.23。坑：投影核 `t + skip*scale` 被合成 FMA 差一个 f16 ulp，要 `precise`。
- 01:45 fast35：算字节数发现 **C32 家族是带宽瓶颈**（每块 280MB 光带宽 0.47ms，块本身 0.53）；raw/ffn 1790 万值 100% 在 f16 格点 → f16 存储逐位无损，C32 家族 10.61→8.70（−1.9），显存再省 500MB。
- 03:25 fast36：post FFN 里 merge fold 的运行时整数除法改每 token 广播 1.69→1.2；finish（tile→raster + 2×2 池化）和 rgb 头并进注意力收尾 −1.5ms。PSNR 41.96。**0.07 发布包 = fast36**（`package-release.py`，权重转 f16，zip 约 220MB，网友包布局 `DLSS5-AMD\`）。
- 09:05 fast37：C32 家族 9 个 stage 的 FFN 并进注意力 dispatch（每 wave 16 token 正好一组，输出累加器直接 Cast 成 QKV 的 A 并留寄存器当残差）：隔离 6.95→5.75（−1.2），整帧 37.3→35.4（−1.9）。**FMA 收缩上下文相关**，参考链两边都 `precise`。
- 10:40 多头 FFN+proj0 合核：逐位相同但整帧零收益——**合核只对带宽型段有效，多头段是算力型**；代码留默认关。
- 12:10 fast38：**2 的幂行步长 = 内存通道撞车**（ViT contract 0.152 vs expand 0.078 同 FLOP；行步长 4096 让 16 行 tile 全落一条通道）。权重重排 512B/1KB 连续 tile、激活按 tile 写读：ViT 每层 0.486→0.288（−1.6）、C512 每块 0.249→0.196（−0.85）、decoder entry −0.01（proj62 用 tile 反而 +0.045，不动）。整帧 fast37→fast38 min-of-means 37.0→34.0、sum-of-mins 36.7→33.6。
- 12:10 **浪人崛起（XeSS 路径）出图**：无 FFX dll，钩 `xessD3D12Execute`；1080p 要 XeSS 超高质量（1.5×，输入 1280×720）；速度贴图 R16G16_TYPELESS 且 `SetVelocityScale(-w,-h)` 运动向量符号相反；输出 R16G16B16A16_TYPELESS 当 UNORM16；LDR 曝光 1；渲染线程调 XeSS、另一线程 ExecuteCommandLists（跨线程提交）。七次 device removed 真凶是自己 codec 建三个输入 SRV 时格式变量被输出描述覆盖成 UNKNOWN。

- 15:20 fast39：C512 FFWD 两刀，逐位相同。(a) FFWD 三个 f16 权重矩阵 1KB tile（`DLSS5_SPLIT_FFWD_TILED`）：单独量零收益；(b) FFWD 输出本来就是 F(H()) 在 E4M3 格点上，直接存 E4M3 的 512B `[token 16][k 32]` tile，投影 0 直读 A（build 宏 `DLSS5_BUILD_SPLIT_FFWD8`），不再逐元素重量化进 LDS：第一版按 `[token][512]` 存反而慢 0.08（16 行 × 32B 散在 512B 步长上），改 tile 后 16 块每块 −0.06，整帧 sum-of-mins −0.8（37.49→36.71）。今天机器整体比昨天慢约 4ms（每帧 31～55 晃，隔离法同目录两次差 50%），只信 60 帧取每段最小的同批比较。

- 17:10 fast40：C512「mapped + FP8 stream」（`DLSS5_SPLIT_STREAM8`），把多头段 09-08 的做法搬到 C512：FFWD 按 raster 索引直接读块输入（边界补零）、投影 1 直接写裁剪后的 raster（窗口 pack/crop 两个 dispatch 没了）；投影 0 的输出 F(H()) 本来就在 E4M3 格点，存成 E4M3 tile 兼作 QKV 的 A（QKV pack dispatch 没了）；块与块之间 raster 走 E4M3 字节（链首输入 / 链尾输出留 f32，跳块拷贝进出同大小）。每块 8→5 个 dispatch，逐位相同；每块 −0.02，16 块 sum-of-mins −0.32（36.84→36.52）；**显存 3510→3129MB（−380MB）**，这一项对换区掉帧更有用。

- 16:30 fast41：post70 merge fold（fused C32 FFN 序言里的 mode 9）每 lane 一次 4 字节 Load 出 4 通道、系数 Load4，16 趟改 4 趟（build 宏 `DLSS5_BUILD_C32_MERGE4`，逐位相同）：post −0.09，整帧 −0.1。只换 cso，DLL/flag 沿用 fast40。

- 17:40 fast42：解码器入口 / 三段 2× 升采样投影的收尾（`DLSS5_BUILD_DECODER_FAST`，逐位相同）：16×16 tile 先进 LDS，按 token→dx→4 通道 quad 的顺序 float4 连续写、残差/scale Load4、F() 换位元版（原来每个输出元素两次 log2/exp2 版 F + 逐元素散写）：proj62 −0.06、proj66 −0.03，整帧约 −0.1。试过的两个数值改动都不值：硬件 H 再 −0.08 但改位（PSNR 41.96 不变）；"残差已在 E4M3 格点"不成立（56/62 的 skip 是 f16 值）。三段现在是带宽型（proj66 读写 140MB f32 ≈ 0.21ms），再降要输出 f16 并改 C32/多头块首块的读法（估 −0.2）。PLAN 里"升采样投影是标量核"是旧记录，早就是 wave 核了。
- 17:45 全景（decfast，同批，机器慢态）：整帧 sum-of-mins 36.3。多头 33 块 14.6（C256 编码 3.1 / 解码 2.8，C128 2.6 / 2.2，C64 2.0 / 1.9，每块约 0.44）；C32 家族 9.4（pre 2.84、post 2.90 最大的两个单段，1–3 1.9，67–69 1.8）；C512+ViT 约 5；其余小段。布局类的刀已基本收完，剩下的都在核内部的标量尾巴和延迟，需要指令级 profiler（RGP）才看得见钱在哪。

- 18:00 **指令级视角打通**（工具见 `tools/README.md`）：RDTS 免安装；RGP 无界面抓取要 (a) 测试台有 swapchain（`DLSS5_TEST_PRESENT=1`，composition swapchain，结束前多停 30 秒传 trace）、(b) 跑在交互桌面会话（`schtasks /it`），SSH 会话建不了 swapchain；dispatch 模式抓不到（没有帧边界不结束）。`rga -s dx12` 不认 SM6.10 DXIL，但 .rgp 里带每个 pipeline 的完整 PAL ELF，`rga -s bin` 能反汇编——`isa-stats.py` 一条龙。cso 的 DXBC hash 全是占位符（预览 dxc 不签名），核只能靠代码常量认。
  - **发现 1：时钟**。RGP 抓取时驱动把时钟锁峰值，同一测试台整帧 27.4ms（平时 36）——下午"机器慢 4ms"就是时钟状态，游戏里网络+游戏 GPU 满载时时钟跑到多少未知，值得查（AMD 软件的性能面板 / 功耗上限）。
  - **发现 2：C32 合核溢出**。C32 注意力+FFN 合核（除 pre 外 8 个 C32 段都用）：静态 2.6 万条指令、140KB 代码、VGPR 128 且 **scratch 896 字节（寄存器溢出到显存，112 条 scratch 指令散在 5785～19739 行）**、2261 个 s_wait、712 个转换指令；活跃寄存器只在 softmax exp 位映射那 49 条指令冲到 128，其余 <96。不带 FFN 的 C32 注意力（pre/post 尾巴）也溢出 512。多头/C512/ViT 各核都不溢出。下一刀：把 C32 核的峰值压到 96 以下（exp 段的逐元素 global_load 表读、fast4 两窗同时持有的累加器）让溢出消失。

- 19:10 **停电重启后测试台 24.4ms（中位，min 23.8）**，同一条 fast42 链下午是 36、昨天 fast38 是 34。驱动仍是预览版。也就是说这台机 GPU 之前几天一直处在降频态（RGP 锁峰值时的 27.4 也是这个方向的旁证），此前记录的整帧绝对值全部偏大约 9ms，只有同批相对比较可信。**纪律：每次量之前先跑一遍基线，不在 24 档（>30）就先查时钟再量。** 游戏内是否同样受影响待 Zero 看。

- 20:10 C32 softmax 偏置改矩阵 Load（`NATIVE_C32_BIAS_TILE`）：逐位相同但**零收益**——新 profile 里 p87 的 ISA 逐条一样（累加器布局的 Load 在硬件上就是每 lane 8 个散读，和逐元素 W_BIAS 一回事），scratch 896 仍在。溢出要消得动结构（偏置表进 LDS：+16KB 占用率减半，未试）。代码留着默认关。
- 20:00 黑帧探针（`DLSS5_BLACK_PROBE=1`，DLL fast42p）：每帧统计网络输出（非有限值数 / |v|>1.5 数 / 最大 / 均值），异步读回不 Flush，输出两帧环形副本，异常时自动 dump 到 `logs\black-<帧>-*`。1.8 万帧没触发；顺带学到：网络输出均值 0.33、最大 1.0，**是完整画面不是残差**，黑帧的触发条件该是"均值骤降"（待改）。Zero 决定这游戏抓不到就先放下，换游戏再说。
- 19:30 显存之争实锤：掉到 25fps 时专用显存 14.7GB 顶满、693MB 被挤到系统内存；贴图"非常高"→"高"立刻回 36。包的说明改成硬要求。

- 21:30 **一键编译收口**（Zero 在 22.04 的 WSL 上亲手编，暴露了一串）：`build-addon-oneclick.sh` 自动拉 MinHook / ReShade 头文件；老 mingw（gcc 10 win32 线程模型）没有 std::mutex → 自动切 posix 变体；老 `d3d12.h` 没有 `ID3D12SDKConfiguration` → 从 mingw-w64 v11 拉头文件；`d3d12sdklayers.h` 可选。shader 侧新增 `compile-shaders.ps1`（任意 Windows 机 + 预览版 dxc，不需要显卡/权重）；发现仓库 `shaders/` 缺 10 个 bench.ps1 要编的源（在 Development/，已搬回）和 3 个从没进 runner 的手工 cso（head 512 的 pool/project、C256 f16 pack，已补进 bench.ps1）。从零编出的 179 个 cso 跑测试台与现链逐位相同。
- 23:40 fast43：升采样投影 56/62/66 的输出光栅改 f16（`DLSS5_DECODER_OUT16`，kernel `NATIVE_DECODER_OUT16`，值本来就是 H()/F() 在 f16 格点上，f32tof16 截断也精确），下游首块读 f16：块 66 的 C32 合核加 map mode 10（mode 1 的布局、半精度值）；块 56/62 的多头首块加 `native_matrix_pack_fp8_mapped_src16_c{128,64}` 和 `native_wave_project_mapfeature_fp8act_f16in_f8out_c{128,64}`（`FuseShift` 的 `f16_input`）。15 帧对 fast42 逐位相同。机器整晚在 33ms 降频态（Zero 从 A 卡机 ssh 过来，不能重启），两轮 abn 只看方向：tail66_project −0.07～−0.11、tail66_body −0.06、tail62_body −0.04、tail62_project −0.03，合计约 −0.15～−0.2，符合估计；干净数等重启后再量。已装进游戏：`native-game-fast43.addon64`（sha 7da2b88c…）+ `native-game-flags-fast43.txt`（= fast42-blackprobe + OUT16），cso 来自新链目录 `decout16`（`decfast` 暂留作 A/B 基线，量完删）。测试 exe 的 `DLSS5_TEST_DUMP_BLOCK4=2` 转储 proj66 输出时现在是 f16 字节，读它的脚本没改。
- 09-11 00:10 **ViT expand+contract 合核：null**（`DLSS5_VIT_FUSED_FFN`，`native_wave_vit_ffn_fused.hlsl`，保留关 0）。一组 16 token / 16 wave，hidden 按 1024 通道四份进 LDS（E4M3，行距 1040B 防 bank 冲突），expand 四份→contract 四个 K 分区，MMA 顺序、激活、分区求和顺序照抄，逐位相同。但每块 0.35～0.39ms，原来 expand m4 0.08 + split-K contract 0.10：慢一倍。32 token 一组、hidden 八份（权重流量减半，分区累加器跨两份持续）数字纹丝不动 → 不是带宽，是延迟：20～40 组×16 wave 摊到 256 个 SIMD 每个不到两条 wave，每个 K 步都是依赖链 MMA，遮不住；原来 expand 每 wave 16 个独立累加器、contract 四倍分区并发。教训：640 token 的层没有足够并行度撑 LDS 合核，"省 13MB 中间量 + 两个 dispatch 缝"的账抵不过并行度损失。待办第 6 条划掉。
- 09-11 00:10 **网友 1114**：网友在别的游戏里装 0.08 报 ReShade 加载 addon 错误 1114（豆包猜"DllMain 里做 DX 查询"，不对）。真因是 `DllMain` 里写死的进程白名单——不是 `SB-Win64-Shipping.exe` / `Ronin.exe` 就 `return FALSE`，ReShade 就报 1114。已去掉（可选 `DLSS5_ONLY_EXE=<exe 子串>` 环境变量恢复过滤）；别的游戏现在能加载，但只钩 FSR `ffxDispatch` / XeSS `xessD3D12Execute`、只做过 1080p，其他游戏能不能出图没验证。进下一个 tag。Zero 的方向：做成 Magpie 那种窗口捕获 + 光流 MV 的形态（社区 DLSS5-Feeder 同型），等优化线收口后立项。
- 09-11 00:25 **Magpie 路线开工**（Zero 装了 SAOG0721/Magpie experimental fork，`D:\Magpie-DLSS5\...\Magpie-Experimental-x64\`）：它的 FSR3/FSR4 效果走 `amd_fidelityfx_loader_dx12.dll` 的 `ffxDispatch`（D3D11 捕获→共享贴图→D3D12），和《剑星》同一接口，所以不改 Magpie，把 ReShade（`dxgi.dll`）+ 我们的 addon（`dlss5-amd.addon64`）+ `DLSS5-D3D12-721\` 放到 Magpie.exe 旁边即可，flags/权重仍读 D:\DLSSNR-Lab。三个坎已过两个：① DllMain 白名单（已去）；② addon 等的 dll 名是 `amd_fidelityfx_dx12.dll`，Magpie 用 FSR4 SDK 的 loader 两件套，且 Magpie 启动就加载 `libxess.dll` 会被误选成 XeSS 钩子 → 加 loader 名、目录里有 FFX dll 文件时只等 FFX（`DLSS5_UPSCALER=ffx|xess` 可覆盖）。现在每帧拦到 ffxDispatch（1920×1080→1920×1080，jitter 0，mvscale 1）。③ 未过：**Magpie 的输入/输出贴图是 R8G8B8A8_UNORM**（dxgi=28），codec/frame/readback 全按 RGBA16 float 写死（Ronin 时加过 UNORM16 分支）→ `initialization_failed: codec unverified input format/geometry`。下一步：8 位 UNORM 进/出分支（解码写 RGBA8 缓冲再拷回，同 Ronin 路）。另：Magpie 的 FSR3 效果日志 `opticalFlow=false`，运动向量全零，要在效果参数里开光流。Magpie 用的效果组：新建组只加 FSR3→FSR3_SR。
- 09-11 01:10 Magpie 路线续：③ 8 位 UNORM 进/出分支做完（`NativeIsRgba8Unorm`/`NativeIsGameColor`，codec 的 `unorm8_out` 写 RGBA8 原始缓冲 + `BufferFootprint()` 拷回，decode shader `NATIVE_CODEC_UNORM8_OUT`/`NATIVE_CODEC_BGRA`；诊断 `DLSS5_DEBUG_TINT=1` 把绿通道压 1/4 让写回肉眼可见）。00:31 那次（Zero 在机器前，游戏全屏）网络在 Magpie 里跑了 591 帧、49～63ms/帧，转储的输出确实是 DLSS5 画面，但屏幕上看不出变化；之后加了 tint 想验证写回是否上屏，却再没跑成：Magpie 窗口化时捕获 1918×1079→2784×1566（网络只认 1080p，改无边框 + 缩放"原始尺寸"）；Magpie 的重复帧检测（`%LOCALAPPDATA%\Magpie\config\v4\config.json` 的 `duplicateFrameDetectionMode` 1→0，已改，有 .bak）让远程会话下 FSR 一帧不派发；F6 是全局键，游戏和 Magpie 里的 addon 一起切；网络要第 120 帧抓快照 + 20 秒建网络才接管，Zero 一切出来 Magpie 就停。最后两次（远程会话，Splashtop）都在 ready 后处理第一帧时 Magpie 崩（`Magpie.exe.<pid>.dmp`，msvcrt.dll 访问违例）。**待办**：Zero 在机器前（不走远程）再试；崩则加崩溃现场记录；不崩看紫不紫；紫了再去掉 tint、开 Magpie 的光流（`opticalFlow=false` 现在 MV 全零）。每帧写 `native-submission-order.txt` 的观察日志（已 99MB）在 Magpie 里也在拖速度，收口时要关。
- 09-11 01:20 **Magpie 路线打通**。崩溃真因：`NativeReadSubmittedFrame`（第一帧 before/after 转储）按 RGBA16 的 16.5MB 从回读缓冲拷贝，8 位贴图的缓冲只有 8.3MB（00:31 那次越界没踩到页）→ 按格式取 bpp；`CheckNativeFrameInput` 接受 4 字节/像素。之后 tint 版画面发紫（写回上屏确认），去掉 tint 后 Zero 看到真效果："跟游戏钩子那版风格不一样，更白，比之前效果好"——输入是 8 位 sRGB 成品图而不是线性 HDR 场景色，网络当场景色再提亮，和 5090 上 Magpie DLSS5 同味。每帧 33～36ms，约 30fps。**当前 Magpie 布置**：`D:\Magpie-DLSS5\...\Magpie-Experimental-x64\` 里 `dxgi.dll`（ReShade 6.8）+ `dlss5-amd.addon64`（本次构建：白名单去掉、FFX loader 名、8 位分支、回读修复）+ `DLSS5-D3D12-721\`；Magpie 配置 `duplicateFrameDetectionMode=0`；效果组只放 FSR3→FSR3_SR，缩放"原始尺寸"，游戏无边框 1920×1080；flags 共用 D:\DLSSNR-Lab（`DLSS5_DEBUG_TINT=0` 留着）。**收尾待办**：接管从第 120 帧提前到第 1 帧（`request=n==120` 那句）；关掉每帧观察日志（`native-submission-order.txt` 已 100MB，拖速度）；Magpie FSR3 效果参数开光流（现在 MV 全零）；给 Magpie 做独立的 `DLSS5-AMD\` 包（网友用：Magpie fork + 我们三件 + 包目录，不再依赖 D:\DLSSNR-Lab）。
- 09-11 02:00 **Magpie 版包做完并远程验证**（Zero 睡了，"token 耗光了算"）。三刀：① 运动向量单位——《剑星》FSR 给 UV 单位（mvscale=渲染尺寸 1281×721），时序喂入写死 ×1920/×1080；Magpie 给像素单位（mvscale=1,1）被放大 1920 倍，历史全错（色块闪烁的主因）。改为 feed 尺度 = dispatch 的 motionVectorScale × 1920/render_w（`NativeMotionVectorScale()` 由钩子记下；《剑星》算出来仍是 1920/1080，逐位不变；XeSS 无声明照旧）。② 接管帧可配：`DLSS5_SNAPSHOT_FRAME` 直接从 flags 文件读（flag 文件应用到环境是初始化之后的事），默认 120，Magpie 包 1。③ 等上采样 dll 的 10 分钟上限去掉——Magpie 启动 16 分钟后才开缩放，worker 早放弃了（`hook_status` 没出现就是这个）。远程验证手段（Zero 不在）：`schtasks /it` 起 Magpie 和游戏；Magpie 没有命令行切换，热键走 WH_KEYBOARD_LL + RegisterHotKey，keybd_event 合成的 Win+Shift+A 没触发，改为 `PostMessage(FindWindow("Magpie_Hotkey"),WM_HOTKEY,0(Scale),0)`（`logs\magpie-toggle.ps1`），游戏窗口先用 ALT 假键 + SetForegroundWindow 拉到前台；截屏用 `capture_amd_desktop.ps1` 任务。结果：包目录模式（`DLSS5-AMD\` 在 Magpie.exe 旁，flags 读包里的）第 1 帧抓快照、ready 后每帧跑，`neural_coverage armed=ran`，56ms/帧（远程会话 + 游戏自身 60fps + AMD 光流同机，比 Zero 在场时的 34ms 慢）。**包**：`DLSS5-AMD-0.09-magpie.zip` 242MB（`release/` 和 D:\DLSSNR-Lab），内容 dxgi.dll/dlss5-amd.addon64/DLSS5-D3D12-721/DLSS5-AMD/README/SHA256SUMS；`scripts/package-README-magpie.txt`、`scripts/magpie-flags.txt`。注意力权重不是精确 half（attention.f32 保留 f32），包 585MB 解压。**未做**：游戏 DLL 没换（fast43 仍在游戏里；新代码对《剑星》理论逐位相同，等 Zero 验）；tag 0.09 等 Zero 笔记本编过再打；README 的 0.09 行已写。
- 09-11 06:50 **黑块闪烁稳定复现并找到触发条件**（Zero 早上报告：Magpie 下角色立绘左侧手臂一直闪方块）。连拍截图：手臂上一块粉色矩形、8 像素阶梯边，每帧位置变。开 `DLSS5_DEBUG_DUMPS` 拿第 300 帧转储：输入色干净；**history（上一帧网络输出）在手臂上是纯黑的 8 像素阶梯方块**（屏幕上的粉色 = 黑块经输出平滑）；motion 是垃圾——Magpie AMD 光流在平坦暗部给出 ±5 万像素的向量（43% 像素非零，8×8 块状），warp 后的历史满屏黑洞。关掉时序路径（去掉 temporal-history.txt）方块消失；时序开着 + `DLSS5_MOTION_MAX_PX=64`（feed 里超过 64 像素当静止，feed cbuffer 多一个常量）方块也消失，六张全干净。结论：**黑块 = 时序分支吃到带黑洞的 warp 历史，把洞扩散成 8 像素块的黑块，再经历史自我延续**；游戏里的闪烁很可能是同一机制的另一触发（遮挡/边缘），下一步在游戏里试同一夹紧。顺手：Magpie 的 `duplicateFrameDetectionMode` 枚举 0=Always/1=Dynamic/2=Never，昨晚改成 0 是改反了（静态场景一帧不派发），现在 2；一次启动只能激活一次 → one-shot 加 `ResetForNewSession`（队列变了或上次失败就丢掉 frame 回到 idle，快照重新武装，最多 8 次），远程 start/stop/start 验证第二次会话有帧处理（静态主菜单下 WGC 不出新帧，只能看到 30 帧）。包已重打（`DLSS5-AMD-0.09-magpie.zip`，flags 含 `DLSS5_MOTION_MAX_PX=64`、`DLSS5_SNAPSHOT_FRAME=1`）。**光流垃圾的根因在 Magpie 侧**（fork 的 densify 不用置信度），夹紧只是止血。
- 09-11 07:25 **黑块续**：夹紧 64 像素后第 300 帧干净、第 6000 帧又出现（它是慢慢长出来的自持状态）。五帧连续转储（`DLSS5_FLICKER_DUMP=2000`）：motion 干净、warp 干净、history 每帧都带黑块，黑块探针显示输出全有限、无 NaN——网络"正经地"把那个 tile 算成黑。试了三招：① 输出守卫（输出接近黑而输入不黑 → 用输入替换）：块变粉——网络输出空间比输入暗一档、解码再提亮，输入值塞进输出槽颜色不对，撤了；② 历史守卫 `DLSS5_HISTORY_GUARD=3,12`（warp 历史接近黑而当前输入不黑 → 用输入替换，即 preblock_input_mix 无历史时的默认填法，按像素局部退回无历史）：单独用块仍在；③ **codec sRGB 直通 `DLSS5_CODEC_SRGB=1`**：原 encode 是线性 HDR/paperwhite → shoulder → sRGB 曲线（NVIDIA 捕获的 mode1 约定），Magpie 的输入已经是 sRGB 显示图，再套一遍就是双重 gamma（"更白"的真因）；直通后画面亮度正常，三张里两张干净、一张剩小块。②+③ 叠加没等到结果（Zero 叫停："不好搞定就先打包"）。包 0.09-magpie 最终 flags：SNAPSHOT_FRAME=1、MOTION_MAX_PX=64、CODEC_SRGB=1、HISTORY_GUARD=3,12；zip sha256 4809aa04…。**遗留**：暗部皮肤 8 像素块偶发，根因在网络时序分支对这类输入的响应，未解；远程验证套路都在 `logs\setflags*.ps1` + 四个计划任务。
- 09-11 07:50 **发布**：整包 `Magpie-DLSS5-AMD-0.09.zip`（341MB，sha256 98E760F2…，D:\DLSSNR-Lab）= Magpie 实验分支本体（去掉 nvngx/NVCV/npp 等 NVIDIA 运行库）+ 我们的四样 + `config\config.json` 便携模式预设（效果组 DLSS5-AMD、原始尺寸、重复帧检测 Never）+ README；打包脚本 `logs\bundle.ps1`/`bundle2.ps1`。Zero 已发公众号 + 网盘。30fps 是 Zero 锁的游戏帧率，Magpie 每帧跟着游戏出图，网络本身不到 30ms。
- **下一步（压缩上下文前的清单，09-11 07:50）**：
  1. tag 0.09：Zero 笔记本 pull 编译验收 → `release-check.sh` → 打 tag（README 0.09 行已写）。游戏 DLL 换成当前 src（含 mvscale/白名单/8 位/守卫，对《剑星》理论逐位相同）并验一次。
  2. 黑块根因：网络时序分支在 sRGB 输入上把暗部皮肤 tile 算成黑；测量手段齐全（FLICKER_DUMP、黑帧探针、setflags*.ps1 + 四个计划任务远程复现）。候选：对比同一帧在"有历史/无历史"下的中间层，找第一个变黑的块；试把 Magpie 的置信度/光流质量提上去；试 jitter。
  3. Magpie 形态提速：接管 20 秒（权重预读、f16 权重、PSO 缓存）；网络 24ms → 目标 ≥40fps 需要 C32 溢出那刀；每帧观察日志 8192 条上限后无开销，但初始化期间的 fprintf 可关。
  4. 网络本身（原清单）：C32 合核消溢出（结构改法）、C512 尾巴 −0.1～0.2、九块跳过版 −2ms（画质 Zero 定）、游戏 6～7ms 与网络重叠。
  5. Magpie 其他分辨率（现在只认 1080p 进出）：网络几何写死 1920×1152，改成参数化是大工程，先不动。
- 09-11 08:55 **tag 0.09**（Zero 带走笔记本，改为在 9070 机上验：`release-check.sh` shader 179 个全部与游戏资产逐位一致；flags 差 `DLSS5_DECODER_OUT16=1`（fast43 的发布开关，补进 `scripts/game-flags.txt`）和 `DLSS5_DEBUG_TINT=0`（调试开关，加进忽略列表）；游戏里装的 add-on 仍是旧版，游戏开着不能换）。两个仓推完。
- 09-11 09:50 **黑块根因找到并修掉：硬件 E4M3 转换不饱和，超出 ±448 出 NaN**。路线：把 Magpie 转储帧（第 2002 帧，黑块在左臂）喂给 bench 可执行文件离线重放（`logs\replay.ps1`：拷一份 decout16 目录，input.f32 = 转储的 encode 色 f16→f32，history/motion 用转储，flags 读 `magpie-flags.txt`，偶数帧无历史、奇数帧带历史）。①带历史的重放和游戏里下一帧的 history 转储最大差 0.008——重放忠实；②**无历史的重放在同一块也出精确 0**（8640 像素，全在手臂），所以不是时序分支的锅，时序只是把它延续；③ 输出公式 `clamp(color + 8·acc·scale, 0, 1)`，精确 0 = clamp 洗过的东西——用 `DLSS5_TEST_EPILOGUE_MODE=8` 直接看头部累加器 acc：**25920 个 NaN**，和零像素一一对应（黑帧探针"全有限"就是被 clamp 洗掉的；HLSL `clamp(NaN)` 出 0）；sRGB 直通的 1503 帧无历史 44352 个 NaN，更多。④ 上游：project66（f16 光栅，max 449.5）、pre-down、block4-down、encoder 段 sharedraw（f16 max 442）全部有限 → NaN 生于最后一段 C32（块 66～69）内部。⑤ 精确链每次进矩阵前都走 `F()` = `min(…,448)` 饱和；快路径三处用硬件 `Cast<F8_E4M3FN>` 直接转无界值：融合 FFN 的输入（残差流 f16）、FFN 隐层、注意力 AV 输出（P 舍入后略大于 1 → 可超 448）。逐个加 `clamp(±448)` 重编 `native_wave_c32_fused_attention_ffn.cso`：FFN 输入那处把 NaN 从 25920 压到 2214、其余像素逐位不变；AV 那处压到 0。参考 fixture 上带饱和的 cso 输出与原版逐位相同（fixture 里没有超过 448 的值）。落地：`NATIVE_C32_SAT_CAST` 宏（`DLSS5_BUILD_C32_SAT_CAST=1`，bench 头部默认开）、`Development/run_c32_sat_cast_network.ps1`。**为什么 NVIDIA 没这个块**：它的 FP8 转换是饱和的（等价我们的 F()），同样的输入残差被夹到 448 而不是变 NaN；我们的 Magpie 输入（8 位 sRGB 成品图、大片平坦亮肤）把尾段残差推过 448 才暴露。之前三个"止血"（MV 夹紧、sRGB 直通、历史守卫）都只是减少触发概率。**遗留审计**：仓库里硬件 E4M3 Cast 共 43 处（C32 其他变体、C64/C128/C256 attention、ViT、project、split ffwd），这次只修了实际用到的融合 C32 那条；其他段在 2002/1503 两帧上没出 NaN，但同类隐患在，逐处加饱和是下一刀（每处一个 clamp，成本可忽略）。
- 09-11 10:05 Zero 实测 Magpie 包换上新 cso：方块没了，"好像比之前更好一点"。网友反馈"加载不出来"（匹诺曹/机械战警）→ 漏写的前提：**Windows 开发人员模式**（`D3D12EnableExperimentalFeatures` 只在开发人员模式下成功；A 卡机是 2025 年脚本改注册表开的，从没进过文档），两个 README + 包内 README 已补。整包重打 `Magpie-DLSS5-AMD-0.10.zip`（341MB，sha256 D259AD09…，flags 换成仓库 `magpie-flags.txt`，去掉了测试用 BLACK_PROBE）。README 0.10 行已写；游戏 assets 和 tag 0.10 等游戏关掉（release-check 要求资产一致）。
- 09-11 10:40 **硬件 E4M3 Cast 全面审计**：`shaders/native_sat_cast.hlsli`（`SAT8(m)` = 逐元素 clamp ±448，`NATIVE_SAT_CAST` 默认 1，`-D NATIVE_SAT_CAST=0` 回原样），加在所有转换无界值的硬件 Cast 前：C32 split/fused（qkv 入口的输入 tile、QKV 三段含 V、fast2/fast3 的 AV 输出、非融合入口）、`c32_ffn_blocked`（输入、隐层）、`attention_direct`（AV）、`attention_fused_qkv`（V、AV）、`qkv_normalize`（V）、`vit_qkv`（V）、`project`（DIRECT_CAST 快路径的四个输出）；已 F()/归一化/softmax 有界的（P、Q/K、c32_ds 的池化输入、split_ffwd、vit_attention_fp8）不动。31 个 cso 变化，参考 fixture 有历史/无历史输出与原版逐位相同（6635520 个值零差异），2002/1503 帧头部 NaN 0。计时噪声带内（降频态 30～41ms，A/B 分不出），干净计时等重启。
- 09-11 13:20 **Magpie 接管时间 22～30 秒 → 10 秒**（bench 里网络 Create 22 秒 → 3.0 秒）。方法：`NativeVramLog` 加各组件耗时 + `NativeInitTick`（`DLSS5_VRAM_LOG=1` 打到 stdout，`DLSS5_INIT_LOG=<file>` 打到文件，add-on 用后者），所有 `CreateComputePipelineState` 走 `native_pso.h` 的计时包装（PSO 不是瓶颈：1100 个共 0.3 秒，驱动有缓存）。三个根因：① `NativeResidentTable` 每张权重表新建一个队列 + `NativeGameSubmission`（`DLSS5_TEST_ASYNC_SUBMIT=1` 时是 64 个 allocator/list 的环）再同步等——空 GPU 上每张 60ms，游戏 + Magpie 占着 GPU 时每张要排一帧；改成每设备一个拷贝批次（`NativeResidentBatch`，一张 DIRECT 列表攒所有拷贝，256 张或 768MB 自动刷，`NativeResidentFlush()` 在网络 Create 末尾和 Run 开头再刷一次）。**踩坑**：第一版只扣住上传缓冲的引用，`NativeVitLinear` 常驻完初始权重后立刻 Release 换成打包版，GPU 往已释放的目标拷 → 0x887A0006 设备挂起（bench 和 Magpie 里都复现，还带走了游戏；系统日志 `LKD_0x141_Tdr amdkmdag.sys`）；批次现在同时扣住源和目标到 flush 之后，bench 连跑三次干净。② 运行时编译的 6 个 shader 每个进程重编，ViT `normalize` 一个 fxc 就 2.55 秒——`native_shader_cache.h` 加磁盘缓存 `<shader dir>\shader-cache\<key 的 fnv1a-64>.dxbc`（key = 源码+入口+宏+快照的 include，改源码不命中；`DLSS5_SHADER_DISK_CACHE=0` 关）。③ `NativeGameSubmission::Create(q,allow_deferred)`：初始化用途不建环。参考 fixture 输出逐位不变。**Magpie 里现在 10.3 秒的构成**：读权重文件 2.4s（.f16）+ 1.3s（attention 的 .f32）+ f16→f32 展开 0.9s（冷缓存、单线程）、零碎 2.2s、CPU 打包 0.7s。下一刀：DLL 加载时后台预读权重进内存（worker 反正要等十几分钟）、展开/打包多线程、attention 权重也存 f16——能到 5 秒以内。另：重启前那台机今天四次 TDR（11:34 / 12:17 / 12:19 / 12:20，`LKD_0x141_Tdr`），12:17 那次 Magpie 空闲、只有游戏在读档，机器本身已经在随时重置的状态；重启后干净 bench：无历史 24.4～26.5ms、带历史 27.4～29.4ms（decout16 原版 cso）。游戏侧 add-on 还是 0.10 的（游戏开着没换）。
- 09-11 13:50 **Magpie 接管 10 秒 → 3.4 秒**：① worker 一启动就后台预读 assets 目录里的 *.f32/*.f16/*.i32（含 201MB noise）进内存（`NativePrefetchWeights`，`NativeReadFile` 从缓存 move 出来，零拷贝；网络建完 `NativePrefetchRelease` 清残余）；② f16→f32 展开分 8 线程；③ `NativeInitTick` 改 QPC 计时、日志句柄常开——之前每条记录 fopen/fclose 各 5ms，530 条就是 3 秒，把测量本身量进去了（GetTickCount 的 15.6ms 量化也把细分账搅浑过）。现在 Magpie 里（游戏内 DLSS5 同时在跑、GPU 抢着用）network 步骤 3.45 秒：C 段各 block 的 CPU 打包+上传 1.1s、pre 0.3、ViT/split 打包 0.6、f16 展开 0.16，没有大头了。bench 上参考 fixture 逐位不变。包内 README 的"20～30 秒"改成"3～5 秒"。
- 09-11 14:30 **C32 "合核溢出"是认错了流水线；两遍 softmax 为 null**。为验证两遍 softmax（`NATIVE_C32_TWO_PASS_SOFTMAX`：先 QK→exp→f16 行和，再重算 QK+exp 直接写归一化的 P，只留一个分数累加器）有没有消掉 09-10 记的 896 字节 scratch，重抓 RGP：抓出来的 p0087 与 09-10 逐字节相同。原因两层：① 预览 dxc 不签名，所有 cso 的 DXBC 哈希都是 0x02×16 占位符，驱动的 pipeline 二进制按哈希缓存，.rgp 里嵌的 ELF 是缓存里的旧货（GPU 跑的是新代码——把 P 乘 2 的探针确认输出变了，只是 RGP 报的二进制不对）。解法：按 DxilHash.cpp 的算法（MD5 变体，末块 = [bitlen][数据][0x80 填充][1|(n<<1)]，两行版同理）自己给 cso 签名（`scratchpad/dxilhash.py`，运行时接受，实验模式下不校验签名来源），驱动缓存就按真哈希分开了。② 签名后再抓：随源码变化的是 **p0006**（isa 42KB、7321 条、VGPR 112、LDS 29696、wmma 88、**scratch 0**）——这才是 `native_wave_c32_fused_attention_ffn.cso`；p0087（140KB、26341 条、scratch 896、wmma 8、满是 65520/512/448 的软件 H()/F() 常量）是 `native_wave_c32_full_attention.cso`（bench.ps1 第 573 行编的老单派发注意力，无 NATIVE_HW_H）：`DLSS5_TEST_WAVE_C32_PROJECTION=1` 时每段都建成 `split_pso[1]`，但 `Record` 里 `fused_attention` 分支只派发 `split_pso[2]`（融合核），`split_pso[1]` 只在非融合分支用——建了从不派发，RGP 把所有创建过的流水线都嵌进去了。**结论：真正跑的 C32 合核没有溢出；09-10 清单上"C32 消溢出 → 40fps"这条作废。** 两遍 softmax 逐位相同但核变大（VGPR 112→118、7627 条），关掉留档。剩下能动的是占用率：p0006 VGPR 112 在 4 wave/SIMD 档，压到 ≤96 才升 5 档，得看逐指令活跃度（RGP 图形界面 -Instr）再定。附带：`isa-stats.py` 的 rga 输出目录不清旧文件，同名 pipeline 会拿到上次的 .isa——这次是拿 ELF 的 md5 对出来的。
- 09-11 15:10 **0.11**：接管优化（预读/合批/磁盘 shader 缓存/多线程展开）打进 Magpie 整包 `Magpie-DLSS5-AMD-0.11.zip`（341MB，sha256 8BFC9EA7…，add-on sha256 0129F7F4…），包内 README 改 3～5 秒；README 两份加 0.11 行；tag 0.11（shader 与 0.10 相同，release-check 绿）。游戏侧 add-on 仍是 0.10 的（游戏整个周末开着，Zero 回来关了再换）。GPU 被游戏占着，计时类的活（p0006 占用率、C512 尾巴、九块跳过、重叠）等周一。
- 09-11 18:20 Zero 远程试九块跳过：37～38fps、看不出区别；但 +5% 换 PSNR 41.9→36.5（误差能量 3.5 倍）、别的游戏暗场景没验过，定为不做默认，flag 改回 {42,43,46}（九块版留作文档里的"性能档"）。问"风格可调"：捕获的输出合成里 TransferStrength/ColorStrength 两个混合系数一直写死 1,1，现在 `DLSS5_STRENGTH=<transfer>,<color>` 可调（即 NVIDIA 面板的强度；风格预设 = DLL 里别的网络描述符，没提取，只有一套权重）。add-on 已装进 Magpie 包，游戏侧等关游戏。
- **下一步（压缩上下文前的清单，09-11 18:20）**：
  1. Magpie 每帧 33～36ms 里网络 24 之外的 ~10ms：量捕获 / AMD 光流 / 呈现各占多少，光流占大头就换便宜的运动估计或降分辨率算。网友主要用这个形态。
  2. 游戏鉤子版：游戏 6～7ms 与网络 24ms 重叠（多一帧延迟），改提交结构，Zero 远程看帧率。
  3. 游戏侧 add-on 换成当前源码（0.10 的还在游戏里；含 prefetch/合批/shader 缓存/STRENGTH），等 Zero 关游戏；换完 release-check。
  4. 小刀：p0006 VGPR 112→≤96（占用率，收益不定）、C512 尾巴 −0.1～0.2。
  5. 2K 屏：网络先跑 1080p 再让 FSR 放大（结构改动，Zero 拍板）。
  6. 不做：跳块维持 {42,43,46}，九块版只作文档"性能档"；风格预设不做（只有 Natural 那套权重），强度用 `DLSS5_STRENGTH`。
- 09-11 18:35 Zero 18:03 自己关了游戏（事件日志无崩溃；13:09 之后再没有 TDR）。**清单 3 做完**：游戏目录的 add-on 换成 HEAD 构建（备份 `native-submission-order.addon64.0.10`），release-check 三项绿（add-on 哈希"不同"是构建路径进了 debug 段：同一源码在 scratchpad / release-check / Magpie 包三处构建出三个哈希，这一项只能作参考）。顺手：游戏 flag 文件里留着 `DLSS5_BLACK_PROBE=1`、`DLSS5_DEBUG_DUMPS=1`（黑块排查时加的，每帧多一个小派发 + 25MB 拷贝，300/600 帧各转储 233MB），release-check 对这两项免检所以一直没发现——收口时去掉。
- 09-11 18:45 **今晚的安静基线**（游戏关着，Splashtop 会话在）：`logs\quiet-bench.ps1`（replay.ps1 + GPU profile，`stages.py` 按段汇总）decout16-sat 30 帧：无历史 28.5～31、带历史 28～29.4ms，段和 = 总数（无提交空隙）；原版 cso 目录同条件同样 29～31，所以比 13:20 记的 24.4～27 慢不是饱和 clamp 的锅，是机器状态（Splashtop 虚拟显示器在跑）。段账（带 profile 时间戳）：preblock 3.4、encoder5_8/9_14/15_22 2.2/2.8/3.1、post70 1.8、C512 段 1.6、C32 尾段 49～69 合计 5.2、encoder C32 三段 2.4、decoder 十几段 1.5——融合 C32 核（p0006）合计约 13ms，仍是最大的一块。
- 09-11 18:55 **清单 1 有结论：Magpie 自己的开销约 2ms，不是 10ms**。方法：不开游戏，用一个 1920×1080 的动画窗口（`logs\anim.ps1`，WinForms 定时重绘，HWND 写到 `logs\anim-hwnd.txt`；`dlss5anim` / `dlss5toggleanim` 两个计划任务，`logs\runC.ps1` 一键：起窗口 → 起 Magpie → 切换 → 收日志 → 截图）当捕获源。结果：每帧 24.1～24.4ms（41fps），`DLSS5_GAME_PROBE` 看 GPU：网络 21.5 + pre 0.6 + post 0.25 = 22.4，CPU 帧 23.2。也就是捕获 / AMD 光流 / FSR / 呈现加起来不到 2ms。游戏里 33～36ms 的"多出来的 10ms"是游戏自己的渲染在抢 GPU（游戏内探针早就显示 network=30，同一网络单独跑 21.5）。**Magpie 侧没有可砍的**，剩下的杠杆只有网络本身。
- 09-11 19:10 **清单 2 做了，null**：`DLSS5_OVERLAP=1`（默认 0）——网络搬到自己的 COMPUTE 队列，落后一帧：游戏队列每帧先把上一帧结果 decode 到自有缓冲、再从未动过的 target 捕获（原图拷贝 16MB + encode + motion）、最后把上一帧结果拷进 target 并 signal；计算队列 Wait 后跑 input → 网络 → 历史 → neural，signal；decode 用的原图是网络看到的那帧（`original_copy`），结果与同步路径同一公式只是晚一帧。`NativeGameSubmission` 现在接受 COMPUTE 队列（列表类型跟队列），加 `Fence()`/`WaitOn()`。第一版顺序错了（先把上一帧结果拷进 target 再捕获，网络吃自己的输出，画面成噪声），改成 decode → 捕获 → 交付后 Magpie 里画面正确，无游戏时 23.9ms（和 24.1 差不多，本来就没东西可重叠）。**游戏主菜单实测（`logs\gameRun.ps1`：起游戏、等 every_frame、杀游戏、还原 flags）：基线 32.5～34.9ms，overlap 49.5～50ms——更慢**。探针：计算队列上的网络 45.8ms（单独 21.5、串行在游戏队列上 30）：图形队列和我们的计算队列真并发时两边都拖慢，总吞吐反而不如串行。计算队列 HIGH 优先级（`DLSS5_OVERLAP=2`）同样 52ms、网络 45.6——优先级不改变 CU 仲裁（图形波前优先，计算队列捡剩下的），无游戏时计算队列上的网络是 22ms，所以慢是并发本身。**结论：这张卡上游戏图形与我们的计算队列真并发是双输，串行在游戏队列上（现状）最快；清单 2 作废。**代码留着（flag 默认 0；1 = 普通优先级，2 = HIGH），`NativeGameSubmission` 的 COMPUTE 支持和 `WaitOn()` 是通用能力。游戏目录和 Magpie 包里现在都是这个构建（flag 关时与 HEAD 同行为）。测试脚本：`logs\gameRun.ps1 [-Overlap 1|2] [-Probe] [-Deploy]`（起游戏到主菜单、等 8 条 every_frame、杀游戏、还原 flags；主菜单场景基线 32.7ms，比 Zero 游戏内的 27 重）。
- 09-11 20:40 **清单 4 的"p0006 占用率"做了，null**。融合 C32 核 LDS 29696B = qkv8 12KB（每 token 24 uint：Q/K/V）+ pw8 8KB + ex 8KB + ones16 1KB；D3D 上限 32KB，所以"多加 4KB 看会不会变慢"的探针做不了（验证器拒绝）。手术 `NATIVE_C32_LDS_SLIM`（`DLSS5_BUILD_C32_LDS_SLIM`，默认 0）：Q 行放本波自己的 pw8 片（输入瓦片被读进寄存器之后、P 瓦片写入之前那段是空的），qkv8 只留 K/V、每 token 16 uint，LDS → 25600B（每 WGP 多一组）。参考 fixture 逐位相同，同批 A/B：C32 各段合计 16.52 → 16.24ms，整帧 34.88 vs 34.87——噪声以内，占用率不是这个核的瓶颈（和 09-09 的"占用/组同步两假设排除"一致）。第一版试过让 f16 瓦片和 E4M3 瓦片共用一个 uint 数组（F16 矩阵 Load/Store 进 uint groupshared）：dxc/验证器过、驱动建 PSO 时进程 access violation——**wave-matrix 的 groupshared 数组元素类型要和用法一致，别跨类型别名**。另：改动虽然语义不变，宏关着时表达式结合顺序一变 cso 哈希就变（+124 字节）——验收"默认行为不变"要看 cso 哈希，改代码时保持原表达式原样。
- 09-11 20:45 **隔离单核账（`DLSS5_TEST_ISOLATE`，repeat 100，暖缓存）**：pre:2（块 0 融合核，1920×1152）1.63ms、c32:0/1（块 1/2，960×576）0.35/0.37、tail:49 0.27、tail:67 0.36、post:0（post70 整段）1.84。pre 看着是别的 C32 块 4.7 倍，其实就是 4 倍 token 数——**编码器 C32 块 1～4 跑在 960×576（pre 的 2×2 池化之后），不是全分辨率**；按 token 算各 C32 核成本一致，没有异常的核。顺手排除了 pre 的内联前缀（高斯噪声生成、颜色/历史读取各自去掉都不省时间，探针版本没留）。**测试台的一个失真**：bench 把 post base 色（35MB f32）、历史、运动、倒数表都放 UPLOAD 堆直接给 GPU 读（`d3d12_native_network70_test.cpp` 的 `upload()`），post70 的 rgb 头和时序采样在测试台上走 PCIe，游戏里是显存——游戏探针网络 21.5ms、测试台带 profile 29～30，差的 7～8ms 一部分是这个，一部分是 113 个时间戳的排空。以后要对齐可把这些换成 `NativeResidentTable` 拷贝（一行一个），今晚没动。
- **今晚小结（09-11 20:50）**：清单 1（Magpie 开销 2ms，无可砍）、2（重叠 null）、3（add-on 换新 + release-check 绿）、4a（占用率 null）都收口；网络核按 token 成本均匀，剩余杠杆只有结构性的（更小的网络 / 跳块 / 分辨率），都是 Zero 拍板的事。安静基线里可信的整帧数：游戏探针 21.5ms（Magpie，无游戏），测试台 profile 数只用于同批 A/B。

- 09-12 02:45 **0.12：屏幕提示**（Zero 定的：不做缩放，输入不是 1080p 就在画面上写一行）。`native_text_overlay.h` + `shaders/native_text_overlay.hlsl`（运行时编译，随 assets 部署）：5×7 点阵字体（A-Z 0-9 和几个符号，Python 生成的 64 项表）。钩子在 ffxDispatch 里按帧决定文案——输出不是 1920×1080："INPUT MUST BE 1920X1080 (NOW WxH)"（这时钩子根本不会武装，网友之前只能看到"没效果"）；网络初始化中："INITIALIZING..."；初始化失败（phase 5，开发人员模式没开 / 驱动不对）："INIT FAILED - SEE DLSS5-AMD\LOGS"。**两次踩坑**：① 第一版直接往游戏的命令列表里录（拿 FFX 传进来的 list），Magpie 在初始化中途崩在 D3D12Core（c0000005）；② 改成自己的命令列表在 ExecuteCommandLists 钩子里、游戏那批之后提交，还是崩——`DLSS5_NOTICE=1..5` 逐项二分（空列表 / 只设状态 / 建 UAV 不派发 / 全开）定位到 **对宿主贴图 `CreateUnorderedAccessView` 就崩**，和 codec 当年"UNORM 贴图建 UAV 设备移除"是同一类：宿主的贴图只能拷贝、别建视图往里写。终版：字画进自己的 raw 缓冲（按宿主格式打包：RGBA8/BGRA8/RGBA16F/RGBA16 UNORM），`CopyTextureRegion` 拷进宿主贴图（UAV 状态 → COPY_DEST → 回），自己的 `NativeGameSubmission`（同步等待，提示态才有）在游戏批次后提交。Magpie 1600×900 窗口实测（Magpie 放大到 1080p，钩子武装）：初始化期间左上角黄字黑底可见，完成后消失，不崩。尺寸不对那条路在这台 1080p 桌面上造不出来（Magpie 不缩放比屏幕大的窗口），同一条 Draw 换个字符串，按同路径算通过。`DLSS5_NOTICE=0` 关。顺手：`logs\anim.ps1` 读 `anim-size.txt` 可指定窗口尺寸；Magpie 会把小于屏幕的窗口放大到 1080p 再交给 FSR（render=1600x900 upscale=1920x1080），这种情况网络照常武装，画质是 FSR 放大后的。

- 09-12 08:05 Zero 把桌面切 4K 实测：Magpie 把 1080p 窗口放大到 3840×2160，左上角出 "INPUT MUST BE 1920X1080 (NOW 3840X2160)"——三种文案都验过了。热键笔误：Magpie 的缩放热键是 **Alt+Shift+A**（config 里 3137 = 0x0C41），包内 README 和教程原来写的 Win+Shift+A 是错的，已改；0.12 包重打（sha256 160DBD31…，网盘 https://pan.quark.cn/s/a5bafe0e2050 ）。
- 09-12 08:58 **钩子按声明状态接管**（1bb1c90）：网友（UE 游戏，游戏内钩子路线）0.11 日志 `armed=0`，豆包分析说是 "UnwrapDevice 失败终止构造"——那行 `device_identity ... unwrap_hr=80004002` 是纯诊断，不参与任何判断，分析是编的。真实武装条件四条：第 120 帧起、输出 1920×1080、`output.state==2`、ffxDispatch 返回 0。其中"状态必须是 2（UAV）"只是《剑星》和 Magpie 的巧合，别的引擎声明 4（compute 读）/256（渲染目标）就永远旁观。现在 `ffx_state_to_d3d12()` 把 ffx_api 的状态位映射成 D3D12 状态（1 common / 2 UAV / 4 NPSR / 8 PSR / 12 两者 / 16 copy src / 32 copy dst / 20 generic read / 128 present / 256 RT），`PendingSnapshot.state` 一路传到 `OnSubmitted(...,state)` → `ProcessSubmittedFrame(source,state,state,...)` 和 `NativeReadSubmittedFrame(q,source,state)`；映射不了的值记一行 `output_state_unknown` 让网友贴。提示层也按声明状态做拷贝转换。1080p 那条没法松。列表指针匹配那条不改：ffxDispatch 录在哪个列表就得等那个列表提交完，是必要不是苛刻。
- 09-12 15:23 **Windows Update 把预览驱动换掉了**。Zero 用《轮回之兽》试 Magpie，屏幕 INIT FAILED；日志 `initialization_failed native preblock HRESULT=2147942487`（E_INVALIDARG）连续 9 次。排查：0.12 包 add-on 和最新构建都一样 → 环境；开发者模式 1、`experimental=00000000` 正常；测试台也在同一步失败 → 和 Magpie/ReShade 无关；给 206 个 cso 全签 DxilHash 再跑还是失败 → 不是签名；给 preblock 的 `Check()` 加 `__LINE__`（d6fbb92）定位到第一个 SM6.10 wave-matrix PSO 的 `CreateComputePipelineState`。事件日志：**11:48 Windows Insider 预览更新（26340.9482）重启，显卡驱动从预览版 32.0.31007.2048 变成正式版 32.0.31044.16（2026-09-09）**——正式驱动没有 SM6.10 wave matrix，所有 PSO 都 E_INVALIDARG。安装包在 09-10 清盘时删了，AMD 那个链接还活着，重下到 `D:\DLSSNR-Lab\drivers\amd-software-adrenalin-edition-26.10.07.02-win11-rc7-agility-sdk.exe`（874669800 字节，sha256 cca61a1d…，和 09-05 一致），Zero 重装后好了（DriverVersion 回到 32.0.31007.2048）。防再犯：`HKLM\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate\ExcludeWUDriversInQualityUpdate=1` 已设。**这是网友必踩的坑**，包内 README"需要"一节已写。附：decout16-sat 的 cso 现在是签名版（`unsigned\` 备份），逐位同输出。
- 09-12 16:20 **`DLSS5_SHOW_FPS=1`**（6626616）：网络自己的帧率用提示字体画在角上（"DLSS5-AMD 32 FPS (31.6 MS)"，每 100 帧刷新，取 every_frame 的均值）。走提示层同一条路（自己的列表在游戏批次后同步提交），开着每帧多约半毫秒。Magpie 包默认开（`scripts/magpie-flags.txt`），游戏 flags 不开。
- 09-12 17:10 **0.13 + 帧生成**：Magpie 实验分支没带 FSR3 的 FG（只有上采样 dll），但带了 **XeSS 帧生成 ZeroMV**（`libxess_fg.dll`，Intel 跨厂商版，不要游戏运动向量、自己估光流）。在 DLSS5-AMD 效果组 FSR3_SR 后面挂 `XeSSFG\XeSS_FrameGeneration_x2_ZeroMV`（参数照 XeSSFG 组抄：amdOpticalFlowMode 1 / opticalFlowMethod 0），Magpie 效能分析器（Alt+Shift+P，WM_HOTKEY id 3）：**帧率（总/真实）55/28 FPS**，FSR3_SR（= 我们的网络）23.4ms，FG 帐面 0.017ms（异步队列）；网络间隔 24 → 31.6ms（光流和 FG 抢 GPU）。多一帧延迟。x3/x4 的 `XeSS_MultiFrameGeneration_ZeroMV` 没试。打包脚本抄 Zero 的 LocalAppData 配置（备份 `config.json.pre-fg.bak`），所以包里默认带 FG；README 写了怎么关。**tag 0.13**（重打）：`Magpie-DLSS5-AMD-0.13.zip` 342MB sha256 9104C48DA924…（第一版 C984 无 FG 作废）。远程工具新增：`dlss5toolbar` / `dlss5profiler` 任务（给 `Magpie_Hotkey` 窗口 PostMessage WM_HOTKEY id 2 / 3）；`logs\addfg.ps1 [-Remove]` 改/还原效果组；`logs\diag*.ps1`、`finddrv.ps1`、`wu-policy.ps1`。

- 09-12 **0.13 后 FPS 显示优化（待实机验收）**：Zero 定数字三秒更新一次。`NativeGameFrame` 在已有 decode/copy 输出列表末尾叠字，去掉稳态 FPS 专属 `NativeGameSubmission::Create(q,false)` 的提交与 CPU 等待；数字仍取 every_frame 的 100 帧均值，显示更新间隔至少 3000ms。`NativeTextOverlay` 缓存字条内容（文字/缩放/像素格式/行距），不变就只拷贝，不重新 Dispatch；字条放在输出末尾，网络输入及 history 不含 FPS。初始化/失败/尺寸提示仍走原提示提交路径。overlap 路径在交付上一帧结果后叠字；新 session 清空 FPS 均值。开关沿用 `DLSS5_SHOW_FPS` 和 `DLSS5_NOTICE`。验证：MinGW tiled add-on 编译通过、git diff --check 通过；候选 `Development/fps-cached.addon64`，未部署，未测实际收益，不把旧记录的约 0.5ms 当本次实测收益。闇之朱雀。

- 09-12 **Magpie 0.14 整包**（闇）：已部署的 FPS 优化构建（`529a0e7`，DLL SHA256 `B4AA401DE728288033A27B586E31DC0B1BB3EF0EE84385CB1CF894F611C49B8F`）打进 `D:\DLSSNR-Lab\Magpie-DLSS5-AMD-0.14.zip`，358004639 字节，SHA256 `14CCDE3C752B40821CB9F30024579304627A2499E087E06E9AEC399BFE734EED`。沿用 0.13 的 Magpie 本体/资产及 XeSS FG ZeroMV 便携预设，FPS 默认开；说明改三秒刷新，清理备份 DLL、日志和 shader 缓存，重新生成全包 SHA256SUMS.txt。压缩包内 671 个文件逐个解压计算哈希通过。脚本 `Development/tools/package-magpie-fps.ps1`（`-VerifyOnly` 只校验现成包）；旁置 `.zip.sha256`。未打新 tag、未上传网盘；FPS 性能收益尚无实测数字。

- 09-12 18:25 Zero 已上传 0.14 整包，夸克分享：https://pan.quark.cn/s/8bbc3033181d 。中英文 README 下载入口已更新为 0.14。
- 09-12 19:35 **C512 FFWD 换 FP8 矩阵，做了，null**（收尾清单的最后一刀，314 期文章挖出来的线索：这段 FFWD 的 wave matrix 声明 F16，跑的是 195 而不是 389 那档）。前提验证：三个权重矩阵（`blockNN-ffwd.f32`）524288 个值全部在 E4M3 格点（min 非零 1/512、max 0.5625）；三段矩阵的 A 输入本来就是 E4M3（块输入 = 上一块的 E4M3 raster，mix/expand 输出 = F(H()) / Activate()），所以换 FP8 操作数不丢任何位。实现 `NATIVE_SPLIT_FFWD_FP8`（`shaders/native_wave_split_ffwd_parallel.hlsl` waves4 路径的 FP8 分支：A/B 用 `F8_E4M3FN`，权重 host 打成 512B `[k 32][j 16]` E4M3 瓦片，LDS 改成 uint 存 E4M3 字节、行跨 32/80/272 字节，暂存每线程一个 uint 一次 Load 代替四次逐字节解码）；宿主 `DLSS5_SPLIT_FFWD_FP8=1`（`native_split.h`，只对 stream8 链里 E4M3 输入的块生效，链首 f32 块仍走 f16 核），cso `native_wave_split_ffwd_parallel_stream8_fp8.cso`（`DLSS5_BUILD_SPLIT_FFWD_FP8=1` 才编）。结果：**输出逐位相同**（nohist/hist 都和 alias-base 一致——硬件 FP8 和 F16 两条 wave-matrix 路径的累加顺序一样）；把 cso 藏起来初始化即 "split shader failed"，证明核确实被用上。**时间没动**：同批 30 帧，`encoder23_30_body`（8 块 C512 整体）min 0.852 → 0.880、median 1.494 → 1.502；解码器 6 个 C512 段逐段一样；隔离法（`DLSS5_TEST_ISOLATE=split:1,3,5` ×100，两轮）min 0.118/0.114 vs 0.116/0.114；整帧 min 31.9～32.7 两边重合。结论：这个核不是矩阵指令吞吐卡的（每 K 步两次组同步的 LDS 暂存 + 权重搬运才是），09-09 "120 TFLOPS 近算力上限"那句按 F16 算的口径也就不成立——它离 F16 峰值远，只是被延迟绑着。**默认关，不进发布 cso**（bench.ps1 门控，release-check 绿）。314 期那段"F16 档 vs FP8 档"的推断只对了一半：存储省一半 ≠ 计算翻倍，这回连"计算翻倍"本身也没兑现。远程脚本 `logs\ffwd8.ps1 [-Base] [-NoRun]`、`ffwd8ab2.ps1`（隔离 A/B）；decout16-sat 的 exe 已是含此 flag 的构建（备份 `.pre-ffwd8`）。

### fast 链每刀收益表（测试台，ms；同批 A/B，噪声 ±1～2）

| 刀 | 内容 | 前→后 / 收益 | 游戏部署 |
|---|---|---|---|
| exact 终点 | tag 0.01 | 186 | 5fps（每帧 reset） |
| fast-accumulate | GEMM 去每 K32 软件 H，FP32 累加 | 186→159.8 | 6fps |
| fp8-ffn / fp8-vit | 操作数 E4M3 | 154.8 / 151.6 | |
| fast-epilogue | FFN 激活去 3 次中间 H | 130.1 | |
| fast-attention | WaveActiveSum 归一化、exp 去 H | 115.3 | |
| fp8-proj/qkv | 投影 E4M3、多头 QKV wave GEMM | 111.9 | fast，8fps（时序后，0.02） |
| fast-temporal | 时序两 pass 去 double | 115（history 帧 5.2→0.5） | |
| QKV+normalize 融合 | | 115→104.5 | |
| FP8 激活/硬件量化/FP8 QKV | | →99.5 | |
| fast-prefix | 噪声表→ALU Box–Muller | 99.5→97.6（prefix 7.2→2.0） | |
| C32 注意力四段融两段 | | →95.1 | |
| C32 FFN 整块存取 | | →93.9 | |
| 隐藏层硬件 Cast + FP8 contract | | →90.0 | |
| 软件 H/F→硬件 | | →87.9 | fast2 |
| C32 Q/K/V 硬件 Cast | | 84.3 | |
| ViT 注意力快速版 | | 82.4 | |
| ViT contract split-K | | 80.6 | fast3 |
| C512 注意力硬件 H+位元 F | | 79.1 | |
| C512 ffwd 4 wave | stage0 0.289→0.077 | 74.9 | |
| ViT packed 输入 + BLOCK_M=4 | | 71.3 | fast4，12fps |
| C32 mapped input | | 70.8 | fast5，12～13fps |
| 多头残差流 FP8 | | 67.5 | fast6 |
| C512 direct 注意力 | stage2 0.31→0.02 ×16 | 62.7 | fast7，**tag 0.03**，~15fps |
| 直接 Cast / 矩阵残差 / 平铺权重 / C32 FFN fast3 | | ≈−2.7 | fast8 |
| C32 注意力 fast2 / post70 direct / C32 链读 raw | −11% / −3.3 / −1.0 | sum-of-mins 55.7→50.6 | fast10 |
| wave ds4 | 2.79→0.06 | −2.7（→47.8） | fast11 |
| C32 QKV fast2 / 多头注意力 fast2 | | →45.1 | fast12 |
| ViT 注意力 FP8 | 0.36→0.18 ×8 | −1.35（→42.6） | fast13 |
| 延迟提交进游戏 | 游戏 GPU 38.5→33.2 | | fast16 |
| C512 pack/crop 向量化 / post70 merge fold | −0.55 / −0.8 | | |
| C32 注意力 fast3（QKV 合进） | | **−8** | |
| inline prefix | 1.79→0，PSNR 42.16→41.98 | 稳定段 ≈33 | fast17 |
| 命令列合批 | CPU GPU+4.5→+2.8 | 游戏 GPU 32.0 | fast18，**tag 0.04**，23～25fps |
| C32 注意力 fast4（一组两窗） | 35.59→34.09 | −1.5 | fast19 |
| ViT QKV 合核 | 0.17→0.08 ×8 | −0.74（→33.94） | fast20 |
| 输出平滑 | post +0.1 | | fast22/23，**tag 0.05**，24～25fps |
| 关探针 | 省每帧 Flush | | fast24，27～28fps |
| preblock main8 | | −0.55 | fast25，**tag 0.06** |
| 跳块 {42,43,46} | 40.66dB | ≈−1.0 | fast26 |
| 显存收敛 | 6839→5073→3.75GB | | fast27～33，29fps |
| C32 链尾 finish+crop / 块 69 main8 | | −0.9 / −0.23 | fast34 |
| C32 中间量 f16 | 10.61→8.70 | −1.9 | fast35 |
| post FFN 整数除法 / finish+rgb 头并进注意力 | 1.69→1.2 / | ≈−1.5 | fast36，**0.07 包** |
| C32 FFN 并进注意力核 | 6.95→5.75 隔离 | −1.2 / 整帧 −1.9 | fast37 |
| 多头 FFN+proj0 合核 | 逐位相同 | 0（不进游戏） | |
| tile 布局（ViT / C512 / entry） | | −1.6 / −0.85 / −0.01 | fast38，min-of-means 34.0 |
| C512 FFWD 权重 tile / 输出 E4M3 tile 直读 | 0 / 每块 −0.06 | ≈−0.8 | fast39 |
| C512 mapped + FP8 stream（去 pack/crop/QKV pack） | 每块 −0.02 | ≈−0.3，显存 −380MB | fast40 |
| post merge fold 4 通道一次 Load | post −0.09 | ≈−0.1 | fast41 |
| 解码器投影收尾 float4 连续写 + 位元 F | proj62 −0.06 / proj66 −0.03 | ≈−0.1 | fast42 |

---

## 4. DX12 阶段运行时约定（截至 2026-09-10；HIP 与 OptiScaler 变更见后续时间线）

### 游戏目录与根目录规则

- 游戏 `Win64\` 只放：`d3d12.dll`（ReShade 6.8）、`dlss5-amd.addon64`（或开发版 `native-game-fastN.addon64`）、`DLSS5-D3D12-721\`（私有 Agility 721，含 `D3D12Core.dll`）。
- DLL 找根目录：先找与 DLL 同目录的 `DLSS5-AMD\`（有 `native-game-flags.txt` 就用它——这是给网友的包的布局），没有才退回 `D:\DLSSNR-Lab`。**开发机游戏目录里不要留 `DLSS5-AMD` 文件夹**，否则 `deploy_fast.ps1` 改的是 D 盘那份、游戏读的是另一份。
- exe 门：`SB-Win64-Shipping.exe`、`Ronin.exe`。

### `D:\DLSSNR-Lab\` 布局（09-10 清盘后 11.5GB）

| 路径 | 内容 |
|---|---|
| `native-game-tiled-assets\` | 游戏资产：权重/输入 `*.f32`、cso、运行时编译的 hlsl、`manifest` |
| `tiled\`（fast38）/ `ffn-fused\`（fast37）| 当前链目录 = 测试台 + 游戏 cso 来源；回退用上一目录 |
| `native-temporal-valid1080\` | oracle（exact 裁判） |
| `matrix-probe\dxc-preview\`、`matrix-probe\matrix_probe.exe` + `D3D12\D3D12Core.dll` | DXC 与能力探针（runner 链底层；清盘别删） |
| `matrix-probe\native-runtime-rgb512\functions.f32` | 192MiB 通用随机函数表（exact 链需要；快速版 prefix 已改 ALU） |
| `native-game-flags-fastN.txt` | 每个部署版本的 flag 文件 |
| `logs\` | `native-game-oneshot.txt`、`native-game-probe.txt`、`native-submission-order.txt`、`flicker-*` |
| 开关文件 | `continuous-every-frame.txt`（每帧同步跑）、`temporal-history.txt`（启用 history）、`enable-game-sdk721.txt`（建设备前切 721）、`enable-dred.txt` / `enable-d3d12-debug.txt`（浪人诊断） |

### flag 机制

- 游戏 DLL 初始化时读 flag 文件，把 `DLSS5_TEST_*`（测试台链的 56 条）和 `DLSS5_*` 宿主 flag 写进进程环境（`flags_applied=N`）。早期只放行 `DLSS5_TEST_` 前缀，宿主 flag 进不去的症状是"效果很小"不是花屏。
- 每一刀 = 新 flag + 新 release 目录 + 新 commit；exact 链的 flag 组合保留可回退。编译期宏由 `DLSS5_BUILD_*` 走顶层 runner 设置（下层 runner 编译时环境变量还没设）。
- `DLSS5_GAME_PROBE`：每 100 帧写 pre/network/post GPU + cpu_frame，但每帧多一次 Flush（3ms），只临时开。`DLSS5_DEBUG_DUMPS=1` 只在我们自己的 flag 文件里开。
- F6：0 神经 / 1 原生对照 / 2 左右对照。

### 帧内数据流与 buffer 布局

1. 钩 `ffxDispatch`：快照参数，AddRef 五个资源，调 trampoline 让原 FSR 先录；从 `+120` 读运动向量纹理（1284×724 RG16F，UV 单位）、`+408` 读 reset。
2. `native_temporal_feed`：运动向量换算成 1080p 像素单位，上一帧网络输出转 float4 history；`native_temporal_coordinates` / `native_temporal_sample`（快速版 `NATIVE_FAST_TEMPORAL`：float 双线性 + 五点核）。首帧和 reset 帧无 history。
3. 输入：1920×1080 有效 → 镜像补到 1920×1152，8×8 tile-major RGBA；preblock 输出 main（E4M3 raster，`DLSS5_PREBLOCK_MAIN8`）+ down。
4. C32 家族（pre、1–4、67–69、post）共用一对 scratch `SharedRaw/SharedFfn`（fast35 起 f16；fast37 起合核后相邻实例交替当 raw，`SharedParity()`）；映射输入的 stage 用 16 字节替身代替 packed/merged；Heap 的 SRV 大小 clamp 到资源宽度。
5. 多头 Swin（C64/128/256）残差流 E4M3（`DLSS5_FP8_STREAM`），链首块输入 f32、链尾输出 f32；C512 pack→融合 QKV+normalize→direct attention→FP8 直读投影；ViT 640 token，tile 布局权重/激活（fast38）。
6. post70：merge fold 直接读 low/skip（mode 4/7/9），注意力收尾按 `epilogue_mode`（0 只写 raw / 2 main8+down / 3 rgb 头），rgb 头 f32 点积一次 f16 圆整，输出残差原位叠到 FSR 输出（UNORM 或 FLOAT 按 `NativeViewFormat`）。
7. 提交：延迟提交环 64（`DLSS5_TEST_ASYNC_SUBMIT`），命令列合批（`DLSS5_BATCH_SUBMITS=2`：ViT 4 层一列、decoder 两列，约 6 列/帧；整网单列曾 DEVICE_HUNG）。所有 default-heap ≥32MB 的 buffer 每 60 帧 `MakeResident`，驻留优先级 maximum。
8. 输出平滑：`native_output_smooth.hlsl`，|out − warped 上帧| < t/255 的像素向 warped 混合（`DLSS5_OUTPUT_SMOOTH=10,0.8`）。

### 构建 / 部署 / 测试

- 测试台：`bash scripts/build-bench.sh <exe>` 交叉编译（MinGW）；scp 到链目录；`Development/run_<top>_network.ps1 -Folder D:\DLSSNR-Lab\<dir>` 或 `scripts/bench.ps1`（76 层 runner 拍平）；取回 `network.stdout.log gpu-network70*.f32`；`python3 tools/compare_fast_output.py --root release/<dir>`。不重编只跑 `Development/tools/bench-norebuild.ps1`（25 秒，`make-norebuild.sh` 生成）。A/B：`tools/abn.sh` 交替 3 轮 + `cmpmin.py`；单核 `DLSS5_TEST_ISOLATE=<kind>[:index[:part]]`（`isolate.sh`），隔离后输出是垃圾。
- 游戏 DLL：`bash scripts/build-addon.sh <minhook repo> <reshade 6.8 include> <out>.addon64 --tiled`；部署 `deploy_fast.ps1 -Source <链目录> -Dll <dll> -Flags <flags.txt>`（游戏运行中拒绝）；运行时编译的 hlsl（`native_post70` / `native_split_window` / `native_output_smooth` / `preblock_finish`）改了要 `update-manifest.ps1 -Names <一个文件名>`。**host 打包改动在 DLL 里，只换 cso 不换 DLL 会算错。**
- 发包：`scripts/package-release.py` + `scripts/package-README.txt`，权重转 f16（都精确），`noise.f32` 192MB 保持 f32，zip 约 220MB。0.07 包 = fast36。
- Magpie 版包：`package-release.py` 用 `scripts/magpie-flags.txt`，`d3d12.dll` 改名 `dxgi.dll`，README 换 `scripts/package-README-magpie.txt`；远程测试三件套 `D:\DLSSNR-Lab\logs\magpie-toggle.ps1`（拉前台 + WM_HOTKEY 给 `Magpie_Hotkey` 窗口）/ `capture_amd_desktop.ps1` / `launch-sb.ps1`，都用 `schtasks /it` 起（任务名 dlss5toggle / dlss5shot / dlss5game / dlss5magpie）。
- 打 tag 时统一刷新根目录公开集（`scripts/game-flags.txt`、README 状态行、`bench.ps1` 重新拍平）；过程文件只进 `Development/`。打 tag 前先跑 `scripts/release-check.sh`（09-10 加：仓库编译的 179 个 cso 逐个对游戏资产哈希、game-flags 对游戏 flag 文件（去 CRLF）、add-on 哈希仅供参考——mingw 每次编译嵌时间戳，本地两次都不同），再由 Zero 在笔记本上拉下来亲手编一遍。已知未清项：`native_matrix_pack.cso` 游戏资产里是旧源码编的，新的在测试台验过逐位一致，未覆盖。

### 证据目录（`release/`，git 忽略，不入库）

| 目录 | 内容 |
|---|---|
| `release/native-network70-*` | 09-07～09-08 每一刀的 15 帧 exact 证据（stdout、`profile-validation.json`、两份 GPU 输出） |
| `release/<fast-dir>/fast-validation.json` | 快速版每刀对 exact 链的 PSNR/max/RMSE |
| `release/native-temporal-valid1080/` | 时序 exact 裁判与 oracle |
| `release/native-runtime-rgb512/amd/final-validation.json` | 真实移位 RGB512 全 exact |
| `release/native-rgb-valid1080/`、`native-vit/`、`native-c512/`、`native-post70/` … | 09-06～09-07 逐块原版/CPU/AMD 对照 |
| `release/checkpoints/2026-09-07-correct-game-preview/` | 2fps 正确画面版 DLL/exe 封存 |
| `release/driver-install-2026090{7,8}-*` | 预览驱动安装记录与回退材料 |
| `release/temporal-probe-48648/`、`release/determinism/`、`release/half-conversion-probe/` | 运动向量对齐、帧间确定性、half 舍入探针 |
| `dynamic-captures/` | 游戏截图（方格前后、AA 修复对照、审计） |

### 浪人崛起（XeSS）额外约定

`libxess.dll!xessD3D12Execute`（XeSS 1.x/2.x SDK 结构）取输出 1920×1080、速度贴图 1280×720、reset；`NativeMotionSign()=-1`；读游戏贴图用 UNORM SRV，中间纹理一律 FP16，写回走 raw buffer + `CopyTextureRegion`；批匹配只按命令列表身份（`cross_thread_submit`），过期判定放宽到落后 2 帧；探针初始化"第 120 帧强制起一次"。检查单：新游戏先跑 `Development/xess_probe.cpp` 看几何和格式，再改约定。

---

## 5. 历史候选方向（2026-09-10；执行结果见后续时间线）

上限估计：1+2+3 全做约 −3ms → 网络 22ms → 游戏 35～36fps；再往上要动网络本身。

1. ~~C32 家族 FFN 并进注意力核~~（fast37 已做：隔离 −1.2 / 整帧 −1.9）。
2. ~~多头 26 块 FFN+proj0 合核~~（做了，零收益，代码留默认关）。
3. ~~C512 家族激活~~：fast39（FFWD→投影 0 E4M3 tile，−0.8）+ fast40（mapped + FP8 stream，−0.3，显存 −380MB）已做。剩下的小尾巴：注意力输出 result[2] 仍是 `[token][512]` E4M3（512B 步长），投影 1 的 A 直读；FFWD 每个 group 都重新 stage 一遍 16×512 输入（8 倍冗余读）。估 −0.1～0.2，先不动。
4. ~~post merge fold 4 通道 Load~~（fast41，−0.1；post 段 2.90 vs pre 2.81 基本持平了）。
5. dispatch 之间空转约 3.5ms：只能靠合核压（对带宽型段）。
6. ~~ViT expand+contract 合核 −0.3~~：09-11 试过，慢一倍，null（并行度不够）。~~升采样投影输出改 f16~~：fast43 已做（−0.15～−0.2，降频态粗量）。
6b. ~~指令级 profiler~~ 已打通（09-10 18:00）。待做：(a) C32 合核消溢出——换写法无效（20:10），要动结构（偏置表进 LDS / 减少同时在飞的 s[4] 累加器）；(b) ~~时钟~~ 是降频态，重启后 24.4，游戏里也回 36；(c) 让 Zero 用 RGP 界面看一次事件时间线，把 pipeline hash 和耗时对上（Zero 说等某个游戏从头到尾出问题再说，不急）。
7. 显存与波动：PoolSize 6000→5000 或贴图降档；C512 16 块 result 共享 200MB；shared ffn/raw 已 f16；decoder entry+split8 331MB 里 entry 的 f32 权重。
8. 网络约化/蒸馏（写作线，周级）：九块跳过版 36.5dB −2ms 等 Zero 看画面；蒸馏 = exact 链当老师、DLL 每帧 dump 当数据，难点是 71 块可反向传播 torch 模型 + FP8 假量化 + 算力。
9. 浪人崛起验收（上面「当前状态」）；通用化正路是冒充 NGX 的 `nvngx_dlss.dll`（OptiScaler 那条），周级，另一个项目。
10. 瞬态：进游戏颜色骤变、偶发黑帧（先记着）。

---

## 6. 工程教训

### 测量

- **延迟提交下分段时间戳前移**到下一个有硬同步的 stage，只能信整帧 total；同步模式也把 decoder39 入口权重不在本地的 9ms 记在 decoder_stage1。
- **紧跟大核后面的小段区间不可信**（顶端写戳，前一段尾巴算进来）：post70 rgb 头 0.77ms 是误判；旧 <0.2ms 分段数字全偏大。单核成本用 `DLSS5_TEST_ISOLATE` 重复 N 次取均值。
- 测试台基线一天内漂 0.5～1ms，空载 55 与游戏同时跑 63 是两种状态，只信同批 A/B；**GPU 会卡在降频态好几天（09-10：重启前 36、重启后 24.4），量之前先跑基线，不在 24 档先查时钟**；C32 encoder 段 A/A 也晃 ±1ms；<0.3ms 不当结论。
- 测帧率别开 Splashtop（抓屏让 cpu_frame 阶梯涨、GPU 不变）。掉帧先分 CPU 还是 GPU：GPU 满且 Shared Usage 非零 = 显存驱逐；GPU 不满 CPU 涨 = 提交端。
- 加了分段计时标签后旧标签只剩间隔，比较要求和（`tail*`、`c32_probe*`、`post70_*`），别把标签变小当加速。
- 每次 `+timestamp/readback/fflush` 的诊断有观察开销，不能拿 760ms 诊断 wall 否定 649ms 基线。

### 数值 / 舍入

- HLSL `round()` 是 RNE；位元 FP8 量化用 half-away 直接不 exact。E4M3 次正规尾数进位到 8 必须允许，大于最大有限值要显式 SATFINITE。
- 这台驱动/DXC 的 `f32tof16` / `float16_t` cast 是**朝零截断**（探针 2048 样本 1536 处对不上 RNE）；原版 CUDA FP16 输出 surface 也是截断。要 RNE 得自己位运算。
- **FMA 收缩上下文相关**：同一段 `g*(abs(g)*c1+c2)+c3` 或 `t + skip*scale` 在独立核里合成 FMA、搬进大核不合；`mad()` 也不保证。凡要逐位的标量尾链，两边都显式 `precise`（`DLSS5_BUILD_C32_PRECISE_CHAIN` 参考链）。
- 运行时 fxc（cs_5_1）编的 shader 里 `exp2/log2` 有近似，tie 会翻（88M 值里 15 个）；dxc 是干净 RNE。搬核时看 PSNR 别追逐位。
- HLSL 里 double 表达式在某些上下文得到全零或错误结果（post70 head、坐标缩放），改 precise float 或整数实现；"编译器漏加了 acc"这种断言没有证据不要下。
- 单层高相关不代表多层稳定：ViT QKV 两 pass 单层 exact，八层累计 corr 0.9936，必须做完整累计门。E4M3 让单层差异消失也一样。
- 半精度平方和用 float32 中间相加会吞掉中点旁的小项（block8 三值差），要 FastTwoSum 残差或 float64 承载。
- 编的和读的一样重：训练数据里没有的经验（"拔插一下"、"改 F6 看看"）不会自动跳出来拦你；所有比较句先外部核。

### GPU / D3D12

- 2 的幂行步长（4096/2048 字节）= 内存通道撞车，wave-matrix 16 行 tile 全落一条通道；改 512B/1KB 连续 tile。小矩阵（K≤128）tile 反而慢，先量再定。
- 合核只对带宽型段有效（C32 家族每块 280MB）；算力型段（多头 FFN+proj0、C512 FFN 120 TFLOPS）合核零收益。每 dispatch 空转在多头段量不出来。
- 这台卡每个核的成本几乎全在逐元素标量尾巴（Get/Set + 软件舍入 + 散写），不在带宽也不在矩阵乘；去掉一个 Ffast 就是 30%。收尾里的 log2/exp2、逐位 E4M3 编码、整数对齐仿真在 2.2M token 上是 0.5ms 量级。
- LDS 打包 8 位矩阵 Load 的 stride/StartIdx 单位是打包后的 uint 不是元素（写成 32 得 34.8dB 的"半坏"结果）。
- root 描述符槽位有核偷读上一次的绑定：小 buffer 绑到 t2 就 DEVICE_HUNG；新核换绑定要看后续 dispatch；宁可把小 buffer 放 t0。`Create` 里 flag 解析必须在 pso 加载之前。
- **建视图的格式变量别复用**：codec 改输出描述后拿同一个 desc 建输入 SRV，UNKNOWN 格式直接 device removed（887a0001），查了七轮。每个输入按自己的 desc。
- 单 command list 塞整网 / 整阶段合并 DEVICE_HUNG（TDR）；640-token ViT 要 chunk 级独立提交+fence。同列表拆 dispatch 无效。
- D3D12 单轴 group 上限 65535：超过要 2D dispatch 展平，否则静默只写前 131,072 像素（旧 predown 截断到第 68 行以下全是未写区，SHA `4868b7e4…` 因此不再是 golden）。
- 权重放 UPLOAD 堆每帧过 PCIe；decoder39 权重落系统内存时 4.5/13.9ms 双稳态——全部权重驻 DEFAULT。
- 显存 13.3（游戏）+ 4.8（网络）> 16GB 时 Windows 把我们的大 buffer 挤到系统内存，帧率随被挤量单调下降；`SetResidencyPriority` 不管用，只能收网络显存 + 用户 `PoolSize`。
- 所有 C32 段共用 SharedRaw scratch，"后面再读前面的 raw"要私有。
- 代理 device（ReShade/FFX）与 native device 不是同一实例，LUID 相同；DirectML 只能建在 native device；用 `IID_UnwrappedObject {7F2C9A11-…}` 拿 native list；共享堆 alias 跨 device 零拷贝。
- ReShade immediate list 若 `_has_commands=false` 直接 return——用原生 API 录的命令可能根本没提交，要用 ReShade API 做一次 copy 设标记再 flush。
- DRED 在 721 预览返回 UNSUPPORTED；调试层要在 `SetSDKVersion` 之后取。

### 逆向 / 验证纪律

- kernel 存在只证明编译进了 DLL，不证明当前 preset 调用它；权重 block 是序列化分组不等于层。
- `strings`/容量闭合只证明张量边界，不证明矩阵是 row-major；tensor-core tile 排列要 basis/地址位探针。
- 历史同名中间文件不能跨 runner 版本作裁判（frame 编号只在单进程唯一）；两台 GPU 各自原版对候选 exact，不等于跨卡逐位一致。
- 单点候选（"精确 double 求和→float32→half"）能修一个反例、全幅反而更差——不要用单点匹配推广舍入规则。
- 受控 impulse 被量化吃掉不是结构秩；iid FP8 随机 byte 把 attention 推进饱和区，不代表真实分布。
- 5090 上每次 DEVICE_HUNG 后立即重启、只跑一个控制变量；WDDM 锁死过两次。
- 别把"运行返回正常/finite/重放一致"当数值通过；别把"pixels_changed"当画面通过；别把"DLL 已加载/角落 FPS"当补丁生效。

### 运维

- 游戏开着绝不换 DLL（`deploy_fast.ps1` 会拒绝；先 `tasklist | findstr SB-Win64`）；游戏开着不跑测试台（再挤 5GB）。
- 测试台目录里的 `*.f32` 是权重和输入，清 dump 只删 `gpu-network70*.f32 sharedraw/sharedffn/pre-*.f32 *.bin`，别 `del *.f32`（09-10 删过一次，从 assets robocopy 回来）。
- 清盘别删 `matrix-probe\matrix_probe.exe` + `D3D12\D3D12Core.dll`（源 `Development/d3d12_shader_model_probe.cpp`，重编 `-DDLSS5_AGILITY_PROBE -DDLSS5_AGILITY_VERSION=721`）。以后别每个实验克隆一份 0.9GB 资产目录，做完一刀删旧目录。
- 这些头文件全是单行函数：插 `//` 注释会吞掉后面的代码，只用 `/* */`。
- 新 runner 在链顶端编译时下层 runner 的编译环境变量还没设，要用 `DLSS5_BUILD_*`；顶层字面量替换没匹配上会静默漏 `-D`，生成后 grep（fast3 时核按单 tile 编译却按四分之一组数派发）。
- PowerShell 5 `Set-Content` 默认 UTF-16LE，写数字门文件要 `-Encoding Ascii`；launcher 只发 Steam IPC 时环境变量不继承，门只能走文件。
- AMD Install Manager 计划任务会覆盖预览驱动，已 Disable；矩阵配置启动前先过 SM6.10/tier 门禁，别把 E_INVALIDARG 当 shader 回归。
- Steam 云同步/"Report Problem"弹窗会让游戏卡在"正在启动"，不点覆盖存档；`GameUserSettings.ini` 的 AA=OFF 会让 FSR 不创建。
- 时间只信 UserPromptSubmit hook 的时间戳（09-08 夜和 09-09 18:25 的钟点都瞎写过）；每做完一刀给 Zero 一句进度。

### 文档间的矛盾与已纠正处（整理时发现）

- README 顶部和 worklog 前半有多处"移植完成 / 无网格 / 精确帧率 11.9838"，后来都被撤回；`CURRENT-STATE.md` 第 443 行写明"本页优先于 README 中旧里程碑"。以本文时间线为准。
- 权重描述三变：1.48 亿 FP8（08-31 前）→ 7384 万 FP16（08-31）→ packed E4M3 矩阵 + FP16/float32 向量（09-01 SASS）。
- 记录数 153 vs live 层 152（block70 两条记录一个 Layer）。
- 1080p 几何两套：DirectML 时代 front 960×544、ViT 18×30=540 token（"全网各轴减半"）；原生路线实机捕获 1920×1152 处理、ViT 20×32=640。后者才是真的。
- decoder 移位序列早期用 0/1/3/2…（历史脚本配置），09-06 实机解码为 0/3/1/2…，`native_runtime_shifts.h` 唯一。
- 计划里"时序两 pass 去 double 30ms"实测只有约 5ms；"每 dispatch 40µs"判断作废；"C32 注意力占用/组同步是瓶颈"两假设排除。
- worklog 第 2976 行以后是倒序（09-08 在前、09-06 在后），且 09-08「工作纪律」标题下挂着的是 09-07 夜的内容。
- CURRENT-STATE 里 fast37 整帧一处写 35.4、另一处同批 37.0（基线漂移），以同批 A/B 的 37.0→34.0 为准。
- `PLAN.md` 说 0.07 包 = fast36，但仓库只有 0.01～0.06 六个 tag——0.07 是发布包的版本号（`package-README.txt` 写 0.07），没打对应 tag。
- `native-runtime-contract.md` 的"每帧 fence 约 2.4 秒"是 09-06 exact 冷链时的数字，现已无意义。
- `local-patch-tool.md` 描述的"用户自备 DLL"路线 09-05 已取消。

---

光之朱雀，2026-09-10

---

## 2026-09-11～12：黑块修复、Magpie 发布与远程实测（原 context 补入）

以下是当时的发布及部署记录；“待上传”“待换 DLL”和候选方向仅描述当时，后续版本与实验结果见下文。

- **0.09 已打 tag（09-11 08:55，Zero 笔记本带走，改在 9070 机上 release-check 验）。黑块根因（09-11 09:50）= 硬件 E4M3 Cast 不饱和，超 ±448 出 NaN → 8×8 注意力窗口全 NaN → 头部 clamp 出 0**。离线重放套路：`D:\DLSSNR-Lab\logs\replay.ps1 -Folder <decout16 的拷贝> -Flags logs\replay-flags.txt`，input.f32 = 转储色 f16→f32；`DLSS5_TEST_EPILOGUE_MODE=8` 看头部累加器（NaN 在这儿现形）。修法 `NATIVE_C32_SAT_CAST`（bench 默认开），只改两个 cso，参考 fixture 逐位不变。Magpie 包的 assets 已换新 cso（未换游戏 assets 和网盘包，等 0.10）。遗留：仓库另外 40 处硬件 E4M3 Cast 同类隐患，逐处加饱和是下一刀。
- **09-11 下午（周末远程）**：tag 0.10（黑块修 + E4M3 Cast 全审计）、tag 0.11（Magpie 接管 22→3.4 秒：预读/合批/磁盘 shader 缓存）。网盘包 `D:\DLSSNR-Lab\Magpie-DLSS5-AMD-0.11.zip`（sha256 8BFC9EA7…）待 Zero 上传。**游戏侧 add-on 仍是 0.10 的**（游戏整个周末开着，回来关了再换 + release-check 的 add-on 行）。**C32 "溢出"作废**：09-10 认的 p0087 是从不派发的 `full_attention` PSO，真正的融合核 p0006 无 scratch、VGPR 112；RGP 里的 ELF 按占位哈希缓存会拿旧货，抓前用 `Development/tools/dxilhash.py` 签名 cso。GPU 被游戏占着，所有计时活等周一：p0006 占用率（≤96 VGPR 升 5 wave）、C512 尾巴、九块跳过、游戏与网络重叠。网友 2K 屏问题（Magpie 放大到 1440p → 插件只旁观）要"网络先跑再让 FSR 放大"的结构改动，钩子现在是 FSR 跑完再替换输出，需和 Zero 定方案。今天四次 TDR 里两次是我合批第一版的 bug（释放了拷贝目标），两次是机器状态，已重启。
- **09-11 18:20 压缩前状态**：跳块维持 {42,43,46}（九块版 Zero 试了 37～38fps 看不出差别，但 5% 换 3.5 倍误差能量不划算，不做默认）；`DLSS5_STRENGTH=<structure>,<tone>` 已加（输出合成的两个混合系数 = NVIDIA 面板的强度；风格预设只有 Natural，其他要回 5090 重新捕获，不做）。Magpie 包里的 add-on 是最新的（prefetch/合批/STRENGTH），游戏里的还是 0.10。下一步清单见 DevHistory 09-11 18:20：① Magpie 每帧 ~10ms 非网络开销（光流/捕获/呈现）量了砍；② 游戏与网络重叠；③ 换游戏侧 add-on（等 Zero 关游戏）；④ p0006 占用率、C512 尾巴；⑤ 2K 屏方案 Zero 拍板。远程测试套路：改 flags → Zero 远程重开游戏/Magpie 看帧率；schtasks dlss5game/dlss5magpie/dlss5toggle/dlss5rgp；`logs\replay.ps1` 离线重放；`Development/tools/dxilhash.py` 签 cso 后再抓 RGP。
- **09-11 19:15（压缩后续）**：游戏 18:03 Zero 自己关的（无崩溃），GPU 空出来后：① 游戏侧 add-on 已换成当前源码构建（备份 `.0.10`），release-check 绿；② 清单 1 结论：Magpie 自身开销约 2ms（无游戏时动画窗口当源：24.1ms/帧，网络 GPU 21.5），游戏里多的 10ms 是游戏渲染抢 GPU，Magpie 侧无可砍；③ 清单 2 做了是 null：`DLSS5_OVERLAP=1/2`（网络放自己的 COMPUTE 队列、落后一帧）主菜单 32.7→50ms，计算队列上网络被拖到 45.8ms，串行最快，代码留着默认关。游戏目录 / Magpie 包里的 add-on 现在是含 overlap 代码的 HEAD 构建（flag 关 = 同行为）。远程工具新增：`logs\anim.ps1`+`dlss5anim`/`dlss5toggleanim` 任务（无游戏时给 Magpie 一个 1080p 动画源）、`logs\runC.ps1`（Magpie 一键跑）、`logs\gameRun.ps1`（起游戏到主菜单量 every_frame 后杀掉）、`logs\quiet-bench.ps1`（安静 bench + 分段汇总）。游戏 flag 文件里仍留着 BLACK_PROBE/DEBUG_DUMPS（收口时去掉）。剩下的杠杆只有网络核本身（融合 C32 约 13ms 最大）。
- **09-11 20:50**：清单 4a（C32 融合核占用率）做了是 null（`NATIVE_C32_LDS_SLIM` 逐位相同、无收益、默认关）；隔离单核账见 DevHistory 20:45——编码器 C32 块 1～4 在 960×576，pre/post70/tail67-69 才是全分辨率，各核按 token 成本一致，没有异常核可砍。测试台把 post base/历史/运动放 UPLOAD 堆（走 PCIe），bench 数只作同批 A/B，绝对值以游戏探针为准（网络 21.5ms）。剩下的都是结构性选择（更小网络/跳块/分辨率/2K 方案），等 Zero 拍板。decout16-sat 的 cso 已还原为基线（0C10F1D6），hlsl 与仓库 HEAD 一致。
- **09-12 02:45 tag 0.12**：屏幕提示（`native_text_overlay.h/.hlsl`，`DLSS5_NOTICE=0` 关）——输入不是 1080p / 初始化中 / 初始化失败三种文案写进画面。教训：宿主贴图不能建 UAV（Magpie 里 D3D12Core 崩），只能自己缓冲 + CopyTextureRegion，且用自己的命令列表在游戏批次后提交。整包 `D:\DLSSNR-Lab\Magpie-DLSS5-AMD-0.12.zip`（342MB，sha256 D8A884EC…）待 Zero 决定是否上传（0.11 教程刚发，网盘是 0.11）。游戏目录 / Magpie 包 add-on 都是 0.12 构建（5C433F12…）；release-check 绿。0.11 教程文章在 `wechat/dlss5-amd-0.11-安装教程.md`（非正式期数）。
- **09-12 08:11 网盘链接**：0.11 https://pan.quark.cn/s/570436349755 ；0.12 https://pan.quark.cn/s/a5bafe0e2050 （sha256 160DBD31…，含 Alt+Shift+A 热键修正；Magpie 热键是 Alt+Shift+A，不是 Win+Shift+A）。
- **09-12 17:15 tag 0.13（重打）**：`DLSS5_SHOW_FPS=1`（Magpie 包默认开，左上角网络帧率）；钩子按声明状态接管；**Magpie 效果组挂了 XeSS FG ZeroMV（跨厂商）：9070 XT 网络 28 帧 → 显示 55 帧**（Magpie 效能分析器"帧率 总/真实"）。今天的坑：**Windows Update 11:48 把预览驱动 32.0.31007.2048 换成正式版 32.0.31044.16 → 所有 SM6.10 PSO E_INVALIDARG（屏幕 INIT FAILED）**；重装 26.10.07.02（安装包重新下到 `D:\DLSSNR-Lab\drivers\`，sha256 cca61a1d…），已设 `ExcludeWUDriversInQualityUpdate=1`。Zero 本机 Magpie 配置（LocalAppData）已含 FG，备份 `config.json.pre-fg.bak`。远程工具：`dlss5toolbar` / `dlss5profiler` 任务（WM_HOTKEY id 2/3）看 Magpie 工具栏/效能分析器。decout16-sat 的 cso 现在是 dxilhash 签名版（`unsigned\` 备份）。
- 0.13 网盘：https://pan.quark.cn/s/7097bd16dc10 （sha256 9104C48D…，含 FG）

- **09-12 18:08 FPS 优化部署**：Zero 退出 Magpie 后，已将 commit `529a0e7` 构建装到 `D:\Magpie-DLSS5\Magpie-Experimental-x64\Magpie-Experimental-x64\dlss5-amd.addon64`；SHA256 `B4AA401DE728288033A27B586E31DC0B1BB3EF0EE84385CB1CF894F611C49B8F` 与本地一致。旧 DLL 备份同目录 `dlss5-amd.addon64.before-fps-529a0e7`；包内 `DLSS5_SHOW_FPS=1`。数字至少隔三秒更新、字条缓存、叠字并入输出提交。尚待 Zero 重开后验画面和收益；游戏目录及发布 zip 本次未换。

- **09-12 Magpie 0.14 包（闇）**：`D:\DLSSNR-Lab\Magpie-DLSS5-AMD-0.14.zip`（358004639 字节，SHA256 `14CCDE3C752B40821CB9F30024579304627A2499E087E06E9AEC399BFE734EED`），FPS 三秒刷新/缓存/合并提交，保留 XeSS FG ZeroMV，671 个包内文件哈希通过。校验文件同路径加 `.sha256`；待 Zero 上传，未打 tag。

- **09-12 18:25**：Zero 已上传 0.14，夸克链接 https://pan.quark.cn/s/8bbc3033181d ，中英文 README 下载入口已更新。

- **09-12 19:35 C512 FFWD 换 FP8 矩阵：null，项目收尾**。权重全在 E4M3 格点、A 输入本就是 E4M3 → 换 FP8 wave matrix 逐位相同，但 8 块 C512 整体 0.852→0.880ms（同批 min）、隔离法重合——这个核是延迟/暂存绑着，不是矩阵吞吐。留 `DLSS5_SPLIT_FFWD_FP8`（默认关）+ `DLSS5_BUILD_SPLIT_FFWD_FP8`（cso 不进发布）。Zero 定：做完这条项目就算结束。之后只剩三件事之一触发：SM6.10 转正 / 网友多到值得蒸馏 / 想动 Vulkan。remote: `logs\ffwd8.ps1`；decout16-sat exe 含 flag（备份 `.pre-ffwd8`）。

## 2026-09-13：小窗口适配 + Magpie 后接 FSR4（闇）

Zero 在《鬼武者》菜单选 1080p 窗口，捕获实际为 1914×1063。现场原链是 FSR3 1914×1063→3840×2133、FSR4再到7680×4266，所以 DLSS5 `armed=0 ran=0`。Zero 定：宽≤1920、高≤1080的窗口都应能输入，不要求用户精确调整客户区。

- 新开关 `DLSS5_FIT_INPUT=1`，默认关闭；本机 Magpie 已开启。encode/decode 内完成保比例双线性缩放、居中黑边与输出逆映射，网络仍用1920×1080有效区/1920×1152计算区。1914×1063→1920×1066，上下7像素；输出还原为1914×1063。运动坐标和位移同步按视口变换，补边运动为零。奇数宽度raw输出按256字节行距写/拷，读回检查也用实际尺寸；尺寸变化重建会话，释放前等待GPU；单像素运动图关闭时序。原1080p保留原shader分支。
- Magpie两站设置：FSR3 `scalingType=0 scale=1,1`（相对输入），FSR4 `scalingType=1 scale=1,1`（适屏），保留原XeSS FG参数。Magpie会省略保存默认值字段，部署脚本用Add-Member处理，不能直接假设scalingType存在。
- 每站各有自己的D3D12提交，后接Signal→D3D11 Wait→CopyResource；第一站after-submit补跑网络能交给第二站，无需改Magpie本体。第一FFX context负责网络和尺寸提示，销毁后才重新选，后续FSR4不再覆盖motion/计数/提示。销毁钩子完整转发allocationCallbacks；固定loader模块生命周期。跟踪dispatch后同列表同资源的barrier，Magpie输出提交时是COMMON，不是dispatch时声明的UAV。
- 验证：CPU穷举2,073,600合法尺寸；生产D3DCompiler编译36组合；9070真实codec回读五种尺寸（1914×1063/1280×720/1440×1080/641×479/1920×1080）全过，强度0,0时原图逐字节一致，黑边与行padding哨兵正确。测试文件在`Development/tests/`。
- 整链现场：合成动画源受Windows150%DPI影响，初次误变2871×1595；测试窗口修正后WGC实际1914×1064，FSR3保持该尺寸，FSR4输出3840×2135（4K画布内保比例）。网络初始化、连续处理通过；早期候选运行约3000帧，约35ms/帧。最终候选同进程停开再初始化成功，重开后300帧34.5～34.8ms。是无游戏的合成源结果，不是《鬼武者》帧率/画质验收。
- 初期超限窗口的双FSR提示路径出现两次进程访问异常（网络未启动）；最终候选将超限提示也绑定第一context并固定loader生命周期。未把早期异常直接归因成单一已证实根因。
- 已部署Magpie add-on SHA256 `6FB89C030CEC9AC62731D616494A24E50D68364533E5CD21B92A6AF28D8A313E`，三个运行时hlsl同步更新，配置改为上述链。备份`D:\DLSSNR-Lab\fit-input\before-fit-input\`；`Development/tools/deploy-fit-input.ps1 -Action Restore`回退DLL/shader/flags/config，要求Magpie退出。游戏目录/发布zip/tag未动。
- 开发说明`Development/fit-input.md`，候选`release/fit-input/`及远端同名目录；真实游戏的运动画质由Zero继续看。

### 2026-09-13 21:07：Zero《鬼武者》实机反馈

Zero 亲测：游戏设为1080p窗口、中画质，DLSS5正常生效，随后放大到2K，保持30帧。小窗口适配与后接放大已获得真实游戏验证。30帧按用户现场反馈记录，未另行区分帧生成前后读数。

### 2026-09-13：0.15 Magpie 发布包

用户要求升版打包。Magpie默认开启`DLSS5_FIT_INPUT=1`，发布说明放宽到≤1920×1080普通窗口。读取Zero实测后配置发现FSR4已设为Fill（`scalingType=3`，充满屏幕），包按这份实测配置：FSR3原尺寸→DLSS5→FSR4充满屏幕→XeSS FG，FSR4/FG光流参数保留当前值；不是早先合成测试的Fit1。便携包保留0.14基底，仅替换该效果组、已验DLL及3个shader、运行flags与说明。

- `scripts/release-check.sh`通过：全部编译cso对游戏资产一致、game flags一致；重新交叉构建通过。游戏侧DLL仍是之前版本，hash行仅作信息；Magpie包DLL取Zero刚验过的`6FB89C03…`，与运行安装一致。无网络核/runner变化，bench编译清单保持现有有效版本。
- `Development/tools/package-magpie-fps.ps1`沿用同一文件，升级为可指定版本的整包脚本；检查基础Magpie/FSR4/FG运行库与实测安装哈希相同、3个shader与源码相同、参数无诊断开关。新添`DLSS5-AMD-VERSION.txt`供版本识别。
- 整包`D:\DLSSNR-Lab\Magpie-DLSS5-AMD-0.15.zip`，358,010,364字节，SHA256 `af9a03192b9c7c816250997f7778e40c79e653535f809ae5976282811a8bd2f6`；旁置`.zip.sha256`。672个文件逐个解压计算hash通过，检查无多余未列文件。
- 中英文README与包说明同步更新；0.15网盘链接待Zero上传后补，0.14历史链接仍在对应版本行。版本标签`0.15`对应本次发布。

- **2026-09-13 21:16**：Zero已上传0.15整包，夸克链接 https://pan.quark.cn/s/e474fe2061c9 。中英文README下载入口已补齐，包及校验值不变。

### 2026-09-13 22:02：0.15同版本重打包（光流预设修正）

Zero实测FSR4和XeSS插帧额外打开AMD光流反而发糊，关闭后更清晰，要求DLL和版本不变、重打包。最终预设`opticalFlowMethod`按FSR3/FSR4/XeSS FG依次为1/0/0；第一项DLSS5保留AMDOF，后两项None。包内README和仓库中英文说明已说明该设置。

同名新包`D:\DLSSNR-Lab\Magpie-DLSS5-AMD-0.15.zip`，358,010,545字节，SHA256 `9bb7a021d09d987986f96dbd920589018606c9800dbd08e007f4b309388e5909`，旁置`.zip.sha256`已刷新。672个文件逐个解压校验通过；与初包清单对比，仅`config/config.json`与`README.txt`变化（以及校验清单自身），DLL仍`6FB89C03…`、全部shader/权重一致，无需重编。旧zip及清单留在`D:\DLSSNR-Lab\release-0.15\previous-package\`供回退。tag0.15保持不动。当前README下载链接和初包hash仍对应已上传的初包，新包待Zero重新上传后更新链接/校验值。

- **2026-09-13 22:07**：Zero已上传0.15光流预设修正版，新链接 https://pan.quark.cn/s/1601ca8f80ae 。中英文README版本号链接、大小与SHA256已切换到重打包版本（358,010,545字节，`9bb7a021…`）。

## 2026-09-13：720p独立开发分支

Zero要求直接测试真正720p内部计算，并明确独立git分支、开发不影响0.15。已从85feab0创建`720p`；全链改为1280×768处理、240真实ViT token，默认仍保留1080。GPU独立测试60帧：720平均10.934ms/91.46fps，1080平均22.010ms/45.43fps，约2倍吞吐。初测发现m1 ViT expand缺tile布局导致暗图，修复后才采用上述结果。历史全float有限、输出梯度/棋盘恢复。正式安装和main未动，真实游戏画质/帧率未验。复现、范围与坑详见`Development/720p/README.md`。

- **2026-09-13 23:13**：Zero要求720p安装到Magpie；已停止Magpie，备份并替换DLL+35个配套shader，hash核对通过，开启`DLSS5_NETWORK_720P=1`，重启待实机。原版备份`D:\DLSSNR-Lab\network-720p\before-magpie-720p\`，回退`deploy-magpie.ps1 -Action Restore -StopMagpie`。仅本机Magpie测试安装改变，main/0.15发布包/tag不变。

## 2026-09-14：900P分支与部署

Zero于09-13 23:54反馈720游戏链（游戏内FSR→DLSS5→2K→XeSS FG）仍约60/30帧、GPU95%以上，稍稳，要求试900。日志确认720内部计算已生效；未把测试台成绩当游戏帧率。用户外层已切900P，子仓库从dba82fd另建900P。新增`DLSS5_NETWORK_HEIGHT=900`，有效1600×900/处理1600×1024、400token；50×32 split/head及decoder映射已验证。独立测试60帧平均16.708ms/59.85fps，最短16.552ms，历史5,760,000个float全部有限，输出图正常。已把本机Magpie换900DLL `72F87A97…`及13个配套shader，设置900flag、重启等待实玩。备份network-900p/before-magpie-900p，回退同目录deploy.ps1 -Action Restore。main/0.15发布包/tag不变。细节Development/900p/README.md。

- **2026-09-14 0.15-900P包**：Zero实玩900p优于720p且较稳，1080p+FG不稳。按要求固定900打包，自适应暂不做。整包`D:\DLSSNR-Lab\Magpie-DLSS5-AMD-0.15-900P.zip`，358038890字节，SHA256 `718d77941674d6e851e7babc14b40a596da52e86aec603f44f956b99ff6811d5`，676个文件逐个解压hash通过；DLL和配套shader取已测安装，光流1/0/0。原0.15不改，待新链接。

- **2026-09-14 00:25**：Zero上传0.15-900P，链接 https://pan.quark.cn/s/a5339e4c8549 ，中英文README版本号入口已补。按Zero要求，900P分支移除README中0.14网盘链接，保留历史版本记录。

- **2026-09-14 07:42《剑星》内接入**：Zero要求把900DLL集成Steam游戏。游戏已退出，替换native-submission-order.addon64（72F87A97…）及D:\DLSSNR-Lab资产的40个配套shader，逐项hash通过；启用900/FIT_INPUT/线性codec/FPS，清掉旧转储、黑帧探针和占位参数，原生运动与时序保留。ffxDispatch/ffxDestroyContext导出齐全，SDK/驱动检查通过。备份network-900p/before-stellarblade-900p，回退deploy-stellarblade.ps1 -Action Restore。尚待实玩，Magpie/发布包未动。


### 2026-09-14：HIP 路线起点与后来修正（原 context 补入）

309 期写作时讨论 HIP 重写是否值得：预期收益来自寄存器/LDS 控制、搬运与核形状；最初判断提升有限，还误以为要丢掉整条 D3D12 游戏接入。实际实现保留 codec、时序与游戏钩子，只加 D3D12↔HIP 共享 buffer/fence 桥接，推翻了“必须丢掉接入链”的前提。09-15 研究过 jammm/SageAttention 的 jam/gfx12（commit `66f5e64c9e36084c863a4480e570069245e58f90`），只借鉴寄存器排列和片上重用，没有移植其 INT8/标准 softmax 算法；本网络的 64-token/head32、特殊 exp 和 rounding 契约仍须保留。

## 2026-09-14 22:57：HIP 分支第一阶段——完整网络与原生 WMMA 数值通过

Zero创建HIP分支，授权移植推理后端，目标是摆脱SM6.10预览版DirectX依赖。实现集中在`Development/HIP/`，尚未替换Magpie/《剑星》900P安装。

- **无SDK编译链**：动态调用Windows驱动自带`amd_comgr_3.dll`（Clang21），源码→LLVM BC→目标文件→gfx1201 HSACO；`-nogpuinc -nogpulib`，不安装HIP SDK。`hip_api.h`动态加载`amdhip64_7.dll`。当前9070XT仍为预览驱动32.0.31007.2048；HIP路线未启用实验D3D功能，正式驱动兼容性仍待单独实测。
- **GPU共享资源**：D3D12共享buffer与共享fence导入HIP成功，完成D3D写→HIP等待/读取→HIP写/发信号→D3D等待/回读；共享buffer上的原生FP8 WMMA输出256个16全部正确。HIP probe无Agility SDK/实验feature调用。
- **矩阵契约**：79组K32矩阵测试，HIP原生FP8 WMMA与HLSL FP8 CooperativeVector的K32、拆分K16、首K16结果各20224个float逐位一致。必须保留每K32从零积累后H(previous+dot)的舍入边界，不能把残差提前塞进WMMA累加器。模型的自定义指数、归一化树、half中点、移位/裁边按原实现移植。
- **完整参考图**：prefix、C32、多头64/128/256/512、split、ViT、decoder、post70均已HIP实现。512×512真实测试输入、seed0/postshift0，0–70块最终786432个RGB float与既有原版oracle逐位一致，非有限值0。SHA256 `bd52c601b68c4ed27f271cd2c7bcffc8511519f652534f2a0c51ffa9450e6da4`。block0/39/69中间结果也逐位一致。最初错误地用live postshift3导致最终差异；恢复本测试契约的shift0后完全一致。
- **WMMA接入**：C32的6个矩阵算子、多头FFN/QKV/score/AV/projection/pool及deep的8个矩阵算子改为gfx12原生FP8/F16 WMMA。C32全链、MH32/64/128/256/512逐阶段、deep真实block31/block23/decoder39/block66两套输入均bitdiff=0。完整512图开启`--wmma`后最终786432值仍与oracle逐位一致，SHA同上。MH额外导出的split_project未单独测，主图未使用它。
- **计时边界**：参考路径一次含dump约2.085秒；WMMA一次无dump约0.640秒。这是含冷权重上传、分配及逐kernel同步的整次验证耗时，条件不同，不能据此报加速比或游戏FPS。还没有实时内存复用、GPU整图计时或游戏接入。
- **复现入口**：`Development/HIP/README.md`与各ABI/toolchain文档；诊断输出在忽略目录`release/HIP/`及远端`D:\DLSSNR-Lab\hip-backend`。下一阶段处理缓冲复用/同步、720P与900P图、游戏D3D12桥接及性能验证。阶段性提交和DevHistory同步推进。

### 2026-09-14：HIP 缓冲复用、900P参考图与计时

新增单stream张量池：最后一个活跃tensor引用释放后可被后续kernel复用，池保存allocation直到Network销毁，依靠同stream执行顺序；权重仍驻留缓存，上传/dump/readback显式等待。`--pooled --repeat 3`的512输出仍与原版oracle哈希相同，热墙钟99.661/93.392ms（仍包含输入/噪声上传、输出回读及CPU工作）。

新增720/900几何与真实240/400 token fast ViT路径；实际验证900P：将512测试输入平铺到1600×1024，WMMA与HIP scalar的4915200输出float逐位一致、非有限值0，SHA256 `3ce76cb30277c73e847d9bbab0883fd02d58c37b3b12208f53c007db5420dc5c`。900 WMMA第二次墙钟576.159ms，尚不实时；这轮不是与900 HLSL生产图的比较。720/1080整图未测。

`--profile`按kernel汇总HIP events，冷启动曾返回负时长，必须丢弃该轮，代码显式标无效；热轮发现概率归一化、归一化与多头矩阵仍占主要时间。`build-modules.ps1`提供可复现的七模块编译入口。

失败实验：将软件H(x)替换为`float(_Float16(x))`，编译通过但512最终779563值不同、maxabs0.0656862；900最终4881246值不同、maxabs0.0703125。该改动已撤回，不能以有限输出或略快冒充正确。下一步保留精确舍入、改wave协作归约。

### 2026-09-14：HIP wave归一化优化通过

新增`wave_pointwise.hip`：C32与多头Q/K归一化、概率行由单线程串行改为wave32协作，ds_bpermute交换中间值，严格保留half平方中点、先相邻再4/2/1的归约树及概率奇偶分组顺序。256线程处理8行，host按row_count×32计dispatch；`--wave`显式开启，参考分支继续保留。

COMGR及host编译通过。完整512与原版oracle786432值逐位一致；完整900与HIP参考4915200值逐位一致，均非有限值0、hash保持上一阶段。单次热墙钟51269.829ms、900369.462ms（同样含上传/回读），明显改善但仍非实时。模块构建脚本更新为8个模块。正式安装仍未切换。

### 2026-09-14：HIP 硬件half舍入边界通过

追加定位：1258048组有限float（随机bit及half舍入边界），单独软件H、普通_Float16、bitcast half、内联v_cvt四种转换全部逐位一致。先前整网差异因此不能归咎于硬件转换本身；普通cast允许编译器在周围表达式上做不同优化。改为两个内联asm明确f32→f16→f32边界后，512与900完整输出均恢复逐位一致、hash不变。`HIP_ISA_HALF`宏/构建脚本`-IsaHalf`可选启用，软件参考仍保留。512热墙钟68.531ms，900343.748ms，输入/回读仍计入。下一步重点是WMMA输入打包和跨wave复用，并建立900 HLSL生产图独立oracle。

### 2026-09-14：HIP显存接口、完整D3D12桥接、共享矩阵tile

- `Network::SetNoise/Enqueue`：输入/历史/输出接收GPU指针，噪声一次驻留，ViT gather索引缓存；结果GPU→GPU拷入调用者缓冲，游戏调用路径无CPU像素回读。`device_network`三次连续512/900运行，最终hash与精确参考保持一致；热墙钟51248.889ms/900327.115ms（该轮还未启用tiled）。
- `D3D12Bridge`：同一HIP GPU与D3D12 adapter校验，共享输入/历史/输出buffer与fence，D3D拷输入→Signal→HIP Wait→完整网络→HIP Signal→D3D Wait→SRV输出。新增NativeGameSubmission只读Queue访问器用于校验同队列。独立`bridge_network`900连续三次通过，最终RGB hash `3ce76cb3…`，与HIP参考完全一致；热墙钟326.728ms。桥接未启用Agility/实验D3D功能，尚未接入运行游戏DLL；多HIP GPU暂不支持。
- C32 tiled（4个矩阵算子）和MH tiled（6个dense算子）使用LDS打包FP8后跨wave复用。C32 T64/80/256/4096，MH C32/64/128/256/512、M400与poolM100、N32及越界哨兵，全部bitdiff0/invalid0。小矩阵部分算子会变慢，不能把共享LDS等同必然加速。
- 开启ISA_HALF+WMMA+wave+tiled后，完整512和900输出hash仍不变；热墙钟51232.626ms、900148.116ms，均包含上传/回读。不与HLSL16.7ms基准混算。
- 新增`hlsl_network_oracle`（Agility721只用于HLSL对照），对同一900输入直接运行生产网络。最初PSO根签名缺t3：production prefix shader即使history off也声明t3，修成绑定base占位但Run temporal=false。成功输出4915200有限值，hash `ba7f9cd853ccd68886f789926a0c2b4479d0fc6003a097a3eeaff82ba023ea6a`；与HIP精确参考4882871值不同、maxabs0.0983276、RMSE0.01514156。
- 已核实生产FAST_PREFIX直接生成Gaussian，C32 fused FFN/FAST4以及MH fast路径省略多个中间H，与逐值参考精度日程不同。不能把HIP内部一致或512原版oracle一致写成900生产fast一致。正新增独立production-fast核，保留exact参考；阶段dump入口用于继续定位。

### 2026-09-14：HIP候选DLL与完整时序幀流水线

新增编译选项`build-addon.sh --hip`，保留既有`--tiled`生产HLSL构建。`NativeHipNetwork`接入NativeGameFrame，使用其同一DIRECT/COMPUTE队列、RGBA base与采样历史；输出共享buffer允许UAV，以支持后置平滑。HIP构建跳过SDK721/实验SM初始化。候选DLL SHA256 `8962d835bfc260e42ac7f18e1b1acf513a0c438a9c02b5fea046d844a1e300b8`；导入表无D3D12EnableExperimentalFeatures。原HLSL分支回归编译通过（源码条件块改变行号，构建hash不作为旧安装替换依据）。

独立benchmark以`DLSS5_USE_HIP`构建，取消测试EXE的Agility导出与experimental enable，模块路径设实验isa-half目录。900P完整encode→input/时序→HIP精确网络→history/neural/decode，3帧预热+2帧测量通过：GPU队列平均136.681ms、墙钟136.877ms（7.32fps，仅测试台），RGB25–227，历史5760000个float非有限值0。候选仍慢，尚未部署Magpie/《剑星》；正式驱动也未切换测试。快路径数学正在独立对照，不用这份精确后端冒充已完成实时移植。

### 2026-09-14：生产C32 FAST3/FAST4数值对齐

- 新增独立`c32_fast`：无中间H的expand与持续FP32累加contract，普通残差与map3三份E4M3对角矩阵残差分开导出。原FAST3 HLSL（不是重写公式的替身）三类输入：FP8、half、饱和边界。首次11904差异定位到最终显式f32tof16在D3D驱动走RTZ；修成RTZ后普通残差三pattern全0，随后map3三pattern也全0。真原始raw与F(main)只有F(input)一致且shift一致时才能互换map3输入。
- 新增独立`c32_fast_attention`，六阶段整体切换。原FAST4 HLSL逐段dump证明QKV/norm最初就逐位一致，首次分叉在exp的显式f32tof16：改RTZ后六段全部一致。再对齐production HW_H=1的projection RTZ，复测两pattern、六段全部bitdiff0/invalid0。矩阵Cast<F16>仍RNE，不能全模块粗暴改同一种half舍入。
- 前馈/attention验证器已独立命名`c32_fast_ffn_validate`与`c32_fast_attention_validate`，消除并行编写时临时文件撞名；旧冲突文件清掉。
- MH快前馈末尾RTZ也已GPU/CPU独立复核解释所有差异，C64/128/256六pattern全0；QKV+normalize真实生产CSO对照C64/128/256/512八pattern全0，剩余attention/projection继续独立推进。其模块尚未整链接入。
- 实验图仅先切C32快速数学（前缀和其他家族仍精确参考），900热墙钟149.565ms，混合精度图与生产RMSE0.0152379，未改善整体差异。不能将已验算子等同整网生产路径完成；还需prefix、MH、deep、边界与融合的完整对应。


## 2026-09-15：HIP生产快路径、融合与剩余整图差异

- MH快前馈、QKV归一化、attention三段、标量/三对角投影、RAW与C512 stream1raw/stream18、C32/64/128/256/512池化投影均已对真实生产CSO逐值通过。ViT64/400、attention、五种decoder、split FFWD及stream0f投影也通过。ViT激活外层需要FMA，早期6个正负零差由此修正；不能全改F(0)符号。
- C32 map3补齐真实CPU e4m3r的subnormal q≤7规则。原host普通nearest并不完全等同生产打包，先前block1验证不能覆盖其他scale。六个实际chain block共8通道分解变化，block4/67/68各一通道的三项和变化1/512；修正后block2/3/4/67/68/69各三pattern全通过。
- prefix直接数学模式的矩阵投影逐位通过，Gaussian存在小量sin/cos近似差：采样256点中a/b/c/d、log、sqrt、angles逐位相同，三角函数观察max3.80e-7、raw Gaussian max9.91e-7，跨half RTZ边界会放大。是近似选项，未宣称全输入误差上界或整网逐位一致。
- C32 packed attention与FFN+attention融合均逐值通过（LDS15KiB/19.25KiB）；MH三段融合四通道规格均通过。MF normalization由单线程148VGPR改wave协作9VGPR，保留0→31顺序求和；C64 T65536真生产对照全0，计时约0.91–0.96ms→0.17ms。无近似树归约。
- 修复张量池让常驻小权重占住巨大空闲buffer的问题：权重/索引常驻独立分配，900精确图持有10281.6→5268.4MiB，输出hash不变；快图融合后持有1910.0MiB。900快图热墙钟约70–72ms（含输入/回读），仍慢于现有HLSL约16.7ms测试台，不作为实时发布版。
- 补齐生产skip42/43/46与decoder48/55/61/65浮点输出路径额外Hrtz。各局部优化全图A/B保持相同输出；但HIP生产快图对现有HLSL整图仍RMSE约0.00927，尚未完成整体对齐。pre-main8仅86/52428800差；诊断replay注入HLSL pre和block4 main/down后仍约0.00921，说明剩余问题不能全归Gaussian。replay只用于定位、游戏Enqueue禁止observer，不是正常端到端验证。
- 首次replay诊断误用了未同步更新的boundary模块（host新传空down，旧kernel未判空），两次hipErrorLaunchFailure719；同步重编后正常，后续融合图成功。该失败不作为数值结果。
- HIP7持续通过；显式HIP6整图尝试进程exit5且无有效输出，不认为支持HIP6，也不做自动回退。当前仍预览驱动，正式驱动未实测。AMD官方HIP SDK7.2发布说明已列普通Adrenalin26.6.x/HIP7使用，见https://rocm.docs.amd.com/projects/install-on-windows/en/latest/about/releasenotes.html；官方部署说明与实机兼容验证仍需分开。

### 2026-09-15：快图接线与常驻分配阶段提交

完整保留reference路径，新增快族/融合/skip配置选择，模块编译脚本`-Fast`统一生成匹配代码对象并写source/code SHA清单，避免再次混用旧boundary。当前900快图全融合、wave normalization、skip42/43/46、decoder48/55/61/65额外half规则，离线热墙钟72.066ms，graph持有1910MiB；正常输出对HLSL RMSE0.00926926。replay精确pre后0.00923003、再替换block4后0.00920935，剩余问题在后续路径，不能只归因Gaussian。真实NativeC64Shift整块对照正在补，用来排查手选CSO没有覆盖的实际路径选择。HIP addon仍选择reference，当前运行安装未改。

### 2026-09-15：统一重建与真实整块验证

`build-modules.ps1 -IsaHalf -Fast`从独立源码快照重建20个模块全部成功，写modules.json。重建后的512 reference输出hash仍`bd52c601…`；900快图重建与之前输出hash同为`7b9591437302ea680c87684b3c690edd7fa76b56a1f7aca0c11773464512e29d`。HIP候选DLL重新构建成功，SHA256 `ff02886282b7b100cb1d7fe27b27b7e42208e33955e256df8b82a65e2beac9db`，未部署。

新增`whole_mh_block_validate.cpp`直接走NativeC64Shift、900flags、共享workspace与ResidentFlush，不手挑CSO。block5/shift0/rawfalse/inF32/outFP8在16×16及实际400×256两个尺寸、各两pattern，contract/FFN projection/normalize/AV/final五阶段全部bitdiff0。证明这组真实整块路径已对齐，尚不能替代真实图中各block输入的整网差异定位。

### 2026-09-15 06:07起：权重预打包优化

Zero授权开始优化。先完成初始化期lossless FP8权重打包：MH FFN/QKV/projection矩阵及ViT FP8 linear/split projection直接读取打包DWORD，取消每frame重复float→FP8转换。尺度/bias继续f32；矩阵起始byte offset维持原契约，第一版保留空闲padding，不虚报权重显存缩至1/4。F16专用矩阵不改。`--packed-weights`独立选项，旧路径保留。

CPU验证覆盖254个有限E4M3编码往返、不可精确表示/非有限输入拒绝、非矩阵字段不变、范围重叠检查；MinGW host和三种COMGR packed模块编译通过。ABBA四个独立进程，每进程6次，首轮cold排除，各方案10个hot样本：MH-only70.413→70.2215ms，未见明确收益；加ViT/split FP8矩阵后70.4165→65.217ms，耗时降低约7.38%。四轮全部RGB SHA256仍`7b9591437302ea680c87684b3c690edd7fa76b56a1f7aca0c11773464512e29d`。seed123+history输入额外对照也逐位一致。数字是900离线墙钟、含上传/回读，不是游戏FPS；原有HIP/HLSL整图差异没有被这次优化解决。

可复现脚本`Development/HIP/benchmark-packed-weights.ps1`，记录`release/HIP/packed-weight-benchmark.log`和`packed-weight-deep-benchmark.log`。源/模块统一由build-modules.ps1生成。当前正式DLL/驱动未部署变更。

### 2026-09-15：《剑星》HIP试用部署

Zero明确要求安装试用。先确认SB-Win64-Shipping退出。新增`DLSS5_HIP_FAST=1`游戏入口，启用已测fast/fusion/wave-normalize/packed-weights组合，保留0为reference；启动日志写runtime/fast/packed/geometry/modules便于识别。重编HIP DLL SHA256 `60f69f6f843486527afdf2f0052ee490ab061ff2669c285d7153f82141277131`，无D3D12EnableExperimentalFeatures导入。

同版NativeGameFrame完整900时序测试：3warm+2测量，GPU队列均值63.158ms、CPU墙钟63.239ms，历史5760000个float非有限值0。首次测试指定全局assets发现noise不在该目录（旧游戏使用lab下fallback），改为已验证、含noise的network-720p资产目录；游戏私有root采用junction指向这份完整资产集。

已替换Steam剑星Win64/native-submission-order.addon64，并创建游戏本地DLSS5-AMD配置、日志和23个匹配HSACO，逐文件hash校验通过。游戏私有flags为900/线性codec/原生时序/FPS，新增HIP_FAST1及私有HIP模块路径；continuous-every-frame与temporal-history开启，不复制旧PID请求或SDK721开关。全局D:\DLSSNR-Lab flags及Magpie安装未改，驱动未改。

原900P DLL `72F87A97…`保存在`D:\DLSSNR-Lab\hip-backend\stellarblade-hip\before-stellarblade`，hash已核。退出游戏后运行同目录上一级`restore-stellarblade.cmd`回退：恢复原DLL，并在C盘原位重命名HIP私有root使旧DLL重新使用全局lab；不递归删除资产junction。部署脚本Development/HIP/deploy-stellarblade.ps1。尚待Zero实际游戏画面/帧率反馈，快图与HLSL剩余差异仍如前述，不以该安装宣称正式驱动/生产数值验收。

### 2026-09-15 07:29：《剑星》HIP设备身份误判修复候选

Zero反馈init failed。实机日志显示HIP7/fast/packed和所有frame对象已创建成功，第一次render在`bridge input device mismatch`被拒，device removed reason=0。桥接InputContract错误使用owner==device指针相等；ReShade包装设备与原始资源设备的接口地址可不同。改为项目既有NativeSameDevice（经已验证unwrap接口取IUnknown身份），保留真正不同设备拒绝，未降为仅比较adapter LUID。

MinGW重编通过，native/proxy双向同一性、不同设备/错误/null拒绝及引用计数平衡测试通过。修正版DLL SHA256 `0202b4dc4ff94bb0a80300b3488b2b6e7ae942d0e4a58021c941d5924be40bba`，已暂存hip-backend/stellarblade-hip/dlss5-hip-identity-fix.addon64。诊断时游戏PID23984运行，先只暂存候选；复查确认游戏已退出后直接部署，安装hash已核。旧HIP DLL另存before-identity-fix.addon64，原900P HLSL回退副本72F87A97…保持可用。待Zero重开游戏实机复测；内核和模型参数未改。

### 2026-09-15：《剑星》HIP连续帧发黑：同步提交临时修复

Zero留游戏PID4132在装备菜单，实机截图确认人物变成黑色轮廓、背景异常。暂停游戏私有continuous-every-frame标记后原游戏画面恢复。F6模拟按键未见toggle日志，不算有效旁路测试。

通过临时改变窗口宽度16像素触发资源重建，并恢复原1280×720窗口，取得1296×720 RGBA16F真实输入/输出（request900001）。带读回等待、temporal=0的首帧人物正常，输入全部有限，最大34.53125；但temporal=0连续快路径仍发黑，排除“只关历史即可修复”的初始判断。随后保留temporal-history，游戏私有flags改DLSS5_TEST_ASYNC_SUBMIT=0，连续输出恢复正常；超过4700帧后再次截图人物仍正常，日志约68.6–68.9ms/帧，HUD约15FPS。证明同步提交可规避当前菜单发黑，尚未定位异步路径具体缺失的等待或资源生命周期；截图不能替代动态场景闪烁验收。

当前游戏继续运行：continuous和temporal均开启、async=0、DEBUG_DUMPS为空、窗口恢复原尺寸，无手动request。安装DLL仍0202b4dc…，内核/全局flags/驱动未改。部署脚本明确覆盖继承的async=1，防止下一次部署复现。此项为配置修复，无DLL重编。原flags副本留在游戏私有root的native-game-flags.before-capture。

新增capture-live-menu.ps1用于可逆窗口尺寸对照；benchmark_live_capture.cpp及说明提供真实HDR冻结输入的完整Frame回放，MinGW编译通过，未执行GPU回放（实机同步对照已经找到可用规避）。后续恢复异步前需验证连续输入与跨D3D12/HIP同步，原离线测试每轮上传/读回的CPU等待会掩盖这类问题。

### 2026-09-15：gfx12输入寄存器重用首轮候选

新增deep_fast.hip的HIP_VIT_EXPAND_PAIR编译开关：vit_expand每wave计算两个相邻16列输出，共用输入读取和FP8打包；保留各累加器K顺序、FMA激活和原launch ABI，奇数tile早退。默认关闭，build-modules.ps1可用-VitExpandPair生成独立候选。参考SageAttention gfx12的操作数重用思路，自行实现，未复制其量化或softmax算法。

COMGR3/gfx1201 baseline与pair均编译成功；benchmark_expand_pair.cpp在tokens16/400/1600、K1024/N4096、混合符号及E4M3小值输入上输出bitdiff0。ABBA批量墙钟：tokens400 baseline0.241/0.200ms，pair0.183/0.176ms；tokens1600 baseline0.809/0.806ms，pair0.723/0.705ms；tokens16反而较慢。局部结果不代表整网收益。

游戏已退出后执行benchmark-expand-pair.ps1，独立模块目录不动游戏：900完整快图packed+skip42/43/46，四轮各6次排除cold，两方案各10个hot样本。baseline中位65.2045ms、pair65.0745ms，仅约0.20%差异，未确认稳定整网收益；四轮最终RGB SHA256均7b9591437302ea680c87684b3c690edd7fa76b56a1f7aca0c11773464512e29d。保持可选，不启用默认、不部署DLL/内核。日志release/HIP/expand-pair-benchmark.log与expand-pair-network.log。

### 2026-09-15：串行热点诊断与C32硬件RTZ优化

新增--wall-profile：逐kernel执行前drain、执行后等待，用steady_clock累计调用墙钟和次数。包含host提交/等待且改变调度，不当成纯GPU时间或正常帧占比；设备游戏路径拒绝诊断。正常路径关闭时不调用计时钟。900热轮C32融合FFN+attention约17.388ms/10calls，ViT展开2.151ms/8calls。

共享内存连续8个FP8字节以memcpy整组读入寄存器（HIP_C32_LDS_VECTOR可选），编译与四轮整网hash通过，但65.267→65.307ms无收益，保持关闭。

C32融合Hrtz改用v_cvt_pkrtz_f16_f32后转回f32，保留软件路径HIP_C32_RTZ_ISA=0，默认1。COMGR/gfx1201编译成功，代码对象55640→37976字节；共享内存仍19712字节，VGPR166→183，实际收益来自减少指令而非寄存器下降。test_rtz.cpp+rtz_probe.hip覆盖所有有限half及邻接float、随机有限float共1186626个输入，half位差0。

完整900 packed快图，独立模块目录、ABBA四进程各6次排除cold，每方案10hot：seed0中位65.260→61.0745ms（约6.41%），四轮RGB SHA均7b959143…；seed123中位65.5015→61.2275ms（约6.52%），四轮SHA均75b62d2f…。均为正常非profile离线墙钟，非游戏FPS。测试期间游戏退出；安装DLL/HSACO未更换。日志release/HIP/wall-profile.log、c32-vector-network.log、c32-rtz-network.log、c32-rtz-seed123-network.log、rtz-probe.log。benchmark-module-swap.ps1保存每轮日志并检查hash，失败立即停止。

### 2026-09-15：原生FP8转换与C32权重预打包

C32 attention/boundary、deep、MH fast/padded/fused、C32 fused的F(float)新增原生FP8往返，保持输入±0归+0、按符号饱和与有限小值规则；HIP_NATIVE_FP8_F默认1，0保留旧公式。deep与boundary的Hrtz也默认原生RTZ（HIP_NATIVE_RTZ=0回归）。fp8_conversion_probe.hip对比旧F与硬件F，test_fp8_conversion.cpp在half网格/相邻float及随机有限float共1186626输入上bitdiff0。

六个候选模块COMGR/gfx1201编译通过；完整900 ABBA基线包含上轮C32 RTZ。仅转换改动：61.195→59.956ms，四轮SHA7b959143…一致。再加C32权重初始化预打包：60.002→58.9635ms，四轮SHA仍一致。FFN打包矩阵位于float offset512/4608各4096元素，attention前4096元素；scale/bias保持原位float。每个窗口不再重做权重量化。--packed-c32独立离线开关；HIP_FAST游戏入口启用，模块为c32_fused_ffn_attention-packed.hsaco，完整模块数24，构建/部署检查已同步。

合并两项seed123对照：61.1815→58.990ms，四轮SHA75b62d2f…一致；显式传入input900.rgba32f作为history的对照62.516→60.1915ms，同样一致（这组fixture最终hash与无history相同，不据此宣称动态时序画质验收）。各ABBA四轮6次、去cold，每方案10hot；均离线墙钟，不是游戏FPS。尝试amdgpu_waves_per_eu(4)生成指令与原版完全相同，VGPR158、private0、LDS19712，不保留该提示。

HIP DLL编译通过，release/HIP/native-conversion-packed.addon64 SHA256 888fe40b638f7ffbd8194a9e40ae6aa92fa32c21f01fec8a4597e32a69d2413e。游戏安装未替换。候选模块在远端hip-backend/conversion-modules；基线conversion-baseline含上一轮C32 RTZ。测试日志release/HIP/conversion-network.log、c32-packed-network.log、conversion-packed-seed123.log、conversion-packed-history.log、fp8-conversion-probe.log。

### 2026-09-15 15:52：MH归一化中间张量改为FP8字节

新增导出mh_qkv_normalize_fast_wave_fp8与mh_attention_fused_fp8：归一化保持原32通道顺序求和、尺度和量化结果，直接写FP8字节；融合attention直接读字节进共享内存，不再读取f32并重新打包。norm张量有效分配字节数降为原1/4，其他张量分配规则不变；池总容量不保证同比下降。保留原f32导出。--fp8-normalized显式要求fast_mh+mh_wave+fused_mh，否则初始化拒绝，避免生产者消费者ABI混用。

两模块COMGR/gfx1201编译成功。正常900离线ABBA四轮各6次、排除cold，各方案10hot：基线58.8955ms、byte55.3815ms，约5.97%减少；四轮最终SHA256均7b9591437302ea680c87684b3c690edd7fa76b56a1f7aca0c11773464512e29d。seed123加显式history=input900.rgba32f对照60.177→56.7405ms，四轮SHA均75b62d2f36b6861b1536ec06b087c3ddb8850f4cdf810e734e16e2bc0223c3f8。这组history fixture的hash与之前无history相同，仅作路径一致性测试，不宣称动态游戏时序验收。日志release/HIP/byte-normalized.log、byte-normalized-history.log。

HIP_FAST默认启用fp8_normalized，日志加状态字段。无需增加模块数，但必须重编含新导出的multihead-fast-padded-wave-packed与multihead_fused_attention。完整HIP DLL编译通过：release/HIP/native-byte-normalized.addon64，SHA256 9026e1ce4428c5ec95e7731dc44ddc8407646e42299a53fa021347fbb5f8cd46。候选模块在远端hip-backend/byte-modules；安装中的DLL与模块未替换。

### 2026-09-15 16:11：MH前馈隐藏层及AV输出字节化

按Zero指定完成两条边。MH C64/128/256 FFN expand输出FP8字节，contract直接以DWORD装入共享矩阵，省掉float搬运和再次量化。MH C64/128/256/512融合attention输出FP8字节，scalar/matrix projection直接读取；残差feature、raw QKV、累加器与最终投影输出保持原精度。fast_dense以ByteInput/ByteOutput模板编译不同ABI，旧导出保留。--fp8-ffn与--fp8-av独立控制，初始化检查所需fast MH/wave/normalized组合。HIP_FAST默认启用两者，日志增加字段。C512 split与ViT前馈为另一套内核，本轮未修改。

COMGR/gfx1201两个模块编译通过。900正常离线ABBA四轮各6次排除cold，每方案10hot，最终输出逐位一致：FFN单独55.2365→51.6275ms；AV单独55.2625→55.1255ms（幅度小，不确认稳定单项加速）；合并55.379→51.231ms（约7.49%）。四轮SHA均7b9591437302ea680c87684b3c690edd7fa76b56a1f7aca0c11773464512e29d。seed123、history=input900.rgba32f合并对照56.850→52.627ms，四轮SHA均75b62d2f36b6861b1536ec06b087c3ddb8850f4cdf810e734e16e2bc0223c3f8。该history fixture最终hash同既有无history，不替代动态时序验证。两条张量有效存储均减为1/4，但池常驻仍1910.1MiB/195 allocations，未宣称总显存下降。

日志release/HIP/edge-ffn.log、edge-av.log、edge-combined.log、edge-history.log；测试目录远端hip-backend/edge-modules和edge-*。完整HIP DLL编译通过：release/HIP/native-fp8-edges.addon64，SHA256 68c8ba0ca6293660bdab99a66c6eac71576133846dfcef0c700bb8818decd107。需配套含新导出的MH padded/attention模块，仍24模块。游戏安装未替换。

### 2026-09-15 16:19：《剑星》部署FP8中间张量优化版

Zero明确要求部署。确认SB-Win64-Shipping未运行后，用deploy-stellarblade-update.ps1将native-fp8-edges.addon64与edge-modules全部24个HSACO安装至Steam剑星Win64及私有DLSS5-AMD/HIP，DLL SHA68c8ba0c…、各内核hash逐个校验通过。保留900P/HIP_FAST、temporal-history、continuous，明确ASYNC_SUBMIT=0，避免恢复此前发黑的异步路径。原0202… DLL、23模块和flags备份到D:\DLSSNR-Lab\hip-backend\stellarblade-hip\before-fp8-edges；更新脚本-Restore可回退。原900P HLSL备份仍独立保留。等待用户实际游戏测试，离线51.23ms不是已测游戏FPS。

### 2026-09-15 16:28起：完整HDR计时、Graph验证及四项后续优化

读取用户试玩日志，已安装68c8…版最后几批滤镜耗时57.5/57.8ms，之前62.2ms；用户看到游戏14FPS，未把两者等同。游戏退出后，使用真实1296×720 RGBA16F菜单捕获跑完整NativeGameFrame。允许benchmark_live_capture保留flags中的GAME_PROBE；110帧全有限，最后热帧约53ms，前100帧probe的pre0.23/network59.69/post0.15ms包含cold。未以cold均值推断稳态。

新增可选DLSS5_HIP_GRAPH（默认关闭），按官方HIP7.1.1头签名动态加载capture/instantiate/replay API。只录Network内部kernel与最终copy，外部D3D/HIP信号留图外。首次warm填满权重/函数/池；相同input/history/output指针和seed才重放，key改变同步销毁旧图；Infer/SetNoise清缓存。capture期间若需新分配则拒绝并清理捕获，防止不支持操作；单图缓存不无限增长。真实HDR40帧ABBA去前5，各70hot：51.5255→51.0745ms，四轮最终SHA FEEA9EF3…一致；每图build1/replay38。24帧每8帧reset：每图build3/replay18，最终SHA22C171FC…一致，50.748→50.8465ms无收益。Graph不默认启用，也未修复live异步问题。

同时完成四项保持既有数值的存放/搬运优化，独立选项保留：
- fp8_deep：C512 split、ViT前馈隐藏层字节输出；split消费者精确解码到half做原F16矩阵，ViT消费者直接装FP8矩阵操作数。单项51.1015→50.668ms。
- fp8_middle：MH contract到projection中间值字节化，残差feature保持f32。单项51.1195→50.7525ms。
- half_c32：C32融合原始输出本来已Hrtz，改half存放，finish/head读取回float维持算术。单项51.0655→50.5125ms。
- crop_c32：合并C32 finish及随后的main裁切，直接写目标main；down保持原工作网格。post70原已跳过无用main/down，无重复优化。基于前三项49.3775→48.0835ms。

每项COMGR/gfx1201编译、正常900 ABBA各4轮6次、去cold，各方案10hot。四项最终合并对本轮初始配置：51.141→47.923ms（约6.29%），四轮RGB SHA7b959143…一致；seed123/history=input900.rgba32f：52.3935→49.394ms，四轮SHA75b62d2f…一致。日志release/HIP/deep-byte.log、middle-test.log、half-c32.log、crop-test.log、final-pipeline.log、final-history.log。HIP_FAST默认启用四项，初始化检查生产/消费ABI前提，旧导出和选项保留。

完整Frame基准新增HLSL比较构建DLSS5_COMPARE_HLSL，使用已有preview环境的进程级experimental开关。发现旧计时只Flush外层submission，async时未等Frame内部提交，HLSL异步0.488ms为无效CPU提交时间。修复为Frame后在同一queue排空提交并等其fence，再停止计时。固定真实HDR40帧、去前5：HLSL同步29.379ms、异步26.975ms，最终hash彼此一致；当时HIP同步49.761、Graph49.706，最终hash彼此一致。此条件不能套用旧16.7ms数字；HIP与HLSL之间仍有既有输出差异。compare-frame-backends.ps1可复测。

最终四项完整HDR重放40帧：热中位48.476ms，全部有限，最终SHA FEEA9EF3A8FCBF0692DCE7A506B5CA877F6DAEF7E942292F9B9D85A3523D0E58，与本轮改动前相同。含cold均值70.900ms不作稳态性能。最终HIP DLL编译通过：release/HIP/native-packed-pipeline.addon64，SHA9453575db8b7103382a4bb5d771cd1265976798f9b60f6521ac8cd625bc5a2fe；配套24模块在远端hip-backend/crop-modules。游戏安装仍68c8…/edge-modules，未覆盖。

### 2026-09-15 16:52：MH QKV投影与归一化融合

最新串行热点诊断筛出MH QKV与normalize，分别约5.2/5.9ms、各49次调用（含同步开销，不作正常帧占比）。新增fast_dense Normalize模板与mh_qkv_normalize_fused导出：64×64 FP32矩阵结果暂存共享内存，wave按原32通道readlane顺序求平方和，保留scale/rsqrt/F规则，直接写normalized FP8字节。省掉global raw QKV张量及独立normalize启动。独立--fused-qkv-norm要求fp8_normalized，原路径保留；HIP_FAST默认启用，启动日志增加字段。

COMGR/gfx1201及完整HIP DLL编译通过。900正常离线ABBA4轮各6次、去cold、每方案10hot：47.868→45.3685ms（约5.22%），四轮最终RGB SHA7b959143…一致。seed123/history=input900.rgba32f对照49.3665→46.890ms，四轮SHA75b62d2f…一致。真实1296×720 HDR完整Frame40帧，热中位45.317ms，全部有限，最终SHA FEEA9EF3A8FCBF0692DCE7A506B5CA877F6DAEF7E942292F9B9D85A3523D0E58与前版逐位一致。日志release/HIP/latest-profile.log、qkv-fused.log、qkv-history.log、qkv-hdr.log。

新DLL release/HIP/native-fused-qkv.addon64，SHA256 24b54e08d204469befe052c91bccb67e87125095ea3d1f1140a390b5e0987f1a。配套24模块在远端hip-backend/qkv-modules；MH padded模块新增导出，必须配套重建。游戏安装仍68c8…，未部署本轮。

### 2026-09-15 17:00：C32缓存及MH分块实验未采用

完成8组正常900 ABBA，每组四进程各6次、排除cold，每方案10hot；全部最终RGB SHA7b959143…逐位一致。C32 QKV输入寄存器缓存45.255→45.2555ms；全MH32行256线程tile45.568→46.129ms。单内核32行：QKV+norm45.4285→45.527，FFN expand45.337→45.728，contract45.4835→46.131，FFN project45.4655→45.476，attention matrix project45.534→45.3835（幅度小，不认定稳定收益）。C32残差scale三分量初始化预计算45.2945→45.346ms。

各候选COMGR/gfx1201编译通过、主机runner重编通过。没有发现值得替换默认的稳定加速，实验实现已从默认源码撤回；补丁和完整对照表留存Development/HIP/experiments/c32-cache-and-mh-tile32.patch及同名.md，补丁基于e847b44。实验日志release/HIP/c32-cache-test.log、tile32-test.log、tile32-k0..k4.log、scale-test.log。正式候选仍24b54e08…/qkv-modules约45.37ms，游戏安装仍68c8…，本轮未替换DLL/内核。

### 2026-09-15 17:19：HIP/HLSL同输入、实际尺寸层比较

完成compare_layers.cpp与compare-layers.ps1。相同逻辑输入分别提前上传GPU默认内存，真实权重/900层尺寸；涵盖C32 block1/4/post70（pack、mapped raster、raw-chain三种输入模式）、MH block5/6/9/15、Split block23、ViT block31/400tokens。两pattern，每案例预热20次，HLSL/HIP/HIP/HLSL四批每批20次，批结束等待，不含上传/初始化/读回。HLSL另记GPU timestamp，HIP细分使用当前调用位置重复kernel20次摊销等待、三轮平均，重复后核对完整HIP输出不变。FP8读回显式识别0x7f/0xff NaN；两pattern全部有限。

最终pattern1两批墙钟均值(ms)：C32 post70显式pack HLSL1.705/HIP3.320；HLSL mapped1.063/HIP3.408。block1 mapped0.309/0.817，block4 raw-chain0.291/0.878，C64 block5 0.314/0.702，C64 block6 shift3 0.323/0.739，C128 block9 0.226/0.436，C256 block15 0.210/0.318，C512 split23 0.145/0.285，ViT31 0.201/0.626。这些是代表层独立批量成本，不直接累加推断整网。

pattern1的C32、C64 block5、C512和ViT输出逐位相同；block6/9/15分别330/496/129元素不同（max0.5/2/2）。pattern0其他层也有少量差异，ViT7906/409600不同、max2，既有HIP/HLSL数值对齐尚未完成。最初block4用HIP三分量残差比HLSL普通模式，及先建小C32造成共享scratch不足的轮次已作废；正式测试按残差模式对齐且先建最大post70实例。

运行时确认HLSL MH fused_ffn=1、fused_shift=1、fused_qkv_norm=1、fp8_stream=1，而fused_proj0/attn_fused_qkv=0；HIP MH仍分别执行expand和contract。HLSL ViT expand/contract/projection权重tiled=1、contract SplitK=1、fused_ffn=0；HIP矩阵布局/分段执行与之不同。C32源码对照显示HLSL FFN矩阵输出留在寄存器并使用wave局部scratch，HIP有额外half共享数组往返及workgroup barrier；HLSL还可直接映射上游布局，HIP独立pack。代表post70 HIP批量诊断pack约0.72ms、核心约2.65ms，差距不只来自搬运。细分时间不与整层机械相加。

详细表与边界说明Development/HIP/layer-comparison.md。原始CSV/log在release/HIP/layers-p0/p1与layers-final-p0/p1系列。普通runner和诊断runner均MinGW编译通过；诊断friend与重复执行仅DLSS5_LAYER_BENCH下启用。默认推理算术和游戏安装未改。

### 2026-09-15 17:57：MH FFN融合、合作输入与按通道选权重布局

实现C64/128/256的16-token/group融合FFN，分别128/256/512线程；每wave四个expand累加器共用输入，hidden以FP8留LDS，然后按原K16顺序contract，输出F(Hrtz) byte middle。保留HIP既有激活及捨入，不借优化改变数值。初版45.2915→45.2645ms，无明确收益；合作输入打包一次并复用hidden空间后45.370→44.617ms。输入与hidden共享同一LDS数组，覆写前全组同步，不增加另一块共享内存。

补FFN两块权重的byte重排[N16][K32][K][N]，按新_tiled入口装载，投影矩阵和scale区域保持原位置/布局。全通道重排45.4845→43.7165ms。层测显示C64/C128原布局更快（融合core约0.094/0.087ms，重排约0.126/0.100ms），C256重排更快（约0.165→0.101ms）；最终C64/128融合但保留行排列，C256融合+重排。

最终正常900 ABBA四轮各6次、去cold，每方案10hot：45.384→43.340ms（约4.50%），四轮RGB SHA7b959143…一致；seed123/history=input900.rgba32f：46.747→44.8335ms，四轮SHA75b62d2f…一致。正常HDR40帧、去前5热中位43.527ms，全部有限，最终SHA FEEA9EF3A8FCBF0692DCE7A506B5CA877F6DAEF7E942292F9B9D85A3523D0E58与前版一致。另做普通与tiled融合的同输入HLSL层对照；这些步骤保持原HIP输出，不宣称消除既有HIP/HLSL数值差异。

--fused-mh-ffn独立控制；--tiled-mh-ffn为全通道重排参考，--tiled-mh-ffn-large为最终C256重排。HIP_FAST默认fused_mh_ffn/tiled_mh_ffn开启、tiled_ffn_min_c=256。合作输入默认开，HIP_FFN_COOP_INPUT=0保留首版实验。普通两步FFN仍可用。检查packed weights、byte middle与16-token组对齐，避免ABI误用。

COMGR/gfx1201内核、离线runner、层比较与完整DLL均编译通过。最终DLL release/HIP/native-fused-mh-ffn.addon64，SHA256 03269b74ce1f102ad89cb6a724e3a18e7c432fc5ada140145cef0c6a053d681a；配套24模块在远端hip-backend/ffn-tiled-modules。日志release/HIP/ffn-fused-test.log、ffn-coop-test.log、ffn-tiled-test.log、ffn-layer.log、ffn-final-validation.log、ffn-selected-test.log、ffn-selected-history.log、ffn-selected-hdr.log。游戏安装未替换，仍68c8…。

### 2026-09-15 18:24：C32局部同步、寄存器FFN及直接映射输入

审计QKV阶段：scratch.raw每wave独占16×33float行，packed写区按part/行分离，循环内无跨wave读。HIP_C32_LOCAL_QKV_SYNC将循环内6次全组barrier换为release/acquire内存fence，循环结束仍全组同步后才跨wave读取K/V；其他别名/跨wave阶段保持原barrier。首轮43.6805→43.348ms，输出一致。此项是kernel内部同步，不是开启游戏D3D/HIP异步。

HIP_C32_REGISTER_FFN把half FFN结果留h8寄存器用于末端残差，仅将量化的byte ff n8存共享数组给QKV，替代half共享数组和反复打包。基于local-sync基线43.388→43.077ms；未mapped的此变体LDS19712→17664字节、VGPR217、private spill0，寄存器增加，收益只按实测认定。

新增--mapped-c32及c32_fast_ffn_attention_fused_half_mapped：融合核按原窗口偏移读取raster，执行相同零填充，连同标量残差输入也映射，C32()跳过hip_c32_pack及全图buffer。前置preblock本来是tile布局，保持原入口。要求half/crop快速链，普通入口保留。基于register版本43.230→41.4535ms。

最终三项对本轮起点正常900 ABBA4轮各6次、排除cold：43.295→41.362ms（约4.46%），四轮SHA7b959143…一致；seed123/history=input900.rgba32f：44.6365→42.757ms，四轮SHA75b62d2f…一致。真实HDR独立40帧两次热中位41.454/41.533ms，最终均FEEA9EF3…；24帧每8帧重置history热中位41.550ms，最终22C171FC…，匹配既有相同重置测试。所有帧有限。validate-hdr.ps1统一执行此类验证。

两宏默认1，0保留旧路径；HIP_FAST默认mapped_c32=1。内核和DLL编译通过：release/HIP/native-c32-mapped.addon64，SHA125511f0d3dc205ef57c62d7e427d949a1bd064298b373427735244bf911c9c9，24模块在远端c32-mapped-modules。日志release/HIP/c32-sync-test.log、c32-register-test.log、c32-mapped-test.log、c32-final.log、c32-final-history.log、c32-hdr-a/b/reset.log。游戏安装未动，仍68c8…。

### 2026-09-15：ViT四输出分块通过，Split-K实验保持关闭

在已提交C32优化基线上继续对照HLSL的ViT组织。首先将4096维contract原四段K1024改为四组并行、独立combine按原p0→p3顺序加到H(skip*scale)，保留全部舍入与累加顺序。COMGR与整网逐值通过，但16×16小块Split-K 41.4745→42.8955ms更慢，未默认。

依据native_wave_vit_blocked.hlsl改为单wave同时输出16×64、四acc共用输入，实际dispatch数为原1/4；不是早期pair实验中保留原grid让奇数块空返回。expand单项41.5155→40.754ms。contract同样四输出片段，原四K段串行时40.5465→39.983ms；配合Split-K时40.487→41.7755ms，仍慢。最终采用expand/contract分块，不启用Split-K，权重仍原packed行布局。

最终ViT两项正常900 ABBA四轮各6次、排除cold，各方案10hot：41.3815→39.925ms（约3.52%），四轮RGB SHA7b959143…一致；seed123/history=input900.rgba32f：42.7345→41.4075ms，四轮SHA75b62d2f…一致。真实HDR40帧热中位40.300ms，最终FEEA9EF3…；24帧每8帧重置历史热中位39.172ms，最终22C171FC…，都匹配旧hash且全有限。均为离线/冻结帧数据，不是实测游戏FPS。

--vit-blocked和--vit-contract-blocked独立控制、HIP_FAST默认开启；--vit-split-k保留实验，默认关闭。旧内核入口保留。主机runner、COMGR/gfx1201模块和完整DLL编译通过。最终DLL release/HIP/native-c32-vit-blocked.addon64，SHA4c0620a559a6a1ca6d633f5dcfb8b3b241d2a56116b7bee714917256859278f0；24模块在远端hip-backend/vit-contract-modules。日志release/HIP/vit-splitk-test.log、vit-blocked-test.log、vit-contract-serial.log、vit-contract-parallel.log、vit-final.log、vit-final-history.log、vit-final-hdr.log、vit-final-reset.log。游戏安装未动，仍68c8…。

### 2026-09-15 19:09：部署C32/ViT约40ms候选到《剑星》

Zero接受继续部署并优化。确认游戏退出后安装native-c32-vit-blocked.addon64（SHA4c0620a5…）和vit-contract-modules的24个HSACO，逐hash校验通过。旧68c8… DLL/模块/flags/候选标记备份before-c32-vit-blocked。保留900P、continuous/history和同步提交。更新脚本参数化候选/模块/备份/预期hash，回退也恢复候选标记。驱动/全局lab flags未改；待实际试玩。

### 2026-09-15 19:15：用户实机反馈

Zero实测新版900P约17FPS，回忆之前HLSL在游戏1080P设置约37FPS。当前PID26192启动日志确认最新4c062…功能组合、内部processing1600×1024，末批处理帧间隔43.9–51.6ms（含游戏调度，不是纯滤镜耗时）。旧HLSL备份flags也写NETWORK_HEIGHT=900，但尚未确认是否对应37FPS那次；不把游戏设置分辨率自动等同模型内部计算分辨率。用户报告原样保留，性能差距仍显著。随后确认游戏已退出，再继续离线测试。

### 2026-09-15：ViT展开权重重排与输入预打包

增加独立vit_weight_mask（bit0展开、bit1收缩）与vit_pack_input。展开/收缩权重以既有TilePackedMatrix做[N16][K32][K][N]纯byte重排，显式_tiled入口和独立cache键区分；scale位置不变。展开输入先用配对硬件转换打包FP8，再由_bytein入口直接读DWORD，保持原饱和和捨入。输入临时缓冲在展开后释放回池，残差原float输入保留。

正常900 ABBA四轮各6次、去cold：仅展开重排40.0255→39.649ms；仅收缩重排40.1465→40.447ms（更慢）；同时重排40.0885→40.0825ms（无明确收益）；仅输入打包39.9615→39.6115ms。最终只选展开重排+输入打包：40.108→39.563ms（约1.36%），seed123/history41.3445→40.724ms。每组四轮最终RGB hash分别保持7b959143…/75b62d2f…，没有数值变化。

完整HDR40帧热中位39.768ms，最终FEEA9EF3…；24帧每8帧历史重置39.514ms，最终22C171FC…，均匹配之前hash且全有限。原生runner/内核/完整DLL编译通过。HIP_FAST默认mask1、pack_input=1，收缩重排保持关闭。小幅离线收益不等于用户17FPS已追上旧37FPS。

候选DLL release/HIP/native-vit-layout.addon64，SHAf0460eb0b00ade0ab26c8d430b3f7203a6be08374321ce0a357c94806a9ee8ff，24模块在远端vit-layout-modules；尚未部署，游戏保持本轮已安装4c0620a5…约40ms版。日志release/HIP/vit-layout-expand/contract/both.log、vit-pack-input.log、vit-layout-combined.log、vit-layout-combined-history.log、vit-layout-hdr.log、vit-layout-reset.log。基准脚本新增轮次结束时的游戏进程检查，若游戏启动则丢弃受干扰计时。


### 2026-09-15：修复异步颜色／运动描述符重绑竞态

RebindSourceAfterCompletion原来只持有CPU锁，在deferred提交尚未完成时就原位重写SRV并释放旧resource；TemporalFeed轮换motion纹理也会改写仍在使用的SRV。现在仅在颜色或运动纹理指针变化时先Flush已有提交，overlap路径同时Flush计算队列，再重绑。纹理不变不增加等待，模型和内核计算未改。

新增queued_frame_probe.cpp、run-queued-frame.ps1和test-rebind-lifetime.ps1：12帧排队、逐帧独立快照、末尾统一读回，保留所有源资源以隔离描述符竞态。11组进程、7项比较全部通过：旧异步颜色轮换和motion轮换均偏离同步结果；修复后颜色、motion、两者同时轮换的全部帧分别匹配411DF44C…、F687BC96…、43712DDE…。固定同一texture但交替内容的旧同步／旧异步／新异步均匹配411DF44C…。旧异步错误hash随调度变化，不能写死错误hash。

独立真实HDR40帧同步／异步热中位39.375／38.833ms，最终均FEEA9EF3…且全有限；短轮离线计时不作为稳定提速或游戏FPS结论。validate-hdr.ps1增加Flags参数。更正此前日志解释：游戏avg_ms_per_frame是处理帧间隔，包含游戏调度，不能当纯推理耗时。

完整DLL编译通过：release/HIP/native-async-rebind.addon64，SHA256 8568acff121c6fab93b7b1f77d9df8c1c14549a6bee303a4e7ab35abd2bd09d7；配套沿用vit-layout-modules。未部署，游戏仍4c0620a5…及async=0。实机异步正确性、overlap性能未在本轮验证。复现说明见Development/HIP/rebind-lifetime.md，结果日志release/HIP/rebind-regression.log和rebind-hdr-sync/async.log。


### 2026-09-15 20:26：部署异步重绑修复到《剑星》
用户授权实机验证。部署脚本确认游戏退出后安装8568acff121c6fab93b7b1f77d9df8c1c14549a6bee303a4e7ab35abd2bd09d7 DLL及vit-layout-modules的24个HSACO，逐hash校验通过。游戏私有配置ASYNC_SUBMIT=1，NETWORK_HEIGHT=900、HIP_FAST=1，continuous/history保持。旧4c062… DLL、模块、配置和标记备份在D:\DLSSNR-Lab\hip-backend\stellarblade-hip\before-async-rebind。部署脚本新增AsyncSubmit参数，默认仍0；-Restore -BackupName before-async-rebind可恢复原同步安装。实际画面和FPS待用户启动游戏测试，本次没有启动游戏或宣称实机已通过。


### 2026-09-15 20:50：用户实测异步版约24FPS
Zero试玩8568acff…异步修复候选后报告约24FPS，上一同步版报告约17FPS。保留为实机观察，不把两次试玩当严格同场景基准；用户未逐项描述画面检查结果。本轮开始确认游戏已退出。

### 2026-09-15：颜色输入不可变描述符快取
NativeGameCodec最多保留8组完整输入绑定、shader-visible heap和resource引用。命中直接切换heap；未命中创建独立heap，不改写GPU使用中的描述符；快取满额才由NativeGameFrame等待完成后淘汰。编码器和解码器分别维护快取。运动纹理保持原重绑等待。未改模型计算、量化或HSACO。

queued_frame_probe新增rotate=2（12张不同颜色纹理），覆盖8组上限后的淘汰。test-binding-cache.ps1的颜色轮换、颜色+motion轮换、固定纹理变内容、12贴图淘汰、淘汰+motion五项均通过，全12帧分别匹配已有411DF44C…／43712DDE…同步hash，全部有限。随后两轮ABBA共8次、每次排除前2帧：旧版4次热均值的中位37.52715ms，快取版37.16005ms，约0.98%降低；所有输出hash一致。此为离线颜色轮换场景，不是实机FPS增幅。

完整DLL编译通过：release/HIP/native-binding-cache.addon64，SHA256 56a97612a43f2cc200a5e395d72fd2fdaab816ee3478b6dbb4a4105303f255ac。配套仍vit-layout-modules24模块。没有部署，游戏保持用户报告24FPS的8568acff…、async1。日志release/HIP/binding-cache.log。


### 2026-09-15：运动纹理不可变绑定快取
NativeTemporalFeed增加最多8组纹理/SRV heap引用快取，命中切换不可变heap；满额未命中才要求NativeGameFrame先Flush后淘汰。消除一般motion轮换的CPU等待，模型与HSACO保持。queued_frame_probe的temporal=3以12张运动纹理交替同样向量，覆盖淘汰。

test-motion-cache.ps1五项正确性测试通过（颜色、颜色+motion、变内容、颜色淘汰、颜色+motion同时淘汰），全部12帧匹配原同步411DF44C…/43712DDE…。8轮ABBA对比颜色快取版与双快取版，颜色+motion轮换每轮10热帧平均耗时的中位37.87255→37.60930ms，约0.70%降低。所有hash一致，离线小幅收益，不是实机FPS证明。

完整DLL编译通过：release/HIP/native-motion-cache.addon64，SHA7358aedb9a3be1d1bdae0acbfac06488ebda05cf717970ba5a90a303d1bd9d00，配套vit-layout-modules不变。未部署，游戏保持用户约24FPS的8568acff…异步修复版。日志release/HIP/motion-cache.log。


### 2026-09-15：刷新内核诊断与无位移复制消除实验
重新编译当前reference_current.exe（远端旧reference_network.exe不支持最新参数），profile-current.ps1以全部当前快速选项及vit-layout-modules执行900P逐核等待诊断。最终RGB保持7b959143…。热轮C32融合mapped9次合计9.013ms，非mapped前块1次2.993ms；MH QKV归一化6.729ms、shift pack4.228ms、crop3.234ms。逐核等待包含提交等待开销，改变调度，不能当正常39ms帧的组成百分比。旧latest-profile.log含已移除hip_c32_pack，不再用于当前热点判断。

新增实验开关--elide-identity-shift：Body中仅当sx=sy=0且工作宽高等于输入宽高时，pack直接引用input、crop直接引用attended，免去两次恒等复制，保留shared_ptr生命周期；默认关闭，尚未接入HIP_FAST。test-identity-shift.ps1正常900P ABBA4轮各6次，去cold各10样本，中位39.4005→39.2290ms；四轮最终RGB均7b959143…一致。日志release/HIP/current-profile.log和identity-shift.log。尚未完成其他seed/history及完整HDR验证，未构建/部署此实验DLL。


### 2026-09-15：恒等shift复制消除完成历史与HDR验证
--elide-identity-shift在seed123/history=input900.rgba32f的ABBA四轮各6次，排除cold后中位40.779→40.367ms，四轮最终RGB均75b62d2f…一致。完整真实HDR异步40帧热中位39.625ms，最终FEEA9EF3…；24帧每8帧重置历史38.606ms，最终22C171FC…；均全有限。HDR这里是正确性回归，没有同期旧版ABBA，不宣称HDR速度增幅。

已纳入HIP_FAST，启动日志增加elide_identity_shift状态。test-identity-shift.ps1支持seed/history/预期hash，validate-hdr.ps1支持指定runner以隔离实验二进制。完整DLL编译通过：release/HIP/native-identity-shift.addon64，SHA5ab7d7d37d5b8fdbf8248031cf073e389df8c358e262cddf096718b1113bc225，含颜色及motion快取，沿用vit-layout-modules。未部署，游戏仍8568acff…异步版（用户24FPS）。日志release/HIP/identity-history.log、identity-hdr.log、identity-reset.log。


### 2026-09-15：C32 FFN权重DWORD合作装载
C32融合核的FFN expand128×32和contract32×128权重从每线程逐byte复制改为每次4byte memcpy装入LDS。输入、padding、矩阵字节顺序、WMMA和捨入保持不变；仅packed weights路径生效。HIP_C32_WEIGHT_DWORD默认1，设0保留旧实现。没有复用此前失败的QKV寄存器缓存。

COMGR/gfx1201编译通过。当前完整快速链+identity-shift正常900 ABBA4轮各6次、排除cold：39.0205→35.941ms，约7.892%；seed123/history同条件40.355→37.334ms，约7.486%。每组四轮最终RGB分别全匹配7b959143…/75b62d2f…。真实HDR异步40帧35.179ms、最终FEEA9EF3…，24帧每8帧reset34.733ms、最终22C171FC…，均全有限；HDR未做同期ABBA，不据此单独计算收益。

主机DLL不变，使用已编译release/HIP/native-identity-shift.addon64（SHA5ab7d7d3…）；新packed C32 HSACO SHAD2E66FE7D94EBD67622DE37230C9B44DEC71C5E51EE248131FF3AA6C58D91F74。24模块固化于远端hip-backend/c32-dword-release-modules，实验脚本使用另一个c32-dword-modules目录，避免ABBA结束恢复baseline后污染候选。没有部署，游戏仍8568acff…异步版，用户24FPS。

测试脚本test-c32-dword.ps1；本轮编译输入是在c32_fused_ffn_attention.hip前定义HIP_ISA_HALF=1、HIP_PREPACKED_WEIGHTS=1、HIP_C32_WEIGHT_DWORD=1，调用D:\DLSSNR-Lab\rtc_compile.exe OUTPUT SOURCE comgr。日志release/HIP/c32-dword-build/test/history/hdr/reset.log。


### 2026-09-15：C32 QKV/输出投影直接8-byte权重读取
新增matrix8，packed权重路径直接memcpy8byte到WMMA的i2输入，替换QKV与输出投影的逐byte读取/位拼接；不改变矩阵字节、乘加顺序和舍入。HIP_C32_DIRECT_WEIGHT8默认1，0保留逐byte对照，非packed路径仍逐值转换。基线为上轮DWORD FFN装载，避免混记收益。

COMGR/gfx1201编译通过。正常900P ABBA4轮各6次、去cold中位36.1015→35.2145ms（约2.457%），seed123/history37.4245→36.583ms（约2.249%）；每组四轮最终RGB分别保持7b959143…／75b62d2f…。真实HDR异步40帧34.869ms/FEEA9EF3…、24帧每8帧reset34.746ms/22C171FC…，均全有限；HDR为正确性回归，不单独计算速度增幅。

新packed C32模块SHA2A885047BC050B06BD7188A66B7A7CE72AFEF77C45C40F003C1694E97E942DCD，24模块固定在远端hip-backend/c32-direct8-release-modules；test-c32-direct8.ps1只覆盖独立实验目录。配套主机DLL仍native-identity-shift.addon64/5ab7d7d3…，不需改DLL代码。未部署，游戏仍8568acff…异步版（用户报告24FPS）。日志release/HIP/c32-direct8-build/test/history/hdr/reset.log。编译输入定义HIP_ISA_HALF=1、HIP_PREPACKED_WEIGHTS=1、HIP_C32_DIRECT_WEIGHT8=1，其余沿用源码默认。


### 2026-09-15：C32输入DWORD写入；LDS整组读取仍关闭
先复查旧HIP_C32_LDS_VECTOR实验，在当前DWORD权重+direct8布局重新ABBA：35.465→35.422ms，输出一致但无明确收益，仍关闭。test-c32-lds.ps1保留复现，旧65ms阶段结果不代替本次测量。

另独立实现HIP_C32_INPUT_DWORD：输入逐值fp8转换后四个byte合并，以一次memcpy4写入原64×36 LDS布局；每行末尾四byte仍写零，mapped坐标和padding规则不变。默认1，设0回旧逐byte写法。没有开启LDS_VECTOR。

正常900 ABBA四轮各6次去cold：35.2785→33.9735ms，约3.699%；seed123/history36.516→35.4325ms，约2.967%。各组四轮输出分别全匹配7b959143…/75b62d2f…。HDR异步40帧32.984ms/FEEA9EF3…，24帧每8帧reset33.740ms/22C171FC…，全有限。HDR为正确性回归，无同期ABBA速度结论。

COMGR/gfx1201编译通过，packed C32模块SHAC3CF4A4D0C597F9302F69F417A360297F988F196B10A328A7EDAC952AE98561C；24模块固定在hip-backend/c32-input-dword-release-modules，DLL仍native-identity-shift.addon64/5ab7d7d3…。未部署，游戏仍8568acff…、用户24FPS。日志release/HIP/c32-lds-build/test.log，c32-input-dword-build/test/history/hdr/reset.log。编译input候选定义HIP_ISA_HALF=1、HIP_PREPACKED_WEIGHTS=1、HIP_C32_INPUT_DWORD=1；test-c32-input-dword.ps1以固定direct8候选为基线。


### 2026-09-15：C32输出投影LDS整组读取撤回，刷新热点
仅将输出投影a操作数从逐byte put_bits改成memcpy8，宏HIP_C32_OUTPUT_READ8控制。COMGR编译及正常900 ABBA4轮输出hash全部通过，但热中位34.0665→34.243ms略慢，已从生产源码撤回。补丁归档Development/HIP/experiments/c32-output-read8.patch，测试脚本test-c32-output8.ps1保留（需先应用补丁编译候选，定义HIP_ISA_HALF=1、HIP_PREPACKED_WEIGHTS=1、HIP_C32_OUTPUT_READ8=1）。没有用此变体更新固定候选。

profile-current.ps1更新为c32-input-dword-release-modules并开启identity-shift。热轮逐核等待诊断：mapped C32 9次6.061ms；MH QKV归一化49次7.454ms、融合attention49次4.507ms、matrix投影36次3.515ms、shift pack44次3.286ms/crop45次2.744ms。输出保持7b959143…。逐核等待含调度开销，不当正常帧组成比例；identity-shift确实把原pack49/crop50各减少5次。日志release/HIP/c32-output8-build/test.log和profile-after-c32.log。游戏与固定候选均未改变。


### 2026-09-15：MH QKV输入预打包无收益，撤回
新增独立pack4输入核和fast_dense<true,false,true>的QKV归一化入口，沿用原pack4转换，没有使用带额外clamp的ViT pack核。临时--prepack-qkv开关和min-c选择器在所有49次或仅C>=256时先量化一次，以减少N列块重复转换。

COMGR/gfx1201模块及主机runner编译通过。正常900 ABBA4轮各6次去cold，全通道33.891→34.2605ms更慢；仅C>=256时33.963→34.1615ms也更慢。两组四轮最终RGB全匹配7b959143…。没有明确收益，生产源码与默认配置已恢复，未进一步跑历史/HDR，不把正确输出当提速证明。

实验补丁Development/HIP/experiments/mh-qkv-prepack.patch，脚本test-mh-prepack.ps1（需先应用补丁构建runner和multihead-fast-padded-wave-packed候选；MinChannels默认0，另测256）。实验模块mh-prepack.hsaco由multihead_fast_padded.hip前置HIP_ISA_HALF=1/HIP_PREPACKED_WEIGHTS=1生成。日志release/HIP/mh-prepack-build/test.log、mh-prepack-large-test.log。远端reference_current.exe暂为带实验开关runner，但默认关闭；生产源码已撤回。最优固定候选仍c32-input-dword-release-modules+5ab7d7d3 DLL，游戏仍8568acff…异步版，未部署。


### 2026-09-15 21:19：部署约34ms候选到《剑星》
用户确认继续部署。脚本重新确认游戏退出，安装native-identity-shift.addon64（SHA5ab7d7d37d5b8fdbf8248031cf073e389df8c358e262cddf096718b1113bc225）和c32-input-dword-release-modules全部24个HSACO，逐hash核验通过；再次核验packed C32为C3CF4A4D0C597F9302F69F417A360297F988F196B10A328A7EDAC952AE98561C。保留900P、HIP_FAST=1、ASYNC_SUBMIT=1和原history/continuous配置。

包含颜色/motion不可变绑定快取、identity-shift及C32 DWORD权重/直接8byte权重/输入DWORD写入。不包含已撤回的输出LDS整组读取、独立QKV预打包；LDS_VECTOR仍关闭。旧用户24FPS的8568acff… DLL、模块、配置及标记备份D:\DLSSNR-Lab\hip-backend\stellarblade-hip\before-c32-input-dword，可用部署脚本-Restore -BackupName before-c32-input-dword回退（须游戏退出）。实际新FPS与画面待试玩，不能把离线34ms等同游戏FPS。此前被打断的部署回合只检查，实际文件替换发生在本次。


### 2026-09-15 21:25：用户实机26～27FPS，分辨率差异保留
用户实测当前5ab7d7d3… DLL+c32-input-dword模块约26～27FPS，并明确再次指出这是900P；之前HLSL约37FPS对应1080P。不能把两者描述为同工作量追平。新反馈已归档，目标仍有显著差距。

### 2026-09-15：当前HLSL/HIP同条件完整流水线对照
编译当前benchmark_hlsl_current.exe（DLSS5_COMPARE_HLSL），与已编译benchmark_identity.exe的当前HIP模块做HLSL/HIP/HIP/HLSL四进程对照。输入同一1296×720真实HDR、内部900P、同rebind-async-flags、history开启、异步提交、每进程40帧去前5帧。各轮热中位HLSL18.840、HIP33.896、HIP32.814、HLSL18.821ms；每后端各自最终hash稳定（HLSL C7C2F49D…，HIP FEEA9EF3…），全有限。两后端输出不逐位相同，不把此计时当数值等价验证；此测试是推理流水线隔离对照，不替代用户900P/1080P游戏实测。

源码审计确认HLSL在native_actual_network70.h与native_decoder_tail69.h用ChainFromRaw+SetSkipFinish，让相邻C32阶段直接读上阶段raw tile；native_c32_stage.h用input mode3传递前后位移。HIP C32()当前每阶段仍运行finish/crop并生成float main，然后下阶段再映射读取。此外HLSL preblock main8/block4 skip8/block69 main8路径已存在，HIP仍多处float。未据此断言这些差异解释全部15ms。测试脚本compare-current-backends.ps1，日志release/HIP/compare-current-backends.log。


### 2026-09-15：HIP C32 raw-chain接通并验证
新增half_chain入口及RawMapped模板：当前窗口映射到逻辑raster坐标，先检查边界，再加前阶段sx/sy定位其half raw tile，读取时执行原finish的F量化。输入pack与残差读取共用此映射，不能直接用未量化raw。block1/66首块仍mode0 raster输入，后续2–4/67–69为mode3 raw输入；两条链各跳过前3块main finish/crop，末块block4的main/down和block69的main保留。保留前阶段raw的shared_ptr直到下阶段提交，内存池沿同stream有序复用。

--raw-chain独立控制，要求fused_ffn/half/mapped/crop配置；stage observer/dump与raw-chain明确不兼容，避免把raw tile冒充raster输出。默认参考runner仍关闭，HIP_FAST开启并记录日志；旧路径保留。

正常900P ABBA四轮各6次去cold34.080→33.5075ms，seed123/history35.742→34.9245ms。每组四轮RGB分别匹配7b959143…/75b62d2f…。HDR异步40帧33.554ms/FEEA9EF3…，24帧每8帧reset33.262ms/22C171FC…，全有限。节省约0.6–0.8ms，不能据此解释HLSL18.8ms与HIP33ms的主要差距。

内核、runner和完整DLL编译通过。候选release/HIP/native-c32-chain.addon64 SHAd8fb00dc9599c7c38cd18dab8b772d424ac8314934f79a48650751df9b545dae，packed C32模块SHA07182D2DC2FA5CC6B8DD734CDEF7FFD69F0144530261FF0E74C51FA85D0C3992；24模块固定c32-chain-release-modules。未部署，游戏仍5ab7d7d3…+inputDWORD，用户900P26～27FPS。脚本test-c32-chain.ps1，日志release/HIP/c32-chain-build/test/history/hdr/reset.log。编译C32模块定义HIP_ISA_HALF=1、HIP_PREPACKED_WEIGHTS=1。


### 2026-09-15：前置块高分辨率跳接main8
新增c32_finish_fast_half_main8，前置block0的main先执行原F量化，再存E4M3 byte，downsample保留原计算；hip_post_merge_fast_skip8读byte还原到float后执行原乘法和Hrtz。未改变精度，尤其保留F对零/饱和的语义。900P内部1600×1024×32的跳接从200MiB float降到50MiB byte（逻辑缓冲减少150MiB，不等于驱动实测占用下降）。--pre-main8默认参考runner关闭，HIP_FAST开启并记日志；需要fast/half且禁用stage observer/dump，避免将byte当f32诊断。

正常900P ABBA4轮各6次去cold：33.515→33.139ms；seed123/history35.2505→34.655ms。每组四轮RGB分别匹配7b959143…/75b62d2f…。HDR异步40帧33.849ms/FEEA9EF3…，24帧每8帧reset31.875ms/22C171FC…，均全有限；HDR仅正确性回归，不与异轮数字拼接成速度曲线。

COMGR内核、runner和完整DLL编译通过：release/HIP/native-pre-main8.addon64 SHAc8eb1c7ac56049492710698a88cbffb4f1d6a5a4436dc6cf78580c0a082583ac；boundary-fast模块SHA9C369C1B488D0FFD80FCF31CDE22FEED4BAEF2090F1A81380501B5EA10323DB0；24模块固定pre-main8-release-modules（包含raw-chain C32）。未部署，游戏仍5ab7d7d3…+inputDWORD，用户900P26～27FPS。脚本test-pre-main8.ps1，日志release/HIP/pre-main8-build/test/history/hdr/reset.log。编译boundary时前置HIP_ISA_HALF=1，拼接c32_fast_attention.hip及boundary_fast.hip。


### 2026-09-15：post70输入融合末端merge
新增c32_post_merge_fused_half入口，在mapped读取时从低分辨率float及高分辨率main8跳接直接计算Hrtz(Hrtz(low*scale)+skip*scale)，输入量化和标量残差都调用同一公式。先检查当前坐标边界再读两源，保留padding零值。去掉独立hip_post_merge及全分辨率merged float缓冲（900P逻辑200MiB）；增加读取时计算，实际收益按测量。--post-merge-fold独立开关，要求pre_main8+fused/half/mapped；HIP_FAST默认启用并记日志。

正常900 ABBA4轮各6次去cold33.2485→32.855ms；seed123/history34.700→34.1255ms。每组四轮RGB分别保持7b959143…/75b62d2f…。完整HDR异步40帧33.300ms/FEEA9EF3…，24帧每8帧reset31.376ms/22C171FC…，均全有限；HDR非同期ABBA，不把异轮绝对值串成收益。

COMGR内核、runner、完整DLL编译通过：release/HIP/native-post-merge.addon64 SHAa7b7521b1ade857a9867327499a77a6ed12beb0b66ce8dbe5e67011d1a923b07；packed C32模块SHA4023CEBB405CB724CA7A43CE1224ADC412387DB0AD30C88F1DC7B9638A097B06；24模块固定post-merge-fold-release-modules，含main8 boundary。未部署，游戏仍5ab7d7d3…+inputDWORD（用户900P26～27FPS）。脚本test-post-merge-fold.ps1，日志release/HIP/post-merge-fold-build/test/history/hdr/reset.log。编译C32定义HIP_ISA_HALF=1、HIP_PREPACKED_WEIGHTS=1。


### 2026-09-15：刷新MH同输入层级对照，QKV单wave四输出实验撤回
compare_layers.cpp增加mh-only过滤，并在所选融合FFN配置中启用identity-shift；compare-current-mh.ps1对C64块5/6、C128块9、C256块15、C512块23用当前模块执行同输入两后端比较。各层均有限，已有跨后端数值差异保留，不能称整体逐位一致。例C64块5 HLSL wall0.3402/0.2887ms、HIP0.4797/0.4690ms；HLSL详细QKV归一化约0.04344ms，HIP重复20次隔离批次约0.167883ms。一个是D3D GPU区间，一个是HIP批次wall时间，不直接当精确4倍证据，但可指向QKV。日志release/HIP/mh-current.log。

对照native_wave_qkv_normalize.hlsl，试验单wave16×64输出、四acc共用A，替代HIP fast_dense的512线程64×64；QKV平方和仍保持原32项顺序，以LDS每行串行求和，不启用HLSL可选half平方MMA以避免混改精度。COMGR及runner编译通过；正常900 ABBA4轮各6次去cold32.939→33.727ms更慢，四轮RGB7b959143…一致。未继续历史/HDR，已撤回生产改动。补丁experiments/mh-qkv-wave4.patch、脚本test-mh-wave4.ps1；需应用补丁再构建实验runner/模块，当前默认源码不含该入口。日志release/HIP/mh-wave4-build/test.log。

最优固定候选仍a7b7521b…DLL+post-merge-fold-release-modules，游戏仍5ab7d7d3…+inputDWORD，未部署新实验。


### 2026-09-15：MH QKV逐行平方和替换readlane串行广播
核对native_wave_qkv_normalize.hlsl的NATIVE_QKV_FAST2默认0，源码构建路径未找到显式启用宏；本轮不引入half平方MMA，不更改精度。保留HIP fast_dense的512线程64×64矩阵结构，QKV写入64×65带padding的float LDS，由128个线程各负责一行/一head的32项顺序平方和，存inverse后全组同步；其余线程从各自原accum和inverse输出byte。消除原每wave反复32次readlane，保持逐项加法顺序。HIP_QKV_ROW_SUM默认1，0保留旧实现。

COMGR/gfx1201编译通过。正常900 ABBA4轮各6次去cold32.722→31.6795ms；seed123/history34.277→32.9245ms。各组四轮最终RGB分别匹配7b959143…/75b62d2f…。HDR异步40帧30.328ms/FEEA9EF3…，24帧每8帧reset30.258ms/22C171FC…，均全有限；HDR非同期ABBA，不把30ms当游戏FPS或直接拼接前轮收益。

新multihead-fast-padded-wave-packed模块SHA49458E566DF2E94E2098333BE1DB7F3D15D83A0655E39BDA9D01363910A53A8C；24模块固定mh-row-sum-release-modules。主机DLL无需修改，配套native-post-merge.addon64/a7b7521b…；未部署，游戏仍5ab7d7d3…+inputDWORD（用户900P26～27FPS）。test-mh-row-sum.ps1复现；日志release/HIP/mh-row-sum-build/test/history/hdr/reset.log。编译定义HIP_ISA_HALF=1、HIP_PREPACKED_WEIGHTS=1、HIP_QKV_ROW_SUM=1。


### 2026-09-15：MH attention QKV byte输入DWORD搬运
multihead_fused_attention的两个byte输入入口把normalized→LDS的逐byte搬运改为memcpy4，保留3×64×36布局、padding和原softmax/WMMA顺序。HIP_MH_INPUT_DWORD默认1，0保留旧逐byte路径；float输入入口不改。

COMGR/gfx1201编译通过。正常900 ABBA4轮各6次去cold31.5085→31.1215ms，seed123/history32.871→32.549ms。各组四轮输出分别保持7b959143…/75b62d2f…。HDR异步40帧30.047ms/FEEA9EF3…，24帧每8帧reset29.932ms/22C171FC…，全有限；HDR为正确性回归，不与异轮数字直接相减。

multihead_fused_attention模块SHA3628F07C3F4A1F3926B712BCC52DD34859C85F926B38B02373355580F0272BB2；24模块固定mh-input-dword-release-modules（包含QKV row-sum与C32 merge-fold）。配套DLL仍native-post-merge.addon64/a7b7521b…；未部署，游戏仍5ab7d7d3…+C32 inputDWORD，用户900P26～27FPS。脚本test-mh-input-dword.ps1，日志release/HIP/mh-input-dword-build/test/history/hdr/reset.log。编译定义HIP_ISA_HALF=1、HIP_MH_INPUT_DWORD=1。


### 2026-09-15：MH V转置与直接F-byte输出实验归档
V转置候选在载入时将V写为[channel][token]、行跨度68，保留Q/K布局；V从packed偏移4608开始、末端6784<6912，概率区64×68=4352，不重叠。AV阶段B操作数可连续memcpy8。正常900 ABBA4轮各6次去cold31.155→31.198ms，四轮RGB7b959143…一致，但无收益，已撤回。补丁experiments/mh-transpose-v.patch、test-mh-transpose-v.ps1；编译定义HIP_ISA_HALF=1、HIP_MH_TRANSPOSE_V=1。

另独立试encoded_F直接返回FP8 byte，代替fp8(F(acc))的转换/还原/再转换；显式保留输入正负零归正零、带符号非有限/超范围饱和到448，普通值用原转换指令。正常900 ABBA同条件31.185→31.0765ms，四轮RGB一致；差距约0.1ms，不足认定稳定收益，未默认、未进行历史/HDR或全域转换证明。生产源码已恢复。补丁experiments/mh-direct-fbyte.patch、test-mh-direct-fbyte.ps1；编译定义HIP_ISA_HALF=1、HIP_MH_DIRECT_FBYTE=1。

两候选COMGR/gfx1201编译通过；日志release/HIP/mh-transpose-v-build/test.log、mh-direct-fbyte-build/test.log。最优固定候选仍mh-input-dword-release-modules+a7b7521b DLL；游戏未变，仍5ab7d7d3…+C32 inputDWORD，用户900P26～27FPS。


### 2026-09-15：部署约31ms整合候选到《剑星》
部署脚本确认游戏退出后安装native-post-merge.addon64（SHAa7b7521b1ade857a9867327499a77a6ed12beb0b66ce8dbe5e67011d1a923b07）及mh-input-dword-release-modules全部24个HSACO，逐hash验证通过。再次读取确认900P、HIP_FAST1、ASYNC_SUBMIT1，attention模块3628F07C…匹配。含C32 raw-chain/pre-main8/postmerge融合、MH row-sum及DWORD输入搬运，不含V转置、直接F-byte或此前其他未采纳实验。

旧5ab7d7d3… DLL/模块/flags/标记备份D:\DLSSNR-Lab\hip-backend\stellarblade-hip\before-mh-input-dword；用户900P26～27FPS属于该旧版。回退用deploy-stellarblade-update.ps1 -Restore -BackupName before-mh-input-dword（游戏须退出）。本次未启动游戏，实机新FPS和画面待验证；离线约31ms不等于新游戏FPS。


### 2026-09-15：QKV分块权重合作装载实验撤回
PackedMhWeight仅对QKV矩阵区域做TilePackedMatrix [N16][K32][K][N]纯byte重排，独立cache key和mh_qkv_normalize_fused_tiled入口；尺度/偏置/输出投影区域不动。512线程连续读取tile的DWORD，再按行/k分散写回原LDS布局，WMMA与row-sum不变。--tiled-qkv只在候选启用。

COMGR/gfx1201模块与runner编译通过，正常900 ABBA4轮各6次去cold31.129→31.4705ms更慢；四轮最终RGB7b959143…一致。未进行历史/HDR，生产源码已恢复，未加入已部署版本或固定候选。推测全域连续读取收益被LDS散写/指令成本抵消，未做ISA级归因证明。

补丁Development/HIP/experiments/mh-qkv-tiled.patch，test-mh-qkv-tiled.ps1（先应用补丁构建实验runner和模块），编译定义HIP_ISA_HALF=1、HIP_PREPACKED_WEIGHTS=1；日志release/HIP/mh-qkv-tiled-build/test.log。游戏本轮确认退出，当前安装仍a7b7521b…+mh-input-dword，等待实机反馈。远端reference_current.exe含实验开关但默认关闭；源码已撤回。


### 2026-09-15：QKV ISA资源检查及共享暂存复用实验撤回
读取当前mh-row-sum.hsaco.s：mh_qkv_normalize_fused使用VGPR46、SGPR14、固定LDS21760byte、private segment0，无scratch spill。没有据此给出硬件occupancy百分比。

试用union复用矩阵sa/sb与归一化tile/inverse（两段使用之间已有全组barrier），非Normalize实例仍只分配矩阵空间。编译后的QKV LDS降到17152byte，VGPR47、SGPR14、private0。正常900 ABBA4轮各6次去cold31.1555→31.2715ms，四轮RGB7b959143…一致，但无收益；已恢复生产源码，未跑历史/HDR、未默认。补丁experiments/mh-shared-workspace.patch，test-mh-shared-workspace.ps1（先应用补丁编译，定义HIP_ISA_HALF=1、HIP_PREPACKED_WEIGHTS=1、HIP_QKV_SHARED_WORKSPACE=1）。日志release/HIP/mh-shared-workspace-build/test.log。

当前安装及固定候选仍a7b7521b…+mh-input-dword-release-modules。此次未部署或修改游戏配置。


### 2026-09-15：当前快速路径HIP Graph复测
benchmark-graph-frame.ps1参数化Runner/Modules/ConfigFile/ExpectedHash，增加每轮前后游戏进程检查和graph replay确认证据。新增参数时最初将[string]Flags与局部$flags冲突（PowerShell大小写不敏感），数组被串成一行；首轮固定参考hash检查失败，未使用该轮数据，已改名ConfigFile修复。游戏文件未动。

使用benchmark_merge.exe、mh-input-dword-release-modules、rebind-async-flags做graph0/1/1/0，完整HDR40帧、temporal1、去前5，各方案70热样本：graph0中位30.0715ms，graph1 29.838ms，约0.78%。两个graph轮次均builds1/replays38，四轮最终FEEA9EF3…一致，全有限。没有只开flag却未重放；当前路径收益依旧小，默认和游戏graph保持0，未继续reset或实机Graph测试。

日志release/HIP/graph-current.log，远端graph-current/timings.csv及分轮log。约0.23ms离线收益不能解释HLSL18.8ms vs HIP约30ms主要差距，不据此认定所有调度开销为零。当前游戏仍a7b7521b…+mh-input-dword，尚无新实机FPS反馈。


### 2026-09-15：QKV固定通道数特化
mh_qkv_normalize_fused对64/128/256/512通道分支传递编译期常量N/K给fast_dense，其他计算、LDS布局与32项平方和顺序保持。HIP_QKV_SPECIALIZE默认1，0保留动态尺寸实现。COMGR产物QKV资源：VGPR45（旧46）、SGPR16（旧14）、LDS21760byte、private0；不以寄存器数单独证明收益。

正常900 ABBA4轮各6次去cold31.073→30.785ms；seed123/history32.4475→32.0865ms。两组四轮RGB分别保持7b959143…/75b62d2f…。HDR异步40帧29.660ms/FEEA9EF3…，24帧每8帧reset29.431ms/22C171FC…，均全有限；HDR为回归，无同期ABBA速度结论。

模块编译通过，multihead-fast-padded-wave-packed SHA FEBEF48CAF49ACA87E4A88D06F664F2B183CFD9CCDB1AA5972FFF8824F6EBD61；24模块固定mh-qkv-special-release-modules，配套DLL仍a7b7521b…无需变更。未部署，游戏仍a7b7521b…+mh-input-dword。脚本test-mh-qkv-special.ps1，日志release/HIP/mh-qkv-special-build/test/history/hdr/reset.log。编译定义HIP_ISA_HALF=1、HIP_PREPACKED_WEIGHTS=1、HIP_QKV_SPECIALIZE=1。


### 2026-09-15：QKV K64装载分块实验撤回
在已通道特化的QKV上把K32分块扩大到K64：每次合作装载两倍A/B，LDS行stride9改17，四次K16 WMMA维持原累加顺序，全组barrier次数减半。仅Normalize实例扩大，其他dense入口保持K32。

COMGR/gfx1201编译通过，正常900 ABBA4轮各6次去cold30.7635→30.9125ms略慢，四轮RGB7b959143…一致。编译后QKV LDS25856byte（旧21760）、VGPR69（旧45）、private0。资源使用增加与耗时变化同时出现，但未独立证明单一因果。未继续历史/HDR，生产源码已恢复，维持K32默认。

补丁experiments/mh-qkv-k64.patch，test-mh-qkv-k64.ps1，编译定义HIP_ISA_HALF=1、HIP_PREPACKED_WEIGHTS=1、HIP_QKV_K64=1；日志release/HIP/mh-qkv-k64-build/test.log。最优固定候选仍mh-qkv-special-release-modules+a7b7521b DLL，当前游戏仍mh-input-dword模块，未部署本实验。


### 2026-09-15：MH FFN第三投影融合
mh_ffn_fused_body新增Project模板分支：收缩完成后全组同步，将原F(Hrtz(acc))的byte中间值放入复用LDS，再同步后直接执行第三投影；从原input计算Hrtz残差初值，按原K16顺序累加，输出F(result) float。支持默认C64/C128行权重与C256 tiled前两矩阵，第三矩阵仍原packed行布局。省去独立mh_ffn_project_fast_fp8调用及全局middle缓冲，共36个普通MH块；C512 split路径不改。--fused-ffn-project独立，验证所选packed/byte-middle/tiled-min256配置；HIP_FAST默认开启并记录。

正常900 ABBA4轮各6次去cold31.0285→29.842ms；seed123/history32.4015→31.313ms。每组四轮RGB分别匹配7b959143…/75b62d2f…。HDR异步40帧30.132ms/FEEA9EF3…，24帧每8帧reset28.605ms/22C171FC…，全有限；HDR非同期ABBA，不将其与旧轮次硬比。

COMGR模块、runner和完整DLL编译通过：release/HIP/native-ffn-project.addon64 SHA2934f7950ffab96539238afac0dfb9619f088952b22dade20d934af6d263d9ef；multihead-fast-padded-wave-packed模块SHAE778FAFF162C8F24CBD146E9FB3016EFCE075B322781C54913ABDF2C8F788FDB，24模块固定mh-ffn-project-release-modules。未部署，游戏仍a7b7521b…+mh-input-dword。脚本test-mh-ffn-project.ps1，日志release/HIP/mh-ffn-project-build/test/history/hdr/reset.log。编译MH模块定义HIP_ISA_HALF=1、HIP_PREPACKED_WEIGHTS=1。


### 2026-09-15：融合FFN到QKV的byte接口无速度收益，归档
实验让C64/C128/C256融合FFN第三投影直接输出原F(result)的FP8 bytes；QKV使用ByteInput，attention矩阵残差分支从byte还原，包括matrix_residual的三段尺度路径。C512 split保留float。通过独立入口、缓存分配大小和--ffn-byte开关区分ABI，没有新增量化调度。

COMGR/gfx1201模块与runner编译通过，正常900 ABBA4轮各6次去cold29.934→29.9425ms，四轮RGB7b959143…一致。减少中间逻辑数据量但没有速度收益，未进一步跑历史/HDR或默认启用；生产源码恢复。补丁experiments/mh-ffn-byte.patch、test-mh-ffn-byte.ps1（应用补丁后构建runner和MH模块，定义HIP_ISA_HALF=1、HIP_PREPACKED_WEIGHTS=1）。日志release/HIP/mh-ffn-byte-build/test.log。

最优固定候选仍native-ffn-project.addon64/2934f795…+mh-ffn-project-release-modules，游戏仍a7b7521b…+mh-input-dword，未部署新实验。这个结果与此前独立预打包慢不同：新增调度已消掉，但本布局下仍无净速度收益，不能据此认定所有byte接口都无效。


### 2026-09-15：C512 split分组FFN展缩融合
新增split_ffn_fused：每组16token×64channel、4wave计算256维展开并将原Hrtz+激活+FP8结果留在LDS，再按原K16 FP16 WMMA顺序收缩，输出F(Hrtz)。split_mix和split_projection保持，省去13个展开/收缩之间的全局hidden与一次调度。--split-ffn-fused独立开关，要求fast_deep/fp8_deep，HIP_FAST默认启用并记日志。

初版复用单wave gr()辅助函数导致多wave索引错误，首轮hash检查拦截；修正为(workitem_id>>4)&1，仅将gr明确限制为wave内半组，原32线程核行为相同。失败轮不计性能。修正后正常900 ABBA4轮各6次去cold29.924→29.509ms，seed123/history31.4555→30.8895ms；各组四轮RGB分别匹配7b959143…/75b62d2f…。HDR异步40帧28.902ms/FEEA9EF3…，24帧每8帧reset29.561ms/22C171FC…，全有限。HDR非同期ABBA，无独立速度增幅结论。

内核、runner和完整DLL编译通过：release/HIP/native-split-ffn.addon64 SHA1d9470bc2bb8919a6c3af1cc1dafb97611287ef46bc1c69631484ce92ed33f4c；deep_fast-packed模块SHABC15C323B19D4FDD574C73D8D885D7C3D8328BAF6593AD11C32DE9F774EAD255；24模块固定split-ffn-release-modules，含普通MH第三投影融合。未部署，游戏仍a7b7521b…+mh-input-dword。脚本test-split-ffn.ps1，日志release/HIP/split-ffn-build/test/history/hdr/reset.log，编译定义HIP_ISA_HALF=1、HIP_PREPACKED_WEIGHTS=1。


### 2026-09-15：C512 split mix四输出分块
新增split_mix_blocked：单wave16×64输出、四个acc共用同一A片段；K512仍逐K16执行FP16 WMMA，最终F(Hrtz)不变。dispatch实际数量原1/4。--split-mix-blocked独立，HIP_FAST默认启用并记日志；其他split阶段本轮保持。

正常900 ABBA4轮各6次去cold29.5895→29.136ms，seed123/history30.922→30.5395ms。各组四轮RGB分别匹配7b959143…/75b62d2f…。HDR异步40帧29.785ms/FEEA9EF3…，24帧每8帧reset27.787ms/22C171FC…，全有限；HDR非同期ABBA，不把异轮耗时拼接成收益。

内核、runner和完整DLL编译通过：release/HIP/native-split-mix.addon64 SHA3b2c1d021b1c88c2934f3140b1ca02254abba70b7bb4aaa23160ee52ec6d955c；deep_fast-packed模块SHA5F6646A4102258D7874F587A9E456AB24B4B5D9B178308214DC1997537DBC544；24模块固定split-mix-blocked-release-modules。未部署，游戏仍a7b7521b…+mh-input-dword。脚本test-split-mix-blocked.ps1，日志release/HIP/split-mix-blocked-build/test/history/hdr/reset.log。编译定义HIP_ISA_HALF=1、HIP_PREPACKED_WEIGHTS=1。


### 2026-09-15：split投影四输出分块
新增split_projection_blocked，单wave16×64、四acc共用A片段，K512保持K16 FP8 WMMA累加；残差初值H(skip*scale)、末端F(H(acc))保持RNE，不改成Hrtz。dispatch原1/4。--split-project-blocked独立，HIP_FAST默认启用并记日志。

正常900 ABBA4轮各6次去cold29.1565→28.780ms；seed123/history30.786→30.1375ms。各组四轮RGB分别匹配7b959143…/75b62d2f…。HDR异步40帧29.311ms/FEEA9EF3…，24帧每8帧reset27.353ms/22C171FC…，全有限。HDR为正确性回归，不与异轮数字直接比较速度。

内核、runner、完整DLL编译通过：release/HIP/native-split-project.addon64 SHA13c7cd1273b2fc2d6eb37e1766246005e635fbcb91d3c7a4c5a57a6eb3a34fcd；deep_fast-packed模块SHACFF00D26D3E2D59CF0CE24A023BFFC265E77BF74FE8DD8BF5784C2DD8779EA0C；24模块固定split-project-blocked-release-modules。未部署，游戏仍a7b7521b…+mh-input-dword。脚本test-split-project-blocked.ps1，日志release/HIP/split-project-blocked-build/test/history/hdr/reset.log；编译定义HIP_ISA_HALF=1、HIP_PREPACKED_WEIGHTS=1。


### 2026-09-15：部署约28.8ms FFN/split整合候选
部署脚本确认游戏退出，安装native-split-project.addon64（SHA13c7cd1273b2fc2d6eb37e1766246005e635fbcb91d3c7a4c5a57a6eb3a34fcd）及split-project-blocked-release-modules的24个HSACO，逐hash验证通过。再次确认900P/HIP_FAST1/ASYNC_SUBMIT1、deep_fast-packed为CFF00D26…匹配。包含QKV通道特化、普通MH第三投影融合、C512分组FFN融合及mix/projection分块；不含已拒绝byte接口、K64等实验。

旧a7b7521b… DLL/模块/flags/标记备份D:\DLSSNR-Lab\hip-backend\stellarblade-hip\before-split-project。回退用deploy-stellarblade-update.ps1 -Restore -BackupName before-split-project（游戏须退出）。未启动游戏，新版实际FPS尚未验证，离线28.8ms不等于游戏帧率。用户最后明确实测仍为更早5ab7版900P26～27FPS；不要把此数字归到a7b或新13c7版本。


### 2026-09-15 22:24：用户实测当前900P约30FPS
Zero反馈已部署13c7cd12… DLL+split-project-blocked模块在《剑星》900P约30FPS。此前明确反馈：5ab7版900P26～27FPS、8568异步版约24FPS；HLSL历史基准是1080P约37FPS，不能描述成同分辨率追平。此为用户试玩观察，没有严格固定场景或逐项画面验收。

当前profile脚本已更新为完整split-project快速选项，刷新诊断输出在release/HIP/profile-after-split.log，最终RGB匹配7b959143…。热轮QKV归一化累计5.040ms、MH attention3.168ms、matrix projection3.284ms、ViT QKV1.787ms；均为逐核等待诊断，不当正常帧百分比。用户反馈到达前只完成诊断，尚未开始下一内核改动。


### 2026-09-15：ViT QKV两输出分块，四输出不采用
先试单wave16×64四输出，正常900 ABBA4轮去cold29.0145→29.1985ms略慢，四轮RGB一致，不采用；函数存experiments/vit-qkv-four.hip，需替换同名核及host groups=count/1024复现。

改为单wave16×32两输出共用A，FP16 K16乘加顺序、part分离输出保持。入口仍命名vit_qkv_project_blocked，--vit-qkv-blocked明确对应最终两输出；host groups=count/512。正常900 ABBA4轮各6次去cold28.9295→28.6415ms，seed123/history30.215→30.074ms，收益较小；各组四轮RGB分别匹配7b959143…/75b62d2f…。HDR异步40帧29.648ms/FEEA9EF3…，24帧每8帧reset28.691ms/22C171FC…，全有限。HDR非同期ABBA，不计算异轮收益。

内核、runner和完整DLL编译通过，HIP_FAST默认两输出并記日志：release/HIP/native-vit-qkv.addon64 SHAd498ff836fecd3576e58964f8a703553ec4fee99c877a9494bf0a58712802989；deep_fast-packed模块SHA804DAB0C81E4CA09FCCAFEF7AD41DEEF3C635107DAA4CF472979371268BD3230；24模块固定vit-qkv-pair-release-modules。未部署，游戏仍13c7cd12…+split-project-blocked，用户900P30FPS。脚本test-vit-qkv-pair.ps1，日志release/HIP/vit-qkv-blocked-build/test.log及vit-qkv-pair-build/test/history/hdr/reset.log；编译定义HIP_ISA_HALF=1、HIP_PREPACKED_WEIGHTS=1。


### 2026-09-15：MH输出投影融合shift crop
fast_dense新增Crop模板，仅改变最终有效区写地址，工作区矩阵乘加与归一化保持。mh_attention_crop同时支持普通MH的matrix residual和C512的scalar residual；C512/rounded/raw对应epilogue保持。Body对非identity块直接调用带crop几何的AttentionFast，输出只分配逻辑w×h×c，省去44次独立mh_shift_crop和工作区attended缓冲；block4 down裁剪保持。--mh-project-crop独立，要求fast_mh/fp8_av，HIP_FAST默认启用并记日志。

正常900 ABBA4轮各6次去cold28.791→28.3495ms，seed123/history30.285→30.0065ms。各组四轮RGB分别匹配7b959143…/75b62d2f…。HDR异步40帧28.648ms/FEEA9EF3…，24帧每8帧reset28.188ms/22C171FC…，全有限；HDR非同期ABBA，不据此计算异轮收益。

内核、runner及完整DLL编译通过：release/HIP/native-mh-crop.addon64 SHA111e8c4033e7d7f33c0792dac467307cccf2898b1d39a0054d866f0f379366d7；multihead-fast-padded-wave-packed模块SHAA118C4678D217DB25482E11D194350137890202B8399570C290EEDFEB21CBDAC；24模块固定mh-project-crop-release-modules。未部署，游戏仍13c7cd12…+split-project-blocked（用户900P30FPS）。脚本test-mh-project-crop.ps1，日志release/HIP/mh-project-crop-build/test/history/hdr/reset.log；编译定义HIP_ISA_HALF=1、HIP_PREPACKED_WEIGHTS=1。


### 2026-09-15：普通MH融合FFN直接映射输入
新增ffn_input<C,Mapped>，按工作区token映射至原raster坐标，减sx/sy后越界返回0；合作输入装载、非合作fallback及第三投影残差均使用同一映射。C64/C128/C256的非identity块选择_project_mapped入口，不分配packed、不启动shift_pack；C512 split及identity路径保持。--mh-input-mapped独立开关，要求已融合第三投影的受支持配置；HIP_FAST默认启用并记日志。

正常900 ABBA4轮各6次去cold28.8175→27.8315ms；seed123/history29.8155→29.5695ms，历史组收益较小。各组四轮RGB分别匹配7b959143…/75b62d2f…。HDR异步40帧28.696ms/FEEA9EF3…，24帧每8帧reset27.046ms/22C171FC…，全有限。HDR为正确性回归，不以异轮耗时宣称稳定1ms收益。

内核、runner及完整DLL编译通过：release/HIP/native-mh-mapped.addon64 SHAb4e46e159746caafe4877e5128af2bb1b5098d89950882ed7a4f750f3ef3a4e0；multihead-fast-padded-wave-packed模块SHA899CDB452D514DC5A7731EA39D26C9F7AD347CC3CFF2350DB1D2ADFF65180FD0；24模块固定mh-input-mapped-release-modules。未部署，游戏仍13c7cd12…+split-project-blocked（用户900P30FPS）。脚本test-mh-input-mapped.ps1，日志release/HIP/mh-input-mapped-build/test/history/hdr/reset.log；编译定义HIP_ISA_HALF=1、HIP_PREPACKED_WEIGHTS=1。


### 2026-09-15：C512输入/残差映射实验暂不采用
新增split_mix_blocked_mapped及split_projection_blocked_mapped，前者矩阵输入、后者残差均按原shift_pack的workgrid→raster规则映射，越界0；中间mix/FFN/contract保持工作区布局和计算。--split-input-mapped开关候选启用，正常900 ABBA4轮各6次去cold28.038→27.963ms，仅约0.075ms差距，四轮RGB7b959143…一致。无明确速度收益，未继续历史/HDR、未默认；生产源码恢复。

内核和runner编译通过。补丁experiments/split-input-mapped.patch，test-split-input-mapped.ps1（应用补丁后编译runner/deep模块，定义HIP_ISA_HALF=1、HIP_PREPACKED_WEIGHTS=1）；日志release/HIP/split-input-mapped-build/test.log。保留固定候选native-mh-mapped.addon64/b4e46e15…+mh-input-mapped-release-modules，游戏仍13c7cd12…+split-project-blocked，用户900P30FPS。本轮未部署。


### 2026-09-15：部署MH输入映射/输出裁剪整合候选
部署脚本确认游戏退出，安装native-mh-mapped.addon64（SHAb4e46e159746caafe4877e5128af2bb1b5098d89950882ed7a4f750f3ef3a4e0）及mh-input-mapped-release-modules全部24个HSACO，逐hash验证通过。再次确认900P/HIP_FAST1/ASYNC_SUBMIT1、MH packed模块899CDB45…匹配。包含ViT QKV两输出、MH投影crop及普通MH输入映射；不含已归档的C512映射。

旧13c7cd12… DLL/模块/flags/标记备份D:\DLSSNR-Lab\hip-backend\stellarblade-hip\before-mh-mapped，用户900P30FPS属于此旧版。回退用deploy-stellarblade-update.ps1 -Restore -BackupName before-mh-mapped（游戏须退出）。本轮未启动游戏或验证新FPS；离线约27.8ms不等于游戏FPS。当前新安装的画面和性能待试玩。


### 2026-09-15：当前两后端对照出现HLSL基准漂移，未认定追平
compare-current-backends.ps1更新HIP为benchmark_mh_mapped.exe+mh-input-mapped-release-modules，同一HDR/900P/history/async做HLSL/HIP/HIP/HLSL：HLSL26.622、HIP26.341、HIP27.142、HLSL26.520ms。各后端自身hash稳定（HLSL C7C2F49D…、HIP FEEA9EF3…），全有限。HLSL显著慢于此前同条件18.840/18.821ms，不能把参考变慢算作HIP优化收益。

再用更早benchmark_live_hlsl.exe、相同配置和输入复核：26.719ms，hash仍C7C2F49D…。远端compare-hlsl-async-flags.txt与rebind-async-flags.txt逐行Compare-Object无差异。因此新/旧HLSL测试程序都出现当前慢值，未定位根因；可能仍需检查驱动运行状态/频率/资源驻留或其他共享环境因素，不能把猜测当结论。进程检查未见游戏运行；未终止其他进程或更改驱动/全局设置。

日志release/HIP/compare-after-mapped.log和hlsl-old-binary-check.log。当前测得两后端接近只限当下条件，不证明重现历史HLSL18.8ms，也不证明HIP900P或1080P游戏帧率已追平。目标保持未完成。游戏仍b4e46e15…+mh-input-mapped，本轮未部署或修改配置。


### 2026-09-15：分离HLSL命令列表内/间时间
新增仅DLSS5_BENCH_LIST_TIMING编译的NativeActualNetwork70诊断，每个网络命令列表两端插时间戳，末尾resolve/Flush，输出列表内区间、列表间空档、CPU记录/提交与wall时间；未定义宏不启用计时。默认batch2共6列表，诊断限制128个时间戳。此额外Flush可能改变后处理重叠，不作为优化。

40帧去前5，35热样本中位：in-list25.83108ms、gaps0.06636ms、CPU record/submit0.543ms、network wall26.373ms。完整HDR热中位27.001ms，最终C7C2F49D…匹配HLSL参考，全有限。当前HLSL慢值主要处于GPU列表执行区间，不是CPU记录或列表间空档，但区间含GPU等待/抢占，尚未定位频率、驱动或shader路径原因。

诊断exe编译通过，说明Development/HIP/hlsl-list-timing.md，日志release/HIP/hlsl-lists-summary.log、hlsl-lists.log（后者UTF-16）。游戏未修改，历史HLSL18.8ms尚未重现，目标未判完成。


### 2026-09-15：HLSL慢基准的只读ADL遥测
新增read-adl-telemetry.cpp，按AMD官方头文件/接口动态查询驱动时钟、负载、温度，不调用任何设置API；measure-hlsl-telemetry.ps1并行记录150×200ms样本与HLSL40帧检查。当前接口已deprecated但驱动仍返回supported数据，说明和来源见Development/HIP/adl-telemetry.md。

HLSL本轮26.872ms、C7C2F49D…一致。tick284568312–284575609窗口内adapter0、gfx activity>50%的8样本：核心1511–1849MHz，中位1807MHz；edge43–44°C，hotspot47–52°C。多个逻辑adapter不当独立GPU统计，ASIC power不支持不报0W。窗口包括初始化/收尾、样本少，且旧18.8ms无遥测，不能据此认定降频就是漂移根因。

日志release/HIP/hlsl-adl-summary.log、hlsl-adl.log。工具编译/查询成功；未修改游戏或驱动设置。代码审计显示逐帧benchmark会完整读回、扫描HDR数据，可能造成GPU间歇负载，后续需要控制此条件才可判断频率影响。


### 2026-09-15：确认读回节奏造成HLSL/HIP比较偏差
benchmark_live_capture增加可选edges_only诊断模式，逐帧推理和历史更新不变，仅首尾读回；CSV明确checked与未检查空字段。validate-hdr支持该模式并输出sampled_finite/checked_frames，默认完整验证不接受缺帧。两后端新程序编译通过。

同程序/同配置0/1/1/0：HLSL全读回26.577/26.588ms，首尾16.736/16.686ms；ADL活跃样本核心分别约1.79GHz和2.8–3.0GHz，负载65%和94–95%。HIP全读回28.454/27.166ms，首尾26.290/26.365ms；核心约2.74–2.81GHz和3.0GHz，负载78–84%和99%。后端内部四轮最终hash保持各自C7C2…/FEEA…；全读回全部有限，首尾仅2帧检查。未改变游戏或驱动设置。

读回/CPU图像扫描位于原计时区间外，却改变负载间隔与GPU频率。这解释当前可复现的HLSL慢值，不能把先前26ms/26ms当HIP追平。较连续同条件当前HLSL约16.7ms、HIP26.3ms，仍差约9.6ms。历史18.8ms无同期频率，不能反推精确历史状态。详见Development/HIP/readback-cadence.md。性能测试以后采用一致节奏，完整图像正确性仍单独保留。


### 2026-09-15：融合MH FFN输入pack4，采用连续负载对照
HIP_FFN_INPUT_PACK4默认1：合作输入以四个值调用原pack4量化并memcpy4写入共享区，替代四次q8/byte写入。映射与非映射输入共用，padding四byte为0，后续计算不改；0保留旧路径。

用同一benchmark_hip_edges.exe、相同配置和输入做模块ABBA，每轮40帧、去前5，首尾检查：基线热中位26.363/26.382ms，候选26.129/26.182ms。四轮最终FEEA9EF3…匹配；首尾模式只验证2帧，不称全帧验证。随后候选独立40帧全读回和24帧每8帧reset均全有限，最终分别匹配FEEA9EF3…/22C171FC…。两次全检计时27.478/26.545ms仅记录，不与连续负载计时相减。未另跑seed123的整网ABBA。

COMGR/gfx1201模块编译通过，multihead-fast-padded-wave-packed SHA12425000552D3B5AC34C98BB7E10156091D981D98B0D5722C151C2D973BD8F47；24模块固定ffn-input-pack4-release-modules，配套DLL仍native-mh-mapped.addon64/b4e46e15…无需修改。未部署，游戏仍b4e46e15…+mh-input-mapped模块。脚本test-ffn-input-pack4.ps1，日志release/HIP/ffn-input-pack4-build/test/full/reset.log。编译定义HIP_ISA_HALF=1、HIP_PREPACKED_WEIGHTS=1、HIP_FFN_INPUT_PACK4=1。


### 2026-09-15：QKV/FFN直接量化byte收益过小，归档
实验dense_byte_F合并q8(F(x))，保留输入正负零归正零、带符号超范围/非有限饱和，其余直接执行FP8转换；用于QKV归一化和FFN隐藏/收缩输出。未改输出float的路径。

COMGR/gfx1201编译通过。连续负载EdgesOnly=1、40帧ABBA：基线26.127/26.129ms，候选26.053/26.045ms；四轮最终FEEA9EF3…匹配，首尾检查有限。收益约0.08ms，未进一步做全域转换证明、全帧/history-reset验证，生产源码已恢复，未默认。补丁experiments/dense-direct-fbyte.patch，test-dense-direct-fbyte.ps1（需应用补丁编译，定义HIP_ISA_HALF=1、HIP_PREPACKED_WEIGHTS=1、HIP_DENSE_DIRECT_FBYTE=1）；日志release/HIP/dense-direct-fbyte-build/test.log。

最优固定候选仍ffn-input-pack4-release-modules+b4e46e15 DLL，游戏仍b4e46e15+mh-input-mapped，本轮未部署。


### 2026-09-15：修正过时的层级基准，定位前置路径差异
compare_layers的mh-only现要求fused-selected并启用当前普通FFN第三投影融合、MH crop/mapped和split优化，模块更新至ffn-input-pack4-release-modules。新增c32-only；HIP mode0保持显式pack，mode1真正调用mapped入口，mode2从与HLSL相同的half raw tile输入调用chain入口，而非此前所有模式都额外pack。Profile也使用对应输入和入口。测试工具编译通过，游戏/推理源码未变。

当前MH批次wall两轮中位（HLSL/HIP ms）：C64块5 0.3121/0.3257；移位块6 0.3213/0.3903；C128块9 0.2304/0.2501；C256块15 0.2020/0.2173；C512块23 0.1393/0.2182。全有限，既有跨后端数值差异保留。C32高分辨率块70 mode1 1.0619/1.4210ms，mode2 1.0660/1.6221ms；块1 mode1 0.2832/0.3793ms、块4 mode1 0.2847/0.3839ms。该组C32测试mode0/1/2的输出比较通过，无非有限；详情见日志，不推广到所有输入等价。

这些隔离批次的预热/工作集/时钟条件仍不同于整网，不直接按层数求和解释约9.6ms差距。此前旧工具额外pack/未启用融合的HIP时间不再用于当前归因。

源码确认HLSL NativePreblockRuntime有DLSS5_INLINE_PREFIX映射mode5，HIP仍独立dlss5_prefix_fast_features+project并物化两幅float图。下一步值得对照该前置路径，不再只盯普通MH的小差距。脚本compare-current-mh.ps1、compare-current-c32.ps1；日志release/HIP/mh-current-latest.log、c32-current-latest.log。


### 2026-09-15：前置特征生成与投影融合
新增dlss5_prefix_fast_fused：每wave处理16token，前16lane按原PCG/高斯噪声、RGB及history公式生成16维特征，放入共享内存，再共同计算32维投影。保留原第二次零填充K16 WMMA及Hrtz输出，不改变量化顺序；省去32通道float features全图（900P逻辑200MiB）和一次独立kernel。prefix投影输出float仍保留，尚未与C32 body完全内联。

连续负载40帧ABBA：基线26.171/26.160ms，融合25.359/25.372ms，约0.8ms收益。四轮最终FEEA9EF3…匹配，首尾抽样有限；随后40帧全检、24帧每8帧reset全有限，最终匹配FEEA9EF3…/22C171FC…。全检计时25.680/25.585ms仅记录，不与连续负载混算。

HIP_FAST默认prefix_fused=true；DLSS5_HIP_PREFIX_FUSED=0/1可覆盖，reference runner增加--prefix-fused。内核、测试runner和完整DLL编译通过：release/HIP/native-prefix-fused.addon64 SHA3894351ccba9080524095cd5c87644806224c62f071250ead3c3e18526283f9b；prefix_fast模块SHAF9AF53C5B71F40142BA59CF291E1C91DCCD92EEEEC692D5280AA4610A20C8784；24模块固定prefix-fused-release-modules，含FFN input pack4。未部署，游戏仍b4e46e15…+mh-input-mapped。脚本test-prefix-fused.ps1，日志release/HIP/prefix-fused-build/test/full/reset.log。


### 2026-09-15：prefix完全内联C32的实验暂不采用
实验c32_prefix_inline在128线程前置C32内生成原PCG/历史/RGB特征，复用scratch执行前置投影；保存投影Hrtz后的两组h8供标量残差使用，FP8副本写packed供FFN，省去prefix float中间图。使用独立prefix_rtz保留原prefix的软件截断语义；没有将残差错误替换成FP8值。

内核和测试程序编译通过。连续负载40帧ABBA、仅首尾检查：基线25.432/25.481ms，候选25.354/25.367ms，最终FEEA9EF3…匹配。收益很小，且原型的非内联分支也把tiles/history释放延后到C32调用之后，此比较不能作为严格旧版生命周期不变的提速证明。未进一步全帧/reset验证，生产源码已恢复，不采用。

同一COMGR产物中普通half C32 VGPR207、inline193，LDS均17664byte、private均0；并未出现预想的寄存器溢出，不能以寄存器压力作为无收益的已证原因。补丁experiments/prefix-inline.patch，test-prefix-inline.ps1；需应用补丁构建测试程序和C32模块，定义HIP_ISA_HALF=1/HIP_PREPACKED_WEIGHTS=1。日志release/HIP/prefix-inline-build/test.log。

保留最优固定候选native-prefix-fused.addon64/3894351c…+prefix-fused-release-modules，尚未部署。游戏仍b4e46e15…+mh-input-mapped。


### 2026-09-15：部署前置特征/投影融合候选
部署脚本确认游戏退出，安装native-prefix-fused.addon64（SHA3894351ccba9080524095cd5c87644806224c62f071250ead3c3e18526283f9b）及prefix-fused-release-modules全部24个HSACO，逐hash校验通过。再次确认900P/HIP_FAST1/ASYNC_SUBMIT1，prefix_fast为F9AF53C5…匹配，未设置覆盖默认的PREFIX_FUSED/INLINE字段。默认采用特征+投影融合，不含完全内联C32实验，含FFN input pack4。

旧b4e46e15… DLL/模块/flags/标记备份D:\DLSSNR-Lab\hip-backend\stellarblade-hip\before-prefix-fused。回退用deploy-stellarblade-update.ps1 -Restore -BackupName before-prefix-fused（游戏须退出）。本次未启动游戏、未验证实机FPS；连续负载约25.36ms仅是离线性能。当前游戏安装已更新为3894351c…，不是b4e46e15；最后明确实机900P30FPS仍对应更早13c7版。


### 2026-09-15：前置融合核硬件RTZ实验无明确收益
测试用HIP_PREFIX_RTZ_ISA将prefix的有限值Hrtz替换为v_cvt_pkrtz_f16_f32+v_cvt_f32_f16，非有限输入仍先按原实现返回x；不改变其他矩阵RNE转换。COMGR/gfx1201编译通过。

同一连续负载40帧ABBA：基线25.392/25.389ms，候选25.370/25.383ms，四轮最终FEEA9EF3…匹配且首尾有限。收益约0.01ms，不作稳定提速；未另做全域转换或全帧/reset验证，生产源码已恢复。补丁experiments/prefix-rtz.patch，test-prefix-rtz.ps1；编译时定义HIP_PREFIX_RTZ_ISA=1，日志release/HIP/prefix-rtz-build/test.log。

当前游戏及固定候选仍3894351c…+prefix-fused-release-modules，未改驱动/游戏配置。


### 2026-09-15：C32成对输入量化无稳定收益，归档
HIP_C32_PAIRED_INPUT候选把每四个输入值的4次单值FP8转换改为2次双值转换，仍先按原范围clamp、相同mapped/raw-chain/merge读取，DWORD写回布局不变。COMGR/gfx1201编译通过。

连续40帧ABBA：基线25.445/25.392ms，候选25.412/25.435ms；最终FEEA9EF3…一致，首尾有限，计时无稳定收益。未做进一步全帧/reset测试，生产源码恢复。补丁experiments/c32-paired-input.patch，test-c32-paired-input.ps1；编译定义HIP_ISA_HALF=1、HIP_PREPACKED_WEIGHTS=1、HIP_C32_PAIRED_INPUT=1。日志release/HIP/c32-paired-input-build/test.log。

当前游戏及固定候选仍3894351c…+prefix-fused-release-modules，本轮未部署。


### 2026-09-15：C32 waves-per-EU提示未改变产物资源
依据Clang官方amdgpu_waves_per_eu文档（https://clang.llvm.org/docs/AttributeReference.html#amdgpu-waves-per-eu），对C32的128线程入口试min3及min8提示；它是编译资源提示，不是运行时保证。两个COMGR候选编译通过，连续40帧ABBA最终FEEA9EF3…均匹配、首尾有限。

min3：基线25.417/25.447ms，候选25.427/25.413ms；min8：基线25.428/25.436ms，候选25.449/25.521ms。未见收益。两候选实际资源都与当前一致：普通half VGPR207、mapped/chain214、post-merge193，LDS17664byte、private0；汇编报告Occupancy7。这是编译器估计，非实测运行中的wave数量。min8并未让实际资源降至新档位，不能据此认定提高真实occupancy一定无效。

生产源码恢复。补丁experiments/c32-waves-hint.patch，test-c32-waves3.ps1/test-c32-waves8.ps1；编译定义HIP_ISA_HALF=1、HIP_PREPACKED_WEIGHTS=1及HIP_C32_MIN_WAVES=3或8。日志release/HIP/c32-waves3-build/test.log、c32-waves8-build/test.log。游戏未改，仍3894351c…+prefix-fused。


### 2026-09-15：C32 FFN byte暂存复用V区
HIP_C32_ALIAS_FFN_V默认1，仅在REGISTER_FFN路径将ffn8映射到packed的V区（128×36偏移）。Q/K阶段不覆盖此区；V阶段在本wave完成FFN输入读取后才覆盖为normalized V，最终残差使用既有saved_ffn寄存器。因该区与contract权重重叠，contract结束后额外全组barrier，再写FFN byte；不能移除此同步。0保留旧独立ffn8。

COMGR产物LDS17664→15360byte；普通half VGPR207→190，mapped214→201，post-merge193→170，private均0。连续40帧ABBA：基线25.469/25.448ms，候选25.264/25.299ms，最终FEEA9EF3…匹配，首尾有限。随后40帧全检与24帧每8帧reset全有限，最终分别匹配FEEA9EF3…/22C171FC…。全检25.207/25.419ms只记录，不与连续负载混算。

内核编译通过，packed C32模块SHA4E333BCDAB6B11CC9BE2306C0CA9112414229548B5D8B06F2325635E85E7C48F；24模块固定c32-alias-ffn-release-modules，配套DLL仍native-prefix-fused.addon64/3894351c…无需修改。未部署，游戏仍3894351c…+prefix-fused模块。脚本test-c32-alias-ffn.ps1，日志release/HIP/c32-alias-ffn-build/test/full/reset.log；编译定义HIP_ISA_HALF=1、HIP_PREPACKED_WEIGHTS=1、HIP_C32_ALIAS_FFN_V=1。


### 2026-09-15：LDS复用后再次核验8-wave提示，仍无效果
基于已验证c32-alias-ffn候选，给128线程入口增加amdgpu_waves_per_eu(8)。COMGR编译通过；资源仍为LDS15360、private0，普通half VGPR190、mapped/chain201、post-merge170，未改变配置。

连续40帧ABBA：基线25.296/25.340ms，候选25.314/25.336ms，最终FEEA9EF3…一致且首尾有限，无收益。未进行全帧/reset，不采用；生产源码未加入该提示。补丁experiments/c32-alias-waves8.patch，test-c32-alias-waves8.ps1；编译定义HIP_ISA_HALF=1、HIP_PREPACKED_WEIGHTS=1。日志release/HIP/c32-alias-waves8-build/test.log。固定候选仍c32-alias-ffn-release-modules+3894351c DLL，游戏仍prefix-fused模块。


### 2026-09-15：对角残差独立GPU对照与C32试验
新增diagonal_probe.hip和test_diagonal.cpp。读取block2/3/4/67/68/69 FFN的192个真实尺度，每尺度遍历254个有限FP8编码，共48768个组合，比较原3段×2个K16 WMMA与保留零项位置的6次标量FMA；全部bitdiff0。测试tile内各输入使用相同FP8编码，不是任意混合符号矩阵的穷举证明；同一case比较全部两列块/行结果。编译probe时将其附在c32_fused_ffn_attention.hip后，定义HIP_ISA_HALF=1/HIP_PREPACKED_WEIGHTS=1。

C32生产候选仅在matrix residual分支按输出坐标解码packed输入，保留scale_piece及三段顺序、kt零项位置，以FMA替换WMMA。连续40帧ABBA：基线25.252/25.327ms，候选25.204/25.213ms，最终FEEA9EF3…一致、首尾有限。收益不足0.1ms且基线波动相近，未进一步全帧/reset验证，生产源码恢复，不默认。

补丁experiments/c32-scalar-diagonal.patch，test-c32-scalar-diagonal.ps1；候选编译额外定义HIP_C32_SCALAR_DIAGONAL=1。日志release/HIP/diagonal-build/test.log、c32-scalar-diagonal-build/test.log。独立probe和实验内核均编译并运行成功。游戏仍3894351c…+prefix-fused，最优未部署模块仍c32-alias-ffn-release-modules。


### 2026-09-15：MH三段对角残差改为同序标量FMA
新增mh_diagonal_probe.hip/test_mh_diagonal.cpp，用普通MH 36块的6144个真实残差尺度，254个有限FP8基值×3种排列（整行同值、交替符号、按channel轮转并排除NaN编码），共4681728组合对照原WMMA与同序FMA，mismatched_cases=0。MH小尺度分解按原remain<0取符号，不混用C32的符号位版本。每128尺度一批提交，避免单次诊断过长；此为有限覆盖测试，不称任意矩阵的形式化证明。

生产HIP_MH_SCALAR_DIAGONAL默认1：仅matrix_residual分支按原输出坐标读取feature并执行原FP8量化，保留三段component和每段两个K16零项位置，以显式FMA替代WMMA。C512 scalar residual不改，crop及最终epilogue不改，0保留原矩阵路线。

连续40帧ABBA：基线25.245/25.306ms，候选24.850/24.895ms；四轮最终FEEA9EF3…匹配、首尾有限。随后40帧全读回和24帧每8帧reset全有限，最终分别匹配FEEA9EF3…/22C171FC…。全检25.110/25.238ms仅记录，不与连续负载混算。

probe、host及内核编译通过。multihead-fast-padded-wave-packed模块SHA068617327EE1706D75AAAC00D783F413B2571421633DCF974199DE5AFC923832；24模块固定mh-scalar-diagonal-release-modules，含C32 FFN/V暂存复用；配套DLL仍3894351c…无需修改，未部署。游戏仍3894351c…+prefix-fused。脚本test-mh-scalar-diagonal.ps1，日志release/HIP/mh-diagonal-build/test.log和mh-scalar-diagonal-build/test/full/reset.log。生产候选编译定义HIP_ISA_HALF=1、HIP_PREPACKED_WEIGHTS=1、HIP_MH_SCALAR_DIAGONAL=1；probe拼接C32公共函数及mh_diagonal_probe.hip。


### 2026-09-15：部署C32暂存复用及MH标量残差候选
部署脚本确认游戏退出，安装已编译native-prefix-fused.addon64（3894351ccba9080524095cd5c87644806224c62f071250ead3c3e18526283f9b）和mh-scalar-diagonal-release-modules的24个HSACO，逐hash校验通过。DLL与上版相同，内核增加C32 FFN/V暂存复用及MH同序标量残差，不包含未采用的C32标量残差或occupancy提示。维持900P/async1。

旧DLL/模块/flags/标记完整备份D:\DLSSNR-Lab\hip-backend\stellarblade-hip\before-mh-scalar-diagonal；回退使用部署脚本-Restore -BackupName before-mh-scalar-diagonal（游戏须退出）。当前候选连续负载约24.85–24.90ms，新实机FPS待验证，未宣称追平HLSL。游戏当前模块已更新，不再是原prefix-fused模块集合。


### 2026-09-15：MH投影通道特化收益过小，归档
在同序标量残差基线上，分别对matrix FP8投影及crop投影按64/128/256/512通道传入编译期常量，保留各通道的matrix/scalar残差及post epilogue；普通matrix入口的C512 fallback不改。COMGR/gfx1201编译通过。

连续40帧ABBA：基线24.822/24.850ms，候选24.740/24.771ms，最终FEEA9EF3…匹配、首尾有限；收益约0.08ms，未进一步全帧/reset，不采用，生产源码已恢复。补丁experiments/mh-project-special.patch，test-mh-project-special.ps1；编译定义HIP_ISA_HALF=1、HIP_PREPACKED_WEIGHTS=1、HIP_MH_PROJECT_SPECIAL=1。日志release/HIP/mh-project-special-build/test.log。

另核对尺寸来源：HLSL NativeActualNetwork70与HIP NativeHipNetwork均取NativeCurrentNetworkGeometry的processing_width/height，900P均1600×1024；有效区1600×900不等于工作区。未发现顶层工作尺寸不一致，不能靠降低HIP工作尺寸假称实现性能对齐。当前游戏及固定候选仍3894351c…+mh-scalar-diagonal-release-modules。


### 2026-09-15：最新两后端连续负载基准
compare-current-backends.ps1改用benchmark_hlsl_edges.exe/benchmark_prefix_fused.exe、最新已部署mh-scalar-diagonal-release-modules、明确PREFIX_FUSED=1，并为各后端校验自身参考hash。固定40帧（已有golden对应帧数），首尾检查、去前5。最初试80帧误套40帧golden被hash检查拦截，该轮不作性能结论，脚本已限制40帧。

HLSL/HIP/HIP/HLSL四轮热中位16.743/24.826/24.907/16.775ms，后端内部最终分别匹配C7C2F49D…/FEEA9EF3…，首尾有限。当前同条件差距约8.1ms，目标仍未达成；不将首尾检查称为全帧验证。日志release/HIP/backend-scalar-current.log。

桥接代码审计：D3D12Bridge用DEFAULT shared资源和HIP外部内存映射，输入/历史由GPU CopyBufferRegion复制到共享缓冲、输出由共享缓冲返回D3D，围绕HIP Enqueue使用外部信号量；未见每帧神经网络通过CPU读回整图再上传。尚未独立测桥接copy/cache代价，不能据此称桥接开销为零。游戏未改。


### 2026-09-16：HIP直接消费工作区raster，去掉入口重复副本
新增fused_prefix_values<Raster>及dlss5_prefix_fast_fused_raster，噪声/历史坐标保持，仅RGB从工作区raster按(x,y)读取。direct_prefix_input路径不再分配base/tiles副本、不启动hip_input_reflect，末端原色也直接读取原输入。原hip_input_reflect调用的有效/工作尺寸均W/H，所以此处没有丢失额外反射操作；输入合同仍要求完整工作区。900P逻辑上省两份25MiB缓冲。共享资源由bridge持有、队列在HIP完成信号前不能执行下一帧覆盖，保留原外部同步。

HIP_FAST默认开启，DLSS5_HIP_DIRECT_INPUT=0/1可覆盖，要求fast_prefix/prefix_fused。连续40帧ABBA：基线24.921/24.903ms，候选24.688/24.757ms，最终FEEA9EF3…一致、首尾有限。随后40帧全检和24帧每8帧reset全有限，最终匹配FEEA9EF3…/22C171FC…。queued_direct_input对颜色+motion两张轮换及12张轮换/淘汰，全部12帧拼接hash均43712DDE…匹配既有同步参考。

内核、runner、排队探针和完整DLL编译通过：release/HIP/native-direct-input.addon64 SHAc4a256596c1cbfb540e3877db65bcb5d9897abd0ada6b7241525258a5d4e3f87；prefix_fast模块SHA24AD5E702A81C858C1EC6CB65EBBE9E772552AB15C2619E09847E515D4E699AB；24模块固定prefix-direct-input-release-modules。未部署，游戏仍3894351c…+mh-scalar-diagonal。run-queued-frame.ps1增加Flags/Modules参数，test-prefix-direct-input.ps1和validate-direct-input.ps1复现；日志release/HIP/prefix-direct-input-build/test.log、direct-input-validation.log。


### 2026-09-16：HIP D3D raster-only边界实验暂不默认
代码确认NativeGameFrame的HIP分支只给网络传PostBase，Tiles消费者在HLSL分支。实验为NativeGameRgbInput增加emit_tiles参数（默认true），HIP测试通过DLSS5_HIP_RASTER_ONLY控制；false不分配Tiles，使用仅写post_base的专用shader，跳过null资源barrier，根签名未使用u0绑定到有效color地址。HLSL默认保持两份输出。

测试程序编译并运行成功，连续40帧ABBA：基线24.803/24.773ms，候选24.693/24.745ms，四轮最终FEEA9EF3…匹配、首尾有限。减少逻辑25MiB缓冲和每帧写入，但时间收益仅约0.03–0.11ms，未进一步全帧/reset或默认采用；生产源码恢复。

补丁experiments/rgb-raster-only.patch，shader存experiments/native_game_rgb_raster_input.hlsl，test-rgb-raster-only.ps1复现；需应用补丁并将实验shader复制到测试资产目录。测试资产只新增该专用文件，没有覆盖旧shader；游戏旧DLL不会使用新文件。日志release/HIP/rgb-raster-only-test.log。最优未部署候选仍c4a25659…+prefix-direct-input-release-modules，游戏仍3894351c…+mh-scalar-diagonal模块。


### 2026-09-16：部署HIP直接工作区输入候选
部署脚本确认游戏退出，安装native-direct-input.addon64（SHAc4a256596c1cbfb540e3877db65bcb5d9897abd0ada6b7241525258a5d4e3f87）及prefix-direct-input-release-modules的24个HSACO，逐hash校验通过。维持900P/async1，采用已完成全帧、reset及轮换/淘汰排队验证的直接raster读取；不包含未采用的D3D raster-only边界实验。

旧3894351c… DLL/模块/flags/标记备份D:\DLSSNR-Lab\hip-backend\stellarblade-hip\before-direct-input；回退用部署脚本-Restore -BackupName before-direct-input（须游戏退出）。本轮未启动游戏或测新FPS，当前约24.7ms为离线连续负载。游戏现已更新为c4a25659…，不再是3894351c版本。


### 2026-09-16：最新profile及保持64×64的QKV双输出wave实验
profile-current.ps1更新为prefix-direct-input-release-modules及全部当前快速选项，reference runner新增--direct-prefix-input。运行最终RGB7b959143…匹配；已不再有普通MH shift/crop及独立prefix features/project，QKV仍是热点之一。逐核等待累计仅用于定位，不当连续负载百分比，日志release/HIP/profile-direct-current.log。

另试QKV保持64×64输出区、256线程/8wave，每wave16×32两输出，共用A，K16顺序与row-sum不变。它不是早期M32 tile方案。首版四个C模板各分配一份LDS，COMGR报87040>65536；改由入口统一分配sa/sb/tile/inverse并传给模板后编译通过。未使用编译失败后的不完整基准。

连续40帧ABBA：基线24.737/24.708ms，候选24.974/24.931ms，最终FEEA9EF3…一致、首尾有限；更慢，不采用，生产源码恢复。补丁experiments/qkv-pair-group.patch、test-qkv-pair-group.ps1，需配套256线程host配置与实验模块；编译定义HIP_ISA_HALF=1、HIP_PREPACKED_WEIGHTS=1、HIP_QKV_PAIR_GROUP=1。日志release/HIP/qkv-pair-group-build/test.log。游戏仍c4a25659…+prefix-direct-input，未改。


### 2026-09-16：HIP head直接写共享输出实验暂不采用
RunGraph实验允许借用最终输出buffer，EnqueueRaw用非owned Allocation包装rgb_output，head直接写入并省去末尾hipMemcpyAsync；检查输出与输入/history虚拟地址区间不重叠，外部信号量及D3D转态不变。默认reference调用仍自分配输出。

测试程序编译通过，连续40帧ABBA：基线24.804/24.782ms，候选24.716/24.763ms，最终FEEA9EF3…一致、首尾有限。收益约0.02–0.09ms，未进一步全帧/排队/reset，不默认，生产源码恢复。不能把该差值直接当纯copy耗时，因为分配与访存也改变。

补丁experiments/direct-output.patch，test-direct-output.ps1（应用补丁编译benchmark_direct_output.exe，DLSS5_HIP_DIRECT_OUTPUT=0/1），模块沿用prefix-direct-input-release-modules。日志release/HIP/direct-output-test.log。游戏仍c4a25659…+prefix-direct-input，未改。本轮通过异步文字问题请求用户回报当前实机FPS和画面情况，离线工作不依赖该答复。


### 2026-09-16：MH投影单wave四输出及wave打包均不采用
新增实验projection_wave4<C>：普通C64/128/256投影每wave16×64、共用byte AV输入，保留三段标量残差、K16顺序、crop和post epilogue；C512保持旧路径。32线程版本连续40帧ABBA：基线24.747/24.792ms，候选25.289/25.302ms，更慢。随后将4个独立wave放进128线程组、虚拟bid=group*4+wave，所有运算保持，ABBA基线24.818/24.731ms、候选25.388/25.400ms，也更慢。

两组最终FEEA9EF3…均匹配、首尾有限，未进一步全帧/reset，生产源码恢复。两版本的host dispatch不同，须匹配补丁：experiments/mh-projection-wave4.patch（32线程、count/1024）或mh-projection-wavepack.patch（128线程、ceil(count/4096)）。对应test-mh-projection-wave4.ps1/test-mh-projection-wavepack.ps1；编译定义HIP_ISA_HALF=1、HIP_PREPACKED_WEIGHTS=1。日志release/HIP/mh-projection-wave4-build/test.log及mh-projection-wavepack-build/test.log。当前游戏和固定候选不变。


### 2026-09-16：MH投影lane-major权重实验不采用
在单wave四输出投影上，只重排C64/128/256 attention权重的projection区域（3*C*C），按[N16][K32][kt16][lane32][byte8]存放；每lane一次load8且跨lane连续。QKV/偏置/尺度不动，C512保持旧布局，独立cache key。CPU纯byte置换与kernel索引配套，不是此前通用[K][N] tile。

程序与COMGR内核编译通过，连续40帧ABBA：基线24.830/24.732ms，候选25.240/25.277ms，最终FEEA9EF3…一致、首尾有限；仍比现有合作LDS投影慢，未进一步全帧/reset，不默认，生产源码恢复。补丁experiments/mh-projection-lanes.patch、test-mh-projection-lanes.ps1；需配套host权重重排及32线程实验入口，编译定义HIP_ISA_HALF=1/HIP_PREPACKED_WEIGHTS=1。日志release/HIP/mh-projection-lanes-build/test.log。游戏未变。


### 2026-09-16：同实际输入隔离完整流水线与纯HIP
增加仅DLSS5_BENCH_BRIDGE_ISOLATE编译的借用诊断访问，取得Frame PostBase与原始网络输出，以及同一已初始化Network。40帧no-history完整Frame后捕获输入，上传一次到HIP私有缓冲，预热10次、40次Enqueue+同步计时，末尾读回逐float位比较；再返回完整Frame40次复核。游戏及生产DLL不启用这些接口。

完整before24.401ms、pure HIP24.269ms、完整after24.548ms；三段原始RGB bitdiff0、无非有限值。首轮额外对照24.480/24.2575ms也一致。当前no-history输入下差值约0.13–0.28ms，桥接/前后处理合计不足解释此前8ms差距；纯HIP计时仍含CPU提交和等待，不能直接叫纯GPU指令时间，也未推广到history场景。

诊断程序编译/运行通过。说明Development/HIP/bridge-isolation.md，脚本measure-bridge-isolation.ps1，日志release/HIP/bridge-isolation-summary.log。本轮无推理算法或游戏部署变化。


### 2026-09-16：同输入桥接隔离下Graph复核
measure-bridge-isolation.ps1增加Graph/Tag参数，实验flags独立写入并检查graph_stats确有重放。Graph1测得完整before24.324ms、pure HIP24.205ms、完整after24.524ms，原始RGB bitdiff0、无非有限；builds3/replays127，与三段输入/输出指针切换相符。

对照此前同程序Graph0 pure24.269ms，只有很小差异。此no-history测例中未见可由Graph消除的大量提交延迟，不能以此声称所有CPU开销为零或泛化到所有场景。Graph默认仍0，未改游戏配置。日志release/HIP/bridge-isolation-graph-summary.log，详细说明bridge-isolation.md。目标仍未达HLSL水平。


### 2026-09-16：发现并利用MH FFN收缩的精确分组零结构
inspect-weight-sparsity.ps1只读统计200个实际矩阵。FFN收缩总体零值约84%，展开/QKV约1%；连续K4中至多2个非零的条件并不覆盖任何完整矩阵，不能直接当完整2:4稀疏支持。进一步核实：36个普通MH收缩矩阵在每32个输出通道对应的128个hidden通道以外全为零；另外10个C32收缩宽度本来就是128，其分组检查是平凡成立。

新增ValidateGroupedMhContract，在未打包权重上逐值检查范围与零结构；grouped cache key独立，越界非零直接拒绝。host测试覆盖有效组、负零、分组外非零及截断数据。新_g128入口只遍历本wave所属组的128个K，原非零K16顺序和F(Hrtz)输出保持，不修改权重。C64/C128/C256收缩从K=256/512/1024缩到128，展开和第三投影不变。零项可能影响零符号，但此收缩输出的F会规范化精确零，实际测试仍做结果核验。

连续40帧ABBA：基线24.709/24.727ms，候选23.792/23.781ms，最终FEEA9EF3…匹配、首尾有限。40帧全检及24帧每8帧reset全有限，最终分别匹配FEEA9EF3…/22C171FC…。另编译reference runner，以seed123/history=input900.rgba32f运行完整当前链，最终RGB75b62d2f…匹配；该带上传读回计时不当性能基准。

HIP_FAST默认grouped_mh_contract=true，DLSS5_HIP_GROUPED_CONTRACT=0/1覆盖，reference CLI --grouped-mh-contract；旧入口保留。内核、runner、完整DLL编译通过：release/HIP/native-grouped-contract.addon64 SHAce90a942507482e559f523372e2fee91f84f9e9e3c004e473bebf8969d96ffd2；MH packed模块SHAE239DC3A1B4C5B9D9E2C2D3075FC7ED01832EED9E9E0E0A3AB58B7D96637EB7C；24模块固定mh-grouped-contract-release-modules。未部署，游戏仍c4a25659…+prefix-direct-input。

工具：inspect-weight-sparsity.ps1、test_grouped_contract_guard.cpp、test-mh-grouped-contract.ps1、validate-grouped-contract.ps1、validate-grouped-history.ps1；日志release/HIP/weight-sparsity.csv、mh-grouped-contract-build/test.log、grouped-contract-validation.log。编译MH定义HIP_ISA_HALF=1、HIP_PREPACKED_WEIGHTS=1。


### 2026-09-16：部署已验证MH分组收缩候选
部署脚本确认游戏退出，安装native-grouped-contract.addon64（SHAce90a942507482e559f523372e2fee91f84f9e9e3c004e473bebf8969d96ffd2）及mh-grouped-contract-release-modules全部24个HSACO，逐hash校验通过。再次核验900P/HIP_FAST1/ASYNC_SUBMIT1，MH模块E239DC3A…匹配，未设置覆盖默认的GROUPED_CONTRACT字段。保持原画质配置，使用载入时零结构验证。

旧c4a25659… DLL/模块/flags/标记备份D:\DLSSNR-Lab\hip-backend\stellarblade-hip\before-grouped-contract，回退用部署脚本-Restore -BackupName before-grouped-contract（须退出游戏）。未启动游戏或验证新FPS；约23.8ms是离线连续负载。当前游戏已更新为ce90a942…+分组收缩模块，目标仍未达到HLSL水平。


### 2026-09-16：普通MH FFN与QKV融合验证完成
将C64/128/256的分组FFN三段计算与QKV投影/归一化融合，复用hidden LDS为Q/K原始结果，保留FFN float输出供attention残差使用；Q/K仍按原32项顺序归一化，C512沿用旧路径。HIP_FAST默认启用，可用DLSS5_HIP_FFN_QKV和DLSS5_HIP_FFN_QKV_MAX_C覆盖。

同模块连续40帧ABBA：关闭融合23.782/23.849ms，启用至C256为22.957/22.937ms，最终图像hash均匹配FEEA9EF3…；12组中间结果对照（C64/128/256 × 映射/非映射 × 两种输入）FFN float逐位、QKV逐byte一致，无非法值。40帧全检、24帧每8帧reset全有限，最终分别匹配FEEA9EF3…/22C171FC…；seed123/history参考输出匹配75b62d2f…。日志release/HIP/ffn-qkv-all-test.log及ffn-qkv-validation.log。

候选DLL release/HIP/native-ffn-qkv.addon64已生成，SHA256=801cfc5c86458a31fd4365b6ded71b7f208ba6398ef4629fc23d3a92f069bb68；固定24模块ffn-qkv-release-modules，MH模块SHA256=D4C9A849DD9A17DB00DE6608F6AC256F77558A5F1E6E93A84D48CD837A58BDC5。此融合候选尚未部署，游戏仍为ce90a942…分组收缩版本。

用户最新实测反馈：900P现在30FPS（此前26–27FPS）；历史HLSL为1080P约37FPS。此为用户游戏观测，不将离线22.95ms换算为实际游戏FPS，也不把30FPS归到尚未部署的新融合候选。


### 2026-09-16：部署FFN/QKV融合候选并刷新profile
部署前脚本确认《剑星》退出，安装801cfc5c… DLL及ffn-qkv-release-modules共24模块，安装hash逐项通过。保持AsyncSubmit=1，旧DLL/模块/config备份D:\DLSSNR-Lab\hip-backend\stellarblade-hip\before-ffn-qkv。新游戏FPS尚未测；用户30FPS反馈属于部署前版本。

profile-current.ps1改用固定融合模块并显式启用grouped-mh-contract/ffn-qkv/max-c256，运行成功，最终RGB7b959143…匹配。日志release/HIP/profile-ffn-qkv-current.log。逐核串行计时仍含等待，不作为连续GPU耗时比例；剩余独立mh_qkv_normalize_fused共13次、mh_shift_pack13次，对应C512路径仍待分析。


### 2026-09-16：C512直接映射输入实验不采用
实验将C512的mh_shift_pack去掉，split_mix_blocked直接按原图坐标读取输入，split_projection_blocked的残差同步改为映射读取；越界补零，保留F16/FP8矩阵运算及舍入顺序。新增独立mapped入口与host dispatch，实验DLSS5_HIP_SPLIT_MAPPED=0/1切换。

COMGR与benchmark编译通过；同新模块、连续40帧ABBA：基线22.906/22.943ms，映射22.942/22.976ms，最终FEEA9EF3…一致、首尾有限。未见收益，未扩大到全帧/reset验证，生产源码恢复，游戏仍801cfc5c…融合版。

实验保存Development/HIP/experiments/split-mapped.patch及test-split-mapped.ps1，须应用补丁编译benchmark_split_mapped.exe并以HIP_ISA_HALF=1/HIP_PREPACKED_WEIGHTS=1编译deep_fast.hip为split-mapped.hsaco；测试脚本将其放入独立模块目录deep_fast-packed.hsaco。日志release/HIP/split-mapped-test.log。


### 2026-09-16：ViT QKV投影与归一化融合
新增vit_qkv_project_normalize_fused：沿用每wave16token×32channel双输出投影，Q/K将float暂存在16×33 LDS，再用原来的两次F16 WMMA归约平方和；保留平方后转F16、rsq、Q scale和最终F顺序，V直接F。省去8次独立归一化调度和全局QKV float中间缓冲。HIP_FAST默认启用，DLSS5_HIP_VIT_QKV_FUSED=0/1覆盖；reference CLI --vit-qkv-fused，要求fast_deep。

同新模块连续40帧ABBA：基线22.961/22.917ms，融合22.810/22.801ms，最终FEEA9EF3…匹配、首尾有限。8个ViT层31–38 × 零输入/两种幅度混合正负输入，共24组32token中间结果逐float位一致，无非法值。40帧全检及24帧每8帧reset全有限，最终分别匹配FEEA9EF3…/22C171FC…；seed123/history参考输出匹配75b62d2f…。全检读回频率不同，其22.669/22.657ms不作为额外提速证据。

完整DLL编译成功：release/HIP/native-vit-qkv-fused.addon64 SHA256=13d4dc10e6507eb05120b056a6710498e4922a53e699dd6878be2674614ae821；固定24模块vit-qkv-fused-release-modules，deep_fast-packed模块SHA256=5AFA30B02A10269FD6AE69D68DAD542C5432150E58EC5ACC63D54A48BFC0FEE7。未部署，游戏仍801cfc5c… FFN/QKV融合版。

脚本test-vit-qkv-fused.ps1、test_vit_qkv.cpp、validate-vit-qkv.ps1、validate-vit-qkv-history.ps1；日志release/HIP/vit-qkv-fused-test.log、vit-qkv-validation.log。编译deep_fast.hip时HIP_ISA_HALF=1/HIP_PREPACKED_WEIGHTS=1。


### 2026-09-16：部署ViT QKV融合并刷新profile
部署脚本确认游戏退出，安装native-vit-qkv-fused.addon64（SHA13d4dc10e6507eb05120b056a6710498e4922a53e699dd6878be2674614ae821）和vit-qkv-fused-release-modules全部24模块，逐hash校验通过。AsyncSubmit保持1，旧版本备份D:\DLSSNR-Lab\hip-backend\stellarblade-hip\before-vit-qkv-fused。新实机FPS未测。

profile-current.ps1改为固定ViT融合模块并显式传--vit-qkv-fused，运行通过，最终RGB7b959143…匹配。日志release/HIP/profile-vit-qkv-current.log。注意日志包含两轮，不能混排后当单轮汇总；串行逐核计时包括等待，只用于定位。MH attention及crop投影仍居前。


### 2026-09-16：MH AV双输出共用概率读取实验不采用
在mh_attention_fused_fp8_out最后AV阶段，将两次16输出通道循环改为两组累加器共同遍历K16，复用概率矩阵操作数读取，各自K16累加顺序、输出F、布局及同步保持。COMGR编译通过。

连续40帧ABBA：固定ViT融合基线22.792/22.812ms，候选22.862/22.831ms；最终FEEA9EF3…匹配、首尾有限。未见收益，未做额外全帧/reset，生产源码恢复，游戏仍13d4dc10… ViT融合版。该结果不能单独归因为寄存器压力，需要ISA/资源证据。

实验补丁experiments/mh-av-pair.patch、test-mh-av-pair.ps1，编译multihead_fused_attention.hip定义HIP_ISA_HALF=1为mh-av-pair.hsaco，再放到独立目录的multihead_fused_attention.hsaco。使用benchmark_vit_qkv_fused.exe及vit-qkv-validation-flags.txt，日志release/HIP/mh-av-pair-test.log。


### 2026-09-16：MH AV-pair编译指令对照
重新以HIP_ISA_HALF=1编译当前MH源码，并读取上轮候选.s，逐函数比较mh_attention_fused_fp8_out。当前VGPR103、候选104，SGPR均20、LDS均15360、private segment与spill均0。两版静态LDS读写、WMMA及barrier指令数量相同；其中ds_load_u8均64、ds_load_2addr_b32均20。不能把源码共用读取当实际减少加载，也不能用VGPR差1单独归因负优化。

可审阅表Development/HIP/experiments/mh-av-pair-isa.md。本轮未改推理内核/游戏。回查历史V转置已测无收益，避免重复。


### 2026-09-16：MH crop投影K64分块实验不采用
回查通道特化已做过，未重复。仅fast_dense的Crop实例由K32扩大到K64，合作装载两轮DWORD、LDS stride9→17，同步次数减半，四次K16 WMMA保持累加顺序；普通QKV/其他dense保持K32。与旧Normalize K64实验不同，此实例不含归一化临时LDS。

COMGR编译通过；mh_attention_crop资源：LDS8704 bytes、VGPR43、SGPR34、private segment0、spill0。连续40帧ABBA：基线22.799/22.820ms，候选22.823/22.802ms，最终FEEA9EF3…匹配、首尾有限。未见收益，未扩大全帧/reset验证，生产源码恢复，游戏仍13d4dc10… ViT融合版。

补丁experiments/mh-crop-k64.patch、test-mh-crop-k64.ps1，编译定义HIP_ISA_HALF=1/HIP_PREPACKED_WEIGHTS=1，模块名multihead-fast-padded-wave-packed.hsaco。日志release/HIP/mh-crop-k64-test.log。


### 2026-09-16：刷新当前全帧及逐层HLSL/HIP对照
compare-current-backends.ps1更新至vit-qkv-fused-release-modules及明确启用当前融合选项，使用benchmark_vit_qkv_fused.exe。连续40帧edges-only ABBA：HLSL16.782/16.799ms，HIP22.847/22.816ms；各自最终C7C2F49D…/FEEA9EF3…匹配、首尾有限，整帧差距仍约6ms，未达目标。

compare_layers.cpp的fused-selected过滤模式补上grouped_mh_contract/ffn_qkv/max256/vit_qkv_fused；MH与C32脚本均更新固定模块。重新编译与运行通过。代表性单层ABBA wall：C64 block5 HIP0.350/0.320ms、HLSL0.334/0.283；位移block6 HIP0.389/0.380、HLSL0.339/0.301；C128 block9 HIP0.180/0.178、HLSL0.238/0.217；C256 block15 HIP0.161/0.157、HLSL0.215/0.197；C512 block23 HIP0.218/0.198、HLSL0.151/0.135。block5/23逐位一致，其余跨后端bitdiff分别330/496/129、maxabs0.5/2/2，无非有限值；不能声称全网络跨后端数值相同。

C32 mode2 raw half-chain测试：block70 HIP1.729/1.714ms、HLSL1.087/1.117；block1 HIP0.463/0.402、HLSL0.316/0.266；block4 HIP0.552/0.418、HLSL0.390/0.267，三者输出逐位一致。block70这里测原始stage本体，不是实际postmerge融合完整路径。单层重复调度/时钟条件与整网不同，不能直接将差值相加归因6ms。

日志release/HIP/backend-vit-current.log、compare-mh-fused-current.log、compare-c32-vit-current.log。生产游戏未更换。


### 2026-09-16：C32 V结果直接FP8写出实验不采用
在REGISTER_FFN路径单独处理QKV的V：两组16列结果保留寄存器，完成本wave全部FFN读取后直接写packed V，省去scratch.raw的写回与再读取；保留owned-row同步以及后续全window同步，避免FFN/V别名覆盖未读值。Q/K归一化及FP8/累加顺序不变。

COMGR编译通过；连续40帧ABBA基线22.823/22.824ms、候选22.853/22.862ms，最终FEEA9EF3…一致、首尾有限。略慢，不采用，未扩大全帧/reset，生产源码恢复，游戏仍13d4dc10… ViT融合版。

实验experiments/c32-direct-v.patch、test-c32-direct-v.ps1，编译c32_fused_ffn_attention.hip定义HIP_ISA_HALF=1/HIP_PREPACKED_WEIGHTS=1，模块为c32_fused_ffn_attention-packed.hsaco。日志release/HIP/c32-direct-v-test.log。


### 2026-09-16：C32 FFN直接读取权重取得约0.9ms收益
对照shaders/native_c32_ffn_fused.hlsli发现HLSL展开/收缩矩阵直接从权重缓冲Load，HIP先合作搬到LDS。当前改为matrix8(fw+512,…)和matrix8(fw+4608,…)直接读取，删除两段权重LDS装载及一处由此产生的同步；输入/hidden/FFN-V布局、矩阵顺序和舍入保持。保留已验证的contraction→FFN阶段barrier。

COMGR编译通过；连续40帧ABBA：基线22.782/22.847ms，候选21.916/21.927ms，最终FEEA9EF3…匹配、首尾有限。全40帧及24帧每8帧reset全有限，最终分别匹配FEEA9EF3…/22C171FC…；seed123/history匹配75b62d2f…。全检计时读回频率不同，不额外声称21.332ms为连续性能。C32 block70/1/4 × mode0/1/2共9组输出与HLSL逐位一致、无非法值；验证脚本补上CHECK数量与bitdiff/invalid/maxabs强制检查。

固定24模块c32-global-ffn-release-modules，c32_fused_ffn_attention-packed.hsaco SHA256=4F2B207DF5FE199EA2E4BE3868F8F8BDAC9259E83CE8E38F6BA94DC8BD461FDE。仅内核改动，配套DLL沿用13d4dc10…，本轮未部署，游戏仍旧ViT融合模块。脚本test-c32-global-ffn.ps1、validate-c32-global-ffn.ps1、validate-c32-global-ffn-history.ps1；日志release/HIP/c32-global-ffn-test.log、c32-global-ffn-validation.log。


### 2026-09-16：部署C32直接权重候选，局部同步实验暂不采用
部署脚本确认游戏退出，将c32-global-ffn-release-modules全部24模块安装并逐hash通过，DLL沿用13d4dc10e6507eb05120b056a6710498e4922a53e699dd6878be2674614ae821。旧模块/config/DLL备份D:\DLSSNR-Lab\hip-backend\stellarblade-hip\before-c32-global-ffn，AsyncSubmit保持1。实机新FPS未测。

随后仅在独立候选中将FFN展开后、contract→FFN、FFN→QKV的三处整组同步换为已有sync_owned_rows，保留合作输入载入及注意力跨wave读之前的整组同步。当前直接权重路径不再共享contract权重，各wave负责16行，hidden stride132与raw stride33float对应相同字节分区。

COMGR编译通过；连续40帧ABBA：基线21.939/21.940ms、候选21.900/21.912ms，最终FEEA9EF3…一致、首尾有限。仅约0.03ms收益，未进一步全帧/reset，不纳入当前版本，生产源码恢复。补丁experiments/c32-local-ffn.patch、test-c32-local-ffn.ps1，编译定义HIP_ISA_HALF=1/HIP_PREPACKED_WEIGHTS=1，日志release/HIP/c32-local-ffn-test.log。游戏仍已部署的直接权重版。


### 2026-09-16：C32分半隐藏层计算实验不采用
参照HLSL ffn_fused的两半结构，先初始化残差累加器，再每次展开64个hidden通道并立即收缩，hidden stride132→68、重复使用64通道暂存。收缩仍依次K0–127，保留各舍入及阶段同步。scratch union受raw/ex大小限制，总LDS未减少。

COMGR编译通过；连续40帧ABBA基线21.888/21.943ms，候选22.739/22.787ms，最终FEEA9EF3…一致、首尾有限。明显更慢，不采用，未扩大全帧/reset，生产源码恢复。half_chain汇编资源基线VGPR190、候选180；二者SGPR46、LDS15360、private/spill0，因此不能把慢归因于VGPR增长或spill，尽管累加器生存区间发生变化。

补丁experiments/c32-half-hidden.patch、test-c32-half-hidden.ps1，编译定义HIP_ISA_HALF=1/HIP_PREPACKED_WEIGHTS=1，日志release/HIP/c32-half-hidden-test.log。游戏仍已部署c32-global-ffn-release-modules+13d4dc10… DLL，离线约21.9ms。


### 2026-09-16：C32 raw-chain固定模式入口无收益
核对生产C32 prev路径固定传mode3/raw1；新增独立half_chain3入口把这两个值以常量传入融合body，host仅该调用换入口，旧通用入口保留。COMGR与专用benchmark编译通过。

连续40帧ABBA：基线21.905/21.892ms，候选21.938/21.861ms，最终FEEA9EF3…一致、首尾有限；没有稳定收益，未扩大全帧/reset，不采用，生产源码恢复。游戏仍c32-global-ffn-release-modules+13d4dc10… DLL。

补丁experiments/c32-chain3.patch、test-c32-chain3.ps1；应用补丁编译benchmark_c32_chain3.exe及HIP_ISA_HALF=1/HIP_PREPACKED_WEIGHTS=1内核c32-chain3.hsaco，host与新增入口配套。日志release/HIP/c32-chain3-test.log。


### 2026-09-16：MH crop投影直接权重读取不采用
保留原64×64输出tile、512线程、共享A输入和K32分块，仅Crop+packed实例取消B权重LDS写入，WMMA直接读取对应两DWORD。与以前单wave多输出/全局lane-major布局实验不同，本轮不改工作组与矩阵布局。非Crop及非packed路径保持。

COMGR编译通过；连续40帧ABBA基线21.969/21.881ms、候选22.055/22.064ms，最终FEEA9EF3…一致、首尾有限。更慢，不采用，未扩大全帧/reset，生产源码恢复，游戏仍c32-global-ffn-release-modules+13d4dc10… DLL。C32直接权重收益不能泛化到当前MH投影。

补丁experiments/mh-direct-b.patch、test-mh-direct-b.ps1，编译定义HIP_ISA_HALF=1/HIP_PREPACKED_WEIGHTS=1，候选multihead-fast-padded-wave-packed.hsaco；日志release/HIP/mh-direct-b-test.log。


### 2026-09-16：C512 mix FP8指令探测不采用
读取真实block23-ffwd.f32，mix262144/expand131072/contract131072个float逐值检查均可由有限E4M3精确表示。仅此block的权重检查，未证明所有C512输入/层都适用。

实验只将split_mix_blocked的F16操作数改为现场pack FP8后用FP8 WMMA，仍读取float权重与float输入、K16顺序不变。COMGR编译通过。连续40帧ABBA基线21.904/21.893ms，候选22.059/22.083ms，最终FEEA9EF3…一致、首尾有限；更慢，未扩大全帧/reset/所有权重检查，生产源码恢复，游戏不变。

补丁experiments/split-mix-fp8.patch、test-split-mix-fp8.ps1，编译定义HIP_ISA_HALF=1/HIP_PREPACKED_WEIGHTS=1，模块deep_fast-packed.hsaco，日志release/HIP/split-mix-fp8-test.log。该结果包含现场操作数打包成本，不能代表预打包权重/byte输入布局的性能。


### 2026-09-16：C512 mix预打包权重实验暂不采用
新增独立split_mix_fp8_packed入口，保留float输入现场FP8转换，权重使用PackedDeepWeight(ffwd,262144)一次精确编码并缓存，读取两DWORD作为WMMA B。原始ffwd缓存仍供后续F16展开/收缩使用，打包缓存@fp8独立。沿用ExactWeightFp8逐值检查，运行所经过的mix矩阵均通过，未把此检查扩称所有展开/收缩或任意输入已验证。

COMGR及benchmark编译通过，连续40帧ABBA基线21.886/21.945ms，候选21.925/21.834ms；最终FEEA9EF3…一致、首尾有限。无稳定可见优势，不纳入生产，未扩大全帧/reset，源码恢复。相对上一轮现场打包两操作数不再明显慢，但不同轮次不能直接当精确收益。

补丁experiments/split-mix-packed.patch、test-split-mix-packed.ps1，需配套编译benchmark_split_mix_packed.exe和HIP_ISA_HALF=1/HIP_PREPACKED_WEIGHTS=1内核split-mix-packed.hsaco，目标deep_fast-packed.hsaco；日志release/HIP/split-mix-packed-test.log。游戏仍c32-global-ffn-release-modules+13d4dc10… DLL。


### 2026-09-16：当前直接权重版C32与整网基线复核
四个current脚本（backends/c32/mh/profile）统一指向c32-global-ffn-release-modules，避免继续使用旧ViT融合模块。当前C32 mode2逐层：block70 HIP1.358750/1.358900ms、HLSL1.086200/1.104750ms；block1 HIP0.412750/0.332300、HLSL0.333650/0.272550；block4 HIP0.411350/0.337200、HLSL0.369350/0.276800。三个输出逐位一致，无非法值。block70仍为stage本体，不是postmerge整条路径；单层重复测量有时钟/调度波动，不将各差值直接相加。

同条件连续40帧edges-only ABBA：HLSL16.800/16.789ms，HIP21.898/21.927ms，各自最终C7C2F49D…/FEEA9EF3…匹配、首尾有限。当前整帧差距约5.1ms，目标未达成。本轮复核已有收益，无新内核改动或部署。

日志release/HIP/compare-c32-global-current.log、backend-c32-global-current.log。已异步请求用户下次试玩反馈900P实际FPS及画面异常，离线工作不依赖回复。


### 2026-09-16：C32 raw输入移除重复FP8往返实验暂不采用
mapped_c32_input的Raw分支原先F(half)返回float，再由输入暂存的fp8重新编码。实验增加ForPacking模板参数，仅输入装载路径返回保留零规范化和有符号饱和的原half值，让外层完成一次编码；残差读取保持旧F。未改变Merge/raster分支。

COMGR编译通过；连续40帧ABBA基线21.924/21.949ms，候选21.914/21.878ms，最终FEEA9EF3…一致、首尾有限。仅约0.04ms差异，未扩大全帧/reset/全half编码对照，不纳入生产，源码恢复。补丁experiments/c32-input-single-cast.patch、test-c32-input-single-cast.ps1；编译HIP_ISA_HALF=1/HIP_PREPACKED_WEIGHTS=1，日志release/HIP/c32-input-single-cast-test.log。游戏与最快固定模块不变。


### 2026-09-16：C32输入两两FP8打包实验暂不采用
输入DWORD打包由四次fp8(value,0)改为两次cvt_pk_fp8(value0,value1)，保留每值clamp、mapped读取与字节顺序；不合并上轮单次cast实验。COMGR编译通过，连续40帧ABBA基线21.883/21.950ms、候选21.875/21.927ms，最终FEEA9EF3…一致、首尾有限。无明显收益，未进一步全帧/reset，源码恢复，游戏不变。

补丁experiments/c32-input-pair.patch、test-c32-input-pair.ps1，编译HIP_ISA_HALF=1/HIP_PREPACKED_WEIGHTS=1，日志release/HIP/c32-input-pair-test.log。此结果不等价于已证明动态指令数量变化；只表明本次源代码改写未带来明显全帧改善。


### 2026-09-16：整网MH分组绕过诊断定位C64
新增measure-mh-ablation.ps1，只在独立flags/benchmark临时绕过残差块，生产配置不变。C64=5–8/62–65，C128=9–14/56–61，C256=15–22/48–55，C512=23–30/40–47；保留原42/43/46跳块。绕过改变网络输出，结果不可当画质保持的优化或实际FPS提升。

HIP前后完整基线21.904/21.955ms；绕过C64/C128/C256/C512后分别18.840/19.515/19.305/19.185ms。HLSL前后基线16.754/16.795ms；绕过C64/C128/C256后15.165/14.820/14.339ms。两边完整基线各自golden匹配，所有成功测例首尾有限；绕过输出hash变化属预期，不做跨后端一致性声明。

HLSL C512绕过被已有布局检查拒绝：skip block23 input/output layouts differ。未绕过该检查，脚本显式标记不支持该组；第一次运行在此终止，随后仅补测baseline-after并通过。不能将缺失项当0。

以两端基线均值粗估：C64组HIP差约3.09ms/HLSL约1.61ms，C128约2.41/1.95ms，C256约2.62/2.44ms。绕过会改变后续数据、缓存与调度，不能简单加总或当纯核耗时；但C64是后续重点。日志release/HIP/mh-ablation-current.log、mh-ablation-hlsl-current.log。


### 2026-09-16：补C64解码层并纠正比较工具格式
compare_layers.cpp新增62–65（400×256，shift0/3/1/2）和c64-only过滤，compare-current-c64.ps1运行当前固定模块。首次统一f32输入/FP8输出测得block65大量差异；核对native_decoder_tail69.h发现实际63–65接FP8输入、65输出float。工具改为给63–65上传精确FP8输入，65按float读出，不改任何生产推理代码。

修正后block63/65逐位一致；block62 bitdiff198/maxabs0.5、64 bitdiff159/maxabs0.25，均无非有限值。62仍使用明确的raster f32样本；未声称覆盖实际up62可选f16接口。旧block65差异不作为算法错误证据。

单层ABBA wall：62 HIP0.321350/0.313300ms，HLSL0.328150/0.291700；63 HIP0.387800/0.386350，HLSL0.322800/0.283100；64 HIP0.372900/0.367700，HLSL0.319350/0.279400；65 HIP0.375150/0.369000，HLSL0.346350/0.300100。HLSL首次/末次仍有时钟或调度变化，不能把整网绕过差额与逐层值直接相加。

编译/运行通过，日志release/HIP/compare-c64-decoder-current.log（格式未修正）及compare-c64-decoder-formats.log（修正后）。本轮定位与工具修正，无新增性能收益、无游戏部署。


### 2026-09-16：仅C64启用单wave四输出投影仍慢
依据C64解码器phase诊断，复用旧projection_wave4实现但仅c==64且FP8 AV启用，其他C128/256/512保持当前投影。单wave16token×64输出，32线程、count/1024 dispatch，保留三段标量残差、K16顺序与crop/epilogue。区别于旧全通道实验，单独检查C64是否曾被其他通道退化掩盖。

COMGR及专用benchmark编译通过；连续40帧ABBA基线21.898/21.953ms，候选22.075/22.054ms，最终FEEA9EF3…一致、首尾有限。仍更慢，不采用，未扩大全帧/reset，源码恢复，游戏不变。

补丁experiments/c64-projection-wave4.patch、test-c64-projection-wave4.ps1，配套benchmark_c64_projection_wave4.exe与HIP_ISA_HALF=1/HIP_PREPACKED_WEIGHTS=1内核，模块multihead-fast-padded-wave-packed.hsaco；日志release/HIP/c64-projection-wave4-test.log。


### 2026-09-16：MH残差三段尺度预计算实验暂不采用
给packed attention缓存增加独立-residual3 key，验证原长度并在末尾追加3*C个float。加载后在同HIP stream运行mh_prepare_residual，逐通道沿原F/小尺度规则分解三段；matrix_residual投影每帧直接读这三段，乘加顺序不变。实验仅配套packed benchmark，未补齐非packed生产兼容，不直接交付。

COMGR和专用benchmark编译通过；连续40帧ABBA基线21.875/21.911ms，候选21.876/21.822ms，最终FEEA9EF3…一致、首尾有限。仅约0.04ms差异，未扩大全帧/reset/Graph验证，不采用，源码恢复，游戏不变。

补丁experiments/mh-residual-precompute.patch、test-mh-residual-precompute.ps1；配套benchmark_mh_residual_precompute.exe及HIP_ISA_HALF=1/HIP_PREPACKED_WEIGHTS=1内核，模块multihead-fast-padded-wave-packed.hsaco。日志release/HIP/mh-residual-precompute-test.log。


### 2026-09-16：C64注意力与输出投影融合取得约0.55ms收益
新增c64_attention_project，256线程每组处理一个8×8窗口的两个head。各head沿用原score/prob/AV操作，AV复用已失效的ex共享空间存64×68 byte；全组同步后进行跨64通道投影，保留三段标量残差、K16顺序、crop及post0/3/4。共享QKV/ex按head隔离，AV写入前已有整组同步，避免覆盖仍在读取的ex。host仅C64+packed+fused MH+byte norm/AV启用；其他路径沿用旧实现。

COMGR与benchmark编译通过；连续40帧ABBA基线21.899/21.892ms，候选21.364/21.312ms，最终FEEA9EF3…匹配、首尾有限。全40帧及24帧每8帧reset全有限，最终分别FEEA9EF3…/22C171FC…；seed123/history输出75b62d2f…匹配。全检计时20.955/21.132ms因读回频率不同不作为连续性能。

独立test_c64_attn_project覆盖8个真实C64权重块×post0/3/4×有无crop共48组，融合前后输出逐float位一致、无非法值。首次测试因mh_wave配置缺失在初始化即拒绝，补齐后实际全部通过；没有放宽比较。日志release/HIP/c64-attn-project-test.log、c64-attn-project-validation.log、c64-attn-project-intermediate.log。

完整DLL release/HIP/native-c64-attn-project.addon64 SHA256=2d8d1db1c67de875bb2bbca1809aed6cc74ac57f2c024c3e2637e40d2f886ca8；固定24模块c64-attn-project-release-modules，multihead_fused_attention.hsaco SHA256=EEAC76C3B08EDFD273F7A27EC7675466E0F19E63CFA94D70709F1C7DE7B331D5。本轮未部署，游戏仍c32-global-ffn-release-modules+13d4dc10… DLL。目标仍未达HLSL约16.8ms水平。


### 2026-09-16：部署C64注意力投影融合并核对当前差距
部署脚本确认游戏退出，安装native-c64-attn-project.addon64（SHA2d8d1db1c67de875bb2bbca1809aed6cc74ac57f2c024c3e2637e40d2f886ca8）及c64-attn-project-release-modules全部24模块，逐hash校验通过。AsyncSubmit保持1；旧DLL/模块/config备份D:\DLSSNR-Lab\hip-backend\stellarblade-hip\before-c64-attn-project。尚无新版实际游戏FPS反馈。

当前比较/profile脚本统一更新模块与benchmark_c64_attn_project.exe，compare_layers_current.exe重新编译上传以匹配融合host路径。连续40帧edges-only ABBA：HLSL16.792/16.783ms，HIP21.309/21.335ms，各自最终golden匹配、首尾有限；当前差距约4.54ms，未达目标。日志release/HIP/backend-c64-fused-current.log。

COMGR资源元数据c64_attention_project：LDS30720bytes、VGPR94、SGPR30、private segment0、spill0。仅资源证据，不直接推断GPU利用率。


### 2026-09-16：C128四head注意力投影融合通过
新增c128_attention_project，512线程处理8×8窗口四个head，AV复用ex为64×132 byte，再进行128通道投影；保留原概率/三段残差/累加顺序及crop/post。C64融合保持，host在packed+fused byte路径增加C128分派。COMGR资源LDS61440bytes、VGPR118、SGPR30、private/spill0。

连续40帧ABBA：首轮基线21.317ms，候选21.119/21.115ms，完整日志release/HIP/c128-attn-project-test.log；所有最终FEEA9EF3…匹配、首尾有限。全40帧及24帧每8帧reset全有限，最终分别FEEA9EF3…/22C171FC…；seed123/history75b62d2f…匹配。全检计时20.684/21.059ms读回节奏不同，不作为连续性能。

12个C128块9–14/56–61 × post0/3/4 × 有无crop共72组融合前后逐float位一致，无非法值。日志release/HIP/c128-attn-project-validation.log。完整DLL编译成功：release/HIP/native-c128-attn-project.addon64 SHAae66d4c8e3730424c41a0a935fb9668465547aa1d15b848d4712209eb0d681b6；固定24模块c128-attn-project-release-modules，MH attention模块SHA1B307FB5DC7036E396ECBCCCA5A978B435B6A79479DA302CD5F16CE4F967523E。

本轮未部署，游戏仍C64融合2d8d1db1…+c64-attn-project-release-modules。约0.2ms收益保留，目标尚未达到HLSL水平。


### 2026-09-16：部署C128融合并刷新基线
部署脚本确认游戏退出，安装native-c128-attn-project.addon64（SHAae66d4c8e3730424c41a0a935fb9668465547aa1d15b848d4712209eb0d681b6）与c128-attn-project-release-modules全部24模块，逐hash通过，AsyncSubmit保持1。旧DLL/模块/config备份D:\DLSSNR-Lab\hip-backend\stellarblade-hip\before-c128-attn-project。实际游戏新FPS未测。

current比较/profile脚本切换固定C128融合模块和benchmark_c128_attn_project.exe，逐层比较工具重新编译上传。连续40帧edges-only ABBA：HLSL16.799/16.794ms，HIP21.117/21.119ms，各自golden匹配、首尾有限；差距约4.32ms，仍未达目标。日志release/HIP/backend-c128-fused-current.log。


### 2026-09-16：C128两头分批复用工作区更慢，撤回
实验将四head同时处理改为两head×两批，256线程，共用2head ex/packed工作区，AV独立64×132byte跨批保存，每批末同步后复用工作区；投影每wave负责16token×64通道四片结果。COMGR与专用benchmark编译通过。

资源LDS61440→39168bytes，VGPR118→151，private segment0→160bytes，metadata VGPR/SGPR spill count仍0，不能把private segment直接等同寄存器溢出计数。连续40帧ABBA基线21.092/21.109ms，候选21.399/21.324ms，最终FEEA9EF3…匹配、首尾有限。更慢，不采用，未扩大全帧/reset，源码恢复，游戏仍ae66d4c8…+c128-attn-project-release-modules。

补丁experiments/c128-batched-heads.patch、test-c128-batched-heads.ps1；需配套256线程host编译benchmark_c128_batched_heads.exe及HIP_ISA_HALF=1内核，目标multihead_fused_attention.hsaco。日志release/HIP/c128-batched-heads-test.log。


### 2026-09-16：C128分批投影循环展开消除private segment但更慢
在上轮两头分批候选基础上对投影四片结果的j循环显式unroll，COMGR编译通过。c128_attention_project的private segment160→0bytes，VGPR151→180，LDS仍39168，spill metadata仍0；说明循环表达影响累加器存放，但不能据此预判性能。

连续40帧ABBA相对当前四头并行基线21.107/21.110ms，候选21.701/21.692ms，最终FEEA9EF3…一致、首尾有限。明显更慢，未扩大全帧/reset，不采用，生产源码恢复。游戏仍ae66d4c8…+c128-attn-project-release-modules。

补丁experiments/c128-batch-unroll.patch包含完整分批host/kernel变化；脚本test-c128-batch-unroll.ps1复用benchmark_c128_batched_heads.exe（256线程host），编译内核HIP_ISA_HALF=1，模块multihead_fused_attention.hsaco。日志release/HIP/c128-batch-unroll-test.log。


### 2026-09-16：C128四头并行复用指数空间取得约0.13ms收益
保持512线程四head并行，Q/K改为直接全局读取，LDS仅保留V；概率先编码到每线程8个uint，整组同步后写入已失效ex，每head概率64×68bytes。AV两片先存寄存器，整组确认所有概率读取完成后，才覆盖all_ex为全通道AV，再同步投影。保留原矩阵/归约/舍入顺序，避免指数→概率→跨head AV三次生命周期重叠。

COMGR资源LDS61440→43008bytes（60→42KiB），VGPR仍118，SGPR30，private/spill0。连续40帧ABBA基线21.065/21.087ms，候选20.937/20.960ms，各最终FEEA9EF3…匹配、首尾有限。72组C128逐位对照全部一致无非法值；全40帧及24帧每8帧reset全有限，最终FEEA9EF3…/22C171FC…，seed123/history75b62d2f…匹配。全检20.850/20.942ms不当额外连续收益。

固定24模块c128-reuse-ex-release-modules，multihead_fused_attention.hsaco SHA1AE47F2EAB4843A531EC72966AE4B1F61315D59858A604FAFA1A5E582ADDF503；仅内核变化，配套DLL沿用ae66d4c8…，本轮未部署。游戏仍c128-attn-project-release-modules。脚本test-c128-reuse-ex.ps1、validate-c128-reuse-ex.ps1、validate-c128-reuse-ex-history.ps1，复用已编译test_c128_attn_project.exe；日志release/HIP/c128-reuse-ex-test.log及c128-reuse-ex-validation.log。


### 2026-09-16：部署C128复用版并完成C64复用验证
部署脚本确认游戏退出，安装c128-reuse-ex-release-modules全部24模块并逐hash通过，DLL沿用ae66d4c8e3730424c41a0a935fb9668465547aa1d15b848d4712209eb0d681b6，AsyncSubmit保持1。旧版本备份D:\DLSSNR-Lab\hip-backend\stellarblade-hip\before-c128-reuse-ex。current脚本更新此固定模块；整帧ABBA HLSL16.756/16.815ms，HIP20.923/20.946ms，各自golden匹配，日志release/HIP/backend-c128-reuse-current.log。

随后独立C64候选沿用C128的Q/K直接读取、ex→prob→AV生命周期复用，保持两个head并行，LDS30720→21504bytes（30→21KiB），VGPR94/SGPR30不变、private/spill0。连续40帧ABBA基线20.946/20.951ms，候选20.792/20.774ms，最终FEEA9EF3…匹配、首尾有限。48组C64逐位对照全部一致无非法值；全40帧及24帧每8帧reset全有限，最终FEEA9EF3…/22C171FC…；seed123/history75b62d2f…匹配。全检21.029/21.027ms读回节奏不同，不与连续计时混用。

固定24模块c64-reuse-ex-release-modules，MH attention模块SHA233EC7A820DA34EF9CC5F1F7375EC916212DAC746F63A3A6E7D10FAE5C46A558，DLL仍可沿用ae66d4c8…。新C64复用候选尚未部署；游戏当前仅C128复用。脚本test-c64-reuse-ex.ps1、validate-c64-reuse-ex.ps1、validate-c64-reuse-ex-history.ps1，日志release/HIP/c64-reuse-ex-test.log及c64-reuse-ex-validation.log。


### 2026-09-16：部署C64复用版并刷新逐核profile
部署脚本确认游戏退出，安装c64-reuse-ex-release-modules全部24模块并逐hash通过，DLL沿用ae66d4c8e3730424c41a0a935fb9668465547aa1d15b848d4712209eb0d681b6，AsyncSubmit保持1。旧DLL/模块/config备份D:\DLSSNR-Lab\hip-backend\stellarblade-hip\before-c64-reuse-ex。实机新FPS未测。

current比较/profile脚本统一更新固定模块。profile运行通过，最终RGB7b959143…匹配；第二轮独立mh_attention_fused_fp8_out及mh_attention_crop各29次，对应C256的16块+C512的13块。c64_attention_project8次，C64/C128融合已进入当前实际路径。日志release/HIP/profile-c64-reuse-current.log；逐核串行计时含等待，仅定位，不当连续耗时比例。本轮无新增内核改动。


### 2026-09-16：C256两批注意力+投影融合通过，约0.23ms收益
新增c256_attention_project，512线程、每批4head共两批，沿Q/K直接读及ex→prob复用，独立AV64×260bytes跨批保存，LDS59648bytes。初版四片投影同时存活，private160/VGPR124；ABBA基线20.800/20.814ms，初版20.743/20.684ms，小幅收益。随后改成两对投影依次计算，保持各列K16顺序及三段残差，private降为0，VGPR152/SGPR42、spill0，LDS不变。

最终连续40帧ABBA基线20.784/20.809ms，候选20.573/20.551ms，最终FEEA9EF3…匹配、首尾有限。16个C256权重块15–22/48–55 × post0/3/4 × crop开关共96组逐位对照全部一致无非法值；全40帧及24帧每8帧reset全有限，最终FEEA9EF3…/22C171FC…；seed123/history75b62d2f…匹配。全检21.693/21.010ms读回节奏不同，不当连续性能。

完整DLL release/HIP/native-c256-attn-project.addon64 SHA57b8ab453ffc21bde177d080d382ff1c47eee0a5fe085bf973b552f7a8d1b182；固定24模块c256-attn-project-release-modules，MH attention模块SHA88B85C7DB6C5F0258031975E9902BE165009EF9858C0C025652B89A5D4ED5935。未部署，游戏仍ae66d4c8…+c64-reuse-ex-release-modules。

脚本test-c256-attn-project.ps1对应初版（由experiments/c256-four-acc.patch在最终源码上还原），test-c256-projection-pairs.ps1及validate-c256-projection-pairs*.ps1对应最终版，test_c256_attn_project.cpp做中间对照。日志release/HIP/c256-attn-project-test.log、c256-projection-pairs-test.log、c256-projection-pairs-validation.log。


### 2026-09-16：部署C256融合并刷新当前整网差距
部署脚本确认游戏退出，安装native-c256-attn-project.addon64（SHA57b8ab453ffc21bde177d080d382ff1c47eee0a5fe085bf973b552f7a8d1b182）和c256-attn-project-release-modules全部24模块，逐hash通过，AsyncSubmit保持1。旧DLL/模块/config备份D:\DLSSNR-Lab\hip-backend\stellarblade-hip\before-c256-attn-project。新版游戏FPS仍待用户实测。

current比较/profile脚本更新固定模块与benchmark_c256_attn_project.exe，compare_layers_current.exe重新编译上传。连续40帧edges-only ABBA：HLSL16.774/16.789ms，HIP20.502/20.527ms，各自最终golden匹配、首尾有限；差距约3.73ms，尚未达到目标。日志release/HIP/backend-c256-fused-current.log。本轮完成部署和基线复核，无额外内核优化。


### 2026-09-16：C512两头八批注意力投影融合更慢，撤回
新增独立c512_attention_project候选，256线程、两head一批共八批，独立全AV64×516bytes跨批保存。投影按两片一对、共八对覆盖各线程组负责的256列；C512残差保持原Hrtz(feature*scale)标量初始化，不套用普通MH三段分解。post保持C512原语义。

COMGR和专用benchmark编译通过，资源LDS54528bytes、VGPR230、SGPR40、private/spill0。连续40帧ABBA基线20.514/20.532ms，候选21.047/21.036ms，最终FEEA9EF3…一致、首尾有限。更慢，不采用，未扩大全帧/reset，生产源码恢复，游戏仍57b8ab45…+c256-attn-project-release-modules。

补丁experiments/c512-attn-project.patch、test-c512-attn-project.ps1；配套benchmark_c512_attn_project.exe及HIP_ISA_HALF=1内核，模块multihead_fused_attention.hsaco。日志release/HIP/c512-attn-project-test.log。


### 2026-09-16：C512 FFN FP8预打包有速度收益但出现逐位反例，暂不采用
实验新增PackedSplitFfnWeight缓存，分别精确打包ffwd展开/收缩区域262144/393216起各131072个元素，旧float缓存仍供mix使用。split_ffn_fused_fp8直接读取byte权重/隐藏层，用FP8 WMMA替代F16 WMMA，保持K16顺序与激活/舍入。首轮因组偏移仍按float推进而golden失败，修正g*16384→g*4096后重新编译测量；初次失败不计性能。

修正版连续40帧ABBA首轮基线20.509ms，候选20.094/20.148ms，最终FEEA9EF3…匹配、首尾有限（末轮基线见日志）。全40帧及24帧每8帧reset均有限且最终golden匹配，seed123/history也匹配。但16个C512块×零/一般FP8/更宽有限FP8混合符号输入共48组中，block46 pattern2出现10个float位差，其余47组一致、均有限。block46在生产默认跳过，但此反例使任意有限FP8输入逐位等价的主张不成立，不能仅凭真实帧fixture通过交付。

因此源码恢复，不部署此候选。反例保留在test_split_ffn_fp8.cpp（需应用experiments/split-ffn-fp8.patch后编译）。验证脚本validate-split-ffn-fp8*.ps1会因该反例失败，不能写成全部通过。临时DLL release/HIP/native-split-ffn-fp8.addon64 SHA0f67e72dccc9e3be4348997184ed08074ad88577f98541fc5d9375b1ade754bb仅为未接受实验产物。

日志release/HIP/split-ffn-fp8-test.log（错误布局）、split-ffn-fp8-fixed-test.log（修正版ABBA）、split-ffn-fp8-validation.log（反例）。游戏仍57b8ab45…+c256-attn-project-release-modules，未改。


### 2026-09-16：定位FP8反例到展开，保留F16展开+FP8收缩取得约0.21ms
阶段隔离使用同一精确打包权重，控制组两阶段F16（权重从FP8解码）48组全一致；只换展开为FP8复现block46 pattern2的10个差值；只换收缩为FP8则48组全一致。日志release/HIP/split-stage-isolation.log。未进一步声称差异的硬件内部根因。

最终split_ffn_fused_fp8保留原dot16展开与原始float展开权重，只预打包393216起131072个收缩权重，直接用byte hidden和FP8收缩WMMA。PackedSplitFfnWeight使用独立@split-contract-fp8 key，精确编码检查；非packed沿用旧kernel。激活、Hrtz及输出F保持，未增加跳块或改变分辨率。

连续40帧ABBA基线20.519/20.533ms，候选20.311/20.316ms，最终FEEA9EF3…匹配、首尾有限。48组（含block46大幅度反例）全部逐位一致，无非法值；全40帧及24帧每8帧reset全有限，最终FEEA9EF3…/22C171FC…；seed123/history75b62d2f…匹配。全检20.499/20.670ms不当连续计时。

完整DLL release/HIP/native-split-contract-fp8.addon64 SHA872ac9cf127375cec5c9398cb0c75365f16185204587c07800e7c750975086d0；固定24模块split-contract-fp8-release-modules，deep_fast-packed模块SHA2B88724479BBFA5F89B124319AAE9D373349C33D1AA6B8ACA92BC807A1713497。尚未部署，游戏仍57b8ab45…+c256-attn-project-release-modules。

最终脚本test-split-contract-fp8.ps1及validate-split-contract-fp8*.ps1，复用重新编译的benchmark_split_ffn_fp8.exe/test_split_ffn_fp8.exe；日志release/HIP/split-contract-fp8-test.log、split-contract-fp8-validation.log。阶段隔离补丁experiments/split-ffn-stage-isolation.patch基于2364bb5前的生产源码，需匹配该补丁的host/test重新编译，并以HIP_SPLIT_EXPAND_FP8/HIP_SPLIT_CONTRACT_FP8的00/10/01组合生成split-stage-control/expand/contract.hip供isolate-split-ffn-stages.ps1使用；不能混用当前仅打包收缩的测试exe。


### 2026-09-16：部署F16展开+FP8收缩并刷新整网基线
部署脚本确认游戏退出，安装native-split-contract-fp8.addon64（SHA872ac9cf127375cec5c9398cb0c75365f16185204587c07800e7c750975086d0）及split-contract-fp8-release-modules全部24模块，逐hash通过，AsyncSubmit保持1。旧DLL/模块/config备份D:\DLSSNR-Lab\hip-backend\stellarblade-hip\before-split-contract-fp8。新版实际游戏FPS尚无反馈。

current比较/profile脚本更新固定模块和benchmark_split_ffn_fp8.exe（已重编译为仅收缩FP8），compare_layers_current.exe重新编译上传。连续40帧edges-only ABBA HLSL16.783/16.799ms，HIP20.341/20.355ms，各自golden匹配、首尾有限；当前差距约3.56ms，目标仍未完成。日志release/HIP/backend-split-contract-current.log。本轮是部署与复核，没有额外内核提速。


### 2026-09-16：ViT QKV精确F16权重预打包取得约0.57ms收益
先检查block31的3145728个矩阵float均可精确F16表示。新增ExactWeightHalf/PackHalfMatrix（拒绝需要舍入、溢出或非有限的值），PackedVitQkvWeight独立@qkv-f16缓存，矩阵压为连续half，末端32个float尺度保持原字节偏移。新vit_qkv_project_normalize_fused_f16weight直接memcpy16bytes到h8，保留F16 WMMA、K16顺序及原归一化。非packed沿用旧fused入口。

连续40帧ABBA基线20.250/20.336ms，候选19.751/19.693ms，最终FEEA9EF3…匹配、首尾有限。CPU全部63488个有限half编码（含正负零/次正规）往返一致，3个非精确/超范围样本正确拒绝。8个ViT块×3种输入共24组输出逐位一致、无非法值；全40帧及24帧每8帧reset全有限，最终FEEA9EF3…/22C171FC…，seed123/history75b62d2f…匹配。全检21.013/20.315ms读回节奏不同，不当连续性能。

完整DLL release/HIP/native-vit-qkv-halfweight.addon64 SHA121042cdfd89492e4dee4b1477142470abdc67b6f678e6617d2952744de34883；固定24模块vit-qkv-halfweight-release-modules，deep_fast-packed模块SHAAB04D407E6A78B429FEAD20CB4D24A8A29BC680AED5D248FD904BA29ECF07627。未部署，游戏仍872ac9cf…+split-contract-fp8-release-modules。

脚本test-vit-qkv-halfweight.ps1、test_vit_qkv_halfweight.cpp、validate-vit-qkv-halfweight*.ps1，日志release/HIP/vit-qkv-halfweight-test.log、vit-qkv-halfweight-validation.log。当前实现仍分配原qw缓存后取half缓存，初始化存储可后续单独检查；未将CPU编码检查扩称所有浮点舍入算法验证。


### 2026-09-16：部署ViT F16权重版并刷新当前差距
部署脚本确认游戏退出，安装native-vit-qkv-halfweight.addon64（SHA121042cdfd89492e4dee4b1477142470abdc67b6f678e6617d2952744de34883）及vit-qkv-halfweight-release-modules全部24模块，逐hash通过，AsyncSubmit保持1。旧DLL/模块/config备份D:\DLSSNR-Lab\hip-backend\stellarblade-hip\before-vit-qkv-halfweight。新版实际游戏FPS尚无反馈。

current比较/profile脚本更新固定模块和benchmark_vit_qkv_halfweight.exe，compare_layers_current.exe重编译上传。连续40帧edges-only ABBA：HLSL16.772/16.770ms，HIP19.681/19.687ms，各自最终golden匹配、首尾有限；当前差距约2.91ms，仍未达到目标。日志release/HIP/backend-vit-halfweight-current.log。本轮完成部署及复核，无额外内核改动。


### 2026-09-16：C512 mix精确half权重预打包暂不采用
新增独立@split-mix-f16缓存，PackHalfMatrix精确打包前262144个mix权重；split_mix_halfweight直接加载half向量，输入转换、F16 WMMA/K16顺序及输出F(Hrtz)保持。展开/收缩沿原路径。

COMGR和专用benchmark编译通过；连续40帧ABBA基线19.647/19.680ms，候选19.636/19.595ms，最终FEEA9EF3…一致、首尾有限。约0.05ms差异，未扩大全帧/reset，不纳入生产，源码恢复，游戏仍121042cd…+vit-qkv-halfweight-release-modules。

补丁experiments/split-mix-halfweight.patch、test-split-mix-halfweight.ps1；需匹配benchmark_split_mix_halfweight.exe及HIP_ISA_HALF=1/HIP_PREPACKED_WEIGHTS=1内核，模块deep_fast-packed.hsaco。日志release/HIP/split-mix-halfweight-test.log。


### 2026-09-16：C512展开half预打包通过，约0.17ms收益
PackedSplitFfnWeight改用独立@split-expand-f16-contract-fp8 key：展开区域262144起131072个float精确编码为half，收缩保持已验证FP8区域及原始起始字节偏移。展开直接读取half向量，保留F16 WMMA/K16顺序、激活与舍入，不启用有反例的FP8展开。非packed旧路径保持。

连续40帧ABBA基线19.663/19.752ms，候选19.554/19.528ms，最终FEEA9EF3…匹配、首尾有限。48组C512逐位对照全部一致无非法值，含block46宽幅输入；全40帧及24帧每8帧reset全有限，最终FEEA9EF3…/22C171FC…；seed123/history75b62d2f…匹配。全检20.818/21.316ms因读回节奏不同，不当连续计时。

完整DLL release/HIP/native-split-expand-halfweight.addon64 SHA152c5bf83ac760fb71bf12d7b7498132d0e23d7bdd5bf968ccb2151c7e589693；固定24模块split-expand-halfweight-release-modules，deep_fast-packed模块SHA187092DABAC3387AC17302AB7C0CABE8CC53F44845403A3AC47EB3D2ACA6E37D。未部署，游戏仍121042cd…+vit-qkv-halfweight-release-modules。

脚本test-split-expand-halfweight.ps1、validate-split-expand-halfweight*.ps1，test_split_ffn_fp8.exe按当前布局重新编译；日志release/HIP/split-expand-halfweight-test.log和split-expand-halfweight-validation.log。


### 2026-09-16：部署C512展开half权重版并刷新profile
部署脚本确认游戏退出，安装native-split-expand-halfweight.addon64（SHA152c5bf83ac760fb71bf12d7b7498132d0e23d7bdd5bf968ccb2151c7e589693）和split-expand-halfweight-release-modules全部24模块，逐hash通过，AsyncSubmit保持1。旧DLL/模块/config备份D:\DLSSNR-Lab\hip-backend\stellarblade-hip\before-split-expand-halfweight。新版实际游戏FPS仍待用户反馈。

current比较/profile脚本更新固定模块和benchmark_split_expand_halfweight.exe，compare_layers_current.exe重编译上传。profile成功、最终RGB7b959143…匹配；第二轮主要项仍为C32 chain、普通MH FFN-QKV，C512 standalone QKV13次。memory owned1286.9MiB/213 allocations。逐核串行计时不作为连续性能，最快连续测例仍19.53–19.55ms；日志release/HIP/profile-split-expand-current.log。本轮完成部署与诊断，无额外内核提速。


### 2026-09-16：ViT QKV紧凑缓存减少144MiB显存，帧时间无明显收益
Vit按实际路径只获取所需QKV缓存，packed fused不再先创建原float GPU权重。PackedVitQkvWeight改为@qkv-f16-compact，精确编码后把32个尺度移到half矩阵末端并resize；独立vit_qkv_project_normalize_fused_f16compact入口尺度索引1572864，与旧3145728分开，旧shader入口保留。矩阵与尺度值不变。

连续40帧ABBA基线19.530/19.511ms，候选19.568/19.531ms，最终FEEA9EF3…匹配、首尾有限。差异约0.03ms，不声称帧率提升。profile实测owned1286.9→1142.9MiB，allocations213→205，少144MiB/8个分配；profile最终RGB7b959143…匹配。63488个有限half编码检查及24组ViT逐位对照通过，全40帧/24帧reset/seed123 history各自golden匹配，均无非法值。全检20.748/21.375ms不作为连续性能。

完整DLL release/HIP/native-vit-qkv-compact.addon64 SHAc8842686a683443962619055745d43a4daf22026f68864705e5d6f0f8df06554；固定24模块vit-qkv-compact-release-modules，deep_fast-packed模块SHA73B5CBD3CD2214AE01030D390015F7045326F2A915367C4348D99ABA751699D9。作为显存优化保留，本轮未部署，游戏仍152c5bf8…+split-expand-halfweight-release-modules。

脚本test-vit-qkv-compact.ps1、validate-vit-qkv-compact*.ps1、profile-vit-compact.ps1；test_vit_qkv_halfweight.cpp更新新布局入口。日志release/HIP/vit-qkv-compact-test.log、vit-qkv-compact-validation.log、vit-qkv-compact-profile.log。


### 2026-09-16：部署ViT紧凑缓存并复核当前差距
核对工作树为c77d6c6且干净；部署脚本确认游戏退出，安装native-vit-qkv-compact.addon64（SHAc8842686a683443962619055745d43a4daf22026f68864705e5d6f0f8df06554）和vit-qkv-compact-release-modules全部24模块，逐hash通过，AsyncSubmit保持1。旧DLL/模块/config备份D:\DLSSNR-Lab\hip-backend\stellarblade-hip\before-vit-qkv-compact。实际游戏新FPS仍待反馈。

current比较/profile脚本更新固定模块和benchmark_vit_qkv_compact.exe，compare_layers_current.exe重新编译上传。连续40帧edges-only ABBA：HLSL16.761/16.796ms，HIP19.521/19.531ms，各自golden匹配、首尾有限；差距约2.75ms，仍未达到目标。日志release/HIP/backend-vit-compact-current.log。本轮完成已验证144MiB显存优化的部署，不另称新增帧率收益。


### 2026-09-16：当前C256 FFN-QKV非tiled权重布局更慢，撤回
新增C256非tiled g128_qkv及mapped入口，使用mh_ffn_qkv_body<256,false,…>，生产实验阈值256→512使仅C256从tiled回到连续DWORD权重，其他通道/分组收缩/QKV融合保持。首次运行被原selected-pipeline阈值检查拒绝，尚未推理；实验guard仅补充min512+ffn_qkv+max256组合后重新编译测试。

COMGR/benchmark编译通过。有效连续40帧ABBA基线19.501/19.566ms，候选20.514/20.521ms，最终FEEA9EF3…一致、首尾有限。明显更慢，未扩大全帧/reset，生产源码与guard恢复，游戏仍c8842686…+vit-qkv-compact-release-modules。

补丁experiments/c256-ffn-linear.patch、test-c256-ffn-linear.ps1，需配套benchmark_c256_ffn_linear.exe与HIP_ISA_HALF=1/HIP_PREPACKED_WEIGHTS=1内核，模块multihead-fast-padded-wave-packed.hsaco；日志release/HIP/c256-ffn-linear-test.log（初始化拒绝）及c256-ffn-linear-compatible-test.log（有效ABBA）。


### 2026-09-16：C256 tiled权重DWORD读取+wave交换更慢，撤回
保持现有tiled矩阵布局，只替换mh_ffn_qkv_body的Tiled展开/收缩B操作数读取：各lane读相邻4列的两DWORD，再用ds_bpermute在半wave内转置成原8字节片段，保持各lane列/K顺序及全部矩阵/舍入。无需host布局变化。

COMGR编译通过；连续40帧ABBA基线19.469/19.490ms，候选19.985/19.980ms，最终FEEA9EF3…一致、首尾有限。更慢，未扩大全帧/reset，生产源码恢复，游戏仍c8842686…+vit-qkv-compact-release-modules。

补丁experiments/c256-tile-shuffle.patch、test-c256-tile-shuffle.ps1，编译HIP_ISA_HALF=1/HIP_PREPACKED_WEIGHTS=1，复用benchmark_vit_qkv_compact.exe，模块multihead-fast-padded-wave-packed.hsaco。日志release/HIP/c256-tile-shuffle-test.log。


### 2026-09-16：MH FFN-QKV合并重复FP8往返取得约0.19ms收益
新增q8_fused_round，直接返回q8(F(x))的字节结果，保留原零规范化与按符号饱和规则。仅mh_ffn_qkv_body中的隐藏层、收缩、归一化写byte合并编码/解码往返；FFN第三投影同时需要float残差和byte QKV输入，先编码一次再解码出float，两者共享字节结果。矩阵指令、累加顺序、尺度与布局不变。

COMGR编译通过；连续40帧ABBA基线19.505/19.551ms，候选19.335/19.335ms，最终FEEA9EF3…匹配、首尾有限。12组（C64/128/256×映射开关×两种输入）FFN float逐位及QKV逐byte全部一致，无非法值；全40帧及24帧每8帧reset全有限，最终FEEA9EF3…/22C171FC…；seed123/history75b62d2f…匹配。全检20.488/20.511ms不当连续计时。

固定24模块ffn-qkv-round-byte-release-modules，MH packed模块SHAD6360593220F499E7CAC0F9317F209CB1255A8A7624EE6AF2AA58A67CAB39661。仅内核改动，DLL沿用c8842686…；本轮未部署，游戏仍vit-qkv-compact-release-modules。脚本test-ffn-qkv-round-byte.ps1及validate-ffn-qkv-round-byte*.ps1，复用test_ffn_qkv.exe；日志release/HIP/ffn-qkv-round-byte-test.log、ffn-qkv-round-byte-validation.log。


### 2026-09-16：部署FFN-QKV量化往返优化并复核差距
部署脚本确认游戏退出，安装ffn-qkv-round-byte-release-modules全部24模块并逐hash通过，DLL沿用c8842686a683443962619055745d43a4daf22026f68864705e5d6f0f8df06554，AsyncSubmit保持1。旧DLL/模块/config备份D:\DLSSNR-Lab\hip-backend\stellarblade-hip\before-ffn-qkv-round-byte。新版实际游戏FPS仍待反馈。

current比较/profile脚本更新固定模块。连续40帧edges-only ABBA：HLSL16.796/16.806ms，HIP19.359/19.360ms，各自golden匹配、首尾有限；差距约2.56ms，目标尚未达到。日志release/HIP/backend-round-byte-current.log。本轮部署与复核，无额外内核提速。


### 2026-09-16：融合投影直接读取FP8精确float残差更慢，撤回
核对当前FFN第三投影输出经F或q8_fused_round后decode，当前生产feature为精确FP8 float。实验仅将C64/128/256融合attention-project的残差x从decode(fp8(feature))改为直接feature读取，保留三段分解及FMA顺序；C512不动。不能把该前提推广到任意float输入接口。

COMGR编译通过；连续40帧ABBA基线19.340/19.337ms，候选19.860/19.822ms，最终FEEA9EF3…一致、首尾有限。明显更慢，未扩大全帧/reset，源码恢复，游戏仍c8842686…+ffn-qkv-round-byte-release-modules。未进一步确认变慢的指令/调度根因，不把少源代码操作等同快。

补丁experiments/mh-exact-feature.patch、test-mh-exact-feature.ps1，编译HIP_ISA_HALF=1，复用benchmark_vit_qkv_compact.exe，目标multihead_fused_attention.hsaco。日志release/HIP/mh-exact-feature-test.log。


### 2026-09-16：直接残差读取退化的编译资源对照
读取上轮候选与已验证attention融合基线COMGR汇编，C64 VGPR94→82/SGPR30→53，C128118→106/30→53，C256152→133/42→43；三者private/spill仍0。C64静态FP8编码102→81、解码54→33，v_dual_fmac_f32从38→3。减少转换/寄存器并不意味着更快，编译器同时改变指令安排。

完整表experiments/mh-exact-feature-isa.md。以上是静态资源和指令数量，不是动态周期/占用率或因果证明；原版更快的结论仍来自上一轮ABBA。本轮无生产代码/游戏修改。


### 2026-09-16：C256单独直接读取精确FP8残差仍慢
依据上轮ISA中C256 SGPR只增1而C64/128增23的差异，单独测试C256去掉decode(fp8(feature))，保留其余通道原转换及全部三段FMA顺序。COMGR编译通过。

连续40帧ABBA基线19.289/19.330ms，候选19.481/19.509ms，最终FEEA9EF3…匹配、首尾有限。单独C256也更慢，未扩大全帧/reset，源码恢复，游戏保持c8842686…+ffn-qkv-round-byte-release-modules。

补丁experiments/c256-exact-feature.patch、test-c256-exact-feature.ps1，编译HIP_ISA_HALF=1，目标multihead_fused_attention.hsaco，复用benchmark_vit_qkv_compact.exe。日志release/HIP/c256-exact-feature-test.log。此结果排除仅其他通道造成整体退化的解释，但不证明具体硬件根因。


### 2026-09-16：C32展开相邻输出成对计算收益不明显，暂不采用
当前直接读取FFN权重的C32展开阶段，每次计算相邻两片16列，复用同一K16输入片段，保持每片累加、激活/FP8舍入、隐藏层布局及后续收缩不变。与旧隐藏层分半立即收缩实验不同，不移动收缩顺序或其累加器生命周期。

COMGR编译通过；连续40帧ABBA基线19.325/19.365ms，候选19.300/19.328ms，最终FEEA9EF3…一致、首尾有限。约0.03ms差异，未扩大全帧/reset，不纳入生产，源码恢复。游戏仍c8842686…+ffn-qkv-round-byte-release-modules。

补丁experiments/c32-expand-pair.patch、test-c32-expand-pair.ps1，编译HIP_ISA_HALF=1/HIP_PREPACKED_WEIGHTS=1，模块c32_fused_ffn_attention-packed.hsaco。日志release/HIP/c32-expand-pair-test.log。


### 2026-09-16：C512投影同时写byte供QKV使用无稳定收益
独立split_projection_blocked_dual保留float残差输出，同时写额外FP8 byte缓冲；c512_qkv_normalize_bytein使用fast_dense<true,false,true>直接读取字节，host新增内部可选byte_input并仅兼容C512快速路径启用。没有替换float残差或改变QKV矩阵/归一化。

两个COMGR模块及专用benchmark编译通过；连续40帧ABBA基线19.330/19.371ms，候选19.379/19.280ms，最终FEEA9EF3…一致、首尾有限。无稳定收益，未扩大全帧/reset，不采用，源码恢复，游戏仍c8842686…+ffn-qkv-round-byte-release-modules。

补丁experiments/c512-qkv-byte.patch、test-c512-qkv-byte.ps1；配套benchmark_c512_qkv_byte.exe，deep_fast.hip和multihead_fast_padded.hip各编译HIP_ISA_HALF=1/HIP_PREPACKED_WEIGHTS=1为c512-qkv-byte-deep/mh.hsaco，分别替换独立实验目录deep_fast-packed与multihead-fast-padded-wave-packed模块。日志release/HIP/c512-qkv-byte-test.log。


### 2026-09-16：刷新当前profile与逐层对照，调整后续重点
当前ffn-qkv-round-byte-release-modules profile通过，最终RGB7b959143…匹配；owned1142.9MiB/205 allocations。串行逐核前列仍为C32 chain与C256 FFN-QKV，但计时包含等待，不能把累计名次直接当跨后端差距。

重新运行当前MH同输入逐层对照：block9 C128 HIP0.150200/0.149200ms，对HLSL0.228050/0.207850；block15 C256 HIP0.140550/0.138000，对HLSL0.213450/0.192550；block23 C512 HIP0.205350/0.173400，对HLSL0.150650/0.133600。既有跨后端数值差异不变：block9/15 bitdiff496/129、maxabs2，block23逐位一致；所有测例无非法值。单层重复条件不能直接加总成全帧差距。

日志release/HIP/profile-round-byte-current.log、compare-mh-round-byte-current.log。本轮刷新证据，无生产代码/部署变化，不声称新增提速。


### 2026-09-16：decoder上采样投影half权重无稳定收益，暂不采用
新增@decoder-f16缓存，精确打包inputs*outputs矩阵并保留float残差尺度原偏移；decoder_project2x_halfweight直接读取h8，保持F16 WMMA、inputs1024四段累加、H/F与上采样merge顺序。Up仅在fast_deep+packed路径使用新入口，五处实际矩阵均通过精确编码检查。

COMGR/benchmark编译通过；连续40帧ABBA基线19.318/19.390ms，候选19.363/19.303ms，最终FEEA9EF3…一致、首尾有限。无稳定收益，未扩大全帧/reset，不采用，源码恢复，游戏仍c8842686…+ffn-qkv-round-byte-release-modules。

补丁experiments/decoder-halfweight.patch、test-decoder-halfweight.ps1，配套benchmark_decoder_halfweight.exe和HIP_ISA_HALF=1/HIP_PREPACKED_WEIGHTS=1内核，模块deep_fast-packed.hsaco。日志release/HIP/decoder-halfweight-test.log。


### 2026-09-16：post-head每线程RGB三通道计算无收益，撤回
新增hip_post_head_fast_half_rgb，每线程负责一像素，读取32个half特征同时更新RGB三个累加器，保持各通道j0–31顺序、Hrtz、base合并与clamp；host半精度head工作量由W*H*3改为W*H，输出仍RGB float。

COMGR与专用benchmark编译通过；连续40帧ABBA基线19.330/19.368ms，候选19.374/19.337ms，最终FEEA9EF3…一致、首尾有限。无稳定收益，未扩大全帧/reset，源码恢复，游戏仍c8842686…+ffn-qkv-round-byte-release-modules。

补丁experiments/post-head-rgb.patch、test-post-head-rgb.ps1；配套benchmark_post_head_rgb.exe，内核按HIP_ISA_HALF=1拼接c32_fast_attention.hip+boundary_fast.hip，目标boundary-fast.hsaco。日志release/HIP/post-head-rgb-test.log。


### 2026-09-16：重新检查HIP event计时可靠性
新增check_event_timing.cpp：HIP7 runtime实际报告70260201，同一stream固定vit_pack_input、1/8/64/256次重复各5轮，输入零/输出先填255，末尾检查全输出为零。20组event时间非负、self event均0、输出正确。单次event约0.011–0.017ms但CPU完成等待约0.046–0.297ms，说明两者度量不同，不可混称纯计算耗时。

随后完整网络--profile复现PROFILE INVALID，两轮中有负值/非有限区间，输出RGB golden仍匹配。再加--wall-profile逐核drain/同步，首轮仍出现INVALID，第二轮数值正常（TOTAL23.152340ms），不能据此宣布整网event可靠。两个网络脚本显式检测INVALID并返回失败，未将失败计时用于优化结论。保留整体wall-time ABBA为性能判断依据。

工具check-event-timing.ps1、check-network-event-profile.ps1、check-network-event-serialized.ps1；编译探针：MinGW C++17/O2/static，include hip_api.h。日志release/HIP/event-timing-current.log、network-event-profile-current.log、network-event-serialized-current.log。本轮诊断，无生产代码/部署变化、无新增提速。


### 2026-09-16：C512展开交换循环共用输入无稳定收益
仅split_ffn_fused_fp8的F16展开阶段将j外层/k内层改为k外层/j内层，四个输出片段共用同一h8输入，保持各片K0/16/32/48累加顺序、half权重布局、激活与FP8收缩。COMGR编译通过。

连续40帧ABBA基线19.312/19.361ms，候选19.328/19.318ms，最终FEEA9EF3…一致、首尾有限。候选HSACO SHA4DA96B4F…与基线73B5CBD3…不同，未见稳定帧时间收益，未扩大全帧/reset，不采用，源码恢复，游戏不变。

补丁experiments/split-expand-share.patch、test-split-expand-share.ps1，编译HIP_ISA_HALF=1/HIP_PREPACKED_WEIGHTS=1，目标deep_fast-packed.hsaco。日志release/HIP/split-expand-share-test.log。


### 2026-09-16：ViT attention三核融合为单核，约0.1ms收益（光之朱雀接手，闇GPT额度用尽）
先修层对照工具：compare_layers.cpp新增all/vit-only过滤，并按src/native_hip_network.h的HIP_FAST补齐ViT生产选项（vit_blocked/vit_contract_blocked/vit_weight_mask=1/vit_pack_input/vit_qkv_blocked）——此前ViT层对照跑的是非blocked老路径（vit_expand_fp8/vit_project_fp8），数不作数。生产路径block31（400token）：HLSL 0.184–0.220ms，HIP 0.280ms；HIP批量分段expand 0.067/contract 0.047/qkv+norm 0.061/attention三核 0.077/project 0.059ms，对HLSL时间戳0.036/0.042/0.035/0.038/0.021。同一轮all过滤下C32/C64/C128/C256/C512各代表层HIP已持平或更快（C64 0.24–0.27对0.30–0.39，C128 0.159对0.343，C256 0.154对0.293，C512 0.205对0.244），2.5ms差距的可定位部分集中在ViT（每层约0.06–0.1ms×8）。

实现vit_attention_fused_{256,400,640}：一wave=16query×1head遍历全部key，Q只打包一次，scores经同一exp位图，分母用f16 MMA对全一按key顺序K16逐tile累加（与vit_attention_inverse_fast同序），PV用pack(ex)对V的FP8 MMA一次算两个16列半段，尾部F(Hrtz(acc*inv))；不再写[query][head][key]f32 ex张量（900P每层20.5MB）。第一版按640上限在LDS存half表20.7KB：单元测试逐位一致、整图hash一致，但ABBA 19.343/19.295→19.977/19.979更慢（每WGP只放7个workgroup，占用率崩）。第二版P只存E4M3字节、16×16双缓冲half tile转置后逐tile累加分母、按token桶实例化（LDS 8.2/8.2/12KB）：连续40帧ABBA基线19.343/19.333ms，候选19.242/19.213ms；全40帧FEEA9EF3…、24帧每8帧reset 22C171FC…、seed123/history 75B62D2F…全部匹配。单元测试test_vit_attn_fused.cpp（240/400/640token×两种FP8格点pattern）对三核路径逐位一致。层对照fused 0.065ms对三核0.077，HLSL 0.037：仍差近一倍，剩余成本是K/V按float读（每lane每tile 8次4字节标量读+转换），HLSL读的是E4M3字节。

状态：选项DLSS5_HIP_VIT_ATTN_FUSED=1 / reference --vit-attn-fused，默认关、未部署、未改DLL；候选模块vit-attn-fused-modules（deep_fast-packed SHA B840EDE755EAA102E4FB531BC672B60827FEE18C118A10BCA2C446B9664893D5，其余23模块沿用ffn-qkv-round-byte-release-modules）。是否默认待下一刀（ViT QKV字节输出+attention字节输入）合并后按合计收益定。脚本compile-vit-attn-fused.ps1、test-vit-attn-fused.ps1、validate-vit-attn-fused.ps1、compare-current-all.ps1、compare-current-vit.ps1、compare-vit-attn-fused.ps1（远端源码目录src-vit-attn-fused）；日志release/HIP/all-current-latest.log、vit-current-latest.log、vit-attn-latest.log、vit-attn-fused-{0..3,full,reset,history-check}.log。


### 2026-09-16：ViT QKV字节输出+attention字节输入，约0.04ms，两刀合计约0.15ms
新增vit_qkv_project_normalize_fused_f16compact_fp8（投影/归一化与f16compact逐字相同，F()结果经byte_F存E4M3字节，norm张量900P每层4.9MB→1.2MB）和vit_attention_fused_{256,400,640}_bytein（Q/K每lane一次8字节load，V八次单字节load按pack()同样位置拼装）。单元测试扩展：真实block31 QKV权重+FP8格点contract输入，字节解码对f32存储逐位一致（含零的符号），bytein attention对f32输入融合核逐位一致，240/400/640×两pattern全过。连续40帧ABBA基线（vit-attn-fused-modules+ATTN_FUSED=1）19.304/19.286ms，候选19.268/19.245ms，最终FEEA9EF3…一致、首尾有限——K/V按float读不是主要成本。选项DLSS5_HIP_VIT_QKV_FP8 / --vit-qkv-fp8（要求vit_qkv_fused+packed_weights+vit_attn_fused），默认关、未部署；模块vit-qkv-fp8-modules（deep_fast-packed SHA CC666072011136853A76D57A080F65E7B03E9ED2B6F0DE5997C61CB9EC6D286E）。脚本compile/test/validate-vit-qkv-fp8.ps1；日志release/HIP/vit-qkv-fp8-{0..3}.log。全帧/reset/history三道验证待与后续改动合并后一起跑。

判断：ViT各段对HLSL的单层差距（0.06–0.1ms×8）和其他层的持平/反超加起来凑不出整帧2.45ms，剩余差距应主要在launch之间——HIP每帧259次launch，昨夜event探针里近乎空核的event时间已达11–17µs。下一步先用一workgroup空核连发直接量每次launch的GPU侧间隙，再决定是继续抠核还是做launch级合并。


### 2026-09-16：launch间隙探针与HIP event计时——launch不是主因，event不可用
check_launch_gap.cpp：一workgroup的vit_pack_input在同一stream连发，wall到stream完成：1次0.030ms（含提交往返），16次0.061，64次0.142，259次0.382，1024次1.216ms——稳态每次launch约1.2–1.5µs，每帧259次launch的纯间隙约0.4ms，不构成2.45ms差距的主体。日志release/HIP/launch-gap-current.log。

hip_d3d12_bridge.h加DLSS5_HIP_SPAN_PROBE=1诊断：输入semaphore等待之后、输出signal之前各记一hipEvent，并计Enqueue的CPU耗时（hip_api.h补hipEventSynchronize）。40帧结果：CPU Enqueue中位0.36ms/帧（259次launch每次约1.4µs，CPU不是瓶颈），但GPU event跨度中位0.023ms、最小−0.51、最大18.5——与昨夜PROFILE INVALID同一现象，这套HIP7+预览驱动上event时间戳不可信，不用于任何结论。探针默认关，不影响生产路径。日志release/HIP/span-probe.log。

结合bridge-isolation（桥接约0.13ms）：19.3ms就是HIP流上核串行执行的时间，差距在核的帧内行为，不在提交、桥接或launch间隙。


### 2026-09-16：跳块差分——各MH家族的帧内真实成本，隔离对照看不见DRAM/MALL效应
skip-family-diff.ps1：同一flags（含ATTN_FUSED/QKV_FP8）下用DLSS5_SKIP_BLOCKS跳掉整个通道家族（identity），两后端各跑连续40帧edges-only，none减去跳后即该家族帧内成本（输出非网络输出，只作计时）：

| 家族 | HLSL帧 | HIP帧 | HLSL成本 | HIP成本 |
|---|---|---|---|---|
| none（42,43,46） | 16.768 | 19.205 | – | – |
| C64 8块 | 15.204 | 16.932 | 1.56 | 2.27 |
| C128 12块 | 14.884 | 17.179 | 1.88 | 2.03 |
| C256 16块 | 14.412 | 16.908 | 2.36 | 2.30 |
| C512 16块 | HLSL跳块不支持 | 16.894 | – | 2.31 |

C64家族HIP帧内每块0.284ms，HLSL约0.195（HLSL跳块含一次copy，略低估）；而逐层对照里HIP C64 0.24–0.27反比HLSL 0.30–0.39快——隔离对照20次重复同一层，52MB工作集留在64MB Infinity Cache/L2里，且HLSL对照跑的是f32进出的链首链尾变体而非帧内fp8_stream链中变体，两边都不代表帧内。**结论：差距的可定位部分 = C64家族约0.7ms（核形状，见下条否定存储格式）+ ViT约0.7ms（8×0.09）+ C32链/其余约0.6ms（估算：HIP其余10.3ms对HLSL约8.7ms，按HLSL C512≈HIP假设）**，没有单一大头。日志release/HIP/skip-{hlsl,hip}-*.log。mh-all一组HIP端失败（跳掉全部MH块后流水线不支持），不影响结论。


### 2026-09-16：MH残差流E4M3字节化——逐位一致但更慢0.1ms，不采用
按HLSL fp8_stream设计（链首f32输入、链尾f32输出、链内块间字节）实现：mh_ffn_qkv_body加ByteIn/ByteFeature模板（ffn_input8解码后走原f32代码；特征输出直接存q8_fused_round的字节，原路径存的就是该字节的解码值），c64/c128/c256_attention_project模板化ByteFeature/ByteOut（特征按字节解码，原路径是f32再编码再解码；块输出F()格点值存字节，post==3链尾保持f32），Run()的attention_project线程数改前缀匹配。host：Body(byte_in,byte_out)按链位置选核，AttentionFast(feature_byte,out_byte)，选项mh_byte_stream / DLSS5_HIP_MH_BYTE_STREAM / --mh-byte-stream，要求ffn_qkv(256)+fused_ffn_project+mapped+crop，禁止跳C64/128/256块。

test_mh_byte_stream.cpp（C64/128/256×mapped×两pattern×post 4/0×crop）：特征字节解码对f32逐位一致、bytein对f32输入一致、QKV字节一致、project_fb对f32逐位一致、byteout解码逐位一致，48组全过。连续40帧ABBA基线（vit-qkv-fp8-modules，MH_BYTE_STREAM=0）19.272/19.165ms，候选19.375/19.336ms，最终FEEA9EF3…一致——**慢约0.1ms**。与09-15"融合FFN到QKV的byte接口无速度收益"同向：MH块不是带宽绑着的，把f32换字节省下的DRAM流量换不来时间，HLSL C64帧内更快在核形状不在存储格式。代码作为可选路径保留（默认关、未部署、未改DLL默认），模块mh-byte-stream-modules（padded SHA 1806C9D9…、attention SHA B096BB2C…）；脚本compile/test/validate-mh-byte-stream.ps1；日志release/HIP/mh-byte-stream-{0..3}.log。未跑全帧/reset/history三道（不采用）。

今天汇总（光之朱雀，06:00起）：ViT attention融合（−0.11）+ ViT QKV字节（−0.04）逐位一致、三道验证过、默认关未部署；MH字节流null；launch/桥接/CPU排除；差距分解见上。剩余可做：① C64家族核形状对照HLSL native_c64 fp8_stream变体（约0.7ms空间）；② ViT expand BLOCK_M=4共享B tile、qkv/project半精度输入（约0.7ms）；③ C32链帧内成本先用跳块法量（HIP C32不支持skip，需加）；④ Graph在当前19ms基线上重测（09-15测的0.45ms是51ms时代的CPU侧收益，现占比更大）。


### 2026-09-16：Graph重测null；ViT expand M4慢、fragment权重布局小赚；三项ViT合计−0.23ms转HIP_FAST默认
- **Graph**（DLSS5_HIP_GRAPH=1，现基线）：ABBA 19.159/19.162→19.135/19.211，null，与09-15一致。日志release/HIP/graph-current-{0..3}.log。
- **ViT expand BLOCK_M=4**（vit_expand_blocked_fp8_tiled_bytein_m4，四token tile共用权重fragment，每累加器K序不变）：单元测试240/400/640逐位一致，但ABBA 19.215/19.199→19.711/19.717慢0.5ms。ISA：private_segment 544字节、VGPR 155——累加器数组进了scratch。加#pragma unroll后scratch 0、VGPR 155仍慢0.45（19.636/19.643）：这个形状在HIP上就是不行（闇09-15双tile 0.2%同向），选项vit_expand_m4 / DLSS5_HIP_VIT_EXPAND_M4保留默认关。
- **fragment原生权重布局**（FragmentPackedMatrix：tile内[k半段][gr][row%16][8字节]，lane一次8字节load代替8次单字节gather；vit_expand_blocked_fp8_frag_bytein，VGPR 73→47）：逐位一致，ABBA 19.211/19.207→19.140/19.192，约0.04ms。frag+M4回到基线（19.205/19.25）。选项vit_expand_frag / DLSS5_HIP_VIT_EXPAND_FRAG / --vit-expand-frag。
- **合计**：ATTN_FUSED+QKV_FP8+EXPAND_FRAG对原基线（ffn-qkv-round-byte-release-modules，三项关）ABBA 19.415/19.361→19.196/19.126，−0.23ms；全40帧FEEA9EF3…、24帧reset 22C171FC…、seed123/history 75B62D2F…匹配。**已改为src/native_hip_network.h HIP_FAST默认开**（env仍可关），compare_layers.cpp生产选项同步。模块集vit-expand-fm4-modules（deep_fast-packed SHA 7A97A773E90BAF507F8F2DC6A460A87C63073AAE0633D082028CC051F5D91AEA，含fused attention/字节QKV/frag/M4全部入口，其余23模块沿用ffn-qkv-round-byte-release-modules）。**游戏未部署、DLL未重编**，等Zero定。脚本compile/test-vit-expand-{m4,frag,fm4}.ps1、test-vit-all.ps1、validate-vit-all.ps1；日志release/HIP/vit-expand-{m4,frag,fm4}-*.log、vit-all-{0..3,full,reset,history-check}.log。

ViT结论：三刀共减约0.1ms/层，剩余对HLSL约0.06ms/层差距分布在expand（HIP 0.067对0.036）与qkv/project的float输入转换上，M4形状和权重布局都已证明不是expand的瓶颈，下一步要看expand核的ISA找真正的等待（可能是WMMA与load的交错/占用率）。
完整DLL候选：release/HIP/native-vit-fused.addon64 SHA256 be4568d566e8c59dd8ad97ae13f1b84b5ee4a2f999d89834e22aa2e644ae85c0（HIP_FAST默认含三项ViT改动），配套模块集vit-expand-fm4-modules（24个）。未部署，游戏仍c8842686…+ffn-qkv-round-byte-release-modules。部署时DLL与模块必须同换（新DLL按名字调用新入口）。

### 2026-09-16 07:45：ViT全链字节流（contract→qkv→attention→project→下一块expand）逐位一致但无收益
contract输出、attention输出、project输出都是F()格点值，所以整条ViT残差链可以按E4M3字节传递：vit_contract_blocked_fp8_bstream（skip字节读入、字节输出）、vit_qkv_project_normalize_fused_f16compact_fp8_bytein（两次dword load+8次cvt_f32_fp8解码为half）、vit_attention_fused_*_bytein_bout、vit_project_bytein_bout（dot8字节A操作数、字节skip，同时写float out和下一块expand直接消费的字节out8，block32–38不再跑vit_pack_input）。host选项vit_byte_stream（Vit()内vit_in8成员传递块间字节），env DLSS5_HIP_VIT_BYTE_STREAM、CLI --vit-byte-stream，默认关。

test_vit_attn_fused.cpp新增四段对照：contract字节解码、QKV字节、attention字节解码、project float及打包字节，3种token×2种pattern全0差异。COMGR通过；连续40帧ABBA关19.268/19.239、开19.302/19.330，最终FEEA9EF3…一致——约+0.06ms更慢。和09-16 MH字节流同向：这些核不吃带宽，解码指令比省下的字节贵。脚本compile/test-vit-byte-stream.ps1，模块集vit-byte-stream-modules，日志release/HIP/vit-byte-stream-test.log。

### 2026-09-16 07:50：ViT half流+QKV四列片段：N2更慢，N4在噪声内
读HLSL native_wave_vit_qkv.hlsl（900p时400 token不是64倍数，HLSL走BLOCK_M=1）：其fused QKV A片段用pack16的half直接WaveMatrix load、零转换、BLOCK_N=4；HIP版每片段8次float标量读+8次cvt到half、两列。于是在字节流上加vit_half_stream：contract改写F16（vit_contract_blocked_fp8*_hstream），QKV每A片段一次16字节memcpy（vit_qkv_project_normalize_fused_f16compact_fp8_h16in，模板N=2/4：N4每wave 64列=两个头、两次平方和MMA，raw LDS 16×65），project的skip读half（vit_project_bytein_bout_hskip）。选项vit_half_stream/vit_qkv_n4，env DLSS5_HIP_VIT_HALF_STREAM/DLSS5_HIP_VIT_QKV_N4，CLI --vit-half-stream/--vit-qkv-n4，默认关。

单元测试新增half段（contract半精度解码、h16in N2/N4字节、project float与字节）全0差异；COMGR通过。六轮ABBA（关/N2/N4/N4/N2/关）：关19.323/19.188，N2 19.455/19.525（+0.23更慢，出乎意料——去掉转换反而慢，未查根因），N4 19.230/19.180（−0.05，在本机±0.07噪声内），最终FEEA9EF3…全部一致。不采用。教训：ViT这几个核的瓶颈不是输入格式，"更少指令"不等于更快。脚本compile/test-vit-half-stream.ps1，模块集vit-half-stream-modules，日志release/HIP/vit-half-stream-test.log。

### 2026-09-16 07:55：跳块法补齐ViT与C32链的帧内成本
HIP侧补齐DLSS5_SKIP_BLOCKS：ParseSkipBlocks放开1–4/31–38/66–69；Vit()跳块=恒等（并清vit_in8）；raw chain的C32块跳过=保留上一块raw状态（链入口本来就吃任意前一块的shift几何），跳收尾块（4/69）只对上一块raw跑c32_finish_crop_half（SkipChainFinish），四块全跳报错。HLSL本来就支持ViT跳块（NativeSkipCopy），不支持raw链C32跳块。

连续40帧edges-only：HLSL none 16.857、跳ViT 15.457 → **ViT家族1.40ms**；HIP none 19.145/19.160、跳ViT 17.359 → **1.79ms**，差0.39ms（不是隔离对照推的0.7——隔离对照的HLSL侧跑的是老变体）。HIP跳C32 2+3：18.486（0.66ms，0.33/块）；跳67+68：18.524（0.62ms，0.31/块）；HIP前后两条C32链8块约2.5ms，HLSL无对照。注意HLSL跳块是CopyBufferRegion真拷贝、HIP跳块零成本，HLSL家族成本被略微低估。脚本skip-family-diff2.ps1，日志release/HIP/skip-family-diff2.log（HLSL+HIP none）、skip-family-diff2-hip.log。

当前差距账（19.15对16.86≈2.3ms）：C64家族0.7、ViT 0.39、C128 0.15、C256持平、launch间隙≈0.4、C32链/C512/其余未对照约0.6。

### 2026-09-16 07:58：C64/C128 FFN-QKV核批量归一化（先算三段QKV再一次LDS归约）逐位一致但更慢
读HIP mh_ffn_qkv_body与HLSL native_wave_qkv_normalize.hlsl：两边生产版都是每行32项顺序求和（HLSL的NATIVE_QKV_FAST2 MMA求和未编进生产cso），顺序决定逐位一致，不能改树形。HIP版每段各写raw、两道barrier、32线程串行求和；C64一块要4道barrier加两段串行。改法BatchNorm：先算Q/K/V三段累加器，两段raw一次写LDS（union raw加倍），2×16×(C/32)线程一道barrier后并行求各行和（每行顺序不变），再一道barrier写norm——每块少两道barrier、串行段减半。导出mh_ffn_fused_c{64,128}_project[_mapped]_g128_qkv_bn，C256不做（LDS会到38KB）。选项ffn_qkv_batched_norm，env DLSS5_HIP_FFN_QKV_BN，CLI --ffn-qkv-bn，默认关。

test_ffn_qkv.cpp加bn对照，12组FFN逐位/QKV逐byte全0差异。ISA：C64 VGPR 55→95、LDS 5440→10816；C128 87→87、21568（原10816→21568）；private 0。ABBA关19.188/19.172、开19.314/19.275，最终FEEA9EF3…一致——**+0.11ms更慢**。VGPR/LDS都没到占用率门槛，慢在编译器对三段累加器同时存活的排布，没继续查。今天四次"按结构推理该更快"的改动（ViT字节流、half流N2、N4、QKV批量归一）全部不快或更慢：这批核的瓶颈不在barrier数、转换指令数或输入格式上，下一步不该再凭结构直觉改，要拿RGP抓HLSL与HIP同一核的真实指令占用/等待类型对照。脚本compile/test-ffn-qkv-bn.ps1，模块集ffn-qkv-bn-modules，日志release/HIP/ffn-qkv-bn-test.log。游戏与DLL未动。

### 2026-09-16 09:10：编译器旋钮全局试验：默认-O3已是最优
思路：同一份源码HIP比HLSL慢，而HLSL走驱动DXIL管线自带调度策略，HIP侧COMGR只给过-O3。rtc_compile.cpp新增RTC_EXTRA_OPTS（空格分隔，-mllvm用拼接形式`-mllvm=-amdgpu-...`，分开写会被COMGR错配吞掉-nogpulib），前端与codegen两段都追加；build-opt-modules.ps1按选项全量编24模块到opt-<name>-modules，test-opt-modules.ps1对opt-base（同编译器、无额外选项）ABBA。

结果（哈希全FEEA9EF3…一致）：base 19.15–19.23；`-mcumode` 19.247/19.255（+0.08）；`-amdgpu-sched-strategy=max-ilp` 19.923/19.924（+0.74）；`max-memory-clause` 19.442/19.442（+0.25）；`-amdgpu-kernarg-preload-count=16` 编出的hsaco与base逐字节相同（选项未生效，gfx12/COMGR3这条路不通）。编译器层没有免费午餐。日志release/HIP/opt-modules-test.log（精简版：原日志因PowerShell `-Command "...; type 同一文件"` 自我追加涨到4.8GB，已删；**远端跑脚本一律用 -File，别在 -Command 里type自己的输出文件**）。工具rtc_compile_opts.exe（rtc_compile.cpp已含该功能，默认行为不变）。

同时复核B：compare-mh-round-byte-current.log里block63/64本来就是HLSL fp8_stream变体（decoder段fp8_input=1）：隔离HLSL gpu 0.265–0.31ms（含input_pack 0.028）对HIP 0.27ms，持平；而帧内HLSL≈0.195–0.215/块、HIP 0.284/块。C64差距不在核本身，在帧内串接。

### 2026-09-16 09:55：重复launch量HIP核的帧内边际成本：C64块≈隔离值+0.02/launch，核就是核
Options新增dup_prefix（env DLSS5_HIP_DUP_PREFIX，诊断用）：Run()对名字前缀匹配的核连发两次——纯函数核重跑一遍输出逐位不变，帧时间增量=该核帧内真实边际成本（含launch/ramp/tail）。九轮（无/C64 FFN-QKV×2/C64 attn-project×2/C256 FFN-QKV×2 各两遍）哈希全FEEA9EF3…一致：基线19.284/19.235/19.248；C64 FFN-QKV 20.582/20.605（+1.34ms/8块=0.167/launch，隔离0.14）；C64 attn-project 20.136/20.164（+0.90/8=0.112，隔离0.10）；C256 FFN-QKV 20.873/20.931（+1.65/8=0.206）。

结论：HIP C64块帧内=0.167+0.112=0.279，与跳块差分0.284一致；比隔离只多0.02/launch（launch+尾巴）。HIP侧账是平的。那HLSL帧内0.195–0.215/块却低于其隔离0.248——要么HLSL跳块差分被拷贝成本压低，要么D3D12帧内真有重叠。下一步给HLSL加同样的重复Record量它的帧内边际成本。日志release/HIP/dup-launch-test.log，脚本test-dup-launch.ps1，runner benchmark_dup.exe。

### 2026-09-16 10:40：两后端"重复录制/launch"法重画帧内成本地图：MH+ViT只差0.74ms，1.7ms在MH之外
HLSL侧对称加DLSS5_DUP_BLOCKS（native_block_skip.h NativeDupBlock；network70的run lambda、decoder_tail69的run、decoder69的split分支、encoder c32[i]都在Record后按需再Record一次——Record自带recorded状态回转，重录合法；ViT走record_vit未接）。HIP侧dup_prefix加DLSS5_HIP_DUP_COUNT。所有轮次哈希各自golden一致（HLSL C7C2F49D…、HIP FEEA9EF3…）。

按块边际成本（ms/块；HIP=重复法，与跳块法互相印证；HLSL=重复法）：C64 HIP 0.167+0.112=0.279 对 HLSL 0.243（8块，+0.30）；C128 0.103+0.057=0.160 对 0.182（12块，−0.26）；C256 0.101+0.041=0.142（跳块0.152）对 0.169（16块，−0.35；此前一次把16块当8块除得出"0.247"是算术错误，×2/×3/跳块三方对照已澄清）；C512 HIP跳块0.178 对 HLSL 0.134（13块，+0.57）；ViT HIP 0.235 对 HLSL跳块0.175（8块，+0.48）。MH+ViT合计+0.74ms。整帧HIP 19.3对HLSL 16.86差2.4ms，**其余≈1.7ms在C32链（HIP 2.5ms/8块）、prefix/pre-block、post70、上下采样、gather、launch间隙里**。此前跳块法给HLSL的家族成本因跳块拷贝被压低（C64 1.56→实际1.94），把注意力误导到MH。

日志release/HIP/dup-launch-test.log、dup-launch2{a,b}-test.log、dup-c256-test.log（HIP）、dup-hlsl{,2,3}-test.log（HLSL）；脚本test-dup-launch{,2}.ps1、test-dup-c256.ps1、test-dup-hlsl{,2,3}.ps1；runner benchmark_dup/dupc/hlsl_dup.exe。
补C32：HLSL重复录制block1–4 +1.09ms/4=0.272/块（66–69走decoder_tail直接分支未接，无效）；HIP跳块0.33/0.31 → C32链8块约+0.4ms。至此MH+ViT+C32合计≈1.14ms，其余≈1.25ms在prefix/pre-block、post70、上下采样、gather、pool project、launch间隙。日志release/HIP/dup-hlsl4-test.log。
HIP块外阶段（重复launch法，基线19.21）：prefix三核 +0.48；**post70（c32_post_merge_fused_half）+1.56**；mh_pool×4+mh_pool_project +0.63；decoder_project2x×5 +0.49；vit_gather×2 +0.06。post70一个核占帧8%。HLSL侧DLSS5_NETWORK_GPU_PROFILE=1那轮没打出network_gpu_interval（benchmark路径不回读），改用重复录制量HLSL的pre/post/ds。日志release/HIP/stage-remainder-test.log。
HLSL块外（重复录制，基线16.83）：post70 +1.32/+1.25（≈1.30，HIP 1.56，+0.26）；pre-block +1.17（HIP prefix三核0.48，**HIP快0.7**）；ds4/8/14/22 +0.15（HIP mh_pool×4+pool_project 0.63，+0.45）。日志release/HIP/dup-hlsl5-test.log，脚本test-dup-hlsl5.ps1。

**帧内差距全图（HIP−HLSL，ms）**：C512 +0.57 ｜ ViT +0.48 ｜ pool/ds +0.45 ｜ C32链 +0.4 ｜ C64 +0.30 ｜ post70 +0.26 ｜ up（HIP 0.49，HLSL未量）｜ launch间隙≈0.4 ｜ C128 −0.26 ｜ C256 −0.35 ｜ prefix −0.7。HIP不是全面慢：赢在prefix/C128/C256，输在C512、ViT、pool、C32、C64、post。先前把火力全压在MH（C64）是被跳块法的拷贝偏差误导。

### 2026-09-16 17:50：池化+投影融合核逐位一致但无收益；下采样家族真实差距不到0.2ms
精确名匹配重复法（dup_prefix末尾`$`=精确）：mh_pool四次launch≈0，mh_pool_project_production五次+0.50。新核mh_pool_project_fused_c{64,128,256}（一个wave算16个池化token×64列，池化在核内从raw算一次复用四个列片，权重PackedDsWeight预打包F16，逐32块f32求和+H(acc+sum)+F(acc)与原核同序），选项pool_project_fused（env DLSS5_HIP_POOL_PROJECT_FUSED、CLI --pool-project-fused，默认关），C32/C512下采样仍走原路径。test_pool_project.cpp四种几何×两种输入全0差异（含1080p head有效矩形）；ABBA关19.25/19.267、开19.235/19.296，最终FEEA9EF3…一致，无收益。

开着融合再拆：fused_c64单独+0.15（一次launch，26MB raw的带宽下限0.045，慢在A操作数按MMA行布局跨行读，每条load指令只用到每行64B里的8B），c32+c512两次production +0.12（c32那个52MB raw已接近带宽下限）。c128/c256可忽略。家族合计≈0.3对HLSL 0.15，差距<0.2，先前+0.45是两次噪声叠加。可做但不急：c64下采样改workgroup协同合并读入LDS再池化，预期省≈0.07。日志release/HIP/pool-project-test.log、dup-pool2-test.log。

### 2026-09-16 18:20：C512块逐核帧内成本（重复法，13块，基线19.23）
split_mix_blocked +0.44（0.034/块）；split_ffn_fused_fp8 +0.22（0.017）；split_projection_blocked +0.41（0.032）；**mh_qkv_normalize_fused +0.74（0.057）**；mh_attention_fused_fp8_out +0.21（0.016）；mh_attention前缀（fused+project）+0.53 → attention_project≈0.025。合计0.181/块，与跳块0.178一致；HLSL 0.134/块。C512每块只有1600 token（900p），六次launch里QKV归一化一个核占三成，先看它。日志release/HIP/dup-c512{a,b}-test.log，脚本test-dup-c512.ps1。

### 2026-09-16 18:50：C512 QKV归一化wave级重写逐位一致但更慢；今日结论：这批核卡在权重的L2重读
mh_qkv_normalize_wave_c512：128线程组把16 token的A按pack4打包进LDS一次，四个wave各算64列（两个头）K=512 FP8 MMA链，B直接读预打包[N][K]字节，行平方和按j=0..31顺序在wave私有LDS tile上做，输出q8(F(acc*inv))与fast_dense<Normalize>同式。选项qkv_norm_wave_c512（env DLSS5_HIP_QKV_WAVE_C512、CLI --qkv-wave-c512，默认关）。test_c512_qkv_wave.cpp：block23/40×1600/960/2160 token×两种输入全0差异。ABBA关19.277/19.231、开19.418/19.386，最终FEEA9EF3…一致，**+0.15更慢**。

把今天六次null放一起看（ViT字节流/half流/N4、FFN-QKV批量归一、池化投影融合、C512 QKV wave级）：凡是"每16个token一个wave、B从全局重读"的设计都不比"64×64 tile经LDS共享A/B"的老核快，甚至更慢——权重每16 token重读一遍，L2流量是tile版的4倍（C512 QKV：78MB对20MB每次launch）。瓶颈是B的重读带宽，不是barrier数或转换指令。这也解释了HLSL为什么给ViT留了BLOCK_M=4版本（900p因token数不整除没用上）。方向应改为：加大每次权重加载覆盖的token数（M tile 32/64），同时控制累加器数量避免scratch（ViT expand M4就是栽在16个累加器上）。日志release/HIP/c512-qkv-test.log。

### 2026-09-16 18:15：部署ViT三刀（native-vit-fused.addon64 + vit-expand-fm4-modules）
游戏未运行。deploy-stellarblade-update.ps1 -AsyncSubmit 1 -BackupName before-vit-fused，安装DLL BE4568D566E8C59DD8AD97AE13F1B84B5EE4A2F999D89834E22AA2E644AE85C0与24模块并逐hash通过；旧DLL c8842686…+ffn-qkv-round-byte-release-modules备份在D:\DLSSNR-Lab\hip-backend\stellarblade-hip\before-vit-fused（-Restore可回滚）。HIP_FAST默认含fused ViT attention/字节QKV/frag expand（离线−0.23ms，19.36→19.13）。今天下午新增的选项（byte/half stream、N4、FFN-QKV bn、pool fused、C512 wave QKV、dup诊断）全部默认关，不影响游戏。游戏实测FPS待Zero反馈。

### 2026-09-16 18:36：游戏实测
Zero进《剑星》实测约30FPS（native-vit-fused + vit-expand-fm4-modules，900p HIP路径）。

### 2026-09-16 19:20：ViT expand M2与tile布局输入均无收益
M2（vit_expand_blocked_fp8_frag_bytein_m2，86 VGPR无scratch）：关19.157/19.11、开19.148/19.189；tile布局输入（vit_pack_input_tiled + vit_expand_blocked_fp8_frag_tiledin，A片段整wave连续256B）：关19.218/19.161、开19.171/19.146。逐位一致，都在噪声内。选项vit_expand_m2/vit_input_tiled（env DLSS5_HIP_VIT_EXPAND_M2/DLSS5_HIP_VIT_INPUT_TILED）默认关。髒L2后空核launch探针（check_launch_flush）：device/host写入后首个空核1.0/2.7µs，无L2回写代价。至此ViT expand的权重重读、A散读、launch回写都排除；唯一给过收益的是核融合（attention三核合一−0.11）。日志release/HIP/vit-m2-test.log、vit-tiledin-test.log。

### 2026-09-16 19:50：ViT expand+contract融合核逐位一致但慢0.55ms：并行度不够
vit_ffn_fused（选项vit_ffn_fused，env DLSS5_HIP_VIT_FFN_FUSED，CLI --vit-ffn-fused，默认关）：16个wave管16个token，hidden按四段1024列进LDS（16.6KB，无scratch），每段contract部分和按vit_contract_blocked_body的顺序累加进total（初值H(skip*scale)），expand激活/byte_F与原核同式；expand权重frag布局、contract权重行主序。单元测试240/400/640 token×两种输入对vit_contract_blocked_fp8全0差异。ABBA关19.189/19.200、开19.763/19.711，最终FEEA9EF3…一致，**+0.55更慢**。原因：900p ViT只有400 token → 25个workgroup，64个CU大半空转；拆开的expand是1600个wave铺满全卡。HLSL自己的native_wave_vit_ffn_fused也没进生产（flags里VIT_SPLIT_K=1走split-K），同一原因。

ViT一天的账：字节流/half/N4/M2/tile输入/融合FFN六刀全部逐位一致、全部无收益或更慢；唯一有效的是早上的attention三核合一（−0.11）。ViT这一级是"token太少"的问题：每个核几十微秒，按token tile的融合杀并行度，按列拆分又多launch。剩下能动的只有把整条8块链塞进一个持久化大核（原子计数做grid barrier）——把48次launch和尾巴一起消掉，但要保证workgroup全常驻，风险高。日志release/HIP/vit-ffn-test.log。

### 2026-09-16 19:40：C32融合核输入按lane分段（照HLSL ffn_fused骨架）−0.14ms，逐位一致
把HIP c32_fused_body和HLSL native_c32_ffn_fused.hlsli并排对：HLSL每个wave只由16个lane各算一次自己token的映射源索引，再用WaveReadLaneAt广播，32个lane按通道连续读一行128字节；HIP按元素调mapped_c32_input，每个window算2048次（窗口/坐标/边界/除法），dword模式下更是每元素4次。新增编译宏HIP_C32_LANE_STAGE（默认0，实验模块编1）：lanes 0..15算一次索引（chain模式算raw tile索引、Merge算skip与low两个索引），`__builtin_amdgcn_readlane`广播，每token一次连续读；值与原路径相同（Raw用F(half)、Merge用同一Hrtz链）；mode 0的残差初值从新增的f16暂存in16读（所有映射源都是H()舍入值，f16精确；chain模式不需要，LDS不变）。ISA：chain 190 VGPR/15360 LDS不变，mapped 190→225/19712，post 170不变，均无scratch。

ABBA（同runner、模块集交替）基线19.155/19.159、候选19.029/19.000，最终FEEA9EF3…一致，**−0.14ms**。全套验证见下一条。日志release/HIP/c32-lane-test.log，脚本compile-c32-lane.ps1、test-c32-lane.ps1，模块集c32-lane-modules（c32_fused_ffn_attention-packed SHA D2F713F6540981C6D103B0C8F9996AA1F1C6BBC286C6342A4A5E81085884B000）。方法论上今天最有用的一课：别按自己的直觉猜瓶颈，把两边同一个核的源码并排逐段对，找"做同一件事但做法不同"的段。
验证：c32-lane-modules全40帧FEEA9EF3…、24帧每8帧reset 22C171FC…、seed123/history 75B62D2F…全部匹配（validate-c32-lane.ps1，日志release/HIP/c32-lane-validate.log）。**源码HIP_C32_LANE_STAGE默认改为1。**

同核再两刀（对c32-lane-modules ABBA）：HIP_C32_LOCAL_FFN_SYNC=1（staging/hidden两处全组barrier改wave局部fence+残差A用load8）18.994/18.997对18.968/19.055，null；HIP_C32_DIAG_WEIGHTS=1（chain残差三段对角B片由host按scale_piece/F同式预算成12×512B追加在FFN权重字节34944处，核内load8+matrix8直接取，packed_weights.h AppendC32ResidualDiagonals；VGPR 190→200）19.021/18.995对19.097/19.028，−0.05在噪声边缘，哈希一致。两个宏默认0保留。日志release/HIP/c32-lane2-test.log、c32-diag-test.log。

### 2026-09-16 20:20：C32 lane staging进生产：全量模块集opt-lane-release-modules验证通过并部署
build-opt-modules.ps1 -Name lane-release（当前源码，无额外编译选项，HIP_C32_LANE_STAGE默认1）编24模块；全40帧FEEA9EF3…、24帧reset 22C171FC…、seed123/history 75B62D2F…全部匹配（validate-lane-release.ps1，日志release/HIP/lane-release-validate.log）。deploy-stellarblade-update.ps1 -BackupName before-c32-lane，DLL沿用BE4568D5…（纯核改动，host不变），24模块逐hash通过。离线：ViT三刀后19.13→lane staging后≈19.00。
HIP_C32_LOCAL_ATTN_SYNC=1（概率行改写进本wave的ex行，attention段五次全组barrier降为wave局部fence；同时LOCAL_FFN_SYNC=1）：19.08/19.075对19.05/19.05，null，哈希一致。C32融合核不吃barrier数。宏默认0保留。日志release/HIP/c32-attn-test.log。
HIP_C32_WAVES_PER_EU=10/12（amdgpu_waves_per_eu属性）：编译器未理会，VGPR仍192/223/168，ABBA 19.024/19.029对19.016/19.059，null。HLSL fast4约128 VGPR跑12 waves/SIMD对HIP 190跑8，占用率差距是真的，但得手工削寄存器，属性压不下来。日志release/HIP/c32-occ10-test.log。

### 2026-09-16 21:30：C512 mix并入FFN核逐位一致但慢0.05；改做mix权重预打包half
并排HLSL native_split.h：HLSL每块5次dispatch（ffwd_parallel把mix算在FFN核里、每组只算自己64列mixed）对HIP 6次。split_ffn_fused_fp8_mix（4个wave各算本组16列mixed，F(Hrtz)存半精度LDS，expand从LDS取A，其余同split_ffn_fused_fp8），选项split_mix_fused（env DLSS5_HIP_SPLIT_MIX_FUSED，CLI --split-mix-fused，默认关）。test_split_mix_fused.cpp block23/40×1600/960/2160×两种输入全0差异；ABBA关19.116、开19.190/19.160，慢0.05，null。省一次launch抵不过mix段搬进4-wave组后的损失。日志release/HIP/c512-mix-test.log。packed_weights.h补#include <cmath>（此前test/reference编不过、只有benchmark成功——c32-diag那轮的reference也没编出来）。

### 2026-09-16 21:50：C512 mix权重预打包half −0.12ms，逐位一致，进生产并部署
split_mix_blocked每wave对B做8次f32标量读+8次(_Float16)转换×32步×4列片（每wave1024次load/转换），1600 token的GEMM却占0.034/块。新增PackedSplitFfnWeightMixHalf（mix区[0,262144)按RoundWeightHalf——与设备(_Float16)转换同为RNE含次正规——就地打包成half，expand/contract区同PackedSplitFfnWeight），核split_mix_blocked_h16w每B片段一次16字节memcpy；选项split_mix_h16w（env DLSS5_HIP_SPLIT_MIX_H16W，CLI --split-mix-h16w）。test_split_mix_fused.cpp加mix_h16w对照全0差异；ABBA关19.078/19.014、开18.933/18.920，**−0.12ms**；全40帧FEEA9EF3…、24帧reset 22C171FC…、seed123/history 75B62D2F…全部匹配。**HIP_FAST默认开**；DLL重编release/HIP/native-c512-mixw.addon64 SHA256 95117FD6ED6991A12ECBD787E44B08823A62DDEE65C56807F889507A1A304C31，与c512-mixw-modules（opt-lane-release+新deep_fast-packed）一起部署，备份before-c512-mixw。离线HIP≈18.93 / HLSL 16.8。日志release/HIP/c512-mixw-test.log、c512-mixw-validate.log。

### 2026-09-16 22:20：下采样投影权重预打包（half/E4M3）−0.21ms，逐位一致，进生产并部署
同一模式第三次命中：mh_pool_project_production五次launch合计0.5ms，B是(_Float16)weights[...]逐元素读（c=32路径是pack(b,e,weights[...])逐元素fp8转换）。新核mh_pool_project_production_h16w结构不变，只把B片段换成一次memcpy；host PackedDsWeightCast：c==32按clamp(±448)+HostF+ExactWeightFp8（与pack()同值），c>=64按RoundWeightHalf（与(_Float16)同值）。选项pool_project_h16w（env DLSS5_HIP_POOL_PROJECT_H16W，CLI --pool-project-h16w）。test_pool_project.cpp加c=32几何（800x512->400x256 block4-ds）与h16w对照，五种几何×两种输入全0差异；ABBA关18.982/18.947、开18.734/18.765，**−0.21ms**；全40帧FEEA9EF3…、reset 22C171FC…、history 75B62D2F…全部匹配。**HIP_FAST默认开**；DLL重编release/HIP/native-pool-h16w.addon64 SHA256 E827F22BF01F735E3197EF91BFF8E5BAFAF95BAC7ED799A601ED692BF8A43A88，与pool-h16w-modules一起部署，备份before-pool-h16w。离线HIP≈18.75 / HLSL 16.8。早上的pool融合核null是因为它同时改了结构（16 token一组重读权重），只换打包不动结构才拿到收益。日志release/HIP/pool-h16w-test.log、pool-h16w-validate.log。

### 2026-09-16 22:45：decoder上采样投影权重预打包half −0.07ms，逐位一致，进生产并部署
同一模式第四次：decoder_project2x的B是(_Float16)w[...]逐元素读（五处Up共0.49ms）。decoder_project2x_h16w只换B为16字节memcpy（PackedDecoderHalf：矩阵区RoundWeightHalf就地打包，末尾skip尺度float偏移不变）；选项decoder_h16w（env DLSS5_HIP_DECODER_H16W，CLI --decoder-h16w）。ABBA关18.769/18.758、开18.687/18.689，−0.07（两轮一致；09-16凌晨闇的decoder-halfweight同幅度但当时噪声更大被判无收益）；全40帧FEEA9EF3…、reset 22C171FC…、history 75B62D2F…匹配。**HIP_FAST默认开**；DLL release/HIP/native-dec-h16w.addon64 SHA256 63EC269D257FC4C1BFAEC602334EF85BC7CA9812E9F1F422394F4BECD7B888FD，与dec-h16w-modules一起部署，备份before-dec-h16w。离线HIP≈18.69 / HLSL 16.8。今晚四刀（C32 lane staging −0.14、C512 mix权重half −0.12、下采样权重half/fp8 −0.21、decoder权重half −0.07）合计约−0.55，全部逐位一致。生产路径已无逐元素读f32权重的核（prefix_fast三处wmma待核）。日志release/HIP/dec-h16w-test.log、dec-h16w-validate.log。

### 2026-09-16 20:55：游戏实测
Zero实测第四版（native-dec-h16w）仍约30FPS，"似乎更稳定些"。账：整帧≈33ms，网络≈18.7ms，今晚−0.55ms只占全帧1.7%，FPS分辨不出；即使追平HLSL 16.8也只到≈32FPS，剩余14ms是游戏渲染与编解码，不在网络优化范围内。

### 2026-09-16 21:25：C64 attention-project按HLSL attention_direct拆成16 wave：逐位一致，null
隔离账：HIP FFN+proj+QKV融合核0.145对HLSL ffn+qkv 0.172（HIP赢），HIP attention-project 0.104（帧内0.112）对HLSL attention 0.058+projection 0.018（每块输0.035≈C64全部差距）。HLSL attention_direct每(window,head)一组8个wave，每wave 16 query×2个key tile，两侧部分和LDS合并——与HIP现有的部分和分组（keys 0-15∪32-47 / 16-31∪48-63）相同。c64_attention_project_w16：512线程/window，每wave 2次score MMA+2次求和MMA+4次AV+4次projection（原24次减半），part_sum LDS 1KB，VGPR 90、LDS 22528、无scratch。test_c64_attn_w16.cpp四种几何（含crop、post 0/3/4）×两种输入全0差异。ABBA关18.802、开18.785/18.828，null。选项mh_attn_w16（env DLSS5_HIP_MH_ATTN_W16，CLI --mh-attn-w16）默认关。日志release/HIP/c64-w16-test.log。
HIP_C32_LDS_VECTOR=1在当前基线第三次复测：19.106/19.018对19.033/19.089，null，与闇09-10/09-13两次结论一致。注：长时间连续跑后基线从18.7漂到19.0（发热/频率），小幅结论只认ABBA交错差。日志release/HIP/c32-ldsv-test.log。

### 2026-09-16 20:45：C512 QKV归一化照HLSL形状重写（tile A、fragment B、K循环无LDS无barrier）−0.37ms，逐位一致，进生产并部署
C512逐核隔离对齐（block23）：mh_qkv_normalize_fused（fast_dense 64×64 tile，每32个K过LDS+两道barrier）0.061对HLSL native_wave_qkv_normalize 0.019。HLSL的核每wave 16 token×64列，**A、B都直接从全局读、主循环无LDS无barrier**：A是projection直接存的16×32 E4M3行主序tile（512B连续），B是按片段tiled的E4M3权重（每(列tile,k32)一个512B tile，lane内8B连续）。我19:00那版wave核慢0.15的原因就是B行主序逐行取8B不合并、A过LDS三道barrier——形状像、访存不像。

实现：split_projection_blocked_t8在原f32输出之外把F(H(acc))再按tile存E4M3（值同cvt）；PackedMhWeightQkvFrag对QKV区做FragmentPackedMatrix（projection字节与f32尺度偏移不变）；mh_qkv_normalize_frag_c512：grid (n/16)×24个wave，16步×2半×4列片fp8 MMA，行平方和在wave私有LDS按j=0..31顺序，输出q8(F(acc*inv))；host在c512分支把它作为producer_norm交给AttentionFast。VGPR 72、LDS 4420、无scratch。选项c512_qkv_frag（env DLSS5_HIP_C512_QKV_FRAG，CLI --c512-qkv-frag）。test_c512_qkv_frag.cpp：projection f32逐位、tile字节、QKV字节对mh_qkv_normalize_fused全0差异（block23/40×三种token×两种输入）。ABBA关18.730、开18.369/18.355，**−0.37ms**；全40帧FEEA9EF3…、reset 22C171FC…、history 75B62D2F…匹配。**HIP_FAST默认开**；DLL release/HIP/native-c512-frag.addon64 SHA256 1448DC65D6F56FBF4A7C2EAACFC1DEB3674B72C3FB9FCC0674A0ABA1F540E484，与c512-frag-modules一起部署，备份before-c512-frag。离线HIP≈18.36 / HLSL 16.8。下一刀同法做C512的attention投影（mh_attention_crop=fast_dense，0.032对HLSL wave_project 0.014）。日志release/HIP/c512-frag-test.log、c512-frag-validate.log。

### 2026-09-16 21:05：C512 attention投影照同一形状重写（**首轮ABBA无效，见21:55更正**）
mh_attention_project_frag_c512（wave=16 token×64列，A=av字节行主序8B/lane，B=frag投影权重，残差Hrtz(feature*scale)、post/crop同fast_dense<ByteInput,Crop>），PackedMhWeightQkvFrag同时把projection区做frag布局。test_c512_qkv_frag.cpp加三种crop/post模式全0差异（注意fast_dense的Crop版cropw=0时不写任何元素，测试必须带crop）。ABBA关18.693/18.697、开18.725/18.700，null。选项c512_proj_frag（env DLSS5_HIP_C512_PROJ_FRAG，CLI --c512-proj-frag）默认关。QKV那0.37没有在投影上复现：投影K=512、N=512，每wave权重流量只有QKV的1/3，fast_dense的LDS共享在这里够用。日志release/HIP/c512-pfrag-test.log。
复核默认：benchmark_c512_pfrag.exe用纯vit-all-on-flags（无C512行）18.351/18.369，显式DLSS5_HIP_C512_QKV_FRAG=1 18.376——默认生效；显式=0对=1的交错ABBA 18.752/18.718对18.402/18.415（−0.33）。投影frag那轮"关"侧18.69是紧接全套验证（20ms重负载）后的热漂移，同晚已两次看到±0.3的批间漂移：**绝对帧时只在同一批交错ABBA内可比，跨批次要留±0.3**。日志release/HIP/c512-fragdef-test.log、c512-frag2-test.log。

### 2026-09-16 21:40：C512 split projection照QKV-frag形状（contract存tile、frag权重、无LDS）（**首轮ABBA无效，见21:55更正**）
split_ffn_fused_fp8_t8（contract输出另存16×32 E4M3 tile）+ split_projection_frag（A读tile、B读PackedSplitProjectionFrag，残差与F(H(acc))同split_projection_blocked，顺便存QKV用的tile，替代split_projection_blocked_t8）。选项c512_proj_tiles（env DLSS5_HIP_C512_PROJ_TILES，CLI --c512-proj-tiles），默认关。test_c512_qkv_frag.cpp加contract/投影f32与tile对照，12组全0差异。ABBA关18.687/18.740、开18.716/18.704，null。C512这条线结论：只有QKV（N=1536，权重流量三倍）从"无LDS直读tile"形状获益，投影/attention投影两处都不。日志release/HIP/c512-ptile-test.log。

### 2026-09-16 21:55：更正——投影两刀的"null"是测试脚本runner没换；真结果 attention投影frag −0.06、split投影tile化 −0.18、两者合计 −0.31
test-c512-pfrag/ptile.ps1由sed派生时`benchmark_c512_frag.exe`（下划线名）没被替换，跑的是不认识新env的旧exe，开/关同配置。用带全部选项的benchmark_mh_fb.exe重跑：c512_proj_frag关18.359/18.404、开18.325/18.324（−0.06）；c512_proj_tiles关18.359/18.364、开18.200/18.168（**−0.18**）；两者同开关18.374/18.392、开18.069/18.084（**−0.31**），哈希全FEEA9EF3…。同一bug也让mh_feature_byte首轮成了假null，真结果+0.26更慢（18.655/18.666对18.40）——字节feature再次否定。**教训：派生测试脚本后先grep runner名；ABBA前确认exe认识那个env（对照无变化先怀疑脚本）。**日志release/HIP/mh-fb-test.log、c512-pfrag-test.log、c512-ptile-test.log、c512-both-test.log。

### 2026-09-16 22:00：C512两个投影选项进生产并部署
全40帧FEEA9EF3…、24帧reset 22C171FC…、seed123/history 75B62D2F…匹配（validate-c512-both.ps1，日志release/HIP/c512-both-validate.log）。**HIP_FAST默认开c512_proj_frag与c512_proj_tiles**；DLL release/HIP/native-c512-proj.addon64 SHA256 19F4C90F10971854EC1CCAA3EF6ADF46DC430EB4D01FFC4289F25AC405781C03，与c512-ptile-modules一起部署，备份before-c512-proj。离线HIP≈18.07 / HLSL 16.8。今天累计−1.3ms（19.36→18.07）。

### 2026-09-17 00:21起：MH attention-project残差改为预打包对角片MMA（−0.09，进生产并部署）
并排对照：HLSL wave_project把三段残差尺度预打包成16×32 E4M3对角片（`project_weights`末尾channels×24字节）用WMMA累加，HIP的c64/c128/c256_attention_project融合核则是每lane标量做（每列3段×8token×2半片FMA＋每段fp8编解码）。新增`PackedMhWeightDiag`（packed_weights.h `AppendMhResidualDiagonals`，在attention权重float尾巴后追加3×(c/16)×512字节对角片），内核模板加`Diag`位，`_diag`导出：feature片直接作A操作数（f32路径每lane8次fp8编码，一次算三段共用），三片对角B各一次MMA，其后投影K循环不变。运算顺序与标量版同（三段依次进累加器再投影），逐位一致。选项`mh_proj_diag`（env `DLSS5_HIP_MH_PROJ_DIAG`，`--mh-proj-diag`）；仅float feature/float out变体（生产路径）。
COMGR：c64 VGPR94→111、c128 118→121、c256 152→164，LDS不变、spill 0。test_mh_proj_diag.exe（c64块5/63、c128块9/58、c256块15/50×4几何×2数据）48组逐位一致无非法值。ABBA（benchmark_mh_diag.exe，认识新env）：关18.168/18.155，开18.052/18.086，**−0.09**，哈希FEEA9EF3…。全40帧FEEA9EF3…、24帧reset 22C171FC…、seed123/history 75B62D2F…匹配（validate-mh-diag.ps1）。**HIP_FAST默认开mh_proj_diag**；DLL release/HIP/native-mh-diag.addon64 SHA256 3AD74685B3485782B749754B86B5CD9F97C875D9AA09C287B65B1EFC6CD65C27，与mh-diag-modules（c512-ptile-modules换multihead_fused_attention.hsaco 8B46A505…）部署，备份before-mh-diag。日志release/HIP/mh-diag-test.log、mh-diag-validate.log。离线HIP≈18.0 / HLSL 16.8。

### 2026-09-17 00:21起（续）：post70 RGB head融进C32 post-merge核尾部（−0.29，进生产并部署）
并排对照：HLSL生产路径`DLSS5_C32_EPILOGUE=1`把post70的rgb head放在C32 post-merge核的attention尾部算（native_wave_c32_split_attention.hlsl epilogue_mode≥3），不写raw、无head dispatch；HIP是c32_post_merge_fused_half写整幅raw半精度，再跑hip_post_head_fast_half（每输出元素32次跨步半精度读+权重读，同一像素3行重复读）。新增内核`c32_post_merge_head_half`（c32_fused_body模板加RgbHead位）：最后投影段把Hrtz后的v（即原raw里的半精度值）写进已空闲的scratch.ex（wave自己的16行），组同步后48个任务（16token×3行）分给32 lane，32项点积按原顺序f32顺序累加、Hrtz、`(acc*scale+centered)*8+.5`夹到[0,1]，直接写W*H*3输出；越界token（padding）跳过。选项`post_head_fused`（env `DLSS5_HIP_POST_HEAD_FUSED`，`--post-head-fused`），host路径跳过raw分配与head核。
COMGR：VGPR 168→172、LDS 19712不变、spill 0。ABBA（benchmark_post_head.exe）：关18.162/18.142，开17.854/17.880，**−0.29**，哈希FEEA9EF3…。全40帧FEEA9EF3…、24帧reset 22C171FC…、seed123/history 75B62D2F…匹配（validate-post-head.ps1）。**HIP_FAST默认开post_head_fused**；DLL release/HIP/native-post-head.addon64 SHA256 E4E6A97EE2322DC0FEC106BF29437674C7A48853E33BD56DCAA4AE657A58902B，与post-head-modules（mh-diag-modules换c32_fused_ffn_attention-packed.hsaco D350E220…）部署，备份before-post-head。日志release/HIP/post-head-test.log、post-head-validate.log。离线HIP≈17.87 / HLSL 16.8，差≈1.07。

### 2026-09-17 00:21起（续）：C32 finish（main8/主输出+下采样）融进融合核尾部（−0.41，进生产并部署）
并排对照：HLSL的`DLSS5_C32_EPILOGUE`除rgb head外还有"finish (main8+down)"模式——C32核尾部直接写main8与下采样；HIP是先写整幅raw半精度，再跑c32_finish_fast_half_main8（block0，全分辨率：raw写105MB+读105MB）或c32_finish_crop_half（block4/69）。c32_fused_body加`Finish`/`Main8`模板位：尾段把Hrtz后的v写进wave自己的scratch.ex行（同head做法），组同步后lane l负责通道l：主输出16个token各写F(v)（block0为fp8字节，链尾为裁剪f32），下采样按wave的两行8像素做4个2×2（top/bottom/Hrtz顺序与finish核相同），链尾不再写raw。新内核`c32_fast_ffn_attention_fused_half_finish_main8`（block0）、`c32_fast_ffn_attention_fused_half_chain_finish`（block4含down、block69仅main）。选项`c32_finish_fused`（env `DLSS5_HIP_C32_FINISH_FUSED`，`--c32-finish-fused`）；跳块实验仍走旧路径（链尾无raw时SkipChainFinish取上一块raw）。
COMGR：finish_main8 VGPR 237（原196）、chain_finish 109（原192，raw写消失后编译器安排不同）、LDS 15360不变、spill 0。ABBA（benchmark_c32_finish.exe）：关17.931/17.941，开17.513/17.538，**−0.41**，哈希FEEA9EF3…。全40帧FEEA9EF3…、24帧reset 22C171FC…、seed123/history 75B62D2F…匹配（validate-c32-finish.ps1）。**HIP_FAST默认开c32_finish_fused**；DLL release/HIP/native-c32-finish.addon64 SHA256 1EAA94ABC4ADB5387A8865ECCA25FFE9D76E69CE529B4671A22DF8B40B1EFA30，与c32-finish-modules（post-head-modules换c32_fused_ffn_attention-packed.hsaco）部署，备份before-c32-finish。日志release/HIP/c32-finish-test.log、c32-finish-validate.log。离线HIP≈17.5 / HLSL 16.8，差≈0.7。

### 2026-09-17 00:21起（续）：block4尾巴——下采样直接裁剪写出 + C32池化投影预打包权重（−0.11，进生产并部署）
block4链尾之后HIP还有两个独立小核：mh_shift_crop（把ww/2×hh/2的下采样裁到W/4×H/4）和mh_pool_project_production（c=32，f32权重逐元素pack）。① `down_crop_fused`：链尾finish核加`DownCrop`模板位（`c32_fast_ffn_attention_fused_half_chain_finish_dcrop`），下采样像素按(sx/2,sy/2)偏移直接写进裁剪网格、越界跳过，C32Result带down_cropped标记，host跳过mh_shift_crop。② `pool32_h16w`：block4-ds投影改用已有的mh_pool_project_production_h16w + PackedDsWeightCast(c=32，E4M3字节，与f32核的pack()逐位同)。env `DLSS5_HIP_DOWN_CROP_FUSED`/`DLSS5_HIP_POOL32_H16W`，`--down-crop-fused`/`--pool32-h16w`。跳块路径（SkipChainFinish）仍产未裁剪down并走mh_shift_crop。
COMGR：chain_finish_dcrop VGPR 193、spill 0。ABBA两项同开（benchmark_b4tail.exe）：关17.551/17.548，开17.472/17.407，**−0.11**，哈希FEEA9EF3…。全40帧FEEA9EF3…、24帧reset 22C171FC…、seed123/history 75B62D2F…（validate-b4tail.ps1，含post-head/c32-finish全开）。**HIP_FAST默认开down_crop_fused与pool32_h16w**；DLL release/HIP/native-b4tail.addon64 SHA256 1E6B686273A7AD732A7A7B75CFF5B1AD2C4440065CA6DA3DC969AAA1F2B1C7D5，与b4tail-modules部署，备份before-b4tail。日志release/HIP/b4tail-test.log、b4tail-validate.log。离线HIP≈17.4 / HLSL 16.8，差≈0.6。今晚四刀累计−0.9（18.07→17.4）。

### 2026-09-17 00:21起（续）：当前构建成本表（dup法）；C32不展开循环降寄存器 null（+0.64）
**HIP dup成本表（test-dup-map.ps1，benchmark_b4tail.exe，基线17.55）**：C64 ffn 1.32+attn 0.90=2.22（HLSL 1.93，+0.29）；C128 1.28+0.72=2.00（HLSL 2.16，−0.16）；C256 1.67+0.61=2.28（HLSL 2.70，−0.42）；C32 pre(block0) 1.36（HLSL 1.15，+0.21）；C32 mapped(1,66) 0.70、chain(2,3,67,68) 1.34、finish(4,69) 1.00 → 每链≈1.52（HLSL 前链1.09，+0.43/链）；post 1.41（HLSL 1.27，+0.14）；ViT 1.91（HLSL dup未接线，按skip法≈1.4，+0.5）；pool家族0.37（HLSL ds 0.13，+0.24）；decoder 0.45、prefix 0.49（HLSL未量）。日志release/HIP/dup-map-0/1.log。pool_project_fused用当前runner复测仍+0.16更慢（poolf-retest.log），真null。
**C32 no-unroll**：之前amdgpu_waves_per_eu被编译器忽略，占用率假设一直没真测。加`HIP_C32_NO_UNROLL`（`_Pragma("clang loop unroll(disable)")`于FFN列/收缩/QKV/key/投影五个循环）：VGPR 237→99、201→80、192→83、193→85、172→92，spill 0，占用率8→10 wave/SIMD（LDS上限）。模块交替ABBA：基线17.572/17.581，候选18.208/18.235，**+0.64更慢**。结论：C32核不是占用率受限，是wave内发射/延迟受限，展开的MMA流水比占用率值钱；宏默认0保留。日志release/HIP/c32-nounroll-test.log。

### 2026-09-17 00:21起（续）：C64/C128 FFN tiled权重 null（+0.38）
HLSL生产全通道用tiled权重（DLSS5_FFN_TILED_WEIGHTS/TILED_WEIGHTS），HIP只有C256走tiled（tiled_ffn_min_c=256）且C256是HIP领先的通道。补mh_ffn_fused_c{64,128}_tiled_project[_mapped]_g128_qkv导出（同mh_ffn_qkv_body模板Tiled=true），host选项`tiled_ffn_small`（TiledMin()=64；env `DLSS5_HIP_TILED_FFN_SMALL`，`--tiled-ffn-small`），PackedFusedMhWeight加force_tiled供测试。test_ffn_tiled_small.exe：C64/C128×mapped×两种输入8组，对分离参考与未tiled融合核FFN逐位/QKV逐byte一致。COMGR VGPR c64 87/88、c128 88/89、spill 0。ABBA（benchmark_tsmall.exe）：关17.421/17.424，开17.786/17.822，**+0.38更慢**——tiled对小通道不利（每wave列数少、tile利用率低），闇当初的min_c=256成立。选项默认关。日志release/HIP/tsmall-test.log。

### 2026-09-17 00:21起（续）：池化+投影改为组共享池化 + fragment权重（−0.28，进生产并部署）
dup成本表里pool家族HIP 0.37对HLSL 0.13。旧mh_pool_project_fused（null，复测+0.16）的形状问题：每个64列组的wave各自重做16 token的2×2池化（C256为8倍冗余raw读），B权重按行跨步16B读。新内核`mh_pool_project_group_c{64,128,256}`：一个工作组=2C/64个wave共管16个token，池化（与mh_pool同一H/F链）每组只做一次写进LDS f16行（F()量化值，f16精确），各wave从LDS取A片、从预打包16×16 f16 tile（`PackedDsWeightFrag`，每片一次连续512B读）取B片，逐32通道块求和、H(acc+sum)链与F(acc)存储照mh_pool_project_production。选项`pool_project_group`（env `DLSS5_HIP_POOL_PROJECT_GROUP`，`--pool-project-group`），仅C64/128/256；c=32（block4）与c=512头部池化不变。
COMGR：VGPR 98–100、LDS 2304/4352/8448、spill 0。test_pool_group.exe：四个生产几何+1080p头部矩形×两种输入10组，对mh_pool+production与h16w逐位一致。ABBA（benchmark_poolg.exe）：关17.464/17.462，开17.170/17.206，**−0.28**，哈希FEEA9EF3…。全40帧FEEA9EF3…、24帧reset 22C171FC…、seed123/history 75B62D2F…（validate-poolg.ps1）。**HIP_FAST默认开pool_project_group**；DLL release/HIP/native-poolg.addon64 SHA256 9D5A041685F7F3A8C718F1FD551609875903100EB8CC944254D166009C6B8078，与poolg-modules（b4tail-modules换multihead-fast-padded-wave-packed.hsaco）部署，备份before-poolg。日志release/HIP/poolg-test.log、poolg-validate.log。离线HIP≈17.2 / HLSL 16.8，差≈0.4。今晚五刀累计−1.2（18.07→17.2）。

### 2026-09-17 00:21起（续）：prefix内联进block-0 C32核 null（+0.07）
dup表里HIP prefix核0.49 + block0 C32 1.36 = 1.85对HLSL preblock 1.15（HLSL `DLSS5_INLINE_PREFIX=1`把特征计算与16→32投影放在block-0 FFN核的输入映射里，不写prefix缓冲）。照做：c32_fused_body加`Prefix`模板位，输入阶段由每wave的16个lane算各自token的16个特征（同一PCG/Box-Muller/Hrtz链）进scratch（此时空闲），两K片×两列片的f16 MMA（第二K片全零照算，保符号零）得到Hrtz后的prefix值：累加器布局直接留作mode-0残差（pre_acc），token主序经f16 LDS转成packed E4M3输入。内核`c32_fast_ffn_attention_fused_half_prefix_finish_main8`（rgba光栅+history+seed/temporal进，main8+down出，不再有W*H*32 f32 prefix缓冲与prefix核）。选项`prefix_inline`（env `DLSS5_HIP_PREFIX_INLINE`，`--prefix-inline`，要求direct_prefix_input+c32_finish_fused+pre_main8）。
test_c32_prefix_inline.exe：400×256/160×64×seed 0/123×temporal 0/1共8组，main8字节与down浮点对"prefix核+finish_main8核"逐位一致。COMGR VGPR 92、LDS 15360、spill 0。ABBA（benchmark_pinline.exe）：关17.242/17.122，开17.247/17.251，**+0.07，null**。解读：省掉的210MB×2带宽在这里不是瓶颈，block-0 C32核本身发射受限，塞进去的RNG/超越函数/4次MMA/2道同步刚好抵掉独立prefix核的成本（dup的0.49是"再跑一遍"的边际成本，不等于可省成本）。选项默认关。日志release/HIP/pinline-test.log。

### 2026-09-17 01:35起：ViT分阶段成本对照 → QKV与projection改fragment权重（−0.27，进生产并部署）
Zero实测900p 47 FPS（之前游戏锁30帧所以一直"30"）。接线HLSL的ViT分阶段dup（`DLSS5_DUP_VIT_STAGE`，native_block_skip.h `NativeDupVitStage`，record_vit里对列出的stage重录一次；HLSL benchmark要不带-DDLSS5_USE_HIP编译，否则还是HIP）与HIP逐核dup（test-dup-vit-hlsl/hip.ps1）：HLSL expand 0.28 / contract 0.34 / qkv 0.29 / attention 0.48 / project 0.14（合计1.53）；HIP expand 0.32 / contract 0.43 / **qkv 0.56** / attention 0.49 / **project 0.44** / gather+pack 0.01（合计2.24）。差距在两个线性层，attention持平——持久化大核不必做。
两核的B读法都是老问题（QKV每lane 16B跨2KB、project每lane 8B跨1KB）。新增`vit_project_frag`（`dot8_frag`：B从FragmentPackedMatrix tile连续8B取，其余含4段split-K顺序相加与F(H())尾巴完全同vit_project；权重`PackedVitProjectionFrag`）与`vit_qkv_project_normalize_fused_f16compact_fp8_frag`（B从16×16 f16 tile取，`PackedVitQkvWeightFrag`，scales留在compact偏移1572864；A、row-sum、归一化不变）。选项`vit_proj_frag`/`vit_qkv_frag`（env `DLSS5_HIP_VIT_PROJ_FRAG`/`DLSS5_HIP_VIT_QKV_FRAG`，`--vit-proj-frag`/`--vit-qkv-frag`）。
test_vit_linear_frag.exe：块31/38×400/640 token×两种输入8组，projection逐位、QKV逐byte一致。COMGR VGPR 64/61、spill 0。ABBA（benchmark_vitlin.exe）：关17.191/17.198，proj单开17.044（−0.15），qkv单开17.015（−0.18），两个同开16.913/16.947（**−0.27**），哈希FEEA9EF3…。全40帧FEEA9EF3…、24帧reset 22C171FC…、seed123/history 75B62D2F…（validate-vitlin.ps1）。**HIP_FAST默认开vit_proj_frag与vit_qkv_frag**；DLL release/HIP/native-vitlin.addon64 SHA256 34A6A255318721AC2C677941B3FF2B274903AAF375A7E2858F1E690C4B6C27AB，与vitlin-modules（poolg-modules换deep_fast-packed.hsaco）部署，备份before-vitlin。日志release/HIP/vitlin-test.log、vitlin-validate.log、dup-vit-hlsl.log、dup-vit-hip.log。**离线HIP≈16.93 / HLSL 16.8，差≈0.13。**

### 2026-09-17 01:35起（续）：ViT contract改fragment权重（−0.05，进生产并部署）
contract的"tiled"变体是8次逐字节gather（vit_weight_fragment<true>），生产走的是行主序跨步读；expand早已有FragmentPackedMatrix连续片读（vit_weight_fragment_native）。给vit_contract_blocked_body加Frag位、导出`vit_contract_blocked_fp8_frag`，权重PackedVitWeight(...,tiled=true,frag=true)。选项`vit_contract_frag`（env `DLSS5_HIP_VIT_CONTRACT_FRAG`，`--vit-contract-frag`）。test_vit_linear_frag.exe加contract对照，8组逐位一致。COMGR VGPR 205（原156）、spill 0。ABBA（benchmark_vitcf.exe）：关16.940/16.949，开16.898/16.898，**−0.05**。全40帧FEEA9EF3…、24帧reset 22C171FC…、seed123/history 75B62D2F…（validate-vitcf.ps1）。**HIP_FAST默认开vit_contract_frag**；DLL release/HIP/native-vitcf.addon64 SHA256 D2F9DB9310A4C953F149357FAF9BC3622C02F25BDD03A2C61666625BA0FF6E63，与vitcf-modules（vitlin-modules换deep_fast-packed.hsaco）部署，备份before-vitcf。日志release/HIP/vitcf-test.log、vitcf-validate.log。**离线HIP≈16.90 / HLSL 16.8，差≈0.1。**

### 2026-09-17 01:35起（续）：C64/C128 FFN+QKV融合核frag权重 null（+0.08）
mh_ffn_qkv_body加Frag位（`frag_b`：expand/contract/project/QKV四处B片改为FragmentPackedMatrix连续8字节读），权重`PackedFusedMhWeightFrag`（三个FFN区域）与`PackedMhWeightQkvFragOnly`（attention只frag QKV行，投影区留行主序给attention核）；导出mh_ffn_fused_c{64,128}_frag_project[_mapped]_g128_qkv；选项`mh_ffn_frag`（env `DLSS5_HIP_MH_FFN_FRAG`，`--mh-ffn-frag`，仅c≤128）。test_ffn_frag_small.exe 8组FFN逐位/QKV逐byte一致，VGPR 61/62/98/99、spill 0。ABBA：关16.847/16.889，开16.932/16.955，**+0.08更慢**——小通道权重（C64 FFN合计约20KB）本就常驻L0/L1，跨步读不是瓶颈，换布局只添指令；与tiled同结论。默认关。日志release/HIP/ffnfrag-test.log。**至此：HIP≈16.90 / HLSL 16.8，差≈0.1ms（<1%），离线追平。**

### 2026-09-17 02:02起：读C32核ISA当穷人profiler → F()去分支 + 输入16个load连发（−0.68ms，逐位一致，进生产）
Zero定调"尽量超过HLSL"。没有RGP看HIP核，退一步读COMGR吐的`.hsaco.s`（c32-finish.hsaco.s，生产模块）：chain核静态4731条，其中s_cbranch 128条、标量1036条、s_wait 634条；对照HLSL p0006（09-11 RGP抓的`native_wave_c32_fused_attention_ffn`，7321条、VGPR 118、scratch 0）。逐条看分支落点：**每一处F()调用都被编成一对发散分支**（`a==0`早退 + `a>=0x43e00000`饱和，v_cmpx/s_and_saveexec/s_or exec各一套，约50条/次），链核输入staging 16个token各一次、投影输出16个元素各一次，占静态指令约四成；更糟的是staging循环里每个token是`readlane→global_load_u16→s_wait_loadcnt 0→分支F`，**16次load串行等延迟**（分支挡住了编译器把load提前），HLSL那边是16个Load在无分支的展开循环里一次发完。
两刀，都是宏（默认已翻1）：`HIP_C32_BRANCHLESS_F`——F()改`fmin(fmax(x,-448),448)→cvt_pk_fp8→cvt_f32_fp8`，`a==0?0:r`一条cndmask（值与分支版对所有非NaN输入相同：±448精确转换，(447.75,448]两边都RNE到448，零保+0；NaN给−448而非±448，链上不出现NaN），fp8()同样换fmin/fmax；`HIP_C32_STAGE_PREFETCH`——lane staging拆两趟，第一趟16个token用无分支索引（越界token读元素l占位）把load全发出去，第二趟转换并用`srcs[j]>=0?c:0`选值。ISA：chain分支128→60、静态4731→4036，16条global_load_u16连发后s_wait_loadcnt 0xf..0x0逐个递减；VGPR 192/193/194/223/172不变、spill 0。
ABBA模块交替（benchmark_vitcf.exe，vitcf-modules对候选）：只去分支 16.800/16.855对16.610/16.669（**−0.19**）；去分支+连发 16.857/16.877对16.209/16.167（**−0.68**）。哈希FEEA9EF3…；全40帧FEEA9EF3…、24帧reset 22C171FC…、seed123/history 75B62D2F…（validate-c32-bf.ps1 -Candidate c32-bfp）。源码默认翻1后重编（bfall-c32.hsaco）与c32-bfp.hsaco的.s除cuid外逐行相同。日志release/HIP/c32-bf-test.log、c32-bfp-test.log、c32-bfp-validate.log。**离线HIP≈16.19 / HLSL 16.8，HIP反超约0.6ms。**
教训：dup表说C32链比HLSL每链贵0.4，四种盲改（no-unroll/局部同步/对角权重/prefix内联）全null，是因为都没碰到真因；真因在ISA里一眼可见——**分支型饱和转换 + 串行等load**。以后核内延迟问题先读.s再动手。

### 2026-09-17 02:14起：同一把尺子扫其余生产核 → 三刀再−0.26（逐位一致，进生产并部署）
把"读.s数分支/数load→wait"做成脚本扫全部生产核（scratchpad里的python，按`^name:`切核，统计s_cbranch、global_load后4条内s_wait_loadcnt、同操作数重复load）。三处命中：
1. `c64/c128/c256_attention_project_diag`（33个多头块的注意力+投影核）静态6936条、**435条分支**——全是投影尾巴的软件Hrtz（4层if嵌套：inf/溢出/下溢/次正规）×(post3与post0两条路径)×16元素。fused attention文件是唯一还用软件Hrtz的，改成其余文件的`v_cvt_pkrtz_f16_f32`版（宏`HIP_MH_RTZ_ISA`）：6936→1575条、435→35分支、VGPR 76/74/143。ABBA（rtz对bfall）16.037/16.039对16.010/16.018，**只−0.02**——分支跳过的路径本来就不执行，静态指令数不是钱。
2. `mh_ffn_fused_c*_project_mapped_g128_qkv`（FFN+QKV核）92分支里64个是`q8_fused_round`的"零早退+饱和早退"（同F()的病）。宏`HIP_BRANCHLESS_Q8`：`a==0?0:q8(fmin(fmax(x,-448),448))`（非NaN输入字节相同）。ABBA（rtzq8对bfall，含上一条）16.003/16.044对15.816/15.906，**−0.16**。
3. C32链核剩的17个load→wait：投影尾巴`residual*w[8193+col+rc()]`——`out[i]`的store夹在两次读之间，编译器按别名假设每个输出重读一次（global_load_b32 offset:32772/32836各8次，每次紧跟s_wait_loadcnt 0）。宏`HIP_C32_HOIST_PW`：两列scale先读进寄存器（`col==0?pw0:pw1`）。ABBA（c32h对rtzq8）15.831/15.846对15.789/15.784，**−0.05**。
c32h-modules三道全过：全40帧FEEA9EF3…、24帧reset 22C171FC…、seed123/history 75B62D2F…（validate-c32-bf.ps1 -Candidate c32h）。三个宏默认翻1。部署c32h-modules（DLL仍native-vitcf D2F9DB93…），备份before-c32h。日志release/HIP/rtz-test.log、rtzq8-test.log、c32h-test.log、c32h-validate.log。**离线HIP≈15.79 / HLSL 16.8，反超≈1.0ms（6%）。**
扫描还剩的信号：`mh_attention_project_frag_c512`（16个C512块）ser=33、同址重复load 42（残差初值`w[skip+c]`和feature在`p<tokens?:`条件里逐元素读）；`vit_qkv_project_normalize_fused_f16compact_fp8_frag`的scale（offset 6291456）重复7次；`vit_project_frag`重复18。下一刀从C512开始。

### 2026-09-17 02:20起：两处同款别名重载在小核上为null
同一扫描剩下的两处"同地址重复load"：`mh_attention_project_frag_c512`残差初值（`p<tokens?Hrtz(feature*w[skip+c]):0`条件里逐元素读，宏`HIP_C512_HOIST_RES`：4个scale和32个feature先读、行号钳到first、再select；VGPR 80）ABBA（c512h对c32h）15.807/15.771对15.784/15.755，−0.02；`vit_project_frag`残差scale与`vit_qkv_project_normalize_fused_f16compact_fp8_frag`的行scale（宏`HIP_VIT_HOIST_SCALE`）ABBA（vith对c512h）15.765/15.752对15.747/15.755，−0.01。都在噪声内，宏保留默认0，不进生产。解读：重载只在被N个块重复调用的大核（C32链）上值钱，C512投影和ViT线性层每帧各只跑16/4次。日志release/HIP/c512h-test.log、vith-test.log。

### 2026-09-17 02:23起：dup成本表复测 → prefix内联翻盘（−0.27，进生产并部署）
用c32h-modules重跑家族dup表（test-dup-map2.ps1，基线≈15.78）：C64 ffn 1.22+attn 0.87=2.09（HLSL 1.93）；C128 1.27+0.69=1.96（HLSL 2.16）；C256 1.59+0.72=2.31（HLSL 2.70）；c32pre 1.19、prefix 0.43（HLSL preblock含prefix 1.15）；c32mapped 0.56；c32chain 1.24；c32finish 0.70；post 1.24（HLSL 1.27）；vit 1.58（HLSL 1.53）；pool 0.18；decoder 0.40。最大缺口变成pre+prefix合计1.62对1.15——就是HLSL内联prefix、不写不读W*H*32 f32缓冲那0.47。
00:21时`prefix_inline`为null（+0.07）是因为block-0核当时发射受限（分支F+串行load），省下的带宽显不出来；现在核瘦了再测（同模块c32h，flag ABBA `DLSS5_HIP_PREFIX_INLINE=1`，test-flag-ab.ps1）：15.800/15.795对15.545/15.511，**−0.27**。三道全过（validate-pinline.ps1：全40帧FEEA9EF3…、24帧reset 22C171FC…、seed123/history 75B62D2F…）。HIP_FAST默认加prefix_inline（src/native_hip_network.h），DLL release/HIP/native-pinline.addon64 SHA256 8D5A218FF26FC6E89B01DB77E1BEADC1B2C028C4DC4066D40D39AEC243270343，配套benchmark_pinline.exe用默认flag再过三道（validate-pinline-dll.ps1）后与c32h-modules部署，备份before-pinline。日志release/HIP/dup-map2-0/1.log、pinline2-test.log、pinline2-validate.log、pinline-dll-validate.log。**离线HIP≈15.53 / HLSL 16.8，反超≈1.3ms（7.5%）。**
教训：null不是永久的——一刀的收益取决于当时的瓶颈；核换了瓶颈，旧null要重测。

### 2026-09-17 02:27起：C32三个旧null复测 → 对角权重残差翻盘（−0.16），no-unroll更慢，局部同步现已不逐位
核瘦身后按"旧null要重测"复测三个宏（compile-c32-retest.ps1，基线c32h-modules，ABBA各4轮）：
- `HIP_C32_NO_UNROLL=1`（VGPR 192→84）：15.778/15.806对16.779/16.795，**+1.0更慢**（09-16是+0.64）。占用率彻底排除，展开的MMA流水就是这核的命。
- `HIP_C32_LOCAL_FFN_SYNC=1 + HIP_C32_LOCAL_ATTN_SYNC=1`：第1轮HDR哈希变化（validate-hdr抛'HDR output changed'），与lane staging/prefetch组合后不再逐位一致，不再考虑；宏留0并在此记录为"当前不正确"。
- `HIP_C32_DIAG_WEIGHTS=1`（链残差三段对角B片由host预算，核内load8+3×2×2次MMA替代逐元素scale_piece循环；VGPR 199）：15.833/15.852对15.687/15.683，**−0.16**（09-16只有−0.05在噪声边缘）。默认翻1，验证后部署。
c32dg-modules三道全过（validate-modules.ps1：benchmark_pinline.exe默认flag，全40帧FEEA9EF3…、24帧reset 22C171FC…、seed123/history 75B62D2F…），与native-pinline.addon64部署，备份before-c32dg。decoder的skip scale提升（`HIP_DEC_HOIST_SCALE`）ABBA 15.807/15.816对15.804/15.841，null，宏留0。日志release/HIP/c32nu-test.log、c32ls-test.log、c32dg-test.log、c32dg-validate.log、dech-test.log。**离线HIP≈15.5 / HLSL 16.8，反超≈1.3ms。**
两个多头旧null复测（flag ABBA，c32dg-modules + benchmark_pinline.exe）：`DLSS5_HIP_MH_FFN_FRAG=1` 15.358/15.360对15.353/15.380，仍null；`DLSS5_HIP_TILED_FFN_SMALL=1` 15.372/15.407对15.719/15.682，仍+0.32。小通道FFN权重读法不是瓶颈这一条没变。日志release/HIP/ffnfrag2-test.log、tsmall2-test.log。
`HIP_C32_LDS_VECTOR=1`第四次复测（c32lv对c32dg）：15.718/15.734对15.730/15.716，仍null（VGPR 199不变）。日志release/HIP/c32lv-test.log。

### 2026-09-17 02:32起：多头FFN+QKV核残差/输入读法（ISA：8+12个load→wait）
`mh_ffn_fused_c*_project_mapped_g128_qkv[_bytein_fb]`的ISA：投影残差初值`Hrtz(load_in(...)*w[9*C*C+..])`8个元素各一次条件load+等待，byte输入变体的协作staging（pack4）再12个。宏`HIP_FFN_HOIST_RES`：1=残差8个读改无分支（`ffn_input[8]_nb`：索引钳到窗内、无条件load、窗测试select，scale读一次）；2=staging的pack4也用无分支读。ABBA（对c32dg）：级1 15.688/15.699对15.658/15.669，**−0.03**；级2见下。
级2（ffnh2对c32dg）：15.661/15.706对15.547/15.545，**−0.14**（VGPR 96/96/81，spill 0）。默认翻2，验证后部署。
ffnh2-modules三道全过（validate-modules.ps1：全40帧FEEA9EF3…、24帧reset 22C171FC…、seed123/history 75B62D2F…），与native-pinline.addon64部署，备份before-ffnh2。日志release/HIP/ffnh-test.log、ffnh2-test.log、ffnh2-validate.log。**离线HIP≈15.55（同批基线漂到15.7档时候选15.55）/ HLSL 16.8。今夜累计：16.90→15.5，−1.4ms，全部逐位一致。**
末次ISA扫描（c32dg/ffnh2/rtz/bfall-deep四个.s，阈值load→wait≥5或分支≥60）：生产核里剩下的只有C32链核的16个（residual_mode==0路径，链用mode 3，运行时不走）、C512投影残差33个（已测null）、ViT QKV frag的15个（scale重载，已测null）；其余命中的都是非生产变体。ISA能看见的串行/分支问题在生产路径上收完。

### 2026-09-17 02:37：末次家族dup表（ffnh2-modules + native-pinline，基线≈15.24，机器冷态）
C64 ffn 1.10+attn 0.87=1.97（HLSL 1.93）；C128 1.13+0.68=1.81（2.16）；C256 1.50+0.66=2.16（2.70）；C32 mapped 0.55+chain 1.07+finish 0.58=2.20（HLSL前后两链≈2.18）；post 1.22（1.27）；ViT 1.53（1.53）；pool 0.12；decoder 0.37；pre/prefix已内联进block-0核（dup家族名不再匹配，记0）。每个家族都到了HLSL的水平或更好。精确逐位这条路上，ISA可见的问题已收完；再往下只剩改累加顺序（PSNR门）那条路。日志release/HIP/dup-map3-0/1.log。

### 2026-09-17 03:27：Zero游戏实测900p 52 FPS（昨夜47）
native-pinline.addon64 + ffnh2-modules在游戏里跑，900p由47帧到52帧；离线−1.4ms对应游戏里约−2ms/帧（游戏GPU满载时钟更低，核内省下的发射周期按比例放大）。画面无异常报告。

### 2026-09-17 03:30：补量C512家族dup（ffnh2-modules，基线≈15.18）
split_*（FFWD mix/expand/contract/projection）0.69 + mh_qkv_normalize_frag_c512 0.63 + mh_attention_project_frag_c512 0.25 = **1.57ms/13块 = 0.12/块**，对HLSL 0.134/块（1.74）。09-16时HIP 0.178/块的差距已由frag三刀收平并反超。至此dup表全部家族≤HLSL，逐位精确路线上没有家族级缺口了。脚本test-dup-c512b.ps1，日志release/HIP/dup-c512b.log。

### 2026-09-17 03:31起：Zero授权路线1（PSNR门）与路线2（launch间隙）；先做PSNR验收与C32相位消融
- PSNR验收：validate-psnr.ps1（三道同样的检查但不锁hash，输出 X-p-full.f16 / X-p-reset.f16 / X-p-history.f32）+ tools/psnr-check.sh（scp回来与逐位golden ffnh2-full/reset/history-check比PSNR、最大绝对误差、逐位相同比例）。自检：精确配置三项inf dB、100%逐位。HDR输出峰值29.75。
- C32相位消融（HIP_C32_ABLATE=1/2/3，仅计时，test-abl.ps1不验hash，基线ffnh2）：跳FFN −1.08、跳注意力 −1.61、跳投影 −0.41（10个C32核合计）。注意力最重。
- V转置布局（`HIP_C32_VT`：V在QKV阶段写成[col][key] 68字节行的独立LDS数组，AV的B片一次8字节读，字节不变）：ABBA（c32vt对ffnh2）15.551/15.604对15.565/15.549，**null**，哈希一致。与LDS_VECTOR四次null同一结论：C32核的LDS字节凑片不是瓶颈。宏留0。日志release/HIP/c32vt-test.log。
- 多头注意力核同款V转置（`HIP_MH_VT`，V字节staging时按[col][key]写进同一2304字节区域）：ABBA（mhvt对ffnh2）15.557/15.591对15.559/15.519，−0.03在噪声内，哈希一致；宏留0。两处都说明：fp8 MMA的A/B片从LDS按字节凑不是这些核的瓶颈，issue预算花在别处（消融看是softmax阶段的标量尾巴）。日志release/HIP/mhvt-test.log。
- 多头FFN激活消融（`HIP_FFN_ABLATE_ACT`，仅计时）：15.570/15.561对15.215/15.217，**激活多项式尾巴值0.35ms**（36块；ISA里med3 104/add 70/mul 77/cndmask 112/cvt_pk 74 ≈ 该核1752条的25%）。两个候选：`HIP_FFN_PK_ACT=1`精确——两元素共用一条v_cvt_pk_fp8_f32、零选择在字节上；`=2`放宽——多项式改packed f16（v_pk_*），a*poly在f16里成形再转E4M3（PSNR门）。
结果（对ffnh2）：`PK_ACT=1`精确配对 15.540/15.557对15.538/15.552，null，哈希一致；`PK_ACT=2` packed f16 15.554/15.547对15.643/15.622，**+0.08更慢**（多出的f32↔f16转换抵掉了pk指令省的一半VALU）。消融能省0.35但两种重写都拿不到——这段尾巴的成本不在多项式本身的算术量，更像是32个ds_write_b8与后面barrier前的依赖尾巴。两宏留0。日志release/HIP/ffnabl-test.log、pkact1-test.log、pkact2-test.log。

### 2026-09-17 03:41：路线1/2今夜结论
- 路线1（改累加顺序/放宽精确）：验收工具就位（validate-psnr.ps1 + psnr-check.sh），但试的第一刀（FFN激活packed f16）反而更慢；C32注意力相位1.61ms里LDS凑片（VT）、LDS向量读都不是瓶颈，剩下的是softmax标量尾巴+3道barrier，放宽精度改不动结构。没有找到值得越过PSNR门的刀。
- 路线2（launch间隙）：当前每帧约200次launch，稳态1.2–1.5µs/次≈0.3ms硬成本；可合的相邻核（C512 projection+QKV、ViT各段）每处<0.1ms且要重排workgroup分解，性价比低，不做。
- 今夜净收益全部来自逐位精确路线：16.90→≈15.5ms，游戏900p 47→52 FPS。生产=native-pinline.addon64 + ffnh2-modules。

### 2026-09-17 06:14起：0.20 打包（HIP 后端，游戏版 + Magpie 版）
Zero定：不再追性能，打0.20并集成Magpie。那台机没有python，以0.15-900P的暂存目录（权重已是精确f16）为底，`Development/HIP/package-hip.ps1`在AMD机上生成两包：
- `D:\DLSSNR-Lab\DLSS5-AMD-0.20.zip`（游戏版）：d3d12.dll（ReShade，哈希与0.15的dxgi.dll同）+ dlss5-amd.addon64（=native-pinline 8D5A218F…）+ DLSS5-AMD\{native-game-flags.txt（`scripts/hip-game-flags.txt`：游戏现用flag去掉绝对路径的DLSS5_HIP_MODULES与DEBUG_DUMPS，模块目录按DLL默认取flags旁的HIP\）、HIP\24个hsaco（ffnh2-modules）、native-game-tiled-assets、logs\}+README（`scripts/package-README-hip.txt`）+两份许可。493文件，250,645,459字节，SHA256 2c620c1167f3dd150fbb236ce6b2c98c91a2b0797a0b548b017ad616bea9ca2e。
- `D:\DLSSNR-Lab\Magpie-DLSS5-AMD-0.20.zip`（Magpie版）：0.15-900P整包去掉DLSS5-D3D12-721与enable-game-sdk721.txt，换DLL、加HIP\、flags用`scripts/hip-magpie-flags.txt`（游戏flag+CODEC_SRGB=1/HISTORY_GUARD/MOTION_MAX_PX/SNAPSHOT_FRAME），README`scripts/package-README-magpie-hip.txt`。698文件，354,830,682字节，SHA256 6228d43f652a8cc55184bf738d1933745a1de148e386c5e8803139a68d27d2f6。
- 包内容验证：用游戏包里的f16权重目录+HIP目录+flags（拷成pkg020-modules/pkg020-flags.txt，validate-hdr-assets.ps1）跑40帧FEEA9EF3…、24帧reset 22C171FC…，与实验室f32资产逐位一致；每包SHA256SUMS逐条对zip内容校验通过，旁置.zip.sha256。
- 根README（中英）状态行、依赖行、版本表加0.20；HIP版不需要开发人员模式/Agility 721/SM6.10，只要驱动带amdhip64_7.dll（已验证预览驱动32.0.31007.2048，正式驱动未测——README如实写）。
- **待Zero**：Magpie路径下的HIP DLL没在本机实测过（游戏内钩子已实玩52fps），发布前先在自己的Magpie上跑一次0.20整包；上传网盘后把链接填进README版本表。
- 06:25 Zero在Magpie上试0.20（《鬼武者》900p窗口）INIT FAILED：oneshot日志`initialization_failed c32_prefix_reference.hsaco: hipErrorFileNotFound`。原因：`native_hip_network.h`在没有`DLSS5_HIP_MODULES`时默认`directory+"\HIP"`，而这个directory是资产目录（native-game-tiled-assets），游戏里一直靠flags里的绝对路径没踩到。改包不改DLL：两包的24个hsaco放到`native-game-tiled-assets\HIP\`（package-hip.ps1 Common()），README同步。重打：DLSS5-AMD-0.20.zip 250,646,612字节 SHA256 87736c42e0bdcb7cd72917ba316c468ec7271ec4c3a6c9613e02184fb2f3d9ca；Magpie-DLSS5-AMD-0.20.zip 354,831,865字节 SHA256 016b585d02bb881ceeeb1b87385d3d1700d5fb100d3ed354006ef480fe39b3d3。（首版两个zip的哈希作废。）
- 06:44 Zero在Magpie（《鬼武者》900p窗口）实测0.20通过。整包上传夸克：https://pan.quark.cn/s/3c8b5329353c ，根README中英版本表加链接。

### 2026-09-17 10:00 生产内核搬进顶层 `hip/`（0.20 之后）

- Zero 问：打 0.20 时 Development 之外的目录验证过编译没有？答：只编了 `--hip` 的 DLL；`shaders/` 三个 HLSL 自 09-14 900p 提交后没动、没重编（DX12 路线最后验证是 09-14 部署 0.15-900P）；补跑了一次不带 `--hip` 的 `build-addon.sh --tiled`，能编。Zero 的真正意思：HIP 的编译过程该像 `shaders/` 一样单独放一个正式目录。
- 追链（子代理）：生产 `ffnh2-modules` 的 24 个 hsaco 没有一份能从仓库直接重建的配方——opt-lane-release → c512-mixw → … → c32dg → ffnh2 共 21 级，每级拷上一级再覆盖 1～3 个。结果：20 个来自 `build-modules.ps1`（只加 `HIP_ISA_HALF 1`，`-packed` 再加 `HIP_PREPACKED_WEIGHTS 1`），4 个单独覆盖：ffn_attention-packed +`HIP_C32_DIAG_WEIGHTS 1`（c32dg）、deep_fast-packed +`HIP_BRANCHLESS_F 1`（bfall）、multihead_fused_attention +`HIP_MH_RTZ_ISA 1`（rtz）、padded-wave-packed +`HIP_FFN_HOIST_RES 2`（ffnh2）。坑：`c32_fused_attention.hsaco` 编自 `c32_fused_attention_packed.hip`。
- COMGR 确定性：同一份文本两次编译字节一致；文本变（哪怕只加默认关的宏）只改 `__hip_cuid_*` 符号，指令不变。所以校验标准 = 三道 hash + 去 cuid 的 `.hsaco.s` 对比。
- 新目录 `hip/`：21 份生产源 + `rtc_compile.cpp`（git mv），`build-modules.ps1`（24 行配方，宏显式），`SHA256SUMS`，`README.md`。远端从 `hip/` 全量重编（10:07，hip020-modules）：三道 hash 全过（FEEA…/22C1…/75B6…）；17 个与 ffnh2-modules 字节一致，4 个生产加载的模块去 cuid 后指令零差，3 个非 packed 变体（只在关 packed_weights 时加载）跟上了源默认（分支消除），与 09-16 编的旧字节不同。
- README 两版：目录表加 `hip/`，「编译」拆成 HIP 版（0.20）/ DX12 版（≤0.15）两节。`Development/HIP/build-modules.ps1` 加了指向说明。

### 2026-09-17 10:18–12:00 显存路线：先量，再试 VMM 稀疏映射（失败）

- **先量**。加 `DLSS5_HIP_MEMORY=1` 诊断（第三帧后把权重按 key、激活池按块、共享缓冲、建网络前后 free 写进 logs\native-hip.txt）。900p 生产配置，测试台与游戏内读数一致：权重 607 MiB（237 份打包表）、激活池 291 MiB（14 块）、共享缓冲 69 MiB、gather 表 3 MiB、噪声 0（fast_prefix 不上传）；整个插件占用 = 建网络前后 free 之差 = **1.17 GB**（测试台 15958→14785，游戏内 14700→13523）。README 里"网络自己约 3 GB"是 DX12 时代的数字，HIP 版没重量过，待改。
- **权重里 382 MiB 是死区**：`PackWeightRegions` 原地打包，E4M3 区只用前 1/4、f16 区只用前 1/2，上传却按 f32 原长度传（内核用 f32 布局的固定字节偏移读 tail 标量，所以不能简单截断）。估算活字节 225 MiB（ViT expand/contract 16 份 256 MiB → 69）。
- **VMM 稀疏映射实验**（`opt.sparse_weights`，`DLSS5_HIP_SPARSE_WEIGHTS=1`，`--sparse-weights`）：保留原 f32 长度的虚拟地址，只给活页做物理映射，内核偏移不变。amdhip64_7 全套 VMM 导出都在，粒度 64 KiB，`vmm_probe.exe` 验证 hipMemcpy 读写正确、洞里写入报错、边际成本精确等于映射字节数（首次使用固定开销 12.75 MiB）。三个坑：(1) 一个保留区里混用不同大小的块 → `hipMemSetAccess` InvalidValue；等大小铺满任何尺寸都过（`vmm_probe.exe tile`）；改成一律 64 KiB 块后权重 607→236 MiB，但三道 hash 全变。(2) `vmm_kernel_probe.exe`（最小拷贝内核）：内核读单块=整个保留区的 VMM 内存正确；读 64 KiB 多块 → 设备挂死（sync 不返回）；读 2 MiB 多块 → hipErrorLaunchFailure；拷贝引擎（hipMemcpy）读都正常。(3) 一个物理块分段映射（`hipMemMap` offset≠0 或部分尺寸）→ InvalidValue。结论：这版驱动上内核只认"保留区==一个完整物理块"，无法跳洞。路线封死，代码留在 flag 后（默认关，注释标 DO NOT SHIP），新驱动可重测。
- 剩下的显存路：真正压紧布局（改所有内核的字节偏移，逐位不变但工作量大，收益 ≈370 MiB）；激活池按峰值重排（≈100 MiB）。是否值得由 Zero 定——插件实际 1.2 GB，不是 3 GB。
- 游戏目前装的是 native-mem.addon64（= 生产 + 内存诊断，flag 文件多一行 `DLSS5_HIP_MEMORY=1`），行为与 native-pinline 相同。

### 2026-09-17 11:40 路线 3：性能档（九块跳过）在 HIP 上重量，写进文档

- 09-09 的跳块表：{42,43,46} 40.66dB（默认）；+{12,28,41,44,52,53} 九块 36.5dB（对全网络）。HIP 生产配置 ABBA（benchmark_pinline + ffnh2-modules，40 帧 edges-only 热中位）：off 15.25/15.28 → on 14.48/14.49，**−0.78ms（5.1%）**；对现行默认输出：40 帧 HDR 72.8dB（峰值 29.7，dB 虚高）、seed123 SDR history 检查 **30.6dB**、maxabs 0.147。
- 不改代码：就是 `DLSS5_SKIP_BLOCKS=12,28,41,42,43,44,46,52,53`。写进两份包内说明和 README（三档：不写 / 默认三块 / 性能九块）。`test-flag-ab.ps1` 加 `-CheckHash 0`（非逐位实验用），`perf-tier.ps1` 一键计时 + PSNR 输出。

### 2026-09-17 12:05 路线 2：hipGraph 在当前生产配置上复测——空

- `DLSS5_HIP_GRAPH=1`（09-15 已有的整帧录制/重放）ABBA，benchmark_pinline + ffnh2-modules，40 帧 edges-only 热中位：off 15.26/15.27，on 15.26/15.25；graph_stats builds=1 replays=38，四轮 hash 全 FEEA…。**零收益**。09-15 是 +0.23ms（30ms 时代）；现在每帧约 200 次 launch 的 CPU 提交完全藏在 GPU 时间后面，图只省 CPU 侧，GPU 排队间隙本来就没有。默认保持 0。
- 至此三条计划路线结果：1 显存——插件实测 1.2GB 非 3GB，README 已改；VMM 稀疏映射被驱动封死；真压紧（≈370MB，改全部内核偏移）与激活池重排（≈100MB）待 Zero 决定。3 性能档——已写进文档。2 hipGraph——空。

### 2026-09-17 12:20 计划收口

- Zero：显存压紧不做（9070 显存够，GPU 核心才是瓶颈）；正式版驱动有用户反馈可用（README/包内说明改为"已有用户反馈"）；OptiScaler 路线不急——网络学习是核心，Magpie 对一般人够用，Magpie 光流在暗部出垃圾向量的问题记为已知限制。

### 2026-09-17 12:30 网络档位按窗口自动选择 + 覆盖层显示实际分辨率

- Zero："为啥不能根据分辨率自动适配？"网络本身任意尺寸都能算（补到 64 倍数），锁死的是宿主：`native_network_geometry.h` 只认 720/900/1080 三档，小窗口 letterbox、大窗口缩到画布。
- 实现 `DLSS5_NETWORK_HEIGHT=auto`：frame Create 拿到输入贴图尺寸后先 `NativeResolveNetworkGeometry`（≤1280×720 → 720，≤1600×900 → 900，其余 1080；>1920×1080 仍在前面被拒），结果放进进程级槽，`NativeCurrentNetworkGeometry` 各处照旧读。固定值行为不变。FPS 覆盖层加档位：`DLSS5-AMD 52 FPS (19.2 MS) 1600X900`（覆盖层字库无小写，用大写 X）。
- 验证：benchmark_auto.exe + ffnh2-modules，`DLSS5_NETWORK_HEIGHT=auto` 在 900 输入上三道 hash 全过（FEEA…/22C1…/75B6…）。
- 部署：本机 Magpie-DLSS5-AMD-0.20 换成 native-auto.addon64（0f99f666…，旧 DLL 留 .pinline.bak），flag 改 auto，待 Zero 用不同窗口尺寸实测（720/900/1080 三档 + 覆盖层数字）。仓库 `scripts/hip-*-flags.txt` 改 auto，两份包内说明和 README 已写；《剑星》窗口本来就是 1600×900，auto 落 900，游戏侧 DLL 下个版本一起换。
- 16:40 Zero 实测（自动选档 + 覆盖层）：900p 窗口 52～54 fps，切到 1080p 窗口 37～38 fps，覆盖层随窗口显示 1600X900 / 1920X1080。1080 档像素 1.44 倍，网络约 22ms，与预期一致。《剑星》也已换成 native-auto.addon64（备份 before-auto，flag auto）。

### 2026-09-17 16:45 tag 0.21：自动选档 + 文案，两个包

- `package-hip.ps1 -Version 0.21 -Addon native-auto.addon64`（0F99F666…）+ ffnh2-modules，基底仍 Magpie-DLSS5-AMD-0.15-900P。产物 `D:\DLSSNR-Lab\DLSS5-AMD-0.21.zip`（493 文件，250,663,839 B，sha256 38e1518917b8f60b48791bccacf84800f87caf5766c454688f198d8879b08e6e）、`Magpie-DLSS5-AMD-0.21.zip`（698 文件，354,849,225 B，sha256 8eeee2ddebe2a0328be59b00c241963ab238e3c54229ab87e0adb06412b0e911）。flag 文件 `DLSS5_NETWORK_HEIGHT=auto`；包内说明加"0.21 与 0.20 的区别"。内核/权重与 0.20 相同。
- 包验证：游戏包内 assets + HIP 模块 + flag（auto）在 benchmark_auto 上 40 帧 FEEA…、reset 22C1… 全过；Magpie 包 flag 带 CODEC_SRGB 等本就改输出，不拿它验（0.20 同法）。包内 DLL 0F99F666…。

### 2026-09-17 18:35～19:10 900 档补边 1024 → 960 行（"900s"，实验，待 Zero 看画面）

- 动机：900 档 1600×900 补到 1024 行，13.8% 的行是补边，且全网络每段都算它。硬约束是 ViT token 数须为 16 的倍数：1600 宽 25 列，1024 高 16 行 = 400；960 高 15 行 = 375 ✗。解法照抄 1080 档（30×18 真 token 补零到 32×20）：head 下采样输出 25×16，第 16 行为零 token（`mh_pool*` 的 valid 掩码本来就有）。
- 代码：`hip_reference_network.h` token 网格规则泛化（非 1920 时 (vw*vh)%16 则 vh+1；Down(head) 按 ow*oh≠rw*rh 决定 valid 掩码）、几何白名单加 1600×960；`native_network_geometry.h` 新增 `DLSS5_NETWORK_HEIGHT=900s` → {1600,900,1600,960}；参考链 `--960`；`input960.rgba32f` = input900 裁前 960 行。
- 两个坑：① 几何 flag 用 `_wgetenv` 读，测试台的 flag 加载只刷新窄环境 → 改成 `std::getenv`（游戏加载器两边都设，不受影响）；② `benchmark_live_capture.cpp` 加载 flag 后硬写 `DLSS5_NETWORK_HEIGHT=900` → 改成 flag 未写时才默认 900。**意味着此前测试台上的 `auto` 验证其实跑的是被硬写的 900，auto 只由 Zero 游戏内实测证明过。**
- 结果：ABBA（benchmark_g960 + ffnh2-modules）900 15.20/15.19 → 900s **14.48/14.42，−0.75ms（4.9%）**，输出有限，参考链 1600×960 跑通。
- 与 1024 版输出的差：HDR 40 帧 67.5dB（峰值 29.7 虚高，底边带 36.4dB）；seed123 SDR history 检查 **31.5dB，全图均匀**（0～99 行 31.1、800～899 行 31.7）——补边行通过 ViT 全局注意力影响整幅，不是局部。量级和跳九块（30.6dB）相当，但这里没有"谁对"：900 档本就是我们自定的几何，NVIDIA 只有 1080（1152 行，72 行反射 + 100 个零 token）；1024 版是 124 行反射（12% 假 token），960 版是 60 行反射 + 25 个零 token（6%）。只能看画面定。
- 已部署到本机 Magpie-DLSS5-AMD-0.20（native-g960.addon64，flag 900s），Zero 用《鬼武者》900 窗口对比 900/900s 两种 flag。
- 20:55 Zero 定：主观看不出差别，"就這樣了"。960 行成为 900 档的正式实现（`FromHeight(900)` → 1600×960），auto 跟随；0.21 的 1024 布局保留为 `900w`，测试台默认仍 `900w`（远端 vitcf-on-flags.txt 改成 900w），老的三道黄金值继续用于内核验证。三轮复测 ABBA：15.81/15.90/15.93 → 14.84/14.91/14.85，−0.9～1.0ms。
- 21:05 960 档三道黄金值（benchmark_r960 + ffnh2-modules + reference_g960 --960 input960）：full **383FA5BCF544860F40D55E15E390E4154EDE54E784FE07366FB0445CCD93BFC3**、reset **698E1A39AFDF6BA47F53AD1BE559837076FF291E72ADD743C5BF7999AD2DC9C1**、history seed123 **78AF52D3FEA5068EBFA28E00047437011B0DED73578C0A2BC206F658CF470998**，连跑两次一致；`validate-modules-960.ps1` 固化。新 runner 在 900w 下仍出 FEEA…。DX12 编译路径共用几何：DX12 版在 900 档现在也会拿到 960 行，没测（DX12 版已是历史）。
- 部署：native-r960.addon64（a707a873…）进《剑星》（备份 before-r960，flag auto）和本机 Magpie-0.20 目录（flag auto）。

### 2026-09-17 21:00 tag 0.22：900 档 960 行

- `package-hip.ps1 -Version 0.22 -Addon native-r960.addon64`（a707a873…）+ ffnh2-modules。产物 `DLSS5-AMD-0.22.zip`（493 文件，250,666,468 B，sha256 59226956694c1f90e2fb31483b2585e0ee9114e1e9d76a7c51a61fbaee5693d6）、`Magpie-DLSS5-AMD-0.22.zip`（698 文件，354,851,936 B，sha256 8186b67daca50f7d2ea6965fb535be1cbb1c72b288da3481c27f8b733f11926f）。包内说明加"0.22 与 0.21 的区别"（含 900w 回退方法）。
- 包验证：游戏包 assets + HIP 模块 + flag（auto，测试台 1296×720 输入落到 900 档=960 行）在 benchmark_r960 上 40 帧 383FA5BC…、reset 698E1A39… 全过。包内 DLL a707a873…。

### 2026-09-17 21:20 C64/C128 段拆分与注意力核 ISA

- dup 拆分（4 块 C64 / 6 块 C128）：FFN+QKV 核 1.14 / 1.16ms，注意力+投影核 0.87 / 0.62ms。按 MMA 峰值算 C64 每块只需 0.05ms，实际 0.49，瓶颈在 MMA 之外。
- ISA 统计（c64_attention_project_fb）：1815 条/22 wmma，28 个 s_cbranch，131 个 v_movrel（`result[j][e]` 被寄存器相对寻址），141 个 cvt——来源是字节特征版没有 diag 路径，残差缩放走逐元素标量 FMA + 带分支的舍入。
- 加 `c64/c128/c256_attention_project_fb_diag`（Diag 模板已支持 ByteFeature，只补实例）+ 选项 `mh_proj_diag_fb`（env `DLSS5_HIP_MH_PROJ_DIAG_FB`，CLI `--mh-proj-diag-fb`）：指令 1815→1439，movrel 131→2，VGPR 72 无 scratch。ABBA（diagfb-modules，900w）：15.24/15.22 → 15.26/15.25，**空**，hash 一致。默认关。教训同昨晚：静态指令数≠时间，这核的时间在 LDS/barrier/全局字节读上。
- 21:27 C64 注意力核相位消融（`HIP_MH_ABLATE`，test-abl 模块互换 ABBA，4 块合计，不看 hash）：跳过分数+softmax −0.08ms、跳过 AV −0.18、跳过投影 −0.12，三相加 0.38，而整核 0.87——**一半以上在三段计算之外**（V 字节进 LDS 的搬运、exp 写 LDS、5 次 barrier、输出散写、尾巴）。没有单一相位可打；这一级要动只能重构（每工作组两个窗口摊 barrier/装载，或 FFN+注意力按窗口合核去掉一次全局往返，C32 的 fused 版走的就是这条路）。今晚不做。

## 2026-09-17 22:15 遊戲內探針結果（DLSS5_GAME_PROBE=1，《劍星》1080p，11600 幀）

pre 0.49 ms / network 20.8 ms / post 0.26 ms / gpu_total 21.5 ms / cpu_frame 24.6 ms。插件 D3D12 側前後處理合計 0.75 ms，不是優化對象；網絡耗時與測試台一致。

探針副作用：每幀一次 Flush 插進 ASYNC_SUBMIT 的提交鏈，1080p 掉到 33～35 幀，且地面出現透明（歷史幀錯位）。去掉 flag 後 37 幀、畫面正常。結論：GAME_PROBE 只當調試工具，不與 ASYNC_SUBMIT 同開；讀數只取 pre/post/network 分項，cpu_frame 含 Flush 代價不代表真實幀時間。遊戲 flag 已恢復（HIP_MEMORY / ASYNC_SUBMIT / NETWORK_HEIGHT=auto）。

## 2026-09-17 22:50 C32/post/decoder ISA 統計無病灶；C64 按窗口合核（FFN+QKV+注意力+投影一核）= null

- ISA 統計（`Development/tools/isa-kernel-stats.py`，讀 rtc_compile 的 .hsaco.s）：C32 mapped/chain/finish_main8/chain_finish 2976/3705/3239/4360 條、70 wmma、VGPR 207/199/196/185、佔用 6～8、無 scratch 無 movrel；post_merge_head_half 2959 條/58 wmma/VGPR 172；decoder_project2x_h16w 479 條、佔用 16。C32 家族 cvt 佔 VALU 約 20%（v_cvt_pk_fp8_f32 184、f32↔f16 往返約 150、64 位地址算術約 230 條/核），沒有單點熱點；佔用率之前已用 no-unroll 排除（+1.0 更慢）。結論：候選 3（post/C32 鏈/decoder 再拆）在分析層面關閉。
- C64 合核 `Development/HIP/c64_window_fused.hip`（模塊 c64-window-fused = multihead_fast_padded.hip + 本文件，defines 同 padded-packed；`compile-c64win.ps1`）：一個 256 線程 WG 做一個 8×8 窗口——先按 16 token 一組、兩組並行兩趟跑 mh_ffn_fused_c64_project[_mapped]_g128_qkv 的 body（FFN+投影+QKV+歸一化），normalized 12 KB 和 feature 4 KB 留在 LDS，再跑 c64_attention_project_diag 的 body（ByteFeature 讀 LDS）。主機選項 `mh_window_fused`（env `DLSS5_HIP_MH_WINDOW_FUSED`，CLI `--mh-window-fused`），Body() 在 c==64、producer_norm、非 byte feature、mh_proj_diag 的生產條件下走 `Run("mh_window", c64_window_fused[_mapped][_bytein][_bout], n/64, packed, PackedFusedMhWeight(ffn), PackedMhWeightDiag(attention), out, w,h,ww,hh,sx,sy,post,crop)`；PackedMhWeightDiag 與 PackedMhWeight(…,true) 的 E4M3 前綴相同，QKV 和注意力共用一個指針。
- 第一版 VGPR 196/佔用 6（LDS 37888）：ABBA 15.22/15.19 對 15.21/15.22，dup +2.0（拆開的 c64ffn 1.22 + attn 0.87 = 2.09）。逐相位編譯：只留 FFN 相位 193、只留注意力 79、單趟 FFN 131——兩趟循環裡權重片段的 64 位地址（幾十對）被 LICM 常駐，展開和內存 clobber 都壓不下。修法：每趟開頭用空 asm 洗一遍 tid/wave/rc/group，地址不再是循環不變量，VGPR 196→136（佔用 8，LDS 綁定）。復測 ABBA 15.18/15.20 對 15.25/15.24，dup +2.1。**佔用率 6→8 對時間零影響**：這個家族的成本在每個窗口的串行鏈（FFN 兩趟各 9 個 barrier、注意力 7 個、MMA 延遲），不在流量（早前算過 26 MB 的帶寬下限 0.045 ms）也不在佔用率。四輪哈希全 FEEA9EF3…。代碼留著默認關，模塊不進生產 24 個。
- 至此候選 2、3 都關閉。逐位路線上還沒試過的一條：權重片段的 64 位地址算術換成 buffer_load 的 32 位 voffset（C32 chain 核裡 v_add_co/ci 124 條 + lshlrev_b64 50 條 ≈ 4.7% 指令）。非逐位的路（PK_ACT=2 已測 +0.08 更慢、放寬精度已封、跳塊有性能檔）不再看。
- 23:05 buffer_load 這刀的底數：COMGR 的 clang 21 接受 `__builtin_amdgcn_make_buffer_rsrc` + `__builtin_amdgcn_raw_buffer_load_b64`，探針（hip-backend\buf_probe.hip）出 `buffer_load_b32 v, v, s[4:7], null offen offset:4096`，32 位 voffset、立即數偏移可折。C32 chain 核的 ISA：global_load 60 條已是 saddr 形式（SGPR 基址 + 32 位 VGPR 偏移，不用 64 位加法），54 條 load + 16 條 store 是 vaddr64 形式——這 70 處就是 62 對 v_add_co/ci + 50 條 lshl_b64 的來源（≈170 條，4.6%），都在按 token 的輸入映射讀和輸出寫上，不在權重。post 核 94 saddr / 19 vaddr64。換成 buffer 形式要在 c32_fused_body 的 70 個讀寫點各換一個 helper，上限約 C32 家族 4.1 ms 的 4%，且 09-16 的 no-unroll 實驗說明這個核吃的是 MMA 流水不是指令數，實際能拿到的估計 ≤0.1 ms。未做，等 Zero 決定值不值。

## 2026-09-17 22:55～23:06：5090《剑星》多遍 NR 试装与回退

- **09-17 22:55 5090《劍星》裝了 DLSS5-ReShade-AIO v2.2.4（kibblerz）**——Zero 要「三倍 DLSS5」= 這個插件的 NR pass count 2x/3x（同一幀網絡串跑 N 遍，每遍獨立歷史，成本 ×N；每次啟動回 1x）。遊戲目錄 `D:\SteamLibrary\steamapps\common\StellarBlade\SB\Binaries\Win64`：ReShade 6.8.0 addon 版原本就在（d3d12.dll）；新放 standalone-dlssnr.addon64、nvngx.dll（AIO 橋）、兩個 .fx，nvngx_dlss.dll / nvngx_dlssg.dll 從遊戲自帶的 Plugins 目錄複製；原 renodx-dlss5.addon64 和 7 個逆向探針 addon 移到 `Win64\before-aio\`（兩套 NR 不能同掛）。回退：`powershell -ExecutionPolicy Bypass -File D:\work\aio\install-aio.ps1 -Action Restore`（遊戲關著）。用法：遊戲裡關自帶 DLSS/FG/AA → ReShade 面板 Add-ons 頁 → Standalone DLSS-NR + SR → NR pass count 3x；F10 對比源畫面，Ctrl+Alt+N 換 NR 模型 1/2/3。不同於 Magpie：它是 ReShade 插件鉤遊戲自身 D3D12，運動矢量走 NVIDIA 光流。
- **09-17 23:06 AIO 已從 5090《劍星》去掉**（Zero 試了 3x：效果沒差別，卡到不能玩）。Restore 跑過：renodx-dlss5 + 探針 addon 回位，AIO 的 addon64/nvngx.dll/nvngx_dlss(g).dll/.fx 刪除，ini 復原。結論：多遍 NR 在 5090 上也不划算，不再碰。

## 2026-09-18 00:20 第二個 FFX/XeSS 遊戲《黑神話：悟空》（9070 XT）——鉤子通用化的三道坎

- 目標：驗證「遊戲有 FSR3.1/FSR4/XeSS 就能直接用《劍星》包」；副線：OptiScaler（只有 DLSS 的遊戲）怎麼拼。網友拼 danielblnc 的方法 = OptiScaler（dxgi.dll，鉤遊戲 DLSS/XeSS/靜態 FSR3 輸入，用自帶 amd_fidelityfx_dx12.dll 跑 FSR4）+ 他的 version.dll 代理鉤 ffxCreateContext/Dispatch；三份同一 DLL = 三遍。
- 坎 1：黑神話（UE5 出貨版）的 FSR3 超分**靜態鏈進 exe**（`ffxFsr3UpscalerContextCreate` 在 b1-Win64-Shipping.exe 裡），目錄裡的 amd_fidelityfx_dx12.dll 只給插幀；FFX 鉤子裝上了（hook_status=0）但 ffxDispatch 零調用（第一次的 2500 幀是插幀）。裝了 OptiScaler 0.9.4 鏈（dxgi.dll + ReShade64.dll + LoadReshade + Dx12Upscaler=ffx）也沒截到，未深究（它的 FSR4 走了 loader→upscaler_dx12 4.1.1，不經 amd_fidelityfx_dx12.dll），已回退。
- 坎 2：UE5 在錄製線程之外的 RHI 線程提交，且升採樣列表在批次中間（第 1/3、第 10/13）。加 `DLSS5_SPLIT_SUBMIT=1`（flag 文件）：按列表身份任意線程匹配、落後 ≤2 幀、**把 ExecuteCommandLists 從該列表後切成兩半**，網絡插中間。《劍星》不開 flag 行為不變。
- 坎 3：走 XeSS：`DLSS5_UPSCALER=xess` 現在也認 flag 文件，且明確 xess 時**只等 libxess**（黑神話啟動就加載 FFX DLL，原邏輯 FFX 永遠先中）。鉤上後幀在流：輸出 1600×900 **DXGI 26 = R11G11B10_FLOAT**、輸入 1280×720、速度 R16G16F 1600×900。但 armed=0：XeSS 路還是《浪人》時代的合同——只認 1920×1080、只在第 120 幀武裝、無提示文字；且 codec 只收 RGBA16F/RGBA8（NativeIsGameColor），R11G11B10 要加一個轉換 pass（→RGBA16F 暫存跑網絡→寫回）。運動矢量符號（浪人是 −1）UE 也待驗。
- 下一步（明天）：① XeSS 路對齊 FFX 路（supported_input / 相位 0、5 重武裝 / 提示文字）；② R11G11B10 轉換 pass；③ 黑神話上驗 split submit；通了再寫「遊戲通用包」教程（A 檔直接丟三樣，B 檔 OptiScaler）。DLL 現裝在黑神話目錄的是 e7d4371e（split + xess 選擇），《劍星》未動。

## 2026-09-18 00:50 黑神話收尾：R11G11B10 + XeSS 對齊 + 延遲掛鉤已寫，實機沒驗；黑神話目錄已復原

- 代碼（本次提交，都沒在遊戲裡驗過，《劍星》裝的 DLL 未動）：① `NativeIsR11G11B10` / `NativeBytesPerPixel`，codec 新增 `r11_out`（原始緩衝 4 B/像素，`NATIVE_CODEC_R11_OUT` 解碼分支打包 f11/f11/f10，幀按 footprint 拷回）；② XeSS 路對齊 FFX 路（supported_input、相位 0/5 重武裝、提示文字、UAV 狀態）；③ 鉤子推遲到 present 30 幀後再裝（避開啟動期加載器鎖的猜想——後來證明黑神話卡死與此無關）。提示層 `native_text_overlay.h` 的 CopyFormat 仍不認 R11，黑神話上黃字不會顯示，待補。
- 黑神話啟動故障：UE 日誌 10 行、開檔後 62 秒 `Log file closed`，卡在 `Unreal.js started` 之後、EOS/Steam SDK 初始化之前；**把 dlss5-amd.addon64 停掉（ReShade 留著）照樣復現**，Windows 無崩潰事件，機器無殘留進程 → 遊戲/Steam 側的問題，不是插件。00:05 之前四次啟動全正常，之後越來越頻繁；Zero 決定不折騰。
- 目錄已 Restore：ReShade、addon、DLSS5-AMD、before-opti 全刪，遊戲三個 FFX DLL 原版（1.0.1 / 2.1.0）。
- OptiScaler 結論保留：對純 DLSS 遊戲要走 OptiScaler（dxgi.dll + FFX 輸出 + LoadReshade + 咱們三樣），黑神話因 FSR3 靜態鏈接屬於這類；沒驗證。工具留在 D:\DLSSNR-Lab\opti\、deploy-wukong*.ps1。

## 2026-09-18 17:50：多 GPU 主機初始化失敗修復（網友反饋）

- 網友日誌：`event=initialization_failed detail=bridge currently requires exactly one HIP GPU`，畫面上 INIT FAILED。原因：`hip_d3d12_bridge.h` 的 `Create()` 要求 `hipGetDeviceCount()==1`，而 `Network` 構造又寫死 `hipSetDevice(0)`；帶核顯或第二塊卡的機器 HIP 數到 2 就拋錯。
- 修法：橋接先取 D3D12 適配器名（DXGI 按 LUID 查），再枚舉 HIP 設備逐個 `hipDeviceGetName` 找同名的那塊（第一個匹配），`hipSetDevice(chosen)` 後才構造 `Network`；`Options` 新增 `device` 索引，`Network` 用它代替 0。沒有同名設備時報 `no HIP device matches D3D12 adapter '<名>' (HIP devices: 0:… | 1:…)`，日誌 `native-hip.txt` 多一行 `hip_device=<索引>`。單卡機器行為不變（chosen=0）。
- 未覆蓋：兩塊同型號卡（按名字只能挑到第一塊；要精確得對 PCI bus id 與 LUID，這版沒做）。
- 構建：`build-addon.sh --hip` → `native-multigpu.addon64`（sha256 fda2edab…，3595356 B）；`--tiled` 也重編通過。已拷到 9070 `D:\DLSSNR-Lab\native-multigpu.addon64`，部署腳本 `D:\DLSSNR-Lab\deploy-multigpu.ps1 -Action Install|Restore|Status`（《劍星》+ 本機 Magpie 目錄，備份 `*.before-multigpu`）。**未在單卡機上回歸、未給網友驗證**；下個 tag 打包前要先在《劍星》跑一次。
- 18:00 Zero 在《劍星》回歸 native-multigpu（fda2edab…）：1080p 37 fps、900p 52 fps，畫面正常，與 0.22 一致。
- 18:05 打包 0.23：`package-hip.ps1 -Version 0.23 -Addon native-multigpu.addon64`（fda2edab…）+ ffnh2-modules，底仍是 0.15-900P 暫存目錄。產物 `DLSS5-AMD-0.23.zip`（493 文件，250,670,338 B，sha256 3dde0e0a32f1250bdda292fb42261e3d981c60b4d608d5e3f732984e0f2404b0）、`Magpie-DLSS5-AMD-0.23.zip`（698 文件，354,855,813 B，sha256 7146569c61b6b3e957ac2e211ba8bbc35a567fdd5939e79894a9e20defdaf0ef）。包內說明加「0.23 與 0.22 的區別」；README 兩版加 0.23 行（網盤鏈接待 Zero 上傳後補）。內核/權重/flag 與 0.22 相同，`verify-pkg023.ps1` 跑 960 兩道黃金。


## 2026-09-19 10:45：OptiScaler组合包在《剑星》验通

OptiScaler 0.9.4 + ReShade + HIP在《剑星》验通。配置必须 `Dx12Upscaler=fsr31`，旧说明中的 `ffx` 会静默落到FSR2.1；游戏菜单“FSR3”只是输入接口。组合包默认关闭插帧、LoadReshade=true、Dxgi=false。

## 2026-09-19：合并旧context，统一开发记录入口

在main开发，生产宿主src/、内核hip/、实验Development/HIP/。旧context/dlss5-9070移植.md已合并删除，不再重建，独有旧记录已并回对应日期。后续沿用“时间＋事件”的正序日志格式，重复操作合并到对应事件；详细数据留结果文件，阶段commit照常。

### 运维入口与工作约定

- AMD 机：`ssh amd9070`，实验根 `D:\DLSSNR-Lab`，《剑星》`C:\Program Files (x86)\Steam\steamapps\common\StellarBlade\SB\Binaries\Win64`。5090：`ssh rtx5090`，《剑星》`D:\SteamLibrary\steamapps\common\StellarBlade\SB\Binaries\Win64`。每次操作前读实际文件与进程，历史 DLL 名、目录名不等于当前安装。
- 发布成品统一 `D:\給網友打包`；开发资产和实验仍在 lab。给网友的配置不含本机绝对 `DLSS5_HIP_MODULES` 路径，默认模块位置是 `DLSS5-AMD\native-game-tiled-assets\HIP`。
- 游戏或 Magpie 运行中不换 DLL/HSACO，也不同时跑 GPU 测试台。改动独立 flag/宏及可回退部署；每刀报进度，commit 不加 Co-Authored-By。开发过程材料在 `Development/`，版本发布时把验证过的源码同步到 `hip/`、`shaders/`、`src/` 并实际编译。
- 打版核验：HIP 与 DX12 addon 都编一次，`hip/build-modules.ps1` 全量重编，900w 与 960 两套黄金输出验证；HLSL 有改动才重编对应 cso。09-10 的笔记本干净 checkout 验收曾抓到缺 10 个源、3 个手工 cso 和过期 flags，不能只验开发机缓存。09-17 起直接在 main 开发，旧 HIP 分支不再用。
- 游戏内包优先读取 DLL 旁 `DLSS5-AMD`，不存在才回落 lab。09-10 的“开发机不要留同名目录”针对旧全局部署脚本；后来的 HIP 游戏私有包和 OptiScaler 包明确使用该目录，不能照旧规则删。
- 远程 UI 走交互计划任务：`dlss5game` / `dlss5magpie` / `dlss5toggle` / `dlss5shot` / `dlss5rgp`；Magpie 工具栏与分析器是 `dlss5toolbar` / `dlss5profiler`（WM_HOTKEY id 2/3）。Magpie 启停热键是 **Alt+Shift+A**。
- 无游戏测 Magpie：`logs\anim.ps1` + `dlss5anim` / `dlss5toggleanim`；`logs\runC.ps1` 启动 Magpie；`logs\gameRun.ps1` 启游戏到菜单取 every_frame 后关闭；`logs\quiet-bench.ps1` 做安静测试与分段汇总。这些旧脚本使用前先看内容，避免意外关闭用户正在玩的游戏。
- 离线重放：`logs\replay.ps1 -Folder <decout16拷贝> -Flags logs\replay-flags.txt`，输入色从 f16 转 input.f32；`DLSS5_TEST_EPILOGUE_MODE=8` 看头部累加器。`Development/tools/dump.sh` 抓中间量；bench.ps1 变了用 `make-norebuild.sh` 重生无编译 runner。
- 掉帧诊断：`Development/tools/gpu.ps1` / `shared.ps1` 区分 CPU 提交与共享显存驱逐；测量关 Splashtop、核实帧率上限。曾有游戏锁 30fps 掩盖优化收益；Engine.ini 的 `r.Streaming.PoolSize=6000` 曾帮助驱逐后恢复，原备份 `.before-poolsize`，不作为所有场景的默认设置。
- 预览 DX12 路线曾被 AMD Install Manager 与 Windows Update 自动换驱动破坏：前者计划任务已禁用，后者设 `ExcludeWUDriversInQualityUpdate=1`。这是 SM6.10 阶段事故；HIP 0.20 起不再要求该预览接口，正式驱动仅有用户可用反馈，不能混成亲测。
- `DLSS5_GAME_PROBE` 每帧 Flush 会破坏异步提交的时序，曾出现地面透明；不要与 ASYNC_SUBMIT 同开。旧探针 pre/post/network 分项可用，含 Flush 的 cpu_frame 不能当正常帧时间。

### 补充工程教训

- RGP 可能按占位 DXIL 哈希读到旧 ELF；抓 HLSL 前用 `Development/tools/dxilhash.py` 签名 cso。09-10 的 p0087 是未派发 full_attention PSO，不能拿它的 scratch 诊断实际融合核 p0006。
- codec 每个输入 SRV 用各自资源描述；复用改过的输出 desc 会建立 UNKNOWN 格式视图，造成 device removed（887a0001）。
- post 有 shift 3，写输出栅格必须减去 shift；pre 没 shift，pre 正确不能证明 post 正确。
- fxc cs_5_1 的 round/exp2 与 dxc 路径有舍入差异，搬核时区分结构逐位检查和对 exact 的 PSNR 检查。C32 的 f16 中间流优化不能直接套给已是 E4M3 的多头/C512。
- 内核形状变化先算权重 B 的重读量；减少 barrier/转换/寄存器不自动变快。单核隔离会把工作集留在缓存，不能直接相加当整帧；按实际生产路径做重复 dispatch/launch 与同批 ABBA。旧 null 可在依赖变化后重测，但原方案、基线与变化必须说清。

### 同行与写作素材（仅保留当时调查结论）

- 09-10／09-17 调查 `danielblnc/DLSS-NR-on-AMD`：闭源 HIP、安装时从 310.8 原 DLL 抽权重、FFX API 钩子，二进制使用 amdhip64_7.dll/hipLaunchKernel。已检查的 setup.exe（`scratchpad/nramd/setup.exe`，sha cf7ada14…）没发现本项目环境变量/核名标识，记录结论是独立平行实现，没有抄袭证据。其自报帧率不能作为与本项目的同条件跑分。
- `MatheusGViana/dlss-5-amd-project` 当时是 OptiScaler fork 包装上面的闭源 DLL，提供多遍/顺序/颜色开关，网络本身无源码；`SAOG0721/Magpie` 是 NVIDIA DLL + 光流的截图后处理路线。以上都是当时版本观察，不当成永久产品状态。
- OptiScaler 组合在黑神话未验通的历史，不能外推为接口不可用；09-19 已在《剑星》验通，并发现 0.9.4 的 fsr31/ffx 配置名问题，见同日记录。
- 公众号 296/297/298/299/301/304 是早期 DLSS5 系列，304 是优化篇；0.11 安装教程位于 `wechat/dlss5-amd-0.11-安装教程.md`，0.20 教程为 `wechat/dlss5-amd-0.20-安装教程.md`。写作定位是学习笔记，实验结果与未验证推测分开记录。

## 2026-09-19 12:48：低分辨率DLSS5前置链跑通，发布0.24

实现低分辨率颜色 → DLSS5 → FSR → 最终输出：`src/native_pre_upscale.h` 拦下FFX dispatch并保留资源，到原列表提交后用自有列表运行网络、重放FFX，再提交剩余列表。只适用于超分调用后同列表没有消费者的场景；**不关闭/重置游戏列表，不移除following_work安全检查**。PRE_UPSCALE=0旧路径、1网络前置、2仅延后FFX烟测。模式1仍每帧reset网络历史，FSR自己的时序不变；没有完成抖动输入的网络跨帧契约。

关键修复：接受合法upscaleSize=0；自有列表必须由FFX的ReShade包装设备创建，提交原生queue前unwrap，否则包装描述符堆导致E_INVALIDARG；保留私有颜色纹理flags，只校正该纹理重放期间的barrier。删去reset模式无用motion提交，PRE_UPSCALE_ASYNC=1消除额外CPU等待，资源按最终fence保留。同步/异步GPU烟测通过。《剑星》2560×1440输出、1707×961输入实玩约34～35fps（1080网络档）；4K未验。

## 2026-09-19 13:20～13:44：RE9前置契约不兼容，资产路径修复进入0.24.1

《生化危机9》安装OptiScaler+REFramework后可进菜单，但网络前置被 `UNSAFE: draw/dispatch after deferred upscaler in same list` 拒绝，不能靠改flags解决。DLSS5在根目录及_storage_缓存均已改.off，保留基础组合。工具 `Development/tools/optiscaler-re9.ps1`，远端 `D:\DLSSNR-Lab\re9-opti`（before备份、Restore还原）；REFramework来源/校验在 `release/re9`。该测试发现DLL被加载到_storage_时资产误落旧lab，0.24.1修为DLL旁→游戏exe旁→lab。此修复已回归《剑星》，不代表RE9兼容。

## 2026-09-19 14:31～15:29：《剑星》主城掉帧定位并修复

原整套链同地点：朝城外F6 OFF60/ON43，朝城内OFF30/ON31。显存/温度未见足以支持驱逐或降频的证据，GPU利用率和等待线程本身不能定因；游戏专用FrameLimit实际60，不能只读通用FrameRateLimit=0判无限制。

先修F6真实旁路：关闭后不再新捕获FFX、不复制私有颜色；已捕获任务仍重放一次。该修复正确但主城仍30fps。随后逐层对照：**原生游戏、仅OptiScaler、OptiScaler+ReShade无addon均60fps**。原生对照须恢复安装前6.6MB游戏FFX库，不能只移走dxgi；旧备份中的d3d12.dll是ReShade代理，不应当原库恢复。

根因范围落到addon：draw/dispatch/barrier在没有待处理任务时仍查环境、取native对象、锁全局Jobs；日志配额耗尽仍逐draw原子递增。以锁内发布的pending原子标记快速跳过空闲观察，保留有任务时的following_work/状态检查，私有barrier修正不依赖pending；日志满额先读计数。用户复测“正常了，49帧”，支持修复有效；该条未单独给出F6状态及OFF帧率，不补写OFF60。两种改动的贡献未分别测量。

验证：`Development/pre-upscale-smoke.cpp` 同步/异步各6帧，覆盖F6边沿、直接旁路、捕获后关闭、恢复、pending发布/清除与following_work；每帧FFX恰好一次，4096个half读回全一致。主城修正版 `release/HIP/native-idle-tracking.addon64` SHA256 `395dabfe20261832fac8a43188eb69661baa52eb140c8a99655d8b7f4e4ac75d`。

部署/回退：`Development/tools/stellarblade-native-city-test.ps1` 支持Native/OptiScalerOnly/ReShadeOnly/AddonCandidate/Restore/Status；当前记录state=addon-idle-tracking-candidate。备份 `D:\DLSSNR-Lab\pre-upscale\before-native-city-test`，Restore恢复当时完整插件链并保留现行游戏画质设置。更早F6备份before-f6-passthrough。所有换DLL须游戏退出。

## 2026-09-19 15:37～15:55：Magpie测试包回归与内核文件加载报错

用户回测优化效果不明显；它不开前置路径、回调远少于游戏内，本就不会付出同量Jobs锁开销。中文目录“給網友打包”运行时报 `c32_prefix_reference.hsaco: hipErrorFileNotFound(301)`，文件实际存在，建议英文目录后用户继续测试；尚未通过代码修复/严格A/B确认编码机制。网友日志 `C:\Users\lmxxf\Downloads\logs202009131113` 同样27次报该错误，但未记录安装路径，应先检查完整解压和英文路径，不能直接判同因。

## 2026-09-19 16:14～16:18：修复前置路径FPS显示开关

前置路径原先无条件绘制状态/FPS。改为首次从flags读取并缓存：NOTICE默认2，0/1跳过文字Prepare/Draw；SHOW_FPS默认0，非零且NOTICE>=2才显示FPS，关闭FPS保留状态行。修改后重启。HIP与DX12编译通过，屏幕回归待做。修正版 `release/HIP/native-overlay-flags.addon64` SHA `be9e82cee99037ef92eb2bec18acc51c428ec3cd02ccf80a1fd1033b3a8c3119`（源码提交300a565）。本机DLL/flags备份before-overlay-flags。旧完整包归档before-0242-overlay-repack；误发的1MB补丁已移到pre-upscale/overlay-patch-archive，**给网友发完整包**。

本机《剑星》仍为主城修正版395dabfe…，尚未换be9e82…；flags已改DLSS5_SHOW_FPS=0，待部署后验证显示。

## 2026-09-19 16:21～16:27：0.24.2完整包重打并更新下载

成品目录 `D:\給網友打包`，完整包及.zip.sha256：

| 包 | 字节数 | SHA256 | 下载/状态 |
|---|---:|---|---|
| OptiScaler-DLSS5-AMD-0.24.2.zip（显示开关修复重打） | 385886558 | `91cc2ce444edef687dba7236867a1b343836c1bc4dba151eb864f422d52e21a8` | https://pan.quark.cn/s/f74aaa5c7f9a |
| Magpie-DLSS5-AMD-0.24.2.zip | 354881266 | `167a3dcaee84770cee505bf857a7614368846e505c229dc71944835ac989bccf` | 本机测试包，未收到上传链接 |

两包分别513/698个有效载荷从zip逐项读回校验，24个HIP模块与基底一致。网络权重/内核未改。README链接挂在版本行的OptiScaler文字上；历史0.24/0.24.1下载见README。工具 `Development/tools/optiscaler-stellarblade.ps1 -Action Release`、`package-magpie-candidate.ps1`；Magpie基于0.23，显式PRE_UPSCALE=0，流水线与auto档保持。

## 2026-09-19 16:30～16:37：1080档基线与分块候选初筛

开始前检查游戏/Magpie均退出，下次运行仍须现场核实。

当前源码编译benchmark1080.exe；脚本 `Development/HIP/profile-1080.ps1`、`tune-1080.ps1`。固定真实HDR输入1296×720，强制网络有效1920×1080/处理1920×1152，默认跳42/43/46，每帧reset（对应前置行为）。每轮40帧，舍前8帧取中位，检查首尾有限与最终hash。**21.15ms包含NativeGameFrame转换、桥接与同步，不是纯网络或原生1080抓帧，不能直接对比网友“1080P12ms”。**

四轮基线21.144/21.1215/21.1705/21.1765ms，最终SHA `1DE20C219105CC281CBA1364A0A21D0DDC4510A95458EC0CA3B8C893EA54F23A`。重复执行法的边际成本：C32约4.91ms、ViT2.80、C256 FFN1.92、post1.67、C64 FFN1.64、C128 FFN1.54、C64 attention1.33，其余各<1ms；不是可直接相加的层时间，也不等于能省掉的时间。详细表/初筛结果在 `Development/results/1080-20260919/`，远端完整数据 `hip-backend/profile1080`。

候选结论：ViT M2无收益；M4 ABBA基线21.1305/21.1335对21.134/21.050，差异小且不稳定，暂不采用；小通道tiled FFN慢约0.41ms，关闭；W16+diag组合缺所需内核入口，停止该项，未计性能。成功轮次最终输出均逐位一致。**本阶段无生产变更；下一步优先分析C32内部阶段/占用率，再看ViT。** 源码/结果提交65c711a。

## 2026-09-19 16:41：C32概率配对转换与1080阶段消融

先回读旧结果，no-unroll虽降低VGPR却慢0.64～1ms，局部同步与新lane staging组合还曾破坏逐位，故不再盲试占用率/删barrier。新C32概率配对FP8转换（独立patch，未改生产）COMGR通过、1080最终hash不变，ABBA基线21.1055/21.2255、候选21.109/21.1705ms，无稳定收益，暂不采用。当前源的1080相位消融：基线21.1255/21.125，跳FFN19.6545（−1.47）、跳注意力19.053（−2.07）、跳投影20.6465（−0.48）；消融会改变输出，只定位阶段，不能部署或把差值当可直接取得的收益。脚本/patch为 `test-c32-pair-prob.ps1`、`c32-pair-prob.patch`、`ablate-c32-1080.ps1`，结果并入同一1080结果目录；完整远端模块在hip-backend/c32-1080-ablate{1,2,3}。下一步细分注意力的分数生成/归一化/概率写入与AV，找数据传递和指令依赖开销；这轮没有新生产优化。


## 2026-09-19 16:58起：C32注意力细分与指数值寄存器复用候选（约−0.19ms）

1080细分消融：基线21.1235/21.1335ms；跳Q×K乘法20.888（−0.24）；固定归一化分母20.4085（−0.72，同时省求和并简化除法，不能全归因于MMA）。独立诊断patch `Development/HIP/c32-attention-phases.patch`，ablate脚本支持Source/Phases，4/5模式仅计时，绝不部署。

实际候选 `c32-register-ex.patch`：生成指数half值时留在h8寄存器数组，归一化乘法复用，避免回读scratch.ex；原求和、数值与同步保持。首版动态数组索引令chain出现1814条cndmask，ABBA约21.12→36.47ms，private段仍0，不是spill。显式展开固定key/e循环后，选择指令降到49，代码体积478360→241944字节；ABBA基线21.113/21.103、候选20.9255/20.919，约−0.186ms（0.88%），最终输出hash不变。

验证脚本 `test-c32-register-ex.ps1` / `validate-c32-register-ex.ps1`；720/900/1080档×连续历史/每8帧reset共6组，每版24帧全部有限、首尾half输出逐位匹配。仍是同一张1296×720真实HDR输入，尚未跨内容/seed扩验或游戏回归；候选保留独立patch/模块，生产源与游戏未改。数据并入Development/results/1080-20260919（regex-indexed为失败首版、regex为展开版、regex-validation为多档检查）；远端模块hip-backend/c32-register-ex-modules。下一步先补不同输入/seed以及900档性能，过关再决定生产采用；归一化数据传递继续是研究方向。


## 2026-09-19 17:04起：C32寄存器复用扩验通过，纳入生产源码

测试台新增可选seed/pattern参数，旧命令行为保持：pattern0真实抓帧，1合成HDR纹理（<8），2暗部/次正规half梯度。900/1080档×真实seed123、HDR seed123、暗部seed9876共6组，每版24帧全部有限、首尾输出逐位一致；此前720/900/1080×两种历史模式检查亦通过。900档ABBA基线14.229/14.2395，候选14.108/14.057，约−0.152ms；1080上一轮约−0.186ms。扩验及900性能数据在同一results/1080-20260919目录。

将c32-register-ex.patch并入hip/c32_fused_ffn_attention.hip，补注释强调固定索引循环必须展开。正式build-modules.ps1配方重编两个受影响模块：普通版SHA145d3dd186db27ab13ae82b17a6690992fd93de8e97599552c6a454ff42aed1c，packed版71baedcc0486790c48c884917ab96faa722f781bf415d195e195928e651921de；hip/SHA256SUMS同步。其余22模块沿用基底，未重编无关内核。生产候选远端hip-backend/c32-regex-production-modules。

旧黄金脚本先因过时资产目录报noise missing；为validate-hdr/validate-modules/validate-modules-960补Assets参数后，用0.23完整资产重跑，900w与960两套共六道黄金值全部匹配（40帧、每8帧reset、seed123参考历史）。主机DLL和发布包未变，尚未部署游戏；下次需要部署的是新的HSACO，不是重新编DLL。本轮收下约1%的确定收益，下一步仍可继续研究归一化的数据传递，不再重复已证伪的占用率捷径。


## 2026-09-19 17:15：《剑星》安装C32优化内核与显示修正版

用户要求实玩测试；确认游戏/Magpie退出后，用Development/tools/deploy-stellar-c32-regex.ps1备份并替换两个C32 HSACO（145d3dd…/71baedc…）及此前待装的显示修复DLL be9e82…，共3文件逐SHA验证。备份pre-upscale/before-c32-regex，脚本-Action Restore可回退。flags前后SHA一致：auto、SHOW_FPS=0、PRE_UPSCALE=1、ASYNC=1；保持游戏图形设置，未启动游戏。待用户用Steam/AMD帧率显示比较，插件FPS应隐藏但状态行保留；整行隐藏另设NOTICE=0。17:22用户确认同一地点49→50fps，小幅收益与离线方向一致。


## 2026-09-19 17:24起：C32归一化有界倒数，追加约0.05～0.06ms收益

确认游戏/Magpie退出后继续。C32指数half范围[2^-14,9.75]，64项分母在[1/256,624]；原通用除法含div_scale/fmas/fixup。用硬件rcpf加两次FMA误差修正，chain静态指令行3669→3603、div系列32→0。`test-normalize-rcp.cpp/.hip`在gfx1201遍历该区间全部144,441,345个float，结果与原1.f/x逐位全同（记录normalize-rcp-exhaustive.txt）；限定本设备/编译路径的实测，不外推其他硬件。

相对上一刀寄存器复用基线：1080 ABBA20.9115/20.9215→20.862/20.871（−0.050ms）；900 14.0755/14.0715→14.0225/14.006（−0.059ms）。900/1080×真实seed123/HDR seed123/暗部seed9876六组首尾逐位匹配、24帧均有限；正式配方重编后900w/960六道黄金也全过。源码纳入hip/c32_fused_ffn_attention.hip，SHA表同步：普通模块0f3aff3fb906bd493443b1c74440716969b780d39ff98c785412dd36112f042b，packed模块31e295cc7d90ce7f77accc7bb517562c8ecbbff44269a2587298135eb378f7a1。远端生产模块hip-backend/c32-normrcp-production-modules；未部署，剑星仍为上一刀。实验patch/脚本留Development/HIP，数据并入同一1080结果目录。下一步可评估相同有界倒数在多头注意力的适用范围与实际收益，不直接批量替换所有除法。


## 2026-09-19 17:33起：有界倒数扩到多头/ViT，收益很小，暂留候选

多头注意力与C32指数范围相同，保留其两路partial sum，只替换最终倒数；ViT融合注意力指数范围不同（单项[0.0040283203125,1.640625]、最多640keys），扩大倒数穷举到[1/256,1050]：151,207,937个float全同，记录normalize-rcp-exhaustive-1050.txt。生产源码未改，独立mh-normalize-rcp.patch/vit-normalize-rcp.patch及测试脚本留Development/HIP。

相对已采用C32倒数的基线：MH 1080初轮约−0.030ms，80帧ABBA基线20.9225/20.971对20.9085/20.937，约−0.024ms且有漂移；900基线14.0555/14.0405对14.0165/14.0005，约−0.040ms。ViT 1080基线20.8095/20.8385对20.8125/20.8025，差约0.017ms，与波动相当，暂不采用。所有计时轮次最终输出hash相同。数据在同一1080结果目录（mhrcp、mhrcp-long、mhrcp900、vitrcp）。没有部署/重打包；下一步优先研究MH概率/指数中间量的数据传递，倒数优化的剩余收益已很薄。


## 2026-09-19 17:42起：多头指数half寄存器复用，整组约−0.08～0.10ms

C64/C128/C256 attention-project借鉴C32：指数half保留h8寄存器，概率计算免回读ex，固定key/e循环显式展开；保留MH原来的双partial sum和同步，未混入上一轮未采用的MH倒数。初轮1080约−0.066ms；逐形状对称拆测无明确单项收益（C64略慢），因此追加100帧ABBA：基线20.983/21.0005，整组20.875/20.9175，约−0.096ms。900基线14.0585/14.0835，整组13.985/13.9935，约−0.082ms。仅确认整组实测收益，不将单项时间相加或宣称已解释原因；无scratch溢出。

900/1080×真实/HDR/暗部、seed123/9876六组24帧均有限、首尾逐位匹配；正式配方重编后900w/960六道黄金全过。源码并入hip/multihead_fused_attention.hip，SHA256SUMS更新multihead_fused_attention.hsaco为dea239e4aaa36825884ca052aeea045bb1ba368afa738f728b758a1930c7ba6c。最新完整模块目录hip-backend/mh-ex-production-modules，包含已采用的C32复用/倒数；游戏与发布包未动。实验mh-register-ex.patch、test/split-mh-register-ex.ps1和mhex系列CSV归档。下一步可看Q/K全局读取复用，但需评估额外LDS对并行度的代价，不机械推广缓存。


## 2026-09-19 17:49起：多头Q/K缓存与预取三种尝试均不采用

基于已采用MH指数寄存器复用的mh-ex-production-modules，尝试复用ex共享空间暂存Q/K，全部分数先留寄存器、全组同步后再写ex，LDS大小不增加但多一道同步。1080 ABBA基线20.695/20.7205，候选20.7485/20.7565，约+0.045ms更慢。只缓存K、Q沿原路径：基线20.696/20.7405对20.7295/20.729，变化在噪声内，无收益。最后把Q/K提前读到固定索引寄存器数组（不加共享缓存/同步）：20.734/20.727对20.710/20.7435，同样无稳定收益。三者COMGR通过、所有最终输出hash匹配；不以省读次数推断提速，不进生产。独立patch/脚本mh-qk-cache、mh-k-cache、mh-qk-prefetch留Development/HIP，mhqk/mhk/mhqkpre数据并入同一1080结果目录；游戏与包未动。


## 2026-09-19 17:56起：多头概率DWORD布局与融合AV共读均无稳定收益

概率按[row][key%16][key/16]存储，让每lane的pv整数一次写4byte，AV按新布局取原概率；LDS容量及数值不变。1080 ABBA基线20.701/20.756，候选20.7615/20.7305，无收益。C64 diag静态指令1803→1855，概率32个byte写变4个双地址DWORD写，但总指令增加；不采用。

再针对当前c64/c128/c256 attention-project做AV双输出共用A片，保持每片K累加顺序（区别于09-16旧mh_attention_fused_fp8_out入口）。ABBA20.7545/20.788对20.7725/20.744，同样无稳定收益；C64 diag共享读写计数完全相同（ds_load_u8仍64），总指令仅1803→1801并有调度差异，不能说生成代码逐字相同，说明源码共读未减少实际读取。两候选COMGR/最终输出hash通过，不扩测或部署。mh-prob-dword、mh-project-av-pair的patch/脚本与mhprob/mhavpair数据归档，生产不变。


## 2026-09-19 18:00起：跨核字节接口复测，局部特征字节＋快速残差组合有效

当前mh-ex-production模块上，1080完整字节流仍慢：基线20.8135/20.851，MH21.0005、ViT21.3595、两者21.443ms，输出hash均同，不采用。查host路由：完整MH链的byteout不走diag快速残差；旧“字节流不值”的结论混合了存储/计算路径变化，不能泛化到所有局部接口。

只启用DLSS5_HIP_MH_FEATURE_BYTE=1与DLSS5_HIP_MH_PROJ_DIAG_FB=1，FFN→attention特征按原FP8格点存byte，同时保留矩阵残差路径，链间输入/输出仍f32。1080 ABBA20.798/20.8645→20.2295/20.283，−0.575ms；900 13.975/13.983→13.769/13.744，−0.223ms。900/1080真实/HDR/暗部及seed123/9876六组、720连续历史/每8帧reset两组均24帧有限、首尾逐位匹配。720性能未单独测，不宣称提速幅度。

代码已有两个开关，无需重写或重编内核；配套配置片段Development/HIP/feature-byte-flags.txt，须追加到完整flags，且使用含*_fb_diag入口的最新mh-ex-production-modules，不给旧包裸开。recheck-byte-stream-1080、recheck-feature-byte-1080、validate-feature-byte脚本与bytes1080/featurebyte结果归档。生产默认/游戏/发布包暂未改；下一次部署可将此组合与最新完整模块一起带上，再实玩回归。


## 2026-09-19 18:16起：局部特征方案黄金通过，完整字节流扩验发现旧反例

局部MH_FEATURE_BYTE+MH_PROJ_DIAG_FB方案补过900w/960共六道黄金；脚本golden-feature-byte.ps1，参考runner从当前源重编。validate-modules支持Runner/Reference/Flags/Modules/ReferenceArgs，960脚本支持ExtraFlags/ReferenceArgs，避免旧runner忽略新增开关。局部方案保持已验证配置片段，默认/游戏/发布包未改，待与最新模块配套部署。

另做隔离候选mh-byte-stream-diag.patch：为c64/128/256导出fb_bout_diag，并让host在byteout时也选择快速残差；仅在/tmp/mh-byte-diag-build副本编译benchmark_byte_diag/reference_streamdiag，生产源未改。默认输入1080相对局部方案20.253/20.2155→20.0495/20.029，约−0.195ms；900约−0.064ms。但扩展900 seed123真实输入首帧即不一致（1,057,029个half不同，maxabs0.1962；24帧末2,771,648个不同），停止采用，不当作可用提速。

test_mh_byte_stream新增可选diag模式，48组独立FFN/QKV/投影/byteout检查全过；回测原有完整MH_BYTE_STREAM同样在此seed123输入上失败，说明此前默认输入/小单元检查未覆盖整链反例，不能归咎于新导出，也不能把局部特征方案牵连为失败。完整字节流继续关闭，下一步若追这条路应逐块定位首个偏差。测试patch/脚本与streamdiag CSV、单元日志、counterexample统计归档；大输入/输出仍在远端hip-backend/profile1080。


## 2026-09-19 18:29：《剑星》安装局部字节特征方案及最新已验证内核

用户要求实玩，确认游戏/Magpie退出。deploy-stellar-feature-byte.ps1备份到pre-upscale/before-feature-byte，更新三个已验证模块（C32普通0f3aff…、packed31e295…、MH dea239…），其余21模块与既有安装逐SHA一致；保留be9e82显示修复DLL。启用MH_FEATURE_BYTE=1、MH_PROJ_DIAG_FB=1，明确MH_BYTE_STREAM=0、VIT_BYTE_STREAM=0，排除未通过的完整流候选。auto、前置异步、FPS关闭保持；游戏图形设置/OptiScaler.ini/DLL前后SHA一致，未启动游戏。脚本-Action Restore回退本轮模块和flags。18:43用户实玩未见明显帧率差异，画面正常。


## 2026-09-19 18:43起：逐块定位到900档解码尾部漏派发，修复完成

隔离探针按原字节格式抓中间张量，额外同步前后最终hash一致，未掩盖反例。编码器5～22、47全部逐位相同；48开始不同，底边最后几行集中。继续抓48输入/FFN/QKV，输入已不同，定位到47→48的Up而不是注意力/byteout。

根因：900档Up输入50×30=1500 token，输出256通道。旧ceil(1500×256/256)=1500组，正确应ceil(1500/16)×(256/16)=1504组；最后4个通道tile没派发，留下48个输出像素×64通道=3072个float未写，缓冲复用改变了残留值。Run改为分别对token/通道分块；fast float/halfweight与WMMA decoder补尾token读取/写出保护。专门identity权重＋哨兵/guard测试：旧网格确切复现3072未写；修复后三内核×7尺寸/裁剪共21组全正确，guard不变。

生产源码修改在hip_reference_network.h、deep_fast.hip、deep_wmma.hip；临时模块decoder-tail-modules，未部署。整链对比已确认字节流反例消失：900/1080不同输入与seed六组、720两组首尾全同；720/1080也与此前记录一致。900w三道旧黄金保持，960新黄金047c36e1…/b4f66e9d…/0e4afd83…，再用局部字节特征改变缓冲布局复核三道均一致，已更新validate-modules-960默认runner/模块/期望值；不再匹配含未初始化值的旧900结果。诊断patch/脚本、test_decoder_tail.cpp与build-test-decoder-tail.ps1可复现。

修复交付：HIP DLL `release/HIP/native-decoder-tail.addon64` SHA174c78270b4a3ab7fd4e98b8933c87682f176688e25257760f3b370084c91d64，DX12构建53297ac9…；模块decoder-tail-modules中的deep_fast=a77c349b…、deep_fast-packed=104e2c84…、deep_wmma=b562c898…，hip/SHA256SUMS同步。须DLL＋内核配套，尚未部署/打包。完整字节流的额外diag导出/host路由仍仅在隔离副本，不能把诊断模块decoder-tail-stream-modules当生产包。下一步在修正后的基线上重测完整字节流收益；本轮首先是修复旧漏算，不是性能或精度档位变化。


## 2026-09-19 19:17：《剑星》部署900档解码尾部修复

用户要求试玩，确认游戏/Magpie退出后，deploy-stellar-decoder-tail.ps1安装174c7827… DLL与deep_fast/deep_fast-packed/deep_wmma三个配套模块，共4文件逐SHA验证；备份pre-upscale/before-decoder-tail，-Action Restore可回退。游戏画质、OptiScaler.ini、flags均未变：auto、FPS关闭、局部字节特征＋快速残差开启、完整MH/ViT字节流仍关闭。19:26用户实玩反馈无异常、肉眼看不出变化，设置2560×1440＋平衡档。日志PID31608持续processed=1/replay=0，实际render1506×848，auto对应900档（1600×900有效画布、1600×960处理网格）。


## 2026-09-19 19:27起：正确解码基线重测完整MH字节流，正式接口通过

已确认游戏/Magpie退出，在decoder-tail修复基线上比较局部特征与完整MH流，80帧ABBA：720 9.067/9.0675→9.0285/9.011（−0.048ms）；900 13.961/13.9715→13.8895/13.903（−0.070）；1080 20.585/20.578→20.423/20.420（−0.160）。全部最终hash相同，之前修复后的八组输入/历史检查已通过。跨轮总耗时有时钟波动，只认同批对照。

将mh-byte-stream-diag.patch并入生产host与HIP源：byteout也选diag残差，新增三个*_fb_bout_diag入口；正式配方重编MH模块SHA5038bab3f34fdd576a99f7f5e5a919b321dd4a938fabdbcf07eb789eefdf690a，包含修正解码器的完整模块集hip-backend/full-byte-modules。新benchmark/reference和正式模块跑过900w旧黄金＋960正确黄金共六道，均匹配。HIP DLL release/HIP/native-full-byte.addon64 SHA d8006df95da60e5f85cb756a649dc1016409a320d3b42c947f3dd4fc9e5a40e8，DX12构建8f402470…；启用片段Development/HIP/full-byte-flags.txt，必须配套新DLL/模块，VIT_BYTE_STREAM仍0。默认不开、尚未部署/打包；局部配置片段明确关闭完整流，便于回退。下一步可检查解码器的尾块保护能否只让尾组付出开销，完整组使用更简单的路径。


## 2026-09-19 19:34起：解码完整tile快路径，保持尾部保护并省约0.14/0.34ms

decoder_project2x_h16w拆为FullTile模板：入口按整组剩余token数选择，完整16-token组不做逐项token判断，尾组保留读取/写出保护；两条路径都保留输出尺寸裁剪和正确派发网格。21组identity/哨兵/guard测试通过。完整MH字节流配置下80帧ABBA：900 13.880/13.8935→13.7495/13.738（−0.143ms），1080 20.395/20.4155→20.0845/20.055（−0.336ms），最终hash一致。

正式配方重编deep_fast/packed（SHA16676e10…/8009b86a…），900w＋修正后960六道黄金全过，720两组历史/重置首尾匹配；生产源码及SHA表更新。完整模块目录hip-backend/decoder-fast-path-modules，配native-full-byte.addon64即可，本刀不改DLL；游戏/发布包未动。decoder-full-tile.patch及test/build-golden脚本、fulltile与fastpath720结果归档。下一步可研究上采样输出直接用FP8字节交给下一块FFN，避免先扩成f32再量化；需保持decoder尾部修复及局部/完整流的接口约束。


## 2026-09-19 19:40起：上采样FP8字节输出接首个FFN，约−0.08/0.05ms

新增decoder_project2x_h16w_byteout，沿用完整/尾tile保护，只将原F(merged)结果存为byte。host的Up支持byte_out，三个MH上坡48/56/62在完整流＋DECODER_BYTE下启用，首个FFN也切bytein；decoder39和最终32通道Up保留float。新入口纳入正确二维tile派发，oc32禁止字节请求，开关默认0。

80帧ABBA：900 13.803/13.707→13.6595/13.6865（−0.082ms，基线有漂移）；1080 20.0705/20.066→20.0155/20.0195（−0.051ms），最终hash一致。新增byte模式后28组解码边界/裁剪/哨兵测试全过，八组跨档输入/seed/历史首尾相同；正式重编两模块、新benchmark/reference后900w＋960六道黄金全过。

生产代码与SHA表更新，deep_fast=d8bbcef9…、packed=d0a9adfa…；完整模块hip-backend/decoder-byte-production-modules。HIP DLL native-decoder-byte.addon64 SHA7bb889bec5a49200a6ec4cd5b2f6693dda8bcbda2c6fc47010729fc1e53ed60a，DX12构建0128e805…；full-byte-flags.txt追加DECODER_BYTE=1并写明配套要求。尚未部署/打包。下一步可查ByteIn的FFN是否仍先解码再pack，尝试直接搬运已编码的输入字节；必须验证负零/边界等编码语义。


## 2026-09-19 20:02起：gfx1200/gfx1201双构建与OptiScaler 0.25完整包

用户要求以后正式编译带gfx1200，并先发含全部当前优化的OptiScaler包。rtc_compile增加目标参数，build-modules默认同源码编两套24模块到gfx1200/gfx1201，保存分目标及汇总SHA；显式单目标保持平铺目录以兼容9070诊断。两套48模块全部COMGR编译成功，汇编目标逐项核对，hip/SHA256SUMS更新双目录48行。0.25启用局部/完整MH字节流与解码字节输出，保留C32/MH复用、有界倒数、完整tile快路径、900漏写修复；ViT字节流保持关闭。

运行时通过官方HIP R0600属性ABI读gcnArchName（结构1472字节、arch偏移1160已在9070实测），优先用LUID匹配D3D设备；仅HIP不提供LUID时退回名称。选择HIP/架构子目录，旧平铺目录仍可用。native-hip-device.txt记录设备、架构、模块路径、匹配方法与runtime。模块用Unicode文件读取＋hipModuleLoadData，权重同样用Unicode路径；测试台原先把UTF8路径逐字节扩成wchar，修为UTF8解码，模块override改读宽字符环境变量。

验证：9070 XT自动选gfx1201，match=luid/runtime=70260201；900w＋修正960六道黄金通过。中文发布路径与英文路径加载完整网络，8帧结果逐位同（B4F66E9D…）。gfx1200无本机硬件，仅编译/目标/打包校验，README明确9060/XT待网友实测。HIP DLL SHA02b4031994aca161098e4240e2a6782bc93cba19a6fd63f3a2b6bdea8ee14e31，DX12亦编译通过。

成品`D:\給網友打包\OptiScaler-DLSS5-AMD-0.25.zip`，387,840,134字节，SHA256 `d5d4c8fdf26d20767d10abf5a7308267ddf4c4d279441ff461f0c4e225a8cfd9`，旁置.sha256；538个有效载荷逐个从zip读回校验，含48个目标内核。清掉Unicode回归产生的shader-cache，保留HIP API MIT许可。Release工具支持ModulesPath/Optimized及Repack，后续打包需传双架构根目录。20:35用户已上传完整包：https://pan.quark.cn/s/636691131c5f ，中英文README链接挂在0.25的OptiScaler标签上。20:40按用户要求部署到《剑星》：确认游戏/Magpie退出，安装同一02b40319… DLL（含_storage_副本）与双架构48模块，逐文件SHA核对；沿用auto/FPS关闭及游戏、OptiScaler设置，启用完整MH流和解码字节输出。备份pre-upscale/before-0.25，deploy-stellar-025.ps1 -Action Restore可回退，待用户试玩；未打tag。

## 2026-09-19 20:43起：Magpie 0.25完整包

按用户要求补齐Magpie包，以校验过的0.23归档为底，使用0.25同一02b40319… DLL及gfx1200/gfx1201各24模块；开启局部/完整MH字节流、解码字节输出，ViT流关闭。保留Magpie的PRE_UPSCALE=0、auto档、sRGB输入、FSR3→FSR4→XeSS配置，未读取/覆盖正在使用的安装目录。附HIP API许可，更新中文路径/架构支持/关闭整行显示说明，移除旧日志及shader缓存；内核目录仅装48个hsaco，不带开发汇编/源码。

成品`D:\給網友打包\Magpie-DLSS5-AMD-0.25.zip`，356,831,870字节，SHA256 `a253fa1331e5d0bc6d8983dccc482d64045fb650671e4d068550f2d2f90507fb`，旁置.sha256；722个有效载荷从ZIP逐项读回哈希通过，模块逐个与生产双架构目录核对。复用已编译并通过网络回归的0.25产物，未另跑GPU测试、未启动Magpie；20:57用户实测1080P Magpie只做DLSS5约37fps，与之前基本持平，未观察到帧率下降；已上传完整包：https://pan.quark.cn/s/09630ed99606 ，中英文README链接挂在0.25的Magpie标签上。工具package-magpie-candidate.ps1默认更新双目标0.25。

## 2026-09-19 20:59起：FFN字节输入直接搬运，验证后合入小幅省时

继续检查ByteIn FFN：合作式LDS staging原本把四个FP8字节逐个解码，再pack4重新编码。隔离候选ffn-direct-byte-word.patch直接memcpy四字节，mapped坐标仍钳到合法位置、窗口外选择正零；浮点输入和残差计算不变，有限输入生产约束不变。未改生产源/安装/发布包。双目标COMGR编译通过：gfx1200 E748DFC398382E89EF356D5D87168B2A1C4C4D822BCC74D58FA508EB1C939187，gfx1201 B15762DAD7741948763A96EE97320077EEBCD4474ADC0D948732D0A37634DF93，目录D:\DLSSNR-Lab\ffn-direct-build。汇编中C64/128/256 mapped bytein_fb各少两个静态v_cvt_pk_fp8_f32，global_load_b32数量相同，说明编译器原先已合并部分读取；不能据此声称加速。

准备test_fp8_staging.cpp与fp8-staging-probe.hip：遍历256种编码（254种有限值含±0要求往返一致，NaN单独报告），C64/128/256映射窗口逐word对照原解码/重编码及CPU坐标结果。探针和exe编译完成；test-ffn-direct-word.ps1先测探针，再以同一0.25完整字节流配置做720/900/1080历史/重置及扩展HDR/暗部/种子共12组基线候选对照，-Timing另跑900/1080的80帧ABBA。每轮检查游戏/Magpie退出。当前Magpie PID22080仍运行，已请用户完全退出；没有运行GPU验证/计时，尚不知正确性与性能收益。待退出后先运行hip-backend/test-ffn-direct-word.ps1，通过再加-Timing；只有正确且有收益才并入生产。

21:05用户退出后完成验证：254种有限FP8编码往返一致（含±0），C64/128/256映射窗口共64,512个word与原路径/CPU结果一致；NaN127会被旧转换规范为255，候选保留原码，但生产输入限定有限值。12组720/900/1080首尾逐位一致、全部24帧均有限，含历史/重置/不同种子/HDR/暗部。80帧ABBA平均中位数：900 13.700→13.66725ms（−0.03275），1080 20.005→19.9835（−0.0215）；120帧BAAB复核：900 13.7665→13.7515（−0.015），1080 20.0505→19.9865（−0.064）。结果全部hash一致；幅度小、跨轮漂移存在，只记两轮微小正收益，不换算游戏FPS。

已将有限字节直搬合入hip/multihead_fast_padded.hip，正式双目标重编packed/unpacked四模块；完整48模块保存在D:\DLSSNR-Lab\ffn-direct-production-modules，hip/SHA256SUMS更新四项。gfx1200 unpacked CA52AC43…/packed C566F5B4…，gfx1201 unpacked 8F7AED4F…/packed C63249CD…。正式目录自动选gfx1201，900w及960六道黄金全过。无需重编DLL；游戏/Magpie安装和已上传0.25包保留原状。脚本build-golden-ffn-direct-word.ps1、recheck-ffn-direct-word.ps1与结果归档Development/results/1080-20260919/directword-*。下一步优先回到较大块的内存访问/同步开销；FFN激活配对、半精度多项式已在09-17测过无收益/更慢，避免重走。

21:16发布说明约定：每个版本changelog简要写明具体优化内容，不用“集成当前优化”代替；兼容/修复和实测效果一并简述，按实际随包内容写。中英文README的0.25已补C32/MH指数复用、有界倒数、FP8字节传递和解码完整分组快路径；21:05后的FFN直接搬运尚未发布，不计入0.25。

## 2026-09-19 21:17起：FFN入口缓冲复用无收益，直接字节片段省约0.10/0.15～0.17ms

以已采用的ffn-direct-production-modules为基线，入口暂存借用稍后才使用的qfeature，expand读qfeature、激活写hidden，去掉防止覆盖输入的一道barrier，无额外LDS。C64/128/256的mapped bytein_fb静态barrier 10→9，VGPR 96/103/81→95/102/80，LDS仍5440/10816/21568，private0。gfx1200/1201均编译成功；12组720/900/1080×历史/重置/HDR/暗部/种子首尾一致、全部帧有限。80帧ABBA：900基线13.6485/13.6555、候选13.6685/13.677（+0.02075ms）；1080基线19.931/20.0075、候选19.944/19.968（−0.01325，基线漂移）。无稳定收益，不采用；隔离patch ffn-reuse-input-stage与ffnstage结果存档。

第二候选从原生产基线另起：ByteIn直接读取8字节WMMA输入片段，省入口LDS暂存及两道barrier；f32仍走原路径。代价是多个wave重复读取全局输入，和旧float输入下关COOP不同，这次直接读FP8无逐元素转换。patch ffn-direct-input-fragment；测试脚本test-ffn-reuse-stage.ps1 -DirectLoad（正确性）、-DirectLoad -Timing（ABBA）、再加-Recheck（BAAB）。双目标构建ffn-load-build，待验证/计时后决定。生产源码、游戏及发布包未改。

第二候选完成：双目标COMGR通过，12组首尾逐位匹配、每组全部24帧有限。80帧ABBA：900 13.6765→13.57675ms（−0.09975）；1080 19.98875→19.8215（−0.16725）。120帧BAAB复核：900 13.75275→13.65575（−0.097）；1080 20.036→19.88325（−0.15275）。各轮最终hash相同；基线是上一刀字节直搬，不把两刀收益相加。静态barrier 10→8，LDS大小不变、private0，C64/128/256 VGPR为97/108/79（基线96/103/81）。

采用直接FP8片段方案，合入hip/multihead_fast_padded.hip；正式双目标重编packed/unpacked四模块、完整48模块目录D:\DLSSNR-Lab\ffn-load-production-modules。gfx1200 unpacked 9C901B31…/packed 17F1B31F…，gfx1201 unpacked DB9B7B08…/packed C33DA4F6…。SHA表更新四项，完整目录自动选架构及900w/960六道黄金通过；9060仍仅编译验证。无需换DLL，游戏/已上传0.25包未动。中英文changelog加“未发布”简述；归档ffnload-*与ffn-input-staging-isa.json。第一候选未采用；后续优先考察矩阵权重片段读取的地址计算/依赖，入口同步这条路已有明确结果，不再盲删barrier。

## 2026-09-19 21:32起：C256预读变慢，权重连续片段省约0.26～0.32/0.40ms

先看当前ffn-load-production ISA：C64 expand的20次global load全部在第一条MMA前发出，C128为34/40，已有编译器预读；C256仍循环读取/拼接32个权重字节后做4次MMA。只对C256 ByteIn tiled做下一批预读（ffn-c256-prefetch.patch），双目标COMGR通过；VGPR79→87，private0。80帧ABBA：900基线13.552/13.545、候选13.613/13.620（+0.068ms）；1080基线19.8005/19.7985、候选19.877/19.890（+0.084）。输出hash一致但更慢，不采用；ISA仍等下一批拼接完才发当前MMA，没形成有效重叠。

进一步发现09-17的MH FFN fragment-layout失败实验只覆盖C64/C128。C256旧tiled布局每个B片段读8个分散字节，可以用现成FragmentPackedMatrix初始化重排，再由frag_b一次读8字节。隔离host暂把MH_FFN_FRAG仅路由C256、优先选_frag名，新增6个C256导出（mapped/identity × float/fb/bytein_fb）；计算顺序不变。双目标编译成功，独立benchmark_c256frag.exe；测试脚本test-c256-fragment.ps1。C256 VGPR79→101，LDS21568/private0不变。首轮80帧ABBA：900 13.611→13.294ms（−0.317）；1080 19.81275→19.40975（−0.403），输出hash一致。完整12组正确性与倒序复核进行中；候选patch c256-ffn-fragment，生产源码、游戏/发布包未改。若通过，正式版需独立C256开关，保留旧小通道实验语义，并配套新DLL/模块。

C256片段方案验证完成：12组720/900/1080×历史/重置/种子/HDR/暗部，首尾逐位相同、每组全部24帧有限；120帧BAAB复核900 13.61925→13.3575（−0.26175ms），1080 19.877→19.4755（−0.4015），与首轮方向一致。收益相对已含FFN直接输入片段的ffn-load-production基线，不相加推算游戏FPS。

正式接入使用独立Options.mh_ffn_frag256、环境DLSS5_HIP_MH_FFN_FRAG256=1及CLI --mh-ffn-frag256，默认0，只在C256＋融合FFN/QKV＋字节feature接口启用；小通道实验开关不复用。六个新C256_frag导出、FFN/QKV权重初始化时FragmentPackedMatrix重排、_frag命名优先于_tiled。新HIP DLL release/HIP/native-c256-fragment.addon64，SHA256 f5d3f7348e42f362a0db4233814e1b2d8c4842de0d1196620a40bbc299adde75；远端D:\DLSSNR-Lab\c256-frag-src同名文件。生产benchmark_c256frag_production.exe/reference_c256frag.exe重编，独立正式开关与完整双架构目录自动选择、900w/960六道黄金全部通过。

正式四模块重编：gfx1200 unpacked AE28609A…/packed 5A91FB9A…，gfx1201 unpacked 1D0CAC28…/packed FD24B2FF…；完整48模块D:\DLSSNR-Lab\c256-frag-production-modules，hip/SHA256SUMS更新。full-byte-flags.txt已标注新DLL/模块并加FRAG256=1，后续部署/打包需同步这组配置；已上传0.25和游戏安装未动。README中英文“未发布”简述输入直读＋C256权重预排。源码/候选/脚本/CSV均归档；未做9060实机测试。预读尝试不采用；下一步仍可查C32输入映射与输出的32位地址，权重预读不再盲做。

21:46按用户要求部署到《剑星》：现场确认游戏/Magpie退出，deploy-stellar-c256-fragment.ps1安装f5d3f734… DLL（同步_storage_副本）及c256-frag-production双架构48内核，按仓库SHA256SUMS逐个验证；启用MH_FFN_FRAG256=1，保留完整MH/decoder字节流、auto和FPS关闭，OptiScaler.ini未变。覆盖前完整备份DLL/内核/flags至D:\DLSSNR-Lab\pre-upscale\before-c256-fragment，脚本-Action Restore可回退。此安装包含0.25之后的输入字节直读与C256权重预排，待用户实玩反馈。

21:57用户回测《剑星》反馈“玩了下，没啥问题”，未提供新帧率；确认本机游戏/Magpie均退出，继续研究C32输入/输出地址计算。

## 2026-09-19 21:57起：C32半精度buffer地址正确，收益不足以采用

在c256-frag-production基线上，仅把RawMapped半精度输入（入口/残差）及HalfOutput写出换为raw_buffer_load/store_b16，保留坐标、边界、舍入和数组布局；uint字节偏移配固定基址。初版照搬09-17仅编译过的buf_probe常量0x27000，720首组输出错误（未装到游戏）；不能把“出buffer指令”当执行正确。单独枚举65536半精度位型定位：0/0x20000/0x27000配置读写均错，AMD CK官方ck.hpp对gfx1200/gfx1201使用0x31004000。改用该常量后65536种位型读写无差异、全部有限值f16→f32转换相同。来源：https://github.com/ROCm/composable_kernel/blob/develop/include/ck/ck.hpp ；builtin签名核对LLVM release/21.x BuiltinsAMDGPU.def。test_buffer_half.cpp、c32-buffer-probe.hip及双架构编译/探针脚本归档。

修正候选双目标COMGR通过，gfx1200 7DA02C86…、gfx1201 59534773…，完整候选由test-c32-buffer.ps1组装（基础保留FRAG256=1）。C32 chain静态64位加法族124→42、64位移位50→17，buffer读写48条，VGPR177/LDS15360/private0不变；chain_finish加法160→80、移位69→37，VGPR182不变。统计c32-buffer-isa.json。正在跑12组正确性，尚未计性能；候选patch c32-buffer-half-io，生产源码/已部署版本未改。

修正后12组720/900/1080×历史/重置/HDR/暗部/种子全部通过（每组24帧有限、首尾逐位同）。80帧ABBA计时严重漂移：900基线13.456/13.7005对13.3465/13.2865，1080基线19.8305/19.394对19.811/19.4085，不据此报加速。拉长240帧BAAB：900基线13.38475→候选13.35125（−0.0335ms），1080 19.51825→19.45425（−0.064）。进一步双目标编译只读/只写分离，240帧BAAB：只读900 13.3765→13.33925（−0.03725）、1080 19.51125→19.50375（−0.0075）；只写900 13.5145→13.51475（+0.00025）、1080 19.602→19.5965（−0.0055），全部最终hash一致。

结论：写侧无收益；读侧900有约0.03ms的小变化，但1080接近零，整体收益受漂移影响、没有清楚的跨档优势，本轮不合入生产。保留正确的RDNA4 buffer探针/候选供后续使用；不再靠静态指令减少推断提速。test-c32-buffer.ps1支持-Variant both/read/write，-Recheck用240帧；split-c32-buffer.ps1从同一候选源生成宏分离版本并默认编两个目标。数据c32buf-*，游戏保持用户刚验证的C256版本，README未发布项不添加未采用实验。下一步先在最新C256基线上重测各家族的整帧边际耗时，再选较大热点。

## 2026-09-19 22:36起：最新C256基线分族计时

用户要求继续慢慢量。确认游戏/Magpie退出，以已实玩通过的c256-frag-production-modules、benchmark_c256frag_production.exe和完整字节流＋FRAG256=1为基线，关闭Graph。profile-1080.ps1新增可配置预热及P10/P90，默认预热8保持旧脚本兼容；新profile-current-families.ps1每组160帧、舍32帧，900/1080两档，14类核各重复一遍，测量顺序baseline→family→baseline，取相邻两基线均值作差并记录漂移。每轮核对该档末帧黄金hash及首尾有限性。耗时是完整NativeGameFrame回放，分族差值是重复执行的边际成本，含缓存/提交影响，不能相加当精确分解或视为可全部省去的时间。过程中持续保存current-map-runs/margins.csv；结果待汇总，随后只对大头细分。

完成14类×两档及11项细分×两档：共104组正常/重复测量，每组160帧；另12组入口噪声消融对照，合计18,560帧用于计时（只检查首尾有限性和末帧hash，不声称每帧逐值验证）。正常回放中位数约900 13.462ms、1080 19.633ms，均含NativeGameFrame转换/桥接/同步；粗分最大相邻基线漂移0.0715/0.0655ms，细分0.044/0.0375ms。正常测量所有末帧黄金hash匹配。

粗分边际成本900/1080(ms)：C32 3.198/4.704，ViT 1.549/2.972，post/head 1.165/1.600，C256 FFN/QKV 1.026/1.493，C128 1.047/1.403，C64 0.953/1.338，其余单族<1ms。C32细分：入口融合1.102/1.695，普通链0.993/1.441，mapped入口0.558/0.772，链尾0.552/0.829。注意调用数/尺度：入口和post是全W×H，普通链4次、mapped2次、链尾2次均在W/2×H/2，不能拿入口单次与普通单次直接判“入口低效”。ViT注意力0.489/1.177最大，QKV0.345/0.616、投影0.307/0.473、FFN展开0.279/0.458、收缩0.184/0.297；pack/gather为小项。

追查固定seed=0的噪声是否值得缓存：独立双目标消融只把fused_prefix_values的g0/g1/g2置零，图像故意改变，绝不部署。入口VGPR169/LDS15360/WMMA74不变，log2/sqrt2/sin1/cos2消失；分别在原版和零噪声版测入口重复的边际成本，900 1.14575→1.12825（差0.0175ms），1080 1.6825→1.65275（差0.02975），与小漂移同量级，不把它当确定可兑现的提速；噪声缓存暂不追。patch c32-prefix-noise-ablation、measure-prefix-noise.ps1及prefix-noise数据归档，候选目录prefix-noise-ablate-build/hip-backend/prefix-noise-ablate-modules仅诊断。

完整报告Development/results/1080-20260919/current-profile.md，由summarize-current-profile.py从current-map/current-detail/prefix-noise CSV生成。下一步优先C32高分辨率首尾的算术主干，以及ViT attention内部评分/归一化/AV细分；不重做已否定的buffer地址、小项pack/gather或噪声缓存。生产算法/游戏/发布包本轮未改。

## 2026-09-19 23:11起：安装Lies of P，准备复现issue #1效果质量黑屏

用户切换排查《匹诺曹的谎言》，目录C:\Program Files (x86)\Steam\steamapps\common\Lies of P；实际执行文件LiesofP\Binaries\Win64\LOP-Win64-Shipping.exe。issue https://github.com/lmxxf/dlss5-on-amd-9070xt-porting/issues/1 报告的是“效果质量”不设最高就黑屏，F6关闭恢复；附件https://github.com/user-attachments/files/32415774/default.zip（24093字节）已读，网友9070XT/gfx1201、runtime70260201，网络初始化/processed=1正常，并发生过render geometry changed；日志没有足够的像素/格式对照来确定根因，不判为架构或初始化问题。

确认LOP/游戏/Magpie未运行。用公开OptiScaler 0.25完整包建立复现基线，安装到实际Shipping.exe旁；538个载荷逐文件验证，addon为02b40319…（不是《剑星》新候选f5d3…）。游戏已有6,609,624字节amd_fidelityfx_dx12.dll，保留原文件及SHA；OptiScaler自己的依赖放OptiScaler子目录，INI设置绝对OptiDllPath和FfxDx12Path指向子目录，dxgi/ReShade/addon/DLSS5-AMD/D3D12_Optiscaler留EXE旁。保留预上采样+auto及FSR31预设，FPS关闭，未改游戏画质。备份/安装清单D:\DLSSNR-Lab\liesofp-before-optiscaler-025；Development/tools/install-liesofp-optiscaler.ps1支持Install/Restore。未启动游戏，加载与问题复现待用户试玩；未回帖issue。

## 2026-09-19 23:20起：Lies of P黑屏复现，发现打包遗漏R11写回shader

用户稳定复现，进程30760、render1506×848→2560×1440，网络正常processed=1，F6开关可用。定位到确定的资产版本不匹配：游戏随0.25安装的native_codec_decode.hlsl SHA977c2d52…仍是旧版，没有NATIVE_CODEC_R11_OUT分支，而DLL的NativeGameCodec已为R11G11B10选择raw buffer输出。旧shader在该宏下仍编成RWTexture2D，与DLL绑定的raw UAV不匹配；源码09-18已补R11分支，但包沿用了旧资产目录。当前源SHA98a790d928e61c3eab905f43374dc9a1e560aff22946cd17b166cd0ca3e6c5f8，相比旧文件仅多R11写回分支。23:32用户按最高→原低档切换后确认“出來畫面了”，替换这一shader即可恢复，支持打包遗漏为本次黑屏根因。

compile_fit_shaders.cpp新增R11输出组合并反射断言OutputBits为raw UAV，44种生产D3DCompiler组合通过；旧shader负对照被正确拒绝。只备份/替换了游戏的这一个HLSL文字文件，DLL仍02b40319…，未动运行中DLL/HSACO；备份D:\DLSSNR-Lab\liesofp-before-r11-shader，工具fix-liesofp-codec.ps1。已请用户最高效果品质→原低档触发格式重建，shader-cache按源码变化自动失效；23:32已获用户画面恢复确认。

两条打包脚本补上CodecDecodePath（默认D:\DLSSNR-Lab\native_codec_decode.hlsl），打包时同步当前解码shader并调用compile_fit_shaders.exe做44组合/绑定校验，防止再次直接继承旧资产；Magpie基底校验白名单允许该明确更新。当前shader与验证器已上传D:\DLSSNR-Lab，脚本PowerShell解析通过；已上传ZIP尚未重打，未回帖issue。


## 2026-09-19 23:39起：Magpie与OptiScaler 0.26完整包

按用户指定版本0.26，采用已编译/回归/《剑星》实玩通过的f5d3f734… DLL与c256-frag-production双架构48内核，开启MH_FFN_FRAG256=1及完整字节流/解码字节输出，保留FFN直接片段读取；未纳入C32 buffer或噪声消融。同步98a790d9…解码shader，包含《匹诺曹》已实测的低效果品质黑屏修复。DLL/HSACO沿用已验证产物，本轮无需重编；两份实际staging均跑过44种shader编译/反射绑定校验，随后从ZIP逐项读回所有有效载荷，再核对两套48内核、DLL、shader和配置。OptiScaler保持PRE_UPSCALE=1/ASYNC=1/FSR31；Magpie保持PRE_UPSCALE=0及原流水线。打包工具补齐FRAG256配方，package-026.ps1先核对源文件SHA再顺序打两包。中英文README及包内说明简述0.26优化与修复；23:49用户上传两款完整包，OptiScaler：https://pan.quark.cn/s/c880a70f0824 ，Magpie：https://pan.quark.cn/s/7ce2ca11db43 ；中英文README链接挂在0.26对应标签上。不打tag、不改游戏安装。

输出D:\給網友打包，旁置.zip.sha256：

| 包 | 字节数 | 有效载荷数 | SHA256 |
|---|---:|---:|---|
| OptiScaler-DLSS5-AMD-0.26.zip | 387976939 | 538 | e008013b3c6dfa9ae1268a66fe5d4cf4170462e9bd9270cf550bf4b2a730f9a6 |
| Magpie-DLSS5-AMD-0.26.zip | 356968661 | 722 | 9571fc0cda7ad78cb2e12312fdd0d0cc28e270109edad15301ec94fc007276f0 |

机器可读清单Development/results/release-0.26.json。0.26 Magpie整包尚待用户实玩，9060/XT仍无本机硬件验证；原0.25归档保留。

## 2026-09-20 00:04起：RE9重新调查，准备provider/NGX双观察点

重读上次日志：OptiScaler+REFramework基线可进菜单，DLSS5因FFX后同列表draw/dispatch被拒；根目录/_storage_的旧addon均.off。官方OptiScaler兼容表仍列RE9需REFramework。制作只转发原始FFX、记录后续普通draw/dispatch调用栈的隔离观察版，首版54aa65b6…部署为re9-ffx-observer.addon64并启动RE9（PID3212），原DLSS5保持.off。截图确认可见菜单，OptiScaler为DLSS→FSR4.1.1、约1130×636→1920×1080；新loader hook_status=0但没有FFX调用记录。使用Insert隐藏REFramework时同时打开了OptiScaler菜单，Enter未进入游戏；未更改图形参数。

核对官方0.9.4提交7534ad0源码，发现FfxApiProxy可直调upscaler provider，输入hook与后端同库时还会把内部函数指针换成Detours trampoline。当前加载_storage_的loader2.1.0.604/upscaler4.1.1.2740，bare dx12 2.3.0.2740文件存在但不在当前模块清单；root/_storage_对应文件hash一致，没有证据说是缓存旧版本。FSR31Feature在FFX后可能继续做RCAS/输出缩放/ImGui，所以旧following_work不能直接归为游戏本身，需观察实际路径。

新版观察DLL加入provider入口＋OptiScaler NVSDK_NGX_D3D12_EvaluateFeature出口，记录list_type与前四个后续命令的模块/偏移栈；签名从NVNGX_DLSS_Dx12.cpp核对，限定前16次调用。编译SHA2bc1ee441c117c349bd30364789f8e23dc7f61d1354e010622fb227060f944e3，已放D:\DLSSNR-Lab\re9-opti\native-re9-observer.addon64；尚未覆盖游戏运行中的首版。已请求用户退出RE9，下一步install-observer.ps1 -Action Update同步root/_storage_再启动。Development/RE9保存源码、准备/安装脚本、研究说明；本次未实现新的DLSS5接入，不移除following_work、不关闭/重置游戏列表，不把无观察结果当安全证据。原0.26发布代码未动。


## 2026-09-20 00:45起：RE9真实NGX出口确认同列表后续来自游戏

用户退出后，按进程/校验保护把观察版2bc1ee44…同步root/_storage_，再启动RE9（PID32500）。NGX Evaluate出口hook成功；16次成功返回后列表均为DIRECT，随后同一列表有51～53次普通draw/dispatch（51×5、52×1、53×10）。前几条栈显示re9.exe+5865c16、draw +59098aa、draw_indexed +59099a3，经ReShade转发；这次明确是游戏自己的后续命令，不只是OptiScaler内部RCAS/菜单。没有追踪所有资源读写，不能将“后续命令”细化成已证明的具体纹理消费者，但现有尾部契约已经明确不满足。provider导出仍无调用日志，不把它当没执行；NGX外层证据独立成立。

完整记录Development/RE9/results/ngx-following-work-20260920.txt及summary.json。已向用户询问后续路线：推荐先做后置兼容模式（FSR后处理成品画面，UI亦受处理，保持HIP）；若保留前置则继续找引擎提交边界或新的原生D3D12接法。后置需先做Present时序及R10G10B10A2转换验证；现有HLSL RecordUnsubmitted明确保留了single-list device-hang保护，不能直接启用。游戏仍运行观察版，原DLSS5 .off；未宣称DLSS5已接通，等待用户路线选择。

## 2026-09-20 01:36起：RE9后置HIP兼容候选

用户授权直接退出游戏并继续兼容，由助手处理退出/启动，无需反复请用户退出。先做独立ReShade present插件：游戏提交后、ReShade效果前，用自有列表复制R10G10B10A2后缓冲→FP16私有纹理→HIP→raw buffer打包R10并写回，保持PRESENT状态，不关闭/重置游戏列表。范围暂限SDR、R10、≤1920×1080，900P网络，UI也会处理。mode文件0只转换/1推理/2绕过，F6绕过；后台初始化，首次推理前后私有FP16诊断读回；失败停止处理并保留可能在途资源。

独立test_present_bridge在9070上完成17×9、128×32、1920×1080，两种位型图案各三次，共18次GPU往返逐位一致。第一版66b6aa74…进入游戏时在ReShade get_back_buffer调用内崩溃，尚未做转换/推理；汇编定位返回点+75e7。改用IDXGISwapChain3原生GetBuffer避免跨编译器的resource返回接口；需继续游戏验证。备份D:\DLSSNR-Lab\re9-opti\before-present，旧observer已.off；同步c256-frag-production双架构48模块与当前解码shader，原发布版本不变。新源码/编译/安装脚本均在Development/RE9；这阶段不能宣称已兼容。

修正候选1d8ac806f5150e11e3ae313091e372dfb8c4fcb945069bbdf9cd57ec4b02dccb已安装root/_storage_并启动PID25852：只转换模式成功，启动中1920×1080→2560×1440（超范围绕过）→1920×1080正常。切mode1后HIP初始化成功，gfx1201/LUID匹配，900P网络；菜单连续1200帧（约30fps菜单上限）无提交错误，截图正常。首帧私有RGBA16F前后均全有限，RGB均值0.23444→0.23708，99.65%分量改变，MAE0.01777；此读回仅证明实际处理且无非有限值/整屏黑，不代替视觉质量验收。统计results/present-first-frame.json；原始诊断在游戏logs和D:\DLSSNR-Lab\re9-opti。用户进游戏测试尚待，当前不是前置路径、无运动矢量历史、HDR/大于1080P输出会绕过。

01:59补查F6：日志确认bypass后停止增加处理计数，再次F6 enabled后恢复，累计2400帧。游戏留在菜单、后置HIP开启供用户实玩，保持现有1080P SDR设置。证据present-menu-20260920.txt；阶段提交b54b05f（实现及往返测试），本次补实测记录。

## 2026-09-20 07:02起：RE9接回信息层，准备用户第二轮试玩

用户已实玩并退出，要求先接回信息层再测，确认后独立打0.26 OptiScaler-REFramework包。后置插件增加独立信息绘制：有效NET/OUT尺寸、ON/OFF/初始化/失败/超范围提示，以及Present回调FPS（不冒充插帧后帧率）；右上角避开REFramework菜单。F6切换处理，F7隐藏/恢复所有信息；SHOW_FPS/NOTICE每秒热读分别控制两行。默认取消首帧完整读回写盘，诊断需RE9_SNAPSHOT=1。NativeTextOverlay/字体shader增加R10G10B10A2 raw打包分支，信息在HIP之后绘制，不进入模型输入。900P显示1600×900有效区域，而非1600×960填充画布。

编译通过，初版3c81f00c…已在游戏显示文字并处理2100帧；发现左侧REFramework面板遮住信息，最终改右上角。当前候选4d31484f276cbc144c717da6e13996ac5674204ac3ac8d6bf165c34b8448441c与字体shader8d20c7f5…在退出后同步root/_storage_；before-info备份原DLL/配置/shader。待完成开关实测，尚未打包。

07:07实测最终候选PID5248，菜单累计3900帧以上无处理错误。截图确认右上有效1600×900→1920×1080、ON/FPS文字正常；F7隐藏/恢复成功，F6 OFF提示及恢复处理成功；SHOW_FPS=0、NOTICE=0热改后两行完全消失。结束恢复SHOW_FPS=1/NOTICE=2、F6 ON、F7可见，游戏留菜单供用户第二轮实玩。日志results/present-info-20260920.txt。通用0.26包及其他游戏安装未改；专用包等用户本轮确认再打。

## 2026-09-20 07:16起：RE9后置支持2K输出、仍用900P网络

用户2K输出/低超分质量不出DLSS5：后置接收的是超分后整幅画面，降低超分输入质量不会降低后缓冲尺寸；原专用版明确只接受≤1080P。沿用codec已有FIT缩放与高分辨率原图融合，只在RE9构建定义DLSS5_RE9_POST_1440，将外部帧上限放宽到2560×1440，神经网络仍1600×900，通用构建上限保持1920×1080。bridge使用完整2K FP16原图，编码缩至900P，解码将网络结果映射并融合回2K原图，不把整幅UI简单缩小再放大。信息层上限提示同步更新。

CPU检查：通用input_geometry_test的2,073,600尺寸回归通过，专用900P网络/2K输出、非严格窗口尺寸与越界检查通过；GPU R10往返测试扩到2560×1440待运行。候选已编译，准备游戏退出后GPU验证/部署。用户尚未确认专用包，不打包。

07:19验证完成：24组R10→FP16→R10 GPU往返全部逐位一致。候选f2c0aa4e4d0e33b0fc91497065472d275fb9c302f791ccc48393154d8f597b79已在退出后同步安装；启动保留用户Resolution=2560x1440、UpscalingQuality_DLSS=UltraPerformance。PID24140真实2K后缓冲创建/推理成功，连续2400帧，菜单截图显示ON NET1600X900 OUT2560X1440，画面正常。首帧是启动暗画面，2K读回前后全有限、22.75%分量变化，不能用它评价游戏场景质量；统计present-1440-first-frame.json，日志present-1440-20260920.txt。诊断RE9_SNAPSHOT恢复0，游戏保持2K/后置开启供用户试。

## 2026-09-20 07:33起：用户改定原生≤1080P契约，撤回2K后置支持

用户否定先超分再缩小的后置流程，要求关闭游戏超分、最大1080P，超范围报错且不工作。撤回RE9_POST_1440构建开关，统一外部帧上限1920×1080，超过显示ERROR MAX 1920X1080并在创建/执行HIP前返回；内部900P计算保持。新增原生设置要求：读取RE9 config.ini UpscalingAlgorithm=None，否则显示TURN OFF GAME UPSCALING、不执行HIP；另外只观察OptiScaler NGX Evaluate入口（原函数照常执行），有最近调用也禁用后置，覆盖尚未保存的DLSS开关。不会强行改变游戏的渲染调用；用户开启不支持设置时停用的是我们的处理。native_only.h、configure-native.ps1归档。

候选c9873d7e…已编译/部署，CPU通用2073600尺寸与专用1080上限测试通过。准备验证2K拒绝、1080关闭超分可用，以及开启超分的错误提示。上条2K实测作为被撤回实验保留，后续专用包以本条契约为准。

07:43实测：2K显示ERROR MAX 1920X1080并不处理；1080配置配无边框仍会产出桌面2K，因此配置脚本最终用WindowMode=Normal（两处）及NormalWindowResolution=(1920.000000,1080.000000)，不是无效的Windowed值。PID15276原生1920×1080/None下HIP连续2400帧以上，截图ON NET1600X900 OUT1920X1080。负对照仅临时把保存配置改DLSS（不声称实时切了游戏后端），画面显示ERROR TURN OFF GAME UPSCALING、计数暂停；恢复None后继续。NGX guard安装状态0。配置最后保留Native1080、DLSS5开启、F7信息可见；用户可直接试玩。证据present-native-20260920.txt，3440e9c实现提交；尚未打包。

## 2026-09-20 07:49起：允许上游超分，仅保留1080P输出限制

用户重新决定超分仍有必要，保留游戏低分辨率渲染→FSR超分→DLSS5后置；撤回07:33“必须关闭超分”要求。删除原生配置检查、NGX观察和MinHook依赖，以及强制None配置脚本；保持后缓冲≤1920×1080、SDR R10、固定900P网络和F6/F7。超范围仍ERROR MAX 1920X1080并不处理。

候选f7c10735b1447c7ed5ed29c92d3bc4e6a3a97a3583220a1e7ae515f4c0b6420a编译通过，游戏退出后同步root/_storage_；恢复UpscalingAlgorithm=DLSS，保留Balanced、1920×1080普通视窗。临时恢复脚本仅修改超分开关，备份before-restore-upscale.ini。待检查启动处理；用户实玩后才打专用0.26包。

07:51验证PID3604：OptiScaler重新创建fsr31接口后端，HIP随后正常初始化并处理600帧以上；启动短暂2K时仍被尺寸保护拒绝，回到1920×1080后正常。日志present-upscale-restored-20260920.txt。游戏保留已开启超分/平衡档，后置与信息显示开启，待用户继续试玩；无原生超分禁用检查。

## 2026-09-20 09:11起：OptiScaler-REFramework 0.26.1完整包

用户确认最新流水线实玩通过，指定单独发布0.26.1 OptiScaler-REFramework版本。采用07:49已部署/验证的f7c10735…后置DLL（允许上游超分，输出≤1080P SDR，900P网络，F6/F7），REFramework nightly-01424/d1461375… DLL SHA14f4d6fd…，OptiScaler0.9.4/ReShade6.8及完整模型资产、c256-frag双架构48内核。以旧完整包SHA256SUMS白名单从游戏目录提取，不递归拷游戏；未改动资产逐项比对旧SHA，替换当前解码/字体shader和配置。OptiScaler路径auto，ReShade改便携配置、关闭自己的额外FPS，保留本插件两行信息；保留日志目录占位文件，默认关闭快照诊断。加入REFramework对应提交MIT许可证及release.json。

包名OptiScaler-REFramework-DLSS5-AMD-0.26.1.zip，输出D:\給網友打包。说明涵盖旧插件清理（含_storage_）、1080P输出/无边框限制、900P计算与游戏渲染尺寸区别、HIP7系统运行时依赖及实际后置顺序。通用0.26包不改，不宣称所有RE引擎游戏兼容。中英文README新增专用0.26.1条目。package-0261.ps1归档；44种实际staging shader编译/绑定检查通过，正在完成ZIP逐文件读回校验。首次压缩路径分隔符问题改为显式创建标准斜线路径ZIP，最终以读回校验成功的产物为准。

09:21最终ZIP完成：379,780,870字节（约362.2MiB），543个载荷文件全部逐项读回SHA匹配，双架构48模块完整；SHA256 58d58f3f65048eaab0d3a32a48e8b6af983e84ad3d1ec4003417a4860e1dc4e9，旁置.zip.sha256。清单Development/results/release-0.26.1-reframework.json。用户测试通过的DLL沿用未重编，打包阶段复核实际shader与资产。09:28用户上传网盘：https://pan.quark.cn/s/624c87a6aa11；中英文README链接挂在版本表OptiScaler-REFramework标签，明确标注“专门针对RE9这类特殊接入场景的非常规版本，普通游戏使用通用版，目前仅RE9实测”。已上传ZIP不重打。

## 2026-09-20 12:08起：调研Sol-Attn与Spectrum优化线索

用户转述闭源作者曾与两仓作者交流；公开源码只能核实方法，不能证实交流或33→60fps具体实现。读取kijai/ComfyUI-SolAttn_triton（26d816ebd4f1e43a2c6e4d4759be3137f10a7a73）及xmarre/ComfyUI-Spectrum-MiniMax-H3（5161f0457bc8c52535212d6783eee73f439e1537）README和核心源码。Sol的_preprocess/_tri_fwd：K均值、V块汇总、按mean+tau*std阈值路由；重要块/相邻块精算，其余以块代表近似，带块长度归一化，并非只丢弃低分块；另有融合预处理/INT8量化。Comfy节点64-token分块、head_dim128，README已标deprecated（上游ComfyUI/comfy-kitchen已有实现），仓库报告测试4090/5090，不当成AMD即插即用库。

本项目MH已是64-token窗口/head32，还有位置偏置、特殊指数近似和FP16/FP8舍入；Sol默认一个块就覆盖整个窗口，不能直接替换或照搬标准softmax。可探索更细块的近似路由，但必须先量归一化权重分布/可压缩性，计入摘要与选择开销。ViT较长（900/1080为400/640 token），可先作为候选；09-19 ViT attention重复执行边际0.489/1.177ms，只用于选热点，不能当精确可节约上限，更不能据此声称全网翻倍。

Spectrum forecast.py/runtime.py：沿扩散采样时间，用Chebyshev基+ridge从实际中间特征拟合/预测，跳过部分H3 transformer调用；近似、会改输出。我们的每帧一次网络并无多步去噪循环可直接跳；若借鉴成跨游戏帧特征复用/预测，就是新算法，须处理运动对齐、遮挡、转镜/切场景失效，先比较便宜复用再考虑多项式预测。候选研究优先“注意力质量集中度/分块收益”和“动态游戏输入下中间特征可复用性”，不把静态重复回放当跨帧质量验证。尚未改核/运行新GPU实验，所有已部署和发布包保持原状。

来源：https://github.com/kijai/ComfyUI-SolAttn_triton ，https://github.com/xmarre/ComfyUI-Spectrum-MiniMax-H3 。

## 2026-09-20 12:16起：首次ViT注意力近似实验，K/V相邻均值代表

用户要求先试某个算子。隔离修改vit_attention_fused_body：Q不变，相邻两个K/V分别取均值并按原FP8打包，用一半代表计算；保留特殊指数bit映射/输出舍入，代表数量的共同2倍因子在归一化中抵消，但均值后非线性与V相关性导致近似误差。400-token半长尾部无效权重置0、读取钳制。不是完整Sol-Attn，不改权重。prepare.py只生成/tmp候选与patch，hip/deep_fast.hip生产源不改。双架构COMGR通过：gfx1200 3ae02111…、gfx1201 6a7611fe…；机器空闲检查后仅gfx1201实跑。

最新c256-frag-production基线，固定1296×720捕获，900/1080各160帧ABBA、舍32：完整NativeGameFrame回放900 13.3995→13.2070ms（−0.1925，1.44%），1080 19.54175→18.85725（−0.6845，3.50%）；原版hash匹配黄金，候选重复hash一致。补重复attention边际：900原0.419/候选0.43475ms，1080原1.12525/候选0.73375ms，不能把全帧差全部解释成算子本体节省；尤其900还需匹配真实QKV的独立事件计时归因。

两档×真实画面/合成HDR/近黑×AB各12帧，144帧全有限且每组冻输入输出一致；不是动态时序测试。真实画面clip到SDR再转sRGB的MAE约2.63/255，两档PSNR35.79/35.22dB；raw相对RMSE11.97%/13.30%，最大分量差0.1546/0.2437，人物/高光差异集中。1080合成HDR的显示MAE5.27/255；暗部绝对误差小但相对误差大，均归档，不把“没黑屏”当画质等价。首轮结论：近似能跑且完整回放有收益，但无条件合并偏粗，保留实验不部署；后续考虑相似性/空间分组保留重要项并计入预处理成本、测动态输入。

报告Development/results/vit-kv-pair-20260920/report.md、summary.json和对比图；工具Development/HIP/experiments/vit-kv-pair，原始输出Windows hip-backend/profile1080/vitpair-*。游戏DLL、发布包和生产内核未改。

12:33用户指定后续注意力近似实验统一放AttExp分支。从首轮实验提交084f8df创建并切换AttExp；该分支保留已有实验与生产基线，后续实验提交在此继续。

## 2026-09-20 12:37起：comfy-kitchen Windows HIP分支与TeaCache调研

用户提供0xDELUXA/comfy-kitchen_win-rocm/branches与welltop-cn/ComfyUI-TeaCache。GitHub API限额，改用git ls-remote/fetch核对分支并读真实源码：amd/hip-sol-attn=d9630b8c6524c791671138520c84afa358ef08b3；amd/hip-sol-token-aug=b99d2207b0865a3cc660ffb8265eadde3f891ead；amd/hip-rdna4-wmma=31e4bfef6d67e0cf141d7d2751baea32ff4f6d7d；amd/hip-gemm-tile-selection=9192c77751175c67ee76c7ef7ebb1327642320d9，另有hip-int8-attention等。确有原生HIP后端，不只是Triton：mma.h针对gfx11/gfx12的wave32片段排列、FP8/INT8 WMMA；gemm_wmma.h字节LDS、8-byte padding及提前预取下一K tile的软件流水；README显式列gfx1200/gfx1201，不调用hipBLAS/hipBLASLt。库整体仍是Comfy/PyTorch接口，不能把它直接当我们DLL的替换件。

hip-sol-attn分支sage_attention下preprocess/producer/vtranspose/route/exact齐全：一次workspace分配，先量化/块摘要/阈值，再选择精算块和摘要尾部，最后恢复在线softmax精算；producer融合RMSNorm/RoPE/量化/布局，避免完整bf16 QKV落地。hip-sol-token-aug新增sol_attn_token.hip：对未选块内token再打分，用直方图门槛和预算挑重要token补入精算列表，排序/合并保持确定性。比刚才无条件K/V两两平均更有针对性，优先参考“粗筛块→补救重要token”及预处理/精算拆分。当前Sol接口B,T,H,128/bf16，块64；本项目head32、短窗与自定义指数/舍入不匹配，需自己缩小分块与保留公式，不直接移植标准softmax；节点摘要/路由开销对400/640token可能显著。硬件后端支持声明不等于本机已做性能验证。

TeaCache固定提交91dff8e31684ca70a5fda309611484402d8fa192，读nodes.py和README。以FLUX实现为例：取首块调制输入，计算相对L1变化，经模型专用多项式校准并累计；未过门槛时跳过blocks，将缓存的previous_residual加到当前输入；需要重算则更新residual=block_output-block_input。它不是冻结上一张RGB，也不是Spectrum那种直接预测未来特征曲线。源码有各模型独立系数/采样范围；README宣称的“lossless”速度不等于数值逐位等价。DLSS5没有相同扩散时间步，若借鉴需改成相邻游戏帧/块级残差缓存，测输入变化是否能预测输出误差、运动/遮挡/切镜重算条件与缓存成本，不能照搬系数或2x数字。

下一轮研究优先comfy-kitchen HIP的选择性精算来改善本轮KV均值误差；TeaCache式便宜变化检测/残差复用另列跨帧方向，先用动态捕获验证相关性再跳算。本轮仅源码调研并记在AttExp，未改核/跑GPU或部署。

来源：https://github.com/0xDELUXA/comfy-kitchen_win-rocm/tree/amd/hip-sol-attn ，https://github.com/0xDELUXA/comfy-kitchen_win-rocm/tree/amd/hip-sol-token-aug ，https://github.com/welltop-cn/ComfyUI-TeaCache/blob/91dff8e31684ca70a5fda309611484402d8fa192/nodes.py 。

## 2026-09-20 13:01起：AttExp后续计划及选择性精算启动

用户确认先记计划再动手。阶段A：用HIP预处理一次统计K/V差异并生成可合并标记，避免每个Q块重复算相似度；空间跨行/不完整组和本地Q组保留精算，低差异组才用成对摘要，混合组正确计入2倍权重。先跑全精算控制（含预处理但不近似）确认黄金输出，再扫门槛、记录合并比例、完整回放耗时/误差；只在质量收益合理时继续考虑更接近Sol的Q相关路由/重要token补救。阶段B：静态正确后采动态游戏帧，查局部闪烁、转镜/遮挡/切场景，并把预处理/选择/同步成本全部计入。阶段C：TeaCache式块残差复用先测变化量与输出误差的相关性，建立强制重算规则，再实现跳算；不照搬扩散步系数、不直接用整屏平均判变化。全部在AttExp，生产/游戏/发布包不自动替换。

当前从阶段A开始，针对ViT400/640token做隔离候选和自有benchmark；沿用无条件KV均值实验作速度/误差参照。每完成可复现阶段更新此记录并commit，拒绝把静态不黑屏当动态质量通过。

阶段A实现：Development/HIP/experiments/vit-select新增select.hip与隔离host生成器。prep每head/32-token组计算K和V成对差异能量比的较大值，存均值FP8及路由；跨行/尾组强制精算，本地32-token查询组精算。混合精算/摘要时摘要权重乘2，保持各组贡献尺度。prep只算一次，注意力不重复统计；900/1080有效，先限定FP8 QKV/f32输出路径。host.patch及临时benchmark在/tmp/vit-select-src，生产源码未改。双目标COMGR：940f8793…/7288c37f…；自有benchmark编译通过。全精算门槛−1对900/1080×真实/合成HDR/近黑六组全部输出hash与原版一致，正式计时不使用这轮6帧带诊断的冷启动耗时。接下来扫描0.05/0.15/0.5/1门槛并量质量。

阶段A第二轮：修正混合摘要的FP8权重位置——分母仍计2倍概率，分子保持原P量化尺度，把2倍放在已量化V上（超过224的V代表强制精算，避免加倍溢出）。原来在P端先加倍会引入额外subnormal舍入差异。新模块51f8e0ee…/d80fa230…；门槛0仅合并完全相同K/V，六组均与原版逐位相同，1080真实样本可合并最后两组重复token，900空间约束下无合并。门槛0.5真实画面显示MAE约1.209/1.287，相比无条件均值2.63/2.62减少，但尚不满足动态验收。

160帧ABBA（舍32，诊断读回关闭）发现当前动态分支版负优化：门槛0.5，900 13.433→13.7245ms、1080 19.57375→20.0705；门槛1，900 13.506→13.7685、1080 19.62425→19.915。只作为失败候选，不部署。正在分开重复prep/attention以定位额外成本，随后测试简化循环/把归一化2倍移到MMA乘数，避免先生成2倍概率再除回去。后续无损重复padding聚合也可研究，但目前只观察到样本中18/19两组相同，不能先假定所有输入恒同。

阶段A成本定位：同输出重复prep的归一化边际约900 0.046/1080 0.098ms，重复select attention约0.551/1.195ms（不是精确GPU分解），主问题在注意力体。改为单层可变步长循环、提前选择K/V地址，把分母2倍交给F16 MMA乘数；双架构20cc4051…/8be566ea…，六组全精算+18组近似输出全部与前版逐位一致。阈值1的160帧ABBA：900 13.42425→13.561（仍+0.13675ms），1080 19.5965→19.429（−0.1675ms）。尚不足以合入，接下来测同序逐tile立即AV累加，去掉完整P8 LDS表和第二遍读取；保持各累加器K序与舍入后再计时。

阶段A流式：逐tile概率立刻AV，去掉完整P8 LDS表与第二遍读取；659eeb69…/3207ea7f…双架构通过，全精算六组与原版相同、18组近似与前版相同。阈值1回放900 13.4305→13.327（−0.1035ms）、1080 19.569→18.673（−0.896）；阈值0.5为−0.08275/−0.769ms，真实捕获MAE1.209/1.287（0..255）仍有差异。

进一步去掉近似/路由，把同一数据流改写直接用于原版精算注意力：experiments/vit-stream-exact/prepare.py只生成候选patch，不修改hip/deep_fast.hip；每个累加器K序、指数bit映射、FP8量化和输出舍入保留，LDS8/11.75KiB→1.5KiB。双架构57a5ae37…/b338d70e…编译通过；720/900/1080×三输入九组AB、216输出全有限且冻输入一致，AB最终hash全同。160帧ABBA精算候选900 13.4135→13.19325（−0.22025ms，1.64%），1080 19.54625→18.72925（−0.817ms，4.18%），基线漂移0.007/0.0485ms。

本阶段决策：优先保留精算流式候选，近似并未展示值得承担画面误差的充分额外优势；不同候选来自不同轮ABBA，不把约0.08ms差距当严格直接优势。下一步先实玩精算候选，再以它作近似基线；动态路由验证和TeaCache相关性/残差复用仍按计划后续做。报告results/vit-select-20260920/report.md与timing-summary.json；Windows vit-stream-exact-build双目标模块已备好，游戏/发布包/生产源均未替换。
