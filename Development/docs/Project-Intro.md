# DLSS5@AMD 工程结构导读

仓库：<https://github.com/lmxxf/dlss5-on-amd-9070xt-porting>

这个工程做一件事：把 NVIDIA DLSS 5 的神经渲染网络（DLSSNR，藏在 `nvngx_dlssnr.dll` 里的 71 块 Swin/ViT 网络）逐块逆向出来，在 AMD RX 9070 XT（RDNA 4）上重新实现一遍。权重来自用户自己电脑上的 NVIDIA DLL，仓库里不带任何 NVIDIA 的东西。

截至 0.32，仓库一共约 5600 个文件，但真正编进发布包的只有一两百个。其余大部分是这一个月逆向和调优留下的实验、日志和证据。下面先讲发布包是怎么拼起来的，再按目录逐个讲文件。

## 一、先看整体：一帧画面怎么走

输入 → 操作 → 输出：

1. 游戏以较低的渲染分辨率画出一帧（比如 2K 质量档是 1707×961）；
2. 我们的程序在超分之前把这帧截下来，编码到网络的输入尺寸（720 / 900 / 1080 三档，按输入大小自动选）；
3. 71 块网络在显卡上跑一遍（HIP 内核，走 AMD 驱动自带的 HIP 7 运行时，不用装 SDK）；
4. 网络的输出和原画面合成，只调亮度和颜色，再交给游戏原本的超分（FSR）放大到屏幕分辨率。

同一套网络内核，有三种"接进游戏"的方式，对应三个发布包：

- **OptiScaler 常规包**：游戏调 DLSS，OptiScaler 把它换成 FSR；我们的 ReShade 插件 `dlss5-amd.addon64` 在 FSR 之前插一脚。剑星、匹诺曹的谎言走这条。
- **Magpie 包**：Magpie 抓游戏窗口，网络占据它效果组里的 FSR3_SR 一格，后面接 FSR4 放大。任何游戏都能用，但拿不到游戏的运动矢量。
- **OptiScaler-REFramework 包（RE9 专用）**：生化危机 9 的命令提交方式常规插件拆不开，所以用 TheAutomatic 改过的 OptiScaler 宿主 + 我们的 `LmxxfNrRuntime.dll`。

## 二、根目录

- `README.md` / `README.zh-CN.md`：英文 / 中文说明。当前版本、下载链接、配置、编译方法、每个版本的更新记录都在这里。
- `LICENSE`：我们自己的代码是 MIT。RE9 宿主沿用上游的 GPL-3.0。
- `.gitignore`：不进仓库的东西——第三方源码、编译产物、部署用的二进制。

## 三、`src/` —— 插件和 runtime 的主机端代码（C++）

主机端的意思是：跑在 CPU 上，负责抓帧、分配显存、排 GPU 任务。真正的矩阵计算在 `hip/` 里。

两个入口文件：

- `native_submission_order_probe.cpp`：**常规插件和 Magpie 插件的入口**，编出来就是 `dlss5-amd.addon64`。它挂进 ReShade 和 D3D12 的命令提交，找到超分调用，在它前面插入网络。名字里的 probe 是历史遗留：它最早是个"观察命令提交顺序"的探针，后来长成了正式插件。
- `LmxxfNrRuntime.cpp`：**RE9 用的独立 runtime**，编出来是 `LmxxfNrRuntime.dll`。对外只暴露一张 C 函数表（接口定义在 `include/LmxxfNrApi.h`），由 TheAutomatic 的宿主调用。

配置和几何：

- `native_hip_env_options.h`：读 `native-game-flags.txt` 里的 `DLSS5_HIP_*` 等开关。插件和 RE9 runtime 共用这一份，所以新开关加在这里两边都能生效。
- `LmxxfProductionOptions.h`：RE9 runtime 的默认生产配置（新内核、分档规则都默认开）。
- `native_network_geometry.h`：三档网络尺寸（720 档 1280×768、900 档 1600×960、1080 档 1920×1152），以及"输入不超过某档 110% 就用那档"的自动选档规则。
- `native_input_geometry.h`：检查输入尺寸是否支持。
- `native_frame_input_check.h`：检查每帧交进来的请求合不合法（格式、尺寸、资源状态）。
- `native_block_skip.h`：`DLSS5_SKIP_BLOCKS` 性能档，跳过指定的网络块（有损，默认关）。
- `native_runtime_shifts.h`：解码器第 40～69 块的窗口平移方向表，直接从原版的内核启动参数里解出来。

