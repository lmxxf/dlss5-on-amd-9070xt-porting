# DLSS5（DLSSNR）→ AMD RX 9070 XT 移植：开发史

> 本文件是本项目**唯一持续更新**的开发史，按时间线重新整理，以后只续写这一份。
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

## 4. 运行时约定（当前有效）

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
- 打 tag 时统一刷新根目录公开集（`scripts/game-flags.txt`、README 状态行、`bench.ps1` 重新拍平）；过程文件只进 `Development/`。

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

## 5. 当前状态（截至 2026-09-10 12:10）

- **剑星游戏里装的**：fast42p = fast42 + 黑帧探针，DLL `f56b9eef…`、flag 文件 `native-game-flags-fast42-blackprobe.txt`，cso 来自 `decfast`（D 盘只剩这一个链目录）；回退：`native-game-fast40.addon64` + `native-game-flags-fast40.txt`。Zero 实测 fast40 静止 36fps。跳块 {42,43,46} 在链里（40.66dB）。
- **帧率**：fast33 实测 29fps（网络 GPU 约 30.7ms + 游戏 6～7ms，GPU 满载）；fast38 Zero 实测 33～34fps，一般场景 35 上下。换区/下车仍会掉几秒（显存之争）。
- **测试台**（`decfast`，fast42）：09-10 19:10 停电重启后整帧中位 24.4 / min 23.8；之前几天的 33～37 是机器降频态的数字；单核隔离合计约 23.6 之后再减 fast38 的 2.4；PSNR 对 exact 41.96（fast36 起，fast37/38 逐位不变）；网络显存 3.75GB。
- **0.07 发布包** = fast36 已发（网盘）。
- **浪人崛起**：出图、颜色对；未验运动向量符号（拖影）、LDR 亮度是否与训练域一致、帧率、进游戏颜色骤变/黑帧是否同样出现。
- **已知瞬态问题**（09-10 09:55 Zero 报告，暂不处理）：fast37 刚进游戏颜色骤变几秒后稳定；偶发整帧变黑。
- 雨景闪烁：网络本身的空间扩散，输出平滑压到"不太闪"，问题严重时再弄。

---

## 6. 待办与候选方向（照 `PLAN.md`，09-10 07:45 重排）

上限估计：1+2+3 全做约 −3ms → 网络 22ms → 游戏 35～36fps；再往上要动网络本身。

1. ~~C32 家族 FFN 并进注意力核~~（fast37 已做：隔离 −1.2 / 整帧 −1.9）。
2. ~~多头 26 块 FFN+proj0 合核~~（做了，零收益，代码留默认关）。
3. ~~C512 家族激活~~：fast39（FFWD→投影 0 E4M3 tile，−0.8）+ fast40（mapped + FP8 stream，−0.3，显存 −380MB）已做。剩下的小尾巴：注意力输出 result[2] 仍是 `[token][512]` E4M3（512B 步长），投影 1 的 A 直读；FFWD 每个 group 都重新 stage 一遍 16×512 输入（8 倍冗余读）。估 −0.1～0.2，先不动。
4. ~~post merge fold 4 通道 Load~~（fast41，−0.1；post 段 2.90 vs pre 2.81 基本持平了）。
5. dispatch 之间空转约 3.5ms：只能靠合核压（对带宽型段）。
6. ViT expand+contract 合核 −0.3。升采样投影输出改 f16（值本来就是 H()），下游 C32 首块 / 多头首块读 f16：估 −0.2（fast42 之后三段已是带宽型）。
6b. ~~指令级 profiler~~ 已打通（09-10 18:00）。待做：(a) C32 合核消溢出——换写法无效（20:10），要动结构（偏置表进 LDS / 减少同时在飞的 s[4] 累加器）；(b) ~~时钟~~ 是降频态，重启后 24.4，游戏里也回 36；(c) 让 Zero 用 RGP 界面看一次事件时间线，把 pipeline hash 和耗时对上（Zero 说等某个游戏从头到尾出问题再说，不急）。
7. 显存与波动：PoolSize 6000→5000 或贴图降档；C512 16 块 result 共享 200MB；shared ffn/raw 已 f16；decoder entry+split8 331MB 里 entry 的 f32 权重。
8. 网络约化/蒸馏（写作线，周级）：九块跳过版 36.5dB −2ms 等 Zero 看画面；蒸馏 = exact 链当老师、DLL 每帧 dump 当数据，难点是 71 块可反向传播 torch 模型 + FP8 假量化 + 算力。
9. 浪人崛起验收（上面「当前状态」）；通用化正路是冒充 NGX 的 `nvngx_dlss.dll`（OptiScaler 那条），周级，另一个项目。
10. 瞬态：进游戏颜色骤变、偶发黑帧（先记着）。

---

## 7. 工程教训

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
