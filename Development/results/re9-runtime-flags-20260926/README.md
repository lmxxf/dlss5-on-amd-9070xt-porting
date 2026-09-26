# RE9 runtime 可配置 + 接入 0.31 新核（2026-09-26）

起因：TheAutomatic、ouco 反馈 RE9 包无法配置优化开关——runtime 只从 native-game-flags.txt 读 DLSS5_FIT_LARGE，核选择写死在 `src/LmxxfProductionOptions.h`，RE9 用户吃不到 wave-owned / C512_M32 / VIT_PROJ_N64 / C32 vec-input。

## 改动

- **统一来源**：以仓内 `src/LmxxfNrRuntime.cpp`（PR #9 合并后）为 runtime 唯一源，构建 `bash scripts/build-runtime.sh <out>`。此前发布包 runtime（6e9974d7）来自 `Development/RE9/presr` 对上游快照的补丁路线，两份已分叉。
- **读 flags**：`LoadFlagsFileOnce`（Create 时一次，call_once）在 DLL 旁 `DLSS5-AMD\native-game-flags.txt` 及 assets 向上 4 级找文件，**白名单** `DLSS5_HIP_*`、`DLSS5_SKIP_BLOCKS`、`DLSS5_FIT_LARGE`、`DLSS5_NETWORK_HEIGHT` 放进环境（已在环境里的键不覆盖）。其他键（CODEC_SRGB、PRE_UPSCALE 等是常规 add-on 的，RE9 模板里历史遗留）不放行，避免改变 RE9 编解码行为。
- **同一解析器**：add-on `NativeHipNetwork::Create` 里 55 行 `getenv` 覆盖原样搬到 `src/native_hip_env_options.h`（`NativeApplyHipEnvironment(o,fast)`，语句顺序不变），add-on 与 runtime 共用。
- **默认值** = 0.31 常规模板：`wave_owned=c512_m32=vit_proj_n64=pdl=1`，skip 42,43,46。`RuntimeOptions` 在所需模块缺失时（按存在的 gfx1200/gfx1201 子目录逐一检查）自动关掉该组并记 `:nomodule`，NR 不因旧模块集失败。
- **状态**：`GetStatus` 追加 `wave_owned=请求/兼容 c512_m32=… vit_proj_n64=… pdl= skip=` 与 flags 文件路径/应用键数。
- **兼容 RE9 包宿主**（ABI 2，PR #9 之前的头）：GetApi 接受 ABI 1 与 2；ABI 2 调用方拿到两参数 `EnqueueHip` 包装（PR #9 把签名改成三参数，老宿主第三个寄存器是垃圾值，会被当队列指针解引用）。
- **RE9 包布局**：shader 追加 `DLL\DLSS5-AMD\native-game-tiled-assets` 等候选（原只认 TheAutomatic 的 `shaders\` 布局）；权重追加 assets 上一级（RE9 包传的是 `HIP\`）。
- **顺带修**：PR #9 删 `native_rgb_reflect.h` 里的 `native_split.h` include 后，`native_preblock_runtime.h` 失去间接的 `<cmath>`，常规 add-on 编不过；已显式 include。
- 模板 `scripts/hip-re9-flags.txt`：`NETWORK_HEIGHT` 900→auto（旧 runtime 不读该键、实际走 auto），追加四个新核键。

## 验证（RX 9070 XT，`rt_bench.cpp` 直接调 C API，RE9 宿主式 ABI 2 + 完整 FrameInfo；固定伪随机 RGBA16F 输入，24 帧取末帧 private output 哈希）

| 输入 / 档位 | 旧 runtime 6e9974d7 | 新（默认） | 新（四键=0） |
|---|---|---|---|
| 1920×1080 / 1080 | 5f58c676… | 5f58c676… | 5f58c676… |
| 1280×720 / 720 | 1041b804… | 1041b804… | 1041b804… |
| 1707×961 / 强制 1080 | 92d97ecf… | 92d97ecf… | 92d97ecf… |
| 1707×961 / 强制 900 | 08216267… | 08216267… | 08216267… |

新（默认）状态行 `wave_owned=1/1 c512_m32=1/1 vit_proj_n64=1/1 pdl=1 skip=3`、flags applied=15。

计时 ABBA（300 帧去前 1/4，含 D3D 前后处理，产品默认 auto）：1920×1080 旧 16.816/16.937 → 新 14.934/14.934，**−1.94 ms（−11.5%）**；1707×961 旧 16.856/16.949（1080 档）→ 新 10.781/10.756（900 档），**−6.13 ms（−36%）**。

显存（同进程 1920×1080 / 1280×720 / 1707×961 轮换 8 轮 = 24 次切换，DXGI 本地用量）：旧约 +180 MiB/次（1552→4567）；新（默认）约 +35 MiB/次（1755→3005）；新四键=0 约 +13～35 MiB/次。显存池生效，剩余约 35 MiB/次未解释（候选：共享栅栏信号量导入、PR #9 为安全保留到退出的资源），列为遗留。

`runtime-smoke`（单帧 RGB9E5 1506×848）通过。部署脚本在实验目录上安装→运行→回滚一轮，四个文件哈希回到原值。

## 部署包（未安装、不发包）

`Development/deployments/re9-runtime-flags-20260926/`（AMD `D:\DLSSNR-Lab\re9-runtime-flags-20260926\deploy`）：`install.ps1 -GameDir <RE9 目录>`，payload = LmxxfNrRuntime.dll 2aedb521… + c32-wave1 gfx1200 128bb82c… / gfx1201 7ac34418…；替换 runtime（含 `_storage_`）与 c32-wave1、更新 HIP\SHA256SUMS、flags 只增补（NETWORK_HEIGHT→auto，缺的四键追加）；`-RestoreBackup` 回滚。0.31 包模块齐全（29/架构）；0.30 包缺新模块时新核组自动关闭。

## 限制

- 未在 RE9 游戏内实测（本机未装 RE9）；宿主 dxgi.dll 未改，靠 ABI 2 兼容。
- add-on 侧仅搬移代码（编译通过），未重装/重测；下次合包时随 add-on 重编一起回归。
- 复现：`Development/HIP/experiments/re9-runtime-flags/`（rt_bench.cpp、stage.ps1、run.ps1、test-install.ps1）。