抓帧和合成（游戏侧）：

- `native_pre_upscale.h`：**"超分前"路线**的核心：截下超分输入、延迟提交、判断能不能安全插入（比如游戏在超分之后同一命令列表里还在画东西，就放弃插入）。
- `native_game_frame.h`：一帧的完整流程：编码 → 网络 → 解码合成。
- `native_game_codec.h`：编码 / 解码着色器的封装，把任意尺寸的输入贴到网络的档位尺寸上（边缘镜像填充），再把结果贴回去。
- `native_game_rgb_input.h`、`native_rgb_texture.h`、`native_rgb_reflect.h`：RGB 数据在纹理和缓冲之间搬运、镜像填充。
- `native_game_submission.h`：插件自己的命令列表和提交管理。
- `native_game_oneshot.h`：初始化、显存预留、设备丢失时的诊断信息（DRED）。
- `native_lab_paths.h`：各种 DXGI 格式的判断，以及资产路径查找。
- `native_device_identity.h`：确认拿到的资源和我们属于同一块显卡。
- `native_text_overlay.h`：屏幕左上角的黄字（状态、分辨率、FPS、AE/EXACT），用 5×7 点阵字体自己画。
- `native_temporal_coordinates.h`、`native_temporal_feed.h`、`native_temporal_sample.h`：时序输入（运动矢量 + 上一帧输出）。现在的"超分前"路线每帧重置网络自己的历史，时序交给 FSR 去做，所以这几个主要服务于早期的"超分后"路线。

显存和 GPU 资源：

- `native_pinned_resource.h`：创建显存常驻的资源、设置驻留优先级。
- `native_resident_table.h`：约一千张权重表批量上传，只等一次 GPU。
- `native_matrix_workspace.h`：网络中间激活的工作区。
- `native_shader_cache.h`：运行时编译的着色器落磁盘缓存，第二次启动更快。
- `native_pso.h`：计算管线创建和统计。
- `native_vram_log.h`：`DLSS5_HIP_MEMORY=1` 时打印显存明细。
- `native_hip_network.h`：HIP 版网络在插件里的外壳，负责加载模块、录入输入拷贝。
- `native_submitted_readback.h`、`native_snapshot_gate.h`、`native_network_timestamps.h`：调试用——回读 GPU 数据比对、按批抓快照、GPU 时间戳计时。

DX12 时代的网络（0.15 及以前，现在是逐位对照的"裁判链"）：

- `native_actual_network70.h`：DX12 版整网调度。
- `native_preblock_runtime.h`：网络第 0 块（pre-block）。
- `native_c32_stage.h`、`native_c32_ds.h`：C32 块和它的下采样。
- `native_c64.h`、`native_c64_shift.h`、`native_split.h`、`native_split_window.h`：C64～C256 多头注意力块，以及窗口平移（Swin 的 shift window）。
- `native_vit_block.h`、`native_vit_qkv.h`、`native_vit_attention.h`、`native_vit_linear.h`、`native_vit_gather.h`：中间 8 个全局 ViT 块（640 个 token × 1024 维）。
- `native_actual_decoder69.h`、`native_decoder_tail69.h`、`native_post70.h`：解码器和最后的 RGB 输出头。

两个独立测试程序：

- `d3d12_native_game_rgb_test.cpp`：只测 RGB 输入通路。
- `d3d12_native_network70_test.cpp`：离线测试台，喂固定帧跑整网，出 hash 和计时。

## 四、`include/`

- `LmxxfNrApi.h`：RE9 runtime 的 C 接口，由 TheAutomatic 设计，包括函数表、帧结构、错误码。ABI 版本 2；从 PR #9 起 `EnqueueHip` 带三个参数，同时保留了兼容旧宿主的两参数版本。

