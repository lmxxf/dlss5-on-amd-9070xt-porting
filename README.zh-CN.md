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

```bash
bash scripts/build-addon.sh <minhook-src> <reshade-include> native-game.addon64 --tiled
bash scripts/build-bench.sh native-network70-temporal.exe
```

```powershell
# Windows 侧：<lab> 目录放着色器、权重和测试台 exe
powershell -File scripts\bench.ps1 -Folder <lab> -DxcRoot <dxc-preview>
powershell -File scripts\deploy_fast.ps1 -Source <lab> -Dll native-game.addon64 -Flags scripts\game-flags.txt
```

## 权重

网络权重属于 NVIDIA。宿主代码运行时加载的权重文件（`block31-expand.f32`、`post70-attention.f32` 等，连同参考
dump 约 16 GB）不在仓库里，是用 `Development/` 里的脚本（`prepare_native_*_gpu.py` 和各块笔记）从自己拥有的
`nvngx_dlssnr.dll` 提取的；脚本记录了布局，但不是一条整理好的流水线。仓库不分发任何 NVIDIA 的 DLL。

## 作者

Kien——方向、游戏接入、测试。逆向、核与优化由 AI 协作者（Claude、GPT）完成，`Development/` 里的工作
笔记是它们写的。完整过程见[公众号 DLSS5 系列合集](https://mp.weixin.qq.com/mp/appmsgalbum?__biz=MzYzMzMwNzk0NA==&action=getalbum&album_id=4687269655390453762#wechat_redirect)。

## 许可

本仓库代码以 MIT 许可发布。NVIDIA 的二进制和权重不在其列。
