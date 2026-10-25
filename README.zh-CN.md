# DLSS5@AMD RDNA4

DLSS 5（DLSSNR）跑在 AMD RX 9070 XT / RDNA 4 上。

[English](README.md)

> 🔗 **NVIDIA RTX 30 系显卡（`sm_86` 安培架构）姊妹项目：[DLSS5-RTX-3080-laptop](https://github.com/lmxxf/DLSS5-RTX-3080-laptop)** —— 将最新 `310.9.16.0` 的 31 块 DLSS 5 轻量新模型（Small `0.5x`）移植至 RTX 3060～3090 Ti 及笔记本端显卡（同时包含满血 `O4` PTX 原位二进制改写与独立 Ampere CUDA 推理引擎两条路线；RTX 3080 Laptop 实测 720p `14.1～15.8 ms`、900p `23.5～24.5 ms`、1080p `31.3～32.0 ms`，提供常规 OptiScaler、REFramework 与 Magpie 三套开箱即用发布包）。

把 NVIDIA DLSS 5 的神经渲染器（`nvngx_dlssnr.dll` 里那张 71 块的 Swin/ViT 网络，"DLSSNR"）从零重做到 AMD RDNA 4 上。网络逐块逆向后，
现在以每架构 40 个 HIP 模块的形式运行（gfx1201 = RX 9070 系，gfx1200 = RX 9060 系），跑在 AMD 驱动自带的 HIP 7 运行时上。权重是 NVIDIA 的，
从用户自己那份 DLL 里提取；本仓库不分发任何 NVIDIA 的东西。

现在每个包的流水线都是：**游戏按它的（低）渲染分辨率出一帧 → 我们的网络处理这一帧 → 宿主的超分（FSR）放大到显示分辨率。** 三个包是三种接到这个位置的方式：

| 包 | 给谁 | 怎么接 |
|---|---|---|
| **OptiScaler**（常规） | 本身支持 DLSS 的游戏（《剑星》《匹诺曹的谎言》……） | OptiScaler 用 FSR 接管游戏的 DLSS 调用；我们的 ReShade 插件（`dlss5-amd.addon64`）先在 FSR 的输入上跑网络 |
| **Magpie**（便携） | 任何游戏，不需要游戏支持超分 | Magpie 抓游戏窗口；网络接在效果组的 FSR3_SR 一项里，之后 FSR4 放大到全屏（可选 XeSS 帧生成） |
| **OptiScaler-REFramework**（只给 RE9） | 《生化危机 9》，常规路线切不开它的命令提交 | TheAutomatic 改的 OptiScaler 宿主 + 我们的 `LmxxfNrRuntime.dll`（成对使用，别和常规包混装） |

**当前版本：0.45（2026-10-24）。** 下载：[夸克](https://pan.quark.cn/s/0ccb3eff675b) · [Gofile 镜像](https://gofile.io/d/hmrcTjuS)（0.44 旧版：[夸克](https://pan.quark.cn/s/ea87c0ffd257) · [Gofile 镜像](https://gofile.io/d/YCNT57JP)）。保持 Small 0.5x 默认、MODE/AE 和 F9 语义，补齐紧凑字节路径，改进 V 片段布局与 C512 成对打包；Q/K 新布局仅在三个已验处理形状启用（**Small 0.5x 单帧耗时：1080 档 `5.487 ms`、900 档 `4.245 ms`、720 档 `2.990 ms`；Original 1x：900 档 `6.408 ms`、1088 档 `8.794 ms`**）。[完整说明与验证范围](CHANGELOG.zh-CN.md#045--2026-10-24)。

**📖 使用与调参必看：[使用与参数配置完整说明（选包、custom-config.txt 写哪、F6～F9 热键、全部参数与抄作业配方）](使用与参数配置说明.md)**（底层全量字典见 [scripts/CONFIGURATION.zh-CN.md](scripts/CONFIGURATION.zh-CN.md)）。

**历史版本：0.40（2026-10-03）。** **默认输出变了**：全 71 块 + fast 数值（`DLSS5_SKIP_BLOCKS=` 空值、`DLSS5_FAST_NUMERIC=1`），对 NVIDIA 44.26 → 47.55 dB、偏色消失，每帧慢约 0.12 / 0.19 ms（900 / 1080）；写 `DLSS5_SKIP_BLOCKS=42,43,46` + `DLSS5_FAST_NUMERIC=0` 就逐位回到 0.39。新东西：叠层 `DLSS5_MULTI_PASS=1/2/3`（风格更浓，耗时约 N 倍；游戏里 F9 轮换，《剑星》2K 57 / 37 / 27 fps）、三个配置文件（`default-config.txt` → `custom-config.txt` → `native-game-flags.txt`，环境变量最高；包里不再带你的文件）、逐位提速（C32/C64 改 LLVM 23 编，1080 约 −0.2 ms）。下载：[夸克](https://pan.quark.cn/s/d38e0f653c5a) · [Gofile 镜像](https://gofile.io/d/moSf7cqf)。0.39：[夸克](https://pan.quark.cn/s/dea9c0ef2f95) · [Gofile 镜像](https://gofile.io/d/iqtFTSpS)。

**环境要求。** RDNA 4 显卡（RX 9070 XT 实测；RX 9060 的内核随包但没机器测）和带 `amdhip64_7.dll` 的 AMD 驱动（现在的正式版驱动就带）。
不需要 HIP SDK、Agility SDK、预览版 DXC、Windows 开发人员模式。900P 下插件占显存约 1.2 GB（权重 0.6 GB、激活 0.3 GB；`DLSS5_HIP_MEMORY=1`
会把明细写进 `logs\native-hip.txt`）；显存被顶满会掉帧且不恢复，《剑星》里贴图质量开"高"或更低。

**配置。** 每个包带的 `DLSS5-AMD\default-config.txt`（0.40 以前是 `native-game-flags.txt`）就是仓库模板（[常规](scripts/hip-game-flags.txt)、[Magpie](scripts/hip-magpie-flags.txt)、
[RE9](scripts/hip-re9-flags.txt)；键的说明在 [scripts/CONFIGURATION.zh-CN.md](scripts/CONFIGURATION.zh-CN.md)）。网络档位按输入自动选（两个方向都不超过某档的 110% 就用那档：
720 到 1408×792，900 到 1760×990，其余 1080，所以 2K 质量档 1707×961 走 900；`DLSS5_NETWORK_HEIGHT` 可强制）。常规包默认开着有损的"ViT 自适应复用"（`DLSS5_VIT_ADAPTIVE=1`，
相关键见 [Development/HIP/VIT-REUSE.md](Development/HIP/VIT-REUSE.md)；F8 在它和 EXACT 之间切，要逐位输出就设 0；Magpie 和 RE9 包默认关）。
0.32 起 RE9 runtime 也从 flags 文件读 `DLSS5_HIP_*`、`DLSS5_SKIP_BLOCKS`、`DLSS5_FIT_LARGE` 等键；宿主侧选项仍在 `OptiScaler.ini` 的 `[DlssNr]`。

**三个配置文件（0.40 起）。** `DLSS5-AMD` 文件夹里可以有三个文件，按顺序读：`default-config.txt`（模板，随包发布，升级会覆盖）→
`custom-config.txt`（你自己的改动，安装和升级都不动它；包里带一份 `custom-config.template.txt` 可以照着抄）→ `native-game-flags.txt`
（旧版的单文件，已有的照常生效）。后一个文件覆盖前一个的同名项，没写的项沿用前面的值，文件不存在就跳过。系统环境变量里的 `DLSS5_*` 比三个文件都优先。
同一文件里同一项写两次，以最后一行为准。值留空（`DLSS5_SKIP_BLOCKS=`）也会覆盖前面的文件，意思是用程序内置默认。所以只想改一项，就在
`custom-config.txt` 里只写那一行，比如 `DLSS5_MULTI_PASS=2`。所有读设置的地方（常规与 Magpie add-on、热重载、RE9 runtime）用同一套合并规则。
细节见 [scripts/CONFIGURATION.zh-CN.md](scripts/CONFIGURATION.zh-CN.md)。

**Magpie 提示。** 包内效果组里的 `FSR3_SR` 就是 DLSS5 的入口（界面名字还是 FSR3）；这一项保持输入尺寸，后面的 FSR4 负责放大。
AMD 光流只在第一项开，FSR4 和 XeSS 帧生成的 Optical Flow Method 选 None。`Alt+Shift+A` 启停缩放，`F6` 在所有包里都是开关网络。

**分游戏说明（常规包）。** 自带 FSR dll 的游戏（REDengine 的《赛博朋克 2077》2.31、Katana 的《卧龙 2》）要改两处，2026-09-24 在《赛博朋克 2077》上验证：`OptiScaler.ini` 的 `[Inputs] EnableFfxInputs=false`（否则 OptiScaler 走自己的 Detours 派发 FSR，插件根本接不到调用）；`native-game-flags.txt` 里 `DLSS5_PRE_UPSCALE_ASYNC=0`（这类引擎的颜色缓冲是瞬态别名资源，延后提交时插件拷到的是那块显存当时的住户——天空探针，画面就只剩色调变化）。需要 0.30 及以后的插件（直接钩 upscaler dll，并接受这些引擎留下的只读组合资源状态）。《卧龙 2》还会在同一条命令列表里于超分之后继续绘制，常规路线目前仍拒绝。《剑星》《匹诺曹的谎言》保持默认（`ASYNC=1`）。0.30 起插件把这两处都做成自动（`DLSS5_PRE_UPSCALE_ASYNC=auto`、`DLSS5_STRENGTH=auto`，按 exe 查表）。强度表给《赛博朋克 2077》只转亮度（`1,0`）：前置路线把色调映射之前的颜色缓冲交给网络，在那里取网络的色相再过游戏的 LUT，绿色霓虹环境光会变成褐色；只转亮度细节增益不丢（对原版 FSR 高频能量 +19%，全转是 +22%，2026-09-24 实测），颜色是游戏自己的。《极限竞速：地平线 6》（Xbox 商店版，2026-09-25 实测）：装常规包能进游戏，但它和《卧龙 2》一样在同一条命令列表里超分之后继续绘制，前置路线第一帧判定不安全就放弃、之后全部透传（F6 没反应、画面不变，日志 `native-pre-upscale.txt` 里 `UNSAFE: draw/dispatch after deferred upscaler`）。改 `native-game-flags.txt` 里 `DLSS5_PRE_UPSCALE=0` 走后置路线即可：2K 质量档（渲染 1508×848）约 44fps，F6 有效；黄字帧率不显示。下个版本改为自动探测。

**来路。** 0.15 及之前，网络是 Direct3D 12 Shader Model 6.10 wave-matrix 着色器（现在放在 `shaders/dx12-network/`，仍是逐位参考链）。
0.20 把推理搬到 HIP，输出逐位相同、耗时少约 8%；0.22 把 900 档补到 960 行；0.24 引入渲染分辨率上的前置路径；0.26.1～0.28.1 和 TheAutomatic
一起做出 RE9 的宿主/runtime 路线。每一版的细节在更新记录里。

**有损画质换速度（可选配置）。** 下面几项都是运行时开关，写进 `custom-config.txt`（RE9 runtime 也认）就生效，不用换模块。全部打开就是"快速模式"。**现在的默认是"全 71 块 + fast 数值"**（`DLSS5_SKIP_BLOCKS=` 空值 + `DLSS5_FAST_NUMERIC=1`）：比以前的默认（跳 42,43,46、逐位数值）每帧慢 900 约 0.12 ms、1080 约 0.19 ms，对 NVIDIA 原版从 44.26 dB 升到 47.55 dB，偏色消失，运动画面里的偏差也比跳块小约 3 dB。跳块变成可选的提速项（下表第一行）。默认配置仍不和 NVIDIA 原版逐位一致：fast 数值有损，常规包还开着 ViT 复用；想要逐位输出，把 `DLSS5_FAST_NUMERIC` 写 0、ViT 复用关掉。

| 配置 | 作用 | 收益 | 画质代价 | 因果是否确定 |
|---|---|---|---|---|
| `DLSS5_SKIP_BLOCKS=42,43,46`（默认空值 = 全 71 块；以前的默认，现在是可选提速项） | 跳过 ViT 段的 42、43、46 三块 | 每帧 900 约快 0.20 ms、1080 约快 0.32 ms | 对 NVIDIA 原版约掉 3.3 dB（逐位数值下 47.43→44.26 dB），并有约 +0.15/+0.23（1/255）的整体偏移（1080p 单帧，Style 0） | 确定：结构性改动，每帧独立，不跨帧累积 |
| `DLSS5_NETWORK_1080_ROWS=1088` | 1080 档从 1152 行（上下镜像补 72 行，NVIDIA 原版几何）改成 1088 行（只补 8 行，Daniel 和 mochizuki 用的几何） | 1080 档每帧约快 0.45 ms（计算量少 5.6%）；720/900 档不受影响 | 对 1152 行全帧约 54～56 dB；对 NVIDIA 原版从 47.4 dB 降到 45.8 dB（单帧，Style 0，全 71 块） | 确定：几何改动，算术不变，不跨帧累积 |
| `DLSS5_NETWORK_HEIGHT=900`（默认 `auto`，按输入高度选档） | 本该走 1080 档的输入也压到 900 档（1600×960）算 | 离线整网约 9.5 ms → 6.8 ms（0.39 版两档的离线耗时） | 未实测（没有 900 档对 1080 档或对 NVIDIA 的 PSNR） | 确定：几何改动，不跨帧累积；但代价未量化 |
| `DLSS5_FAST_NUMERIC=1`（三个模板默认已开；写 `0` = 逐位） | 快速数值路径：C32 和 C64/C128 窗口核的激活和归一化用 f32（省掉半精度舍入步骤），softmax 的 1/sum 只用 `rcp`；宿主改装包里的 `c32-wave1-fast` / `c64-wave2-fast` 模块 | 离线每帧 900 约快 0.07～0.13 ms、1080 约快 0.09～0.10 ms | 对逐位输出最差帧 51.8 dB，各序列均值 53.1～55.5 dB；对 NVIDIA 原版基本不变（跳块 44.26→44.23 dB，全 71 块 47.43→47.55 dB；1080p 单帧，Style 0） | 不确定：数值近似，误差逐层传播，有累积风险。每一块的舍入差会带进后面所有块，最后会在哪种画面上冒出来事先说不清；不跨帧 |
| `DLSS5_MULTI_PASS_SKIP_BLOCKS=31,32,33,34,35,36,37,38,40,41,42,43,44,45,46,47`（默认空；只在 `DLSS5_MULTI_PASS=2`/`3` 时起作用） | 叠层减负：列出的块只在第 2、3 遍跳过，第 1 遍永远跑全网。这一组是 ViT + C512 上行段；`42,43,46` 是轻量版 | 3 遍离线：900 21.1 → 18.4 ms，1080 29.8 → 25.7 ms（−13%/−14%）；`42,43,46` 只快 0.4/0.5 ms | 对**我们自己的** 3 遍全网输出（不是对 NVIDIA）：这一组 34.1～35.0 dB，`42,43,46` 43.9～46.0 dB。作参照，3 遍对 1 遍是 35.0 dB：这一组把后几遍加上的东西去掉了一大块（偏亮、对比弱），是另一种观感，不是便宜版的 3 遍 | 确定：结构性改动，每帧独立，不跨帧。省得不多，因为耗时在全分辨率的块上，那些跳不了 |
| `DLSS5_VIT_ADAPTIVE=1`（常规包默认开，Magpie/RE9 包默认关） | ViT 自适应复用：画面变化低于阈值时跳过 ViT 重算，沿用上次真算的结果；游戏里 F8 在它（AE）和完整计算（EXACT）之间切，`=0` 永远完整计算 | 整网离线：有运动的序列 900/1080 约快 4.9%/6.3%，静止画面约 9.0%/11.3%；《剑星》站立不动 EXACT 17.9 ms，AE 16.66 ms | 静止单帧与 EXACT 逐位相同；运动场景对 EXACT/NVIDIA 的 PSNR 未实测 | 有累积风险：复用跨帧，最多沿用 `DLSS5_VIT_REUSE_PERIOD`（默认 4）帧；近似帧不回写，误差不递归叠加，重算、切换模式、几何变化或间隔超过 500 ms 时重置；阈值是启发式，不保证所有场景等价 |

**快速模式**（复制进 `custom-config.txt`；`FAST_NUMERIC=1` 已是默认，列出来只为完整）：

```
DLSS5_SKIP_BLOCKS=42,43,46
DLSS5_NETWORK_1080_ROWS=1088
DLSS5_NETWORK_HEIGHT=900
DLSS5_FAST_NUMERIC=1
DLSS5_VIT_ADAPTIVE=1
DLSS5_VIT_REUSE_PERIOD=4
```

`NETWORK_HEIGHT=900` 打开后所有输入都走 900 档，`1080_ROWS` 就不起作用了；只想保住 1080 档清晰度时去掉 `NETWORK_HEIGHT` 那行（回到 `auto`）。

**叠层（风格加强，成本线性增加）。** 和 Magpie 0.6.8 的 DLSSNR Multi Pass 同一个意思：整个网络每帧跑 N 遍，上一遍的输出图当下一遍的输入，风格一遍比一遍重。做在三条路线共用的网络层里，常规 OptiScaler、Magpie、RE9 runtime 都认同一行：

```
DLSS5_MULTI_PASS=2
```

可选 `1`/`2`/`3`，默认 `1`（就是现在的单遍，输出逐位不变）；其它值退回 1 并在 stderr 写一行。网络耗时按遍数线性涨：离线整网 900 档单遍约 7.0 ms，2 层约 13.7～13.8 ms、3 层约 20.4～20.6 ms；1080 档单遍约 9.8 ms，2 层约 19.5 ms、3 层约 29.0 ms。和单遍输出比，2 层约 38.4～39.1 dB、3 层约 34.2～34.7 dB（这不是画质损失的读数，是风格加强了多少）。显存多一块（2 层）或两块（3 层）输入缓冲，1080 档每块 35.4 MB。每一遍用同一组 Style/强度（没有 Magpie 那种逐遍参数）；开叠层时 ViT 自适应复用自动关闭。细节见 `scripts/CONFIGURATION.zh-CN.md` 和 `Development/results/multi-pass-20261003`。

游戏里按 **F9** 在 1 → 2 → 3 → 1 遍之间轮换（常规 OptiScaler 和 Magpie add-on；`DLSS5_MULTI_PASS_HOTKEY` 可改键，写 `0` 关掉）。按键会把 `DLSS5_MULTI_PASS=N` 写进 `custom-config.txt`，热重载大约一秒内切过去，不用重启。`native-game-flags.txt` 里也有 `DLSS5_MULTI_PASS` 时（它会盖过 custom），那一行也一起改。RE9 runtime 没有热重载，只能改文件后重启。想让后几遍便宜点，用 `DLSS5_MULTI_PASS_SKIP_BLOCKS` 只在第 2 遍及以后跳块（见上表，有损）。

为什么只列这几项：几何和结构类的几项每一步算术都和原来相同，每帧独立，代价可以量出来；ViT 复用是跨帧的，所以单独标了风险。快速数值路径不一样，它改的是算术本身，舍入差异会一层层传过后面的块，最后会在哪个画面冒出来事先说不清楚；离线测的代价很小（对 NVIDIA 基本不变），但测试画面覆盖不了所有场景，当前默认为FAST_NUMERIC=1；设0可选择参考数值路径。其他有损做法（f16 累加、softmax 改成 half）没有做成选项。

## 仓库结构

| 目录 | 内容 |
|---|---|
| `src/` | 常规 ReShade addon、图像编解码与游戏接入；Magpie和普通OptiScaler共用这套addon源码。 |
| `hip/` | 共用HIP网络计算核、gfx1200/gfx1201编译配方；三包共用同一网络源码。 |
| `shaders/` | 现在的包运行时仍要编的 12 个 D3D12 胶水 shader（编解码、屏幕文字、RGB 搬运、时序座标、帧检查）；`shaders/dx12-network/` 是历史的 Shader Model 6.10 网络链（0.15 及之前）。见 `shaders/README.md`。 |
| `scripts/` | 常规addon编译、发布配置和包内说明；`hip-game-flags.txt`、`hip-magpie-flags.txt`为常规包默认配置，`re9-presr.ini`为新RE9宿主覆盖配置。 |
| `Development/HIP/` | HIP宿主、D3D12互操作、离线验证；`experiments/`是未必采用的实验。这里部分代码参与编译。 |
| `Development/RE9/presr/` | RE9专用OptiScaler/runtime适配：锁定的上游版本、补丁、准备/构建/安装脚本及测试。完整上游源码不在本Git里。 |
| `Development/tools/` | 完整包组装脚本，按已校验框架底包加入DLL、shader、模型与双架构内核。 |
| `Development/results/`、`Development/deployments/` | 回归/性能证据及部署清单。 |
| `Development/DevHistory.md` | 已完成工作的开发日志；方案、实验限制和待办在对应专题文档。 |
| `tools/` | 输出对照、图像统计等辅助工具。 |

**源码与完整包分开管理。** Git管理我们自己的addon/runtime/HIP代码，以及RE9宿主的适配补丁；不包含完整Magpie或普通OptiScaler框架源码，也不包含可重打全部整包所需的模型权重和框架底包。RE9宿主基于[TheAutomatic的release/1.9.0](https://github.com/TheAutomatic/dlss-5-amd-project/tree/release/1.9.0)，`upstream.json`锁定版本，`prepare-host.py`应用适配；其GPL许可与本项目MIT代码分别保留。

维护机分工：Linux工作区保存本仓库；9070的Windows端保存框架底包、模型和构建产物。当前完整发布目录是`D:\給網友打包`；RE9完整宿主源码/产物在`D:\DLSSNR-Lab\re9-presr\source`和`bin`，HIP实验产物在`D:\DLSSNR-Lab\hip-backend`。这些是维护机路径，不是用户安装时要创建的目录。新包从已校验ZIP构建，不从正在玩的游戏目录打包。

三种接入关系：**Magpie/普通OptiScaler → dlss5-amd.addon64 → 共用HIP核**；**RE9专用OptiScaler → LmxxfNrRuntime.dll → 同一套HIP核**。RE9宿主和runtime必须成对使用。

## 原理概要

- **网络**：pre 块（C32 @1920×1152）→ 编码器（C32 ×4、C64 ×4、C128 ×6、C256 ×8、C512 ×8）→ 8 个全局 ViT 块
  （640 token × 1024）→ 解码器（C512 → C32，带跳连）→ 第 70 块 post → RGB 头。8×8 窗口注意力，4 倍隐层 FFN，
  残差流在块间以 E4M3 字节传递。
- **核**（`hip/`）：所有 GEMM 都是 RDNA 4 的 WMMA 16×16×16 指令，FP8（E4M3）或 f16 操作数、f32 累加器，操作数直接从显存或 LDS 加载；
  行归约（归一化的平方和、softmax 分母）用对全 1 tile 的 WMMA 完成；量化用硬件 FP8 转换。C32 块把一个 8×8 窗口的 FFN + 注意力 + 投影
  放在一个 128 线程组里做完，隐层激活留在寄存器；C64～C256 的注意力把指数留在寄存器；权重在初始化时预排成 WMMA 片段顺序。每架构 38 个模块（0.31 起）。
- **游戏侧**：宿主把渲染分辨率的那一帧交给我们；codec（`shaders/native_codec_encode.hlsl`）把它编码到网络面上（任何尺寸的输入都适配到
  720/900/1080 档，镜像补边），网络在 D3D12↔HIP 共享缓冲上跑（用 fence 同步），decode 着色器把结果和原帧合成（网络只引导原分辨率画面的
  亮度和颜色）再交给宿主的超分。前置（渲染分辨率）路径里网络自己的时序历史每帧重置，时序工作由超分做；Magpie 路径没有游戏的运动向量。
  屏幕左上角有状态行（`DLSS5 ON 1707x961 -> FSR 2560x1440`、INITIALIZING、UNSUPPORTED）。
- **数值**：生产快速链不等于原NVIDIA逐位真值。逐位优化相对于明确的同源基线验收；FAST_NUMERIC、自适应复用和第三遍预测等有损控制分别注明。历史0.36 FMA曾改变基线，不能声称0.20以来所有更新均逐位相同。

当前源码默认`DLSS5_MULTI_PASS_PREDICT=1`：3x跑两遍真实网络再预测第三遍（有损）；显式0运行真三遍，1x/2x不变。F9只切遍数，肤色保护默认0。此策略随0.41包发布；历史0.40包保持原默认。

## 0.41

**默认1x。** 选择3x才默认两遍真实网络加预测第三遍（有损），显式PREDICT=0运行真三遍，肤色保护默认0。本地包加入首帧/设备绑定修复、RE9强度文件控制、快速数值加深及C256/ViT/C512/数据流逐位优化。详见[0.41更新日志](CHANGELOG.zh-CN.md)。三个完整包已提供[夸克](https://pan.quark.cn/s/dbda3e470f8f)和[Gofile 镜像](https://gofile.io/d/YAENU0ex)；未创建GitHub release或tag。

## 配置

全部配置说明与默认文件入口：[配置参考](scripts/CONFIGURATION.zh-CN.md)。该页复用完整中英注释模板，说明三层优先级、强度、热载及 RE9 差异。

## 编译

### 当前 HIP 源码怎么编（0.43）——每个发布文件从哪来

| 包里的文件 | 源码 | 编法 |
|---|---|---|
| `dlss5-amd.addon64`（Magpie / OptiScaler 包） | `src/native_submission_order_probe.cpp` + `src/*.h`、`Development/HIP/*.h`（桥） | Linux/WSL：`bash scripts/build-addon-oneclick.sh dlss5-amd.addon64 --hip`（自动把 MinHook 和 ReShade 6.8 头文件拉到 `third_party/`；需要 `g++-mingw-w64-x86-64`）。当前脚本固定image base并去PE时间戳；同编译器/依赖下核hash，另验行为，不承诺跨工具链hash相同 |
| `DLSS5-AMD\native-game-tiled-assets\HIP\gfx120{0,1}\*.hsaco`（40/架构，共80） | `hip/build-modules.ps1`及其源文件 | 按下方混合编译流程；六个LLVM23模块/架构，其余34个驱动COMGR；必须用`-RowOpts -PrebuiltDir`复现生产配方 |
| `shaders/*.hlsl` 那 12 个（编解码、屏幕文字、RGB 搬运、时序座标、帧检查） | `shaders/`（历史 DX12 网络链在 `shaders/dx12-network/`） | 直接以源码随包；运行时由系统 `d3dcompiler` 编（宿主按 `#define` 选 44 个变体） |
| RE9 包：`dxgi.dll`（改过的 OptiScaler 宿主）+ `LmxxfNrRuntime.dll` | TheAutomatic 的 fork `release/1.9.0` @ `8f71f73` + 我们在 `Development/RE9/presr/` 的补丁 | `python3 Development/RE9/presr/prepare-host.py`（需要锁定版本的宿主源码：`git clone https://github.com/TheAutomatic/dlss-5-amd-project /tmp/re9-upstream-bridge-review && git -C /tmp/re9-upstream-bridge-review checkout 8f71f73`；重写宿主/runtime 源码并把 `src/`、`shaders/`、`hip/` 拷进 `third_party/lmxxf/`），再 `bash Development/RE9/presr/build-runtime.sh`（MinGW，编 runtime 和冒烟测试）和 Windows 上跑 `build-host.ps1`（Visual Studio 2022 Build Tools：MSVC v143 + Windows SDK 10.0.26100；用 vswhere 自动找，或 `-MSBuild <路径>`）。不用 Linux 也行：每个 RE9 包都带着准备好的源码 `sources\re9-presr-source.tar.gz`，`powershell -File Development\RE9\presr\build-host.ps1 -Root <工作目录> -Archive <那个 tar.gz>` 就能编出宿主（`<工作目录>\bin\OptiScaler.dll`，发布时改名 `dxgi.dll`；2026-09-26 用 0.32 包里的源码实测：91 秒，大小与发布的 `dxgi.dll` 相同，hash 因 MSVC 时间戳不同）。上游 clone 放别处就设 `RE9_UPSTREAM=<目录>`——见 `Development/RE9/presr/README.md`；同一套源码打成 `sources/re9-presr-source.tar.gz` 随包（`bundle-source.py`） |
| 独立的 `LmxxfNrRuntime.dll`（接口 `include/LmxxfNrApi.h`，TheAutomatic 贡献） | `src/LmxxfNrRuntime.cpp` | Linux/WSL：`bash scripts/build-runtime.sh`；Windows：`scripts\build-runtime.cmd`（要 MSYS2 UCRT64 的 g++：`pacman -S mingw-w64-ucrt-x86_64-gcc`；用别的 g++ 就设 `LMXXF_GXX`） |
| `DLSS5-AMD\default-config.txt`及`custom-config.template.txt` | [配置参考](scripts/CONFIGURATION.zh-CN.md)链接的三宿主模板及`scripts/custom-config.txt` | `Development/tools/stage-config-layers.ps1`复制；不打包玩家custom/native文件 |
| 权重（`*.f16` / `*.f32`）、`noise.f32` | 不在仓库里（见"权重"） | 随包；新包从上一个完整包接着做 |

打包入口为 `Development/tools/package-043.ps1`：以已校验0.42完整ZIP为底包，复用已验10982cf4宿主与双架构80模块，执行包内shader、RE9 smoke和ZIP逐文件回读。配置通过`stage-config-layers.ps1`进入default-config，不升级已安装游戏。

发布前需核对双架构ELF目标和全部导出、80模块清单，以及当前同源正常19（normal/AE/roll）、相关多遍与RE9路径；数值改动开关另行标注。历史`validate-modules.ps1`及prod6脚本不覆盖所有新路径，不能当当前完整验收。

内核的活是怎么组织的（给想接着做的人）：每个优化都是 `Development/HIP/experiments/<名字>/` 下的一个实验（`prepare.py` 把生产源码改成 `_pairN` 变体或模块集，`build.ps1`、`run.ps1`，有时还有 `analyze.py`），结果写在 `Development/results/<名字>-<日期>/README.md`；采用的改动变成源码里带默认值的 `HIP_*` 开关。`Development/WorkingPlan.md` 是现在在干什么，`Development/DevHistory.md` 是干过什么、为什么。

### 当前混合模块构建

Linux/WSL需要Python3、MinGW-w64 C++、git/curl；一键add-on脚本获取MinHook/ReShade 6.8头文件，默认是历史`--tiled`，现HIP必须显式`--hip`。Windows模块编译需要PowerShell和带`amd_comgr_3.dll`的AMD驱动；无需GPU执行。CPU宿主MinGW与GPU内核LLVM/COMGR是两套编译器。

先准备公开LLVM23编译器（Linux，需要CMake、Ninja及C/C++编译器）：

```bash
git clone --depth 1 --branch llvmorg-23.1.2 https://github.com/llvm/llvm-project llvm-src-23
cmake -G Ninja -S llvm-src-23/llvm -B llvm-build-23 -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_ASSERTIONS=OFF -DLLVM_ENABLE_PROJECTS="clang;lld" -DLLVM_TARGETS_TO_BUILD=AMDGPU -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF
cmake --build llvm-build-23 --target clang lld llvm-objdump llvm-readobj llvm-dis -j 12
```

维护者的[build-llvm23.sh](Development/tools/llvm-fork/build-llvm23.sh)使用同样参数，但写死了本地工作目录；上述命令不依赖该目录。将生成的`llvm-build-23/bin`绝对路径填入下方`--bin`。

```bash
bash scripts/build-addon-oneclick.sh dlss5-amd.addon64 --hip
bash scripts/build-runtime.sh bin
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static hip/rtc_compile.cpp -o rtc_compile.exe
python3 Development/tools/llvm-fork/compile-modules.py --bin /path/to/llvm-23.1.2/bin --out /path/to/prebuilt --targets gfx1200 gfx1201 --compiler-rows llvm23 --row-opts --target-feature=-real-true16
```

`--bin`必须指向公开LLVM23.1.2的clang/lld，默认路径属于旧LLVM21实验而非23，见[编译器构建记录](Development/tools/llvm-fork/README.md)。预编译直接解析生产配方，包括专用屏障宏，不要手列删减宏。把当前源码编出的rtc_compile.exe、源码树和完整prebuilt/gfx1200、prebuilt/gfx1201目录传到Windows，再从仓库根目录运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File hip\build-modules.ps1 -Compiler .\rtc_compile.exe -OutputDir .\modules -RowOpts -PrebuiltDir C:\build\prebuilt
```

默认构建双架构。LLVM23五行：`c32-wave1`、`c32-wave1-rtz`、`c32-wave1-fast`、`c64-wave2`、`c64-wave2-fast`；其余33行用驱动COMGR（当前已验环境为LLVM21）。省略`-RowOpts`会全走COMGR，不能复现生产工具链。`-Only`仅选一个精确模块名；只选`-Targets gfx1201`时直接写OutputDir，双架构才创建子目录。安装/打包保留架构目录，使用当前hip/rtc_compile.cpp；旧辅助程序可能忽略gfx1200，发布前核ELF目标。

仅源码构建不是完整安装：还需匹配的框架/模型/noise资产、模块及SHA256SUMS和DLL旁shaders。RE9另需匹配的OptiScaler宿主（见上表）。玩家custom/native覆盖更新后的default，不要静默替换。此处只记录命令，没有新构建或发行。

### 历史：DX12 版（0.15 及之前）

**现在的包不需要下面任何东西**（0.20 起是 HIP 后端：不要预览版 DXC、不要 Agility SDK、不要开发人员模式）。留着是因为 `shaders/` 里那套 DX12 wave-matrix 实现仍是逐位参考链，也是这个移植的历史。

需要：Linux 上的 `x86_64-w64-mingw32-g++`（交叉编译）；Windows + RDNA 4 显卡 + 暴露 D3D12 wave matrix（linalg
tier 10）的驱动；Shader Model 6.10 预览版 `dxc`（带 `dx/linalg.h`）；ReShade 6.8 插件头文件；MinHook 源码。

预览版组件从哪来（都链在微软那篇 [Announcing Agility SDK 1.721 preview and more Shader Model 6.10 features](https://devblogs.microsoft.com/directx/announcing-agilitysdk-721-preview-and-more-shader-model-6-10-features/)）：预览版 DXC 是 [microsoft/DirectXShaderCompiler](https://github.com/microsoft/DirectXShaderCompiler/releases) 的 *preview* 发布（我们用 v1.10.2605.24，`dxc_preview_2026_05_22.zip`，解压到任意目录作为 `-DxcRoot`）；Agility SDK 运行时（`D3D12Core.dll`，包里的 `DLSS5-D3D12-721` 文件夹）是 NuGet 包 `Microsoft.Direct3D.D3D12` 1.721.3-preview；AMD 驱动是 RC「Agility SDK」版 26.10.07.02（32.0.31007.2048），不是正式版：[AMD 官方下载](https://drivers.amd.com/drivers/amd-software-adrenalin-edition-26.10.07.02-win11-rc7-agility-sdk.exe)。

**Windows 开发人员模式必须打开**（设置 → 系统 → 开发者选项）：插件靠 `D3D12EnableExperimentalFeatures` 打开实验性着色器模型，这个调用只在开发人员模式下成功；关着的话初始化停在第一步（`logs\native-submission-order.txt` 里 `sdk721_before_device ... experimental=` 后面不是 `00000000`）。跑发布包不需要再装别的东西：着色器是编好的，不用 DXC、HIP、任何 SDK。

```bash
# 一键（Ubuntu / WSL）：sudo apt install g++-mingw-w64-x86-64 git；自动把 MinHook 和 ReShade 头文件拉到 third_party/
bash scripts/build-addon-oneclick.sh            # 产出 native-game.addon64
# 或者手动
bash scripts/build-addon.sh <minhook源码目录> <reshade的include目录> native-game.addon64 --tiled
bash scripts/build-bench.sh native-network70-temporal.exe
```

```powershell
# 只编 shader，任意 Windows x64 机器（需要 SM 6.10 预览版 dxc 包；不需要显卡和权重）
powershell -ExecutionPolicy Bypass -File scripts\compile-shaders.ps1 -Folder D:\dlss5-shaders -DxcRoot <dxc-preview>
# Windows 侧：<lab> 目录放着色器、权重和测试台 exe
powershell -ExecutionPolicy Bypass -File scripts\bench.ps1 -Folder <lab> -DxcRoot <dxc-preview>
powershell -ExecutionPolicy Bypass -File scripts\deploy_fast.ps1 -Source <lab> -Dll native-game.addon64 -Flags scripts\game-flags.txt
```

## 更新记录

每一版的详细改动（改了什么、效果、开关、是否逐位、实验目录）见 [CHANGELOG.zh-CN.md](CHANGELOG.zh-CN.md)。

除注明 Magpie 场景的记录外，帧率为《星刃》1920×1080、RX 9070 XT；「测试台」是只跑网络的离线程序。0.01 之后的每个 tag 都以逐位精确的参考链为裁判（对它约 42 dB PSNR）；下面写「逐位相同」指快速链自己的输出一位都没变。

| 版本 | 日期 | 做了什么 | 结果 |
|---|---|---|---|
| `0.01` | 09-08 | 精确移植终点：71 块全部走 wave 矩阵核，15 帧与原版逐位一致；权重常驻显存（不再每帧过 PCIe）；块间共享 scratch（14.7 → 7.3 GB） | 测试台 186 ms，游戏约 5 fps |
| `0.02` | 09-08 | 快速链开始（精确链冻结当裁判）：FP32 硬件累加、E4M3 操作数、激活收尾和注意力去掉中间 f16 舍入；游戏里接上时序（运动向量 + 上一帧输出） | 测试台 112 ms，约 8 fps |
| `0.03` | 09-08 | 硬件 f16/E4M3 转换、QKV+归一化合核、C32 注意力两个 dispatch、ViT 打包输入、C512 直接注意力、多头块 FP8 残差流 | 测试台 62.7 ms，约 15 fps |
| `0.04` | 09-09 | 延迟提交环、命令列合批（每帧约 100 → 25 列）、C32 注意力并入 QKV、噪声前缀改 ALU 生成（去掉 200 MB 表）、ViT QKV 合核 | 游戏 GPU 32 ms，23～25 fps |
| `0.05` | 09-09 | 输出侧时间平滑（雨景闪烁）、C32 注意力一组两窗、ViT 注意力走 FP8 | 24～25 fps |
| `0.06` | 09-09 | 仓库重整（`src/ shaders/ scripts/`），`bench.ps1` 拍平 76 层 runner，每帧 GPU 探针默认关（它的 Flush 占 3 ms），pre 块输出 E4M3 | 27～28 fps |
| （0.07） | 09-09 | 只发了用户包没打 tag：跳过三块（40.7 dB）、显存 6.8 → 3.75 GB 并周期 MakeResident、C32 中间量 f16、post 块 merge 折进 FFN | 29 fps |
| `0.08` | 09-10 | C32 FFN 并进注意力序言；ViT / C512 / 解码器入口权重改非 2 的幂步长的 tile 布局；C512 FFWD 输出 E4M3 tile 直读、C512 块直接按 raster 读写并以 E4M3 流相连（去掉窗口 pack/crop 和 QKV pack）；post merge 每次 Load 四通道；解码器投影收尾连续写——全部逐位相同。《浪人崛起》走 XeSS 路径（钩 `xessD3D12Execute`）。指令级工具链（无界面 RGP 抓取、ISA 统计）。黑帧探针。贴图质量「高」或更低成为明确要求 | 测试台 24.4 ms，显存 3.1 GB，36～37 fps |
| `0.09` | 09-11 | 升采样投影 56/62/66 输出 f16 光栅、下游首块直读（逐位相同，−0.2 ms）。**Magpie 版**：插件不再限定进程（写死的 exe 白名单在别的游戏里就是 ReShade 的 1114 错误，已去掉）、也钩 FSR 4 SDK loader 的 `ffxDispatch`、接受 8 位 UNORM 贴图、运动向量按 dispatch 的 `motionVectorScale` 换算、接管帧可配（`DLSS5_SNAPSHOT_FRAME`），于是能跑在 SAOG0721 的 Magpie 实验分支的 FSR3 效果里：任何能开 1920×1080 无边框窗口的游戏都能用，不需要游戏支持 FSR/DLSS（约 30 fps；输入是 8 位 sRGB 成品图，画面比游戏内钩子版更白）。ViT expand+contract 合核试过被否（慢一倍，延迟受限） | 测试台约 24.2 ms，游戏内 36～37 fps；Magpie 约 30 fps |
| `0.10` | 09-11 | 黑块根因修掉：硬件 E4M3 转换不饱和，残差超 ±448 变 NaN，NaN token 扩散到整个 8×8 注意力窗口，rgb 头 clamp 成 0。融合 C32 块在硬件转换前夹到 ±448（`DLSS5_BUILD_C32_SAT_CAST`：FFN 输入/隐层/注意力输入/AV 输出）。参考 fixture 逐位不变；Magpie 转储帧头部 NaN 25920 → 0。文档补上 Windows 开发人员模式（`D3D12EnableExperimentalFeatures` 需要）。Magpie 包 `Magpie-DLSS5-AMD-0.10.zip` | 不变 |
| `0.11` | 09-11 | 接管时间 20～30 秒 → 约 3.5 秒：add-on 加载时后台预读权重进内存（`NativePrefetchWeights`）、约一千张常驻权重表的拷贝合成一批只等一次 GPU（`NativeResidentBatch`；第一版提前释放了拷贝目标把 GPU 挂了，现在源和目标都扣到 flush 之后）、六个运行时编译的 shader 落磁盘缓存（`shader-cache\`）、f16 权重展开 8 线程。数值不变（参考 fixture 逐位相同）。初始化计时探针（`DLSS5_VRAM_LOG=1` / `DLSS5_INIT_LOG=<file>`）。两遍 C32 softmax 试过关掉（null：那 896 字节 scratch 属于一个从不派发的 PSO，融合核本身没有溢出）。Magpie 包 `Magpie-DLSS5-AMD-0.11.zip` | 不变 |
| `0.12` | 09-12 | 屏幕提示（`native_text_overlay.h` / `.hlsl`）：上采样输出不是 1920×1080 时（2K/4K 屏 Magpie 选了适应屏幕，或游戏窗口不是 1080p）插件不再默默旁观，直接把 "DLSS5-AMD: INPUT MUST BE 1920X1080 (NOW WxH)" 写进画面；接管的 3～5 秒显示 "INITIALIZING..."，初始化失败（开发人员模式没开、驱动不对）显示 "INIT FAILED - SEE DLSS5-AMD\LOGS"。5×7 点阵字体画进自己的缓冲再拷进宿主贴图，在游戏那批命令之后用自己的命令列表提交（对宿主贴图建 UAV、或往游戏的命令列表里录命令，在 Magpie 里都会让 D3D12Core 崩）。flag 文件里 `DLSS5_NOTICE=0` 关掉。另：`DLSS5_OVERLAP`（网络放自己的计算队列、落后一帧；这张卡上 null，默认关）、`DLSS5_BUILD_C32_LDS_SLIM`（融合 C32 核少用 4KB LDS；逐位相同、无收益，默认关）。数值不变。Magpie 包 `Magpie-DLSS5-AMD-0.12.zip` | 不变 |
| `0.13` | 09-12 | `DLSS5_SHOW_FPS=1`（Magpie 包默认开）：网络自己的帧率用提示字体画在角上。钩子按游戏声明的输出状态接管（映射 ffx_api 状态位），不再只认 UAV。pre 块错误带源码行号。踩坑记录：Windows Update 会悄悄把预览驱动换成正式驱动（SM 6.10 没了，每个 PSO 都 E_INVALIDARG，屏幕写 INIT FAILED）——重装 26.10.07.02，并设 `ExcludeWUDriversInQualityUpdate=1`。Magpie 包的效果组在 FSR3_SR 后面挂了 XeSS 帧生成（ZeroMV，跨厂商）：9070 XT 上网络 28 帧、显示 55 帧，多一帧延迟，网络本身在光流旁边慢 20% 左右。数值不变。Magpie 包 `Magpie-DLSS5-AMD-0.13.zip`（sha256 9104C48D…） | 不变 |
| `0.14`（整包） | 09-12 | FPS 数字至少三秒刷新一次，不变的字条直接复用，贴字并入已有输出提交，去掉单独的同步提交。保留 XeSS FG ZeroMV 预设。整包清理备份 DLL、日志及 shader 缓存，重新生成文件校验清单。`Magpie-DLSS5-AMD-0.14.zip`，358,004,639 字节；SHA256 `14ccde3c752b40821cb9f30024579304627a2499e087e06e9aec399bfe734eed`。尚未打 0.14 tag。 | 编译通过；671 个包内文件校验通过；帧率收益未测 |
| [0.15](https://pan.quark.cn/s/1601ca8f80ae) | 09-13 | 普通窗口宽≤1920、高≤1080 即可输入，按原宽高比适配固定网络尺寸，再还原窗口尺寸交给 FSR4。便携预设：FSR3（DLSS5 入口）→ FSR4 充满屏幕 → XeSS FG ZeroMV。默认 `DLSS5_FIT_INPUT=1`，保留三秒刷新 FPS。整包 `Magpie-DLSS5-AMD-0.15.zip`。  358,010,545 字节；SHA256 `9bb7a021d09d987986f96dbd920589018606c9800dbd08e007f4b309388e5909`。| 672 个包内文件校验通过；《鬼武者》中画质2K约30帧 |
| [0.15-900P](https://pan.quark.cn/s/a5339e4c8549) | 09-14 | 固定1600×900内部计算（处理1600×1024、400个ViT位置），再经FSR4放大；仅第一项DLSS5/FSR3开启AMD光流。用户实玩：效果比720p更好、帧率较稳，1080p加插帧不稳。独立900p推理约16.71ms，不是游戏帧率。整包 `Magpie-DLSS5-AMD-0.15-900P.zip`，358,038,890字节，SHA256 `718d77941674d6e851e7babc14b40a596da52e86aec603f44f956b99ff6811d5`。 | 用户实玩900p通过；676个包内文件校验通过 |
| [0.20](https://pan.quark.cn/s/3c8b5329353c)（HIP） | 09-17 | HIP 后端：COMGR 编译的 gfx1201 内核（源码与构建配方在 `hip/`，实验在 `Development/HIP/`）、D3D12↔HIP 共享缓冲/围栏，与 DX12 链逐位一致。09-16/17 两天的核内工作（读 ISA 找病：转换函数去分支、load 连发、别名重载提出循环、残差对角片预打包、prefix 内联）把独立 900p 帧从 19.4 压到 ≈15.5 ms，比 DX12 链快 8%；游戏内 900p 52 fps。包：`DLSS5-AMD-0.20.zip`（游戏版）、`Magpie-DLSS5-AMD-0.20.zip`（Magpie 版，不含 Agility 运行时）。需要驱动自带 `amdhip64_7.dll`。|
| [0.21](https://pan.quark.cn/s/85a507a744bd)（HIP） | 09-17 | `DLSS5_NETWORK_HEIGHT=auto`：按输入窗口自动选网络档位（≤1280×720 走 720，≤1600×900 走 900，其余 1080），左上角帧率后显示档位（`1600X900`）；包内说明：显存实测 1.2GB、正式版驱动已有用户反馈可用、性能档（`DLSS5_SKIP_BLOCKS` 九块，−0.8ms、约 30dB）。内核、权重、输出与 0.20 逐位相同。实玩 900p 52～54 fps，1080p 37～38 fps。包 `DLSS5-AMD-0.21.zip`（sha256 38e15189…）、`Magpie-DLSS5-AMD-0.21.zip`（sha256 8eeee2dd…）。 |
| [0.22](https://pan.quark.cn/s/03f9995d0551)（HIP） | 09-17 | 900 档补边 1024 → 960 行（60 行反射 + 一行零 token，与 1080 档同一套做法）：内核不变，−0.9ms（6%，同批 15.8 → 14.9ms；机器空闲时约 14.4ms），900p 约 +3 帧。输出与 0.21 不同（31.5dB，两种布局都是自定义的）；`DLSS5_NETWORK_HEIGHT=900w` 回到 0.21 布局。960 行黄金值见 `validate-modules-960.ps1`。包 `DLSS5-AMD-0.22.zip`（sha256 59226956…）、`Magpie-DLSS5-AMD-0.22.zip`（sha256 8186b67d…）。 |
| 0.23 · [Magpie](https://pan.quark.cn/s/548e52cc4f49) · [OptiScaler](https://pan.quark.cn/s/8b5a402012a2)（HIP） | 09-18 | 修复带核显（AMD Radeon(TM) Graphics）或第二块显卡的机器初始化失败（画面 INIT FAILED，日志 `bridge currently requires exactly one HIP GPU`，2026-09-18 用户反馈）：桥接改为枚举 HIP 设备，选与游戏 D3D12 适配器同名的那块（取第一个匹配；两块同型号卡仍会选第一块）。单卡机器不变，内核、权重、输出与 0.22 逐位相同（《剑星》1080p 37 帧、900p 52 帧复测）。包 `DLSS5-AMD-0.23.zip`（sha256 3dde0e0a…）、`Magpie-DLSS5-AMD-0.23.zip`（sha256 7146569c…）。09-19 新增 `OptiScaler-DLSS5-AMD-0.23.zip`（sha256 9d70d28f…）：OptiScaler 0.9.4 + ReShade + 同一套 0.23 HIP 插件，《剑星》FSR 输入下已验证 FSR2.1、FSR 3.x/4 两种后端；默认 FSR 3.x/4、关闭插帧，其他游戏尚未验证。 |
| 0.24 · [OptiScaler](https://pan.quark.cn/s/1f32ffbd2e96)（HIP） | 09-19 | 前置版：游戏低分辨率颜色 → DLSS5 → OptiScaler 的 FSR 3.x/4 → 最终输出。修改我们自己的插件，OptiScaler 本体、网络权重与内核不变；采用同队列异步提交。《剑星》2560×1440、FSR质量档（输入1707×961）实玩约34～35帧。DLSS5历史暂时每帧重置，FSR时序处理保留；网络输入仍须≤1920×1080，4K输出与新版DLL的Magpie回归尚未验证。包 `OptiScaler-DLSS5-AMD-0.24.zip`（385,880,495字节，sha256 2a46adeb…）。 |
| 0.24.1 · [OptiScaler](https://pan.quark.cn/s/4f73a54d0ff9)（HIP） | 09-19 | 修复配置/资产路径查找：DLL被加载到 `_storage_` 等子目录、旁边找不到 `DLSS5-AMD` 时，继续到游戏EXE旁查找，避免误读开发目录旧配置。已在《剑星》2560×1440回归前置链，网络内核与权重不变；此修正不解决《生化危机9》同一命令列表中的前置接入限制。包 `OptiScaler-DLSS5-AMD-0.24.1.zip`（385,883,920字节，sha256 1e9ec721…）。 |
| 0.24.2 · [OptiScaler](https://pan.quark.cn/s/f74aaa5c7f9a)（HIP） | 09-19 | 修复addon在无前置任务时仍逐次绘制查配置、锁任务表的CPU开销，并减少日志计数争用；F6关闭直接旁路前置捕获/颜色复制，已捕获任务仍执行一次。《剑星》主城回归约49fps（此前约31fps），同步/异步GPU检查通过。网络内核与权重不变。包 `OptiScaler-DLSS5-AMD-0.24.2.zip`。 同日重打完整包补上FPS/状态显示开关，下载链接已更新。 |
| 0.25 · [Magpie](https://pan.quark.cn/s/09630ed99606) · [OptiScaler](https://pan.quark.cn/s/636691131c5f)（HIP） | 09-19 | **优化**：复用C32/多头注意力的指数计算、简化倒数计算；中间特征和解码输出直接用FP8字节传递，减少数据搬运；解码完整分组走快速路径。**修复/兼容**：修复900档尾部漏写和中文路径加载失败，新增9060/XT的gfx1200内核，与gfx1201自动选择。9070 XT回归通过，9060/XT待实机反馈；Magpie实测1080P只做DLSS5约37fps，与之前基本持平。Magpie、OptiScaler均提供完整包。 |
| 0.26 · [Magpie](https://pan.quark.cn/s/7ce2ca11db43) · [OptiScaler](https://pan.quark.cn/s/c880a70f0824)（HIP） | 09-19 | FFN直接读取FP8字节片段，省去入口共享缓冲暂存及两道同步；C256权重在初始化时预排成连续矩阵片段，减少分散读取和字节拼装。补齐漏打包的R11G11B10解码shader，修复《匹诺曹的谎言》降低效果品质后黑屏，用户复测恢复；增加shader编译/绑定校验。两款完整包已生成，包内文件及44种shader组合校验通过。 |
| 0.26.1 · [OptiScaler-REFramework](https://pan.quark.cn/s/624c87a6aa11)（HIP，非常规版） | 09-20 | **专门针对RE9这类特殊接入场景的非常规版本，普通游戏请用通用版；目前仅《生化9》实测。** 后置HIP兼容：R10G10B10A2/FP16转换，FSR后处理、固定900P计算，保留1080P SDR输出保护；补齐状态/分辨率/Present帧率与F7信息开关。集成REFramework、OptiScaler、ReShade、完整模型及gfx1200/gfx1201内核，沿用0.26优化。用户实玩通过。 |
| 0.27 · [Magpie](https://pan.quark.cn/s/ec3a3282aa76) · [OptiScaler](https://pan.quark.cn/s/004278159ed8) · [OptiScaler-REFramework](https://pan.quark.cn/s/010683548f68)（HIP） | 09-20 | 精确流式ViT注意力减少中间存储与重复读取，保持原计算/舍入；可选R3自适应复用增加变化检测、静止输入延长缓存和融合提交，默认关闭。三包直接使用仓库默认配置，带完整模型和双架构内核，不含INT4/剪枝。REFramework保留固定900P、最高1080P SDR后置契约。DLL重新编译，三包各44个shader变体及ZIP逐文件校验通过。 |
| 0.28 · [Magpie](https://pan.quark.cn/s/11547f398eb4) · [OptiScaler](https://pan.quark.cn/s/f7f423b0ea3a)（HIP） | 09-22 | 六项无损核优化：RGB共用读取、C128/C256零填充跳过、ViT展开/投影及解码投影固定尺寸优化。常规《剑星》实玩效果/帧率基本不变。普通版宿主不变，完整模型和双架构核随包；RE9 0.28下载已撤下，改用下方0.28.1。 |
| 0.28.1 · [OptiScaler-REFramework](https://pan.quark.cn/s/1375693a0d21)（HIP） | 09-22 | RE9专用完整包：真实输入超限时在HIP初始化前拒绝并保留原始超分；初始化失败安全回滚，改回有效尺寸可恢复，保护未退休帧。10组/12提交帧回归和用户初步实玩通过，宿主/runtime需配套更新；源码与TheAutomatic署名随包。 |
| 0.29 · [Magpie](https://pan.quark.cn/s/fe1b6af36cad) · [OptiScaler](https://pan.quark.cn/s/209e04e7acaf) · [OptiScaler-REFramework](https://pan.quark.cn/s/505d38a63a85)（HIP） · 三个包的 [Google Drive 镜像](https://drive.google.com/drive/folders/1VPsX33sLTxBG4J8kJ_IzBDlkbBTCc5Eo?usp=sharing) | 09-23 | 超过1920×1080的输入不再拒绝：缩到1080档跑网络，再按原版codec方式还原到原分辨率（`DLSS5_FIT_LARGE=1`，issue #6；《剑星》2K Native AA 44fps、RE9 Native AA实测；超宽屏未实机）。共享核自0.28以来六项逐位无损优化（栅栏作用域、C32折叠FFN、字节链+向量化输入、注意力寄存器化、in16别名、FFN尾段转置）约−7%，《剑星》900P约60fps。RE9宿主不变、runtime更新。 |
| 0.30 · 三个包（Magpie · OptiScaler · OptiScaler-REFramework，HIP）[夸克](https://pan.quark.cn/s/80a735ab9f88) · [Google Drive 镜像](https://drive.google.com/drive/folders/1pKZpLosgJXxUOZTMg_m0sbCipX9Q3WYo?usp=sharing) | 09-25 | 链上 launch 任意序 + tile 旗子（土法 programmatic dependent launch，`DLSS5_HIP_PDL=1`；逐位；900P 约 −1.6%、1080P −0.6%；`results/pdl-chain-20260925`）与 FFN 全行写（逐位，−0.6%）。《赛博朋克 2077》零配置：常规包 `OptiScaler.ini` 带 `[Inputs] EnableFfxInputs=false`，`DLSS5_PRE_UPSCALE_ASYNC=auto`（2077 同步：瞬态别名颜色缓冲），`DLSS5_STRENGTH=auto`（2077 只转亮度 1,0：在游戏 LUT 之前取网络色相会把绿色环境光变褐）。修复：切档位/分辨率后神经处理消失（FSR 上下文钉死，经 shim 销毁清不掉；重启预算只算失败）。RE9 包：同一组核，宿主/runtime 与 0.29 相同。《剑星》900P→2K 60～61、1080P→2K 47～48；2077 低画质平衡 51～52、质量 41。 |
| 0.31 · 三个包（Magpie · OptiScaler · OptiScaler-REFramework，HIP）[夸克](https://pan.quark.cn/s/e76b8611e3cc) · [Google Drive 镜像](https://drive.google.com/drive/folders/1xtBe_XhgF9eqBlrlIQMgWcEkzm0UKHIZ?usp=sharing) | 09-26 | 档位选择：输入两轴都不超过某档 110% 时往下缩进该档（2K 质量 1707×961 → 900 档，原先放大进 1080 档）。一头一 wave 核（C32/C64/C128 整块 + C256 注意力；`DLSS5_HIP_WAVE_OWNED=1`；逐位，整网约 −6%；`results/c64-wave2-20260926`、`wave-owned-*`）；C512 QKV/mix 32 token（`DLSS5_HIP_C512_M32=1`，逐位，约 −1.3%；`results/c512-ffn-20260926`）；ViT 投影 64 列（`DLSS5_HIP_VIT_PROJ_N64=1`，逐位，1080 约 −1.8%；`results/m32-sweep-20260926`）。常规包默认 `DLSS5_VIT_ADAPTIVE=1`（静止 +3 帧，运动自动失效）。每架构 38 个模块。RE9 包：宿主/runtime 与 0.30 相同，新核随包不启用。《剑星》主菜单 2K 质量 57、2K Native AA 43～44。 |
| 0.32 · 三个包（Magpie · OptiScaler · OptiScaler-REFramework，HIP）[夸克](https://pan.quark.cn/s/b805e071405c) · [Gofile 镜像](https://gofile.io/d/CZ67LYIc) | 09-26 | 显存池：HIP 导入的 D3D12 共享缓冲区驱动不归还，改为按档位复用（切 40 次 +3GB → 平台；`results/vram-leak-20260926`）。C32 宽读（逐位，−0.8/−0.9%；`results/c32-wave-phase-20260926`）。RE9 runtime 读 flags（`DLSS5_HIP_*`/`SKIP_BLOCKS`/`FIT_LARGE`/`NETWORK_HEIGHT`），默认开 0.31 新核，兼容老宿主两参数 `EnqueueHip`（`results/re9-runtime-flags-20260926`）；合入 PR #9。RE9 中画质 2K 高质量 54、原生 AA 38。 |
| 0.33 · 三个包（Magpie · OptiScaler · OptiScaler-REFramework，HIP）[夸克](https://pan.quark.cn/s/6bb64e46ab67) · [Gofile 镜像](https://gofile.io/d/8yAjJX1b) | 09-27 | FP8 打包（c32-wave1 的 `CW_PACK8`、c64-wave2 的 `W2_PACK8`）：一条 `cvt_pk` 转两个值写进片段字；逐位相同，网络 900 档 10.74→9.80ms、1080 档 15.01→13.64ms（`results/c64-block-fused-20260927`、`results/pack8-20260927`）。《剑星》1080P 原生 AA、EXACT：主菜单 49～50、常见场景 53～54。黄字显示 AE/EXACT；F7 开关屏幕文字。 |
| 0.34 · 三个包（Magpie · OptiScaler · OptiScaler-REFramework，HIP）[夸克](https://pan.quark.cn/s/4b572b0a5b81) · [Gofile 镜像](https://gofile.io/d/cfHqVzD1) | 09-27 | fmed3 clamp（`HIP_FMED3_CLAMP`、C32 `HIP_FP8_SAT_MODE 3`）与 `W2_PACK8 6`（分段 FP16_OVFL + fma(x,y,+0) 打包），逐位相同（`results/fmed3-ovfl-20260927`、`results/ovfl-census-20260927`、`results/c64-hand-asm-20260927`）；整套模块由 `hip/build-modules.ps1` 编出。PDL 计数回绕保护（`results/pdl-audit-20260927`）。RE9：宿主换队列跟随 + 看门狗（`results/onimusha-presr-20260927`），runtime 切档泄漏 35MB→0 与几何日志（`results/re9-runtime-leak-20260927`）。《剑星》1080P AA EXACT 主菜单 50～51 / 场景 54；RE9 中画质 2K 高质量 58、原生 AA 41；鬼武者 2K 质量约 60。 |
| 0.35 · 三个包（Magpie · OptiScaler · OptiScaler-REFramework，HIP）[夸克](https://pan.quark.cn/s/83e6172e6c79) · [Gofile 镜像](https://gofile.io/d/NnF4GitT) | 09-27 | C32 三轮优化（`CW_DIRECT_OUT`、`CW_RTZ_PAIR`、`CW_PACK_MODE_MASK`、`CW_PREFIX_DIRECT_OUT`、`CW_PREFIX_FULL_TILE`、finish 完整窗口；`results/c32-aco-20260927`、`c32-round2-20260927`、`c32-round3-20260927`）与 ViT 字节流（`DLSS5_HIP_VIT_STREAM`，`results/vit-bytestream-20260927`）；每架构 30 模块。相对 0.34 全部逐位相同。《剑星》1080P AA EXACT 约 56.7；RE9 中画质 2K 高质量 58～59、原生 AA 42。 |
| 0.36 · 三个包（Magpie · OptiScaler · OptiScaler-REFramework，HIP）[夸克](https://pan.quark.cn/s/e5afdaca0769) · [Gofile 镜像](https://gofile.io/d/Z1hWdjcB) | 09-28 | 算术精简（`CW_ACT_FMED3`、`W2_BOUNDED_RCP`，`results/aco-lineup-20260928`）、float FMA 激活（`results/float-fma-20260928`，**与 0.35 不逐位，新基准**）、输入直写 `DLSS5_DIRECT_IO`（`results/zero-copy-io-20260928`）、C256/C512/C64/C128/C32 块融合（`results/c256-fusion-20260928`、`c512-fusion-20260928`、`fusion-round3-20260928`）、帧时间日志 `DLSS5_FRAME_STATS`。离线相对 0.35：900 −0.79ms、1080 −1.13ms。《剑星》1080P AA EXACT 59～60，2K AA EXACT 54。 |
| 0.37 · 三个包（Magpie · OptiScaler · OptiScaler-REFramework，HIP）[夸克](https://pan.quark.cn/s/7dbfdc6425fd) · [Gofile 镜像](https://gofile.io/d/onqeAHST) | 09-29 | 与 0.36 逐位相同：C512 紧凑布局（`results/deep-layers-20260929`）、head 分组融合 + ViT attention 转置（`results/kernel-map-20260929`）、C256 跨层持久化 `DLSS5_HIP_SWIN_RUN=1`（`results/swin-persistent-20260929`）、ViT attention 新核（`results/vit-attention-20260929`）、ViT QKV 五 wave 共用权重（`results/vit-qkv-20260929`）。离线 900 约 8.5 → 8.0ms。《剑星》2K AA EXACT 55～56。 |
| 0.38 · 三个包（Magpie · OptiScaler · OptiScaler-REFramework，HIP）[夸克](https://pan.quark.cn/s/6856d875bbe9) · [Gofile 镜像](https://gofile.io/d/lzsqfUiE) | 09-30 | 默认与 0.37 逐位相同：900 去 shift_pack、C32 字节化与跳全零半块、复合量化、C256 FFN W16、C512 attention 去冗余 F、三处宽写；离线 900 约 8.0 → 7.6ms、1080 约 10.8 → 10.4ms（`results/hip-roofline-20260930`）。新开关 `DLSS5_FORMAT_FALLBACK=1`、`DLSS5_HOT_RELOAD=1`；可选有损 `DLSS5_NETWORK_1080_ROWS=1088`（默认不开）。 |
| 0.39 · 三个包（Magpie · OptiScaler · OptiScaler-REFramework，HIP）[夸克](https://pan.quark.cn/s/dea9c0ef2f95) · [Gofile 镜像](https://gofile.io/d/iqtFTSpS) | 10-01 | 默认与 0.38 逐位相同：C512 FFN 一 wave 寄存器内完成、残差初始化/权重读取外提、解码加宽、ViT QKV/attention 合并等；离线 900 约 7.27 → 6.8ms、1080 约 10.05 → 9.5ms，《剑星》2K +1.5～2 帧。新开关 `DLSS5_STYLE=1`；add-on/runtime 可复现构建。 |
| 0.40 · 三个包（Magpie · OptiScaler · OptiScaler-REFramework，HIP）[夸克](https://pan.quark.cn/s/d38e0f653c5a) · [Gofile 镜像](https://gofile.io/d/moSf7cqf) | 10-03 | **默认输出变了**：全 71 块 + `DLSS5_FAST_NUMERIC=1`（对 NVIDIA 44.26 → 47.55 dB，+0.12/+0.19 ms）。叠层 `DLSS5_MULTI_PASS` + F9；三个配置文件（`default-config` → `custom-config` → `native-game-flags`，环境变量最高）；C32/C64 改 LLVM 23 等逐位提速。 |
| 0.41 · [夸克](https://pan.quark.cn/s/dbda3e470f8f) · [Gofile 镜像](https://gofile.io/d/YAENU0ex) | 10-05 | 默认1x；选3x才默认两真实遍加预测。首帧/HIP设备修复、RE9强度、C256/ViT/C512及直接RGBA优化，76模块；[完整说明](CHANGELOG.zh-CN.md)。 |
| 0.42 · [夸克](https://pan.quark.cn/s/316704620069) · [Gofile 镜像](https://gofile.io/d/HNE20Ocp) | 10-13 | 默认MODE0/1x/SKIN0；已采纳数据流、可选时序接口及狭域分配节省（省216MiB权重+33.75MiB无history缓冲），80模块；单帧耗时 1080 档约 9.65 ms。[完整说明](CHANGELOG.zh-CN.md)。 |
| 0.43 · [夸克](https://pan.quark.cn/s/3b19c0403e3f) · [Gofile 镜像](https://gofile.io/d/hz5cDOPF) | 10-17 | W16权重访问、跨核1B E4M3字节直连（消除全部8次`vit_pack_input`）、上下采样W16 FP8与MODE2/3资源环优化；已验固定配置逐位一致，80模块；**单帧耗时 1080 档 9.12 ms（最佳单槽 8.96 ms）、900 档 6.54 ms**。[完整说明](CHANGELOG.zh-CN.md)。 |
| 0.44 · [夸克](https://pan.quark.cn/s/ea87c0ffd257) · [Gofile 镜像](https://gofile.io/d/YCNT57JP) | 10-18 | **默认改为 `310.9.16.0` 的 31 块 Small 模型（`0.5x`）**：**单帧耗时 1080 档 5.554 ms（−39.35%）、900 档 4.377 ms（−33.26%）**，常驻权重 147.6 MiB；接通 1080/900/720/free 几何、8 块 ViT AE 复用与 MODE1 时序（`0x3A23`）；F9 按 `0.5 → 1 → 2 → 3 → 0.5` 循环（含 `4764e4f1` 热修：同尺寸零 IO 切换、双模型权重常驻、RE9/鬼武者只读虚拟盘 F9 内存即时切换）。[完整说明](CHANGELOG.zh-CN.md)。 |
| 0.45 · [夸克](https://pan.quark.cn/s/0ccb3eff675b) · [Gofile 镜像](https://gofile.io/d/hmrcTjuS) | 10-24 | 紧凑字节路径（Small 900/720/free1088 各省 120MiB 活动权重与 8 次输入打包）、ViT V 片段 16B 连续布局、C512 成对 FP8 打包；Q/K 限定 Original900、Small900/1080（**Small 0.5x 单帧耗时：1080 档 5.487 ms、900 档 4.245 ms、720 档 2.990 ms**）。保持 0.5x 默认和 MODE/F9，负候选不入包。[说明](CHANGELOG.zh-CN.md)。 |


## 权重

网络权重属于 NVIDIA。宿主代码运行时加载的权重文件（`block31-expand.f32`、`post70-attention.f32` 等，连同参考
dump 约 16 GB）不在仓库里，是用 `Development/` 里的脚本（`prepare_native_*_gpu.py` 和各块笔记）从自己拥有的
`nvngx_dlssnr.dll` 提取的；脚本记录了布局，但不是一条整理好的流水线。仓库不分发任何 NVIDIA 的 DLL。

## 作者

Kien——方向、游戏接入、测试。逆向、核与优化由 AI 协作者（Claude、GPT）完成，`Development/` 里的工作
笔记是它们写的。完整过程见[公众号 DLSS5 系列合集](https://mp.weixin.qq.com/mp/appmsgalbum?__biz=MzYzMzMwNzk0NA==&action=getalbum&album_id=4687269655390453762#wechat_redirect)。

## 许可

本仓库代码以 MIT 许可发布。NVIDIA 的二进制和权重不在其列。
