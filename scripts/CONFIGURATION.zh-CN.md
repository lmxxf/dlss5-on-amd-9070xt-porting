# 配置参考

[English](CONFIGURATION.md) · [中文 README](../README.zh-CN.md)

## 全部配置的默认文件

完整选项（含兼容/历史实验键）的中英文逐项说明以以下三份版本控制模板为准；发行时复制为`DLSS5-AMD/default-config.txt`。本页集中说明常用控制、优先级和宿主差异，不再复制一份全项清单。旧DX12实验键不代表当前HIP/RE9都会读取；RE9仅接受下文白名单。

| 宿主 | 全注释默认文件 |
|---|---|
| 普通游戏OptiScaler add-on | [hip-game-flags.txt](hip-game-flags.txt) |
| Magpie add-on | [hip-magpie-flags.txt](hip-magpie-flags.txt) |
| RE9专用runtime | [hip-re9-flags.txt](hip-re9-flags.txt) |

用户在[custom模板](custom-config.txt)对应的`custom-config.txt`写覆盖项，例如`DLSS5_STRENGTH=0.7,0.3`控制亮度细节与色彩。先核native/系统环境有无更高优先级同名键。当前源码快速3x预测默认1，显式0真三遍；1x/2x不受影响，skin默认0。这些默认随0.41包发布，旧包默认没有追溯改变。

## 三层配置文件（2026-10-03 起的源码）