## 五、`hip/` —— 真正干活的 GPU 内核

每个 `.hip` 文件都用 AMD 驱动自带的编译器库 `amd_comgr_3.dll` 编成 `.hsaco` 模块，两种架构各 29 个：gfx1201 是 RX 9070 系，gfx1200 是 RX 9060 系。所有矩阵乘法都用 RDNA 4 的 WMMA 指令做 16×16×16 小块乘加，操作数是 FP8（E4M3）或 FP16，累加是 FP32。

构建工具：

- `rtc_compile.cpp`：一个 32 行的小编译器外壳，直接调系统里的 `amd_comgr_3.dll`。不用装 HIP SDK，也不需要显卡在场。
- `build-modules.ps1`：模块清单和编译配方：每个模块由哪几个源文件拼成、带哪些宏。
- `compare-modules.py`：比较两套模块。每次编译都会嵌一个随机符号，文件 hash 永远对不上，所以这个脚本只比代码段。
- `SHA256SUMS`、`HIP-API-LICENSE.txt`、`README.md`：模块校验和、HIP 头文件的许可证、目录说明。

"参考"内核（标量计算、逐位复现 NVIDIA 原版的舍入，用作裁判，不求速度）：

- `c32_reference.hip`、`prefix_reference.hip`、`multihead_reference.hip`、`deep_reference.hip`、`boundary_reference.hip`

早期的 WMMA 版本（现在是备用 / 对照）：

- `c32_wmma.hip`、`multihead_wmma.hip`、`deep_wmma.hip`、`c32_tiled.hip`、`multihead_tiled.hip`、`wave_pointwise.hip`

生产内核（快速链）：

- `prefix_fast.hip`：网络第 0 块。
- `c32_fast.hip`、`c32_fast_attention.hip`、`c32_fused_attention_packed.hip`：C32 块的前馈（FFN）和注意力，分开的版本。
- `c32_fused_ffn_attention.hip`：C32 块的融合版：一个 8×8 窗口的 FFN + 注意力 + 投影在一个 128 线程组里做完，隐层留在寄存器里。
- `boundary_fast.hip`：块与块之间的边界处理。
- `multihead_fast.hip`、`multihead_fast_padded.hip`：C64～C256 多头块的 FFN 和投影（padded 版把 900 档的行数补齐到 960）。
- `multihead_fused_attention.hip`：C64～C256 的注意力，指数留在寄存器。
- `deep_fast.hip`：C512 和中间 8 个 ViT 块的矩阵运算（FFN 用 FP8，QKV 用 FP16），也包括解码器投影。

0.31 起的"一个头一个 wave"新内核（`.inc` 文件拼到上面的源文件后面编）：

- `wave_owned_c32.inc`：一个 wave（32 个线程）包下一整个 64 token 的 C32 窗口。
- `wave_owned_mh.inc`、`wave_owned_attention_setup.inc`、`wave_owned_attention_exports.inc`：C64/C128/C256 每个 wave 包一个 32 通道的头。
- `c512_m32_mh.inc`、`c512_m32_deep.inc`：C512 一个 wave 处理 32 个 token。
- `vit_wide_deep.inc`：ViT 投影一个 wave 包 16 token × 64 列。

这些新内核都和旧版逐位一致（输出一个比特都不差），只是更快，由 `DLSS5_HIP_WAVE_OWNED`、`DLSS5_HIP_C512_M32`、`DLSS5_HIP_VIT_PROJ_N64` 三组开关控制，发布包默认全开。

## 六、`shaders/` —— D3D12 着色器（HLSL）

这些着色器不做网络计算，只负责"胶水"：搬数据、编解码、画字。它们以源码形式随包发布，游戏启动时由系统自带的 `d3dcompiler` 现场编译，根据宏组合出 44 个变体。

