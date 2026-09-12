# DLSS 5（DLSSNR）跑在 AMD RX 9070 XT 上

[English](README.md)

把 NVIDIA DLSS 5 的神经渲染器（`nvngx_dlssnr.dll` 里那张 71 块的 Swin/ViT 网络，"DLSSNR"）逐块逆向，用
Direct3D 12 从零重写成 Shader Model 6.10 wave-matrix（`dx::linalg`）+ FP8（E4M3）的 HLSL 计算着色器，在 AMD RDNA 4
显卡上跑起来，并通过 ReShade 插件钩住游戏的 FSR dispatch，对 1080p 画面做后处理。

**现状（2026-09-10，tag `0.08`）**：《星刃》1920×1080，RX 9070 XT，整张网络在环内 **36～37 fps**（测试台网络单独 24.4 ms/帧，显存 3.1 GB）。输出与逐位精确的参考链相比约 42 dB PSNR。贴图质量必须是「高」或更低：「非常高」时游戏加网络超过 16 GB 显存，帧率崩掉。这是研究项目不是产品：没有调节界面、一个游戏、一个分辨率。`scripts/game-flags.txt` 是本 tag 的完整运行参数，`scripts/bench.ps1` 编译与之配套的全部 shader。

## 仓库结构

| 目录 | 内容 |
|---|---|
| `src/` | 宿主代码：ReShade 插件（`native_submission_order_probe.cpp` + `native_game_*.h`）和离线测试台（`d3d12_native_network70_test.cpp`）；网络每一段一个头文件（`native_c64.h`、`native_preblock_runtime.h`、`native_vit_*.h`、`native_post70.h` 等）。 |
| `shaders/` | 快速链的 HLSL 计算核。wave-matrix 核是 `native_wave_*.hlsl`，`NATIVE_*` 宏选择快速路径。 |
| `scripts/` | `build-addon.sh`（mingw-w64 交叉编译插件）、`build-bench.sh`、`bench.ps1`（用预览版 dxc 编译快速链全部着色器并跑测试台）、`deploy_fast.ps1` / `update-manifest.ps1`（装进游戏资产目录）、`game-flags.txt`（游戏当前使用的运行时 flag）。 |
| `tools/` | `compare_fast_output.py`（对精确链算 PSNR）、`flicker_stats.py`（游戏内 dump 的帧间分析）。 |
| `Development/` | 过程中产生的一切：逆向笔记、逐块参考实现与校验脚本、快速链长出来之前的 76 层嵌套实验 runner、计划和状态日志。`DevHistory.md` 是统一整理后的开发史（唯一持续更新的一份），各时期的原始文档在 `history/`。编译用不到。 |

## 原理概要

- **网络**：pre 块（C32 @1920×1152）→ 编码器（C32 ×4、C64 ×4、C128 ×6、C256 ×8、C512 ×8）→ 8 个全局 ViT 块
  （640 token × 1024）→ 解码器（C512 → C32，带跳连）→ 第 70 块 post → RGB 头。8×8 窗口注意力，4 倍隐层 FFN，
  f16 残差流在块间量化到 E4M3。
- **核**：所有 GEMM 都是 wave-matrix 乘（A 16×32、B 32×16、f32 累加器），E4M3/f16 操作数直接从显存加载；行归约
  （归一化、softmax 分母）用对全 1 tile 的 MMA 完成；量化用硬件 `Cast<F8_E4M3FN>`。C32 注意力把两个窗口的
  QKV + 注意力 + 投影放在一个 256 线程组里做完。
- **游戏侧**：插件钩住 FSR dispatch，编码画面，用运动向量采样上一帧网络输出（时序 history），网络走延迟提交环
  （每帧 6 个命令列表），结果拷回。输出侧有一个小的时间平滑 pass（`native_output_smooth.hlsl`），压网络在抖动边缘
  周围产生的闪烁。
- **数值**：一条"精确链"逐位复现 NVIDIA 的核（每步显式 f16 舍入）；快速链放宽（f32 累加、硬件舍入），用 PSNR
  对精确链校验。