`DLSS5-AMD\` 下按顺序读三个文件，都可选（实现只有一份：`src/native_config_layers.h`）：

| 顺序 | 文件 | 谁写 | 安装/升级 |
|---|---|---|---|
| 1 | `default-config.txt` | 包（本目录的模板） | 升级可以覆盖 |
| 2 | `custom-config.txt` | 用户 | 永远不覆盖；包里只带 `custom-config.template.txt`（来源 `scripts/custom-config.txt`），部署脚本在文件不存在时生成 |
| 3 | `native-game-flags.txt` | 旧版单文件 | 含义不变；老用户已有的照常生效，新包不再带 |

规则：
- 后一个文件覆盖前一个的同名键；后一个没写的键沿用前面的值；文件不存在就跳过这一层。
- 系统环境变量最优先：进程环境里已有的 `DLSS5_*` 变量（第一次读配置时取快照）压过三个文件。add-on 和 RE9 runtime 往环境里写文件值时跳过这些键；热重载和直接读文件的键也按这个合并结果。
- 行的写法不变：去掉行尾 CR/LF/空格后，以 `DLSS5_` 开头、含 `=`、至少 8 个字符的行才算；键是第一个 `=` 之前的部分，值是之后的部分。`#` 注释、空行、中文说明都忽略；文件开头的 UTF-8 BOM 跳过；行长不再有 255 字节限制。
- 同一文件里同一键出现两次：**最后一行为准**（add-on 一直如此；RE9 runtime 以前取第一行）。
- 空值（`DLSS5_SKIP_BLOCKS=`）：覆盖前面的层，结果是"空"；写进环境时 `_putenv` 会删掉变量，所以等于用程序内置默认。
- RE9 runtime 只放行白名单键（`DLSS5_HIP_*`、`SKIP_BLOCKS`、`FIT_LARGE`、`NETWORK_HEIGHT`、`NETWORK_1080_ROWS`、`STYLE`、`DIRECT_IO`、`NETWORK_FREE_RES`、`FAST_NUMERIC`、`MULTI_PASS`、`MULTI_PASS_SKIP_BLOCKS`、`MULTI_PASS_SKIN_PROTECT`、`MULTI_PASS_PREDICT`、`FRAME_STATS`、`STRENGTH`），在 DLL 旁的 `DLSS5-AMD\` 或资产目录往上四层里找第一个含任一层文件的文件夹。
- 热重载（`DLSS5_HOT_RELOAD`）监视三个文件的修改时间，任一改动/新建/删除都会重读合并结果。叠层热键（`DLSS5_MULTI_PASS_HOTKEY`，默认 F9）写的是 `custom-config.txt`；`native-game-flags.txt` 里也有 `DLSS5_MULTI_PASS` 时连那一行一起改（否则会被它盖住），系统环境变量里有就改不动（stderr 提示）。
- add-on 认定配置文件夹（DLL 旁 `DLSS5-AMD\`）的条件从"有 native-game-flags.txt"改为"三个文件有任意一个"。

测试：`tools/test_config_layers.cpp`（Linux 原生或 mingw 编出 Windows 版都能跑，28 项）。

当前默认模板见上表。RE9同样读取hip-re9-flags.txt白名单配置；re9-presr.ini是OptiScaler宿主覆盖，不代替网络文件。当前发布组装入口为`Development/tools/package-040.ps1`，通过`stage-config-layers.ps1`复制default-config和custom-config.template，不带实际玩家custom/native。`-ConfigDirectory`须指向同提交模板；0.41包已完成独立打包/发行核验。注释中英两行，每行≤191字节。

## 配置项

| 配置键 | 默认值 | 说明 |
|---|---|---|
| `DLSS5_PRE_UPSCALE` | `auto`（2026-10-03 起的常规包模板）/ `0`（Magpie / RE9 模板） | 仅适用于 add-on；RE9 runtime 不读取此键，它只有一条处理路线。`1` = 前置放大路线：网络在 FSR 放大之前运行，要求游戏的放大器 dispatch 位于其命令列表末尾（《剑星》的接入合同）。`0` = 后置放大路线。`auto`（2026-10-03 起）：首个处理帧探测此合同；如果同一命令列表中，放大器 dispatch 后面还有 draw/dispatch 工作（Forza Horizon 6、Wo Long 2），add-on 会在本次会话中永久退回后置路线，并在 `logs\native-pre-upscale.txt` 记录原因，不再因致命的直通状态而失效（“装了但没效果”）；否则继续使用前置路线。判断结果在会话内保持不变；无法识别的值按 `0` 处理，与此前行为相同。`2` = 仅 FFX 的冒烟重放（诊断用途，行为未变）。探测帧仍遵守 `DLSS5_PRE_UPSCALE_ASYNC` 规则（Cyberpunk 2077 的游戏特例表继续生效）；退回后置后，前置路线已经关闭，ASYNC 不再起作用。只读取一次，修改后需重启。 |
| `DLSS5_PRE_UPSCALE_ASYNC` | `auto`（常规包模板）/ `0`（Magpie） | 前置放大路线的提交方式。`auto`：默认异步，add-on 游戏特例表中的游戏除外（Cyberpunk 2077：临时复用的颜色缓冲需要同步提交）。`1` / `0` 可强制指定。 |
| `DLSS5_STRENGTH` | `auto` | 两个数值：亮度转移/细节光照强度，以及颜色混合强度。add-on/Magpie 接受每项 0..3（1 为完整效果，>1 是诊断性外推）；RE9 接受每项 0..1。不支持用单个数值同时设置两项。add-on 的 auto：除 Cyberpunk2077 为 1,0 外，其余为 1,1。RE9 的 auto/空值/未设置保留宿主或菜单明确给定的强度，否则用 runtime 默认 1,1；配置文件或系统环境中的有效数值会覆盖宿主/菜单。RE9 遇到非法值、非有限值或尾随杂字符时退回宿主/默认值，并只报告一次。配置层仍为 default→custom→native→进程已有环境变量；native 中的 `auto` 仍会覆盖 custom，因此要在 custom 自定义时，先删除或注释旧 native 文件中的同名行。add-on 修改约一秒内生效；RE9 配置修改需重启，但没有数值覆盖时，宿主菜单仍可实时调整。`0` 仅抑制输出增强，不会停止网络计算，也不等同于 F6 的完整旁路；强度不改变 Style 或 MultiPass。 |
| `DLSS5_HIP_PDL` | `1`（0.30 模板） | C64/C128/C256 块的连续派发使用 `hipExtAnyOrderLaunch`；内核等待或发布每个 tile 的标志，模拟程序依赖派发，让下一次派发填补前一次派发的尾部空闲。逐位一致；900p 约 -1.6%，1080p 约 -0.6%。`0` 恢复普通的顺序派发。 |
| `DLSS5_HIP_VIT_STREAM` | `3`（0.35 模板） | ViT attention 将 AV 输出直接以字节形式交给 n64 投影，contract 步骤使用 half 接口（`vit-stream` 模块）；可与自适应 ViT 复用配合。逐位一致；1080p 约 -1.3%，900p 基本持平。`0` 保留此前的 f32 路径；模块缺失时退回该路径，并记录 `vit_stream:nomodule`。 |
| `DLSS5_FRAME_STATS` | `0` | 帧时间日志的统计窗口秒数，追加写入 `DLSS5-AMD\logs\frame-stats.txt`：帧数、fps、帧间隔 avg/p50/p99/max（0.25 ms 分桶，上限 100 ms）、1% low，以及网络运行 / 被旁路（F6）/ 正在初始化 / 错误 / 不支持 / 空闲的帧数。间隔是相邻放大器 dispatch 之间的时间，不是 GPU 耗时。常规 add-on 从配置文件读取；RE9 runtime 从配置文件设置的环境变量读取（2026-10-03 起列入白名单，此前只能直接设置环境变量；RE9 仅按每次 EnqueueHip 统计运行/错误）。`0` 关闭，没有开销。 |
| `DLSS5_DIRECT_IO` | `1`（add-on；RE9 runtime 自 2026-10-02 起仅支持 bit 1，从环境变量或 flags 文件读取） | HIP 网络前后的零拷贝 I/O，仅改变数据搬运，网络输入/输出字节及最终图像逐位相同。Bit 1：RGB 输入 pass 直接写入 HIP 共享缓冲，省去一次 35 MB 拷贝及一次无用的 tile 顺序拷贝。Bit 2：前置放大路线中，FSR 直接读取解码器的输出纹理，而不读取它的副本（仅限 RGBA16F 颜色；其他格式仍照旧拷贝）。History/时序会话和 `DLSS5_OVERLAP` 保留旧路径。`3` 同时开启两项，`0` 恢复原行为。 |
| `DLSS5_HIP_WAVE_OWNED` | `0`（0.30 之后的源码构建） | 为更新后的 Magpie/普通 OptiScaler add-on 显式开启单 wave 窗口内核。需要匹配的 `c32-wave1.hsaco` 和 `c64-wave2.hsaco`；兼容的生产字节流配置会启用它，不兼容的布局保留旧路径。`0` 恢复 prod8 派发，不加载额外模块。完整 runtime 回归已通过，游戏 FPS 验证及发布打包尚待完成。独立 RE9 C API runtime 不由此开关启用。 |
| `DLSS5_HIP_SWIN_RUN` | `1`（0.37 的三套模板：常规 / Magpie / RE9；源码默认 0） | 900/1080 档可选的 C256 内部阶段就绪队列（块 16–21 和 49–54），需要匹配的 `swin-persistent.hsaco`。要求单 wave 字节路径及内存池分配；graph/诊断布局保留旧路径。队列等待超过 100 ms 会中止待执行工作，由有界的 GPU 串行重放在消费者运行之前恢复该阶段；实例观察到映射的错误标志后关闭持久化。其他阶段保留原有 PDL 行为。add-on / RE9 共用选项；RE9 runtime 从 flags 文件设置的环境变量读取。逐位一致；离线 900 约 -1.9%，1080 约 -0.6%。`0` 恢复此前的派发方式。 |
| `DLSS5_HIP_INPUT_POLL` | `0`（源码默认；模板未设置） | 实验性的 GPU 端 D3D→HIP 交接：游戏队列写入标记（`WriteBufferImmediate`），HIP stream 用 `hipStreamWaitValue32` 等待，替代共享 fence。`1` = 每帧单独提交标记列表；`2` = 在输入拷贝列表内记录标记。启动自检和 200 ms 看门狗可使其退回 fence 路径。add-on / RE9 共用选项（桥接头文件）。逐位一致，但两个档位的完整帧重放都慢了 0.01–0.04 ms（results/handoff-gpu-20260930），因此保持关闭。 |
| `DLSS5_HIP_POST_SIGNAL_QUERY` | `1`（源码默认；模板未设置；add-on 和 RE9 runtime 共用，桥接头文件） | HIP 网络向 D3D12 fence 发出完成信号后，调用一次非阻塞的 `hipStreamQuery`，促使 Windows HIP 立即提交整批工作。逐位一致；add-on 重放每帧 −0.01…−0.12 ms（results/outside-net-20261002）。`0` 关闭，可通过环境变量或 flags 文件设置。 |
| `DLSS5_NETWORK_1080_ROWS` | `1152`（三套模板；源码默认 1152） | 1080 档（固定 `DLSS5_NETWORK_HEIGHT=1080`，或由 `auto` 选中）的网络处理行数。`1152` = NVIDIA 的几何合同（1080 + 72 行反射补边，ViT 的 30x18 个有效 token 放在 32x20 网格中），与此前版本逐位一致。`1088` = **紧凑、有损，区别于 NVIDIA 的几何**（1080 + 8 行反射补边，ViT 的 30x17 个 token 仍放在同一个 32x20 网格中，post shift 不变；Daniel/mochizuki 使用的几何）：更快，但整幅画面的输出都会改变，见 `Development/results/geom-1088-20260930`。`DLSS5_NETWORK_HEIGHT=1088` 选择相同几何的固定档位。add-on / RE9 runtime 共用选项。900/720 档不受影响。 |
| `DLSS5_NETWORK_FREE_RES` | `0`（三套模板；源码默认 0） | `1` = 网络按输入本身的尺寸运行，不再套用 720/900/1080 固定档：两轴补齐到 64 的倍数（两轴同时为 256 的倍数时，宽度再多补 64；补齐后为 1088 行时改为 1152，即 NVIDIA 的 1080 几何）；输入放在左上角，补边为镜像；ViT 网格由各轴 /64 后向上补齐到 4 的倍数。覆盖 `DLSS5_NETWORK_HEIGHT` / `DLSS5_NETWORK_1080_ROWS`。允许从 320×320 输入到 3840×2176 处理面，无需 `DLSS5_FIT_LARGE`；超出范围时仍使用固定档位及原有 `DLSS5_FIT_LARGE` 规则。1920×1080 和 1600×900 输入与对应固定档输出完全相同；其他尺寸输出会改变（没有缩放，因此更接近 NVIDIA：完整块、Style 0，对 NVIDIA NGX 输出的 1440p 分数为 35.9→46.4 dB，4K 为 33.3→48.5 dB）。成本随处理像素增加：1440p 为 1080 档帧时间的 1.7×，3440×1440 为 2.3×，4K 为 3.6×。仅限 HIP 后端，适用于 add-on 和 RE9 runtime，在网络创建时读取。诊断项：`DLSS5_NETWORK_FREE_PAD=WxH` 强制处理尺寸，`DLSS5_NETWORK_FREE_EXTRA=w\|h\|0` 指定 256 规则所用的轴。见 `Development/results/free-res-20261002`。 |
| `DLSS5_SKIP_BLOCKS` | 空值 = 全部 71 块（2026-10-03 起的三套模板；add-on 和 RE9 runtime 的源码默认均为空） | 用逗号分隔需要跳过的残差块编号，每个跳过的块以输入副本替代。默认运行全部块；配合 `DLSS5_FAST_NUMERIC=1`，对 NVIDIA 为 47.55 dB（1080p 单帧，Style 0）。`42,43,46` 是此前默认，保留为可选提速配置：**有损**，每帧约少 0.20 ms（900）/ 0.32 ms（1080），对 NVIDIA 约 −3.3 dB（逐位路径下 47.43 → 44.26），并伴随整体约 +0.15/+0.23（1/255）的色偏；属于每帧的结构改动，本身不跨帧携带。空值会删除环境变量（两个读取方都用 `_putenv`），因此空值与未设置含义相同；2026-10-03 以前的 RE9 runtime 内置 `42,43,46`，当时即使写空行仍会跳过这些块。add-on / RE9 runtime 共用选项，在网络创建时读取。见 `Development/results/default-swap-20261003`。 |
| `DLSS5_FAST_NUMERIC` | `1`（2026-10-03 起的三套模板；源码默认 0） | `1` = **快速数值路径，有损**：在普通模块旁加载快速数值 twin 模块（缺失 `-fast` 文件时退回普通模块，并在 stderr 记录一行；选项为 0 时不打开这些文件）。2026-10-03 起的覆盖范围：`c32-wave1-fast.hsaco` / `c64-wave2-fast.hsaco` 中的 C32 + C64/C128 wave 内核（f32 激活/归一化，省去 half 舍入步骤；softmax 的 1/sum 使用 `rcp`）；ViT attention（分母倒数仅用 rcp，出口省去 RTZ half 往返）；`vit-stream-fast.hsaco` 中的 ViT contract + projection（省去 RNE half 往返）；`deep_fast-packed-fast.hsaco` 中的 C512 FFN projection（同样处理）+ decoder。离线网络专用 bench，两边均按此前所有 fast-tier 测量一样跳过 42,43,46：900 每帧约 −0.09 ms，1080 约 −0.10 ms，两个档位每轮 ABBA 都更快；对逐位输出最差帧为 52.1 dB，每段序列为 53.0–55.6 dB（`Development/results/fast-vit-c512-20261003`）。对 NVIDIA 的 44.26 → 47.55 dB（1080p、Style 0）是在仅加速 c32/c64 的模块组合上测得；加深到 ViT 的组合没有重新测量对 NVIDIA 的分数。C512 attention-projection 的 f16 残差变体虽然逐位一致，但更慢，没有发布。这里改变的是数值近似，误差会经过每个后续块传播，具体场景中的累积无法预先判断；本身不跨帧携带。2026-10-03 起，模板默认同时启用该项与全部 71 块：相较此前默认（跳过 42,43,46、逐位路径），每帧约慢 0.12 ms（900）/ 0.19 ms（1080），对 NVIDIA 为 44.26 → 47.55 dB；运动场景下比跳块方案更接近逐位的全块输出约 3 dB（`Development/results/default-swap-20261003`）。`0` / 未设置 = 逐位路径，不打开 `-fast` 文件。其他值会报告并按 0 处理。add-on / RE9 runtime 共用选项，在网络创建时读取。见 `Development/results/fast-numeric-option-20261003`、`Development/results/fast-vit-c512-20261003`。 |
| `DLSS5_MULTI_PASS` | `1`（三套模板；源码默认 1） | 多遍计算（“叠层”）：`2` / `3` 表示每帧完整运行网络 2/3 遍，每一遍输入上一遍的最终 RGB（alpha 为 1），沿用同一份 history、seed 和 noise，类似 Magpie 0.6.8 的 DLSSNR Multi Pass；遍数越多，风格越强。网络输出已经是输入工作编码中截断到 [0,1] 的画面（post head = 输入 RGB + 残差），因此遍与遍之间的 decode→encode 往返为恒等，可直接省略（仅不重复 encode pass 的 RGBA16F 舍入；直接将 f32 输出喂入下一遍）。没有逐遍参数，所有遍使用相同 Style/强度。成本近似线性：离线完整网络 900 为 7.0 → 13.7–13.9 / 20.5–20.7 ms，1080 为 9.8 → 19.5 / 29.0 ms（约 1.95×/2.9×）；输出可重复，两次运行 hash 相同且没有非有限值，见 `Development/results/multi-pass-20261003`。额外显存：`2` 需要一个、`3` 需要两个 RGBA f32 喂图缓冲，每个为处理宽×高×16 字节（1080 档每个 35.4 MB，900 为 24.6 MB，720 为 15.7 MB），在首次多遍帧分配；其余中间张量沿用单遍的内存池。RE9 runtime 实测（`rt_bench`，进程显存）：900 档 1407 → 1435（2）/ 1482 MiB（3），1080 档 1380 → 1491 / 1491 MiB。N>1 时强制关闭自适应 ViT 复用（`DLSS5_VIT_ADAPTIVE`），因为它的缓存按帧而非按遍组织。RE9 的 `GetTimings` / `net_gpu_ms` 和 `DLSS5_NET_TIMING` 报告全部遍数的总耗时。`1` / 未设置 / 空值 = 单遍，与此前路径相同，逐位一致。其他值会在 stderr 报告并按 1 处理。add-on / RE9 runtime 共用选项，位于网络层 `hip_reference_network.h`，在网络创建时读取，仅限 HIP 后端。2026-10-03 起、此行说明最初写成之后，add-on 也可在热重载（`DLSS5_HOT_RELOAD`）时更新它，在下一网络帧之前应用，相同值不做操作；还可用 `DLSS5_MULTI_PASS_HOTKEY` 切换。RE9 runtime 仍只读取一次。 |
| `DLSS5_MULTI_PASS_PREDICT` | `1`（三套模板；源码默认 1） | **有损**的第三遍预测。仅当 `DLSS5_MULTI_PASS=3` 且本项为 `1` 时，真实运行两遍网络，再根据原输入和前两遍输出局部预测第三遍。`0` 关闭预测，真实运行三遍；未设置/空值默认 1。仅遍数为 3 时生效，其他遍数保持现有路径。非法值会报告并按 0 处理。它近似的是我们自己的完整三遍输出，质量和速度需实测，不能宣称 NVIDIA exact。add-on / RE9 runtime 共用选项，已列入白名单。add-on 热重载会在下一网络帧前，与 `DLSS5_MULTI_PASS` 一起应用；RE9 在网络创建时读取，修改后需重启。日志同时记录请求值和三遍预测是否实际启用。 |
| `DLSS5_MULTI_PASS_SKIN_PROTECT` | `0`（三套模板；源码默认 0） | 多遍计算的肤色保护。当本项为 `1` 且 `DLSS5_MULTI_PASS>1` 时，使用启发式肤色 mask 混合第一遍真实输出（`y1`）与最后一遍输出（`yN`）。mask 核心 `m=1` **精确保留 y1**；非肤色 `m=0` **精确保留 yN**；中间值用于边缘过渡。这是颜色启发式，**不是语义分割**：暖色背景可能误选，彩色光照可能导致皮肤漏选。网络输入 `x` 已经是类似 sRGB、带高光肩部压缩的工作编码，mask 不会再次应用 gamma 变换。默认 `0` / 未设置 / 空值，以及所有单遍计算，都保留原行为。非法值在 stderr 报告并按 0 处理。add-on / RE9 runtime 共用选项，已列入白名单。add-on 热重载会在 producer wait 之前准备内核和资源，在下一网络帧前应用开关；RE9 在创建时读取，修改需重启。启动及热重载日志包含遍数、肤色选项和实际启用状态。 |
| `DLSS5_MULTI_PASS_SKIP_BLOCKS` | 空值（常规 / Magpie / RE9 模板；源码默认为空） | 多遍跳块（“叠层减负”）：**只在 `DLSS5_MULTI_PASS` 的第 2..N 遍**跳过指定块，列表语法与 `DLSS5_SKIP_BLOCKS` 相同，在这些遍中追加到该列表；第一遍始终运行原配置的网络。设置后为**有损**。此路径可跳 C32 1–4/66–69、C512 23–30/40–47、ViT 31–38；生产字节流管线不能跳过 C64/C128/C256 块（5–22、48–65）。三遍、当前默认（全部块 + FAST_NUMERIC=1）的离线完整网络：空列表为 21.1 / 29.8 ms（900 / 1080）；`42,43,46` 为 20.7 / 29.3 ms，43.9–46.0 dB；`31,32,33,34,35,36,37,38,40,41,42,43,44,45,46,47`（ViT + C512 上行）为 18.4 / 25.7 ms（−13%/−14%），34.1–35.0 dB；`23`…`38,40`…`47`（再加 C512 下行）为 17.4 / 24.3 ms（−17%/−18%），33.2–33.6 dB。**这些 dB 比的是我们自己的三遍全块输出，不是 NVIDIA**；作为尺度参照，三遍与单遍相比分数为 35.0 dB。ViT + C512 上行组会去掉后几遍增加的一大部分效果，画面更轻、对比度更低，与单遍相距 37.5 dB；更深的跳块组与单遍的距离约等于完整三遍，但方向不同，表现为暗部提亮。属于每帧的结构改动，本身不跨帧携带；省得不多，是因为主要成本在全分辨率 C32/C64 链。`DLSS5_MULTI_PASS=1` 时无效。列表无法解析，或包含该管线不能跳过的块时，在 stderr 报告并按空列表处理。add-on / RE9 runtime 共用选项，已列入白名单，在网络创建时读取。见 `Development/results/multi-pass-skip-20261003`。 |
| `DLSS5_MULTI_PASS_HOTKEY` | `F9`（常规 / Magpie 模板；源码默认 F9；RE9 runtime 不适用） | 游戏内快捷键，从当前生效遍数开始循环切换 `DLSS5_MULTI_PASS`，顺序为 1→2→3→1。按键会将 `DLSS5_MULTI_PASS=N` 写入 `custom-config.txt`：已有行就替换，否则追加；文件不存在则创建，保留 BOM 和换行形式。热重载约一秒内应用，并在 `logs\native-game-oneshot.txt` 记录 `event=multi_pass_hotkey` 和 `multi_pass=N`。层优先级仍然生效：`native-game-flags.txt` 若也有 `DLSS5_MULTI_PASS`，它本来会覆盖 custom，因此该行也会同步改写，并在 stderr 记录；系统环境中的 `DLSS5_MULTI_PASS` 高于所有文件，因此修改不会生效，并在 stderr 提示。可用 `F1`…`F24` 或虚拟键码（如 `0x78`）；`0` 关闭；其他值在 stderr 提示后按 F9 处理。与 F6/F7/F8 一样，每个网络帧用 `GetAsyncKeyState` 轮询一次，因此游戏窗口失去焦点时也会响应。需要开启 `DLSS5_HOT_RELOAD`，默认已开。启动时只读取一次。未按键或关闭时逐位相同。RE9 runtime 没有热重载，也没有此热键。 |
| `DLSS5_FORMAT_FALLBACK` | `1`（0.38 的常规 / Magpie / RE9 模板；源码默认 1） | 颜色格式回退表（`src/native_format_fallback.h`，参考 mochizuki 0.0.2.4）。原表之外、GPU 可通过 typed SRV 解码为 float 的颜色格式也可接受：R9G9B9E5_SHAREDEXP（线性 HDR）、B8G8R8X8（UNORM/SRGB/TYPELESS）、R10G10B10A2（UNORM/TYPELESS）、R32G32B32A32 / R32G32B32（FLOAT/TYPELESS）、R16G16B16A16_SNORM、R8G8B8A8_SNORM、B5G6R5、B5G5R5A1、B4G4R4A4。add-on 的前置路线：一个 compute pass（`native_format_convert.hlsl`，随 `native-game-tiled-assets` 发布）将颜色转换到私有 RGBA16F 纹理，再进入不变的 RGBA16F 路线；FSR 接收该纹理。RE9 runtime：encoder 的 SRV 读取该格式，使用私有 FP16 输出路线（RGB9E5 自 0.28 起使用的路线）。此前已接受的格式不进入此表，因此逐位相同。被拒绝的颜色格式会按名称记录（`native-pre-upscale.txt` / RE9 的 `PrepareFrame: colour rejected`）。后置 / Magpie / XeSS 路线不变，输出仍写回游戏纹理。缺少 shader 时仅关闭这项回退。`0` 使用原表。 |
| `DLSS5_HOT_RELOAD` | `1`（0.38 常规 / Magpie 模板；源码默认 1；RE9 runtime 不适用，OptiScaler 菜单中的强度可实时调整） | add-on 每秒最多检查一次三个配置文件（default / custom / native）的最后修改时间；三个文件的监视从 2026-10-03 起生效，此前只监视 native-game-flags.txt。任一改变后重新读取并从下一帧应用：`DLSS5_STRENGTH`（所有 add-on 路线）、`DLSS5_NOTICE` 和 `DLSS5_SHOW_FPS`（前置路线状态行）；自 2026-10-03 起还包括 `DLSS5_MULTI_PASS`、`DLSS5_MULTI_PASS_PREDICT`、`DLSS5_MULTI_PASS_SKIN_PROTECT`（HIP 网络，在下一网络帧之前）。在 `logs\native-game-oneshot.txt` 记录 `event=hot_reload`。强度 auto/未设置遵循启动时或应用的默认；删除遍数/预测/肤色键会恢复内置默认 1/1/0，而不是保留此前编辑值。网络几何、跳块、HIP/模块/内核开关、DIRECT_IO、PRE_UPSCALE、FIT_*、ASYNC、FRAME_STATS、FORMAT_FALLBACK 只读取一次，因为它们决定缓冲大小、模块加载或网络数值，修改仍需重启。文件未编辑则不变，逐位一致。`0` 完全不轮询。 |
| `DLSS5_IO_FUSE` | `0`（源码默认；模板未设置；RE9 runtime 不适用，它不使用 NativeGameFrame） | 实验选项，逐位相同：解码器直接读取网络的 f32 RGB 输出缓冲，保留相同的 f32→f16 舍入，省去单独的 neural 纹理 pass。仅适用于普通会话，无 temporal/history 输入且没有 `DLSS5_OVERLAP`，否则忽略。离线每帧 −0.004～−0.035 ms，但 900p 的 p99 没有改善（results/input-slim-20261001），因此保持关闭。 |
| `DLSS5_STYLE` | `1`（三套模板；源码默认 1） | NVIDIA NGX Style 控制，可选 `0` / `1` / `2`，以预处理特征 6 = Style/128 传入（C32/prefix 模块中的 `dlss5_style_feature`，宿主在每次模块加载后写入）。`1` 是原 runtime 在《剑星》中使用的风格（2026-09-06 捕获），也是此前所有包内置的风格；`0` 是 NVIDIA 默认，与其参考输出相匹配（1080p 单帧，完整 71 块为 47.43 dB，发布配方为 44.26 dB，而 Style 1 为 24.06 dB）；`2` 为第三种风格。不是精确的 0/1/2 则按 1 处理。未设置或 `1` 不增加 HIP 调用，与此前逐位相同。修改需重启。旧模块没有对应符号时忽略。DX12 非 HIP 路线不适用。RE9 runtime 从 0.39 之后的构建起读取 flags 文件；0.39 的 runtime 只从系统环境读取。**接入方注意**：创建会话、加载模块时只读取一次，不支持热重载；同名环境变量高于 flags 文件行；C32/prefix 模块必须为 0.39 或更新版本，否则旧模块会静默保持 Style 1；实验目录中的 fast-tier c32/c64 模块较旧，也会忽略此项。 |
| `DLSS5_NET_TIMING` | 未设置（任何模板都未设置） | 诊断项：`1` 每帧在 HIP 网络前后记录 hipEvents，与 RE9 runtime 的 `GetTimings` / `GetStatus` 报告同一段耗时；RE9 在第一次 `GetTimings` 调用时开始记录。常规 add-on 不回读该值，因此应保持未设置。输出字节不变。 |


## 历史配置演进

下面是旧包记录，不是当前默认或构建指令。

Development/native-game-flags.txt是早期测试配置，scripts/game-flags.txt与magpie-flags.txt属旧DX12路线；不作为当前HIP正式发布默认值。

0.27三包统一入口：Development/tools/package-027.ps1，同样通过ConfigDirectory读取本目录模板。普通包从已校验的历史ZIP重新解压，不使用可能被运行过的解压目录；REFramework以已校验0.26.1完整包为底座。


0.28三包入口为Development/tools/package-028.ps1。从逐文件校验的0.27完整ZIP构建；Magpie/普通OptiScaler使用各自flags模板，RE9使用re9-presr.ini覆盖基准OptiScaler.ini，并删除无效的旧native-game-flags.txt/post-present addon。RE9网络默认跳层42,43,46、复用关闭，旧F8/SHOW_FPS/NOTICE不适用；细节/颜色强度在OptiScaler菜单调，范围0～1。包内中文/英文说明来自scripts/package-notes/。打包不读取游戏个人配置。

0.28.1仅重打RE9专用包，入口Development/tools/package-0281-re9.ps1；采用74b8a67边界/恢复修复候选，其他两包维持0.28。新包验证通过后撤下本地RE9 0.28整包及其README下载入口。


## 可选 Fast History 抑闪实验（默认关闭）

在 `custom-config.txt` 设置 `DLSS5_FAST_HISTORY=1`，退出并重启游戏后生效。仅支持 HIP 的 FFX pre-upscale、完整网络视口、`MULTI_PASS=1`、`HIP_GRAPH=0`、`OVERLAP=0`；Magpie 与 RE9 独立 runtime 不支持这个消费路径。多层用户要先关闭 Fast History 并重启；不能在开启时用 F9 切到2/3层。预测偏好在 MP1 下不影响单遍。

同时必须声明实际为无抖动的运动向量：`DLSS5_TEMPORAL_MV_UNJITTERED=1`；按游戏实际深度方向设置 `DLSS5_FAST_HISTORY_DEPTH_INVERTED=0`（普通）或 `1`（反向），不要猜值。设置 `DLSS5_TEMPORAL_HISTORY_EXPERIMENT=0`，两套历史不能叠用。现有 `DLSS5_FAST_TEMPORAL=1` 只是旧采样优化，不代表新开关已开启。配置读取仍按本文层级，旧 `native-game-flags.txt` 与环境变量可能覆盖你的设置。

还需匹配所选 FAST/RTZ/norm900 模块的新 logit exports，及模型目录里的 `post70-history-head.f16`；旧包缺少这些内容，不能仅改配置就得到完整功能。[接口与提取方法](../Development/integration_interfaces.md#addon-fast-history)。默认0不依赖这些新资产。

这是独立的近似时序策略：FP32反馈、深度/颜色/黑输出有效性启发式与原版及0.41-a参考实验不同，开启后会改变输出，可能产生拖影。已有数值测试不等于游戏观感验收；新路径成本尚未实测，不能套用参考实验的耗时。开启时整段会禁用近似ViT复用，但不会重写用户偏好。

Fast History currently admits motion textures whose mip-zero dimensions exactly
match the active render width/height. Padded or display-resolution motion
layouts are unsupported: their missing context/active-extent contract resets
history and uses current-frame NR. Depth may be padded because sampling uses
the declared render extent.

当前 Fast History 只接收 mip0 尺寸恰等于实际 render 区域的运动纹理；带padding或显示分辨率MV缺少上下文/有效区域合同，会重置历史并用本帧NR，不猜测采样区域。深度按声明的render区域采样，可保留分配padding。

## Single temporal mode selector / 统一时域模式

`DLSS5_TEMPORAL_MODE=0` is the default: unchanged NR output with no new temporal resources. `1` selects TheAutomatic Fast History (the existing approximate PR12 implementation, FFX-only, explicit unjittered MV/depth/row/logit exports required); `2` selects an independently implemented low-frequency residual EMA at the final NR output, no row or new NN exports. Both active modes require MP1 and cannot combine with `DLSS5_TEMPORAL_HISTORY_EXPERIMENT=1`. Restart to change modes. An explicit MODE value, including0, wins over legacy `DLSS5_FAST_HISTORY`; only absent MODE maps legacy FAST_HISTORY1 to mode1. Invalid/empty MODE is rejected.

Mode2 operates in the codec's encoded NR working domain, not Magpie's display RGB. It retains this frame's high-frequency residual and smooths only guided low-frequency residual over approximately80ms. With a declared valid render-sized unjittered MV it may compensate motion; otherwise it explicitly uses screen coordinates plus conservative original-colour rejection (no invented zero-MV validity, no depth guess). Ghosting/quality and real-game cost remain experimental. The first implementation is synchronous to protect buffer/control lifetimes. It gates approximate ViT reuse for the session without rewriting preference. Runtime input identity is unknown: no duplicate-frame skipping is inferred.

唯一主开关 `DLSS5_TEMPORAL_MODE`：0关闭（默认）、1原TheAutomatic Fast History、2独立实现的低频时域滤波。显式MODE含0压过旧FAST_HISTORY；缺MODE才兼容旧1。两种开启档都要求MP1，不能与reference实验叠用，切档需重启；非法/空值拒绝。Mode2在NR编码工作域而非显示HDR域，仅平滑低频修正、高频留本帧。可信声明的MV可补偿，否则明确用屏幕坐标静态颜色门，可能拖影，不伪造运动/深度合同。当前同步原型优先资源安全；观感/游戏性能需实际试用，旧History耗时不能套用。