- `native_codec_encode.hlsl`：把游戏画面编码成网络输入（缩放到档位、镜像填充）。
- `native_codec_decode.hlsl`：把网络输出和原画面合成：网络只负责调亮度和颜色，细节还是原画面的。
- `native_game_rgb_input.hlsl`、`native_rgb_reflect.hlsl`、`native_rgb_texture.hlsl`：RGB 数据在纹理和缓冲之间的格式转换和搬运。
- `native_temporal_coordinates.hlsl`、`native_temporal_feed.hlsl`、`native_temporal_sample.hlsl`：时序通路（运动矢量换算、历史帧采样）。
- `native_history_guard.hlsl`、`native_output_smooth.hlsl`：可选的历史保护和输出平滑（默认关）。
- `native_text_overlay.hlsl`：画屏幕上的黄字。
- `native_black_probe.hlsl`：诊断用，统计输出里的黑块和异常值。
- `README.md`：目录说明。
- `dx12-network/`（53 个文件）：0.15 及以前那套纯 DX12 实现的网络（Shader Model 6.10 wave matrix）。现在不进发布包，但仍然是逐位对照的参考链，也是这次移植的历史。

## 七、`scripts/` —— 编译、配置、发包

编译：

- `build-addon-oneclick.sh`：Linux / WSL 一键交叉编译插件，自动拉 MinHook 和 ReShade 头文件。加 `--hip` 编出发布用的 `dlss5-amd.addon64`。
- `build-addon.sh`：上面那个脚本实际调用的编译命令。
- `build-runtime.sh` / `build-runtime.cmd`：编 RE9 runtime，分别用于 Linux 和 Windows（Windows 需要 MSYS2 的 g++）。
- `build-bench.sh`：编离线测试台。

配置模板（原样作为包里的 `DLSS5-AMD\native-game-flags.txt`）：

- `hip-game-flags.txt`：常规包默认配置（新内核全开，ViT 自适应复用开）。
- `hip-magpie-flags.txt`：Magpie 包默认配置。
- `hip-re9-flags.txt`：RE9 包默认配置。
- `CONFIGURATION.md`：每个配置键的说明。
- `optiscaler-regular.ini`、`re9-presr.ini`：OptiScaler 的配置覆盖项。
- `game-flags.txt`、`magpie-flags.txt`：DX12 时代的旧配置，留作历史。

包内说明和发包：

- `package-README-*.txt`、`package-README.txt`：随包的"使用说明.txt"，每种包一份。
- `package-notes/`：0.31、0.32 等各版本的更新说明。
- `package-release.py`、`release-check.sh`、`update-manifest.ps1`：生成和校验发布清单。

DX12 时代的工具：

- `bench.ps1`、`compile-shaders.ps1`、`deploy_fast.ps1`：旧 DX12 链的测速、着色器预编译和部署脚本。

## 八、`tools/`

- `lmxxf_zero_fallback_abi.c`：RE9 runtime 接口的 C 语言冒烟测试，不需要显卡，只检查函数表和标志位。
- `compare_fast_output.py`：把快速链的输出和逐位参考链比，报 PSNR 和误差。
- `flicker_stats.py`：统计连续帧之间的闪烁。

## 九、`Development/` —— 开发过程（不进发布包）

这里是这个工程的"实验室笔记本"，占了仓库九成以上的文件。

先看这三份文档：

- `DevHistory.md`：开发编年史——每一步做了什么、为什么、数据是多少，最全的记录。
- `WorkingPlan.md`：当前在做什么、下一步候选、发包规矩。
- `docs/`：说明文档，本文就在这里。

根目录下还散着约 1200 个文件，是 9 月上旬逐块逆向阶段留下的：

- `run_*`、`check_*`、`audit_*`、`analyze_*`（约 300 个 Python / PowerShell 脚本）：逐块对照 NVIDIA 原版的运行、校验、审计脚本。
- `block*-effective.bin`、`block*-live-correction.bin` 等（约 170 个 .bin）：从原版网络里抓出来的中间张量，是逐块对照的"标准答案"。
- `d3d12_*.cpp`、`native_*`（约 150 个 C++ 文件）：各块的 DX12 独立测试程序。
- `*.hlsl`：逆向过程中各版本的着色器草稿。

