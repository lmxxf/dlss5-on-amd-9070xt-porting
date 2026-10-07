# 更新日志

[English](CHANGELOG.md) · README 里的更新记录表是一行一版的摘要；这里按版本展开：改了什么、效果、开关、是否逐位、对应的实验目录。版本从旧到新，新版本加在最后。

说明：
- 「逐位」指快速链输出与上一版一位都不差（EXACT 与 AE 都算）。0.01 之后精确参考链冻结当裁判，快速链对它约 42 dB PSNR。
- 「离线」是只跑网络的测试台（1000 帧弃前 200 取均值），「900 档 / 1080 档」是网络处理 1600×960 / 1920×1152。游戏内帧率除注明外为《剑星》、RX 9070 XT。
- 没测过的数字不写。实验记录在 `Development/results/<目录>/`，过程在 `Development/DevHistory.md`。

## 早期版本（0.01～0.23）

这一段的细节就是 README 更新记录表对应行，这里只列要点。

| 版本 | 日期 | 要点 |
|---|---|---|
| 0.01 | 09-08 | 精确移植终点：71 块全走 wave 矩阵核，15 帧与原版逐位一致；测试台 186 ms，约 5 fps |
| 0.02 | 09-08 | 快速链开始（精确链冻结当裁判）：FP32 硬件累加、E4M3 操作数；接上时序；112 ms |
| 0.03 | 09-08 | 硬件 f16/E4M3 转换、QKV+归一化合核等；62.7 ms，约 15 fps |
| 0.04 | 09-09 | 延迟提交环、命令列合批、噪声前缀 ALU 生成；23～25 fps |
| 0.05 | 09-09 | 输出侧时间平滑、ViT 注意力 FP8；24～25 fps |
| 0.06 | 09-09 | 仓库重整、GPU 探针默认关、pre 块输出 E4M3；27～28 fps |
| （0.07） | 09-09 | 跳过三块（有损，40.7 dB）、显存 6.8→3.75 GB；29 fps |
| 0.08 | 09-10 | 一批逐位的布局/直读优化；《浪人崛起》XeSS 路径；测试台 24.4 ms，36～37 fps |
| 0.09 | 09-11 | 升采样投影 f16 光栅（逐位，−0.2 ms）；Magpie 版可用于任意游戏（约 30 fps） |
| 0.10 | 09-11 | 黑块根因修复：E4M3 转换前夹到 ±448（`DLSS5_BUILD_C32_SAT_CAST`） |
| 0.11 | 09-11 | 接管时间 20～30 秒 → 约 3.5 秒（预读权重、批量常驻、shader 磁盘缓存） |
| 0.12 | 09-12 | 屏幕提示（分辨率不对/初始化中/失败）；`DLSS5_NOTICE` |
| 0.13 | 09-12 | `DLSS5_SHOW_FPS`；按输出状态接管 |
| （0.14） | 09-12 | Magpie 整包：FPS 三秒刷新、清理 |
| 0.15 · [包](https://pan.quark.cn/s/1601ca8f80ae) | 09-13 | 窗口 ≤1920×1080 按比例适配（`DLSS5_FIT_INPUT=1`）；另有 [0.15-900P](https://pan.quark.cn/s/a5339e4c8549) |
| 0.20 · [包](https://pan.quark.cn/s/3c8b5329353c) | 09-17 | **HIP 后端**：COMGR 编 gfx1201 内核，与 DX12 链逐位一致；900p 约 15.5 ms、游戏内 900p 52 fps |
| 0.21 · [包](https://pan.quark.cn/s/85a507a744bd) | 09-17 | `DLSS5_NETWORK_HEIGHT=auto` 自动选档；与 0.20 逐位 |
| 0.22 · [包](https://pan.quark.cn/s/03f9995d0551) | 09-17 | 900 档补边 1024→960 行（−0.9 ms）；**输出变化**（对 0.21 约 31.5 dB），`900w` 可回旧布局 |
| 0.23 · [Magpie](https://pan.quark.cn/s/548e52cc4f49) · [OptiScaler](https://pan.quark.cn/s/8b5a402012a2) | 09-18 | 多 GPU/核显机器按适配器名选 HIP 设备；与 0.22 逐位 |

## 0.24（09-19）

下载：[OptiScaler](https://pan.quark.cn/s/1f32ffbd2e96)（HIP）

- **改了什么**：前置超分路线——游戏低分辨率颜色 → DLSS5 → OptiScaler 的 FSR 3.x/4 → 最终输出，同队列异步提交。只改本项目插件；OptiScaler 本体、权重、内核不变。
- **效果**：《剑星》2560×1440、FSR 质量档（输入 1707×961）约 34～35 fps。
- **行为变化**：DLSS5 历史暂时每帧重置（FSR 时序保留）；网络输入仍须 ≤1920×1080。
- **逐位**：网络输出与 0.23 相同。

## 0.24.1（09-19）

下载：[OptiScaler](https://pan.quark.cn/s/4f73a54d0ff9)（HIP）

- **改了什么**：配置/资产路径查找——DLL 被加载到 `_storage_` 等子目录、旁边没有 `DLSS5-AMD` 时，继续到游戏 EXE 旁查找，避免误读开发目录的旧配置。
- **逐位**：内核与权重不变。
- 不解决《生化危机 9》同一命令列表内的前置接入限制。

## 0.24.2（09-19）

下载：[OptiScaler](https://pan.quark.cn/s/f74aaa5c7f9a)（HIP）

- **改了什么**：插件在没有前置任务时不再每次绘制都查配置、锁任务表；减少日志计数争用；F6 关闭时直接旁路前置捕获和颜色复制。同日重打完整包，补上 FPS/状态显示开关。
- **效果**：《剑星》主城约 49 fps（此前约 31 fps，CPU 开销）。
- **逐位**：内核与权重不变。

## 0.25（09-19）

下载：[Magpie](https://pan.quark.cn/s/09630ed99606) · [OptiScaler](https://pan.quark.cn/s/636691131c5f)（HIP）

- **改了什么**：C32/多头注意力复用指数计算、简化倒数；中间特征和解码输出以 FP8 字节传递；解码完整分组走快速路径。修复 900 档尾部漏写、中文路径加载失败。**新增 gfx1200（9060/XT）内核**，与 gfx1201 按设备自动选择。
- **效果**：Magpie 1080P 仅 DLSS5 约 37 fps，与之前基本持平。
- 9060/XT 当时待实机反馈。

## 0.26（09-19）

下载：[Magpie](https://pan.quark.cn/s/7ce2ca11db43) · [OptiScaler](https://pan.quark.cn/s/c880a70f0824)（HIP）

- **改了什么**：FFN 直接读 FP8 字节片段（省入口共享缓冲暂存和两道同步）；C256 权重初始化时预排成连续矩阵片段。补齐漏打包的 R11G11B10 解码 shader（修复《匹诺曹的谎言》低效果品质黑屏）；增加 shader 编译/绑定校验。
- **校验**：包内文件及 44 种 shader 组合校验通过。

## 0.26.1（09-20）

下载：[OptiScaler-REFramework](https://pan.quark.cn/s/624c87a6aa11)（HIP，RE9 专用）

- **改了什么**：RE9 这类特殊接入的后置 HIP 路线——R10G10B10A2/FP16 转换、FSR 后处理、固定 900P 计算、1080P SDR 输出保护；状态/分辨率/Present 帧率与 F7 信息开关。集成 REFramework、OptiScaler、ReShade、完整模型与双架构内核，沿用 0.26 优化。
- 仅《生化危机 9》实测，普通游戏用通用包。

## 0.27（09-20）

下载：[Magpie](https://pan.quark.cn/s/ec3a3282aa76) · [OptiScaler](https://pan.quark.cn/s/004278159ed8) · [OptiScaler-REFramework](https://pan.quark.cn/s/010683548f68)（HIP）

- **改了什么**：精确流式 ViT 注意力，减少中间存储与重复读取，保持原计算与舍入。可选 R3 自适应复用（变化检测、静止延长缓存、融合提交），**默认关**。
- **逐位**：流式 ViT 与原计算一致；自适应复用是可选的有损开关。
- 三包各 44 个 shader 变体及 ZIP 逐文件校验通过；不含 INT4/剪枝。

## 0.28（09-22）

下载：[Magpie](https://pan.quark.cn/s/11547f398eb4) · [OptiScaler](https://pan.quark.cn/s/f7f423b0ea3a)（HIP）

- **改了什么**：六项无损内核优化——RGB 共用读取、C128/C256 零填充跳过、ViT 展开/投影与解码投影固定尺寸。
- **效果**：《剑星》实玩效果/帧率基本不变。
- RE9 的 0.28 下载已撤下，改用 0.28.1。

## 0.28.1（09-22）

下载：[OptiScaler-REFramework](https://pan.quark.cn/s/1375693a0d21)（HIP，RE9 专用）

- **改了什么**：真实输入超限时在 HIP 初始化前拒绝并保留原始超分；初始化失败安全回滚，改回有效尺寸可恢复；保护未退休帧。宿主与 runtime 需配套更新；源码与 TheAutomatic 署名随包。
- **校验**：10 组 / 12 提交帧回归与用户初步实玩通过。

## 0.29（09-23）

下载：[Magpie](https://pan.quark.cn/s/fe1b6af36cad) · [OptiScaler](https://pan.quark.cn/s/209e04e7acaf) · [OptiScaler-REFramework](https://pan.quark.cn/s/505d38a63a85)（HIP）· [Google Drive 镜像](https://drive.google.com/drive/folders/1VPsX33sLTxBG4J8kJ_IzBDlkbBTCc5Eo?usp=sharing)

- **改了什么**：
  - 超过 1920×1080 的输入不再拒绝：缩到 1080 档跑网络，再按原版 codec 方式还原到原分辨率（`DLSS5_FIT_LARGE=1`，issue #6）。
  - 自 0.28 以来六项逐位优化：栅栏作用域、C32 折叠 FFN、字节链 + 向量化输入、注意力寄存器化、in16 别名、FFN 尾段转置。
- **效果**：内核约 −7%；《剑星》900P 约 60 fps、2K Native AA 44 fps；RE9 Native AA 实测可用（超宽屏未实机）。
- **新开关**：`DLSS5_FIT_LARGE=1`（模板默认开）。
- **逐位**：内核优化逐位；FIT_LARGE 是新的输入路径。
- RE9：宿主不变、runtime 更新。

## 0.30（09-25）

下载：[夸克](https://pan.quark.cn/s/80a735ab9f88) · [Google Drive 镜像](https://drive.google.com/drive/folders/1pKZpLosgJXxUOZTMg_m0sbCipX9Q3WYo?usp=sharing)（三个包）

- **改了什么**：
  - 链上 launch 任意序 + tile 旗子（土法 programmatic dependent launch，`DLSS5_HIP_PDL=1`），C64～C256 链。
  - FFN 全行写。
  - 《赛博朋克 2077》零配置：常规包 `OptiScaler.ini` 带 `[Inputs] EnableFfxInputs=false`；`DLSS5_PRE_UPSCALE_ASYNC=auto`（2077 同步：瞬态别名颜色缓冲）；`DLSS5_STRENGTH=auto`（2077 只转亮度）。
  - 修复切档位/分辨率后神经处理消失（FSR 上下文钉死）。
- **效果**：PDL 900P 约 −1.6%、1080P −0.6%；FFN 全行写 −0.6%。《剑星》900P→2K 60～61、1080P→2K 47～48；2077 低画质平衡 51～52、质量 41。
- **新开关**：`DLSS5_HIP_PDL=1`、`DLSS5_PRE_UPSCALE_ASYNC=auto`、`DLSS5_STRENGTH=auto`（模板默认）。
- **逐位**：PDL 与 FFN 全行写逐位。
- **实验目录**：`results/pdl-chain-20260925`。
- RE9 包：同一组核，宿主/runtime 与 0.29 相同。

## 0.31（09-26）

下载：[夸克](https://pan.quark.cn/s/e76b8611e3cc) · [Google Drive 镜像](https://drive.google.com/drive/folders/1xtBe_XhgF9eqBlrlIQMgWcEkzm0UKHIZ?usp=sharing)（三个包）

- **改了什么**：
  - 档位选择：输入两轴都不超过某档 110% 时往下缩进该档（2K 质量 1707×961 → 900 档，原先放大进 1080 档）。
  - 一头一 wave 核（C32/C64/C128 整块 + C256 注意力，`DLSS5_HIP_WAVE_OWNED=1`）。
  - C512 QKV/mix 32 token（`DLSS5_HIP_C512_M32=1`）。
  - ViT 投影 64 列（`DLSS5_HIP_VIT_PROJ_N64=1`）。
  - 常规包默认 `DLSS5_VIT_ADAPTIVE=1`（自适应复用，静止 +3 帧，运动自动失效）。
  - 五个新模块（c32-wave1、c64-wave2、c512-m32-mh、c512-m32-deep、vit-wide-deep），每架构 29 个模块。
- **效果**：一头一 wave 整网约 −6%；C512 M32 约 −1.3%；ViT N64 1080 约 −1.8%。《剑星》主菜单 2K 质量 57、2K Native AA 43～44。
- **逐位**：三组内核改动逐位；档位选择改变 2K 质量档的网络档（行为变化）；VIT_ADAPTIVE 为有损复用（可关）。
- **实验目录**：`results/c64-wave2-20260926`、`wave-owned-*`、`c512-ffn-20260926`、`m32-sweep-20260926`。
- RE9 包：宿主/runtime 与 0.30 相同，新核随包不启用。

## 0.32（09-26）

下载：[夸克](https://pan.quark.cn/s/b805e071405c) · [Gofile 镜像](https://gofile.io/d/CZ67LYIc)（三个包）

- **改了什么**：
  - 显存池：HIP 导入的 D3D12 共享缓冲区驱动不归还，改为按档位复用（切 40 次 +3 GB → 平台）。
  - C32 宽读（向量输入读取）。
  - RE9 runtime 读 flags（`DLSS5_HIP_*` / `SKIP_BLOCKS` / `FIT_LARGE` / `NETWORK_HEIGHT`），默认开 0.31 新核，兼容老宿主两参数 `EnqueueHip`；合入 PR #9。
- **效果**：C32 宽读 −0.8% / −0.9%；离线 900 档 10.74 ms、1080 档 15.01 ms；RE9 中画质 2K 高质量 54、原生 AA 38。
- **逐位**：C32 宽读逐位。
- **实验目录**：`results/vram-leak-20260926`、`c32-wave-phase-20260926`、`re9-runtime-flags-20260926`。

## 0.33（09-27）

下载：[夸克](https://pan.quark.cn/s/6bb64e46ab67) · [Gofile 镜像](https://gofile.io/d/8yAjJX1b)（三个包）

- **改了什么**：
  - FP8 打包：c32-wave1 的 `CW_PACK8`、c64-wave2 的 `W2_PACK8`——一条 `cvt_pk` 转两个值写进片段字。
  - 插件：前置状态行显示 AE/EXACT；F7 开关屏幕文字；共享的环境选项解析。
- **效果**：离线 900 档 10.74→9.80 ms、1080 档 15.01→13.64 ms（约 −9%）。《剑星》1080P 原生 AA EXACT 主菜单 49～50、常见场景 53～54。
- **逐位**：相对 0.32 逐位。
- **实验目录**：`results/c64-block-fused-20260927`、`pack8-20260927`。
- RE9：宿主/runtime 与 0.32 相同（只换模块）。

## 0.34（09-27）

下载：[夸克](https://pan.quark.cn/s/4b572b0a5b81) · [Gofile 镜像](https://gofile.io/d/cfHqVzD1)（三个包）

- **改了什么**：
  - fmed3 clamp（`HIP_FMED3_CLAMP`、C32 `HIP_FP8_SAT_MODE 3`）与 `W2_PACK8 6`（分段 FP16_OVFL + fma(x,y,+0) 打包）；整套模块由 `hip/build-modules.ps1` 按生产配方编出（3 个回退模块从源码重编）。
  - PDL 计数回绕保护；卸载时释放 PDL 缓冲。
  - RE9 宿主 aa3761f2：跟随实际执行拆分列表的队列 + 8 次评估看门狗（《鬼武者》）；runtime ca6d6bdc：切档泄漏 35 MB→0，每次尺寸/档位变化记几何日志；重新生成宿主/runtime 源码包。
- **效果**：《剑星》1080P AA EXACT 主菜单 50～51 / 场景 54；RE9 中画质 2K 高质量 58、原生 AA 41；《鬼武者》2K 质量约 60。
- **逐位**：相对 0.33 逐位。
- **实验目录**：`results/fmed3-ovfl-20260927`、`ovfl-census-20260927`、`c64-hand-asm-20260927`、`pdl-audit-20260927`、`onimusha-presr-20260927`、`re9-runtime-leak-20260927`。

## 0.35（09-27）

下载：[夸克](https://pan.quark.cn/s/83e6172e6c79) · [Gofile 镜像](https://gofile.io/d/NnF4GitT)（三个包）

- **改了什么**：
  - C32 三轮：去重复 FP8 往返（`CW_DIRECT_OUT`、`CW_PREFIX_DIRECT_OUT`）、RTZ/LDS 写向量化（`CW_RTZ_PAIR`）、分段饱和模式（`CW_PACK_MODE_MASK`）、prefix/finish 完整窗口快路径（`CW_PREFIX_FULL_TILE` 等）。
  - ViT 字节流（`DLSS5_HIP_VIT_STREAM=3`，新 `vit-stream` 模块：注意力输出以字节直接喂投影，兼容自适应复用）。
  - 插件 4151123e；每架构 30 个模块；RE9 runtime 432d8ccf（同一开关，宿主 aa3761f2 不变）。
- **效果**：离线 900 档约 9.3 ms、1080 档约 12.75 ms。《剑星》1080P AA EXACT 约 56.7（0.34 为 54）；RE9 中画质 2K 高质量 58～59、原生 AA 42。
- **新开关**：`DLSS5_HIP_VIT_STREAM=3`（模板默认）。
- **逐位**：相对 0.34 全部逐位。
- **实验目录**：`results/c32-aco-20260927`、`c32-round2-20260927`、`c32-round3-20260927`、`vit-bytestream-20260927`。

## 0.36（09-28）

下载：[夸克](https://pan.quark.cn/s/e5afdaca0769) · [Gofile 镜像](https://gofile.io/d/Z1hWdjcB)（三个包）

- **汇总**：插件 d2290ad7；每架构 30 个模块；RE9 runtime 7ce2bc21（宿主 aa3761f2 不变）；输入 shader `native_game_rgb_input.hlsl` 5be59a41（匹配输入直写）。
- **累计效果（相对 0.35 发布包，同批两轮 ABBA）**：离线 900 档 9.37 → 8.58 ms（−0.79 ms，−8.5%）、1080 档 12.71 → 11.58 ms（−1.13～−1.14 ms，−8.9%）。1080 档每帧派发 214 → 182。
- **逐位**：**0.36 与 0.35 输出不逐位相同**——float FMA 激活（下面第 4 条）是一次有意的数值变化，对 NVIDIA 原版误差基本持平；0.36 起为新的逐位基准，其余各项在各自那一步都与上一版逐位。
- **新开关**：`DLSS5_DIRECT_IO`（常规/Magpie 模板默认 1，RE9 不写）、`DLSS5_FRAME_STATS=<秒>`（模板默认 0）。
- **改了什么**（按时间）：

1. **C64～C256 深挖**（c64-wave2 配方：字节输入整组读取、RTZ 配对、直接坐标）：离线 900 −0.7%、1080 −0.8%；逐位；只换模块。`results/mh-round1-20260927`、`tier900-20260927`。《剑星》1080P AA EXACT 57.1。
2. **帧时间分布日志** `DLSS5_FRAME_STATS=<秒>`（插件 + RE9 runtime，写 `DLSS5-AMD\logs\frame-stats.txt`，模板默认 0）。`results/frame-stats-20260928`。
3. **ACO 逐段对齐两刀**：删除 C32 激活前多余的 NaN 规范化；C64～C256 softmax 用有界倒数（保留求和顺序与误差修正）。900 −0.82% / −0.65%、1080 −0.75% / −0.74%；逐位。`results/aco-lineup-20260928`。
4. **float FMA 激活（有意的数值变化，09-28 起逐位基准变更）**：C32、C64～C256、ViT/C512 的同类激活乘加收缩为 float FMA（此前为与 HLSL `precise` 对齐的两次舍入；NVIDIA 原版是 half FMA）。900 省 0.110～0.115 ms、1080 省 0.148～0.151 ms（约 1.2%）；对 NVIDIA 原版误差基本持平（单帧 RMSE 0.008038→0.008037）。此后以本版输出为逐位基准。`results/fma-vs-nvidia-20260928`、`float-fma-20260928`。
5. **输入直写 / 输出直交** `DLSS5_DIRECT_IO`（0 原路径、1 输入直写进 HIP 共享缓冲、3 再加 FSR 直接读解码输出）：省一次 35 MB 拷贝与一次回拷；离线 −0.02～0.05 ms；网络输出逐位。时序会话（Magpie）、`DLSS5_OVERLAP` 与非 RGBA16F 格式自动走原路径；RE9 不涉及。`results/zero-copy-io-20260928`。
6. **C256 整块融合**（多组 token 共用一份权重，FFN 权重读取减半，主力核 VGPR 190→154）：1080 −1.67% / −1.63%（约 −0.20 ms），900 保留原路径；每帧派发 1080 档 214→198；逐位。`results/c256-fusion-20260928`。
7. **C512 融合 + C64/C128 权重共用**：C512 每块少一次派发（1080 档 198→185、900 档 214→201），C64/C128 权重读取减半。900 −3.59%（约 −0.33 ms）、1080 −2.55% / −2.63%（约 −0.31 ms）；逐位。离线 1080 档约 11.82 ms、900 档约 8.72 ms。`results/c512-fusion-20260928`。
8. **上采样融进首块**：C64/C128 与 C32 的上采样并入下一级首块，每帧再少 3 次派发（1080 档 185→182、900 档 201→198）；900 −0.17～−0.18 ms、1080 −0.25～−0.26 ms；逐位。ViT 与下采样候选实测不赚，未合。`results/fusion-round3-20260928`（含 `package-036-checklist.md`）。

游戏内（本机，RX 9070 XT，EXACT 静止）：《剑星》1080P 原生 AA 约 57 → 60（已触窗口模式 60 Hz 上限）；2560×1440 原生 AA C512 融合后 52～53、最终 54（此后游戏内对比用这个设置）。


## 0.37（09-29）

下载（三个包）：[夸克](https://pan.quark.cn/s/7dbfdc6425fd) · [Gofile 镜像](https://gofile.io/d/onqeAHST)

- **汇总**：插件 b77bbc3c；每架构 31 个模块（新增 `swin-persistent.hsaco`）；RE9 runtime 2c103f6e（宿主 aa3761f2 不变）；shader 与 0.36 相同（输入 shader 5be59a41）。
- **逐位**：**与 0.36 全部逐位相同**（09-28 float FMA 基准，7 用例 EXACT/AE 168 帧 + 回绕/超时压力帧，每一步都验），没有有损改动。
- **累计效果**：离线网络 900 档约 8.5 → 8.0 ms；每帧派发 900 档 198 → 179、1080 档 182 → 162。《剑星》2560×1440 原生 AA、EXACT 54 → 55～56（本机）。
- **新开关**：`DLSS5_HIP_SWIN_RUN`（C256 持久化；源码默认 0，**三个发布模板都设 1**；RE9 runtime 同样读取；设 0 回到原派发）。
- **包内其他**：常规 OptiScaler 包也带 `ReShade.ini`（`TutorialProgress=4`，去掉 Home 引导遮罩），与 Magpie 包一致；RE9 源码包补上 HIP 配方的 `.inc` 文件。
- **改了什么**（按时间，每一步都对上一步逐位）：

1. **C512 点运算紧凑布局**：FFN/卷积只算有效 tile（1080 档 160 → 135 个 4×4 tile），移位与补边推迟到 attention 读取；attention 窗口与 softmax 不变。900 −0.16～−0.18 ms、1080 −0.23～−0.25 ms（两档各约 −2%）；1080 档派发 182 → 169。`results/deep-layers-20260929`。
2. **逐核地图两刀**：全网 1080 逐核对照（我方 169 / 参考 154 次派发）后，C512 head 池化与投影合成一次派发（head 分组融合），ViT attention 分数排布转置。900 −0.02～−0.04 ms、1080 −0.12～−0.14 ms（1.0～1.2%）；派发 198 → 197 / 169 → 168。`results/kernel-map-20260929`。
3. **C256 跨层持久化**（`DLSS5_HIP_SWIN_RUN=1`）：C256 编码器/解码器各六个内部层走设备就绪队列；约 100 ms 超时即在 GPU 上串行重算该段并停用本实例，坏中间结果不出网。900 −1.90～−1.94%（约 −0.16 ms）、1080 −0.57～−0.61%；派发 197 → 179 / 168 → 162。`results/swin-persistent-20260929`。
4. **ViT attention 新核**：有界正常 half 改用精确硬件转换、概率在寄存器内成对编码、分母/AV 输出转置。640 token 单核约 71 → 38 µs；整网 900 −2.20～−2.28%（约 −0.19 ms）、1080 −1.50～−1.58%（约 −0.17 ms）。`results/vit-attention-20260929`。
5. **ViT QKV 五 wave 共用权重**（W5）：5 个 wave 经 LDS 共用一份权重（8 KB 块双缓冲），读取量约除以 5、wave 数不减；640 token 单核 48.8 → 37.0 µs。整网 900 −0.42～−1.39%、1080 −0.74～−1.07%。新宿主按导出自动探测，旧模块自动回落。`results/vit-qkv-20260929`。

不进包的负账：C128/C64 持久化（未达 0.5% 门槛，`results/swin-persistent-c128-c64-20260929`）、C512 投影权重共用（逐位但变慢，`results/c512-proj-share-20260929`）、`MAKE_RESIDENT` 尖峰（离线未复现，`results/resident-spike-20260929`）。打包清单 `Development/results/package-037/checklist.md`。

## 0.38（09-30）

下载（三个包）：[夸克](https://pan.quark.cn/s/6856d875bbe9) · [Gofile 镜像](https://gofile.io/d/lzsqfUiE)

- **逐位**：**默认设置下与 0.37 全部逐位相同**（09-28 float FMA 基准；7 用例 EXACT/AE 168 帧 + 票号回绕，每一刀都验；打包用的插件与 RE9 runtime 从发布源码重编后，又对现装再验一遍 168 帧 + 回绕，全同）。唯一的有损项是可选开关 `DLSS5_NETWORK_1080_ROWS=1088`，默认不开。
- **效果**（离线整网回放，NativeGameFrame，1000 帧弃 200，单帧 ms）：900 档约 **8.0 → 7.6 ms**，1080 档约 **10.8 → 10.4 ms**（发版模块实测 900 7.551/7.606、1080 10.413/10.458，两轮，`results/hip-roofline-20260930` 第 1 节；逐核地图 `results/kernel-map-v3-20260930`）。每帧派发 900 档 179 → 162、1080 档 162 → 158。游戏内只作正确性验证（《剑星》《鬼武者》不闪不花不崩），帧率读数不作为提升依据。
- **新开关**：
  - `DLSS5_FORMAT_FALLBACK`（三个模板为 1，源码默认 1）：原来不支持的颜色格式——R9G9B9E5、B8G8R8X8、R10G10B10A2、R32G32B32(A32)、R16G16B16A16/R8G8B8A8 SNORM、B5G6R5、B5G5R5A1、B4G4R4A4——常规包 pre-upscale 路线用一个 compute pass（新增 `native_format_convert.hlsl`）转成 RGBA16F 后走原路线；RE9 runtime 走私有 FP16 输出路线。原来支持的格式不经过这张表（逐位）。被拒的格式日志里带格式名。post-upscale / Magpie / XeSS 路线不变。设 0 = 原表。`results/product-fmt-reload-20260930`。
  - `DLSS5_HOT_RELOAD`（常规/Magpie 模板为 1，源码默认 1；RE9 不适用）：游戏运行中改 flags 文件的 `DLSS5_STRENGTH` / `DLSS5_NOTICE` / `DLSS5_SHOW_FPS`，一秒内生效；其余键仍需重启。不改文件即无任何变化。
  - `DLSS5_NETWORK_1080_ROWS`（模板 1152 = NVIDIA 原版几何）：**可选 `1088`，有损、默认不开**——1080 档只算 1088 行，整网约 −0.47 ms（4.4%），对 1152 全帧约 56 dB，底边 32 行略差。`results/geom-1088-20260930`。
  - 宿主内部开关（默认 1，一般不用动）：`HIP_C512_PAD16`、`HIP_C256_FFN_W16`，缺新模块或容量不够自动回退旧路径。
- **改了什么**（按时间，每一步都对上一步逐位；数字是各自当时的 ABBA）：

1. **900 档去 C512 shift_pack**：生产者直接按 16 token 补齐分配（1500 → 1504），C512 块原地读，13 次 `mh_shift_pack` 消失；只改宿主。900 −0.09～−0.11 ms，1080 不变。`results/shift-pack-900-20260930`。
2. **C32 块 4 跳连/下采样存 E4M3 字节**：这两个张量的值本来就是 E4M3 精确值，改存字节后读写降到 1/4。900 −0.05 ms（0.62%）、1080 −0.09 ms（0.82%）。`results/c32-align-20260930`。
3. **C32 对角残差跳过全零半块**（`CW_DIAG_ONLY`）：1080 约 −0.04 ms。`results/small-cuts-20260930`。
4. **I+P+O**：C32 入口/prefix 去 half 往返，C512 mix 与 ViT contract 占用上限。900 −0.03 ms、1080 −0.02～−0.03 ms。`results/small-wins-retest-20260930`。
5. **复合量化**：`FP8(Hrtz(x))` 在 WMMA 字节出口改成整数位掩码，省 f32→f16→f32 往返；2³² 穷举证明。c64-wave2/swin-persistent（`results/composite-quant-20260930`）与 C512 两个出口（`results/composite-quant-c512-20260930`），各 −0.005～−0.03 ms。
6. **C256 FFN 权重 16 字节片段**：一条 16 字节读喂两条 WMMA，k 顺序不变；缺模块/旧宿主自动回退。900 −0.01～−0.03 ms。`results/c256-w16-20260930`。
7. **C32 prefix 下采样 / 块 69 主干存 E4M3 字节**：900 −0.01～−0.02 ms、1080 约 −0.02 ms。`results/prefix-post-20260930`。
8. **三处宽写**：C32 prefix 字节尾（900 −0.04、1080 −0.07 ms，`results/prefix-post-arith-20260930`）、C32 finish 字节尾（900 −0.04、1080 −0.05 ms，`results/tail-vec-20260930`）、C512 t8 字节副本经 LDS 转置宽写（约 −0.01～−0.03 ms，`results/deep-tail-20260930`）。
9. **C512 QKV-attention 去冗余 F + 有界倒数**：AV/QKV 出口的多余钳位删掉，softmax 除法换 rcp + 两步 Newton；两档三轮 −0.02～−0.03 ms。`results/c512-av-f-20260930`。
10. **F/除法清理扫全网**：只收 deep_fast-packed（900 −0.01～−0.04、1080 −0.01～−0.03 ms）。`results/f-sweep-20260930`。

- **包内**：新增 `native_format_convert.hlsl`；三个 flags 模板加上面三个开关；插件与 RE9 runtime 从发布源码重编（tag 0.38）；RE9 宿主不变；RE9 源码包重生。
- 不进包的负账（逐位但不全正，宏默认 0）：D3D→HIP GPU 轮询 `DLSS5_HIP_INPUT_POLL`（`results/handoff-gpu-20260930`）、C512 V 转置（`results/c512-compact-vt-20260930`）、W16 推广 C64/C128（`results/w16-c64-c128-20260930`）、C512 FFN LDS 共用权重（`results/c512-ffn-lds-20260930`）、Infinity Cache 热复用（`results/infinity-cache-20260930`）。打包清单 `Development/results/package-038/checklist.md`。

## 0.39（10-01）

下载（三个包）：[夸克](https://pan.quark.cn/s/dea9c0ef2f95) · [Gofile 镜像](https://gofile.io/d/iqtFTSpS)

- **逐位**：**默认设置下与 0.38 全部逐位相同**（7 用例 EXACT/AE 168 帧 + AE 决策 + 900/1080 票号回绕，19 组 SAME；每一刀都验，装机版本整体再验一遍）。
- **效果**（离线整网回放，单帧 ms）：900 档约 **7.27 → 6.8 ms**，1080 档约 **10.05 → 9.5 ms**。《剑星》2K 游戏内实测约 **+1.5～2 帧**（55.6～56.1 → 57～58 fps），无闪烁；《鬼武者》正常。
- **改了什么**：一批逐位重排的核——C512 FFN 一个 wave 在寄存器里做完（`C512_FFN_ONE`）、attn-project 残差初始化外提、C32/C64 权重读取外提、解码加宽（`HIP_DEC_WIDE`）、ViT QKV/attention 合并等；每刀各自 ABBA 三轮两档全正才收。过程与数字见 `Development/DevHistory.md`（09-30 晚～10-01）。
- **新开关 `DLSS5_STYLE`**（三个模板都是 `1`，源码默认 1）：NVIDIA NGX 的 Style 控制，`0`/`1`/`2`。之前预处理里写死 Style 1（第 6 个特征 = Style/128），这是原版运行时在《剑星》里实际用的值。现在改成 C32/prefix 模块里的设备常量，宿主加载模块后写入。`0` 是 NVIDIA 默认值、也是他们参考图用的设置（1080p 单帧对 NVIDIA：Style 1 为 24.06 dB，Style 0 为 44.26 dB（发版配方）/ 47.43 dB（完整 71 块））；`2` 是第三种风格。不是恰好 0/1/2 的值回落到 1。改了要重启游戏。默认 `1`：**逐位一致**，GPU 没有多余工作。RE9 包：runtime 目前不从 flags 文件读这个键（模板那行改了不生效），要换风格得设系统环境变量。`results/rebuild-baseline-20261001`。
- **可复现构建**：add-on 和 RE9 runtime 改为固定基址、不写时间戳链接，同一份源码在哪编出来都是同一个文件（之前链接器按输出路径算基址，每次重编所有绝对地址都不同）。本版包里的 add-on 与 RE9 runtime 由发布源码重编逐字节复现；62 个模块按代码段与源码核对。
- 宿主：去掉了一个默认用不上的 fast 档探测（每帧多 0.005～0.02 ms）；查不到的核函数会缓存结果。
- **包内**：三个 flags 模板加 `DLSS5_STYLE=1`；add-on、RE9 runtime、62 模块与《剑星》《鬼武者》现装逐字节相同；RE9 宿主不变；RE9 源码包重生。打包清单 `Development/results/package-039-20261001/checklist.md`。

## 0.40（10-03）

下载（三个包）：[夸克](https://pan.quark.cn/s/d38e0f653c5a) · [Gofile 镜像](https://gofile.io/d/moSf7cqf)。

- **默认输出变了**（与 0.39 不逐位）：模板改成全 71 块 + fast 数值（`DLSS5_SKIP_BLOCKS=` 空值、`DLSS5_FAST_NUMERIC=1`）。对 NVIDIA（1080p 单帧，Style 0）44.26 → 47.55 dB，整体偏色消失；离线每帧比旧默认慢约 0.12 ms（900）/ 0.19 ms（1080）。写 `DLSS5_SKIP_BLOCKS=42,43,46` 和 `DLSS5_FAST_NUMERIC=0` 就逐位回到 0.39 的输出（下面每一项逐位改动都对上一版验过 19 组 SAME）。
- **叠层**（`DLSS5_MULTI_PASS=1/2/3`，默认 1）：网络每帧把自己的输出再跑 1～2 遍，风格更浓；耗时约为 N 倍。常规与 Magpie add-on 里按 **F9** 在 1→2→3 之间轮换，选择记进 `custom-config.txt`。《剑星》2K 实测 1/2/3 遍 57 / 37 / 27 fps。RE9 runtime 启动时读这个键（没有热键）。
- **三个配置文件**：`default-config.txt`（随包，升级覆盖）→ `custom-config.txt`（你的，包不会碰）→ `native-game-flags.txt`（旧文件，照常生效）；系统环境变量 `DLSS5_*` 比三个文件都优先。两处行为变化：add-on 里环境变量现在压过文件（以前是文件压过环境变量）；RE9 runtime 同一文件里同一项写两次，现在取最后一行（以前取第一行）。
- `DLSS5_MULTI_PASS_SKIP_BLOCKS`（可选、有损，默认空）：只在第 2 遍起跳块。实测省得少（3 遍时 `42,43,46` 约 0.5 ms），跳得多的组合会把风格变成另一种，而不是便宜版的 3 遍。不推荐。
- **提速（逐位）**：C32、C64 窗口核改用 LLVM 23 编（C32 另加一个调度选项），C512 一个核调优；离线每帧 900 约 −0.11～−0.14 ms、1080 约 −0.18～−0.19 ms。1080 档单独用一份带硬件向零舍入转换的 C32（1080 −0.03～−0.05 ms，其它档不变）。输出信号后加一次非阻塞队列查询（−0.01～−0.12 ms）；RE9 runtime 输入直写（流水线宿主 −0.03～−0.08 ms）。
- RE9 runtime：`DLSS5_STYLE`、`DLSS5_FRAME_STATS`、`DLSS5_MULTI_PASS`、`DLSS5_MULTI_PASS_SKIP_BLOCKS`、`DLSS5_FAST_NUMERIC`、`DLSS5_NETWORK_FREE_RES` 都从配置文件读。给集成方的 `GetTimings`，含 TheAutomatic 查出的计时塌缩修正（PDL 开关都适用）。
- 新可选项（默认关）：`DLSS5_NETWORK_FREE_RES=1`（网络按输入原尺寸跑，1080p 以上更接近 NVIDIA，1440p / 4K 慢 1.7 / 3.6 倍）。
- **包内**：`default-config.txt` + `custom-config.template.txt`；不再带 `custom-config.txt` 和 `native-game-flags.txt`，解压覆盖旧版不会冲掉你的设置。HIP 模块 68 个（62 + 1080 档 C32 + fast 数值版，双架构）。包内说明已更新（默认值、三层配置、叠层、F9、有损项）。

细节：

- 新增可选项 `DLSS5_MULTI_PASS_SKIP_BLOCKS`（叠层减负；默认空，三个模板都是空；add-on 和 RE9 runtime 都认，已进白名单）：只在 `DLSS5_MULTI_PASS` 的第 2 遍及以后跳过的块，写法同 `DLSS5_SKIP_BLOCKS`；第 1 遍永远跑配置好的全网。**写了就是有损**。3 遍、当前默认配置、离线整网：空 21.1 / 29.8 ms（900 / 1080）；`42,43,46` 快 0.4 / 0.5 ms，43.9～46.0 dB；ViT + C512 上行段（`31`～`38`、`40`～`47`）18.4 / 25.7 ms，34.1～35.0 dB；再加 C512 下行段（`23`～`38`、`40`～`47`）17.4 / 24.3 ms，33.2～33.6 dB。这些 dB 是对我们自己的 3 遍全网输出，不是对 NVIDIA（3 遍对 1 遍是 35.0 dB，所以大一点的组观感变化和多跑的遍数差不多大）。一遍的耗时主要在全分辨率的块上，生产管线跳不了，所以省得有限。非法列表（解析不了，或者字节流管线跳不了的 C64/C128/C256 块）退回空并在 stderr 写一行。空值逐位不变（19 组 SAME；RE9 回放 900 6f961945… / 1080 aaa31e2d… 不变）。
- add-on：`DLSS5_MULTI_PASS` 可以在游戏运行中改。热重载（`DLSS5_HOT_RELOAD`）现在也重读它，HIP 网络在下一帧之前切换；另加热键（`DLSS5_MULTI_PASS_HOTKEY`，默认 **F9**，可写 `F1`～`F24` 或键码，`0` 关；常规和 Magpie 模板）在 1→2→3→1 之间轮换：把 `DLSS5_MULTI_PASS=N` 写进 `custom-config.txt`（有这行就替换，没有就追加，文件不存在就新建，保留 BOM 和换行风格）。`native-game-flags.txt` 里也有这个键时它会盖过 custom，所以连那一行一起改（stderr 提示）；系统环境变量压过所有文件（stderr 提示，改了也不生效）。日志在 `logs\native-game-oneshot.txt`（`event=multi_pass_hotkey`，热重载行里的 `multi_pass=N`）。不按或关掉时逐位不变。RE9 runtime 不适用（它没有热重载）。
- 配置文件由一个变三个。`DLSS5-AMD` 文件夹按 `default-config.txt`（包里的模板，升级会覆盖）→ `custom-config.txt`（用户自己的改动，安装和升级都不写）→ `native-game-flags.txt`（旧文件，含义不变，老安装照常生效）的顺序读；后一个文件覆盖前一个的同名项，没写的项沿用前面的值，文件不存在就跳过，系统环境变量里的 `DLSS5_*` 比所有文件都优先。同一文件里同一项写两次以最后一行为准。值留空（`KEY=`）也会覆盖前面的文件，意思是用内置默认。读取统一成一份实现（`src/native_config_layers.h`），常规与 Magpie add-on（环境变量加载，以及所有直接读文件的键：提示条、快照帧、预放大、适配、超分选择、显存预留、格式回退）、热重载（现在监视三个文件）和 RE9 runtime 都用它。行为变化：add-on 里系统环境变量现在压过文件（以前文件会把它覆盖掉）；RE9 runtime 里同一文件写两次的键现在取最后一行（以前取第一行）；超过 255 字节的行不再被截断。RE9 runtime 现在也从文件读 `DLSS5_FRAME_STATS`（以前只认环境变量）。打包时模板放成 `default-config.txt`，另带 `custom-config.template.txt`，不再带 `native-game-flags.txt` 和 `custom-config.txt`，解压覆盖旧安装不会动用户的文件。文件不变时输出不变（19 组 SAME，RE9 900/1080 SAME）。*集成方注意：* 自己附带 `native-game-flags.txt` 的宿主照常工作；三个文件任意一个存在，该文件夹就算 add-on 的配置文件夹。
- 新开关 `DLSS5_MULTI_PASS`（叠层；默认 `1`，三个模板写 `1`；常规 add-on、Magpie add-on、RE9 runtime 共用）：`2`/`3` = 整个网络每帧跑 2/3 遍，上一遍的最终 RGB（alpha 1）当下一遍的输入，历史、seed、噪声不变，和 Magpie 0.6.8 的 DLSSNR Multi Pass 同义；风格逐遍加强。只在共享的网络层实现一次（`hip_reference_network.h` 的 `Network::EnqueueRaw`），三条路线读同一行。网络输出本来就是输入那套工作编码下截到 [0,1] 的图，所以两遍之间的"解码再编码"是恒等，直接省掉。离线整网：900 档 7.0 → 13.7～13.8（2 层）/ 20.4～20.6 ms（3 层），1080 档 9.8 → 19.5 / 29.0 ms；对单遍 38.4～39.1 dB（2 层）/ 34.2～34.7 dB（3 层）；跑两遍哈希相同，没有非有限值。默认 `1`：代码路径不变，输出逐位（19 组 SAME，RE9 900/1080 SAME，smoke 0），ABBA 中性。*集成方注意*：`GetTimings`/`net_gpu_ms` 报的是所有遍的总时间，不是单遍；显存多一块（`2`）或两块（`3`）RGBA f32 缓冲，大小 = 处理宽 × 高 × 16 字节（1080 档每块 35.4 MB；RE9 runtime 实测进程显存 900 档 +28/+75 MiB、1080 档 +111/+111 MiB）；N>1 时 ViT 自适应复用强制关闭；建网络时读取，改了要重建会话/网络；没有逐遍的 Style/强度。非法值退回 1 并写一行 stderr。RE9 runtime 从 flags 文件读（已进白名单）。`results/multi-pass-20261003`。
- 默认配置换成"全 71 块 + `DLSS5_FAST_NUMERIC=1`"（三个模板：`DLSS5_SKIP_BLOCKS=` 空值、`DLSS5_FAST_NUMERIC=1`）。对比以前的默认（跳 42,43,46、逐位数值）：离线 ABBA 每帧慢 900 约 0.12 ms、1080 约 0.19 ms；对 NVIDIA（1080p 单帧，Style 0）44.26 → 47.55 dB，整体偏色消失；运动画面里离逐位全块输出比跳块近约 3 dB（最差帧 52.68 对 50.69 dB）。跳块保留为可选提速项：写 `DLSS5_SKIP_BLOCKS=42,43,46` 每帧快约 0.20 ms（900）/ 0.32 ms（1080），画质掉约 3.3 dB。RE9 runtime 内置的跳块默认值也改成空：flags 文件里的 `DLSS5_SKIP_BLOCKS=` 会把环境变量删掉，旧 runtime 这时退回内置的 42,43,46，所以空值在那里关不掉跳块。*集成方注意*：默认输出不再逐位（以前因为跳块，本来也和 NVIDIA 原版不一致）；要逐位输出把 `DLSS5_FAST_NUMERIC` 写 0。自带 flags 文件或不带 flags 文件的宿主：RE9 runtime 没有 `DLSS5_SKIP_BLOCKS` 行时现在跑全 71 块（比旧行为慢约 0.20/0.32 ms；要保持旧行为写 `42,43,46`）；`DLSS5_FAST_NUMERIC` 源码默认仍是 0，要拿到新默认得在文件（或环境变量）里写这一行。add-on 和 RE9 runtime 里，`DLSS5_SKIP_BLOCKS` 空值和不写现在含义相同。`results/default-swap-20261003`、`results/default-c-20261003`。
- 新开关 `DLSS5_FAST_NUMERIC`（默认 `0`，模板现已写 `1`，见上面的默认变化；add-on 与 RE9 runtime 共用）：`1` = 各档都改装有损的快速数值版 C32 和 C64/C128 窗口核（`c32-wave1-fast.hsaco`、`c64-wave2-fast.hsaco`：激活和归一化用 f32，不做半精度舍入；softmax 的 1/sum 只用 `rcp`）。离线 ABBA 900 −0.07～−0.13、1080 −0.09～−0.10ms；对逐位输出最差帧 51.8 dB，各序列 53.1～55.5 dB；对 NVIDIA（1080p 单帧，Style 0）发布跳块 44.26 → 44.23 dB，全 71 块 47.43 → 47.55 dB。默认输出逐位不变（19 组 SAME，RE9 900/1080 SAME），ABBA 中性。*集成方注意*：每个架构多两个模块文件，与其它模块放在一起（包和 `SHA256SUMS` 多四个文件）；宿主只在选项为 `1` 时打开它们，文件缺失时退回正常模块，并往 stderr 写一行。这是数值近似：误差会传过后面每一块，可能累积，和几何类开关不同；不跨帧。网络创建时读取。取代 10-01 那套换模块的 fast 档切换脚本。`results/fast-numeric-option-20261003`。
- 新开关 `DLSS5_NETWORK_FREE_RES`（默认 `0`，三个模板写 `0`；add-on 与 RE9 runtime 共用）：`1` = 网络按输入原尺寸跑，不再吸附到 720/900/1080 三档。补边照 NVIDIA 的 plan walk（每轴补到 64 的倍数，两轴都是 256 倍数时宽再加 64，补成 1088 行时取 NVIDIA 1080 抓包的 1152），输入 1:1 放左上角，补边镜像。默认输出逐位不变（19 组 SAME，RE9 900/1080 SAME），ABBA 中性。对 NVIDIA NGX 单帧（Style 0，全块）：1440p 35.9 → 46.4 dB，4K 33.3 → 48.5 dB；1920×1080 与 1600×900 输入与档位输出完全相同。*集成方注意*：耗时随处理像素增长——1440p 约为 1080 档的 1.7 倍，3440×1440 2.3 倍，4K 3.6 倍；接受范围 320×320 到 3840×2176 处理面（范围外照旧走档位与 `DLSS5_FIT_LARGE`）；优先于 `DLSS5_NETWORK_HEIGHT`/`DLSS5_NETWORK_1080_ROWS`；建网络时读取，分辨率变化会重建网络；只支持 HIP 后端。`results/free-res-20261002`。
- RE9 runtime：给集成方的网络 GPU 耗时。`LmxxfNrApi` 函数表末尾追加 `GetTimings(context, LmxxfNrTimings*)`（`struct_size`、`valid`、`network_ms`、`frame_id`），`GetStatus` 末尾追加 `net_gpu_ms=X.XX (frame N)`。在 HIP 流上用 hipEvent 量，起点在等完 D3D12 生产者之后、终点在通知 D3D12 消费者之前；读取不阻塞，所以拿到的是最近一个已完成的帧（通常是 N-1）。默认输出逐位不变。*集成方注意*：`network_ms` 只含神经网络本身，不含 D3D12 输入拷贝、编解码 pass 和交接等待，不能和夹在 RecordInputs/RecordOutputs 周围的 D3D12 时间戳比（那只量到 D3D12 那几个 pass，网络不在那个队列上跑）。ViT 自适应复用跳过 ViT 的帧会更短；直通/回退帧不计时。ABI 版本号不变：按旧头文件编的宿主传 `LMXXF_NR_API_V1_SIZE`（136）照常工作；新宿主遇到旧 runtime 时完整大小会得到 `LMXXF_NR_INVALID_ARGUMENT`，应改用 `LMXXF_NR_API_V1_SIZE` 重试（没有 GetTimings）。计时从第一次调用 `GetTimings` 开始（这次调用返回 `valid=0`）；从会话开始每帧都记 event 实测慢 0.01～0.10ms，所以不调用的会话零开销，`GetStatus` 显示 `net_gpu_ms=off`。偶有单帧明显偏短（(重)建后的第一帧、其它帧约 1/1200），显示时请取几帧中位数。请在驱动帧的线程里调用。修复：部分 Windows HIP 环境下读数会连续多帧塌成约 0.001ms（PDL 开关都会）；桥接现在在记录结束 event 后、发输出信号前立即对它做一次非阻塞 `hipEventQuery`（不等待、不加 GPU 依赖）。问题由 TheAutomatic 定位、隔离并验证，致谢。普通 add-on 共用同一份桥接代码但不开计时（诊断用 `DLSS5_NET_TIMING=1` 打开）。`results/net-timing-20261002`。
- HIP 桥接（add-on 与 RE9 runtime 共用）：HIP 网络发完输出信号后立刻做一次非阻塞 `hipStreamQuery`，让 Windows HIP 把整批（网络 + 信号）马上提交。逐位不变（19 组 SAME）；离线 ABBA 六轮全快，900 −0.01～−0.10、1080 −0.05～−0.12ms，p99 两档都更好。新开关 `DLSS5_HIP_POST_SIGNAL_QUERY` 默认 1，模板不写；设 0 关闭。*集成方注意*：不改 ABI 和调用顺序。`results/outside-net-20261002`。
- RE9 runtime：输入直写。RGB 输入 pass 直接写进 HIP 桥接的共享输入缓冲，`RecordInputs` 不再录那次 35MB（1080 档）的 `CopyBufferRegion`；与 add-on 自 09-28 起的 `DLSS5_DIRECT_IO` bit 1 同一开关、同一默认值（1）。`RecordInputs` 的 GPU 段 1080 档 0.22→0.08ms、900 档 0.10→0.06ms；流水线宿主（不逐帧等 GPU）整帧间隔六轮全快 900 −0.03～−0.04、1080 −0.07～−0.08ms；逐帧等待的串行宿主中性（那里 HIP 开始受 `EnqueueHip` 的 CPU 发核约 0.3ms 限制）。720/900/1080 与 1600×900、1920×1080 输出逐位不变，smoke 0。*集成方注意*：ABI、调用顺序、你录的列表都不用改，`RecordInputs` 只是少录一条拷贝和两个 barrier；`DLSS5_DIRECT_IO=0`（环境变量或 flags 文件）回到旧行为。另附调用顺序建议 `include/LmxxfNrApi-call-order.zh-CN.md`。`results/outside-net-20261002`。
- RE9 runtime：`DLSS5_STYLE` 现在和其它网络键一样从 flags 文件读取（0.39 的 runtime 不读模板里这一行，只认系统环境变量）。默认 `1` 逐位不变；文件里写 `0` 与环境变量 `0` 输出相同。*集成方注意*：只在创建会话时读一次，不热重载；同名环境变量优先；只换 RE9 runtime 文件，其它不动。`results/night-20261001`。

## 0.41（10-05）

下载（三个完整包）：[夸克](https://pan.quark.cn/s/dbda3e470f8f) · [Gofile 镜像](https://gofile.io/d/YAENU0ex)。

默认仍为 **1x**（`DLSS5_MULTI_PASS=1`）。选择3x时，默认真实运行两遍并局部预测第三遍（**有损**）；`DLSS5_MULTI_PASS_PREDICT=0`恢复真实三遍。已有custom/native/环境覆盖保留，肤色保护默认0。

- **快速数值扩展**：FAST_NUMERIC=1现在也选ViT/deep快速模块，1x/2x算术也会变，不与0.40默认逐位相同。900/1080离线三轮ABBA均快，约每帧省0.09/0.10ms（两边均沿用跳42,43,46的历史测试合同）；对正常路径最差帧52.1dB、序列53.0～55.6dB。此前对NVIDIA的44.26→47.55dB只测了C32/C64版，未重测加深版。`results/fast-vit-c512-20261003`。
- **快速3x预测**：两遍真实网络后，用RGB共同局部拟合预测第三遍，不声称与NVIDIA精确相同。实际GPU输出对本项目真三遍：四个样本49.36～49.83dB、history45.28dB；history暗部/色彩可能略差。首遍回灌/预测/肤色资源在producer等待前准备，修复鬼武者首帧同步上传等待死锁。add-on预测支持热载，RE9改文件需重启。`results/multi-pass-predict-20261004`。
- **逐位数据流优化**：多遍中间post直接写RGBA供下一遍；ViT已量化contract边用FP8字节直送QKV/投影；活跃C512 FFN不再重复解码和打包相同FP8码。最后一刀C512同批单遍900/1080/1440省约0.023/0.042/0.103ms，三轮ABBA均快、合并p99改善。不同实验收益不相加，不承诺整包FPS涨幅。`results/multipass-direct-rgba-20261004`、`results/vit-byteedge-formal-20261004`、`results/c512-direct-whole-20261004`。
- **原生1440P**：真实2560×1472处理面启用已有C256整块融合，不降分辨率；该实验同批单遍墙钟17.483→17.029ms、快速3x墙钟34.409→33.442ms，不含游戏渲染/FSR/Present。自由分辨率功能本身0.40已有。`results/native-1440-optimization-20261004`。
- **接入修复**：`DLSS5_PRE_UPSCALE=auto`探测命令列表合同，不适用时退到后置；HIP Enqueue在调用线程重绑选定设备，正式采纳[XMoon的PR #15](https://github.com/lmxxf/dlss5-on-amd-9070xt-porting/pull/15)。贡献者核显+独显测试从九次InvalidHandle错误变为600多帧无错；本机单HIP设备检查不能代替双设备独立复现。
- **RE9强度文件控制**：合法`DLSS5_STRENGTH=a,b`文件/环境数值（各0～1）覆盖宿主菜单亮度/色彩；auto/空/缺省继续宿主默认。add-on/Magpie仍支持0～3。不改ABI、默认强度或网络Style。RE9文件需重启，add-on强度仍可热载。`results/strength-config-20261004`。
- **可选肤色保护**：`DLSS5_MULTI_PASS_SKIN_PROTECT=1`在肤色掩码核心保第一遍、其他区域用最终遍；只是颜色启发式，不是语义分割，暖色背景/有色灯光及history反馈有局限。用户反馈整体收益不明显，默认保持关闭。`results/skin-protect-20261004`。
- **包与文档**：每架构38模块、合计76，五行LLVM23.1.2、其余33行驱动COMGR；重编CPU宿主、刷新RE9源码包，配置说明拆中英文并链接全部注释默认文件。发行默认MP1/PRED1/SKIN0，不塞玩家custom/native；用户已上传镜像，`0.41` tag已发布；未创建GitHub Release页面。

已装开发版用户读数：剑星1x约57.6fps、快速3x约37fps；鬼武者900P快速3x约49fps，与此前一样且无异常。不是同批ABBA收益证据。gfx1200只做构建/ELF核对，真机验证仍在gfx1201。


## 未发布源码更新 — 2026-10-06

- C32 范数复用同一 lane 内相同的逆平方根。可选模块 `c32-wave1-fast-norm900.hsaco` 仅在 FAST_NUMERIC=1、处理尺寸 1600×960、单遍、graph 关闭、实验 history 关闭时使用；其他尺寸/模式保持原模块。26 个原有出口整体校验，模块缺失或出口不全就整体回落，无新用户开关。
- RX 9070 XT 原 O2、NET_TIMING=0 的 HDR 完整帧回放三轮正式测试，900 档均值改善约 0.045～0.049ms；两轮 p99 改善，第三轮在原计时器 1µs 分辨率内未分辨出退步。1152 行短筛 p99 退步，维持原路由。数字覆盖 codec/桥接/网络/decode，不作为完整游戏帧率。
- 常规 19 项兼容、受控 seed/history 与模块回落逐位一致。AE 检查数学正确性，性能测量关闭 AE。本次仅源码接入，未装机或打包；详见 Development/results/c32-norm-hoist-*。

## 未发布源码更新 — 2026-10-07～08

以下内容已进入源码和本地测试构建，已发布的0.41包保持不变；临时0.41-a历史实验不列为新的正式版本。

- **可选集成接口**：合入 [TheAutomatic 的 PR #12](https://github.com/lmxxf/dlss5-on-amd-9070xt-porting/pull/12)，提供可选编解码控制和最后一遍的辅助输出，供历史消费者使用；原编解码默认行为保留。通用辅助接口支持多遍，下面的时序消费者只支持单遍。
- **统一时序选项** `DLSS5_TEMPORAL_MODE`，默认 **0**：

  | 值 | 行为 |
  |---|---|
  | `0` | 关闭，不创建新增时序shader或历史资源 |
  | `1` | TheAutomatic Fast History：用模型门控混合上一帧输出，并把历史反馈到下一帧网络输入 |
  | `2` | 低频时域稳定：只混合输出修正量的低频部分，高频保留当前帧，不回灌网络 |

  在 `custom-config.txt` 修改后**重启游戏**。显式新选项（包括0）优先于旧 `DLSS5_FAST_HISTORY`；新键仍遵循原有文件/环境变量优先级，非法值明确拒绝。旧 `DLSS5_TEMPORAL_HISTORY_EXPERIMENT` 诊断不能与模式1/2叠加；已有 `DLSS5_FAST_TEMPORAL` 不会开启这两个新模式。
- **模式1配套与runtime支持**：常规HIP插件和RE9式独立runtime均已接入Fast History。需要原模型的64字节 `post70-history-head.f16` 及匹配的C32辅助出口，覆盖normal、RTZ、FAST和norm900变体；旧包只改开关不够，缺资产会明确报错。runtime的ABI没有已验证的运动/深度引导，因此使用静态屏幕空间历史；插件仅在资源有效、明确声明无抖动MV及深度方向时使用运动/深度，否则同样退到静态历史，并按原始颜色变化拒绝旧内容。不猜未知合同。
- **AMD模式2**：依据 [SAOG0721/Magpie](https://github.com/SAOG0721/Magpie/tree/2fceab5e241bc9f8ded001ab3266762f1f8bc51e) 描述的机制独立实现。在codec编码颜色域中，把最终修正量拆成低频与高频，低频使用约80ms历史记忆，高频保留当前帧。不多跑网络，不加强单遍力度，也不等同于模拟两遍/三遍增强；不会自动加入Oklab控制或额外增益。
- **范围与恢复**：模式1/2要求HIP、MP1、graph关闭、无overlap/外部历史；插件使用完整网络视口。时序会话关闭实际ViT复用，但不重写用户偏好。启动及F9/热载检查阻止时序开启时切到MP2/3。重置、曝光变化、长帧间隔和引导模式变化使历史失效；runtime在真实队列提交后才退休常量与资源。本次集成没有在Magpie插件上开启这两个消费者。
- **兼容修复**：恢复RGB输入缓冲首次使用的UAV→SRV状态转换，修复MinGW宽字符路径打开。模式0保持原路；新辅助模块在720/900受控runtime关闭模式检查中逐位一致，时序shader、重置、长间隔和有限输出检查已在WARP/RX 9070 XT通过。这些检查不等于游戏画质改善验收。
- **限定范围的提交优化**：未发布源码中的 `DLSS5_HIP_SUBMIT_PULSE=auto` 仅在已验证gfx1201/Runtime7驱动及兼容的单遍、history关闭配方中录制一个自有HIP事件；其他范围保持原路，`0`可关闭。不改输出算术或默认画质，不承诺游戏FPS。此前norm900优化见上一节。

两个时序模式都是改变输出的近似方案，可能拖影；不声称NGX逐位等价、普遍消除闪烁、画质提高或性能优势。本地剑星/鬼武者试装使用MP1，不改变发行默认。配置详见[中文说明](scripts/CONFIGURATION.zh-CN.md)。证据：`Development/results/pr12-integration-20261007`、`low-frequency-temporal-20261007`、`lowfreq-runtime-20261007`、`mode1-assets-20261008`。

## 未发布 — 单遍增强模式（2026-10-08）

- 新增 `DLSS5_TEMPORAL_MODE=3`：只跑一遍网络，再增强低、高频残差；低频保留时域稳定，高频使用当前帧。冷帧/重置帧也立即增强，因此不只是抑闪滤波。
- `DLSS5_TEMPORAL_ENHANCE_STRENGTH` 只作用于模式3：默认 `1`，有限数值范围 `0..2`，修改需重启。额外修正为 `s × (0.5 × 稳定低频 + 当前高频)`；强度1下，小幅且未饱和的修正低频约1.5倍、高频约2倍。强度0严格保留原单遍网络输出，连历史输出修正也旁路。
- 只对新增增量做软压缩：编码域幅度超过0.04后，按0.12尺度压缩超出部分；再用统一RGB余量比例保留修正方向。这是独立的codec编码域变体，不是参考Oklab算法，也不等于第二遍网络；无需新增学习权重或NN出口。
- 总体默认仍为**模式0**，模式0/1/2及玩家现有配置保持。WARP/RX 9070 XT数学样本与模式2回归通过，两个宿主已编译；模式3尚未部署到游戏，未做游戏观感或性能验收。更强残差可能放大假纹理或拖影，不承诺画质或FPS提高。