## 编译

需要：Linux 上的 `x86_64-w64-mingw32-g++`（交叉编译）；Windows + RDNA 4 显卡 + 暴露 D3D12 wave matrix（linalg
tier 10）的驱动；Shader Model 6.10 预览版 `dxc`（带 `dx/linalg.h`）；ReShade 6.8 插件头文件；MinHook 源码。

预览版组件从哪来（都链在微软那篇 [Announcing Agility SDK 1.721 preview and more Shader Model 6.10 features](https://devblogs.microsoft.com/directx/announcing-agilitysdk-721-preview-and-more-shader-model-6-10-features/)）：预览版 DXC 是 [microsoft/DirectXShaderCompiler](https://github.com/microsoft/DirectXShaderCompiler/releases) 的 *preview* 发布（我们用 v1.10.2605.24，`dxc_preview_2026_05_22.zip`，解压到任意目录作为 `-DxcRoot`）；Agility SDK 运行时（`D3D12Core.dll`，包里的 `DLSS5-D3D12-721` 文件夹）是 NuGet 包 `Microsoft.Direct3D.D3D12` 1.721.3-preview；AMD 驱动是同一篇文章里的 RC「Agility SDK」版 26.10.07.02（32.0.31007.2048），不是正式版。

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

帧率均为《星刃》1920×1080、RX 9070 XT；「测试台」是只跑网络的离线程序。0.01 之后的每个 tag 都以逐位精确的参考链为裁判（对它约 42 dB PSNR）；下面写「逐位相同」指快速链自己的输出一位都没变。

| Tag | 日期 | 做了什么 | 结果 |
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
| `0.12` | 09-12 | 屏幕提示（`native_text_overlay.h` / `.hlsl`）：上采样输出不是 1920×1080 时（2K/4K 屏 Magpie 选了适应屏幕，或游戏窗口不是 1080p）插件不再默默旁观，直接把 "DLSS5-AMD: INPUT MUST BE 1920X1080 (NOW WxH)" 写进画面；接管的 3～5 秒显示 "INITIALIZING..."，初始化失败（开发人员模式没开、驱动不对）显示 "INIT FAILED - SEE DLSS5-AMD\LOGS"。5×7 点阵字体画进自己的缓冲再拷进宿主贴图，在游戏那批命令之后用自己的命令列表提交（对宿主贴图建 UAV、或往游戏的命令列表里录命令，在 Magpie 里都会让 D3D12Core 崩）。flag 文件里 `DLSS5_NOTICE=0` 关掉。另：`DLSS5_OVERLAP`（网络放自己的计算队列、落后一帧；这张卡上 null，默认关）、`DLSS5_BUILD_C32_LDS_SLIM`（融合 C32 核少用 4KB LDS；逐位相同、无收益，默认关）。数值不变。Magpie 包 `Magpie-DLSS5-AMD-0.12.zip`（https://pan.quark.cn/s/a5bafe0e2050 ，sha256 160DBD31…） | 不变 |

## 权重

网络权重属于 NVIDIA。宿主代码运行时加载的权重文件（`block31-expand.f32`、`post70-attention.f32` 等，连同参考
dump 约 16 GB）不在仓库里，是用 `Development/` 里的脚本（`prepare_native_*_gpu.py` 和各块笔记）从自己拥有的
`nvngx_dlssnr.dll` 提取的；脚本记录了布局，但不是一条整理好的流水线。仓库不分发任何 NVIDIA 的 DLL。

## 作者

Kien——方向、游戏接入、测试。逆向、核与优化由 AI 协作者（Claude、GPT）完成，`Development/` 里的工作
笔记是它们写的。完整过程见[公众号 DLSS5 系列合集](https://mp.weixin.qq.com/mp/appmsgalbum?__biz=MzYzMzMwNzk0NA==&action=getalbum&album_id=4687269655390453762#wechat_redirect)。

## 许可

本仓库代码以 MIT 许可发布。NVIDIA 的二进制和权重不在其列。