子目录：

- `HIP/`（约 1250 个文件）：HIP 后端开发。
  - 顶层文件：
    - `hip_d3d12_bridge.h`：D3D12 和 HIP 之间共享显存和同步栅栏的"桥"，插件和 runtime 都靠它把 D3D12 的资源交给 HIP 内核。
    - `hip_reference_network.h`：主机端整网调度，决定每个内核的启动顺序和网格大小，新内核开关也在这里分流。
    - `hip_api.h`、`hip_device_properties.h`：不装 SDK、直接动态加载 HIP 的最小 API 定义。
    - `packed_weights.h`：权重按 WMMA 片段顺序预排。
    - `*_ABI.md`：每个内核的接口说明。
    - `VIT-REUSE.md`：ViT 自适应复用的说明。
    - 另有一批 `validate-*`、`compare-*`、`benchmark-*` 校验和测速脚本。
  - `experiments/`（162 个）：每个优化尝试一个目录，一般包含 `prepare.py`（把生产源码改出对照变体）、`build.ps1`、`run.ps1`。采用了的，变成一个 `HIP_*` 宏或 `DLSS5_HIP_*` 开关；没采用的也留着，写明为什么不行。
- `results/`（约 2600 个文件、98 个目录）：每个实验的结果，每个目录一份 `README.md` 写结论，再加原始日志和 hash。
- `deployments/`（24 个）：每次往剑星 / RE9 等游戏里装候选版本的记录：装之前备份了什么、装了什么（`payload.json` 记录 hash）、回归脚本、怎么还原。二进制本身不进仓库。
- `tools/`（51 个）：日常工具：A/B 测速（ABBA 交替测，消除温度和频率漂移）、显存测量、ISA 统计、打包脚本 `package-0xx.ps1`，以及每次发包的结果 `release-0xx-results.json`。
- `RE9/`（87 个）：生化危机 9 的适配。
  - 早期做过"超分之后"路线的观察和安装脚本。
  - `presr/` 是现在的"超分前"路线：锁定的上游版本、对 TheAutomatic 宿主的补丁（`*.patch`）、准备脚本 `prepare-host.py`、源码打包 `bundle-source.py`、宿主编译 `build-host.ps1`、安装和测试脚本。
- `releases/`：0.28～0.29 的发布清单和校验记录。
- `reviews/`：几次代码审查和问题交接，比如 TheAutomatic 的 PR #5、一个 RE9 用户报的 bug。
- `history/`：早期的计划、移植日志、逆向笔记、OptiScaler 介绍。
- `ghidra/`：用 Ghidra 逆向 `nvngx_dlssnr.dll` 时写的导出脚本（导出函数、虚表、网络块构建过程）。
- `dynamic-captures/`：游戏内截图和动态抓帧对比。
- `720p/`、`900p/`：早期把网络降到 720 / 900 档计算的实验。
- `LiesOfP/`、`Onimusha/`：匹诺曹的谎言、鬼武者的安装脚本。
- `tests/`：几个小单元测试（输入几何、fit 着色器编译、编码器）。

## 十、仓库外的东西

- **权重**：`*.f16` / `*.f32` 和 `noise.f32` 不在仓库里，只随发布包分发。每个新包都从上一个完整包出发，逐文件校验后换掉变过的文件。
- **第三方**：MinHook、ReShade 头文件由编译脚本自动下载到 `third_party/`；RE9 宿主的完整源码在 TheAutomatic 的仓库，我们只存补丁，每个 RE9 包里另带一份准备好的源码 `sources\re9-presr-source.tar.gz`。

## 十一、想自己编？

README 的"编译"一节写了每个发布文件从哪来、怎么编，2026-09-26 在一台干净的 9070 机器上照着走过一遍：

- 插件：Linux / WSL 一条命令；
- HIP 模块：任何装了 AMD 驱动的 Windows，不用 SDK、不用显卡，两个架构约 6 分钟；
- RE9 宿主：Windows + VS2022 Build Tools，用包里带的源码约 1.5 分钟。
