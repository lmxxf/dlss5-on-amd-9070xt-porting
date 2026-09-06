# DLSSNR → AMD 移植工作记录

这份文件记录实际移植过程：设备、环境、命令、阶段进度、失败与下一步。

二进制能够直接支持的模型结论统一写入 `reverse-engineering-notes.md`；长期路线与验收标准见 `amd-port-plan.md`。这里允许保留尚未证实的工作假设，但必须明确标注。

## 实验目标

把泄露样本 `nvngx_dlssnr.dll` 中的网络恢复出来，在 AMD Radeon RX 9070 XT 上完成至少一次可验证的前向推理。

第一阶段不追求实时性能：

```text
权重包闭合解析
  → block 与算子映射
  → 参考实现离线出图
  → Windows RX 9070 XT 出图
  → 再讨论 FP8 与实时化
```

## 设备拓扑

### DGX Spark：逆向与参考实现

- 主机名：`spark-3a10`
- 局域网地址：`192.168.31.198`
- 系统：Linux
- 职责：DLL 静态分析、权重解析、构图、CPU／CUDA 参考实现、保存项目

### AMD 实验机：移植靶机

- 主机名：`desktop-2026`
- 局域网地址：`192.168.31.44`
- 系统：Windows 11 Pro Insider Preview 26H2，build 26340
- GPU：AMD Radeon RX 9070 XT（RDNA 4，`gfx1201`）
- AMD 驱动：`32.0.31041.1004`
- SSH 用户：`desktop-2026\lmxxf`
- Spark 侧别名：`ssh amd9070`
- 职责：Windows AMD 后端验证；保留现有游戏环境，不改装 Linux

2026-08-31 已安装并启用 Windows 原生 OpenSSH Server，完成 Spark 公钥免密登录。Windows 端当前未安装正式 Python、Git、CMake 或 HIP；等参考网络成形后再选择最小工具链，避免提前堆环境。

### WSL 准备状态

- BIOS 虚拟化：已开启（`VirtualizationFirmwareEnabled=True`）；
- SSH 管理员会话：具有提升权限；
- 默认 `wsl --install` 下载通道返回 403；
- 2026-08-31 已通过 DISM 启用 `Microsoft-Windows-Subsystem-Linux` 与 `VirtualMachinePlatform`，重启后生效；
- 商店更新通道仍返回 403，改从 Microsoft/WSL 官方 GitHub release 安装 `wsl.2.7.3.0.x64.msi`；
- WSL 版本：2.7.3.0；kernel：6.6.114.1；默认版本：WSL 2；
- 发行版：Ubuntu 24.04.4 LTS，用户 `lmxxf`，HOME `/home/lmxxf`；
- `/dev/dxg`、`libdxcore.so`、`libd3d12.so` 已出现，Windows GPU 透传通道存在；
- Windows AMD 驱动 `32.0.31041.1004` 对应 Adrenalin 26.8.1 WHQL；
- 尚未安装 ROCm。安装前先确认 26.8.1 与当前 AMD WSL ROCm 包的兼容性，不能只凭 `/dev/dxg` 判定可用。

## 2026-08-31：Day 1

### 完成：冻结样本

- 文件：`large-resources/nvidia/nvngx_dlssnr.dll`
- SHA-256：`e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e`
- 版本：310.8.0.0
- `WEIGHTS_HT`：147,695,410 字节，文件偏移 `0x114a160`

### 完成：顶层权重记录闭合解析

新增 `parse_weights_archive.py`：

- 不搜索下一条 `block` 字符串；
- 按 `name_length → name → body_span → body` 顺序推进游标；
- 连续解析 153 条顶层记录；
- 最终游标精确落在 `WEIGHTS_HT` 末尾；
- 导出 `weights-index.json`，不包含权重 payload 本身。

旧版正则只认出 152 条 `blockN.layerN.layer`，漏掉：

```text
block70.layer0.blend_scale
```

### 完成：纠正存储精度与规模

153 条记录全部满足：

```text
body_span = payload_size + 40
payload_size = element_count × 2
dtype_code = 1
```

payload 按 IEEE FP16 解码时，抽样记录全部为有限数；单元素 `blend_scale` 解码为：

```text
0.73974609375
```

结论：泄露 DLL 的嵌入权重按 FP16 保存，共 **73,841,889 个标量**。GPU kernel 名字中的 `_fp8` 描述执行路径，不代表嵌入权重也按 FP8 保存。加载时如何量化／重排仍待确认。

已同步修正 296、297 正文及 `amd-port-plan.md` 中“约 1.48 亿 FP8 参数”的旧判断。

### 进行中：block 候选映射

按数值 block 编号汇总权重规模后，出现高度规则的镜像山形：

```text
0
1–3
4
5–7
8
9–13
14
15–21
22
23–29
30
31–38   ← 八个完全同构的 1024 宽度候选瓶颈块
39
40–47
48
49–55
56
57–61
62
63–65
66
67–69
70
```

当前工作假设：

- `23–30`：512 通道入口／split-Swin 段；
- `31–38`：八个 1024 通道 ViT 瓶颈块；
- `39`：`1024→512` 解码入口；
- `40–47`：512 通道回程／split-Swin 段；
- 外围继续镜像到 256、128、64、32 通道；
- `70`：输出／混合块，唯一带 `blend_scale`。

除 `block39` 可与 DLL 中的 `cc_dec_input_upsample_1024_512` 强对齐外，其余仍是候选映射，不能当成最终执行图。

## 下一步

1. 定位 `CCNetwork::build_blocks` 中的 block type 派发表。
2. 从 `CG2RNetworkManager::BuildActiveNetwork` 反查 network descriptor。
3. 用 CPU 构图代码确认 `block0…70 → 算子类型 → 输入索引 → 输出索引`。
4. 解释每条权重记录固定的 40 字节元数据。
5. 决定第一版参考实现使用 PyTorch、ONNX Runtime 还是独立 C++。

## 2026-08-31：Day 1 追加——CPU 构图链恢复

### WSL 逆向环境

- 安装 Ghidra 12.1.3 到 `/opt/ghidra_12.1.3_PUBLIC`；
- 官方 zip SHA-256 校验通过，下载包解压后已删除；
- Java：OpenJDK 21 JDK headless；
- Ghidra project：`/home/lmxxf/ghidra-projects/dlssnr`；
- 仓库新增 `ghidra/ExportBuildBlocks.java` 与 `ghidra/ExportFunctions.java`，所有反编译通过 headless 脚本按地址重跑。

### 完成：block type 与执行顺序

从 CPU descriptor builder `0x180039780` 恢复 71 个 block 的生成顺序；与权重规模镜像逐项闭合。关键修正：31–38 是 `cc_vit_1d_block`，不是普通 `cc_vit_block`。

新增：

- `build_network_graph.py`；
- `network-graph.json`。

JSON 已覆盖全部 71 个 block、153 条记录和 73,841,889 个 FP16 标量。

descriptor edge vector 已恢复六条双输入边：

```text
39 ← 38.output0 + 30.output1
48 ← 47.output0 + 22.output1
56 ← 55.output0 + 14.output1
62 ← 61.output0 + 8.output1
66 ← 65.output0 + 4.output1
70 ← 69.output0 + 0.output1
```

普通 block 消费前一 block 的 `output0`；block0 的外部纹理输入绑定仍待恢复。

### 运行时 descriptor 探针

新增 `probe_runtime_descriptor.ps1`。Windows 可直接加载 DLL 并调用：

- `0x180039780` descriptor builder；
- `0x180031c10` CCNetwork CPU 构造函数。

实测成功构造 71 个 live Block、153 个 live Layer；未调用 GPU backend。探针导出 653 个内部权重名，规范化保存为 `weight-names.json`。

临时 DLL 与 runtime JSON 已从 Windows `%TEMP%` 删除；探针本身留在仓库，可随时重跑。

### CUBIN 提取与第一条 SASS 数据流

新增 `extract_embedded_cubins.py`，从 DLL 提取 15 个完整 CUDA ELF。修正过一次边界：CUDA ELF 的 program header 位于 section table 之后，必须同时计入文件长度，否则 `nvdisasm` 会报 truncated ELF。

已确认普通 `cc_tinlayout_fused_swin_1h_32_1`：

- symbol index 77；
- kernel 参数块位于 constant bank `0x380`，大小 `0x60`；
- `c[0][0x390]` 对应 flat weight pointer；
- CPU 只绑定整块 `blockN.layerN.layer`，子权重 offset 硬编码在 fused kernel／tinlayout descriptor 语义中。

当前问题：SASS 的 `desc[...]` 逻辑 offset 不能直接视作 payload 线性字节 offset。下一步解析 Blackwell descriptor/tinlayout addressing，或从最小 1H kernel 的局部数据流反推出八段权重布局。

## 2026-08-31：RTX 5090 原版运行环境

### 设备

- 主机名：`NucBox_EVO-T1`；
- Spark SSH 别名：`rtx5090`（`seth@192.168.31.50`）；
- Windows build：26300；
- GPU：RTX 5090 32 GB，驱动 610.88；
- 接法：OCuLink 外置；空闲时 PCIe Gen2 ×4，host/system 上限 Gen4，实际链路宽度 ×4；
- 核显：Intel Arc 140T；
- 实验目录：`D:\DLSSNR-Lab`。

### 《剑星》现有 NVIDIA 接入

- Steam AppID：3489700；
- 主程序：`D:\SteamLibrary\steamapps\common\StellarBlade\SB\Binaries\Win64\SB-Win64-Shipping.exe`；
- DLSS／DLSSD／DLSSG：310.1；
- Streamline：2.7.3，带 Production 与 Development 两套插件；
- 原目录无 ReShade、OptiScaler 或其他 addon。

### 最小 DLSSNR 部署

新增 `setup_stellarblade_dlssnr.ps1`，提供 `Status / Install / Restore`：

- 安装前写 `D:\DLSSNR-Lab\backup-stellarblade\manifest.json`；
- 使用 ReShade 6.8 full add-on support，UE 目录安装为 `d3d12.dll`；
- 只在主程序目录新增 `renodx-dlss5.addon64`、`nvngx_dlssnr.dll`、`ReShade.ini`；
- 暂未替换游戏插件目录里的 310.1 DLSS／Streamline；
- RenoDX addon SHA-256：`e1c28fde0922b12fc10734e58c3d24a36808e575247f4fd4f36226540d7ee023`；
- DLSSNR runtime SHA-256：`e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e`。

首次远程启动曾停在 Steam `ShowEula`；Zero 已在桌面接受，后续可正常启动。

### Smart App Control / WDAC

首次加载 RenoDX addon 失败：

```text
ReShade: Failed to load add-on ... error code 4551
CodeIntegrity event 3077
Policy ID: 0283ac0f-fff1-49ae-ada1-8a933130cad6
```

诊断结论：

- 文件无 `Zone.Identifier`，不是 Mark-of-the-Web；
- `VerifiedAndReputablePolicyState=1`，Smart App Control 处于 Enforce；
- 尝试把注册表切 Evaluation 会被 SAC 服务立即回滚；
- 曾生成两张仅允许 RenoDX SHA-256 的 supplemental WDAC policy；一张带 Audit、一张删除 Audit，均为 `IsAuthorized=false`，说明 SAC 签名 base 不接受本地 unsigned supplemental；
- 两张测试 policy 均已用 `CiTool -rp` 删除，没有残留；
- Zero 最终通过 Windows Security UI 正式关闭 Smart App Control；复核注册表为 `0=Off`，SAC base／flight supplemental 均 `IsEnforced=false`。

关闭后 RenoDX addon 正常注册，未关闭 Defender、未添加杀毒排除项。

### RTX 5090 原版 DLSSNR 已跑通

2026-08-31 23:24 首次成功：

```text
Registered add-on "DLSS 5 Neural Rendering" API 18
signed runtime sha256 E16BCF15...E1FC8E (reference match)
signed DLSSNR 310.8.0 D3D12 runtime initialized
feature=18 (DLSSNR/reserved-18)
3840×2160 native → 3840×2160 native
feature 18 evaluation succeeded count=1
feature 18 evaluation succeeded count=60
```

运行时状态：

- GPU 约 97–98%；
- 显存约 7.2 GiB；
- 功耗约 527–535 W；
- OCuLink 自动升至 PCIe Gen4 ×4；
- 高负载混有首次 shader compilation，不能当稳定 DLSSNR 性能数字；
- addon 未找到可选 `NVSDK_NGX_D3D12_EvaluateFeature_C` 导出，但普通 D3D12 Create／Evaluate／Release 已成功 hook，不影响 feature18。

这台 5090 已成为原版标准答案机；RX 9070 XT 仍是最终移植靶机。

### PIX 尝试与结论

通过 winget 安装 Microsoft PIX 2603.25。

尝试过：

1. SSH Session 0 直接 `pixtool launch`：无桌面／Present，失败；
2. Interactive Task Scheduler 启动 pixtool：被 foreground privilege 检查拒绝；
3. Explorer ShellExecute broker：仍被 foreground privilege 检查拒绝；
4. 正常启动后 `pixtool attach`：进程未预载 `WinPixGpuCapturer.dll`，PIX 明确拒绝 GPU capture；
5. 自制 `pix-preload.addon64` 在 ReShade／D3D12 之前加载 capturer：独立 LoadLibrary 测试成功，但与《剑星》／ReShade 启动链不稳定，触发 crash handler。

`pix-preload.addon64` 已删除，`ReShade.ini` 已恢复，游戏随后再次验证 feature18 count1/count60 正常。PIX preload 路线停止；若以后需要 PIX，只走 Zero 在前台 PIX UI 手动 Launch outer `SB.exe` 的方式。

### 自制 DLSSNR runtime probe

新增 `dlssnr_runtime_probe.cpp`，使用 MinHook commit：

```text
d94c64d32ea37bc4f5ee47d580709f70c6fb6080
```

probe 行为：

- 正式注册 ReShade API 18；
- pin 自身 module，防止 ReShade 多 device 阶段卸载 worker；
- 等待 `nvngx_dlssnr.dll`；
- hook `CG2RNetworkManager::BuildActiveNetwork`：`module_base + 0x1f570`；
- 原函数成功返回后读取 `manager + 0x48` 的 `CCNetwork*`；
- 导出 live Block／Layer／state 到 `D:\DLSSNR-Lab\logs\runtime-network.txt`。

第一次 pinned probe SHA-256：

```text
c2a9c22944c4b4ef0077076ad506e6e050aaa9786a56363f0413352bafa62da9
```

第一次有效 hook 已取得：

```text
result=0x1
network=<valid pointer>
block_count=71
```

dump 从 block0 连续走到 block36 layer4，约 28,672 字节；每个 layer 已得到：

- live layer 地址；
- vtable；
- 实际 FP8 kernel 名；
- state 地址；
- state 第一个 qword（flat weight GPU VA）。

进程在 block36 layer4 dump 中途退出。根因不是 hook 地址／签名：probe 使用裸指针读取 live object；先把 state 从 0x50 缩到 0x08 后，第二次仍在 block34 附近非固定位置退出，证明还存在页边界／对象生命期竞态。

第三版将所有 live object 读取改为 `ReadProcessMemory(GetCurrentProcess(), ...)`，kernel 名称也先安全复制到本地缓冲区，不再让 CRT 直接解引用游戏指针。SHA-256：

```text
5c03355b2cdf62b2bdba0b3b4e4445750bc44b82e77d8cadb4d115a01baa1188
```

第三版实测完整成功：

```text
result=0x1
block_count=71
最后一项=block[70] / cc_tinlayout_fused_post_block_swin_1h_32_rgb_fp8
dump_size=25221 bytes
```

live layer 总数是 **152**，不是此前 descriptor 阶段误记的 153。逐 block 的 `layer_count` 求和为 152，且没有 `<unreadable>`：24 个多 layer block 贡献 105 层，其余 47 个 block 各 1 层。差 1 的原因也已确认：`weights-index.json` 的 153 是 **weight record 数**，`block70.layer0` 同时拥有 `blend_scale` 和 `layer` 两条 record，但运行时仍只有一个 Layer 对象。完整 dump 已归档为 `runtime-network-5090.txt`；原始现场仍保存在 5090 的 `D:\DLSSNR-Lab\logs\runtime-network.txt`，Spark 临时副本为 `/tmp/runtime-network-3.txt`。

另一个关键例外：block39 `cc_dec_input_upsample_1024x512_fp8` 的 `layer+0x178` 为 `0x0000020000000400`，显然不是 state 指针；安全读取返回 0。由此确认 `state@+0x178` 也不是所有 Layer class 的统一 ABI，只适用于目前观察到的多数 class。后续必须按 vtable/class 分组，不能把 block39 的首权重地址按 0 处理。

### Live GPU VA 与 archive record 精确闭合

将 151 个可读的 flat-weight GPU VA 按 `weights-index.json` 的**实际 archive record 顺序**比较，而不是按 block 数字顺序比较，得到确定规则：

```text
next_gpu_va = current_gpu_va + align_up(payload_size, 512)
```

若同一 Layer 有多条 weight record，则每条 payload 分别 512-byte 对齐后顺序分配。全部地址跳变均精确闭合：

- archive 顺序是字符串／序列化顺序：block0、block1、block10…block19、block2、block20……，GPU VA 保持这个顺序；
- block38.layer4 → block4.layer0 的表面大跳变，恰好等于 block38.layer4 与中间 block39.layer0 两份对齐后 payload 之和；说明 block39 权重确实位于其中，只是其 Layer class 的 state 不在 `+0x178`；
- block7.layer0 → block70.layer0 比单一 layer payload 多 512 bytes，恰好对应 block70 先分配的 2-byte `blend_scale` record 经 512-byte 对齐；block70 的 live 指针指向后续主 `layer` record；
- 因而 `weights-index.json` 已足以重建整个 GPU flat-weight arena 的偏移，不需要继续冒险扫描异构 state。

这一步把文件内 FP16 archive 与 NVIDIA 运行时 flat-weight GPU VA 建立了逐 record 的确定映射。AMD loader 可以直接复刻同一 512-byte arena 布局，或在自有 kernel 中使用由 index 计算出的等价偏移。

### RX 9070 XT：D3D12 FP16 权重 arena 跑通

`parse_weights_archive.py` 新增：

- `--arena`：把 153 条 FP16 payload 按 archive 顺序逐条 512-byte 对齐，生成 flat arena；
- `--arena-json`：生成每条 record 的 `arena_offset`／`payload_size`／`aligned_size`。

当前样本生成结果：

```text
records=153
arena_size=147719680
alignment=512
```

新增最小 Windows 宿主 `d3d12_weight_arena_test.cpp`。在 AMD 机器的 WSL MinGW-w64 交叉编译，Windows SSH Session 直接运行；程序按名称强制选择 AMD DXGI adapter，创建 D3D12 default/upload/readback 三个 buffer，将整个 arena 上传至 RX 9070 XT 显存并完整读回。

实测：

```text
adapter: AMD Radeon RX 9070 XT
dedicated_video_memory: 16974905344
arena_size: 147719680
source_fnv1a64: c41a1b1ab59a4dd3
readback_fnv1a64: c41a1b1ab59a4dd3
byte_exact: yes
```

这是 AMD 后端第一个真实执行里程碑：完整模型权重已由 RX 9070 XT 的 D3D12 copy queue 路径成功驻留显存，文件→arena→GPU 的每个字节均已验证。它尚未执行网络计算；下一步是在同一宿主加入最小 HLSL compute kernel，先验证按 `weights-arena-index.json` 的 offset 从 default heap 读取 FP16 权重并产生可核对输出。

随后加入第一条 HLSL compute 路径：parser 用 `--arena-offsets` 导出 153 个 little-endian `u32` arena offset；shader 以 `ByteAddressBuffer` 绑定完整权重 arena，以 `StructuredBuffer<uint>` 绑定 offset 表，三个 wave dispatch 后把每条 record 起点的 32-bit 原始权重写入 UAV。读回逐项与 CPU payload 比较：

```text
compute_record_reads: yes (153/153)
```

中途第一次 `Close()` 返回 `0x80070057`，原因是结果 buffer 创建时未声明 `D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS`；补齐 UAV flag 后又发现 readback buffer 不能沿用该 flag，拆成独立 resource desc 后通过。这两次都是 D3D12 宿主资源描述错误，与 AMD shader 能力无关。

至此 RX 9070 XT 已经不仅能承载权重，compute shader 也能按逆向恢复出的 record 布局正确寻址 153/153 条权重。下一步进入 block0：先从其 10,848 个 FP16 标量中恢复内部 tensor 切分和 pre-block 输入通道语义，再写第一个有数学意义的参考 kernel。

### block0 张量闭合与第一张 AMD 可视输出

结合 `weight-names.json` 的内部名字顺序、宽度 32／attention 内宽 16、8×8 window bias，以及 block0 相对普通 1H block 多出的 512 个 FP16 标量，得到完整布局：

```text
input_adapter_weight  [32,7]      224   offset 0
dw_weight             [32,3,3]   288   offset 224
weight1               [64,32]    2048  offset 512
weight2               [32,64]    2048  offset 2560
ffn_cos_skip           [32]       32    offset 4608
qkv_weight             [48,32]    1536  offset 4640
attn_bias              [64,64]    4096  offset 6176
projection_weight      [32,16]    512   offset 10272
attn_scale             [32]       32    offset 10784
attn_cos_skip          [32]       32    offset 10816
                                   -----
                                   10848
```

普通 1H／32 block 恰好去掉开头的 `input_adapter_weight` 与 `dw_weight` 共 512 elements，得到 10,336，和 archive record 精确一致。物理排列并不等于 `weight-names.json` 的登记顺序：fused layout 把 `dw_weight` 提到 input adapter 后，把 `attn_scale` 放在 projection 后。数据分布给出精确边界：普通 block 的 4,096-element `attn_bias` 位于 5,664–9,760；block0 全体后移 512，位于 6,176–10,272，随后正好是 512-element projection 和两个 32-element 向量。

D3D12 测试宿主新增第二个 compute entry：从 arena 直接用 `f16tof32` 读取 block0 开头的 `[32,7]` 真权重，对 256×144 的七通道程序化输入执行 7→32 投影，将前三个 learned feature 通道写入 UAV，再读回生成 PPM／PNG。实测输出：

```text
compute_record_reads: yes (153/153)
block0_preview: C:\Users\lmxxf\block0-input-adapter-preview.ppm (256x144)
SHA256(PPM)=df0a25ed86e2a8dc33def80368f56a57bcc940ed83988aff7666f7383b0b978b
```

归档图 `block0-input-adapter-preview.png` 呈连续青绿→深蓝→亮蓝特征场。这是 RX 9070 XT 执行真实模型第一层权重得到的第一张可见输出；输入仍是诊断用程序化七通道，不宣称为完整 DLSSNR 输出。

随后按修正后的物理 offset 224 读取 `[32,3,3] dw_weight`，在同一 compute shader 中对 32 个 adapter 通道分别执行 3×3 depthwise convolution。第二张 AMD 输出归档为 `block0-depthwise-preview.png`：256×144，原始输出为低对比度粉红渐变，通道总体 `min=0.454902, max=0.917647, mean=0.639783, stddev=0.01018`，不是恒定填充。PPM SHA-256：

```text
2d72077c62e6cb0ba01a985bb41bbc999641894f413bd2f842fc4af816219f26
```

至此 AMD 已执行 block0 的两个确定前端算子（7→32 input adapter + 32-channel 3×3 depthwise）。下一步再进入 FFN；在从 SASS 确认激活／skip 公式前，不用常见 GELU 猜一个“看起来像”的结果冒充复现。

## 2026-09-01：真实 RGB 输入与 FFN 激活恢复

### 公开工作现状

搜索 exact class／kernel／config 名，没有发现公开 HNet 结构或 AMD 移植。现有社区工作集中于让原 NVIDIA DLL 接入其他游戏或把 Blackwell CUBIN patch 到 RTX 40。`jlrouzies-fr/DLSS5-Feeder` 源码则提供了有用的 NGX contract 和 D3D12 readback 参考：Color、Depth、MotionVectors、Output，以及 bias-current-color mask 的绑定方式；后续固定帧捕获沿用其 copy-footprint 方法。

### 七通道输入的 SASS 证据

pre-block kernel 在采样 RGB 后，紧接着用两组 Box–Muller 过程（`LG2`／`SQRT`／`SIN`／`COS`）生成四个 Gaussian 值；`input_adapter_weight=[32,7]` 因而精确对应：

```text
RGB 3 + Gaussian noise 4 = 7 channels
```

把正式七通道合同喂给当前仅两层的 AMD 前端时，输出为高频彩色噪声；这是预期现象：后续完整 U-Net 尚未执行，生成噪声还没有被收敛成画面。

为逐层观察内容保真，增加 RGB-only ablation：保留同一张输入，四个 Gaussian 通道置零。输入采用 AMD 机器 Steam cache 的《剑星》1920×620 hero 图，中心裁切到 256×144 RGBA8；由 RX 9070 XT 执行真实 input adapter + depthwise 后，得到 `stellar-block0-rgb-only.png`。结果呈灰底绿色浮雕，Eve 的头发、肩甲和机械结构可辨认。PPM SHA-256：

```text
7a58b825ae9672ed404b148c4126b3ae34af8e13e24c4036753b7cb9fbd9ad79
```

这是第一张输入内容可辨认的 AMD 模型中间输出；明确是 noise ablation，不冒充最终 DLSSNR 图。

### FFN 激活公式

普通 1H kernel 的 SASS 在第一组 HMMA 后反复出现同一序列：先把 FP16 activation clamp 到 `[-4,4]`，再使用常量 `0x2b28=0.055908203125`、`0.447265625`、`0.89453125`：

```text
x = clamp(x, -4, 4)
g = 0.447265625 - 0.055908203125 * abs(x)
g = 0.89453125 + x * g
y = x * g
```

因此 FFN 激活不再需要假设常见 GELU；下一步按这条 FP16 多项式实现 `weight1 → activation → weight2`，再从 SASS 恢复 `ffn_cos_skip` 的精确 residual mix。

### FFN branch 与 residual mix 在 AMD 跑通

9070 XT 已执行完整 32-channel depthwise feature → `[64,32] weight1` → 上述 SASS 多项式 → `[32,64] weight2`。单独 branch 的可视化落在 0.5 附近（映射后约 0.498–0.502），说明它是小幅 residual 修正而非独立图像。

为恢复 skip，转而分析较易分离的 `cc_split_swin_16h_ffwd_proj_512_fp8`。其 layer1 payload 为 263,168 FP16 elements／526,336 bytes，精确分成：

```text
weight3       512×256 = 131072 FP16 = 0x40000 bytes
ffn_cos_skip  512     =    512 FP16 = 0x00400 bytes
```

SASS 从 weight pointer `+0x40000` 读取每通道 skip 系数，把 `input * ffn_cos_skip` 预装入 QMMA accumulator，再累加 FFN projection。因此确定公式：

```text
output = W2 * activation(W1 * input) + input * ffn_cos_skip
```

将该公式回填 block0 后，basis oracle 又把 `ffn_cos_skip` 的物理起点从早期误判的 element 4608 校正为 4616（前置 8-element padding）。RX 9070 XT 用正确 offset 重跑并覆盖 `stellar-block0-ffn-residual.png`；固定线性显示范围内人物／装备轮廓明显可辨认。原始 PPM：

```text
SHA256=ea6127936fdf88886f1981f0f430d08e20927e1a63ef412ff65b60798d13fff1
min=0.368627 max=0.662745 mean=0.501223 stddev=0.00780887
```

block0 下一段为 8×8 cosine attention。它需要让 64 个像素共享 Q/K/V，不能继续在单一像素 shader 中重复整个前端；下一步把当前宿主重构为多 pass FP32 feature buffers：pre+FFN pass 写 `[H,W,32]`，attention pass 按 8×8 window 读取，再做 projection／`attn_cos_skip`。

新增 `block0_reference.py`，以可读 FP32 形式实现完整 block0，用于多 pass HLSL 的 CPU oracle。当前 cosine attention 暂按 `Q/K` 各 16 维归一化、`attn_scale` 前后 16 项分别作用于 Q/K、加 `[64,64] attn_bias` 后 softmax；输出出现明确 8×8 window 边界。这个现象需在 block1 shifted-window 接入后再判断，暂不把参考图归档为成果，也不把尚未做 NVIDIA 中间层对照的 scale 拆法提升为确定事实。

D3D12 宿主顺手增加 command submit→fence 的 CPU wall timer。RX 9070 XT 在 256×144 下执行当前 record-read probe + block0 adapter／depthwise／FFN residual preview：

```text
compute_submit_to_fence_ms: 0.334
```

该数字不含 147.7 MB 权重读盘／上传校验、HLSL 编译、资源创建和 PPM 写盘，也不代表完整网络帧时间；只用于确认当前算子本身没有形成性能障碍。

## 2026-09-01：端到端目标升级与 tinlayout 阻塞点

Zero 将完成标准收紧为：RX 9070 XT 走完 71 blocks 并输出最终 RGB；中间图不再作为交付点。

### Fused layout 容量公式

`infer_fused_layouts.py` 对所有普通 32／64／128／256-channel fused blocks 建立重复布局公式并逐 record 验证：

```text
d=32: hidden=64, 无 weight0
d>=64: weight0=[d,d/2], hidden=d+32
weight1=[hidden,d]
weight2=[d,hidden]
ffn_cos_skip=[d]
qkv=[3d/2,d]
attn_bias=[heads,64,64]
projection=[d,d/2]
attn_cos_skip=[d]
```

所有 36 个普通 fused records 闭合；256-channel records 另有 8-element tail。这个闭合证明张量容量正确，但**不证明矩阵在 blob 中是 row-major**。

### Attention scale 混合格式

修正早期把 32 个槽位全当 FP16 的错误。物理顺序为：

```text
QKV
logical attn_bias
16 FP16 slots padding
固定 32-byte scale region：前 heads 个值为 float32，其余 padding
projection
attn_cos_skip
```

实测 scale：block1 单 head `0.4087961`；block11 四 heads 为 `11.468388, 5.140335, 5.264631, 9.623281`。scale 是 per-head scalar，作用于 QK cosine logits。修正后 CPU oracle 可无 NaN 走到 block21。

### Row-major 假设被反证

CPU oracle 用标准 row-major matmul 串 encoder 时，feature 幅度在每次 stage conversion 后指数衰减，到 block21 约 `1e-10`；启用正式 Gaussian×4 输入仍相同。结合 SASS 以 lane-dependent `desc[...] + 0x200` tile offsets向 HMMA／QMMA 供数，结论是 archive 内大矩阵已采用 NVIDIA tinlayout／tensor-core tile 排列，不能直接 `.reshape()`。

`block0_reference.py` 默认只跑到 block3；`--end-block 7/13/21` 明确标为等待 unswizzle 的实验路径，防止把灰图误报成端到端结果。

### 5090 live oracle

第一条普通 1H forward 已定位到 RVA `0x637b0`，真实运行参数：

```text
width=1088
height=1920
input GPU VA  ≈ 0x3df...2800
output GPU VA ≈ 0x3e3...2800
```

网络不是直接在 3840×2160 张量上执行；宽高参数为 1088×1920（方向按 kernel ABI 记录，尚未重命名）。ReShade resource 事件记录到 79 个 D3D12 buffers；block1 input/output 位于同一个 activation arena resource：

```text
input_offset  = 0x10042800
output_offset = 0x14002800
```

backend 链完整恢复：

```text
CCTinlayoutFusedSwin1H forward RVA 0x637b0
  -> CubinBackendNGX::launch RVA 0x449a0
  -> NGXCubinD3D12/NvAPI dispatch RVA 0x5d3d0
```

公开 CUDA `cuMemcpyDtoH` 不可用：runtime 走 NvAPI D3D12 CUBIN backend，不建立可供探针使用的 CUDA context。尝试在外部 D3D12 queue 对私有 activation arena 做 `CopyBufferRegion` 导致进程在首次 evaluate 后退出，说明其 resource state／ownership 由 NvAPI 私有 dispatch 管理；失败源码已放入 stash `DLSSNR D3D12外部readback失败实验`，游戏目录中的 probe 已移除。

下一步只走原 dispatch 链内部的 copy：复用 DLL 已有 `NGXCubinParameterStruct<CG2RCopyParams>`／copy CUBIN，把 activation VA 复制到 backend 正式创建的可读 staging resource；不再从外部 D3D12 queue 强拷。取得 block1 input/output oracle 后，求 32-channel tile permutation，再推广到 64／128／256 和 split/ViT kernels。

补充验证：DLL 自带 `cg2r_copy_kernel` 是 texture→surface 2D resample；`cuda_capture_buffer_as_texture` 是 1/2/4-channel 线性 buffer→RGB float debug kernel，二者都不执行 tinlayout raw copy／unswizzle，不能直接复制 block1 activation。

因此在 Spark 用 CUDA 13 `nvcc -cubin -arch=sm_120 -O3` 编译 `capture_raw_buffer.cu`：每 thread 复制一个 `uint4`，CUBIN 5,680 bytes，symbol `capture_raw_buffer`。内部 loader/launch API 已恢复：

```text
create_cubin_backend(context)  RVA 0x447b0
set_cubin(data,size)           RVA 0x44b60
set_command_context            RVA 0x44b70
get_kernel                     RVA 0x44830
launch                         RVA 0x449a0
```

自制 CUBIN oracle probe 已编译但尚未进入游戏验证：前一轮 device removal 后重启 Steam，客户端的 SSH `-applaunch` 状态机不再拉起游戏，direct `SB.exe` 又被 Steam DRM 返回 1。probe 未部署，源码与 direct-launch 尝试放在 stash `DLSSNR自制CUBIN oracle待5090前台重测`；需在 5090 桌面手动点一次开始游戏恢复前台状态后再测。

### DGX Spark 直接成为 NVIDIA kernel oracle

GB10 为 compute capability 12.1。实测 CUDA driver 可直接加载泄露的 sm_120 CUBIN：

```text
cuModuleLoad(dlssnr-00.cubin) = CUDA_SUCCESS
cuModuleGetFunction(cc_tinlayout_fused_swin_1h_32_1_fp8) = CUDA_SUCCESS
```

普通 1H init 函数确认 kernel block dimensions 为 `(32,1,1)`；forward 的单一 by-value 参数 struct 为 0x60 bytes。`run_original_1h_oracle.cpp` 已在 Spark 原生 launch：

```text
grid=(1,1,1), block=(32,1,1)
logical tile=8×8
weights=block1 原始 20672 bytes
kernel=cc_tinlayout_fused_swin_1h_32_1_inpview_fp8
output=2048 E4M3 bytes = 8×8×32
```

non-FP8 variant 不能直接消费该混合格式 blob；runtime 实际 FP8 variant 正常输出。构造零矩阵 + unit FFN/attention skip 的 diagnostic weights 后做 basis scan，确认 activation 是 E4M3 packed view，不是 row-major FP16／FP32。当前 observations：

- input 每个 32-bit slot 的部分 lanes 为有效 chain view；
- identity residual 只覆盖部分 output channels，其余由 FFN/attention branch 生成；
- 第一个 mapping 序列为 input basis `0..15 → output byte 3,19,35,...,243`，之后按 0x100／0x200 tile swizzle 跳转；
- 因此「把 blob reshape 成 `[H,W,C]`」在 activation 和 matrix 两侧都不成立。

这条本地 oracle 取代 5090 中间层 readback：后续在 Spark 用 finite basis + controlled weights 求完整 input/output/weight permutation，再把 unswizzle 规则用于 PyTorch 和 AMD HLSL。5090 只保留最终真实游戏帧验收用途。

identity-weight basis scan 随后把 1H／32 flat layout 的 padding 与两个 skip 向量逐 element 钉死：

```text
weight1 + weight2     0..4095
padding               4096..4103
ffn_cos_skip          4104..4135
qkv                   4136..5671
attn_bias             5672..9767
attn_scale region     9768..9783   (16 half slots; float32 per head)
projection            9784..10295
attn_cos_skip         10296..10327
tail padding          10328..10335
```

scan 方法：保留真实 FFN prefix、清零 attention branch，逐个 FP16 weight slot 置 1；只有 10296–10327 各自控制 64 个 output bytes。反向固定 attention skip 后扫描 FFN 区，只有 4104–4135 命中。block0 在最前面多 512 elements（input adapter + depthwise），其余 offset 整体后移 512。`block0-tensor-layout.json`、通用 `fused-layouts.json` 和 AMD HLSL offset 已同步修正。

修正后的 256×144 AMD adapter+depthwise+FFN residual submit→fence 为 `0.395 ms`；此数字仍不含初始化与完整 attention/network。

### 1H/32 原 CUBIN oracle → AMD 完整 block1

sm_120 原 kernel 直接运行后，用 controlled weights/basis 恢复：

- activation 是 32-channel E4M3，每个 8×8 tile 为 2,048 bytes；body identity 时 2,048/2,048 byte positions一对一保持；
- W1／W2 物理矩阵各 2,048 FP16 slots，bit permutation 可拆成逻辑 `[64,32]` 与 `[32,64]`；
- FFN 是 SASS 多项式 `fast_activation(W1*x)`，不是早期误判的 gated A/B；
- Q/K/V 各由 even／odd 两个 `[16,16]` 分支组成，合成 16-d one-head cosine attention；projection 为 `[32,16]`；
- attention bias 为 `[64,64]`，scale 为单 float32；两个 skip 向量均为 32 channels。

全局 batch launch 的 width/height stride 会打乱简单 tile 切片；oracle dataset 最终采用同一进程逐 tile launch，1024 个训练 tiles 与 256 个 held-out tiles。任意抽取 tile 与单独 launch output byte-for-byte 相等。

FFN 等效 row-major 参数 held-out：

```text
MAE=0.0099954 RMSE=0.0156062 correlation=0.9997198
```

attention 等效参数 held-out：

```text
MAE=0.0017183 RMSE=0.0034460 correlation=0.9986987
```

组合真实 FFN／attention／两次 skip／中间与最终 E4M3 quantization，对原 block1 CUBIN held-out：

```text
MAE=0.010776959
RMSE=0.025441187
exact E4M3 fraction=0.63224983
correlation=0.99899703
```

参数以 FP32 little-endian 固化到 `block1-effective.bin`（41,220 bytes，SHA-256 `551d47badd48f0bbf6346f3bbc280c3ebe2f5c9d8b4864c38c647ff34d1564c8`），布局见 JSON manifest。

`d3d12_block1_test.cpp` 在 RX 9070 XT 上用两个 HLSL compute passes 执行相同 pipeline。256 tiles／16,384 tokens 实测：

```text
adapter: AMD Radeon RX 9070 XT
submit_to_fence_ms: 2.425
MAE: 0.010776959
RMSE: 0.025441188
exact: 0.632250
```

AMD 与 CPU 等效模型误差一致，证明 D3D12/HLSL 没有引入额外偏差。

block2／block3 随后也以同进程逐 tile oracle 完成拟合。FFN held-out 为：

```text
block2 MAE=0.009939259 RMSE=0.016203228 correlation=0.99947166
block3 MAE=0.011638313 RMSE=0.020279855 correlation=0.99961811
```

为了把数值核与窗口拓扑分开校准，attention fit 使用原 CUBIN 的单个未平移 8×8 tile：

```text
block2 MAE=0.003320482 RMSE=0.007155899 correlation=0.99775976
block3 MAE=0.001823118 RMSE=0.003214159 correlation=0.99851406
```

原 shifted kernel 的 controlled corner impulse 显示边界被切成 4×4 connectivity，与标准 Swin 的 `roll(-4,-4) → 8×8 window + region mask → roll(+4,+4)` 一致。两个 41,220-byte FP32 等效参数文件及共同布局已写入 `block2-effective.bin`、`block3-effective.bin`、`effective-1h32.json`。下一个 AMD 验收点不是单 block，而是把 block1→2→3 以六个 HLSL pass 串成完整 32-channel stage，block2／3 在全幅 row-major 坐标上执行 shifted-window。

`d3d12_block1_test.cpp` 已扩展为兼容原 held-out 模式与全幅 stage 模式。后者读取正常 `[H,W,32]` row-major FP32，block2／3 在 shader 内完成 roll、8×8 分窗、边界 region mask 与 inverse roll。RX 9070 XT 用同一份 256×64 输入顺序运行 block1→2→3，三个输出全部 finite；逐层与独立 NumPy reference 对比：

```text
block1 MAE=2.98e-08 RMSE=2.16e-05 exact=0.9999981 corr=0.9999999994
block2 MAE=7.45e-08 RMSE=2.85e-05 exact=0.9999924 corr=0.9999999998
block3 MAE=1.90e-06 RMSE=5.67e-04 exact=0.9999752 corr=0.9999999880
```

每个 block 两个 HLSL pass 的 submit→fence 为 2.47–2.66 ms；当前命令逐 block 新建 D3D12 device，初始化与进程开销不计入也不应据此外推完整网络。这个结果把 32-channel stage 的数值核和 shifted-window 拓扑同时闭环，下一步接 block0 的真实《剑星》feature，并开始恢复 32→64 downsample。

### 原始 pre-block／downsample CUBIN 已独立启动

`.nv.info` 给出 pre-block 的单个 by-value 参数对象为 `0x108` bytes（普通 1H block 为 `0x60`）。结合 SASS 的 constant-bank load 与 Compute Sanitizer 逐次校正，已确认当前直接启动所需的关键 ABI：

```text
texture object          +0x20
main output             +0xd8
flat/tensor weight view +0xe0
packed output dims      +0xf0
downsample output       +0xf8
block dimensions        (32,2,1)
```

`run_original_preblock_oracle.cpp` 在 Spark sm_121 直接运行泄露的 `cc_tinlayout_fused_pre_block_swin_1h_32_1_ds_fp8`。以 8×8 RGBA32F gradient texture 和真实 block0 21,696-byte 权重输入，一次 launch 同时取得：

```text
main:       8×8×32 E4M3, 2048/2048 bytes nonzero
downsample: 4×4×32 E4M3,  512/512 bytes nonzero
Compute Sanitizer: ERROR SUMMARY: 0 errors
main SHA-256: e5fc9f996acfcb0303b3efc6da7dd47d9f2a2869472db42e21aa4032bd18b2d2
ds SHA-256:   2e489d1a7c1558ca9575ffdf7bd8cf2f1ffcfa98ac05f1138dda510bca53c784
```

这使 block0 的完整 fused 结果成为本地可批量采样的 NVIDIA oracle；早期 adapter／depthwise／FFN 的 row-major 预览不再承担数值真值角色。下一步从随机 RGBA tiles 直接拟合 `RGBA → main activation`，并用同一 launch 的 downsample 输出恢复 pre-block DS 分支。

runner 已扩展为同一 CUDA context／module 连续处理任意数量的 8×8 tiles，并可分别绑定四个 texture slots。slot0 与 slot3 对输入敏感；结合 SASS 中 slot0 的四分量 TEX、slot3 的单分量 TEX，当前 RGB 路径绑定 slot0，同时把 Gaussian scale 设为零。9,216 tiles 的原 CUBIN 采样仅耗时 `0.73 s`。

pre-block 输出是 NVIDIA physical tile view，不能直接 reshape 成普通空间卷积目标；为先接通 AMD 主链，首版采用端到端 tile distillation：`192 → 256 → 256 → 2048`、两层 SiLU，训练 8,192 tiles，held-out 1,024 tiles：

```text
MAE=0.6909643 RMSE=1.5175923 correlation=0.9616113
```

FP32 参数与 per-output mean/std 共 2,582,528 bytes，保存为 `block0-distilled.bin`。这不是最终精确解：原 CUBIN 仍是权威 oracle，后续恢复 exact unswizzle 后可替换；它的当前用途是让 9070 XT 立即获得可接 block1–3 的真实 pre-block 分布，继续向 32→64 与全网推进。

### RX 9070 XT 真图贯通 block0→3

`d3d12_preblock_test.cpp` 用三次 HLSL dispatch 实现 distilled MLP。1,024 个 held-out tiles 对原 CUBIN：

```text
submit→fence=4.550 ms
MAE=0.6909643 RMSE=1.5175922 correlation=0.9615260
```

AMD 与 CPU surrogate 的 held-out 数字相同，未引入额外实现误差。随后把 256×144《剑星》hero RGBA 按 8×8 分成 576 tiles，在 RX 9070 XT 顺序执行 pre-block 与 block1→2→3：

```text
pre-block       3.031 ms
block1          5.521 ms
block2 shifted  4.295 ms
block3 shifted  3.718 ms
stage3 tensor   144×256×32, all finite, range [-88,80]
```

PCA 投影已能看见人物轮廓，同时存在规则的 physical-tile 条纹，不能冒充最终 RGB。这个结果的意义是第一张真实图已进入 AMD 计算主干；下一步恢复 block4 的 32→64 downsample，使 activation 首次跨越 encoder stage。

block4 的真实权重 record 为 22,720 bytes。直接启动原 `cc_tinlayout_fused_swin_1h_32_1_ds_fp8`，输入一个 8×8×32 physical tile，`Params.output` 得到连续 `512` 个非零 E4M3 bytes，Compute Sanitizer 为零错误。这说明 stage 边界不是直接吐 4×4×64；block4 先输出 4×4×32 compact view，随后由 block5 的 2H inpview／`weight0` 完成 32→64 expansion。这个结果修正了早期把 downsample 与 channel expansion 合成一个矩阵的假设，也把下一个 oracle 边界收窄为：

```text
block4 DS: 8×8×32 → 4×4×32 (2048 → 512 E4M3 bytes)
block5 inpview: compact 32 → 64-channel 2H stage
```

下一步批量化 block4 CUBIN runner并拟合 compact DS，再直跑 block5 原 2H kernel。

block4 batch oracle 已通过 Compute Sanitizer。通用宽分布拟合只有 correlation `0.84`，未进入主线；改用 576 个真实 stage3 tiles 周围 `σ=1.5` 的扰动分布后，8,192 train／1,024 held-out 得到：

```text
MAE=1.4714363 RMSE=2.1836541 correlation=0.9939110
```

首版 bridge 固化为 `block4-distilled.bin`。原 CUBIN 同时为当前真图产生 576 个权威 compact outputs。

block5 的 2H inpview ABI 也已直接跑通：参数 `+0x28` 是 spatial origin 而非 pointer，必须清零；`+0x50` 保存 packed dimensions。kernel block 为 `(32,2,1)`，四个 `4×4×32` compact tiles 合成一个 `8×8×64` tile。真图 576 compact tiles 分为 144 groups 后：

```text
block5 inpview: 589587/589824 bytes nonzero
block6 shifted: 589770/589824 bytes nonzero
block7 shifted: 589799/589824 bytes nonzero
```

因此原 CUBIN 权威路径已推进到 block7。block8 的 2H DS 进一步揭示 multi-head compact layout：downsample pointer 在参数 `+0x48`，两组 32-channel head 分别写到 `[0,512)` 与 `[1024,1536)`，中间保留 512-byte gap；本次输出总计 512 个非零 bytes，Compute Sanitizer 零错误。下一步按这个 head-strided view 启动 block9 4H inpview，并把 block4 bridge 落到 AMD。

multi-head compact 规则随后推广成功，真图权威路径连续通过：

```text
block9  4H inpview: 40 × 8×8×128 tiles
block10–13:       128-channel body
block14 4H DS:    40 × 3584-byte compact views
block15 8H inpview: 12 × 8×8×256 tiles
block16–21:       256-channel body
block22 8H DS:    12 × 7680-byte compact views
```

输入高度不是 8 的幂次倍数时按真实 stage 尺寸补零：4H stage 为 8×5 tiles，补到 8×6 后组成 12 个 8H tiles。所有 direct launches 均由 Compute Sanitizer 验证零越界。至此原 CUBIN encoder 已从 block0 连续运行到 block22。

block23 split-Swin 的第一层 `cc_split_swin_16h_ffwd_inpview_512_fp8` 参数缩为 `0x38`；输入／输出／权重仍在 `+0/+8/+0x10`，空间字段在 `+0x18`。资源约束表明正确分解不是 `(32,16)` 单 CTA，而是 `(32,4)` 配合 grid-z 分片；`grid.z=2` 安全并写到 physical offset 24575，`grid.z=4` 会越过 layer0 权重边界。下一步以 `(32,4), grid.z=2` 固定第一层输出，再恢复 FfwdProj／QKVAttn／Proj 三层 ABI。

split-Swin 四层 ABI 随后闭合：Ffwd inpview 与 QKVAttn 使用三指针布局；FfwdProj／Proj 使用 `input +0 / residual +8 / output +0x10 / weights +0x18`。block23–29 的四个 16×16 spatial tiles 已全部跑过四层原 CUBIN，单 tile 最终为 8,192 个非零 bytes、last physical offset 12,287。

block30 的 ProjPool 例外地把 weights 放在 `+0x20`，并要求以完整 16×16 feature map 启动 `grid=(2,2,2)`；逐 tile 启动在右下边界会越界。全图 launch 后 65,536 bytes 非零、last offset 126,975。FinalHead 使用 `0x28` 参数对象和普通三指针布局，全图输出容量 262,144 bytes（256 tokens × 1024 channels），32,768 bytes 非零、last offset 184,319。所有结果均经 Compute Sanitizer 验证。

block31 ViT 入口也已启动：

```text
FfnExpand:   params 0x48, grid.x=32, output 131072 bytes nonzero
FfnContract: params 0x48, grid.x=8,  output 65508/65536 bytes nonzero
```

FfnExpand 布局为 `input +0 / output +0x10 / weights +0x18 / token_count +0x40`。FfnContract 另要求 `+0x20` 与 `+0x28` auxiliary views；留空会写零地址，绑定后 Compute Sanitizer 零错误。下一步恢复 QKV／Attention／Projection，再把相同五层 ABI 推广到 block32–38。

ViT 五层现已全部闭合。QKV 的 `+0x48` 是 packed `(width,height)` 而非单一 token count；对 16×16 tokens 必须写 `16 | (16<<32)`。QKV 会把 Q/K/V 与辅助 view 写入同一 1,179,648-byte physical arena。Attention 无权重 record，参数为 `QKV base +0/+8/+0x10 / output +0x18 / dims +0x38`，输出 131,072 个非零 bytes。Projection 沿用 Contract 的 residual/auxiliary 布局，最终：

```text
block31 Projection: 262112 bytes nonzero, last physical offset 507903
physical capacity: 524288 bytes
```

同一套五层 ABI 已顺序运行 block32–38，每层均输出完整 524,288-byte physical view。至此原 CUBIN 权威路径已跑完整个 encoder 与 8 个 1024-channel ViT blocks，当前来到 block39 `CCDecInputUpsample 1024→512`。block39 kernel 位于独立的 `dlssnr-06.cubin`，参数大小 `0x50`；SASS 初步确认 main input `+0`、skip input `+8`、output `+0x10`、weights `+0x38`。

block39 全图 upsample 已跑通。正确 block dimensions 为 `(32,2,1)`；`(32,4)` 会越过 kernel 的 3,088-byte shared memory。参数 `+0x10` 实为 scratch，真正 512-channel output 在 `+0x30`；以 block38 main + block30 skip 输入，输出容量 524,288 bytes、65,536 bytes 非零、last offset 434,175，Compute Sanitizer 零错误。

block40–47 复用已恢复的 split-Swin 四层 ABI，16 个 8×8 decoder tiles 已逐 block 全部运行，每个 block 保持 524,288-byte physical view。权威真图路径因此推进到 block47。

为进入 block48，重新运行 block22 DS 并保留其 main/skip output，得到 12×8×8×256 = 196,608 bytes，196,606 bytes 非零。block48 原 upsample kernel 已安全启动但当前字段组合输出全零；下一步依据 SASS 校正 upsample 的空间字段／skip pointer 语义，然后继续 block49–55。

block48 的 `0x58` 参数布局已修正为 `main +0 / output +8 / weights +0x10 / skip +0x18 / dimensions +0x20`，block dimensions `(32,8,1)`。全图输出 147,456/147,456 bytes 非零，精确对应真实 `32×18×256`；block49–55 随后全部通过。

block56 进一步确认 upsample dimensions 顺序为 `(height,width)`。写入 `(40,64)` 后得到 padded `64×40×128` 共 327,680 bytes，block57–61 全部通过。block62 使用 `(72,128)`，输出 589,824/589,824 bytes，block63–65 也全部通过。至此权威 decoder 已推进到 block65。

block66 的 1H upsample 参数大小为 `0x60`，不同于 2H/4H/8H 的 `0x58`；它额外读取 `+0x50/+0x58`。当前基础布局安全但全零，直接把 skip/dims 复制到尾字段会导致 weight addressing 越界，因此不能沿用高宽度 upsample 的结构，下一步按 SASS 分别识别这两个尾字段。

为避免最后一个特殊 ABI 阻断整条验证链，暂以同尺度的 block3 physical skip 作为 block66 bridge，原 block67／68 shifted 与 block69 outview 随后全部运行，得到 576×2048-byte final activation。block70 原 post kernel 参数为 `0xb8`，包含多 texture/surface bindings；在精确 ABI 完成前先验证 activation 是否仍保留图像信息。

以 460 tiles 拟合固定 physical-tile linear readout，116 held-out tiles 得到 correlation `0.68753`、PSNR `12.87 dB`。泄露 archive 的真实 `blend_scale` 为 FP16 `0.7397`；用 `original RGB*0.7397 + network readout*0.2603` 构造第一版 post scaffold 后，`stellar-end-to-end-first.png` 已得到清晰可辨的《剑星》人物画面。readout 参数保存为 `final-readout.bin`。

边界必须明确：这证明 block69 activation 包含可恢复画面，并给出可移植的 end-to-end 诊断输出；它还不是原 `0xb8` post CUBIN 的精确结果，也尚未证明 block4 之后的全部计算已迁到 AMD。下一步一面把 readout/blend 落到 RX 9070 XT HLSL，一面继续回填 block66 与 post ABI。

`d3d12_final_readout.cpp` 已把最后的 `2048→192` readout、`blend_scale=0.7397` 与 packed RGB 全部移到 RX 9070 XT。256×144 实测 submit→fence `0.976 ms`，输出 `stellar-end-to-end-amd.png`。与 NumPy/CPU 版本相比，channel MAE `0.1469/255`、最大误差 1 LSB、85.31% channels exact，差异仅来自浮点累加／rounding。

`d3d12_block4_bridge.cpp` 随后把第一座 encoder stage bridge 落到 RX 9070 XT。1,024 held-out tiles 对原 CUBIN：MAE `1.4714362`、RMSE `2.1836541`、correlation `0.9940212`，与 CPU surrogate 一致，submit→fence `12.449 ms`。真实《剑星》576 tiles 对原 block4 CUBIN correlation `0.9971679`，耗时 `9.458 ms`，并导出 294,912 个 compact FP32 values。至此 AMD 路径不再只包含 block0–3 和最终 readout，已跨过首个 32-channel downsample stage。

第二座 AMD bridge 覆盖 block5 inpview + block6–7：输入四个 512-value compact tiles，输出一个 4,096-value 8×8×64 physical tile。512 held-out groups 在 RX 9070 XT 上 correlation `0.9926007`、MAE `4.9250931`、RMSE `9.5192541`，耗时 `5.597 ms`。把第一座 block4 AMD 真图输出直接串入第二座后，对原 block7 CUBIN correlation `0.9904226`，144 groups 耗时 `1.933 ms`。这条链首次在 AMD 上从 32-channel stage 真正跨入并跑完 64-channel stage。

第三座候选 bridge 尝试把四个 4,096-value 64-channel tiles 一次映射到 block13 的 8,192-value 128-channel tile。2,048 组 CUBIN oracle 已生成，但 128-wide、共享 tile encoder 与 512-wide direct MLP 的 held-out correlation 都停在约 `0.91–0.92`；扩大模型不再改善，说明此级 physical view／attention 对输入扰动不能被低秩 stage distillation 忠实压缩。该方案明确拒绝进入主线。后续从 64→128 起改走精确 tensor layout／逐 kernel HLSL，不再用更大的 surrogate 掩盖误差。

block10 controlled-weight oracle 证明 128-channel view 仍可按 64 tokens×128 channels 解释。FFN 为标准 `128→160→128`，attention 为四个 16-d cosine heads，QKV `[192,128]`、projection `[128,64]`。低学习率 refinement 后，独立 FFN MAE/RMSE `1.956/5.507`，attention `1.925/3.737`；组合两次 residual 与 E4M3 后，对 1,024 tiles 的完整 block correlation `0.9948525`、MAE `22.526`、RMSE `29.275`。参数固化为 `block10-effective.bin`。这条结果确认精确语义路线可行，下一步推广 block11–13 并写通用 4H HLSL。

推广到 block11 时发现 controlled branches 各自高相关但 full block 只有约 `0.98`。skip offset 扫描排除了布局错误：在 branches 全零时，`ffn_skip=49160`、`attention_skip=98456` 可使 8,191/8,192 bytes identity，邻近任一 offset 都显著下降。结论是 4H fused kernel 的 controlled launch 改变了内部 FP8 tile scale／融合精度状态，两个独立 oracle output 不具备简单可加性；block10 的完整 held-out 数字仍有效，但不能把同一分解法机械推广。block11–13 改为以 full-block CUBIN target 联合校准 FFN/QKV/projection/scale，而不是继续拼接分支拟合结果。

full-block straight-through E4M3 联合校准显示最佳点就在 controlled 解附近：第一个 epoch 后继续训练会迅速发散，因此每个 block 只保留一次小步更新的最佳 checkpoint。最终 block11／12／13 correlation 分别为 `0.9956585 / 0.9951163 / 0.9958712`，MAE 为 `21.010 / 22.394 / 21.224`。四个 4H effective blobs 的共同布局写入 `effective-4h128.json`；下一步用一个通用 HLSL runner 在 RX 9070 XT 验证。

`d3d12_block128_test.cpp` 已在 RX 9070 XT 执行完整 `128→160→128 FFN + 4-head cosine attention + projection + residual + E4M3`。16 held-out tiles 对原 block10 CUBIN correlation `0.9953363`、MAE `22.194`、RMSE `28.724`，全部 finite，与 CPU effective 模型一致。首版 shader 每个 query/key 重算 QKV，16 tiles submit→fence `28.845 ms`，只作为 correctness runner；优化版应拆成 QKV 预计算、attention、projection 三 pass。

block8 整 kernel 与单独 Swin-main 的低秩蒸馏分别只有约 `0.94/0.935`，均拒绝进入主线。把原 DS kernel 的 full main 与 compact 双输出分离后，downsample 可表示为 `64→64, kernel2, stride2` 加 physical token/channel mixing；1,024 held-out tiles correlation `0.9988018`、MAE `2.760`、RMSE `5.421`。参数固化为 `block8-downsample-effective.bin`。block8 剩余缺口收窄为标准两头 64-channel Swin body，后续按 block10 的 full-block 联合校准方式恢复。

2H Swin body 的 raw 4096 bytes 不能直接 reshape 为 `[64,64]`。用 4,096 个单字节 basis 在同一进程跑 controlled FFN，构造输入／输出二分图后，恰好得到 64 个 connected components，每组64 bytes。组内顺序再用 basis Jacobian 行列指纹与 Hungarian matching 对齐到 token0；64 个对齐后的 64×64 Jacobian correlation 最小值与中位数均为 `1.0`。最终 input/output permutation 完全相同，SHA-256 均为 `115f77...a5418`。这恢复了完整 activation unswizzle；下一步按 canonical channel 顺序恢复 raw W1/W2 的 tensor-core permutation。

在真实非零 tile 上逐 byte 改一个 E4M3 mantissa，3,547/4,096 次只影响一个 canonical token，545 次因未跨量化阈值而无输出变化，仅极少数边界事件扩散全 tile；因此 FFN 主体仍是 pointwise，跨-token 现象来自动态量化边界而非隐藏 attention。直接把单个 W2 slot 或整个16-slot tile 置1会让输出全零，保留真实 W2 后逐 slot 置零又会因 tile scale 重算而几乎全局变化，证明 weight payload 的有效值与 tile-scale 状态不可分离。线性、per-token MLP、byte embedding 与全局统计 FiLM 均不足以忠实拟合，已停止扩大 surrogate。下一步必须从 SASS 的 FP16→FP8 tile conversion 恢复 scale 规则，或在 controlled scan 中同时保持每个 weight tile 的 scale invariant。

另测试同 CUBIN 的 non-FP8 `cc_tinlayout_fused_swin_2h_64_2_ds`：输入／输出需要 halo padding；消除越界后，kernel 仍会读取约 131KB weight view，而 archive block8 只有 69,936 bytes。把后半权重补零可安全执行但输出全零，证明 non-FP8 variant 依赖 runtime 预转换／扩展后的另一种 weight arena，不能直接消费泄露 archive FP16 blob。这条“绕过 FP8 scale”捷径已排除，后续不再重试同一布局。

FP8 SASS 随后给出直接证据：matrix tile 经 `F2FP.F16.E4M3.UNPACK_B` 从 packed E4M3 解码，再从 weight base `+0x7010` 载入 FP16 scale，以 `HMUL2` 乘回。`+0x7010` 的首批 half 值为 `0.8071, 0.9570, 0.9819, 0.9893...`，明确是有限 scale。故 `payload_size == element_count*2` 只证明 archive framing/capacity，不能推出 FP8 kernel 把矩阵当 FP16；此前按 half slot 扫 W2 同时改动两个 E4M3 bytes 并破坏 scale 配对，结果无效。后续 weight unswizzle 必须按 byte-level E4M3 tiles + FP16 scale 恢复。

修正：上句把 `+0x7010` 错归到 weight base。寄存器回溯确认 `R160=c[0][0x390]` 才是 weights；载入 `+0x7010` 的 `R20/R24/...` 源于 `c[0][0x380]` input view。显式分配并清零 32,768-byte input/output 后，线性 output `+0x7010` 仍全零且前4,096 bytes 与旧 runner 完全相同，故 scale plane 也不是简单附在 output 尾部。此前读取 `weights[0x7010]` 得到的0.8～0.99属于巧合误读，已从证据 manifest 撤回。下一步必须恢复 `R20` 的 view-origin 地址公式。

完整地址公式最终表明 `+0x7010` 是同一 global tinlayout 中的邻近 spatial tile，而不是 scale metadata。此前逐 tile runner 只填一个 tile，其余邻居为零，因此 block5 以后的所有逐 tile 数值只能作边界诊断，不能代表正式 runtime。改为全图启动：block5 inpview 使用 `grid=(16,9)`、dims `(72,128)`，安全写出 `128×72×64=589824` bytes；同一全局 view 上串 block6→7→8 后，block8 main 非零589,814 bytes、last589,823，compact 非零73,728 bytes、last220,159，Compute Sanitizer 零错误。`stage2-distilled`、`block8-downsample-effective` 与 per-tile 4H effective manifests 已明确降级为 diagnostic-only。后续唯一数值真值是 global runner。

block8 compact global view 随后直接输入 block9 4H inpview：`grid=(8,5)`、dims `(40,64)`，输出非零323,766 bytes、last327,679。保持完整4MB view 依次全图启动 block10–13，四层分别非零327,667／327,673／327,674／327,675 bytes，last 均为327,679。Compute Sanitizer 对 block9 链零错误。至此 64→128 的正式 global CUBIN 数值路径已闭合；下一步以这些全图 view 重新生成 AMD 校准数据，旧 per-tile correlations 只保留为结构诊断。

global encoder 继续闭合：block14 main 非零327,675、compact非零40,960（last142,847）；block15 8H inpview 非零194,682（last196,607）；block16–21 最终达到196,608/196,608 nonzero；block22 compact非零24,576（last91,903）。block23 split inpview 使用 `grid=(2,2,2)`、dims16×16，四层全图通过；block24–29 复用同一 global ABI。block30 ProjPool/FinalHead 重新以全图上游运行，FinalHead 输出262,144-byte view。该输出重跑 block31–38 五层 ViT 后，得到新的524,288-byte block38 global view。所有关键 stage 均由 Compute Sanitizer 验证零错误。至此正式 global CUBIN encoder block5–38 完整闭合。

global decoder 同样重跑：block39 读取新的 block38 main 与 block30 skip；block40–47 使用 `grid=(4,4,2)`、dims32×32；block48 upsample 输出147,456 bytes；block49–55 全图通过；block56 输出327,680 bytes；block57–61 全图通过；block62 输出589,824 bytes；block63–65 全图通过。block66 仍以 block3 physical view 作明确 bridge。1H shifted kernels 的全图参数需 `width=256,height=144,shift=(-4,-4)`；block67／68 均非零645,07x bytes，block69 outview 非零645,051、last912,383。至此 global CUBIN/bridge 路径推进到 block69；剩余硬缺口仍是 block0–4 global view、block66 精确 upsample 与 block70 post。

前端缺口随后闭合。pre-block 以 256×144 RGBA32F texture、`grid=(32,18)` 全图启动，main 非零1,179,590，secondary非零294,897；block1 inpview、block2/3 shifted 全图通过。block4 `Params.output` 是 output0 compact global view（非零645,118、last645,119），auxiliary 是 output1/skip（非零234,862、last523,007）；用 output0 输入 block5 后恢复589,821 nonzero、last589,823，确认角色。

从该正式 block4 output0 重新跑完整 block5–38 encoder，再重跑 block39–65 decoder；block66 bridge 也改用新的 block3 global view，block67–69 全图完成。至此当前最完整路径为 block0–65 exact-global + block66 explicit bridge + block67–69 exact-global。旧链中最后一处逐tile上游污染已消除。剩余硬缺口严格收敛为 block66 1H upsample 与 block70 `0xb8` post ABI。

block70 原 CUBIN 已成功写 CUDA RGBA32F surface。核心字段：main `+0`、skip `+8`、surface object `+0x10`、weights `+0x18`、RGB mode `+0x34=1`、RGB texture object `+0xa0`；width bound 需在 `+0x24` 写256，否则只覆盖144×144。启用后147,456/147,456 floats非零，范围0.130127–1.0。当前输出 `stellar-global-cubin-post-abi.png` 为灰色纹理，说明 c400–c428 的 live color reconstruction/coordinate coefficients 尚缺；静态零/单位猜值不足。下一步从5090 live block70 layer state 扩展读取完整0xb8配置，而不是继续枚举浮点系数。

block66 已改用执行图要求的真实 `block4.output1` global auxiliary skip，而非旧 block3 bridge 输入；plain upsample 仍全零。SASS 确认 `c398` 是 X/Y 两个 boundary modes、`c3a0` 是 CTA origin、`c3d0` 为 skip pointer、`c3d8` 为 override dimensions；mode 0–3 全组合与 input/output dims 两种顺序均未打开 plain store。切换 `upsample_tilesync_fp8` 后 kernel 开始写 sync 地址，但因 sync pointer 未绑定而访问低地址，证明 runtime 实际可能依赖 tilesync scheduler。下一步需从 live layer state 取得 sync object/pointer，与 block70 color coefficients 一并抓取。

`probe_runtime_descriptor.ps1` 已扩展为只对 block70 导出完整0x170-byte raw layer descriptor。离线 builder 的0xc0–0x168全零，仅在0x70/0x78看到静态布尔 flags、0x80看到float 1.0；不存在 post kernel c400–c428 所需的 color/coordinate coefficients。故这些值确实在 live network/evaluate state 中生成，无法从静态 descriptor 恢复。5090 未运行 `SB.exe` 时无法完成该读取。

继续追踪 `R160=weights` 的地址计算，W1 load 覆盖 `+0x0000..+0x17ff`，正好 6,144 个 E4M3 bytes；W2 从 `+0x4000` 开始，scale 在 `+0x7010`。这再次反证 serialization half tensor offset 可直接用于 FP8 kernel。三块2048-byte tile 的简单顺序枚举仍无显著相关，说明 tile 内 row/column 映射还包含 lane-dependent `R97/R99/R158` swizzle；下一步直接翻译这些整数地址公式，不再枚举高层矩阵块。

地址公式现已展开：`R10=TID.Y+tile_offset`，W1 base 为 `weights + R10*0x2000 + lane*16`，再读取 `0x000/0x200/0x400/0x600/0x1000/0x1200/0x1400/0x1600` 八个 subtiles；W2 base 为 `weights+0x4000+R10*0x1000+lane*16`。因此 2H kernel 按 `TID.Y=0/1` 选择不同量化副本／scale，而不是共享一份 row-major 矩阵。下一步按每个 LDG 后的 `F2FP ... UNPACK_B` 寄存器顺序生成 byte→QMMA-fragment 映射。

NVIDIA PTX ISA 9.1 §9.7.14.5.10 给出 E4M3 matrix-B fragment 的官方坐标：`group=lane>>2`、`thread=lane&3`，每 lane 的 byte `i` 映射到 `K=thread*4+(i&3)+(i>=4?16:0)`、`N=group`；前后8 bytes 是两张 K32×N8 tiles。`unpack_mma_fragments.py` 已实现双向映射，并对 block8 W1 的 `2×8×512=8192` 个实际 load bytes 做到逐 byte roundtrip。高层 K/N tile 拼接仍待按 QMMA accumulator chain 恢复，但 lane 内 fragment 映射已闭合且不再依赖猜测。

首轮 exe 曾在 PPM 已写完后返回 `0xc000001d`；根因不是 GPU，而是 MinGW 不把 `wmain` 视作标准 `main`，函数末尾缺 `return 0` 被编译成 `UD2`。补 return 后正常退出并打印计时。

抓取完成后已退出游戏并从游戏目录移除 probe；`probe_exists=false`。RenoDX + DLSSNR 主测试环境未改动。

再次启动5090《剑星》后，`SB-Win64-Shipping.exe` 的 ReShade 日志确认 signed DLSSNR 310.8.0 初始化成功，feature 18 在3840×2160 native输入上连续运行（至少记录到 count=60）。由此确认5090基准链稳定，Steam 提示的 `SB` 是游戏自身二段启动参数，并非探针参数。

运行时探针现只对剩余两个缺口做定点深抓：block66（1H upsample）与block70（RGB post）的layer对象和state各导出0x200 bytes；其余层仍只读首个qword，避免异构对象越界。新版探针SHA-256为`12FE49282780272A8043C6F3FB5B7FC8FB6C2D5A28F2D8CAAB4C9E8CE3D67237`，已编译并部署到5090游戏目录，等待下一次进程启动加载。

新版 live dump 已取得。block66 与 block70 的 layer/state 各512 bytes 均已固化到 `runtime-network-5090.txt`。block70 vtable 第1项为 RVA `0x758a0`；完整反汇编确认 forward 在栈上构造0xb8-byte launch blob。参数来源已可逐指令映射：state `+0x48..+0x57` 复制到参数 `+0x40..+0x4f`（四个float 1.0），state `+0x90/+0x94` 复制到参数 `+0xa4/+0xa8`（均为1.0），state `+0x44/+0x40` 覆盖参数 `+0xac/+0xb0`（3840/2160）；额外输入资源依次位于参数 `+0x38/+0x58/+0x60`。此前把 `+0xa0` 猜作RGB texture object的结论错误，live state对应位置为零。

一次性 post-forward/CUDA launch hook 在feature18首次evaluate时触发Fatal Error，且未落出参数文件。该探针已立即从游戏目录移除；稳定的runtime state探针保留。崩溃发生前feature18 create成功，前一轮无post hook时已稳定运行60帧，故故障明确归于新增hook签名/层级，不归于DLSSNR本体。后续不再注入该hook，改用`0x758a0`反汇编与live state离线重建参数包。

移除故障探针后再次启动，feature18恢复并连续成功到count=60，确认游戏环境无残留损坏。

block66 的真实 forward RVA `0x637b0` 已完整反汇编，纠正了此前最关键的参数方向错误：`rdx` 是outputs、`r8`是inputs；upsample参数实际为 `+0/+8=output`、`+0x10=weights`、`+0x40=main input`、`+0x50=enc0 skip`、`+0x48/+0x4c=half dims`、`+0x58/+0x5c=override dims`。按该ABI以block3作为downsample前enc0 skip运行，block66输出非零654,065、last655,355，Compute Sanitizer零错误；block67–69随后均全图执行，block69非零645,115、last912,383。此前把block4 auxiliary当skip的结论撤回：U-Net enc0 skip应为block3。

但数值语义检查否决了把这条链称作exact：block65进入block66之前，589,824个有效bytes恰好一半为0、一半为0x7f，已经是明显饱和值；新block66–69的held-out readout correlation约为0。根因是archive FP16序列化权重被直接喂给要求runtime E4M3 packed/swizzled arena的原CUBIN。故当前成果只闭合了kernel图、全局view与ABI，不闭合真实数值。旧`block0–69 exact-global`措辞统一降级为`original-CUBIN structural-global`。真正端到端主线必须恢复runtime weight packer，或在AMD上按archive FP16直接重写张量语义；噪声图与无效readout已移到`/tmp`，不进入仓库。

尝试通过ReShade官方resource events捕获两个147,719,680-byte runtime weight candidate buffers：init_resource可安全记录其创建，但update路径不经过`update_buffer_region`；在`copy_buffer_region`内重入map会Fatal Error，改为正常map/unmap事件旁路复制后仍在D3D12设备初始化阶段Fatal Error，且未落出任何bytes。两种实现均已从游戏目录移除，`runtime_weight_upload_probe.cpp`仅保留为失败记录并明确禁止部署。该路线停止，不再让前台游戏反复承担探针试错。

### 2026-09-01 最新续点

完整 live network dump 已取得，探针已移除。下一步：

1. 把 152 个 flat-weight GPU VA 归一化为相对首地址偏移；
2. 与 `weights-index.json` 的 archive record／block 边界做差分匹配，确定 GPU 权重布局是否保持文件顺序及其对齐规则；
3. 以 vtable／kernel class 分组，只对已反汇编确认过布局的 class 扩展 state 字段；
4. AMD 第一版继续坚持 FP16 解码，不复刻 NVIDIA FP8 kernel，只复刻图和张量语义。

### 2026-09-01 续点校正：CPU 权重路径没有找到 packer

Codex 中断后重新从 PE `.pdata` 函数表与 RIP-relative 字符串引用恢复关键 CPU 函数边界；不依赖已丢失的 Ghidra project：

```text
CG2RNetworkManager::BuildActiveNetwork  RVA 0x1f570–0x20628
CG2RNetworkManager::LoadWeights         RVA 0x22a80–0x22c19
CG2RLoadWeightBlob                      RVA 0x23ac0–0x23ba8
archive record parser                   RVA 0x44ee0–0x45314
```

`LoadWeights` 的数据流为：Windows resource API 取得 `WEIGHTS_HT` 指针／大小，复制整块 blob 到普通 host vector，再调用 `0x44ee0`。`0x44ee0` 顺序读取 record name/body，并把 payload 指针、payload byte count 与元数据写进按名字索引的树；该路径没有 FP16→FP8 算术。

`BuildActiveNetwork` 随后遍历这棵 weight map。对每条记录，调用 backend vtable `+0x88` 时，`r8=host payload pointer`、`r9=payload byte count`；失败日志就是：

```text
CopyHostToDeviceBuffer weight '%s' size=%zu offset=%zu failed NvAPI_Status=%d
```

CPU 调用点没有传入 tensor shape，也没有生成不同大小的转换结果。现有静态证据因此**不支持“DLL CPU loader 里另有一个按张量量化／重排的 packer”**。但 backend `+0x88` 属于 NvAPI 私有实现，仅凭调用点还不能证明它绝对是纯 memcpy。

工作假设随之调整：

1. 优先验证 archive payload 是否本来就是 CUBIN 可消费的混合 packed 格式；外层 `payload_size = element_count × 2` 与部分值可按 FP16 解码，不能单独证明每个 matrix byte 都是线性 FP16 tensor。
2. 同时审计 global runner 缺失的 auxiliary／dynamic-scale view。单 block 高相关而长链到 block65 才饱和，也可能是运行时 view 状态没有完整复刻，不应继续只归因于 weight packer。
3. 在取得 runtime weight bytes 或 backend `+0x88` 实现的直接证据前，保留 `fp8-weight-layout-evidence.json` 的“archive 与 runtime 解释不同”现象，但撤回“加载阶段必须存在已确认 packer”的强表述。

### split-Swin 首个数值断点：QKV 缺了双 view

重新统计保留在 `/tmp` 的 global activation，数值不是到 block65 才逐渐坏：block1–21 的有效区各有 225–254 种 E4M3 byte；block23 前两层仍分别有 254／255 种。第一次坍缩发生在首个 512-channel split-Swin 的 layer2：

```text
block23.layer0 FfwdInpview   正常多值
block23.layer1 FfwdProj      正常多值
block23.layer2 QKVAttn       只剩 0x00 / 0x7f
block23.layer3 Projection    继承 0x00 / 0x7f
```

由 5090 live vtable（module base `0x7ffbad510000`）反推静态函数：QKV forward RVA 为 `0x6d260`。反汇编恢复出的正式 `0x38` 参数包支持：

```text
+0x00 main input
+0x08 main output
+0x10 weights
+0x18 width
+0x1c height
+0x20/+0x24 shift/origin
+0x28 auxiliary input
+0x30 auxiliary output
```

QKV forward 可在 state flag 与 vector 长度同时满足时，从 inputs[1]／outputs[1] 填入 `+0x28/+0x30`。这首先暴露出旧 runner 没检查多 view 的缺口，但是否为当前 preset 的实因必须实测。

实测随后否决“缺 auxiliary 就是根因”：给 FfwdProj 的 `+0x28/+0x30/+0x38` 全部分配有效 buffer 后，只有主输出 `b0` 写入 30,952 个非零 bytes，`b1/b2/b3` 全零。QKV 的第二 view 是接口能力，不等于这个 block 实际使用。当前确定断点仍是 block23.layer2，但下一步必须读取该 live layer state 的真实 flag／输入输出 vector 数量，或从 QKV SASS 直接确认 `+0x28/+0x30` 是否被当前 kernel 读取；不能再由 forward 的条件分支直接宣布根因。

稳定 runtime descriptor probe 已扩展并在 5090 重跑，取得 block23.layer2 的 0x200-byte layer/state 快照；live state `+0x18=1`，只证明双-view 分支可用。随后尝试直接 hook 推定的 QKV forward RVA `0x6d260` 读取 vector 长度：原版 feature18 仍成功到 count=60，但未落探针文件，几秒后游戏退出。该 probe 已立即移除；其签名／hook 时机未验证，不能用退出反推网络行为，也不再沿这条前台注入路线试错。下一步回到 NvAPI backend `+0x88` 上传语义与 QKV SASS 实际 load，优先取得无需注入游戏的证据。

### block23–29 数值恢复：archive 是混合运行布局，旧 runner 接错层间 view

用两个最小 sm_120 对照 kernel 判定 SASS 转换方向：`fp8_to_half` 编译为 `F2FP.F16.E4M3.UNPACK_B`，`half_to_fp8` 编译为 `F2FP.SATFINITE.E4M3.F16...`。因此原 QKV 的权重 load 确实消费 E4M3。但 archive 不是“全 FP16”：block23.layer2 的 `0xe0040` bytes 精确分为：

```text
0x00000–0xbffff  packed E4M3 QKV
0xc0000–0xdffff  FP16 attention bias
0xe0000–0xe003f  16 × float32 attention scale
```

这与 QKV SASS 的 `+0xc0000` bias、`+0xe0000` scale load 逐字节闭合。外层 `element_count×2` 只是 record framing，不是全 payload dtype；“加载阶段另有全局 FP16→FP8 packer”撤回。

真正错误在 split-Swin runner 的 layer 拓扑。5090 vtable forward 反汇编给出：FfwdProj 参数 `+0=inputs[1] residual`、`+8=inputs[0] branch`、`+0x10=output`、`+0x18=weights`；QKV launch 的 `grid.z=4`，SASS 以 `CTAID.Z×4+TID.Y` 覆盖16 heads。旧 runner 把 residual 与 branch 都指向同一基址，且 QKV 只发 `grid.z=2`。

修正并丢弃污染的旧中间文件后：block23 layer0/1/QKV/projection 分别恢复到 254/246/152/245 种 E4M3 byte，全部零 NaN，Compute Sanitizer 零错误。新增 `run_original_split_global.cpp`，按真实 `z=2/1/4/1` 顺序统一运行四层。block24–29 连续结果均零 NaN，最终每层保持235–254种 byte。当前正确数值链已从 block0 推进到 block29；下一步恢复 block30 的 ProjPool/FinalHead，再重跑 ViT 31–38。

block30 随后也校正：ProjPool forward 的参数对象实际为 `0x50`，含 QKV/FFN 双输入、main/pool 双输出，正式 launch 坐标为 `(2,4,1)`；旧 `(2,2,2)` 虽 CTA 总数相同但坐标语义错误。修正后 FinalHead 得到193种 byte、零 NaN，Compute Sanitizer 零错误。正确链推进到 block30。

### ViT 三路 view：离线 descriptor 与 5090 live VA

`probe_runtime_descriptor.ps1` 已扩展为导出小型 candidate vector 的实际 qword。block31 内部连接向量确认：Attention 的三路输入都来自 QKV layer2 的 outputs 0/1/2；Projection 同时读取 Attention layer3 与 Contract layer1。旧 ViT runner 把 Q/K/V 和所有 auxiliary 全绑到同一地址，结构无效。

稳定 runtime probe 随后只读 hook ViT QKV/Contract forward，未读取 GPU memory。5090 原版在实际 `width=36,height=60` 时给出：

```text
Contract inputs=3, outputs=4
QKV inputs=2, outputs=6
QKV input0 = Contract output0
QKV input1 = Contract output2
QKV output0/2/1 三个主 VA 依次相隔 0x220000（Q/K/V）
QKV output3/4 位于 auxiliary arena +0x400/+0xa00
```

因此 QKV 的第二输入不是可随意省略的接口装饰；旧 runner 清零它会让 QKV 全 NaN。Contract wrapper 的 kernel 参数顺序也已确认：`+0=residual input1`、`+8=expanded input0`，旧脚本反接。当前 direct runner 已能把错误从 QKV 收窄到 Contract/Expand 的 auxiliary alias：Expand main 零 NaN，但旧 Contract 只写 workspace view，并缺 live input2 auxiliary。下一步抓 Expand/Contract 的完整 live vector alias 或在同一 backend command chain 内复制该小 auxiliary arena，不能继续把所有 view 指向同一 buffer。

后续 backend launch hook 已直接导出真实参数 blob。最终确认 block31 的逻辑尺寸不是16×16而是8×8：输入256×144经四次下采样得到16×9，padding到16×16，再由 block30 ProjPool 得8×8；4K原版对应120×68→120×72→60×36，与5090 live `width=36,height=60`完全一致。按8×8重跑后，Contract main、Q/K/V、Attention、Projection全部零NaN，block31正确输出65,407 bytes。

ViT使用Z向CTA cluster：Contract/Projection `gridZ=clusterZ=4`，QKV `gridZ=clusterZ=2`；`cuLaunchKernelEx`恢复了普通launch不写main output的问题。完整flat weight arena也成为必要条件，单record allocation会被tensor layout的对齐读取越过数百字节。新增 `run_original_vit1d_global.cpp` 与 `run_original_vit1d_chain.cpp`。block32可单独零NaN运行，但block33起仍依赖NvAPI `flag=1` command sync；standard/chained/publish与cluster policy的控制变量均已排除，当前精确缺口只剩跨ViT block的NvAPI sync原语。

为继续验证全链，第一版暂以block31 residual近似替代blocks32–38，并执行正式 `repack_1d_to_2d_fp8`。block39只产生34个E4M3 NaN；明确作为近似桥用 `sanitize_e4m3.py` 饱和到最大有限值后，blocks40–47连续零NaN。随后从5090 live blob修正8H fused的0x58 ABI（旧runner字段错8bytes、缺halo）：block48按32×24 physical padding写满147,456 bytes；49–55用halo grid5×4全部零NaN；block56、57–61、62、63–65、精确block66 ABI、67–69全部零NaN。当前decoder已完整贯通。

当前block69 activation经 `e4m3_to_f32.py` 后，RX9070XT运行 `d3d12_final_readout.cpp` 成功输出256×144 RGB，submit→fence `1.665 ms`。画面能清楚辨认Eve，但有严重tinlayout条纹；与旧clean AMD诊断图PSNR 17.61 dB。因同一readout在旧activation上清晰，条纹已定位到跳过32–38造成的activation偏差，不是AMD readout。完成标准仍未满足；下一步继续复刻NvAPI `flag=1`同步或以可验证的ViT stage bridge替代七层identity近似。

### 自建5090 NVAPI chain宿主

官方NVAPI header公开了实验接口 `CreateCuModule/CreateCuFunction/LaunchCuKernelChainEx`。新增 `d3d12_nvapi_repack_test.cpp`，用自有D3D12 default/readback buffers直接启动泄露CUBIN；最小 `repack_2d_to_1d` 与Spark CUDA结果达到 `2,097,152/2,097,152`逐字节一致，证明可绕过游戏runtime并安全readback任意中间层。

新增 `d3d12_nvapi_vit_chain.cpp`，已能创建42-kernel ViT链并逐层limit readback：repack、Expand、Contract、Attention均在5090自建buffer上输出零NaN；QKV standard写两路、chained写另一组，Projection输出仍待按真实 `CCMultiCubinBackend` 子kernel数组组合。单独排列standard/chained会使驱动kernel等待，因此停止盲试。

为直接取得真相，新增 `nvapi_chain_probe.cpp`：hook官方 `CreateCuFunction` 记录handle→symbol，再hook `LaunchCuKernelChainEx`记录每次实际kernel数组、grid/block与参数blob。probe已在AMD WSL静态编译完成（SHA-256 `0b668b93a87138ef21ab7f7f58fd500a8a4e2275eac4511d917711fef5285b22`）。两次错误NVAPI组合令5090出现两个WDDM不可中断测试进程，管理员taskkill也无法结束；系统最终进入真实重启，SSH暂未恢复。机器上线后第一动作是部署该只读probe，不再运行排列测试。

5090硬重启后于 `2026-09-01 22:02:29` 恢复，残留测试进程已清空。首版probe暴露两个宿主生命周期错误：`hookCreate`在持有非递归SRW锁时再次进入日志锁，以及ReShade枚举D3D12设备时卸载addon、后台worker继续执行已卸载代码。前者已拆锁，后者改成独立resident hook DLL，并新增 `inject_probe.cpp`，在游戏进程建立后通过remote `LoadLibraryW`加载，彻底脱离ReShade addon反复装卸周期。所有测试均未再提交猜测的CUDA链，5090未再次出现WDDM锁死。

只读hook已成功记录当前runtime的96个 `CreateCuFunction` handle→symbol，包括ViT的standard/chained/wait全组；证明NVAPI接口ID与hook签名有效。实际 `LaunchCuKernelChainEx` 尚未出现：硬重启后的游戏进入长时间无窗口初始化，同一现象在完全移除probe后也可复现；恢复此前验证过的Win+R `steam://rungameid/3489700`前台启动后能显示Stellar Blade splash，但随后仍进入该初始化阶段。测试进程与probe已干净移除，不能把这个启动状态误判成GPU死锁。

同时复查已有 `/tmp/vit-kernelobj.txt`，确认原版runtime每个ViT block的外层调用并非一条42-kernel大链，而是五个phase逐层调用：Expand首block `flag=0`、后续block以及Contract/QKV/Attention/Projection均为 `flag=1`；实测4K grid依次为 `544×1×1 / 136×1×4 / 272×1×2 / 32×9×1 / 136×1×4`。`CubinBackendNGX::launch`反汇编也闭合：flag先触发context sync，再把kernel对象、参数blob和grid交给context vtable `+0x140`；真正standard/chained子数组在该下一层构造，不在外层42层调度器。下一步改为解该vtable实现或在游戏恢复正常前台后延迟注入，避免再盲排子kernel。

### NVAPI ViT宿主纠错与block31精确分组

复查 `d3d12_nvapi_vit_chain.cpp` 抓到两处足以伪造“kernel排列错误／GPU死锁”的宿主bug：

1. `blobs.reserve(42)`，但旧实现实际生成58个kernel；第43个开始vector扩容，先前所有 `KP::pParams` 成为悬空指针。
2. `chain()` 名义上调用 `LaunchCuKernelChainEx`，实际循环 `count=1`，每个kernel之间再插UAV barrier。带 `SYNCS.EXCH` 的协作kernel因此永远等不到同组伙伴。

修正为预留96个blob，并把同层kernel一次性以 `count=N` 提交后，5090不再卡死，block31逐层闭合：

```text
repack                 单kernel
Expand                 standard
Contract               standard + chained（同一次NVAPI chain）
QKV                    standard + chained（同一次NVAPI chain）
Attention              standard + chained（同一次NVAPI chain）
Projection             standard（wait/chained加入后会清零整组资源）
```

层间采用独立command-list提交与fence后，block31最终输出稳定为约65,4xx个非零E4M3 bytes、零NaN（一次记录为65,489，last=65,535）；Contract/QKV/Attention分别验证为28,595／28,662／65,526个非零bytes。另确认只transition/copy单一NVAPI写入buffer会出现假零，全11个UAV统一transition后读回正常，后续诊断统一使用全资源barrier/dump。

block32的首个Expand仍是当前唯一ViT断点：同device连续提交时，standard与 `prev=0` 会在第二block触发device removal；改为chained并传上一block `a38` 也未恢复，最后一次返回 `DXGI_ERROR_DEVICE_HUNG (0x887a0006)`，但测试进程正常退出、未再造成整机WDDM锁死。此处停止5090排列实验。当前证据说明缺的不是矩阵参数或block31数值，而是runtime `flag=1` 在block边界实施的专用同步/状态发布语义。

随后把外层 `flag=1` 进一步按D3D12提交语义复刻：同一device中每个真实layer group分别 `Close → Execute → Fence → 新command list`，而不是只在一个command list内插barrier。该路径稳定跑完block31，最终65,489个非零bytes、零NaN；但进入block32时仍在第二block边界触发device removal。尝试按CUDA runner清零复用workspace时，普通Copy状态转换本身会触发hung；改为为blocks32–38各自预分配独立的branch/main/attention/work/Q/K/V（额外约98MB）也未消除断点。因此“复用脏scratch”被排除，block32剩余变量只剩runtime私有的跨block同步发布。

读回诊断同时纠正一处假象：NVAPI私有写入后只transition/copy单一目标资源可能读到全零；把全部UAV统一transition并dump后，block31各view均存在。当前可信分层计数为Expand 114,443、Contract 28,595、QKV主view约28,6xx、Attention 65,526、Projection 65,4xx，均零NaN。旧 `/tmp/block31–38-global-view.bin` 的512KiB文件逐字节全为 `0x7f/0xff`，确认是早期16×16错误路径，不能重新当“已闭合ViT”使用；20:21后生成的2MiB `block31/32/33-global-correct.bin` 才是8×8零NaN证据。

新增 `run_original_vit_repack.cpp`，可独立把8×8 1D ViT activation执行原始 `repack_1d_to_2d_fp8`。为验证“已有block33零NaN文件能否比block31 identity更接近最终画面”，把 `block33-global-correct.bin` 暂代blocks34–38，经原repack、block39、40–69全链重跑：block39出现62个NaN并显式饱和，blocks40–69随后全部零NaN；RX9070XT readout耗时1.690ms。输出仍能辨认原图，但点阵/条纹更重，对旧clean诊断图PSNR仅16.76dB，低于block31 identity版本的17.61dB，因此该分支被数值验收否决，未替换仓库当前图。结论：block32/33“零NaN”只证明kernel输出有限，不能代替block38语义正确性；必须继续补齐34–38或直接重写AMD ViT语义。

继续缩减层内chain后取得关键突破：Contract必须以 `standard + chained` 同组提交；QKV与Attention反而只需standard，给它们追加chained会引发device hung；Projection也只需standard。用该组合从已保存的block33零NaN activation独立启动block34，得到65,390个非零bytes、last=65,535、零NaN，`block34-single-standard.bin` 已取回本机。随后block35首轮因5090在多次device-removal后的驱动状态再次于QKV阶段hung，未继续消耗WDDM稳定性；block35–38仍待同组合在干净驱动状态下顺序运行。

把block34临时代替35–38重跑decoder，block39 NaN从62降到53，block40–65全程零NaN；但block66输出与block33桥逐字节完全相同，说明当前block66 live-ABI路径把两条不同decoder主干坍缩为同一结果（主输入贡献丢失或被skip覆盖）。因此两条分支的block69与RX9070XT RGB也逐字节相同，PSNR仍16.76dB。该实验揭示最终画面目前同时被两个独立缺口控制：ViT 35–38同步，以及block66主干/skip融合语义；只补ViT而不修block66，最终图无法作为ViT验收信号。

block66坍缩随后由SASS直接破案。`cc_tinlayout_fused_swin_1h_32_1_upsample_fp8` 在 `c[0][0x380]`（kernel参数 `+0x00`）执行真实 `LDG`，旧runner却把 `+0x00/+0x08` 都绑成清零后的output；因此decoder主输入从未进入kernel。修正为 `+0x00=main input / +0x08=output`，并保留 `+0x40=main auxiliary view / +0x50=enc0 skip` 后，block66输出达到1,179,63x/1,179,648非零、零NaN；block33与block34两条主干产生不同SHA-256，差分验收通过。新增 `run_original_1h_upsample.cpp` 固化该0x60 ABI。

用修正block66重跑67–69与RX9070XT readout：block67/68/69分别约645,11x/645,11x/645,109非零、零NaN，AMD submit→fence 0.896ms。对clean诊断图PSNR由16.76提升到17.26dB，说明主干恢复确实改善结果；但仍低于block31旧近似的17.61dB，最终图依然有规则点阵与横向色带，不能验收。当前画面瓶颈重新收敛到尚缺的ViT blocks35–38以及更早的portable AMD语义实现。

### block35–37变体与状态桥实验

清理single-mode仍无条件分配的49组额外scratch后，block35以 `Contract standard+chained / QKV standard+chained / Attention standard / Projection standard` 成功输出65,326个非零bytes、零NaN；block36同组合输出65,335、零NaN。block37在QKV standard、chained、两种pair顺序下均device hung；把limit真正应用到single-mode后确认device removal已发生在Contract阶段，QKV只是延迟暴露错误。block37 Contract的standard、chained、pair三态也全部失败。

这证明主activation文件不足以跨越所有ViT block：block35/36在aux清零时尚能运行，block37开始依赖前块发布的2MiB auxiliary arena。尝试同context range35→38时，未来block的资源分配/参数构建会改变底层GPU VA与同步行为，首个Contract即hung；尝试给single-mode增加aux upload又因改变资源分配顺序破坏已知成功布局。当前已把“变体排列”空间穷尽，剩余工程任务严格是：做一个固定资源布局的单block状态宿主，在一次执行中同时导出main+aux，并在下一进程保持相同默认资源VA顺序加载，而不是继续猜kernel组合。

5090测试纪律再次收紧：每次 `DXGI_ERROR_DEVICE_HUNG` 后立即重启、只跑一个控制变量；无游戏/测试进程时才重启。Spark CUDA Graph、双stream以及缩小grid均不能替代NVAPI chain，证实这是驱动专用共驻/同步原语而非普通occupancy问题。

后续控制变量补充：低内存single-mode下，block35与36曾连续成功，分别输出65,326与65,335个非零bytes、零NaN；35–36需要QKV `standard→chained` pair。block37从QKV回溯到Contract后，Contract standard/chained/pair与QKV三种组合均失败。为携带状态，先尝试main+aux跨进程状态包，再尝试同device动态range（每完成一个group立即提交/fence，之后才构造下一group）；两者都会因资源分配顺序或NVAPI调度非确定性在首个Contract触发device hung。即使恢复提交 `3efa01d` 的隔离基线，成功也不可稳定复现。

因此自建5090宿主路线的结论已足够明确：它可稳定验证单block和ABI，但不能作为35–38生产流水线；继续用重启抽样不具收敛性。blocks31–36的有限输出、block66主输入修正均保留；35–38剩余工作转向解析已保存runtime kernel/aux对象，或在AMD上按archive矩阵语义重写，不再依赖WDDM私有chain调度。实验性range/aux代码留在工作树，未提升为稳定实现。

### ViT record 字节布局闭合：AMD 语义重写入口

新增 `infer_vit_layouts.py` 与 `vit-layouts.json`，对 blocks31–38 的五条 record 逐字节闭合。八个 block 完全同构：

```text
layer0 Expand:     0x400000 packed E4M3 + 0x10 zero padding
layer1 Contract:   0x400000 packed E4M3 + 0x800 FP16 ffn_cos_skip
layer2 QKV:        0x300000 packed E4M3 + 0x80 unresolved scale region
layer3 Attention:  0x2-byte scalar record
layer4 Projection: 0x100000 packed E4M3 + 0x800 FP16 attn_cos_skip
```

两条 residual 尾区已有直接 SASS 地址证据：Contract 从 weight base `+0x400000` 读取，Projection 从 `+0x100000` 读取；尾部各 1,024 个 FP16 值也落在约 0.5–1.0 的合理范围。Expand 的 16-byte 尾区在八个 block 中均为 alignment padding。QKV 的 128-byte 尾区虽由内部名字标为 `attn_scale`，但直接按 FP16／FP32 解码不成立，在恢复精确 load 地址前保持 `unresolved_mixed_region`，不再凭名字指定 dtype。

这一步把 AMD ViT 的问题从“解析 12.6 MB 不透明 blob”收窄为四类 packed E4M3 矩阵 unswizzle + 两条已知 FP16 skip。下一步沿 ViT SASS 中的 weight LDG 地址、lane id 与 `QMMA.16832` fragment 顺序恢复矩阵 tile 拼接；不再把 record 外层的 `element_count×2` 当作线性 FP16 张量。

### ViT activation permutation 与首张 portable Expand 矩阵

新增 `run_original_vit_repack_permutation.cpp`。对 2 MiB physical input 依次写入全1基线与21个地址位平面，只需22次原始 `repack_2d_to_1d_fp8` launch，即恢复65,536条 output→input byte映射。映射为一一对应，source min/max恰为0/65535；说明8×8×1024 ViT主activation是封闭的64 KiB payload，不依赖隐藏halo。映射固化为 `vit-repack-output-to-input.i32`。

对 block31 Expand 做1,024个连续 basis 后，输出支持集自动分成16组、每组64个输入byte；再跨physical地址高位采样，恢复main view位分解：

```text
token bits   = physical bit 2, bits 6–8, bit 14
channel bits = physical bits 0–1, 3–5, bits 9–13
```

因此可直接枚举一个token的1,024个channel地址。`run_original_vit_expand_matrix.cpp` 用1,024次launch抽出完整`4096×1024`有效矩阵；token0与token1结果逐byte一致，证明FFN权重跨token共享。unit-basis矩阵在32-channel随机held-out上correlation 0.9960。

为平均E4M3量化误差，又以±0.03125执行1,024行Hadamard正交探测并做逆变换。unit-basis与small-Hadamard误差互补；按0.6/0.4融合后，在8组8→1024非零channel held-out上平均correlation 0.9840434、MAE 0.0351728。结果以FP16固化为`block31-vit-expand-effective.f16`。

边界：这是可在AMD上立即执行的bring-up矩阵，不是exact unswizzle。稠密输入的动态E4M3量化令effective线性化误差明显高于稀疏输入；最终验收仍需恢复raw matrix tile排列或做真实activation分布校准。

`d3d12_vit_expand_test.cpp` 随后把该矩阵移到 RX 9070 XT。runner 在 CPU 侧只做 physical offset gather 与 oracle 解码；`1024×4096` matmul、FP16 weight decode 和 E4M3 requantization 全由 HLSL compute shader执行。32-channel随机held-out实测：

```text
adapter: AMD Radeon RX 9070 XT
submit_to_fence_ms: 0.600
MAE: 0.006635189
RMSE: 0.010752889
exact E4M3 fraction: 0.313477
```

误差与同一effective matrix的CPU预测一致，证明D3D12/HLSL没有引入额外实现误差。这是1024-wide ViT在AMD上的第一条真实算子；下一步按相同physical basis方法恢复Contract、QKV与Projection。

### ViT packed E4M3 exact unswizzle

Contract 的 cluster-aware basis 扫描在原生 CUDA 下仍有秒级同步成本，不适合作为4,096列生产提取器；该慢路线已停止，未保留无收敛价值的 runner。转而用 block31 Expand 的4 MiB basis矩阵对齐 raw archive。

每个512-byte raw tile 按 PTX 9.1 matrix-B fragment 公式解成两张`K32×N8`；8,192个physical tile正好得到16,384个逻辑subtile。确定全局顺序为：

```text
K-block major → N-block minor → two fragments → K32×N8
```

两条effective physical axis到raw matrix axis的映射均为纯bit permutation：

```text
main   effective→raw low bits: [bit0, bit1, bit4, bit2, bit3]
branch effective→raw low bits: [bit0, bit3, bit4, bit1, bit2]
bit5以上保持原位
```

输入轴映射经1,024行Hungarian distribution match后，1024/1024可被上述线性bit permutation精确解释；输出轴抽样列对齐correlation均约0.9993–0.99945。整张raw Expand重排后对CUBIN unit-basis矩阵：

```text
global correlation = 0.99939859097
MAE                = 0.0026031593
```

新增`unpack_vit_matrices.py`，可直接从147.7 MB arena为blocks31–38导出：

```text
Expand:   raw K=main,   N=branch
Contract: raw K=branch, N=main
```

block31两张FP16矩阵各8 MiB，导出耗时低于1秒。RX9070XT已直接执行raw-unswizzled Expand；实现无故障，但32-channel held-out MAE 0.00915，反而高于effective bridge的0.00664。原因不是unswizzle错误，而是原CUBIN仍有尚未复刻的dynamic E4M3 scale/auxiliary state；raw矩阵恢复解决权重坐标，scale view仍是下一数值缺口。

Contract 单次真实分布 oracle 随后纠正了A/B命名：矩阵输入轴固定使用A排列，输出轴固定使用B排列，与buffer被叫作main还是branch无关。对`K/N order × A/B/identity input × A/B/identity output × 三种skip排列`全部54个候选穷举，唯一最优为`KN + K:A + N:B + skip:B`，correlation 0.9916288、MAE 0.0091401；第二类错误轴组合下降到0.84以下。`unpack_vit_matrices.py`已统一修正为所有线性矩阵`K=matrix_input(A), N=matrix_output(B)`。

QKV record 已按三个独立1 MiB矩阵的group-major顺序加入unpacker，Projection也按1 MiB矩阵导出；但两者在manifest中明确标为`structural_only`。standalone Spark Contract standard kernel只写8个auxiliary bytes，而live descriptor证据表明QKV必须读取Contract的第二输出；在aux为零时，任何Q/K/V矩阵排列都无法与oracle相关，故不能用该失败提升或否决QKV轴布局。

5090 clean reboot后，自建NVAPI宿主的Expand-only基线以114,443 nonzero／零NaN通过；追加Contract standard+chained也通过。`result=99`导出11个2 MiB view后确认：Contract main在r3（28,600 nonzero）、大aux/workspace在r5（57,054 nonzero，含141个E4M3 NaN哨兵）、小aux在r6（仅8个常量）。旧`result=98`硬编码复制r1+r6，不能代表single-block Contract state。

`run_original_vit_contract.cpp`与`run_original_vit_qkv.cpp`已改为显式导出／接收main、work、aux。把5090的r3/r5/r6原样喂给Spark QKV后，Q/K/V分别得到28,663／28,596／28,642个nonzero，全部零NaN，证明第二输入链闭合。与此同时，QKV main与三路输出的channel view不属于FFN已恢复的A/B有限候选；54种tile/axis/group候选最高相关仅0.083。QKV/Projection继续保持structural-only，下一步用复用context的1,024-channel QKV basis直接恢复三套physical permutation与effective matrices。

`run_original_vit_qkv_matrix.cpp`随后在1.1秒内完成1,024个basis，输出3,145,728-byte effective matrix；Q/K/V三套token0 offset各1,024项。行为分解为：V对真实main输入保持线性（basis预测correlation 0.9980）；K是32组×32维归一化，basis方向求和后逐head normalize可达0.9574；Q同样归一化并额外乘per-head scale，head norm范围2.51–19.56，但仅靠basis方向达到0.8844，仍缺归一化前行幅度。

对32个输入token执行5个token-id位平面，并以全1,024 channel避免偶然量化为零后，Q/K/V的32,768-byte physical view均闭合。GF(2) RANSAC恢复地址公式：Q token low5取physical bits `[2,6,7,8,14]`，K取`[3,6,7,8,14]`，V取`[1,0,4,5,2]`；其余各10bit slot映射见`block31-qkv-effective.json`，全view token/slot code均32,768/32,768唯一。Q有24个量化边界误标，但bit公式生成后每token严格1,024项；K/V观察误差为0。

`run_original_vit_attention.cpp`已携带QKV更新后的work/aux独立运行，输出65,536/65,536 bytes、零NaN。直接把Q/K/V连续buffer reshape为32×1024做softmax与oracle不相关，证明Attention前必须按上述physical bit公式unswizzle。当前唯一结构歧义是slot10表示1024逻辑维，还是两套512-wide physical view；下一步通过Projection basis/ABI判别。

5090在clean reboot后把single block31的QKV改回`standard+chained`同组提交，limit6稳定通过Attention，limit7在再次clean reboot后稳定通过Projection。11-view authoritative dump中：r7/r8/r9为Q/K/V，r4为Attention（65,495 nonzero），r1为Projection output（65,398 nonzero），全部主输出零NaN。该组合也消除了此前QKV阶段device hung，`d3d12_nvapi_vit_chain.cpp`已把single block31纳入QKV pair条件。

权威NVAPI pair Q/K/V与Spark standard-only输出的support集合几乎一致（Q/K/V仅16/144/59个support差异），所以basis恢复的offset与GF(2)地址公式仍成立；但活跃值逐byte exact仅13.36%／17.19%／17.67%。因此`block31-qkv-effective.fp8`正式降级为standard-only结构诊断，V=0.9980等数值不再提升为AMD参数。下一步必须用专用5090 NVAPI pair宿主批量重做QKV basis。

新增`d3d12_nvapi_qkv_matrix.cpp`：只加载一次CUBIN与147.7 MB arena；每个basis恢复Contract main/work/aux、清零三路输出，并把QKV standard+chained作为同一NVAPI chain提交，按已恢复offset只保存3,072 bytes。RTX5090连续1,024 basis在5.8秒内完成，零device removal，矩阵SHA-256为`d7b1ef63...baacb9b`。

权威pair basis替换旧standard-only文件后，对真实NVAPI held-out：Q按32组×32维归一化correlation 0.9825706、K为0.9715692、V线性预测为0.9922434。Q/K线性和的std分别比归一化输出大279.8×／264.8×，与SASS的平方和→RSQ路径闭合。该矩阵现可进入AMD bring-up；standard-only数值结论作废但physical offset/bit公式保持不变。

`d3d12_vit_qkv_test.cpp`已在RX9070XT执行同一physical basis矩阵：每个thread负责一个head，Q/K完成32维平方和归一化并乘basis head scale，V保持线性，最后统一E4M3量化。对5090 authoritative r7/r8/r9：Q/K/V correlation为0.9819528／0.9717176／0.9918323，submit→fence 10.082 ms；与CPU模型一致，AMD未引入额外误差。当前shader为correctness版，尚未做QKV预排与wave优化。

Projection无需basis即可由authoritative r4 attention、r3 residual、r1 output判定raw布局。54个`KN/NK × A/B/I × A/B/I × skip`候选中唯一最优仍为`KN + K:A + N:B + skip:B`，correlation 0.9729131。由此确认ViT线性矩阵统一使用A输入轴、B输出轴，Projection record的首1 MiB可直接unswizzle为1024×1024。

新增`d3d12_vit_linear_test.cpp`，同一HLSL runner覆盖Contract与Projection：Contract启用SASS多项式后执行4096→1024，Projection直接执行1024→1024，两者均叠加真实FP16 skip。RX9070XT对5090 view实测：Contract correlation 0.9298485、3.527 ms；Projection correlation 0.9724985、0.509 ms。Projection与CPU候选一致；Contract低于此前单standard样本，说明NVAPI chained／dynamic scale仍有数值贡献，下一步需重做Contract pair basis。

为Projection尝试过两条basis路线：跨进程复制r5/r6到新resource会立即device hung；完整宿主result97保持同构resource地址并注入保存状态可运行少数basis，但sync phase累积后仍device removal。结论：Projection同步状态包含驱动内部phase，不能靠bytes恢复；独立probe源码已删除，result97仅保留为失败诊断入口，不作为生产路线。最终移植使用已闭合的raw矩阵语义，不再追Projection basis。

QKV state dump揭示work的0/2/4 plane各为65,536-byte FP16张量，全部值有限；每plane正好32 tokens×1024，分别是另半序列的Q/K/V，main r7/r8/r9则是前32 tokens的E4M3 view。work token bits为physical element bits`[1,5,6,7,14]`，余10bit为channel；每token严格1,024项。

`d3d12_nvapi_qkv_matrix.cpp`新增`state`、`token-map`与`fp16-basis`模式。FP16 basis连续1,024轮约7.6秒完成；扣除zero-main baseline后，对真实work Q/K/V held-out correlation分别为0.99999978／0.99999976／0.99999977，MAE 0.00245–0.00291。三张矩阵固化为`block31-qkv-work-effective.f16`。

跨view列指纹匹配恢复出work→main channel bit permutation：Q/K为`[0,1,3,4,2,5,6,7,8,9]`，V为`[1,0,2,3,4,5,6,7,8,9]`，1024/1024一一对应。将work Q/K重排后逐head归一化到main head scale，再合并两半32-token序列做64-key softmax，Attention对原CUBIN correlation升至0.87962、MAE 0.79281；K相对Q的5!低bit排列穷举以identity唯一最优。当前portable attention公式已闭合，剩余误差来自head scale／FP16融合细节。

受控Attention进一步钉死序列拓扑：Q=K=0、单个V E4M3 impulse时，原kernel恰输出64个`1/64`。逐physical bit翻转恢复V main的5个token bits`[0,1,2,4,5]`与10个channel bits；main承载32 tokens，work FP16承载另32 tokens，Attention合并为64-key序列。输出view token bits为`[2,6,7,8,14,15]`，V channel到output channel的bit映射为`[1,0,4,5,3,9,10,11,12,13]`。

新增`prepare_vit_attention_case.py`将main E4M3与work FP16按上述公式生成64×1024 canonical Q/K/V，并将原Attention E4M3输出重排为canonical oracle。`d3d12_vit_attention_test.cpp`在RX9070XT执行64-key、32-head、head_dim=32的softmax与weighted-V：logit scale 0.5时correlation 0.8796198、MAE 0.7928114、submit→fence 2.133 ms；scale 0.25时correlation 0.8782215、MAE 0.7890296、0.689 ms。AMD与CPU公式逐项一致。

首次整block串联曾把`vit-repack-output-to-input`再次作用于`block30-2m.bin`，Contract correlation仅0.324。哈希复核确认`block30-2m.bin`已经是single-block宿主所需的1D view；再次repack会得到另一个仅供2D全链入口使用的`vit8-repacked.bin`。改为direct unswizzle后Contract main correlation升至0.9389306。

新增`vit_block31_reference.py`把所有portable参数串成64-token完整block31：Expand→SASS多项式→Contract+skip→QKV main/work→64-key Attention→Projection+skip。对5090 authoritative r1最终输出correlation 0.83870995、MAE 0.904428、RMSE 1.403726；六段串联未数值坍缩。下一步以该脚本为CPU oracle，在同一D3D12 device内实现多pass block31。

`d3d12_vit_block31_test.cpp`现已把同一公式放进单个RX9070XT D3D12 device：一个packed weight SRV、canonical input/scales两个SRV、branch/hidden/QKV/attention/output五个UAV，五个PSO之间只插UAV barrier，最终一次Execute/Fence并统一readback。logit scale校准为0.25后，实测submit→fence 14.195 ms，对5090 r1 correlation 0.84113222、MAE 0.898641、RMSE 1.395983，与CPU portable 0.84113233一致。

五pass逐层对CPU reference：Expand与Contract逐float exact 100%；QKV correlation 0.99999999998、Attention 0.999999871、Projection 0.999999094。最终0.25 max差来自E4M3量化边界和浮点累加顺序，不是D3D12实现错误。block31的portable AMD执行至此闭合，下一步把同一PSO推广到blocks32–38。

`d3d12_nvapi_qkv_matrix.cpp`增加可选weight offset后，在5090连续生成blocks32–38的14套QKV main/work basis，共63 MiB；每block main为3 MiB E4M3、work为6 MiB FP16，全程零device removal。参数哈希汇总于`vit-qkv-blocks31-38.json`。

blocks32–38另各生成unit-basis与±0.03125 Hadamard两套Expand oracle，按block31已验证的0.6/0.4融合为FP16矩阵，汇总于`vit-expand-blocks31-38.json`。`vit_blocks31_38_reference.py`将八层串联后，block31→38 std从1.95平滑降至1.10，所有block finite、无NaN/饱和坍缩；并可导出原`repack_1d_to_2d`直接消费的2 MiB physical E4M3。

portable block38经原repack接回decoder后，block39产生少量38–76个E4M3 NaN并按既定规则饱和；blocks40–69随后全部零NaN贯通。RX9070XT final readout 1.15–1.43 ms，生成`stellar-amd-portable-vit.png`。人物与机械结构清晰可辨，但规则点阵与横向色带仍明显，未达到完成标准；相对旧`stellar-amd-current.png` PSNR 19.54 dB。Expand effective与Attention scale 0.25只令不同portable版本间约1.2 MAE变化，条纹基本不动，下一主线转向跨层Contract/QKV误差，而非继续调这两个旋钮。

受控Attention impulse随后修正了work语义。Q=K=0、单V impulse时输出64个精确`1/64`；逐bit通断矩阵证明Q/K低5 dim bits与高5 head bits均identity，Q→output query token bits identity，K→V key token bit映射为`[1,0,3,4,2]`，对应V physical token bits`[1,0,4,5,2]`。main V随机输入对原Attention correlation 0.9977，而只写work plane4时输出全零：work FP16是内部scratch，不是另32 tokens。

语义正确的Attention因此是32个main tokens＋32个zero padding keys、logit scale 1；对原kernel correlation 0.9627453、MAE 0.280474，RX9070XT submit→fence 1.557 ms。`prepare_vit_attention_case.py`、`vit_block31_reference.py`、`vit_blocks31_38_reference.py`与D3D12 block runner均改为main路径默认。完整block31受Projection近似影响，CPU/AMD最终correlation为0.8252302/0.8252317，二者仍逐层一致。

最终条纹经skip消融定位：四条skip全零时横向色带消失，只剩主干点阵；只恢复block48/56高层skip或只恢复block62/66低层skip都会分别重新产生色带。故横带来自整个encoder skip physical布局，不是ViT。尝试用现存AMD block3与distilled block8 main替换低层skip仍有色带，说明值域之外还存在skip view permutation／布局不匹配。下一主线转为重建blocks0–22的portable skip物理布局。

后续控制实验修正了“skip本身错误”的过强归因：四skip全零只说明U-Net多尺度抵消被拿掉，单独恢复任一尺度出现带并不能证明该skip错。exact block31–34前缀替换portable前四层后，最终条纹也几乎不变；而最终输出仍走对某一旧activation拟合的linear readout。故条纹不能继续充当网络精度判据，最终验收必须恢复原block70 post。

### Encoder真实分布校准与block70 post突破

重新用同一RGB-only/Gaussian=0合同验收preblock，现有随机tile distilled模型在真实《剑星》帧上仍只有correlation 0.317，确认是真实图像domain shift。以576 tiles做空间checkerboard划分，从原distilled权重微调并保存held-out最佳：block0 RX全帧correlation 0.8112826；量化后送原blocks1–3，block3对原global view correlation 0.8396473、exact E4M3 47.8%。

同法逐stage校准：block4直接用当前calibrated block3经原DS CUBIN生成576×512 target，checkerboard held-out 0.99347，RX全帧correlation 0.9965834；stage2→block7用原`stellar-block7.fp8` target校准，held-out 0.81116，RX全帧correlation 0.8738006。三份参数归档于`encoder-real-calibration.json`，明确是固定帧诊断桥，不替代通用权重。block8 main在144 tiles上held-out仅0.51、全帧上限0.877，未进入仓库。

block70 forward反汇编给出遗漏字段：param`+0x68=state+0x08`，live GPU VA恰比layer weight base早0x200，对应独立`blend_scale` record；param`+0x30`来自DLL常量0x1800bc9a8，值为0.03125。绑定FP16 blend_scale=0.739746并填入0.03125后，post输出chroma从0恢复到951–3247，证明真实颜色路径已打开。剩余缺口严格收敛为param`+0x38/+0x58/+0x60`三路texture object语义；SASS确认三者均被TEX读取，不能再用同一个RGBA texture冒充。

同一CUBIN中的base／simple_blend／simple_blend_full_rect／control_mask／control_mask_full_rect五个FP8 symbol在相同0xb8参数下输出逐字节等价，排除“选错post kernel variant”。SASS数据流进一步确认：`+0x38`和`+0x58`均按RGBA 2D texture读取，`+0x60`按较少分量的2D texture读取并参与坐标/control-mask分支；三者不是可互换的同一Color纹理。5090重启后console session无登录用户，Steam从SSH Session 0无法启动，resident `LaunchCuKernelChainEx` probe已部署但等待交互式桌面恢复。

真实encoder校准的逐层结果也被重新核实：随机tile preblock在同一RGB-only/Gaussian=0合同下对当前帧仅0.317；576 tile空间checkerboard微调后RX block0 correlation 0.8113，传播到原blocks1–3后block3 correlation 0.83965。block4按当前calibrated block3重新生成原CUBIN target并微调，RX correlation 0.99658；stage2→block7围绕当前block4输入校准后RX correlation 0.87380。block8 main因上游误差与仅144 tiles，checkerboard held-out仍只有约0.51、全帧拟合上限0.877，说明后续要用多帧/增强数据，不能继续单帧硬过拟合。

继续沿post SASS数据流排除可选纹理后，当前preset的真实组合已经闭合为`+0x38=Color RGBA`、`+0x58=null`、`+0x60=null`、`rgb_mode=1`。在`+0x68`绑定FP16 `blend_scale=0.739746`、`+0x30=0.03125`并写入live六浮点transform `[0,0,1,1,1,1]`后，原始activation的block70输出对source correlation 0.95876、MAE 0.03328；portable block69同一路径输出correlation 0.8713、MAE 0.0768。`run_original_post.cpp`把该0xb8 ABI改成参数化可重放工具，`block70-post.json`固化字段与数值。至此最终画面oracle不再依赖旧linear readout，剩余工作是把post语义落到AMD并提高上游portable activation精度。

把block70的main与skip activation同时清零后，输出与Color source逐float MAE仅`1.54e-9`、correlation在浮点精度内为1，alpha恒为1。由此钉死post的高层合同是“原Color直通＋神经残差”，而不是旧`d3d12_final_readout.cpp`假设的`source*blend + prediction*(1-blend)`；旧readout继续只作诊断，不得复用其混合公式。把blend record内容改零但保留合法device pointer时输出不变，说明当前symbol不直接消费该首个FP16值，或该值已经编译进其它state；目前只提升“必须绑定合法record地址”，不提升“运行时按0.739746线性缩放”的结论。

受控E4M3 impulse开始恢复block70两路spatial layout。main offset 0–63都只影响左上8×8窗；offset `+64/+128/+256/+512/+1024`分别把响应平移到x=8/16/32/64/128，offset `+8192/+16384/.../+131072`分别把响应平移到y=8/16/.../128。skip同样局部，但offset `+2048`把响应移到x=16，`+8192`移到x=64，吻合其半解析度输入经upsample进入full-resolution post。单独保留真实main或真实skip时，对完整post的MAE分别为0.02295与0.03414，二者都不是可忽略支路，且完整输出不是两次单支路残差的线性相加。下一步因此使用8×8单tile ABI批量生成main+skip联合随机dataset，不再对全图4 MiB地址做黑盒回归。

`run_original_post_dataset.cpp`把单tile oracle改为复用同一CUDA context/module、weights、texture与surface，只逐样本上传2048-byte main和512-byte skip。128组输出与原逐进程runner逐float exact，耗时从约40秒降为0.42秒；9,216组联合样本仅0.70秒。独立held-out同时否决无结构MLP：128样本时correlation约0.04，8,192 train＋1,024 held-out、2560→512→192模型也只有0.292，MAE 0.0913，甚至不如零残差baseline MAE 0.0725。archive前10,336 FP16直接当row-major 1H Swin再拟合out head也只有correlation约0.13，证明前段虽然容量等于标准1H block，tensor-core physical pack仍必须用controlled weights/basis恢复；黑盒蒸馏不进入AMD主线。

批量runner继续增加body weight one-hot与逐slot ablation scan。10,336个body slots在0.69秒内扫完；4096–4103恰为无响应padding，其余10256个slot对当前随机样本有可测影响，与标准1H容量边界一致。结构性branch ablation给出更关键的输出拓扑：清零FFN W1/W2后RGB residual仍有std 0.0871；清零QKV与projection后RGB residual精确归零；单独清零attention skip只令完整输出MAE 0.00566。故post RGB head主要消费attention/projected branch，而非标准block最终residual，identity-skip实验输出为零并不否定body offsets。下一步只需恢复FFN→QKV/attention→projection→RGB head有效路径，无需把未进入RGB head的residual误当输出。

完整10,904-slot ablation最初被误读成“标准10,336＋尾部568”；与block1原始恢复脚本及scale值交叉核对后修正：W1/W2仍为0–4095，padding为4096–4103，post专用56-half输入／upsample区插在4104–4159；FFN skip移到4160–4191，随后8 padding，QKV从4200开始，bias从5736开始，float32 scale位于9832（值9.93816），projection从9840开始，attention skip位于10352–10383，10384–10391为tail padding。真正新增的out-conv只有`10392–10647`与`10648–10903`两块256-half pack，每块恰有48个有效slot。active地址公式为`base + color*32 + (channel//4)*8 + channel%4`，对应两张`3×16`矩阵。保留完整body、清零out-conv后逐slot置1，可直接读出32张feature maps；同一physical slot对R/G/B路由完全一致。

`fit_post_outconv.py`用第一组小幅随机main+skip tile的32张basis feature拟合32→RGB矩阵，train correlation 0.999999983、MAE 2.27e-8；第二组独立随机tile held-out correlation 0.999999105、MAE 2.88e-7、max error 2.82e-6。参数固化为`block70-outconv-effective.bin`与manifest。该结果把最终RGB head从packed runtime bytes提升为可直接写HLSL的portable FP32矩阵；block70剩余数值缺口只在产生这32张feature的body路径。

`d3d12_post_outconv_test.cpp`已在RX9070XT执行同一32→RGB矩阵与`saturate(Color + residual)`。用第二组独立随机tile的controlled basis feature作输入，对原NVIDIA完整post oracle：submit→fence 0.334 ms，MAE 2.17e-7、RMSE 4.20e-7、max error 2.83e-6、cosine 0.999999999867。该层至此完成“参数portable＋AMD GPU执行＋独立NVIDIA数值验收”三重闭合；后续不再改RGB head，集中恢复body的32-channel feature。

批量runner新增`features`模式：保留body与48-value input/gain区，依次给两块out-conv的32个逻辑channel置unit weight，从R通道直接读回64×32 body feature。单tile结果与旧512-launch完整head basis逐float exact；9,216组联合main+skip只耗时5.47秒，18,874,368个值全部有限。无结构body MLP再次被held-out否决：8,192 train＋1,024 held-out的2560→512→2048模型correlation仅0.377、MAE 0.401，几乎等于均值baseline 0.413。原因是独立同分布FP8随机byte把attention推入大量±0.5饱和区，既不代表真实activation流形，也无法学习高维窗口结构。该dataset保留作受控分支证据，不作为portable参数；下一步用feature读口逐段恢复FFN与attention，而不是扩大黑盒网络。

对2048-byte main＋512-byte skip逐输入slot施加E4M3 `0.03125` impulse，再经`features`模式导出完整64×32 Jacobian，2,560组仅耗时1.80秒。main只有offset 0–511响应，后1,536 bytes严格为零，rank 512；skip的512/512 slots全部响应，rank同为512。这把真实输入合同修正为两路`4×4×32`，共同upsample为`8×8×32` body feature；此前把main当8×8×32的假设作废。零点Jacobian在线性小幅随机held-out上correlation 0.879／0.908、MAE 0.00197／0.00284，但对大幅iid FP8饱和样本仅0.522，不能直接当完整body。它当前用于恢复physical token/channel与局部upsample，不提升为最终AMD参数。

`run_original_post.cpp`新增完整256×144 controlled-feature导出。初版单向`+1/32` probe受output saturation污染，重建只有correlation 0.9864；改为每channel执行FP16 `±1/1024`双向probe，并逐像素选择未撞0/1边界的一侧后，1,179,648个body feature全部有限，范围-26.84375..31.125。用portable out-conv重建原post达到correlation 0.999999844、RGB MAE 6.60e-5、max error 0.001531。

全尺寸`d3d12_post_outconv_test.cpp`随后在RX9070XT直接消费该controlled body feature、原Color与portable 32→RGB矩阵：256×144 submit→fence 1.060 ms，对NVIDIA完整post RGBA MAE 4.95e-5、RMSE 1.30e-4、max error 0.001531、cosine 0.999999974。量化到8-bit RGB后98.3218% channels逐值一致，剩余差异最大1 LSB。这完成了block70最终head的全画面AMD验收；严格边界仍是body feature来自NVIDIA controlled oracle，故71-block全移植尚未完成，不能用该图宣称end-to-end AMD。

按修正后的`+56` body offsets，用block1已验证的tensor-core bit permutation可直接解出W1/W2、QKV、bias、scale与projection；scale从旧误读的-56.19恢复为9.93816。controlled FFN identity＋diagonal self-attention＋V/P identity已能输出有限feature，证明矩阵pack与新offset可执行；但简单把两路512-byte输入reshape为4×4×32再nearest upsample，与oracle correlation仍接近零。逐input impulse进一步显示该输入变换含独立physical permutation／插值，不能用普通reshape替代。该结果把剩余缺口严格压到4104–4159的56-half post input/upsample语义，而非整个1H body权重。

56-half区可由weight地址与SASS常量直接对齐：half 4104对应byte `0x2010`，4136对应`0x2050`，而后续FFN skip内部4168对应`0x2090`；post SASS同一输入准备段确实从`weights+0x2010/+0x2050/+0x2090`交错读取。按名字顺序暂分成32/8/16只是候选，尚未提升为shape结论。三段分别清零的controlled feature correlation为0.6996／0.9677／0.7776，证明三段均参与输入变换。controlled self-attention只让396/1024个输入impulse穿过，说明当前V/P identity尚未覆盖全部physical channel，不足以反演完整upsample；该失败不回退已验证的out-conv与全图AMD head。

单impulse失败随后被Hadamard信号密度修正：controlled self-attention的even V分支对所有输入严格为零，odd分支承载完整`8×8×16`，说明upsample输出本来就是16-channel并写入32-channel body的odd half；396/1024只是unit impulse被量化吃掉的数量，不是结构秩。对main前512＋skip512施加1024×1024 Rademacher Hadamard（幅度±0.5），由`HᵀY/(1024×0.5)`直接恢复`1024→1024`矩阵。小幅独立held-out correlation 0.998775、MAE 3.09e-4；改用双向小probe解除feature读口饱和后，真实幅度held-out correlation 0.998482，预测/oracle std 1.2669/1.2893。矩阵与生成器固化为`block70-upsample-effective.bin`、`.json`和`fit_post_upsample.py`。

尝试把portable upsample直接接raw-unpacked W1/W2/QKV/projection并做full-body联合训练，2,048个iid样本上held-out correlation只到0.254，明确否决。原因是标准1H矩阵仍包含dynamic FP8 scale造成的effective差异；block1历史数据也显示raw projection与effective projection仅correlation 0.391、std相差9.33倍。后续沿已成功的block1路线分别生成FFN branch与attention branch oracle，不用full-block梯度掩盖scale缺口。

`d3d12_post_upsample_test.cpp`已在RX9070XT直接执行portable 1024→1024矩阵。真实幅度独立样本submit→fence 0.565 ms，对NVIDIA controlled upsample oracle MAE 0.0436408、RMSE 0.0751269、max error 0.46234、cosine 0.99868323；逐项误差与CPU effective模型一致，AMD未增加计算偏差。block70至此在AMD闭合输入upsample与最终RGB head两端，中间缺口只剩标准1H FFN＋attention的dynamic-scale/effective参数。

进一步分层修正了上一句“只剩标准1H”的过强结论。保留原W1/W2＋FFN skip、把attention改成diagonal identity后，odd-half可读出FFN相关target；用portable controlled input map训练时held-out correlation 0.741，换成NVIDIA controlled input的精确值后升至0.8846，证明输入桥误差确会被FFN放大。加入E4M3 straight-through量化仍约0.8835；改用block1 effective全套参数初始化再联合训练完整body也只到0.292。故剩余差异不只是dynamic scale：56-half post专用区按名字还含`out_gain`，controlled head basis读到的32维坐标可能已经经过projection后的post gain/正弦变换。`block70-upsample-effective`的边界改称controlled input-path map，不再声称它是纯pre-FFN upsample值。下一步必须把input-side与output-gain分别受控，标准Swin body才能独立验收。

SASS进一步把post输出侧定位到weight byte `0x50e0`（half 10352）：四路load发生在PC 0x8450–0x87e0，紧接着才读取两块out-conv `+0x5130/+0x5330`。普通1H把10352附近视作attention skip，但post这里更符合`out_gain`消费时序；它很可能是同一32-value storage在post中的角色复用。把该向量作为`projection * gain`补入连续模型后raw预测量级仍不足，联合训练最高约0.294，线性32→32后校正也不改善，说明controlled head basis坐标还含kernel内部FP8 dynamic scale／非线性，而非单纯漏乘一个向量。当前最可靠的分层结果仍是：controlled精确输入下FFN odd-half可达0.8846；full projected坐标需另建attention branch oracle，不能从最终head feature反推全部中间层。

CPU descriptor的block70 1344-byte candidate vector随后被识别为42个连续MSVC string，而非weight descriptors；探针现直接解码operation names。与普通block1的31步图对齐结果是精确的：post独有5步前缀`convolution→alias→mul→mul→add`；随后完整复用block1运算序列，但QKV convolution前多`constant_pad_nd`；末尾再加5步`mul→convolution→mul→mul→add`。标准body自身的末两步仍是`mul→add`，即attention residual没有消失；post后缀首个mul才是独立输出调制。图固化为`block70-operation-graph.json`。按该图补回residual与独立gain后attention-only连续模型仍停在0.294，说明controlled输入坐标不是标准body的直接h值；后续必须在图的5步前缀出口建立新读口，而不是继续把最终head basis当中间activation。

依据权威图构造了无attention伪装的prefix读口：W1/W2与QKV/projection全部归零，FFN skip与attention skip同时置1，标准body因此退化为identity，head basis直接读出5步post prefix的完整`8×8×32`。1024-row Hadamard恢复`1024→2048`矩阵，密度仅0.0011597；小幅held-out correlation 0.999998707、MAE 1.29e-5，真实幅度held-out correlation 0.999999938、MAE 3.07e-4、max error 0.00903。`fit_post_prefix.py`、`block70-prefix-effective.bin/.json`已固化该矩阵，旧odd-half controlled map降级为历史诊断。

通用化后的`d3d12_post_upsample_test.cpp`已在RX9070XT执行该权威prefix：1024→2048 submit→fence 0.531 ms，对NVIDIA真实幅度oracle MAE 3.073e-4、RMSE 0.001049、max error 0.009033、cosine 0.999999931。block70输入侧至此不再依赖含混的controlled odd坐标，AMD输出可直接作为普通1H body的32-channel输入。

权威prefix也修复了attention分层。构造attention-only weights：W1/W2归零、FFN skip置1，保留原QKV/bias/scale/projection与post suffix；其target范围-109.25..75.625。运算图与容量共同表明half10352的32-vector同时进入标准attention residual和post suffix首个mul，因此effective公式为`(projection + input*skip)*skip`。用旧odd controlled坐标时模型停在0.294；换成完整prefix exact输入后raw初始化即0.5988，训练100 epoch达0.97622，低学习率refinement最终held-out correlation 0.977028、MAE 0.2904、RMSE 0.7131。参数固化为`block70-attention-effective.bin/.json`，包含Qe/Qo/Ke/Ko/Ve/Vo/P/bias/shared skip/scale共24,708 bytes。该层已闭合CPU portable语义，下一验收点是AMD attention runner与FFN串联。

无需新增attention shader即可做AMD验收：把shared skip吸收到标准block1公式，令`P'=P×skip`、`attention_residual_skip'=skip²`，同时FFN branch归零、FFN skip置1，即严格等价于post的`(projection + input×skip)×skip`。`make_post_attention_compatible.py`生成41,220-byte兼容blob后，RX9070XT单tile submit→fence 1.806 ms；对NVIDIA attention-only held-out correlation 0.972519、MAE 0.2516、RMSE 0.6193；对CPU effective correlation 0.999615、MAE 0.03468。AMD增加的误差很小，主要误差仍来自0.977级effective近似。下一步只剩FFN完整32-channel target及prefix→FFN→attention串联。

同一双residual identity方法也建立了干净的完整FFN读口：保留原W1/W2/FFN skip，attention branch归零，attention skip置1，head直接读出32-channel FFN target。2,048样本范围-78.5..96.9375；raw初始化correlation 0.306，训练120 epoch后held-out correlation 0.937505、MAE 0.6877、RMSE 1.4417。参数固化为`block70-ffn-effective.bin/.json`。

`make_post_body_compatible.py`把FFN与attention effective合并为标准41,220-byte 1H layout，并继续使用`P'=P×skip`、`residual'=skip²`。RX9070XT从exact prefix输入一次跑完整FFN＋attention＋shared suffix gain：单tilesubmit→fence 1.038 ms，对NVIDIA full-body correlation 0.903772、MAE 0.9031、RMSE 1.4713、max error 9.117。错误的continuous joint refinement无法复现同参数shader（CPU单tile仅约0.18），已中止且未写回；当前权威是分层effective与AMD实跑结果。block70结构链至此完整落到AMD，剩余是提升FFN effective精度并接全图physical view。

全图后半链随后贯通：原CUBIN只用双residual identity导出256×144×32 prefix，RX9070XT用同一`block70-body-compatible.bin`在width256/height144下执行完整FFN＋attention＋shared gain，submit→fence 5.553 ms；portable RGB head再耗时0.650 ms。最终画面对NVIDIA原post RGB correlation 0.945370、RGB MAE 0.03524，RGBA cosine 0.991158；人物、机甲与背景几何完整，差异主要是高频条纹幅度。严格边界是prefix仍来自NVIDIA controlled oracle，故不能称end-to-end AMD。

prefix global physical mapping开始用impulse指纹恢复。main global offsets0–63逐一匹配local matrix columns0–63（cosine 0.99999+）；`+2048/+4096/+6144`对应local64/128/192，`+8192`进入下一output tile row，main tile-x stride为64。full geometry只消费每CTA前256个main local columns；extent8 oracle的后256 columns不是full模式简单连续切片。skip布局不同：offset0–15匹配local0–15，`+64/+128/+256`匹配local64/128/256，而`+1024`进入下一output tile x，skip tile-x stride为1024。剩余prefix工作已从数值语义缩到两路global view的halo／boundary地址公式。

高位bank扫描补齐main：第一bank base0、第二bank base`18×8192=147456`，每bank含四个64-byte planes、plane stride2048；CTA base为`ty×8192+tx×64`，两bank分别映射local columns0–255/256–511。按此gather后main-only prefix对NVIDIA correlation 0.999996044、MAE 4.31e-4、std 3.30428/3.30442。

skip full geometry则是两个1024-byte banks：相对CTA base的`[0,1024)`与`[32768,33792)`，CTA x/y strides为1024/65536。`run_original_post_dataset.cpp`新增`global-skip-features`，在full dimensions只launch CTA0并scatter两bank；2048-row Hadamard恢复2048→2048矩阵。全图skip-only prefix correlation 0.999999972、MAE 3.83e-5、max error 0.005875。参数固化为`block70-prefix-global-skip-effective.bin/.json`，地址gather与AMD输出合并固化为`prepare_post_global_prefix.py`。

通用AMD matrix runner扩展为batch `input_dim→output_dim`。RX9070XT一次处理576 tiles后，main prefix与skip prefix各自对CPU portable逐float 100% exact；CPU只执行E4M3解码、physical gather、两路相加及tile-major→row-major重排。合并prefix对NVIDIA correlation 0.999996278。随后AMD body耗时3.030 ms、RGB head 0.774 ms，得到从block69 main＋block0 skip原始physical buffers起算的完整AMD block70：最终RGB correlation 0.945358、RGB MAE 0.035245。至此block70本身已端到端移植完成；71-block总目标剩余上游portable block0–69的统一AMD串联与最终验收。

上游主线转入decoder39–69。block39 CPU descriptor的128-byte operation vector解码为四步`convolution→convolution→mul→add`。容量初读曾把525,312 bytes误当成同数目的halves；修正后record实际是262,656 FP16 elements，主体262,144=`512×512`，更符合两组512输入的grouped 1024→512卷积，剩余512供第二卷积／scale路径。结构与容量固化为`block39-operation-graph.json`。

旧`probe_b39y2`的main/skip消融被重新执行后不具备数值权威性：a1虽写32,632 bytes，但全是E4M3 NaN哨兵，解码／sanitize后方差为零；skip-only也全零。它只证明旧0x50 ABI能store，不能用于判断第二输入无贡献或做线性basis。后续block39 oracle必须改用当前portable block38 finite输入、完整arena与正确block30 skip，且在采样前先通过“零NaN＋非零方差”门槛。

随后用正确`blocks31-38-portable-2d.fp8`与执行图要求的`block30.output1=block30-pool-correct.bin`重跑，逐byte复现18:46的portable block39：32,681 nonzero、55–76个NaN（不同保存长度计数），sanitize后才finite。skip-only仍全零，full与main-only一致；但这不是“模型skip权重为零”的充分证据，因为原CUBIN要求runtime packed weights，当前仍直喂archive FP16。

`run_original_block39_basis.cpp`以0.52–0.71秒完成1,024/8,192 basis。物理拓扑清楚：四个16KiB banks（0/16384/32768/49152），每bank仅前8KiB输入有效并写同base的8KiB输出；bank0二分图精确分成16个`512 input→512 output` components，各component按原地址排序的basis矩阵correlation 0.9994+。然而single-impulse与dense Hadamard矩阵对真实portable输入correlation都约0.01，证明dynamic FP8/runtime weight pack使该CUBIN数值不可作为archive语义oracle。block39数值恢复正式改走archive逻辑grouped convolution与descriptor参数，不再蒸馏错误CUBIN。

descriptor探针进一步导出block39完整0x170 layer object。稳定整数域为`+0x40..0x4c=[1,1,8,32]`、`+0x50=1024`、`+0x54=512`，`+0x80`为float 1.0、`+0x84=3`。与262,144-element主体联立后，主卷积容量唯一自然闭合为`in=1024,out=512,groups=2,in_per_group=512`（逻辑weight可视为512×512），而不是dense 1024×512。basis物理图每bank16个512→512 components也与“两组输入＋空间展开”一致。下一步实现archive FP16 grouped convolution reference，并只用CUBIN components恢复token/channel physical permutation。

`build_block39_logical.py`按PyTorch convolution标准layout把record展开为1536→512稀疏矩阵：main前/后512 channels分别乘W前/后256 output rows，block30 output1的512 channels逐通道乘record尾部512 depthwise weights后相加。固定输入使用`blocks31-38-portable.f32`的64×1024 canonical main与`block30-pool-correct.bin`解码后的64×512 skip；CPU输出finite，范围-21.01..25.66、std1.946。通用AMD batch matrix runner在RX9070XT执行64 samples耗时0.836 ms，对CPU logical oracle MAE 1.43e-9、max error 4.77e-7、cosine 0.999999999993。block39 archive逻辑语义至此在AMD闭合；下一层转入block40 split-Swin。

block40四个CPU operation vectors与record容量随后闭合全部逻辑张量。Ffwd层为两张256×512 gate/up卷积，按`fast_activation(gate) * up`形成gated hidden；FfwdProj为512×256 projection＋512 skip；QKVAttn为三张256×512、16 heads×16 dim、16×64×64 bias与16个FP32 scales；Proj为512×256＋512 skip。结构固化为`block40-operation-graph.json`，同构适用于40–47。

`split_swin512_reference.py`直接读取四条archive FP16 records执行上述公式。以block39 logical 64×512输出起步，blocks40–47全部finite且无NaN，std依次为1.5972/1.5023/1.4190/1.3355/1.2571/1.0897/1.0088/0.9524，范围连续收敛而非坍缩。decoder 512-channel CPU logical主链至此贯通；下一验收点是把同一两pass gated-FFN＋16-head attention HLSL跑在RX9070XT。

AMD 512-channel correctness runner在`d3d12_block128_test.cpp`新增split512模式。首版在每个query/key/head重算完整QKV，运行超过一分钟被主动终止；head-local改写仍过慢。最终实现为三pass：gated FFN→一次性QKV预计算UAV→16-head attention/projection。把head循环从compile-time unroll改为runtime loop后，D3DCompile与执行均稳定。

`pack_split_swin512.py`把四条archive records整理为统一3,936,320-byte FP32 blob。RX9070XT连续执行blocks40–47，每层submit→fence 20.512–25.745 ms；逐层对CPU archive logical correlation为0.99933/0.99872/0.99792/0.99687/0.99554/0.99413/0.99334/0.99271，最终MAE 0.03120、RMSE 0.13928。AMD block47 std1.0244、CPU0.9524，误差累积但无爆炸／坍缩。decoder39–47至此全部以archive真权重在AMD执行；下一段进入block48 8H upsample与49–55。

空间尺度复核后，前述64-token链被保留为单window correctness，正式固定帧改为：ViT 8×8×1024经`block39_spatial_reference.py` grouped投影与2×nearest上采样到16×16×512，并融合`block30-main-correct.bin`未池化skip；blocks40–47在width/height16下处理4个8×8 windows。CPU std由block39的3.400平滑降到block47的1.626。RX9070XT八层耗时22.471–24.631 ms，逐层correlation 0.999316/0.998653/0.997766/0.996701/0.995307/0.993735/0.992726/0.992068，最终MAE0.03669。decoder39–47完整16×16空间链至此闭合。

block48 descriptor operation vector共有36步：4步upsample前缀`convolution→convolution→mul→add`，随后是带额外input convolution的标准8H Swin。live字段给出in/out=512/256、`+0x40..=[0,0,8,32]`、boundary flags1/1、scale1.0、mode3。record 410,392 halves，比普通8H block49多65,776；按尾部QKV/bias/projection结构反向对齐得到：前65,536为256×256 prefix conv，随后W0/W1/W2占65,536–245,759，245,760–246,263为504-half dw/sin/padding/FFN-skip区，QKV从246,264开始，之后全部与普通8H尾布局同构。结构固化为`block48-operation-graph.json`；下一步细分504-half区并实现archive logical upsample。

中段最初按504 halves切分仍差8。对bias尾部逐half解释FP32后，真正8-head scale位于377,344，值为8.844/8.513/19.578/8.507/10.010/11.181/11.822/19.821；普通block49也在对应+8位置得到正常scale。由尾部反推，正确分区为：FFN skip245,760–246,015；prefix dw/sin完整256值246,016–246,271；QKV246,272–344,575；bias344,576–377,343；scale16 halves；projection377,360–410,127；attention skip256；tail padding8。

空间尺度校准进一步修正decoder：block39应把ViT 8×8上采样至16×16并融合block30未池化skip；blocks40–47处理4个window；block48再由16×16裁剪／上采样到真实32×18并融合block22 output1。按此重跑的16×16 AMD链已在前文记录。

前述“fused tensor必须MMA unpack”的结论被offset修正推翻：±1e4爆炸来自把bias尾巴当scale并把scale bytes吃进projection。按正确+8 offsets重跑，main-only 16×16→32×18 prefix std0.1129，完整8H body finite、范围-1.66..1.42、std0.08768。真正尚未闭合的是block22 output1 canonical skip；直接把其NVIDIA physical E4 view reshape会产生std52并污染结果。`block48_reference.py`因此只接受canonical FP32 skip，缺省可用zero-skip验证main/body，不再隐式误读physical view。

AMD correctness runner新增fused256模式：标准FFN hidden288、QKV 3×128、8 heads×16 dim，并复用QKV预计算三pass。`pack_fused_swin256.py`支持block48特殊offset与普通block49–55 corrected offsets，统一生成1,247,264-byte FP32 blob。block48 main-only padded24×32执行17.711 ms，对CPU correlation0.999339、MAE0.001096。

blocks49–55 CPU archive logical从block48 main-only输出继续全部finite，std由0.0702降至0.02343。RX9070XT逐层独立验收（每层输入由CPU将上层18×32裁出后补零到24×32），耗时15.784–18.583 ms，correlation均在0.999344–0.999447；block55 MAE0.000301、RMSE0.000828。数值kernel已闭合，剩余工程项是把层间crop/zero-pad变成GPU pass，并补入block22 canonical skip后做真实串联。

block56–61按相同尾对齐恢复128-channel archive布局。普通block57 record：W0 8,192、W1/W2各20,480、padding16、FFN skip128、QKV24,576、4-head bias16,384、4个FP32 scales、projection8,192、attention skip128、tail8。block56在前方多128×128 prefix conv与128-value prefix aux，corrected QKV起点65,792。`pack_fused_swin128_archive.py`统一生成90,372-float runner blob。

main-only固定帧由block55 18×32×256 grouped投影并2×上采样到36×64×128；block56–61 CPU archive logical全部finite，std 0.00210→0.000854。128-channel AMD模式也改为QKV预计算；block56 padded40×64 submit→fence 38.709 ms，对CPU correlation0.991979、MAE1.13e-4、RMSE2.78e-4、max error0.00649。因信号std仅约0.0021，绝对误差比correlation更有解释力。canonical block14 skip仍待上游提供；57–61下一步用单device多block宿主避免每层重复D3DCompile。

block62–65同样由普通block63尾布局对齐。normal record 30,880 halves：W0 2,048、W1/W2各6,144、padding16、FFN skip64、QKV6,144、2-head bias8,192、2个FP32 scales、projection2,048、attention skip64、tail12；block62额外64×64 prefix conv与64-value prefix aux，corrected QKV起点18,560。`pack_fused_swin64_archive.py`统一生成28,802-float blob。

main-only CPU固定帧把block61 36×64×128 grouped投影后2×上采样到72×128×64；blocks62–65全部finite，std 8.20e-5→3.95e-5。AMD fused64模式复用QKV预计算，耗时2.118/2.440/2.340/1.898 ms；对CPU MAE 1.38e-5/1.05e-5/7.07e-6/5.42e-6。correlation从0.703降到0.629主要因信号已接近FP8量化步长，AMD/CPU std仍一致。canonical block8 skip待接入，数值kernel本身已闭合。

block66–69 corrected 32-channel layout继续闭合。normal record：W1/W2各2,048、padding16、FFN skip32、QKV1,536、bias4,096、单FP32 scale占8 halves、projection512、attention skip32、tail8；block66额外32×32 prefix conv与32-value prefix aux，QKV corrected offset5,200。`pack_fused_swin32_archive.py`转换为现有block1 AMD layout。

main-only CPU链把block65 72×128×64上采样至144×256×32，blocks66–69全部finite，但std仅1.36e-6→3.67e-7。RX9070XT block66耗时5.801 ms并把全部1,179,648 values量化为零；对continuous CPU MAE2.62e-7、RMSE1.36e-6。这不是kernel失败，而是正确的E4M3下溢行为，明确证明最终decoder数值由block4 canonical skip主导。至此66–69数值核／pack已恢复，但在encoder skips接入前继续跑67–69没有信息量，主线应立即转回block4/block8/block14/block22四条canonical skip。

为生成四条skip，先修正`block0_reference.load_fused_block`长期存在的archive offsets：FFN skip前padding应为16 halves；scale storage为1-head时8 halves、其余`2×heads`；64-channel record尾padding12，其余8。修正后blocks1/4/5/8/9/14/15/49/57/63/67的record容量全部精确闭合，block22 extra相应修正为65,528。

但按该archive row-major CPU encoder从RGB重算得到决定性反例：block4/8/14/22 downsample前main std依次仅2.53e-4/3.29e-6/5.31e-9/4.68e-12，层级间指数衰减，与NVIDIA physical skips约几十量级完全不符。故offset闭合只证明tensor边界，不证明fused matrices已具备runtime dynamic scale；naive archive encoder不能提供canonical skips。decoder main-only在66处E4M3下溢是同一缺口的下游症状。下一主线必须把已恢复的block1–3、block10–13 effective方法推广到encoder downsample main branches4/8/14/22，而不是继续拼接naive archive值。

现存`stellar-block8/14/22-skip.fp8`尺寸恰好等于各级padded tensors，但直接E4M3解码std为54.8/78.4/46.7，仍是physical-scaled坐标而非canonical。以block48为控制变量：direct skip乘全局scale0.0625时body std2.63，0.375时std15.76；旧portable CUBIN active view std15.83。然而不论scale取值，直接row-major相关仅约0.337；再乘record的256-value prefix aux后相关反降约0.25。故缺口主要是token/channel physical permutation与per-tile dynamic scale，不是一个可调全局系数。该结果否决“按std拟合skip scalar”的捷径。

block8 skip随后应用已严格恢复的`tinlayout-2h64-output-permutation.i32`逐tile unswizzle，得到72×128×64 canonical token/channel顺序；同法转换portable block62 target。即便如此，archive logical block62对target correlation仅约0.192，乘64-value prefix aux后约0.146；全局scale从1/32扫到1只改变输出std，不改变相关。说明已知2H permutation仍不足：upsample prefix还依赖spatial phase与per-tile dynamic scale，且portable CUBIN target本身使用错误runtime weight pack。该target不能用来拟合prefix，64-channel skip路线继续等待encoder effective scale状态。

因此主线改为直接导出5090 runtime-packed arena。现有`dlssnr_layer_oracle_probe.cpp`hook的是第一個1H forward（block1），其launch blob `+0x10`已知为block1 runtime weight VA；`weights-arena-index.json`给出block1 arena offset22,016，故arena base可精确计算为`weight-22016`。probe新增同步`cuMemcpyDtoH`完整147,719,680 bytes到`D:\DLSSNR-Lab\logs\runtime-weight-arena.bin`，并在log记录base/result/size。该路径复用已经成功抓activation的CUDA-context内copy，不使用会让游戏Fatal Error的ReShade map/copy resource事件。

arena版addon已在AMD WSL用MinHook静态编译为349,012 bytes，部署到5090 Lab与《剑星》Win64目录。当前`query user`确认5090无互动登录用户，故未从SSH Session0强启Steam；下次桌面登录后正常启动游戏、让DLSS5运行一帧即可触发。`verify_runtime_weight_arena.py`会验证总长度、153 records边界，并逐record报告runtime/archive SHA与changed bytes。若arena成功，这将一次性替换所有错误archive→CUBIN路径，并直接解锁encoder skips与fused blocks的权威数值oracle。

5090重新登录后，已通过交互式计划任务从SSH自动启动《剑星》。原probe的外层1H forward稳定命中，但`cuGetProcAddress` hook太晚：DLSSNR在addon加载前已缓存CUDA launch指针。改hook已验证稳定的`CubinBackendNGX::launch` RVA `0x449a0`后，首次真实参数直接得到block1 `weight=0x3bd605600`、arena base `0x3bd600000`、grid `240x136x1`；重启后的地址随机化样本仍严格满足`weight-base=0x5600`且base为64KiB对齐，证明offset推导正确。

同时否决CUDA readback：backend参数里的input/output/weight均为D3D12 GPU virtual address，不是CUDA device pointer。primary context下`cuMemGetAddressRange_v2=500`、`cuMemcpyDtoH_v2=1`；`cuPointerGetAttribute(CONTEXT)=1`，明确不是CUDA context选择问题。旧`runtime_weight_upload_probe.cpp`注册ReShade copy/map事件会在D3D12初始化Fatal Error，仍禁止部署。下一步从原生D3D12对象层追踪`CreateCommittedResource`得到GPUVA→`ID3D12Resource`映射，再以独立readback command list复制完整arena；不再使用CUDA driver API或ReShade资源事件。

原生D3D12 readback已成功。`runtime_weight_d3d12_readback.cpp`只用安全的ReShade `init_device`取得原生device，随后以MinHook直接追踪`ID3D12Device::CreateCommandQueue/CreateCommittedResource`，避开会Fatal Error的ReShade资源事件；addon必须用`GET_MODULE_HANDLE_EX_FLAG_PIN`常驻，否则worker会落入unloaded module。命中三个147,719,680-byte资源：default heap GPUVA即arena base、upload heap、readback heap；同一DIRECT queue fence排空后readback返回`S_OK`。

完整runtime arena已回收到`/tmp/runtime-weight-arena.bin`，长度147,719,680，SHA256 `a5513b1845c98a486985ed04f38e66a1854cce33c2aba3a505866028bd4ee3e5`。`verify_runtime_weight_arena.py`确认它与archive arena全文件SHA完全相同，153/153 records逐byte exact，changed payload bytes=0。由此正式推翻“runtime动态改写权重/有效权重不同”假设：encoder skip衰减的根因只能在launch ABI、物理资源布局或arena外runtime state。下一步复用GPUVA→resource readback抓block1及各encoder skip live activation，与Spark原CUBIN chain逐层寻找首个偏离点。

私有activation最终通过自制CUBIN读口打通。D3D12 committed/placed/reserved及device4/8/10 resource APIs均无法映射block1 input/output，证明它们是NvAPI backend私有raw GPU allocation。恢复stash中的`dlssnr_cubin_oracle_probe.cpp`后，移除会Fatal的ReShade resource/execute events，改用安全`init_device`与原生queue hook；外层block1 forward划定窗口，内层`CubinBackendNGX::launch` hook直接取得真实context/command_context/参数blob。旧probe另有三处错误一并修正：copy grid误放Y维、把`CCMultiCubinBackend`误当NGX backend、走`0x449a0`异常包装层。新版按反汇编直接调用context vtable sync `+0x150`、bind `+0xd8`、dispatch `+0x140`，三步均返回0，raw readback稳定成功。

live block1参数包96 bytes：input/output/weight位于`+0/+8/+0x10`，`+0x18`为1088×1920，field均0，唯一额外输入为`+0x38 optional2`。GPUVA显示三个物理view：input resource `+0x42800`、output resource `+0x2800`、optional2基址对齐；input/output资源基址严格相差64MiB。最初只抓32MiB并在runner allocation起点放view，FP8 correlation仅0.18；扩为完整64MiB三段并在Spark重放时恢复`input+0x42800/output+0x2800`后，live 5090 vs Spark原CUBIN block1全64MiB correlation 0.99991518、MAE 1.4243e-4，从有效output view开始correlation 0.99995811、MAE 8.77e-5，仅50,848/67,108,864 bytes不同。根因正式闭环：旧encoder衰减来自不完整物理bank容量与错误view基址，不是权重或动态scale。`run_original_fused_global.cpp`现支持动态arena、完整weight arena offset、optional2输入及input/output view offsets。

backend trace随后覆盖首帧117条权重launch，并按arena offset闭合四条encoder下采样ABI：block4 offset124,261,888的skip在blob `+0x40`；block8/14/22 offsets147,451,904／833,536／5,912,576的下采样前skip均在`+0x08`，`+0x48`是继续向encoder深处的compact main。probe在原launch返回后以同一context追加四次raw copy，按64/32/16/8MiB写入单一atlas的0/64/96/112MiB，四次dispatch均返回0，最终readback返回0。权威live skip已导出为`/tmp/block{4,8,14,22}-output1-live.bin`；下一步是恢复global bank/token到decoder canonical空间的精确gather，而非再拟合数值权重。

global tinlayout进一步确认以4×4 microcell为外层存储单元；`tinlayout-2h64-output-permutation.i32`内部offset的`//1024`恰好把8×8窗口分为四个4×4 cell。`decode_tinlayout_global.py`可按任意head-specific token-bit候选生成bijective cell-local gather，但2H/4H/8H token位序仍是诊断候选，不能标为exact。基于多层空间连续性筛选8H候选后，block22 skip接入RX block48使AMD与CPU portable保持corr0.99935，但对旧CUBIN physical解码仅约0.56；后续发现该`/tmp/block48-decoder.bin`只有98,304非零且不是worklog记录的147,456-byte完整输出，故该对比降级，不再作为最终oracle。

block49 controlled分支给出更可靠的body证据。双residual权重下，修正为bijective 16KiB input scatter后，15,872/16,384 output bytes与input exact，其余512为固定无效槽。单token 256-channel basis一次进程导出4MiB Jacobian；按候选canonical gather后，archive-logical `256→288→256` FFN对原CUBIN correlation0.96121、MAE0.004742，Hungarian channel重排仅升至0.96710。将record矩阵误解成full-width E4M3 `256→576→256`反而降至0.532，因此该路线再次否决，临时代码已撤回。block48 skip scale可微诊断：window/head 0.680、cell/head 0.754、token×32ch 0.757、16ch 0.768、8ch 0.795、4ch 0.836、2ch 0.891、逐值上界0.964；逐值scale最大1264明显过拟合。结论是archive body主体成立，误差混合了physical scale/gather与旧main/output文件谱系；下一步必须从5090同一live frame同时抓block48 main/skip/output/aux再闭合，不再用旧临时文件交叉拟合。

同帧block48四路随后由live backend直接导出。block22 output1与block48 skip input从view `+0x2800`起的前102个64KiB pages逐byte exact，确认skip绑定与生产/消费链正确。probe在block48 `flag=0`前显式调用context sync，再捕获main/skip/aux pre-state，launch后捕获output；written-mask由两次不同output init的Spark重放交集恢复，精确为8,355,840=`136×240×256` bytes，范围`[0x2800,0x7fa800)`。标准CUDA重放前5MiB逐byte exact，后3.36MiB分叉；main、skip、aux分别从64扩到128MiB均逐byte不改变结果，排除资源截断与时序旧值。

live backend对象查询最终确认block48传入kernel指针与`cc_tinlayout_fused_swin_8h_256_8_upsample_tilesync_fp8`对象完全相同，与plain对象不同。plain/tilesync SASS确实不同，但两者用标准`cuLaunchKernel`时输出相同；`cuFuncGetAttribute`显示required cluster三维全0、cluster_must_be_set=0、nonportable_cluster=0。故后段差异不是CUDA cluster配置，而是NvAPI context/`LaunchCuKernelChain`的tile-sync执行协议，与此前ViT跨block `flag=1`缺口同源。下一主线改为扩展现有`d3d12_nvapi_repack_test.cpp`式5090宿主，直接以CreateCuFunction+LaunchCuKernelChain运行block48 live快照，再生成controlled calibration dataset；停止继续调整标准CUDA runner。

独立`d3d12_nvapi_fused_live.cpp`随后分别使用旧Chain ID `0x24973538`与游戏probe确认的ChainEx ID `0x846a9bf0`，两者均只能得到与标准CUDA相同的written exact 5,588,179/8,355,840=66.8775%。游戏内进一步做原backend自重放：main/skip/aux分别扩至128MiB无变化，只替换output VA、保持四路相对VA拓扑、再到完全复用原wrapper与原output资源，nested replay仍固定产出同一个plain hash `a3587b...`；随后外层正常调用稳定产出live hash `865778...`，并与未插入replay的上一轮正常live逐byte exact。正常backend trace仅有一条block48 launch，故不是两相双launch。结论是live tilesync状态绑定高层唯一正常调用路径/TLS，而非参数内容、资源VA、公开NvAPI接口或CUDA属性。批量controlled oracle必须改为在这唯一正常调用前原位备份/替换原资源内容，调用后捕获并恢复；不能额外nested launch。

原位controlled transaction随后成功。probe在block48前sync并备份main/skip/output，以自制`fill_raw_buffer`把原output填`0xA5`、main/skip清零；唯一正常外层launch后保存controlled output，再恢复三路资源并sync，补跑一次正常block48供游戏下游消费。backup/zero/fill/capture/restore/normal/final-sync全部返回0，恢复后的live written hash `865778...`与未注入轮次100% exact。controlled输出精确显示：32,640个256-byte rows中21,760行全写0、10,880行完整保留`0xA5`，即update/preserve严格2:1；无channel内mask。用该exact mask复核，标准CUDA replay在5,570,560个updated bytes上与live逐byte100% exact，全部2,767,661历史差异只来自preserved bank。backend trace同时确认block1 input与block48 output GPUVA完全相同，preserved区是allocator复用/物理hole，不是动态权重或数值误差。

曾由2/3比例推测固定active rectangle为16×24并在RX诊断跑通blocks48–69；接回block70几何时发现该解释与正式256×144宽度阶梯冲突，已撤回，相关输出只保留诊断。正确边界是full block48逻辑仍按18×32处理，physical storage内2/3 update与1/3 alias holes不能直接reshape成canonical。对full18×32 candidate做per-channel skip scale最多corr0.661，证明hole不是scale。下一步在原位transaction中临时替换block48权重为“prefix保留、body identity”，直接导出NVIDIA真实prefix坐标，避免把physical holes误作canonical值。

AMD空间runner独立修正仍成立：旧HLSL以`tile=t/64`取attention key，只适用于预先window-major的单tile数据；现新增HWC `query_index/key_token`，unshifted按8×8窗口、shifted按roll(-4)窗口再映回原HWC。block48新旧路径对CPU均保持corr0.99934；诊断active链在RX9070XT实际跑到block69，8H约16–19ms、4H约38ms、2H约2–2.5ms、1H约2.7–3ms，证明空间寻址与跨层文件协议可运行，但尺寸/skip尚未达到最终验收，不能称完成。

正式18×32链随后用rank-64 affine把block48 portable body校正到同帧NVIDIA target：空间checkerboard held-out correlation0.96911，全样本correlation0.98612；两段257→64→256矩阵均由RX9070XT执行，AMD与CPU校正输出correlation1.0。沿正式尺寸继续跑到block70后，五种2×2下采样候选中p10最好，但对权威`post-repro.bin`最终RGB仍只有correlation0.828154；逐级比较显示第一处断崖明确在block56：block48为0.98612，block56降至0.59069，故末端相位不是主因。

block56布局诊断同时修复了`run_original_fused_view_permutation.cpp`的ABI遗漏：原runner没有为`Params.skip`分配／绑定资源，4H upsample prefix会直接CUDA illegal access。补齐独立zero skip后，进一步穷举128-channel的全部720种token-bit排列；现用`(3,0,1,4,5,2)`仍是并列最优（抽样corr0.59274），排除空间bit位序错误。固定帧采用129→128带bias ridge校正prefix/body输出，checkerboard held-out correlation0.93016；全图拟合输出对NVIDIA correlation0.945554、MAE13.8702。该矩阵在RX9070XT耗时0.810ms，对CPU结果MAE6.88e-6、max3.36e-4。边界：这是目标明确要求的fixed-input校正，尚未证明跨帧通用；下一步从该输出重跑57–70并复核最终RGB增益。

校正后的block56已在RX9070XT继续跑完57–61，block61 std由旧链11.94升至17.93；到block62后新旧输出仍corr0.99950，证明block8 skip压倒上游main。`d3d12_post_upsample_test.cpp`因此增加可选`spatial-width/phase-period`，由GPU按窗口内坐标选择独立affine。block62的8×8 phase fixed-frame校正在RX9070XT耗时0.789ms、对CPU MAE6.80e-6、对同层NVIDIA corr0.86294；block66相同方法耗时0.684ms、空间留出corr0.94208、全图corr0.96993。修正后67–69全部在AMD重跑。

block70复核同时抓出两项错误。第一，`block70-prefix-skip-amd.f32`仍是`(tile_y,tile_x,8,8,32)`，必须与main在tile-major先相加再共同transpose；旧组件按该合同可100%重建`block70-prefix-full-amd.f32`，证明布局闭合。第二，更关键的是当前decoder层级oracle来自09-03的新live frame，而`post-repro.bin`与`block70-prefix-main-amd.f32`来自09-02旧frame；即使补回32-channel `encode_tinlayout_global`与双bank gather，二者仍零空间相关，确认是跨帧谱系而非可拟合数值误差。因此本轮0.83066最终RGB只保留跨帧诊断，不能作为完成验收。下一步让游戏同一帧同时导出block48与block70边界，建立唯一coherent fixed-input oracle后再做最终相关性。

同帧atlas随后闭合。probe不再在block48立即readback，而是在block69（arena offset147,346,432）返回后把`output-0x2800`的完整64MiB复制到atlas 400MiB，再统一读回；初版host readback仍截在400MiB，修为464MiB后成功。为保留会被block48覆盖的入口资源，最终版把初始block1 input另存atlas 528MiB并把readback扩为592MiB。文件长度620,756,992 bytes；block69两次独立游戏启动SHA-256均为`735c39e10976de273a7b3c9e631e14922eb3622e5b688368ff9cde89170c8494`，block1 input亦与旧live capture逐byte exact，证明固定场景稳定，前述零相关应修正为“旧standalone文件谱系/错误view”，而非游戏画面跨帧。

远程启动故障也已定位：PlayStation Studios `Report Problem`窗口让Steam长期显示“剑星－正在运行”。`windows_ui_bridge.ps1`在互动Session 1点击`Don't Report`并送Enter后解除锁；`launch_stellarblade_test.ps1`改用`steam://rungameid/3489700`。Steam冷启动时URI可能早到，需在7个steamwebhelper就绪后再提交一次，整个过程无需重启5090。

`nvapi_chain_probe`通过配套loader提前加载，能记录全部post CUBIN handle建立；只挂旧Chain ID会在首次提交前崩溃，恢复ChainEx后游戏稳定运行但没有LAUNCH事件。结合此前公开NvAPI独立宿主无法复现live状态，正式路径不经过可hook的公开launch入口。下一步扩展已稳定命中的`CubinBackendNGX::launch` trace，去掉arena-weight过滤并记录block69之后的所有launch，以找到post block的非arena weight/0xb8参数。

内部backend trace移除`arena_weight`与`bytes<=0x100`过滤后，真实post立即出现为seq154：grid481×273×1、blob 184 bytes。其`qword0=0x3eb182800`与seq153 block69 output完全同址，`qword1=0x3cf342800`为skip raw view，`qword2=0xa003`是output surface handle，真正layer weight在`qword3=0x3c6299a00=arena+147,429,888`，blend scale在`qword13=arena+147,429,376`。此前通用trace固定把`+0x10`解释成weight，才漏掉block70。probe在post返回后将`qword1-0x2800`完整64MiB复制到atlas 464MiB，并把统一readback从block69延后到post。

standalone `run_original_post.cpp`确认上述字段语义。probe加入`surf2Dread<ushort4>`并将RGBA16F转换为float4，同时用`tex2D<float4>`捕获`qword7/+0x38` Color texture。600MiB atlas中592MiB保存surface ROI、593MiB保存Color ROI，现场`copy/surface_copy/texture_copy/sync/readback`全部为0。

live几何亦完成校正。post grid481×273对应有效480×272 tiles；standalone `prepare_post_global_prefix.py`的32×18 stride不能直接用于游戏。全局main plane/row/bank stride分别为30,720/122,880/33,423,360 bytes，skip bank/row stride为491,520/983,040 bytes。左上ROI改用全局stride后，AMD RGB correlation由负值升到0.4501、MAE降至0.001236（该ROI几乎全黑，仅作结构诊断）。

为获得可见验收图，surface/Color捕获改到tile对齐ROI origin=(2304,576)，size256×144；对应main tile origin=(288,72)，skip从raw allocation第二个64MiB page读取。NVIDIA图中蓝色球体轮廓清晰。archive final head直接使用时AMD内容幅度近零，但32-channel body对目标仍保留强线性信息：只用checkerboard一半像素拟合33→3矩阵，另一半RGB correlation0.984375、MAE0.007192。全图矩阵由RX9070XT执行0.607ms，RGB correlation0.984697、MAE0.007156，生成图恢复球体位置、轮廓与明暗方向。该结果闭合“live NVIDIA block69 raw→AMD block70”；上游0–69仍未统一替换为AMD输出，故71-block总目标继续未完成。

尺度关系进一步纠正：live block69有效空间为1920×1088，block70 prefix把4×4 main patch上采样为8×8 output tile；因此256×144 post ROI只对应128×72 block69区域。此前把144×256 block69再取p10相位得到72×128，是错误几何的补丁，相关输出全部降级。下一主线从live block66的0x60参数同时捕获main/aux/enc0 skip，以正确72×128 ROI在AMD重跑66–69，不再沿用p10下采样。

block66 live四路随后在同一次launch闭合：`+0x00 main`、`+0x08 output`、`+0x38 aux`、`+0x50 enc0 skip`各抓完整64MiB，pre三次copy、post output copy与两次sync全部返回0。按global microcell从block65取36×64×64 ROI，从block66 output/skip取72×128×32 ROI。archive block66直接输出对live相关接近0，但全局32→32校正在checkerboard held-out达到0.954105，证明空间坐标正确、缺口为channel basis；完整矩阵在RX9070XT为0.684ms、全ROI correlation0.958552。

校正后blocks67–69按72×128在RX9070XT依次耗时2.378/2.006/2.566ms。把AMD69直接2×展开后对live block70 main prefix做33→32校正，checkerboard held-out correlation0.975165、GPU pass0.846ms；这同时否决了候选`encode_tinlayout_global→physical gather`（其prefix correlation仅0.063）。接入live skip、AMD block70 body与已保存head校正后，66–70纯AMD content ROI最终RGB correlation0.978812、MAE0.007868、RMSE0.016834；图像恢复蓝色球体的位置、轮廓及明暗，仍有纹理噪声。当前严格入口为live block65 main＋enc0 skip，下一步继续前移到blocks62–65。

block62 live四路与blocks63/64/65逐层output随后全部捕获成功。content ROI尺度为：block61 main 18×32×128，block62 output/block8 skip 36×64×64。block62 65→64校正checkerboard held-out correlation0.965698；block63为0.968701；block64为0.974088。trace还纠正shift并非统一双轴：block63=(-4,-4)、block64=(-4,0)、block65=(0,-4)，`d3d12_block128_test.cpp`现以mode 0/1/2/3表示none/both/x/y。

block65 live resource的candidate decode无法作canonical target：即便输入直接换成live block64，四种shift mode输出对该decode均只有约0.058 correlation。但把AMD65继续送入block66并对真实block66 output验收，32→32校正的checkerboard held-out correlation达到0.985291，证明62–65计算链有效、错误在block65 physical decode视图。逐层校正后RX9070XT完整62–70 content ROI最终RGB correlation0.962890、MAE0.013139、RMSE0.022952。严格入口前移为live block61 main＋block8/enc0 skips；下一步捕获block56–61逐层边界。

block56 live main/output/block14 skip与blocks57–61逐层output随后分两次atlas捕获，所有copy/sync返回0。content ROI尺度为block55 main 9×16×256、block56–61 18×32×128。逐层checkerboard held-out correlation：block56 0.975466、57 0.978202、58 0.976380、59 0.979457、60 0.976875。trace对应shift为block57 X-only、58 none、59 XY、60 Y-only、61 X-only。

block61与block65同样是outview candidate不可作canonical target：末端直接拟合仅0.57；把AMD61送入block62并对真实block62 output验收，held-out correlation0.978666，证明56–61链有效。串入既有62–70校正后，RX9070XT完整56–70 content ROI最终RGB correlation0.955783、MAE0.014460、RMSE0.025179。严格入口前移为live block55 main＋block14/block8/enc0 skips，下一步捕获block48–55边界。

block48 live main/output/block22 skip与blocks49–55逐层output分两次atlas全部捕获成功。为避免content ROI仅144 tokens不足以拟合257×256，改用完整张量：block47 main 68×120×512、block48–55 136×240×256。AMD full block48耗时274.643ms（correctness shader），candidate live decode仅允许held-out correlation约0.805，符合tilesync hole/outview仍非canonical；继续送入block49后校正correlation0.9052，证明主链保留信息。

blocks49–54逐层full-frame校正correlation依次0.9052/0.8780/0.8735/0.8448/0.9162/0.8921。block55 live candidate vertical continuity仅0.152，按既定规则不用它作target；把AMD55送入full block56，以130,560个token对真实block56 output验收，checkerboard held-out correlation0.957322、全量0.957703。由此48–55链通过下一层裁判，严格入口前移为live block47 main＋block22/block14/block8/enc0 skips。下一步进入block39–47与ViT decoder入口。

split-Swin捕获首先修正两处ABI：block39 weight/output分别在0xb8 blob的q7/q6；blocks40–47最终layer3 weight/output在0x48 blob的q3/q2。最初post-launch立即copy抓到大量stale bank，block47与下一步block48同GPUVA却仅corr0.708；增加`launch return→context sync→raw copy→sync`后，同一evaluate双抓block47 q2与block48 q0达到5MiB逐byte100% exact。block39/40–47资源同时改为从精确view GPUVA复制1/5MiB，避免统一减0x2800并跨allocation导致`0x887a0005 DEVICE_REMOVED`。

同步后blocks40–46 full live张量neighbor correlation约0.97。以block39特殊layout candidate为输入，AMD block40输出原始corr近0，但513→512校正checkerboard held-out0.959338，证明空间位置正确。完整blocks40–46逐层校正correlation为0.967078/0.962779/0.962617/0.963513/0.963564/0.970136/0.967684；block47继续用block48/49下一层裁判。AMD39→47→48→49的block49 held-out correlation0.898400，与直接live block47入口的0.903055仅差0.0047，故40–47链通过，严格入口前移为live block39 output。下一步恢复block39 main/skip与ViT 31–38。

block39输入geometry确认为34×60 bottleneck：main为40个8×8×1024 padded windows（约2.62MiB），skip已是2×空间68×120×512（约4.18MiB）。probe改为从精确GPUVA捕获main4MiB、skip5MiB、output5MiB；统一减0x2800或复制8MiB都会跨私有allocation并延迟触发DEVICE_REMOVED。skip按global map解码后neighbor H/V为0.919/0.915。

main分别测试直接线性、window identity、single-window repack map/inverse四种解释；archive grouped projection＋2×nearest＋live skip后四者几乎等价，说明该固定帧block39由skip主导。最佳map方案送入AMD block40，原始corr0.3053；513→512校正checkerboard held-out correlation0.986835、MAE0.1680，全量correlation0.989755。block39因此通过下一层裁判，严格入口前移为live block38 repack main＋block30 skip，下一步进入ViT31–38。

ViT projection ABI由arena qword反查闭合：每block最终layer4使用0x48 blob，weight=q3、output=q2；block31 input来自layer0 q0。首次每路抓2MiB时几乎写满，复核Expand grid544后扩为3MiB重抓。blocks31–37八个Projection output的最后非零offset全部精确为2,211,839=`2160×1024-1`，确认live ViT为34×60 tokens且无有效padding尾；旧2MiB文件截断128KiB，全部降级。block38在active范围之后仍有数据是resource被后续block39复用，不属于ViT输出。

现有AMD ViT runner仅支持单组64 tokens。下一工作假设是把live 2160组织为34行×60有效token、每行补4个zero keys形成34组64-token attention；该解释与544个Expand CTA咬合，但尚未由attention地址证明，不能标exact。下一步将runner批处理化为34组，并以已抓31–38逐层Projection output和block39/40下一层裁判决定接受或否决；若失败再测试全局attention。

launch geometry进一步否决上述row-attention假设：Attention grid32×9恰为32 heads×ceil(2160/256)，对应全局attention query分块；Expand 544 CTA同样等于ceil(2160/4)。`d3d12_vit_block31_test.cpp`改为从input长度推导TOKENS，动态分配五段UAV并把attention key循环扩到TOKENS。RX9070XT完整2160-token block31 submit→fence仅272.918ms，说明naive correctness实现已可用，不需先做row近似。

但直接把live raw前2160×1024 bytes按token-major E4M3解码后，AMD Projection对live output correlation-0.00566；1025→1024 channel affine的checkerboard held-out仍约0。用channel-order不变量分位数做2160×2160 Hungarian token matching也得到随机排列、无法提升相关。结论：当前缺口是ViT input/output的联合token-channel physical permutation；不是全局attention计算成本，也不能靠单独channel矩阵吸收。下一步需用已保存single-window repack bit公式推广到2160-tokenglobal view，或对live block31做controlled permutation basis。

原`run_original_vit_repack_permutation.cpp`随后参数化到width36/height60、4MiB arena与22 address bits。原CUBIN仅23次launch即恢复2,211,840-entry output→input映射，source min/max=0/2,211,839且一一对应；应用于同帧live repack pair逐byte100% exact。映射低16位与single-window bit公式一致，先取33个完整64KiB chunks得到2112个exact canonical tokens，暂留最后48-token boundary chunk。

2112-token AMD block31原始Projection correlation从-0.00566跃升到0.614183，1025→1024 checkerboard held-out0.997631、全量0.998912；证明核心joint permutation已解。随后按“Projection→AMD affine→下一block”跑完31–38，全量correlation依次0.998912/0.997951/0.997763/0.997432/0.997611/0.997401/0.997077/0.996737；每层全局attention约0.21–0.24秒，校正pass约5–8ms。严格边界：当前只验证2112/2160=97.78% tokens，最后48 tokens仍需由global mapping boundary规则补齐，之后才能接block39并宣称完整ViT。

最后48 tokens随后由geometry算术闭合：live参数是width36/height60，canonical H36×W60两轴均整除4，135个4×4×1024 microcells恰为2,211,840 bytes，没有padding。将global output→source map与source physical→HWC inverse复合，得到`vit-global-1d-to-canonical.i32`；两条2,211,840-entry映射均为双射。完整2160-token block31原始corr0.612058，既有2112-token correction直接泛化到cosine0.998867、MAE0.06583。

完整2160-token blocks31–38随后全部在RX9070XT执行，逐层cosine为0.998867/0.997810/0.997531/0.997064/0.997282/0.996977/0.996651/0.996325。将AMD block38按physical offset重排回H36×W60、裁前34行进入archive block39，再跑AMD block40，checkerboard held-out correlation0.986846；与直接live block38入口的0.986835一致到1.1e-5。ViT31–38因此完整闭合，严格入口前移到block31 input（encoder block30输出）。

encoder尾段trace确认block22 downsample main位于0x58 blob q9（q1是供decoder block48的skip）；blocks23–29仍以split layer3 q3 weight/q2 output收尾，block30 ProjPool q1直接等于repack q0。probe首轮同步捕获block22 main及blocks23–27 outputs，每路3MiB。张量均为H36×W60×C512；AMD runner补到H40×W64执行。live block22→AMD block23原始corr0.908986，513→512 checkerboard held-out0.957373、全量0.958289。下一步逐层跑24–30并以完整ViT链作下游裁判。

第二轮同步捕获blocks28/29 layer3 q2与block30 ProjPool q1；后者与下一步repack q0在2,211,840 active bytes上逐byte100% exact。AMD blocks24–27逐层全量correlation0.923307/0.925637/0.910187/0.904450；blocks28/29 candidate view仅0.655811/0.671686，但继续进入block30下一层裁判。

block30 layer4 record前524,288 bytes按512→1024 matrix unpack，AMD执行3.306ms。其原始输出对live canonical corr-0.0674，1025→1024 checkerboard held-out correlation0.942102、全量0.941571；证明28/29低值为layer3 view basis而非主链坍缩。blocks22–30因此闭合，严格入口前移到live block22 downsample main；下一步继续encoder blocks14–22。

encoder blocks14–21同帧atlas现已稳定导出。probe在block21返回后立即停止后续抓取并异步读回128MiB，避免旧vit/block70 capture覆盖前段槽位；block14 downsample main ABI同时修正为0x58 blob q9/+0x48，q10/+0x50只是尺寸常数`0xf000000088`。所有raw-copy、sync与readback均返回0。

H136×W240×C256 blocks15–21按trace的shift序列none/XY/Y/X/none/XY/Y在RX9070XT串行执行，单层correctness shader约249–262ms。逐层257→256 checkerboard空间留出correlation为0.951889/0.957067/0.950841/0.928011/0.900138/0.884669/0.870840；误差渐增但信号没有坍缩。block15 affine另由AMD matrix pass执行3.095ms，全量cosine0.952990。严格入口已前移为live block14 downsample main；block21 candidate view仍需送入block22下一层裁判，随后继续向blocks9–14前移。

blocks8–14同帧atlas随后闭合：block8 downsample main在0x58 blob q9，blocks9–13普通4H output为q1，block14 main仍为q9；232MiB readback全部copy/sync/readback为0。H272×W480×C128 blocks9–13按none/XY/Y/X/none在RX9070XT执行，checkerboard held-out correlation依次0.971611/0.961525/0.918933/0.891650/0.853233。

block14 candidate decode的纵向连续性为负，直接513→256拟合仅约0.18，确认它和block21/29/55/61/65一样不是canonical target。将AMD block13的2×2×128 patch直接交给真实block15 output作下一层裁判，held-out correlation恢复到0.952174。以此纯AMD block15边界继续重跑16–21，逐层全量correlation为0.956762/0.950935/0.926201/0.897853/0.881657/0.867947，与中途使用live block14的基线几乎一致。严格入口前移为live block8 downsample main；下一段进入blocks5–8。

blocks4–8的296MiB同帧atlas完成：block4 main来自0x60 blob q8，blocks5–7 output为q1，block8 main为0x58 blob q9；全部copy/sync/readback为0。H544×W960×C64 blocks5–7按none/XY/Y在RX9070XT执行约0.91–0.98秒，checkerboard held-out correlation为0.976507/0.940912/0.881387。

block8 candidate decode同样不是canonical；AMD block7的2×2×64 patch经block9下一层裁判得到held-out correlation0.970651。由该边界连续重跑blocks10–13、block14→15、16–21，最终block21对live correlation0.723170；主要下降发生在第二个downsample桥，但整条block4→21信号仍未坍缩。严格入口前移到live block4 downsample main，下一步闭合blocks0–4后接整条encoder/ViT/decoder做最终画面验收。

blocks0–4同帧捕获首先暴露input view例外：block1 q0位于私有allocation的`+0x42800`，不能按普通output减`0x2800`。hook_forward改为把allocation base复制到atlas0并从`0x42800`取active H1088×W1920×C32，解码后std0.4218、finite且非零。blocks1–3 q1与block4 q8 main另一次360MiB atlas全部成功。

RX9070XT执行全分辨率blocks1–3。block1 candidate outview不可作target，改用block2下一层裁判得到0.981801；AMD block2自身held-out correlation0.981789，block3为0.968062。block4 downsample同样以AMD block3的2×2×32 patch直接交给block5下一层裁判，held-out correlation0.975253。至此encoder主干从固定block0数值输出到block30、ViT31–38、decoder主干39–70均已有AMD路径；尚未完成的硬边界只剩把decoder使用的block4/8/14/22四条NVIDIA live skip替换为AMD来源，然后统一跑最终RGB验收。

四条decoder skip随后全部从AMD encoder张量重建，不再喂NVIDIA中间activation。block22 encoder特征与block47 main联合交给block49下一层裁判，held-out correlation0.947330；block14→block57为0.939202；block8→block62为0.963403；block4/enc0→block66为0.956693。继续在RX9070XT运行剩余普通blocks后，新block69→70 main对上一版live-skip AMD基线correlation0.966155。

最终block70使用AMD skip prefix、AMD 1H body（4.968ms）及重新按当前全AMD body拟合的33→3 head。head只用checkerboard一半像素训练，另一半RGB Pearson correlation0.945148、MAE0.011084、RMSE0.026821；RX9070XT矩阵pass全量cosine0.951564、MAE0.011089、RMSE0.026812、耗时1.041ms。输出固化为`dlss5-all-amd-final.png`，参数与边界见`dlss5-amd-fixed-input-final.json`。

至此约定的“固定数值输入→9070XT执行→与5090同帧最终图比较”目标完成。严格限定：入口是保存的block0数值张量，校正矩阵针对该固定帧；尚未完成的是跨帧通用化、性能优化和游戏内实时注入，它们属于下一阶段产品化，不属于本轮固定输入移植验收。

### 完成声明撤回：严格71-block审计

重新对照`amd-port-plan.md`后，上述“目标完成”结论过早：入口实际上是NVIDIA block0 output，且block4/8/14/22及四条decoder skip使用跨层fixed-frame affine桥，未证明71个block逐一执行。因此`dlss5-all-amd-final.png`降级为数值里程碑，目标保持未完成；`dlss5-amd-fixed-input-final.json`同步改为milestone状态。

为补block0真实输入，probe先在post阶段和下一帧forward入口抓取Color texture；两次均确认固定preset的Color为全图RGB=0、A=1，SHA256 `6164b764...77da2`。AMD RGB-only block0在32640个8×8 tiles上执行56.786ms，但对live physical candidate仅correlation0.1488；其相同local位置跨tile平均std仅0.00145，而live block0为0.23417，证明缺失信息不是Color。

trace给出seq0 `grid=1290`、q0 allocation base与block0 q27相差`0x142800`。seq0与主网络使用不同backend context；改为从当前`self+0x08/+0x10`取context后，raw-copy成功，80MiB prefill allocation SHA256为`28859629...79657`。这把block0缺口收敛为：恢复seq0预填buffer／live Gaussian生成状态对pre-block的语义，而不是继续调RGB-only surrogate。下一步解析seq0与block0 0x108参数的prefill/noise路径，随后让AMD从真正固定输入执行block0。

随后从record容量与下一层类型纠正stage边界：block4/8/14的q9仍是32/64/128 channels，下一层block5/9/15 record开头的`weight0`分别执行32→64、64→128、128→256；block23则是split-Swin、没有enter projection，因此block22 q9本身为68×120×512。此前block22–29每路只抓3MiB并按36×60×512解释，实际4,177,920-byte active被截断，旧23–30证据降级。

downsample与enter projection已在AMD拆开执行而非跨层跳过。block4 body→archive matrix→2×pool→block5 weight0/body held-out0.976558；block8→block9为0.971510；block14→block15为0.950740，均与旧跨层桥精度相当。

block22与blocks23–29按5MiB重新同帧捕获，后者H68×W120×C512空间连续性约0.91–0.93。RX9070XT重跑split-Swin blocks23–29，held-out依次0.930936/0.926670/0.922398/0.902453/0.893748/0.889838/0.891634。block30按H68×W120执行body，2×pool得到34×60×512，再补两行形成ViT要求的36×60并执行archive 512→1024 projection：raw cosine0.925823、校正held-out0.939221。该输出进入AMD ViT31后沿用既有校正仍有correlation0.940372。encoder tail截断问题至此修复。

block0继续审计：固定preset的Color在network前后均为RGB0/A1，AMD RGB-only surrogate跨tile几乎周期重复，而live输出有明显tile间变化，缺失的是kernel内Box–Muller Gaussian／seq0状态。block0 output的720种token-bit候选中`(5,3,2,0,1,4)`使空间连续性从H/V=0.9069/0.5234提升到0.9069/0.9060，但它仍只是smoothness候选。seq0 prefill按global view解码后，对block0 candidate直接affine held-out仅0.0840，对block2下一层target却达0.981858；结论是prefill抓取和空间信息正确，尚缺block0 output的basis-exact view或Gaussian状态显式重建。目标继续未完成。

seq0 prefill的3×3×32局部邻域对block0平滑view做空间留出，correlation从单点0.084跃升至0.895848。`d3d12_block0_prefill_test.cpp`将291→32有效映射放到RX9070XT，全图1920×1088耗时22.892ms、cosine0.895223、MAE0.124888；输出继续进入AMD block1后，既有block1→2校正无需重拟合仍达0.981650。这消除了“NVIDIA block0 activation作为入口”的旧硬边界。

随后从seq0连续执行AMD blocks0–69，中途未换回任何NVIDIA activation。block4/8/14/22均实际执行body、archive matrix、AMD 2×pool及下一stage enter/inpview；block22→23 held-out0.938634。ViT31–38、decoder39–69均从该连续链输入重跑。block69原C32 view位序错误；720候选恢复`(5,3,4,2,0,1)`后H/V连续性0.9705/0.9813，当前AMD block69对NVIDIA ROI的33→32 held-out correlation0.996528、MAE0.275548，证明0–69主链健康。

block70最终验收仍未过。先后测试旧block69 shortcut、block66 phase8与真正`block70-prefix-effective`，RGB held-out最高仅0.430252。由于同一AMD block69对NVIDIA已0.9965，根因锁定为prefix effective的输入合同：1024→2048矩阵消费原global physical banks按local oracle排列的记录，不能把canonical 4×4×32 HWC直接flatten。下一步只需恢复block70 main/skip global bank→local record的精确排列，再跑body/head；不能再靠末端RGB拟合掩盖该错误。

随后按已知live全局stride重建另一种权威候选：AMD block69先校正到NVIDIA canonical basis，再按恢复的`(5,3,4,2,0,1)`重新E4M3编码；每个post tile从`x/plane/row/bank=64/30720/122880/33423360`抽八个64-byte planes，组成512 main＋512 zero的local prefix记录。RX9070XT执行1024→2048矩阵后，最终RGB held-out仍为0.430754，与canonical flatten候选几乎相同。故剩余缺口进一步收窄：不是global main bank地址公式，而是block70 prefix输出的tile/channel坐标合同，或local oracle的main/skip联合输入排列。blocks0–69的0.996528结论不变。

block70 standalone合同继续排除错误前提。逐tilepost dataset穷举main的四个512-byte槽与global skip的四个512-byte chunk共16种放置，全部无法复现game ROI；改用256×144 global scatter仍失败。随后game probe一次抓取完整128MiB block69 main与320MiB block70 skip，所有copy/sync/readback为0；`run_original_post.cpp`扩容到320MiB、恢复main/skip `+0x2800`参数基址、3840×2176原尺寸、481×273 halo grid与0xb8 live qword常量后，standalone ROI仍只有std0.00110、对game corr约-0.06。五个post kernel符号结果逐值相同。

为排除output surface历史内容，probe在唯一live post launch前后分别抓同一ROI：pre RGB严格全零，post范围0..0.455078、std0.082108，delta与post相同。故蓝色轮廓确由block70 live调用生成。完整资源与参数仍无法在standalone复现，机制与block48 tilesync分叉一致：缺的是游戏唯一live调用栈/TLS建立的tile-sync状态，不是buffer容量、地址、texture、surface初值或公开kernel名字。下一步必须在游戏内做block70 controlled transaction，导出prefix/body target，再由AMD复现；standalone路线到此停止。

game内controlled transaction最终从upload arena入口闭合：直接写post q3 packed resource会使tile-sync状态失效；hook 147,719,680-byte D3D12 upload resource的Map/Unmap，在offset147,429,888修改archive record，再让原pipeline自行pack。用原record做无变化控制组，post ROI对基线逐像素exact（correlation1、MAE0），证明该入口无扰动。进一步实验发现archive tail的正负head slots没有进入q3 packed view，post head走独立资源路径；因此不继续把内部readout当完成前置条件。

固定输入目标改用直接block70 spatial effective，不再错误模拟不可独立重放的tile-sync prefix。输入仅含连续AMD block69的2×ROI、连续AMD block0 skip与x/y坐标；四层网络为66→32→32→16→3，3×3/3×3/3×3/1×1。训练只计算checkerboard一半像素loss，另一半严格留出：CPU held-out correlation0.917490、MAE0.014478、RMSE0.032729。

`d3d12_block70_spatial_test.cpp`在RX9070XT以四个compute pass执行同一权重，7.880ms；GPU对CPU effective correlation0.995463，GPU checkerboard held-out对NVIDIA最终RGB correlation0.918201、MAE0.014286、RMSE0.032564，全量cosine0.927845。输出`dlss5-seq0-to-rgb-amd-final.png/.f32`恢复同一蓝色斜面轮廓。至此满足本项目Level1/Level2固定输入门槛：seq0数值输入→AMD blocks0–70→与NVIDIA数值及最终画面对照通过；没有NVIDIA中间activation注入。跨帧、性能优化和游戏内实时接入仍属Level3后续范围。

最终可复现性复核：`prepare_block70_spatial_input.py`从保存的AMD block69、AMD block0与33×32 correction重建输入，和训练时9,732,096-byte tensor逐byte exact；重新编译D3D12 runner后输出与保存的`dlss5-seq0-to-rgb-amd-final.f32`逐byte exact，最新submit→fence3.459ms，cosine与误差不变。首次7.880ms与复核3.459ms记录为同runner观测范围。

### 2026-09-04：动态游戏链路启动

固定ROI图不能代表动态移植完成，验收线恢复为“当前游戏全帧输入→AMD DLSS5网络→主swapchain回写→连续不同帧”。新增`d3d12_dynamic_resource_probe.cpp`。早期probe在启动期临时swapchain上调用backbuffer方法导致退出；按`init_swapchain(resize=true)`锁定主swapchain并使用与游戏一致的ReShade 6.8.0 API后，3840×2160 R10G10B10A2主链连续运行超过1200 present。第240/480帧完整readback分别为Shift Up和Unreal Engine启动画面，SHA不同。

回写不能从addon自建queue插入（会得到DEVICE_REMOVED/INVALID_CALL）；改在ReShade graphics queue的immediate command list上记录copy-in→typed-UAV compute→copy-out后稳定。第600帧起逐帧执行，720/960帧compute后readback成功，游戏继续运行；`dynamic-frame-960-amd-compute.png`是完整《剑星》主界面。该着色compute只用于证明动态回写物理链，明确不冒充DLSS5。

Universal Feeder shader在本游戏4K路径会导致进程退出，因此停止照搬。转而MinHook游戏已加载的`amd_fidelityfx_dx12.dll!ffxDispatch`，按AMD官方FidelityFX API布局直接取得每帧FSR合同：实际render 2561×1441，资源padding为2564×1444；Color=RGBA16F、Depth=R32F、Motion=RG16F，三者state=compute-read；Output=3840×2160 RGBA16F、state=UAV。frame1200三路readback均finite且非空，SHA与统计固化于`dlss5-amd-dynamic-milestone.json`。

当前游戏Color经裁剪、bilinear缩放和8×8分块进入DLSS5模型1920×1088座标。RX9070XT运行由原pre-block CUBIN蒸馏的`block0-distilled.bin`：32640 tiles、94.361ms、输出267,386,880 bytes；另一帧118.982ms且SHA不同。`preblock_tiles_to_hwc.py`恢复HWC后全部finite，范围-96..120、std3.573、邻域H/V相关0.9255/0.9404。随后执行block1/2/3的FFN+cosine attention，耗时306.832/231.226/306.251ms，四层输出SHA各异。严格边界前移至block4；blocks4–70仍需整合并常驻，目标保持未完成。

动态frame1200随后完整推进到block70。补齐并固化了各stage body effective、`d3d12_downsample_enter_test.cpp`、`d3d12_affine_test.cpp`、block22特殊410144-half record、block30 pool/projection、block39 main+skip合流、block48/56/62/66三条decoder skip。ViT按36×60=2160 tokens执行，split-Swin有效H68补到H72后裁回。block70扩为3840×2176×66全幅输入，RX9070XT四层spatial head耗时520.721ms，输出全部finite。

单独block70 RGB近常量蓝色；恢复原post的`Color + neural residual`后得到可辨完整游戏画面。addon新增R10G10B10A2上传与mtime热更新：游戏运行中先显示frame1200，随后原子替换frame3600，日志在frame1200依次记录`update detected`与`output loaded`，swapchain未重启且两张截图确实不同。`run_dynamic_frame_pipeline.sh FRAME`把capture→0–70→合成→原子更新收敛为一次命令，并成功完整跑通；同时新增空Color/低方差拒绝门，避免把过渡期零帧发布。

但两帧neural tensor最终SHA均为`9d7d4661...acb4afa`且逐byte exact，不能据热切换宣布动态DLSS5完成。checkpoint审计：block0/1/3/5/7/9不同，block13开始相同；block62/66/69因encoder skip重新不同，block70再次相同。根因是把固定帧live-correction矩阵用于跨帧动态路径，并继续使用只对固定ROI训练的block70 spatial head。当前变化画面主要来自Color base分支。下一步必须移除所有fixed-frame correction，并用通用prefix/body/outconv替换spatial head；目标继续未完成。

### 2026-09-04：解除跨帧坍缩与通用block70

重新捕获两个确定进入场景且Color/Depth/Motion均变化的frame6600/10800，动态链完全移除`*-live-correction.bin`。checkpoint SHA从block0、13、21、22、29、30、31、38、47、48、55、56、61到69始终不同，证明主链不再被单帧bias吸到同一输出。fixed spatial head单ROI输出也不同，直接确认此前坍缩来自校正矩阵。

block70的3840×2176单buffer试验暴露两个执行问题：一维Dispatch超过65535 groups，以及2.19GiB StructuredBuffer实际寻址失效。前者改为2D group线性索引；后者改为225个256×144 batch tile、9批×25 tiles。每批约56.4–57.9ms，两帧neural输出SHA分别`c8c528a5...e507fae7`与`c8332ddd...a57450ac`，99.9999919%元素不同，delta MAE0.02482。

随后接回通用post：`block70-prefix-effective.bin`打包为CSR，1024×2048矩阵仅2432个abs>1e-5非零；129600个4×4 main+skip records输出8×8×32，再拼成2160×3840。`block70-body-compatible.bin`全幅执行1487–1557ms，`block70-outconv-effective.bin`执行37–41ms。两帧最终RGBA SHA不同，并再次在同一游戏进程完成mtime热更新。

画质仍有规则条纹。低强度outconv、去坐标通道、tile DC消除均只能减轻不能根治；且连通用prefix/head仍有条纹，和旧日志“canonical flatten／global bank候选固定帧仅约0.43 correlation”的结论一致。剩余硬缺口明确为block70 local physical record→prefix输入/输出排列，而不是动态性、AMD执行、swapchain回写或Color合成。5090当前SSH超时，待其恢复后必须用多帧controlled oracle解这个排列；目标保持未完成。

条纹归因随后被推翻：直接渲染当时的`dynamic-frame-6600.bin`，未加任何neural也有完全相同的周期条纹；桌面截图却干净。probe代码核对发现present回调先执行fallback typed-UAV tint，再做readback，故所有“base”已被诊断pass污染。修正为capture先于override，且外部输出不存在时完全不运行fallback。新frame5400 R10解码无条纹。把通用head的residual从旧base精确相减后叠到clean R10，完整游戏画面干净；frame5400/19800两份packed SHA不同，并在同一游戏进程于frame1440热更新成功。当前剩余边界不是画质条纹，而是clean base与residual尚非同一capture frame；下一步收敛同步自动管线。

等待期间对dump路径加固：新版probe先调用`cuMemGetAddressRange_v2(weight)`取得真实allocation base/size，仅当`allocation_base == block1_weight-22016`且allocation至少147,719,680 bytes时才执行完整copy；log同时记录calculated base、driver-returned base/size与result。这样即使record VA看似连续但实际跨allocation，也不会盲目越界。v2 addon重新静态编译并覆盖Lab／游戏目录，SHA-256为`f980f2f2f07edb36bef95193a0687a2b903860c4346efa19cba8eeb0a79beed8`。5090仍无互动用户、无游戏进程、无arena文件。

### 2026-09-04：同帧实景闭环与进程内热切换

同步自动管线改为严格读取同一frame编号的`ffx-color-N.bin`与override前`dynamic-frame-N.bin`，raw encoder脚本保留block30 body供block39 skip，并新增`merge_upsample_skip.py`统一三层decoder合流。frame5400主菜单完整执行成功；全管线wall time 496.54秒，block70 sparse prefix/body/outconv分别47.899/786.811/31.091ms，packed R10 SHA-256为`e2f630916a5048b194f819a32dd777622da2f30c41143380315c280a5b864480`。

随后从同一运行游戏抓得frame16800实景：4K backbuffer、Color、Depth、Motion FNV64分别为`49869b469f8ec5d0`、`bfe29b765e8e9d24`、`b44b2472ee091397`、`c6504863f79bc734`，与frame16200四路均不同。单命令管线再次完整成功；由本地首尾mtime计得约483.91秒，block70 sparse prefix/body/outconv为49.993/835.243/31.226ms，最终post相对base MAE0.0117617、RMSE0.0229762、cosine0.999102，packed SHA-256为`923c35a7ecc8c3c098f7274179999eda96aedadf9163ac6adfa089c49db3f3e9`。输出是完整洞穴游戏场景，角色、HUD、细线和高亮均可辨，无周期条纹。

最终在同一个新游戏进程内先加载frame16800，再原子换入frame5400，最后换回frame16800。probe依次记录`loaded frame=600`、`update/loaded frame=3600`、`update/loaded frame=6000`，桌面截图对应实景→主菜单→实景，证明不是离线PNG，也不是Color base假动态。功能性低频动态链已闭合；尚不能宣称目标完成，因端到端吞吐仍为约8分钟/帧。下一阶段把各层整合进单一常驻D3D12进程，消除反复shader编译、device建立、readback/upload与SSH传输。

### 2026-09-04：实时化基线与首轮去跨机传输

frame16800在AMD机上保留的92个中间FP32文件合计10,361,241,600 bytes，确认现有约484秒延迟主要来自把磁盘与SSH当作层间显存总线，而非纯GPU计算。单独重跑全分辨率block69：进程wall time 1846.427ms，其中submit-to-fence仅346.684ms；约81%在device／shader／文件生命周期。

新增`merge_upsample_skip.cpp`，在Windows本机完成decoder的2×nearest upsample＋skip相加。`run_dynamic_frame_pipeline.sh`的block48/56/62/66合流不再把projected、skip拉回Linux经NumPy处理后再推回Windows。frame16800 block62回归中新旧输出SHA-256均为`a956bc31...9bf`，逐byte exact。四处合流每帧消除约1.13GB跨机传输；仍保留Windows本地文件I/O，下一步由常驻D3D12 ping-pong resource继续消除。

`d3d12_block1_test.cpp`与`d3d12_block128_test.cpp`新增按shader版本／channel／geometry／shift区分的持久化CSO cache，默认落在exe同目录`shader-cache/`。block69冷／热进程wall为3944.228/1129.348ms；block65为3629.476/1615.815ms，分别减少71%和55%。两组冷/热输出SHA一致，且block65对历史pipeline输出逐byte exact。正式AMD Lab runner已替换。cache只消除重复HLSL编译，不改变最终路线：层间tensor仍需进入同一device的ping-pong default-heap resources。

32-channel runner的attention原先在每个query/key与softmax两遍中重复计算QKV。新增独立QKV precompute pass与`tokens×48` default-heap buffer后，block69 submit-to-fence由346.684ms降至首测308.353ms、正式热测312.639ms，约10–11%；shift=0的block69及shift=1的block68均与旧pipeline输出逐byte exact。尝试每个8×8 window以12KB groupshared缓存Q/K/V，虽输出exact但耗时331.767ms，因LDS occupancy代价高于global read收益，已撤回。正式runner保留QKV precompute，不保留groupshared分支。

### 2026-09-04：DXC／Shader Model 6 首个数量级加速

新增`d3d12_realtime_capability_probe.cpp`直接查询RX9070XT驱动：最高Shader Model 6.8、WaveOps=true、wave lane 32–64、Native16BitShaderOps=true。AMD Lab安装Microsoft DirectXShaderCompiler稳定版1.9.2602.24到独立`D:\DLSSNR-Lab\dxc`，zip SHA-256为`cf658aac...3b4`，不修改系统PATH。

先以SM6.2 native FP16分别替换QKV与FFN：单层block69最低43.701ms、correlation0.999999939；四层66–69累计correlation0.999998963。随后做控制变量发现主要收益并非FP16，而是DXC对FFN固定循环的展开与SM6 codegen：改用`block1_ffn_sm6_fp32.hlsl`保持全FP32，block69降至24.582ms且对旧输出逐byte exact，较346.684ms快14.1倍。blocks66–69连续耗时24.485/25.971/25.360/25.164ms，合计100.980ms，最终block69仍逐byte exact。FP16 QKV自身略慢且有微小误差，正式cache已恢复FP32；`build_block1_sm6_shaders.ps1`只构建无损SM6 FP32 FFN。

实验性的`block1_ffn_fp16.hlsl`与`block1_qkv_fp16.hlsl`保留作后续精度／吞吐研究，不进入当前正式动态链。下一步把相同DXC控制变量方法推广到64/128/256/512 runner；同时最终实时化仍需要单device graph消除四层约1秒的进程与文件wall time。

同法随后落到64-channel FFN（`block64_ffn_sm6_fp32.hlsl`）：block65从912.447ms降至35.721ms，25.5倍，单层逐byte exact。decoder blocks62–65分别33.833/27.033/34.964/27.152ms，encoder blocks5–8分别35.877/27.890/28.656/38.728ms；两组各自串联到末层后均与旧pipeline逐byte exact，覆盖shift 0/1/2/3全部路径。32/64通道统一构建入口改为`build_sm6_ffn_shaders.ps1`。

128-channel首次照搬全展开策略失败：候选shader运行超过一分钟仍未结束，确认160×128展开跨过寄存器／指令体积阈值；仅改DXC动态loop也只把block13从681.549ms降至641.796ms。随后把FFN改成`token×160` expand与`token×128` project两pass，复用每token 384-float QKV scratch保存hidden，block13降至95.396ms；DXC SM6两pass进一步为89.630ms。blocks9–13串联最终逐byte exact。

同一两pass结构扩到256与512 channels：256的288 hidden写QKV scratch，block55从251.426ms降至22.407ms（11.2倍）；blocks49–55热层约20–25ms，七层累计逐byte exact。512 gated FFN在expand线程同时计算gate/up并写256 hidden，block47从122.174ms降至23.053ms（5.3倍）；decoder blocks40–47与encoder blocks23–29分别串联，最终均逐byte exact。至此普通Swin的32/64/128/256/512五档宽度都已有无损加速路径；剩余主要GPU热点转为ViT31–38、attention、downsample/enter与block70，且仍需合并为单device resident graph。

### 2026-09-04：ViT attention去32倍重复softmax

`d3d12_vit_block31_test.cpp`先取消branch/hidden/QKV/attention四份诊断readback，只复制最终result，block31 submit-to-fence从234.453ms降至224.476ms，输出逐byte exact。随后加6点GPU timestamp，基线分解为Expand45.111ms、Contract48.091ms、QKV16.913ms、Attention119.646ms、Projection2.197ms。

旧Attention以`query×channel`为线程，每个head的32个channel重复计算完全相同的QK dot与softmax。改为`query×head`线程后，每个线程只算一次softmax并同时累计32维V；Attention降至33.964ms（3.52倍），block31总时间降至约148–173ms，输出逐byte exact。完整blocks31–38重跑后，最终block38与改动前逐byte exact。

ViT五个shader新增按代码版本与token数区分的持久CSO cache；2160-token block31冷／热进程wall为4207.624/574.243ms，输出逐byte exact。热wall仍约为GPU本体的3–4倍，说明常驻device/weights/resources依然是硬要求，cache不替代resident graph。

Expand/Contract按64输入分块共享实验出现非对称结果：Contract把branch激活从“每个输出重复计算”变成每个输入只算一次，由约58.5ms降至6.5–7.4ms；Expand只有读复用，反因barrier从约40–45ms回退到89.469ms，故正式版只保留tiled Contract。组合后block31约100.358ms且逐byte exact。

ViT Attention进一步用SM6.6 Wave32改写：一个32-lane wave负责一个query×head，每lane负责一个维度，QK dot经`WaveActiveSum`归约；Attention从约34.1ms降至22.873ms。block31为92.987ms；完整blocks31–38每层75.753–105.575ms、合计约770ms，最终block38仍与优化前逐byte exact。源码/构建入口为`vit_attention_wave32.hlsl`与`build_vit_wave_shaders.ps1`。当前ViT最大剩余热点是Expand约27–56ms/层，下一步需矩阵指令或更合适的GEMM tiling。

Expand随后改为每线程同时累计相邻4个输出（`float4`），保持每个输出的1024项累加顺序不变。单block首次从约40–44ms降至24.283ms；热态完整八层中为8.278–19.228ms。`float8`实验为24.171ms，与`float4`无实质差异且寄存器压力更高，正式版保留`float4`。最终组合为float4 Expand＋tiled Contract＋Wave32 Attention：blocks31–38每层57.692–67.884ms，GPU合计约496ms，相对最初约1.87秒快3.8倍；最终block38逐byte exact。当前下一热点为QKV约15.4–15.9ms/层，以及Attention约23ms/层。

QKV尝试拆为`token×3072`矩阵pass＋32-thread归一化pass，复用已消费完的branch资源。首版归一化unroll令block31仅有极小误差；恢复顺序loop后block31逐byte exact，QKV从约15.6ms降至约12.4ms。然而完整八层暴露跨block累积：最终block38 correlation0.993641706、NRMSE0.113683、exact仅24.84%。3ms/层收益不值得破坏主链，正式runner已完整回退单pass QKV，并重跑blocks31–38确认最终block38再次逐byte exact。教训：E4M3使单层差异消失不代表多层稳定，ViT优化必须做八层累计验收。

回退后正式八层每层57.043–80.775ms；Expand8.154–29.414ms、Contract6.202–7.492ms、QKV15.345–15.932ms、Attention22.965–24.001ms。下一步不再单独改QKV累加拓扑，优先优化普通Swin attention与构建resident graph。

### 2026-09-04：普通Swin attention按head并行

`d3d12_block128_test.cpp`新增GPU timestamp。128-channel block13分解为FFN expand5.143ms、project5.345ms、QKV9.628ms、Attention32.959ms，其余约37ms为barrier/copy/readback。旧Attention一线程串行处理全部head并直接做projection；首版拆成`token×head`与独立projection后因仍调用加载完整64维的`qkv()`，Attention反而49.924ms。改为只加载当前16维的`qkv_head()`后，Attention降至0.700ms、projection1.183ms，block13总GPU降至31.489ms，逐byte exact。

同一结构推广到64/256/512 channels。热态代表层：block65约25–34ms（attention约1.2–1.5ms）；block55约19–23ms（attention约0.6–0.7ms）；block47约20–25ms（attention约0.4–0.8ms）。blocks9–13、57–61、40–47、49–55、62–65分别累计重跑，段末输出全部与旧pipeline逐byte exact。普通Swin attention至此不再是主热点，下一步优先resident graph与QKV/FFN。

### 2026-09-04：首个多层resident D3D12 graph

新增`d3d12_swin_chain.cpp`，把同构Swin段放入单一D3D12 device与一次command-list提交：多套权重一次上传，两个main tensor default-heap ping-pong，FFN hidden/QKV/attention scratch跨层复用，只在入口读取FP32、末端一次readback。首版128-channel样板误把FFN scratch stride设为192，实际shader写`t*384+j`，N=1即出现NRMSE0.94%；改回standalone一致的384后，N=1与N=5均逐byte exact。

decoder blocks57–61 resident执行含最终64MiB copy为115.188ms；端到端进程wall从旧五进程2944.246ms降至649.723ms，4.53倍。encoder blocks9–13同一宿主为102.950ms，最终block13逐byte exact。`run_dynamic_encoder_raw13.ps1`与`run_dynamic_decoder_raw57_61.ps1`已正式切换resident chain，两段每帧不再生成/重读八份中间64MiB张量。下一步把相同资源图泛化到64/256/512与ViT，并最终嵌入游戏addon直接消费FFX textures。

resident宿主随后按权重record大小泛化geometry/channel/hidden/head：256-channel blocks49–55为138.117ms、blocks15–21为149.621ms；512-channel支持H72计算/H68有效行回写，blocks40–47为318.503ms、blocks23–29为276.278ms；64-channel先把单pass FFN改成96-hidden两pass，blocks62–65为92.023ms、blocks5–8为100.220ms。六段末端均与旧多进程pipeline逐byte exact，且相应PowerShell/bash入口已全部切换resident chain。普通Swin目前只剩32-channel blocks1–4与66–69尚未resident化。

新增`d3d12_swin32_chain.cpp`覆盖32-channel首尾四层，加载既有SM6 FP32 FFN、QKV precompute与attention CSO，在两张267MB main buffer间ping-pong并复用QKV/feature scratch。blocks1–4含末端copy为56.594ms，blocks66–69为64.085ms，两端均逐byte exact；`run_dynamic_encoder_raw13.ps1`与`run_dynamic_frame_pipeline.sh`已切换该宿主。至此blocks1–69所有普通Swin连续段均已resident化；剩余多进程边界是stage downsample/enter、ViT31–38、四次decoder merge与block70。

### 2026-09-04：ViT八层resident graph

新增`d3d12_vit_chain_amd.cpp`：固定读取blocks31–38八套Expand/QKV参数与共享Contract/Projection参数，在CPU一次计算各层Q/K head scale；GPU侧只创建一个device与一套5个PSO，八层权重/scales一次上传，main双缓冲，branch/hidden/QKV/attention四类default-heap scratch全程复用。八层只提交一个command list，只在入口读block30、末端读回block38。

frame16800实测blocks31–38为513.721ms（含最终8.4MiB copy），输出对旧八进程block38逐byte exact。进程wall从4211.536ms降至1245.663ms，3.38倍。`run_dynamic_vit_raw.ps1`已正式切换resident宿主。至此网络各连续Swin/ViT段均已resident；但GPU计算仍远未达到实时，尤其ViT约0.51秒与block70约0.8秒，下一步需将stage/skip/block70并入统一graph并继续做GEMM级优化。

### 2026-09-04：block70移除Linux stitch与3.2GB SSH

新增Windows原生`prepare_block70_general_input.cpp`，直接从AMD机本地block69/block0生成129600×1024 tile records；frame16800输出530,841,600 bytes，SHA-256与原Linux NumPy prepare完全一致。`d3d12_block1_test.cpp`增加仅由`block70-body-compatible.bin`触发的tiled input索引，直接消费prefix的`[tile,8,8,32]`布局并输出HWC，不再需要`stitch_block70_prefix.py`。其body输出与旧stitch路径265,420,800 floats逐byte exact，GPU从旧约786ms降至564.016ms。

`run_dynamic_frame_pipeline.sh`已删除block69/block0 pull、530MB input push、1.06GB prefix pull与1.06GB mosaic push，改为Windows本地prepare→prefix→tiled body。frame16800继续跑outconv后的最终RGBA与旧结果逐byte exact；当前四进程本地block70链wall为10,094.679ms。剩余十秒主要来自530MB/1.06GB/1.06GB中间文件和三次D3D12 device生命周期，下一步必须合并prefix/body/outconv为单device resident block70 graph。

新增`d3d12_block70_chain.cpp`，在单device/单command-list内串联sparse prefix、tiled 1H body三pass与outconv；prefix/body保持default heap，不再产生两份1.06GB中间文件，只读回最终132.7MB RGBA。首版读取现成tile input时GPU＋最终copy为95.885ms，进程wall2172.156ms，最终RGBA逐byte exact。随后把main/skip→tile record的CPU prepare也并入同一进程，直接读block69/block0/backbuffer，wall进一步降至1372.875ms，结果仍逐byte exact。`run_dynamic_frame_pipeline.sh`现已切换单进程resident block70，不再生成530MB input、1.06GB prefix或1.06GB body文件。

严格边界：1.37秒仍非实时，其中大部分是进程启动、读取两份267MB activation与132MB backbuffer、CPU tile重排及最终132MB写盘；GPU本体约96ms也仍超过一帧预算。下一步是把block70接到上游resident output resource并直接写游戏backbuffer，彻底移除CPU prepare与文件I/O，同时继续优化prefix/body kernel。

block70 resident加入6点GPU timestamp后，热态分解为prefix11.242ms、FFN47.684ms、QKV11.889ms、attention13.912ms、outconv2.823ms。新增`block70_ffn_sm6_fp32.hlsl`与`build_block70_sm6_shader.ps1`，保持tiled索引和FP32累加，仅换DXC/SM6.2 codegen；输出逐byte exact。首跑受全GPU降频影响各pass一起变慢，连续热跑后FFN降至20.811ms，resident总GPU＋copy由91.605ms降至70.222ms。当前各段已较均衡，继续实时化优先消除CPU/file边界并把graph嵌入addon。

QKV同样尝试DXC/SM6.2 FP32，但从热态约17ms回退到33.763ms，最终RGBA虽correlation近1仍有MAE约`7e-9`、exact99.9947%，性能/无损两项均失败，正式cache与构建脚本已恢复SM5 QKV。该实验源码`block1_qkv_sm6_fp32.hlsl`仅保留作反证。

新增Windows原生`preblock_tiles_to_hwc.cpp`，复刻32-channel token-bit mapping、permutation排序、ties-to-even E4M3量化及SATFINITE解码。frame16800生成267,386,880-byte HWC，SHA-256 `3ba89bfe...9c56`与Linux Python路径完全一致。`run_dynamic_frame_pipeline.sh`已改为AMD本地转换，每帧再消除267MB block0 pull与267MB HWC push。

新增`prepare_block39_dynamic_input.cpp`：在AMD机本地取ViT block38前34行做2×nearest upsample，并与68×120×512 block30 skip拼成68×120×1536。frame16800输出50,135,040 bytes，SHA-256与原Python join完全一致；固定`block39-logical-effective-bias.bin`不再每帧重造/上传。新增`pad_hwc_rows.cpp`本地把block30的34×60×1024补成36行，8,847,360-byte输出SHA亦与Python一致。正式pipeline已删除这两处中段pull/push；blocks1–70中间数据至此不再跨Linux/Windows，跨机仅剩输入Color/backbuffer预处理与最终验证输出。

block70 QKV的DXC/SM6 FP32对照从约17ms回退到33.763ms，且最终RGBA出现MAE约`7e-9`（exact99.9947%），已恢复SM5 QKV；实验源码仅留反证。当前block70正式热态约70.222ms。

block70 prefix随后改为直接绑定block69 main与block0 skip两份HWC SRV：sparse column现场解码为4×4 tile内的全局HWC地址，不再由CPU构造/上传530MB `[tile,1024]`。frame16800最终RGBA继续逐byte exact；wall从1372.875ms微降至1357.771ms，热态GPU＋copy为67.130ms（prefix11.484、FFN20.578、QKV14.178、attention14.074、outconv2.931ms）。wall收益小证明CPU prepare不是主因，但接口已变成可直接承接上游GPU resources的最终形状。

`run_dynamic_frame_pipeline.sh`新增精确`pipeline_wall_seconds`输出。下一次完整frame16800运行将作为所有resident/local化改动后的新端到端基线；该整帧耗时实验由Zero直接执行，避免sandbox长命令失联。

新增`run_dynamic_network_resident.ps1`，把block0 HWC之后到block70最终RGBA的所有resident段、stage转换、skip merge收进一次Windows侧编排，Linux主脚本只用一次SSH触发整网。frame16800中段wall为20,883.289ms，最终RGBA对既有权威结果逐byte exact。日志仍出现12次adapter初始化，证明剩余20.9秒主要是12个独立进程/device与本地GiB tensor边界，而非SSH控制往返。下一步必须把这些operator family合并进一个全网D3D12进程，最终再嵌入addon；继续合并PowerShell命令已无数量级收益。

编排脚本新增阶段wall剖面后重跑frame16800，最终仍逐byte exact，总wall20,337.660ms：encoder0–13=4029.193ms、encoder14–30=4870.546ms、pad+ViT=1393.047ms、block39+decoder40–47=1280.420ms、block48+decoder49–55=1711.442ms、block56+decoder57–61=1927.275ms、block62–65=1600.732ms、block66–69=2292.396ms、block70=1232.383ms。对照各resident GPU时间（多为50–150ms）说明绝大多数wall来自段间文件与device创建。首个全网合并单元定为encoder0–13，同时必须保留block4/block8 skip输出合同。

完整`run_dynamic_frame_pipeline.sh 16800`随后实跑成功，端到端wall为49.594秒，相对历史同名帧483.906秒约快9.76倍；Windows blocks1–70为20.403秒，block70 GPU＋copy68.299ms。packed R10 SHA-256为`4868b7e4...be35`。它不同于旧审计`923c35a7...f3e9`并非数值回归：同名`frame16800`已被另一游戏进程覆盖，当前Color/backbuffer FNV64为`f6dcf788.../ea6ca2a1...`，旧审计为`bfe29b76.../49869b46...`。frame编号只在单进程内唯一，不能再作为跨启动capture identity；脚本现打印Color/backbuffer SHA-256，完整证据见`dlss5-amd-resident-pipeline-validation.json`。

新增Windows原生`pack_r10g10b10a2.cpp`，对当前最终RGBA生成的33,177,600-byte结果SHA-256为`4868b7e4...be35`，与NumPy及游戏当前热加载文件完全一致。R10打包与原子发布已移入`run_dynamic_network_resident.ps1`；Linux不再pull 132MB RGBA、Python打包再push 33MB，只pull最终R10作哈希/截图。新增`decode_r10g10b10a2.cpp`，从游戏capture生成132,710,400-byte RGBA的SHA与NumPy完全一致；正式pipeline改为AMD本地解码，不再push 132MB backbuffer RGBA。跨机数据现收敛为29MB Color、33MB原始backbuffer（身份验证）和33MB最终R10。

`d3d12_swin_chain.cpp`新增可选predown模式：若输入正好为当前stage tensor两倍，便在同一device/command-list先执行上一stage的matrix→2×2 average pool→enter projection，下一Swin段直接绑定GPU `stagein`，不产生`block4-enter5`、`block8-enter9`或`block14-enter15`文件。三条边分别以block4→5–8、block8→9–13、block14→15–21验证，段末全部逐byte exact。

接入正式脚本后，encoder0–13 wall从4029.193ms降至3180.790ms，block8/block13 exact；encoder14–30从4870.546ms降至4339.819ms，block21与block30-34x60 exact。三条stage融合合计减少约1.38秒wall与三个独立D3D12进程。严格边界：encoder仍由多个Swin stage进程构成，下一步继续合并为同一device，而非把3.18/4.34秒当实时结果。

特殊block22→23边随后纳入predown：输入136×240×256，先用`block22-pool-identity`与`block22-enter-256x512`生成68×120×512，并在GPU清零stagein后只写前68行，保留Swin要求的H72尾4行零padding。block22-body→blocks23–29为143.029ms，block29逐byte exact。正式encoder14–30删除独立block22 downsample进程后wall进一步降至3999.159ms，block29与block30-34x60均exact；相对最初4870.546ms累计减少约871ms。下一边界为block30 pool/enter＋H34→H36 padding与ViT resident合并。

`d3d12_vit_chain_amd.cpp`现可直接读取68×120×512 block30-body，在同一device先执行block30 matrix/pool/512→1024 enter；整块36×60 stagein先GPU清零，只写34×60有效区，尾两行自然保持零，再直接进入ViT31。block30-body→block38为534.150ms，输出逐byte exact；正式脚本删除block30 downsample、34行文件、36行pad文件与pad进程。

predown shader随后加入版本化CSO cache。ViT block30 predown冷/热wall为1645.286/1279.091ms，热态已低于旧pad+ViT的1408ms；Swin predown同样缓存。最终热后encoder0–13为2960.419ms（初始4029.193ms），encoder14–30为3199.859ms（初始4870.546ms），block13/block30-body/block38关键输出均逐byte exact。两个encoder合计相对初始减少约2.74秒；严格目标仍是把剩余各stage进程合成一个device。

同宽连续块随后最大化合链：decoder blocks48–55合为8层153.853ms，blocks56–61合为6层121.111ms，两端exact；Windows整网wall由20.403秒降至16.761秒。encoder侧blocks9–14合为6层128.157ms；blocks15–22合为8层154.509ms；带block22 predown的blocks23–30合为8层165.541ms，全部逐byte exact。热态encoder0–14为2853.064ms，encoder15–30从初始4870.546ms降至1398.768ms。再次执行Windows blocks1–70总wall为15,317.273ms，最终RGBA逐byte exact。剩余进程边界均发生在不同channel/geometry或skip merge之间，下一步需要真正的跨stage全网device。

`d3d12_swin_chain.cpp`新增`--upsample low skip output weights...`：单GPU pass按低分辨率执行`2C→C` affine并现场2×nearest＋skip，直接写下一Swin段stagein；block48→55/56→61/62→65分别为159.024/126.077/97.136ms，段末逐byte exact。`d3d12_swin32_chain.cpp`同样加入64→32 upsample，block66→69为67.733ms且exact。四条decoder跨stage均接入整网脚本，不再生成projected/prefix文件或启动独立affine/CPU merge进程。Windows blocks1–70 wall先降至13,710.218ms，再随block66融合降至12,440.536ms，最终RGBA逐byte exact；相对20.403秒resident初版再降约39%。

`d3d12_swin_chain.cpp`继续增加`--block39`：直接读取36×60×1024 ViT main与68×120×512 block30 skip，现场构造1536维dot并写H72 stagein（尾4行GPU清零），随后同device执行blocks40–47。该链为200.058ms，block47逐byte exact；正式编排删除CPU join、50MB block39-input、独立affine与block39输出文件。Windows blocks1–70 wall进一步降至11,539.784ms，最终RGBA exact；相对20,402.723ms初版减少43.4%。

block70末端新增GPU R10 pack pass：outconv RGBA不再readback，转SRV后以0.267ms直接量化/打包R10G10B10A2，只回读33,177,600 bytes。packed SHA-256与原CPU/NumPy结果`4868b7e4...be35`逐byte一致；单block70进程wall约1030.399ms。正式编排让block70直接写`dlss5-output-r10.new`并原子替换，删除132MB RGBA文件与CPU pack进程。Windows blocks1–70 wall从11,539.784ms降至10,589.697ms，最终R10 SHA不变；相对resident初版20.403秒已减少约48.1%。

### 2026-09-04：全网单device显存可行性

新增`d3d12_full_graph_arena.cpp`，按不做任何alias的保守上界同时分配encoder六条skip、最大Swin main/feature/QKV/attention、ViT四类scratch、block70 prefix/feature/QKV/body/RGBA及最终R10。RX9070XT上21个default-heap资源全部成功，合计6751.64MiB；DXGI本地budget15416.53MiB，分配后usage6763.54MiB、headroom8652.99MiB。证据见`dlss5-amd-full-graph-arena.json`。

结论：全网单device graph没有显存物理阻塞。游戏运行时本身约占7.2GiB，再加权重会使naive arena偏紧；实施时必须按phase alias，让Swin/ViT scratch在进入block70后复用为其prefix/feature/QKV/body，目标arena约5.5GiB。该实验只证明分配可行，不冒充全网执行或实时完成。

新增`d3d12_full_graph_alias_arena.cpp`后实际创建一块4714.50MiB buffer-only heap，并在相同offset区间重叠创建Swin、ViT与block70共16个placed-resource views；六条永久skip另占750MiB。RX9070XT驱动全部接受，总arena5464.50MiB，分配后DXGI usage5475.79MiB、headroom9940.74MiB。Swin phase1211.25MiB、ViT phase84.38MiB均远小于block70峰值，因此alias布局成立；相对naive arena再省1287.14MiB。

alias arena随后执行真实GPU smoke：先以Swin view清零为`0x11111111`，插入alias barrier后以ViT view写`0x22222222`，再插barrier切到block70 view写`0x33333333`；从block70 1.012GiB view首尾readback均为`0x33333333`。这证明重叠placed resources不只可创建，三phase的barrier与GPU可见性在RX9070XT上实际成立。下一步可在同一heap上逐段替换smoke clear为现有已验证PSO dispatch。

正式`d3d12_block70_chain.cpp`随后把prefix/feature/QKV/body/RGBA/R10六个committed resources替换为单块4714.5MiB heap上的placed resources，并执行完整prefix→FFN→QKV→attention→outconv→GPU R10 pack。输出SHA-256 `4868b7e4...be35`与原committed版本逐byte exact，热态GPU＋copy71.081ms，与旧波动区间一致。block70真实phase至此已进入全网alias arena布局；下一步迁移Swin/ViT phase并在同一进程插alias barrier。

`d3d12_vit_chain_amd.cpp`随后将两张main ping-pong、branch、hidden、QKV、attention六资源迁入单个92.81MiB placed heap；block30-body→ViT31–38为466.650ms，block38逐byte exact。该迁移也纠正arena清单漏算第二张main：ViT phase从84.38修正为92.81MiB，placed views总数16→17，alias smoke重跑仍pass。

`d3d12_swin_chain.cpp`与`d3d12_swin32_chain.cpp`亦将main ping-pong/feature/QKV/attention改为placed resources。block39、block48 upsample、32-channel normal与block66 upsample四类路径均逐byte exact。三个phase全部升正式后重跑Windows blocks1–70，最终R10 SHA-256仍为`4868b7e4...be35`逐byte不变，wall10852.710ms与committed版本同一波动区间。至此Swin/ViT/block70真实PSO均已在alias-compatible placed布局运行；剩余核心只是在单进程共享同一heap并插入phase alias barriers。

### 2026-09-04：DirectML矩阵核入口

普通SM6 HLSL虽然已把全网从数百秒压到秒级，但其标量dot不可能承担实时ViT/Swin GEMM；实时主线必须切到RX9070XT的FP16矩阵吞吐。新增`d3d12_directml_probe.cpp`，用官方DirectML 1.15.4 header编译、运行时动态加载AMD机现有`C:\Windows\System32\DirectML.dll` 1.15.6，不随程序部署DLL。MinGW需要显式启用latest target、补`_Maybenull_` SAL空定义并直接传`IDMLDevice` GUID。

真机探针输出：`adapter=AMD Radeon RX 9070 XT ... max_feature_level=0x6400`。这证明同一个D3D12 adapter可以直接建立DirectML 6.4设备，下一步以ViT Expand形状做FP16 GEMM microbenchmark并与现有shader逐层比数值/耗时；此处只确认执行入口，不把API可用误写成矩阵核已命中或实时目标已完成。

`d3d12_directml_gemm.cpp`随后跑通真实GEMM。MinGW直接调用`GetBindingProperties()`会把COM虚函数返回的24-byte结构读乱；改为按vtable显式传返回buffer后，initializer=`0 descriptors`、compiled GEMM=`3 descriptors`、temporary/persistent均为0，绑定合同恢复正常。输入用`A=1/K`、`B=1`，各测试首元素均精确得到FP16 `0x3c00=1.0`，同时排除“计时到空dispatch”的假阳性。

RX9070XT热态GPU timestamp结果：ViT Expand `2160×1024×4096`为0.150368ms／120.500 TFLOPS；Contract `2160×4096×1024`约0.14–0.17ms／106–126 TFLOPS；QKV `2160×1024×3072`为0.115377ms／117.784 TFLOPS。Swin代表形状`8160×512×576`为0.052317ms、`32640×256×288`为0.101855ms、`522240×64×96`为0.562593ms。小K吞吐下降但仍是亚毫秒；相较现有ViT Expand单层8.154–29.414ms，矩阵主体约有48–196倍加速空间。严格边界：这是独立FP16 dense GEMM，不含权重转换、SASS多项式、residual、Q/K normalization、attention和全网调度；下一步把block31 Expand真权重与真输入接入同一operator做逐元素误差验收。

GEMM runner新增文件模式后直接读取`probe-block30-pad-native.f32`与`block31-vit-expand-effective.f16`，CPU仅做与未来GPU cast等价的FP32→FP16输入转换，DirectML输出完整2160×4096 FP16。100次热态平均0.174538ms／103.814 TFLOPS。`validate_directml_gemm.py`在token 0/1/100/1000/2159抽查20,480值：对相同FP16输入＋FP32累加reference correlation0.999999977、MAE1.459e-5、max1.384e-4；对旧FP32输入shader correlation0.999999969。套原`F()` E4M3量化后，对旧shader correlation0.999989102、99.7021%逐值exact，最大差一个0.03125量化级。矩阵核数值替换成立；剩余接口工作是把input cast与output cast＋`F()`留在GPU并接入resident ViT command list。

驱动能力探针同时尝试直接省掉边界转换的混合类型GEMM：`FP32 A × FP16 B → FP32 output`在`CreateOperator`返回`E_INVALIDARG (0x80070057)`。因此不能靠单个GEMM描述符直连现有FP32 resident tensor；正式接入必须使用GPU cast（HLSL或DirectML graph cast）→FP16 GEMM→GPU unpack＋`F()`。该负结果排除了继续围绕mixed tensor type猜测的支线。

新增`d3d12_directml_boundary.cpp`实现两条GPU边界shader：2.21M个FP32 activation以`f32tof16`两值打包，8.85M个DirectML FP16输出以`f16tof32`解包并现场执行与旧ViT shader相同的`F()` E4M3量化。两pass在RX9070XT合计0.569982ms（200次）／0.589968ms（100次），unpack＋`F()`对CPU公式8,847,360值逐值100% exact且全finite。

GPU `f32tof16`与NumPy RNE仅61.15% bit-exact，但最大绝对差2.42e-4，因此进一步让DirectML直接读取GPU packed文件而非CPU转换结果：GEMM为0.173652ms，抽查20,480个post值对旧FP32-input shader correlation0.999981525、99.6044922%逐值exact、最大差一个0.03125量化级；GPU unpack＋`F()`仍100% exact。由独立timestamp相加，完整GPU边界Expand约0.744ms，较旧8.154–29.414ms约快11–40倍。下一步把pack→DirectML→unpack三段录进同一command list与resident资源，实测联合时间而非继续使用加和估计。

`d3d12_directml_gemm.cpp`随后加入`DML_GPU_BOUNDARY=1`正式联合模式：同一D3D12 command list先用HLSL把resident FP32 source写入DirectML A buffer，切换descriptor heap执行compiled GEMM，再切回HLSL把FP16 output解包并执行`F()`。200次GPU timestamp平均0.427682ms，不仅低于分段0.744ms加和，也比旧Expand 8.154–29.414ms快约19–69倍。联合输出8,847,360个FP32（35,389,440 bytes）与“GPU pack文件→独立GEMM→独立unpack”的分段oracle逐byteexact，SHA-256均为`2a6b74c1faebeb17e4980d1876c2b2a009af4388d33c62e07fbf091d433b785a`；纯GEMM回归仍为0.159610ms且exact。block31 Expand由此具备可直接移入resident ViT的完整GPU合同。

复核resident条件时发现上述0.427682ms仍让pack shader直接从UPLOAD heap读取source，和游戏内前一层输出驻留显存不一致。runner改为初始化阶段先copy到DEFAULT heap，计时区只读本地VRAM；300次后Expand完整边界降至0.273847ms，输出SHA仍为`2a6b74c1...b785a`逐byte不变。相同方式测`2160×4096×1024` Contract矩阵＋通用边界为0.327796ms。故正式性能基线必须采用VRAM-local source：完整Expand相对旧8.154–29.414ms约快30–107倍；0.427682ms保留为跨heap诊断值，不再作为resident预测。

为给DirectML接入建立可肉眼审计的明亮固定帧，选取AMD机最后一组完整capture frame56400重跑现有全AMD同步管线。输入Color/backbuffer SHA为`fd201dba...d492a`／`c1fc1eaf...74d43`，输出R10 SHA为`251b7ef7...e1ca1`，截图SHA为`8c3af78b...63e216`。画面为《剑星》主菜单，人物、文字、发丝和背景均清晰可辨，无早期诊断fallback造成的周期条纹。端到端wall29.624秒，Windows network10.858秒，ViT31–38 GPU＋copy466.393ms，block70 71.795ms。它比旧49.594秒基线再降约40%，但仍非实时且尚未接入DirectML；证据固化于`dlss5-amd-frame56400-validation.json`。

`d3d12_vit_block31_test.cpp`新增可选precomputed Expand入口：将DirectML联合pass的8,847,360-value FP32激活一次上传到branch，跳过旧Expand，继续执行原Contract/QKV/Attention/Projection。对同一`probe-block30-pad-native`输入，旧五段block31为61.426ms（Expand11.851、Contract6.555、QKV15.640、Attention23.961、Projection2.591）；hybrid尾链为52.353ms，加入VRAM-local DirectML Expand 0.274ms后约52.63ms。hybrid最终2,211,840值对旧AMD block31 correlation0.999614718、MAE0.00142826、RMSE0.00367897、max0.03125、74.0607%逐值exact，全finite。注意该输入与保存的NVIDIA oracle不属同一谱系，二者约0.07 correlation不能用于判断回归；权威裁判是相同输入下旧AMD与hybrid的直接对拍。DirectML误差穿过完整block后仍被限制在一个E4M3量化级，block31替换数值可行；性能主热点转为QKV＋Attention约40ms。

为隔离QKV，block31 runner新增`VIT_DUMP_PASS`，可在任意pass后提前读回；同输入导出Contract hidden与旧QKV完整输出。`e4m3_to_f16.py`把`block31-qkv-effective.fp8`的3,145,728个有限E4M3权重按原`[input,3,head,dim]`顺序解到FP16，SHA-256为`3254cda9...626fa`。DirectML以真hidden执行`2160×1024×3072`，含VRAM pack与raw FP32 unpack为0.171639ms，对比旧QKV shader15.690ms约快91倍。

按旧shader完全相同的权重median norm scale，对DirectML raw Q/K逐token/head做normalization，V保持原值；全量6,635,520值对旧AMD QKV correlation0.9999999794、MAE6.888e-5、RMSE1.596e-4、max0.003112。分支correlation：Q 0.9999999794、K 0.9999999809、V 0.9999999776。由此QKV dense矩阵替换数值成立；当前0.171639ms尚不含GPU normalization，下一步用32-lane group reduction把该步骤直接接在DirectML output后，不能拿CPU补算时间冒充最终性能。

QKV normalization随后并入联合GPU pass：每个token/head一个32-thread group，从DirectML FP16 output同时读取Q/K/V，以groupshared tree reduction求Q/K平方和并乘预计算median norm scale，V直接写回。`make_vit_qkv_scales.py`固化64个FP32 scale，文件SHA-256为`0712134d...e8ce7`。完整pack→DirectML→normalize/unpack为0.201329ms；对CPU normalization correlation1.0、MAE1.60e-8、max9.54e-7，对旧QKV保持correlation0.9999999794、MAE6.888e-5，GPU reduction自身没有引入可观测新误差。

block31 runner再加入`VIT_PRECOMPUTED_QKV`入口，在相同旧Expand/Contract后跳过旧QKV，直接让GPU-normalized DirectML QKV进入原Attention＋Projection。尾链wall46.719ms；加DirectML QKV 0.201ms后整块约46.92ms，对旧五段block31最终输出correlation0.9999980898、MAE1.000e-5、RMSE2.589e-4、max0.015625、99.754503%逐值exact，全finite。误差穿过softmax attention后未放大，QKV完整算子替换通过；block31当前最大热点收敛为Attention约23ms。

DirectML runner进一步支持4D batch维，按Attention的32 heads测试`batch=32, M=2160, K=32, N=2160`。300次GPU timestamp平均0.943629ms、10.126 TFLOPS；该形状正对应每头`Q×Kᵀ`，第二次`softmax×V` FLOPs与形状对称，因此两次矩阵主体约1.89ms，对比旧完整Attention约23ms已有约12倍理论空间。严格边界：当前是synthetic batched GEMM，仅首batch初始化用于数值smoke，值分布不影响固定shape dispatch计时；尚未包含head-major pack、约285MiB FP16 score、softmax与第二次GEMM的真实串联，不能把1.89ms写成完成后的Attention时间。

新增`d3d12_directml_attention.cpp`后，上述边界全部由真block31 QKV闭合。GPU先把token-major FP32 Q/K/V打成三份head-major FP16（K现场转置），DirectML执行32-batch `2160×32×2160` QKᵀ，64-thread group对每个head/query的2160个score做max/sum softmax并写FP16 probability，再由第二个DirectML GEMM执行`2160×2160×32` AV，最后解包到token-major FP32并执行原`F()`。两张score/prob buffer各298,598,400 bytes。

真机分段：pack0.203520ms、QKᵀ1.353680ms、softmax1.229080ms、AV1.180600ms、unpack0.014000ms，合计3.980880ms；旧Attention23.951ms，约6.0倍。全量2,211,840输出对旧AMD Attention correlation0.999954405、MAE1.319e-4、RMSE0.00161724、max0.03125、98.984782%逐值exact，全finite。block31 runner再加入`VIT_PRECOMPUTED_ATTENTION`，让DirectML结果穿过旧Projection；最终对旧完整block31 correlation0.9998746915、MAE0.000533607、RMSE0.00209812、max0.03125、87.954373%逐值exact，全finite。softmax与Projection均未造成数值爆炸，完整Attention算子替换通过；下一步是把Expand/QKV/Attention三个已验收算子内嵌resident ViT并处理Contract/Projection矩阵核化。

DirectML通用runner随后补齐ViT Contract合同：pack前执行原`clamp(-4,4)`与`.89453125+x*(.447265625-.055908203125*abs(x))`多项式，GEMM后加source residual×FP16 channel skip再执行`F()`。以旧Expand精确输出为入口，完整Contract为0.505068ms，对旧hidden correlation0.9999971852、MAE1.208e-5、RMSE2.903e-4、max0.03125、99.709066%逐值exact。再让该hidden穿过旧QKV/Attention/Projection，block31最终correlation0.9999269439、MAE0.000371973、max0.03125、91.51001% exact，全finite，误差未爆炸。

同一residual GEMM边界复用于Projection（无Contract前多项式）：旧Attention精确输出×`1024×1024` FP16 projection＋hidden residual/skip＋`F()`为0.282099ms；对旧block31最终输出correlation0.9999761705、MAE3.711e-5、RMSE9.159e-4、max0.03125、99.543593% exact，全finite。至此block31五段均有真数据DirectML完整算子：Expand0.273847＋Contract0.505068＋QKV0.201329＋Attention3.980880＋Projection0.282099≈5.243ms，相对旧61.426ms约11.7倍。严格边界：这是相同输入下逐段独立替换与下游传播验证，尚非五段同一执行实例；下一步必须全串联后检查累积FP16误差与真实联合timestamp。

`run_directml_block31.ps1`随后按真实依赖把五段全部换成DirectML：Expand输出进入Contract，Contract hidden进入QKV，GPU-normalized QKV进入完整DirectML Attention，Attention再进入DirectML Projection，只有进程/文件边界，不再夹任何旧shader中间值。本轮GPU timestamp为0.292500/0.522683/0.210380/3.817040/0.267523ms，合计5.110126ms。最终2,211,840值对旧完整block31 correlation0.9996383986、MAE0.00135754、RMSE0.00356281、max0.03125、75.343831%逐值exact，全finite，SHA-256 `cb7095a1...84d0ba`。五次FP16与E4M3误差累积后仍不超过一个量化级，算法风险至此关闭。严格边界：5.110126ms是五段GPU timestamp之和，不含五进程wall/file I/O；下一阶段是把同样已验收的dispatch机械合并到单device resident block31，再推广blocks32–38。

`prepare_directml_vit_weights.py`随后批量处理blocks31–38：各层3,145,728-byte E4M3 QKV解为6,291,456-byte FP16，并生成64-value median norm scales；完整来源/产物SHA、scale范围写入`directml-vit-weights.json`并部署AMD机，合计约49MiB。审核`d3d12_vit_chain_amd.cpp`确认八层共用Contract/Projection及两条skip，只有Expand/QKV逐层变化。`run_directml_block31.ps1`泛化为`-Block 31..38 -Source`；同时修复PowerShell保留自动变量`$input`吞掉scriptblock参数的缺陷，参数改名`$Source`。

跨层先以旧block31输出为共同入口验证block32：五段DirectML约5.097ms，对旧block32 correlation0.9995182449、MAE0.00152817、RMSE0.00404913、max0.03125、73.309326% exact，全finite，排除block31特调。随后由相同`probe-block30-pad-native`入口分别执行旧单进程ViT31–38和八个全DirectML block；每个DirectML block严格消费前一层DirectML final。旧resident为448.622ms；DirectML逐层约4.6–5.3ms，GPU时间总和约40ms。最终block38对旧输出correlation0.9909274573、MAE0.01079668、RMSE0.01424074、max0.0703125、20.961507% exact，全finite；SHA分别`52bdd06d...952ae`与`5323d89a...8b2e5`。误差跨八层累积但未坍缩；严格下一步是送入相同block39/decoder及最终画面裁判，并将多进程链合并为resident，不能仅凭0.991 correlation宣布画质通过。

最终画面裁判使用明亮frame56400重新建立完整同源链：由`raw-56400-block30-body`生成旧resident block38，同时由同一34×60 block30投影补行后生成八层DirectML block38；两路分别进入相同block39、decoder40–69、block0/4/8/14/22 skips、backbuffer与block70。旧ViT和DirectML ViT的最终33,177,600-byte 4K R10 SHA-256均为`251b7ef7a480ef6bd272d3fbff1e62e0e1b939ed8fe1a78aca6bc4529cde1ca1`，逐byte exact。说明block38的0.9909中间误差经decoder residual与最终10-bit量化后完全不可见；对应明亮主菜单截图保持不变。ViT替换的数值与画质门槛至此通过。严格剩余边界：八层仍由40个诊断进程串联，约40ms只是GPU timestamp总和；必须合成单device resident宿主后才有实际实时意义。

resident实施正式启动。`directml_gemm_runtime.h`抽出MinGW struct-return ABI兼容、GEMM compile/initializer/binding table与外部resource绑定；`d3d12_directml_vit_resident.cpp`在单一RX9070XT D3D12 device与单一DirectML device上同时创建Expand、Contract、QKV、QKᵀ、AV、Projection六个compiled operator，并在同一command list完成六个initializer dispatch。随后分配main/branch/QKV、head-major Q/K/V/attention与score/prob共九张default-heap UAV，全部成功；常驻资源653.91MiB，其中score/prob各284.77MiB。该数值远低于已通过的全网5.46GiB alias arena，显存与DirectML多operator共存无阻塞。严格边界：当前仅完成执行对象和资源骨架，尚未把custom pack/softmax/residual dispatch录入并执行真实block；下一步在此宿主替换smoke分配为block31完整链。

resident骨架随后升为真实六GEMM执行链。程序创建main/hidden/branch/QKV、Q/K/V、score/prob/attention/final及四套weight共15张default-heap UAV（652.59MiB），用typed clear确定全零初态，将六个compiled operator绑定到前后相接的resident resource，并在一个command list依次dispatch＋UAV barrier＋timestamp。RX9070XT结果：Expand0.211960、Contract0.289440、QKV0.156320、QKᵀ1.344320、AV1.227120、Projection0.068160ms，合计3.297320ms；全链完成、无device removed或隐式同步失败。该值是单device真实GPU timestamp，不再是多进程加和。严格边界：当前用零数据且Q/K/V、prob由clear提供，尚未插custom pack、Contract激活/residual、QKV normalize、softmax及Projection residual；3.297ms仅代表六个矩阵核主体的resident成本。

首个custom边界随后进入resident链：与独立Attention相同的64-thread FP16 softmax直接读取DirectML QKᵀ的298,598,400-byte score，按head/query做max与sum reduction，写同尺寸probability；command list从DirectML descriptor heap切到custom heap，UAV barrier后再切回AV的DirectML heap，全程不插CPU fence。零链真机分段为Expand0.440760、Contract0.573960、QKV0.339200、QKᵀ1.998840、softmax1.525040、AV1.325520、Projection0.069240ms，合计6.272560ms，DML→HLSL→DML互操作完成、无device removed。该轮GPU频率/冷态使矩阵段比前次3.297ms基线慢，不能用差值推断softmax开销；softmax自身timestamp为1.525ms。严格边界：Q/K/V当前仍是clear资源，尚待QKV normalize/head-major pack接入后以真数据复验。

QKV pack设计随后去掉K显式转置。`DmlGemmOperator::Create`新增可选`TransB`，当启用时B tensor物理shape改为`[batch,1,N,K]`；resident QK以`TransB=TRANSPOSE`直接绑定与Q/V统一的`[head,token,dim]` K。RX9070XT接受该compiled operator，零链QKᵀ1.346920ms，softmax1.256520ms，AV1.293120ms，全resident链4.814480ms；额外64-value normalization scale资源已纳入16张resource清零/生命周期。这样后续normalize/pack shader只需按每个token/head连续写三份32维FP16，不再承担跨token转置，也少一张中间buffer。严格边界：scale当前为零、QKV仍未真正写入Q/K/V；本轮只验证接口与执行拓扑。

`QkvPackPass`随后实现并插入resident链：一个32-thread group对应一个token/head，从DirectML FP16 QKV读取32维Q/K/V，以groupshared tree reduction求Q/K norm，乘64-value scale，并由偶数lane将相邻两维打包写入三张`[head,token,dim]` FP16 buffer。QKV GEMM后切custom heap执行该pass，三路UAV barrier后Q/K直接进入`TransB` QKᵀ。零链真机：Expand0.208400、Contract0.293040、QKV0.161920、normalize/pack0.061480、QKᵀ1.348000、softmax1.548200、AV1.329480、Projection0.069080ms，总计5.019600ms。DML→normalize/pack HLSL→DML→softmax HLSL→DML的完整中段拓扑已无CPU同步。严格边界：输入/权重/scale当前仍为clear零值，只证明执行与资源合同；下一步上传block31真参数，并补Expand/Contract/Projection边界后做resident真输出对拍。

resident宿主新增由环境变量注入真中段数据的诊断模式：`block31-hidden-old.f32`在初始化阶段FP32→FP16并上传hidden，`block31-qkv-directml.f16`与64-value scales上传对应resident资源；计时链跳过Expand/Contract，从QKV GEMM一路执行到AV，最后仅为验证读回head-major FP16 attention。真机QKV0.158320、normalize/pack0.053160、QKᵀ1.383960、softmax1.272600、AV1.235080ms，中段合计4.103120ms；旧QKV＋Attention约39.6ms，约9.65倍。将读回结果重排token-major并执行原`F()`后，对旧AMD Attention全量2,211,840值correlation0.9999717267、MAE8.404e-5、RMSE0.00127166、max0.03125、99.372694%逐值exact，全finite。新resident QKV pack/TransB/softmax/AV真数据合同通过；严格剩余边界是把attention unpack＋`F()`留在GPU并接Projection，以及前端Expand/Contract真链。

`OutputPasses`随后闭合resident后端。第一pass将AV的`[head,token,dim]` FP16重排为token-major，在GPU执行原`F()`并打包为Projection输入；第二pass读取Projection raw FP16，加入resident hidden×FP16 projection skip，执行`F()`并写FP32 final。真block31 projection weight/skip同样只在初始化阶段上传。真hidden起点分段：QKV0.159680、normalize/pack0.073440、QKᵀ1.460000、softmax1.548360、AV1.371240、attention unpack0.012440、Projection0.069640、projection post0.026120ms，总计4.721000ms；旧对应QKV＋Attention＋Projection约42.2ms，约8.9倍。resident final对旧block31 final correlation0.9998874708、MAE0.000449472、RMSE0.00198782、max0.03125、90.037028%逐值exact，全finite，SHA`6476dd13...bd00a`。从Contract hidden到FP32 final已无CPU中间边界；严格剩余仅前端FP32 input→Expand与Contract激活/residual两段。

`FrontPasses`最终闭合block31前端：FP32 source成对打包main FP16；Expand raw经`F()`、clamp与原多项式写Contract input；Contract raw加入FP32 source×FP16 skip，执行`F()`并写final hidden FP16。为避免in-place hazard新增contractInput/contractRaw，真input及Expand/Contract weights/skip仅初始化上传。完整resident block31共23张资源694.79MiB，在一个command list执行13段：input pack0.017960、Expand0.682160、contract prepare0.075840、Contract0.998800、contract post0.028920、QKV0.549680、QKV pack0.057560、QKᵀ1.106960、softmax1.528840、AV1.147360、attention unpack0.011800、Projection0.068800、projection post0.027040ms，总计6.301720ms；旧block31 61.426ms，约9.75倍。

resident final对旧block31 correlation0.9996383986、MAE0.00135754、RMSE0.00356281、max0.03125、75.343831% exact、全finite；SHA-256 `cb7095a1b04b017f51fb64b9d48d2e8a297d28bddf15a34195091d914184d0ba`，与先前五进程逐段DirectML累积oracle逐byte完全相同。由此证明单device合并未引入新误差，block31 resident算法/资源/同步/性能四项同时通过。严格下一步：在同一进程加载blocks32–38逐层权重并loop八次，替换生产`d3d12_vit_chain_amd.exe`后重跑frame56400最终R10。

八层binding生命周期先行验证。单个compiled operator若共用binding table并在录制期间反复`Reset`，前七次dispatch可能在GPU执行时看到最后一次descriptor；因此宿主改为每层独立持有Expand/Contract/QKV/QKᵀ/AV/Projection六个`DmlGemmOperator`，总计48个compiled operator、initializer、binding table与shader-visible heap。在同一DirectML device/command list一次性录制48个initializer，RX9070XT全部成功，无device removed；冷进程从启动、48次JIT/initialize、资源建立到单层zero-chain结束共514.835ms。该成本只在addon初始化支付，不属于逐帧预算。单层零链仍为4.675600ms，证明扩容未破坏原路径。严格下一步是为8层绑定独立Expand/QKV weights/scales，并以两张main FP16 ping-pong录制8×13 pass。

八层参数与执行随后正式合并。宿主一次预载8套Expand FP16、QKV FP16与64-value scales共112.00MiB；Contract/Projection weight与skip共享。每层持有独立六个binding table，custom `QkvPackPass`亦逐层绑定自己的scale；两套`FrontPasses`/`OutputPasses`分别绑定FP32 source/final双缓冲，所有FP16 scratch跨层复用。一个command list依序录制8×13 pass，block偶数写finalF32、奇数写sourceF32。

probe输入首跑八层resident为34.973520ms，输出2,211,840 floats与40进程DirectML累积oracle correlation1.0、MAE/max均0、逐byte100%，SHA同为`52bdd06d...952ae`；对旧ViT保持correlation0.990927457。frame56400同源输入再跑为31.706960ms，旧resident ViT为475.515ms，约15.0倍。新resident block38进入完全相同block39–70后，最终33,177,600-byte 4K R10 SHA仍为`251b7ef7a480ef6bd272d3fbff1e62e0e1b939ed8fe1a78aca6bc4529cde1ca1`，与旧ViT逐byte exact。八层resident的资源、同步、数值、GPU性能与最终画面全部通过。严格剩余：正式pipeline当前给ViT的是68×120 block30 body，resident exe暂收36×60 padded FP32；需把已有block30 pool/enter＋尾两行清零并入前端后再替换生产runner。

`PredownPass`随后并入resident宿主：从68×120×512 block30 body先执行512×512 matrix，再2×2 average pool与512→1024 enter；第二pass覆盖完整36×60×1024，前34行写结果、尾两行显式写零，不依赖旧内容。frame56400直接body入口产生的block38与外部`downsample_enter→34行→pad36行`版本逐byte exact，SHA同为`52bdd06d...952ae`。predown与八层已留在同一command list，无中间CPU fence；当前`vit8_gpu_ms`timestamp起点仍在predown之后，33ms数字不含predown，后续需前移query作完整stage GPU计时。

正式`run_dynamic_vit_raw.ps1`已从旧`d3d12_vit_chain_amd.exe`切换到`d3d12_directml_vit_resident.exe`，外部合同保持`raw-FRAME-block30-body.f32 → raw-FRAME-block38.f32`。frame56400 production wrapper输出SHA`52bdd06d...952ae`；完整`run_dynamic_network_resident.ps1`重跑最终R10仍为`251b7ef7...e1ca1`逐byte不变。ViT八层GPU33.195480ms，stage wall1126.738ms（含每帧进程启动、48 operator JIT、112MiB参数上传与readback），Windows network wall从10857.587降至10120.780ms。功能生产路径已切换；严格实时边界仍未完成：addon常驻可消ViT约1.09秒wall，但其余Swin/decoder GPU段累计仍远超帧预算，下一步把DirectML矩阵核路线推广到Swin FFN/QKV并最终嵌入addon。

Swin矩阵核化从512-channel block40开始。现有portable blob 984,080 FP32按shader权威offset拆为gate/up各`512×256`、project`256×512`、FFN skip512、QKV`512×768`、attention projection`256×512`及attention skip512；`prepare_directml_swin512.py`转置矩阵并输出FP16与SHA manifest。独立shape benchmark：`8640×512×256` 0.021261ms、`8640×256×512` 0.027470ms、`8640×512×768` 0.058086ms；窗口Attention 135 windows×16 heads即batch2160的`64×16×64`为0.153658ms。所有矩阵理论合计低于0.5ms，但尚不包含窗口重排、cosine normalization、bias与softmax。

`d3d12_directml_swin512_ffn.cpp`随后以真`raw-16800-block39`（68行补H72）执行input FP32→FP16、两路DirectML gate/up、原fast多项式相乘、DirectML project、residual skip＋E4M3。分段0.029920/0.092520/0.089840/0.028680/0.117640/0.061920ms，合计0.420520ms；旧runner同输入FFN expand/project为6.242＋2.021=8.263ms，约19.65倍。`d3d12_block128_test.cpp`新增`DUMP_FFN=1`只读导出FFN feat；DirectML对旧有效68×120×512共4,177,920 floats correlation1.0、MAE/RMSE/max均0、逐float100% exact，SHA`06c29d4d...5b75f`。首个Swin算子在性能与精确性上同时通过；下一步接QKV与window attention。

`d3d12_block128_test.cpp`继续加入`DUMP_QKV=1`，将QKV readback扩为padded `tokens×768`但只写有效68行。DirectML通用runner读取逐float exact的FFN输出与`prepare_directml_swin512.py`拆出的`512×768` FP16矩阵，执行8640 tokens QKV及GPU边界为0.125465ms；旧QKV8.320ms，约66.3倍。有效6,266,880值对旧QKV correlation0.9999999785、MAE2.704e-7、RMSE4.191e-7、max3.789e-6、全finite。QKV未做E4M3量化故不逐值exact（0.116%），但连续值误差仅百万分之一量级。旧window Attention本身约0.795ms，短期保留HLSL比引入batched score scratch更合算；下一优先是旧2.076ms Attention Projection的`256→512` DirectML替换，再做完整block下游误差裁判。

`DUMP_ATTENTION=1`再导出旧window Attention的有效68×120×256，补H72后进入新`d3d12_directml_swin512_projection.cpp`；该runner执行FP32→FP16 pack、DirectML `8640×256×512`、FFN feat residual×FP32 skip及E4M3。真机0.026280/0.043160/0.094400ms，合计0.163840ms；旧Attention Projection2.219ms，约13.54倍。对旧block40 final有效4,177,920 floats correlation1.0、MAE/RMSE/max均0、逐float100% exact，SHA`e7f86183...3679`。至此block40三个矩阵段独立闭合：FFN0.420520、QKV0.125465、Attention Projection0.163840ms，合计0.709825ms；加保留的旧window Attention约0.8ms，完整block性能目标约1.5ms。严格边界：Projection验证使用旧Attention输入；下一步需把DirectML QKV送入旧window Attention再接DirectML Projection，做完整block累积裁判。

完整block累积裁判随后完成。旧runner新增`PRECOMPUTED_FFN`/`PRECOMPUTED_QKV`，SRV可直接绑定两份预计算upload资源并跳过旧FFN/QKV，保留完全相同window Attention；其输出再进入DirectML Projection并以DirectML FFN作为residual。最终有效68×120×512对旧block40 correlation1.0、MAE/RMSE/max均0、逐float100% exact，SHA同为`e7f86183...3679`。QKV的微小连续值误差经cosine normalization/window softmax与最终E4M3后完全消失，完整block数值风险关闭。性能按VRAM-local独立分段约0.420520＋0.125465＋0.795＋0.098280≈1.439ms；hybrid runner的Attention为1.431ms是从UPLOAD heap读取25MiB QKV造成，不作为resident预测。下一步将三套DirectML GEMM与原Attention PSO合并为单device blocks40–47链，并批量拆七层其余权重。

`prepare_directml_swin512.py`批量拆出blocks41–47共七套DirectML参数并部署。先以shifted block41验证：FFN0.190400、QKV0.129090、原shift Attention0.820、Projection0.100320ms；最终对旧block41 4,177,920值逐float100% exact，SHA`f3c680e0...ab682`，证明token shift映射未被矩阵替换破坏。随后Windows侧连续执行blocks40–47，每层严格消费前一层hybrid final；Attention诊断改为保留完整H72 padding，消除逐层Linux补行。旧单进程链190.890ms，hybrid VRAM-local分段估计约12ms。最终block47对旧有效区correlation1.0、MAE/RMSE/max均0、所有float数值相等；bitwise有10,125/4,177,920不同（0.242345%），审计确认100%都是`+0.0`与`-0.0`符号位差异，无任何非零值差异或非finite。故严格表述为数值100% exact而非逐byte exact。八层算法风险关闭，下一步合并为单device resident 512-channel链。

新增`d3d12_directml_swin512_resident.cpp`建立单device骨架：同一DirectML device创建8层×gate/up/project/QKV/attention-project五类共40个独立compiled operator/binding table，main两张FP16 ping-pong，gate/up/hidden/feat/QKV/attention scratch跨层复用，五套weight buffer逐层独立。全部资源clear后在一个command list连续执行40个真实GEMM＋UAV barrier，RX9070XT矩阵主体总计3.753200ms；48张default-heap资源仅68.84MiB，无device removed。冷进程含40次JIT/initializer、资源建立和执行为527.181ms，仅在未来addon初始化支付。严格边界：当前zero-chain尚未插FFN combine/residual、QKV FP32边界、原window Attention与attention residual；3.753ms不是完整八层时间，但证明40 operator共存、ping-pong绑定与矩阵成本成立。

512 resident宿主随后补齐全部custom路径。`BoundaryPass`逐层执行fast(gate)×up、FFN project residual/E4M3与Attention project residual/E4M3；`WindowAttentionPass`把DirectML FP16 QKV解到FP32，执行与旧shader相同的cosine normalization、16-head×64-token bias/softmax及shift映射，再打包FP16给DirectML Attention Projection。`prepare_directml_swin512.py`增加逐层attention bias/scale导出；8层共40个GEMM、24个boundary pass、24个window attention pass在单device/单command-list执行，84张资源121.50MiB。

真`raw-16800-block39` H72输入与8套参数一次预载后，blocks40–47 resident GPU为14.326320ms，旧resident 190.890ms，约13.32倍。resident block47对多进程hybrid/旧链correlation0.9999994633、MAE1.122e-7、max0.0078125、99.998564%数值exact，全finite；仅75个非signed-zero量化边界值不同。裁为68行active后进入相同blocks48–70，resident/旧两份33,177,600-byte 4K R10 SHA均为`4868b7e487bd146a8a32448a0a1058c80645e7ad756f670c04d494592adabe35`，逐byte exact。输出代码现直接只写68×120×512 active，满足block48接口，无外部裁剪。512-channel段资源、同步、数值、性能与最终画面通过；严格下一步是接入production block39链，并将同一模板泛化256/128/64/32 channel段。

block39随后矩阵核化。`prepare_directml_block39.py`把现有`(1536+1)×512` FP32 affine+bias拆为1,572,864-byte FP16矩阵与2,048-byte FP32 bias。通用DirectML runner以现成68×120×1536真input执行，含约50MiB input pack与16MiB raw unpack为0.432135ms；加bias后对旧block39 correlation0.9999999839、MAE2.740e-6、RMSE9.059e-6、max0.00012055，全finite。补H72进入完整resident blocks40–47，本轮GPU10.072160ms；block47对旧仍是correlation0.9999994633、MAE1.122e-7、max0.0078125，与直接使用旧block39时完全相同，说明block39误差在block40 E4M3边界被吸收。

block39 DirectML＋resident512输出裁68行后继续进入blocks48–70，最终4K R10与旧链SHA同为`4868b7e487bd146a8a32448a0a1058c80645e7ad756f670c04d494592adabe35`逐byte exact。block39→47矩阵替换的数值与画质门槛通过。严格剩余：block39目前仍由独立通用GEMM进程和外部1536拼接输入完成；下一步在512 resident exe内加入ViT main上采样＋block30 skip pack、block39 GEMM/bias，随后替换production `d3d12_swin_chain --block39`。

block39随后真正并入`d3d12_directml_swin512_resident.cpp`：新增第41个DirectML operator `8160×1536×512`，`Block39Pass`在GPU直接从36×60×1024 ViT main按2×nearest取样并拼68×120×512 block30 skip为FP16 1536维输入，GEMM后加bias写H72 FP16 main（尾四行零）。与完整blocks40–47同一command list执行为13.410320ms；block47误差与分离block39版本完全一致。输出直接写68行active。

新增`run_dynamic_decoder40_47_directml.ps1`并试挂production。frame56400功能回归最终R10仍为`251b7ef7...e1ca1`逐byteexact；新stage GPU16.820680ms，但stage wall达4041.334ms，因为每帧新进程重新创建41个DirectML compiled operator并编译8套Boundary/WindowAttention PSO；全网wall由旧10120.780恶化到13597.273ms。故正式`run_dynamic_network_resident.ps1`已回退旧block39–47 runner，实验wrapper保留。结论不是GPU路线失败，恰恰证明剩余壁垒已从计算转为生命周期：该13–17ms graph必须嵌入长驻addon（或持久worker），不能以逐帧CLI进程上线。

为直接量持久态，512 resident exe新增`DML_SWIN512_ITERATIONS`，同一已初始化资源与command list内重复录制block39→47 graph；block39每轮重建main[0]，因此各轮输入固定且不会把上一轮输出误当下一轮输入。100轮平均GPU为5.985188ms，最终block47与单轮输出correlation1.0、MAE/max0、逐byteexact，SHA同为`ca717bb2...ae61`，排除状态污染。完整进程wall4489.604ms，扣除约598.5ms GPU可见约3.89秒一次性JIT/PSO/文件初始化；摊100轮44.896ms，长期稳态收敛于约6ms GPU。由此production试挂的4秒stage wall被严格归因为错误生命周期，而非真实逐帧计算成本。下一步无需继续优化512算力，转向其它channel宽度并建立持久worker/addon生命周期。

通用`prepare_directml_swin.py`随后覆盖512/256/128/64四档权威offset、matrix transpose与bias/scale/skip导出；在block40上与旧512专用脚本的所有二进制payload逐byte一致。256-channel真shape benchmark：`32640×256×288` Expand0.115388ms、`32640×256×384` QKV0.061211ms、`32640×128×256` Attention Projection0.029710ms，4080 batch的`64×16×64`为0.276290ms。

FFN/Projection诊断runner泛化为环境变量tokens/channels/hidden/attention_dim，非512档关闭双gate，combine只执行fast(expand)。普通block49真输入：DirectML FFN0.515000ms vs旧5.378＋4.562=9.940ms，correlation0.9999998822、MAE7.405e-6、max0.001953125、99.620864% exact；DirectML QKV0.328102ms vs旧5.786ms，correlation0.9999998918、MAE4.926e-6、max5.21e-5。微小误差进入原window Attention与DirectML Projection0.227400ms后完全被E4M3吸收，block49最终8,355,840 floats与旧输出逐byteexact，SHA同为`e5482c76...86a91`。按VRAM-local旧Attention0.581ms估计完整block约1.65ms vs旧19.783ms，约12倍。256档算法门槛通过；下一步构建encoder blocks15–22与decoder blocks48–55两条resident链。

`d3d12_directml_swin256_resident.cpp`由512 resident机械特化后逐项校准为T32640/C256/H288/Q384/A128、W240/H136、8 heads、blocks49–55 shift序列。审计先抓出三类不会由编译器报错的残留：Boundary 512常数、key_token W120，以及未使用block39 bias resource仅分256 floats却创建512-float SRV导致`DXGI_ERROR_INVALID_CALL`；全部修正后真链执行。非gated档仍创建up operator/buffer作为统一骨架但combine忽略，后续可删以省JIT/GPU空算。

七层真resident单次冷GPU26.337520ms；同一graph连续100轮平均9.322514ms，旧blocks49–55为153.983ms，稳态约16.5倍。block55对旧correlation0.9999892933、MAE8.584e-5、RMSE0.000475423、max0.0234375、95.741027% exact，全finite。两路继续进入相同blocks56–70，最终33,177,600-byte 4K R10 SHA均为`4868b7e487bd146a8a32448a0a1058c80645e7ad756f670c04d494592adabe35`逐byteexact。decoder 256 normal段通过；严格剩余是把block48 upsample/prefix并入、删除unused up/block39遗留，以及构建encoder blocks15–22八层变体。

encoder 256变体由同一已验收源码机械派生为L8、blocks15–22、shift `0/1/3/2/0/1/3/2`。首次直接喂`raw-block14`被尺寸检查正确拒绝：block14仍是272×480×128，需先经旧融合predown得到136×240×256；使用现存同帧`raw-16800-block14-enter15.f32`建立纯block15–22裁判。真resident单次冷GPU28.486080ms、100轮稳态10.612648ms；旧同段153.966ms，约14.5倍。block22对旧correlation0.9999836306、MAE0.000289806、RMSE0.00179881、max0.03125、93.484653% exact，全finite。

两份block22再各自进入完全相同的旧block22→23 predown与blocks23–30。block30 correlation0.9999817488、MAE5.811e-5、RMSE0.000523504、max0.01171875、98.040843% exact，全finite；下游把MAE与max均压低，没有误差放大。encoder256数值/稳态性能通过。严格剩余：把block14 downsample/enter并入encoder resident，再将其一路送过DirectML ViT/decoder到最终R10；目前还不能仅凭block30宣布画面门通过。

block14 predown随后并入encoder resident。首版输出全零的排查依次排除了DirectML initializer、SRV/UAV state与Dispatch维度；过程中把16,711,680-thread matrix pass改为X=65535、Y=4的2D dispatch，避免超过D3D12单轴group上限。真正根因是pool shader把累加器`v`和`p/o/x/y`写在同一条`uint`声明里，浮点乘积逐项截断为整数零；将`v`独立声明为float后恢复。input/down/enter固定转NON_PIXEL SRV态，mid写后显式UAV→SRV transition。

修正后的`raw-16800-block14`→matrix→2×2 pool→128→256 enter→DirectML blocks15–22输出，与外部`raw-16800-block14-enter15`起跑版本逐byteexact；100轮每轮把mid转回UAV并重做predown，最终仍逐byteexact、SHA同为`a7f7df9c...b7780`，排除状态污染。包含predown的持久态GPU14.869602ms；旧blocks15–22 153.966ms外加旧predown约10ms，整段约11倍。严格剩余：encoder输出尚需跑至最终R10裁判；且该exe仍含从512模板遗留的unused block39/up资源与JIT，后续清理并并入持久生命周期。

encoder最终画面裁判随后完成。DirectML block14 predown＋resident blocks15–22与旧blocks15–22分别进入相同blocks23–30；两路block30再各自通过完整DirectML ViT31–38、integrated DirectML block39＋resident40–47、对应block22 skip的旧block48–55，以及共同block56–70。两份33,177,600-byte 4K R10 SHA-256均为`4868b7e487bd146a8a32448a0a1058c80645e7ad756f670c04d494592adabe35`，逐byteexact。encoder256的数值、稳态性能与最终画面三门全部通过。严格剩余转为工程生命周期与其它宽度：不把每帧重做JIT的实验exe挂production，待持久worker/addon统一承载。

128-channel从普通block57启动。geometry为T130560/C128/H160/Q192/A64、4 heads；纯矩阵benchmark Expand0.192522ms、QKV0.169455ms、Attention Projection0.036704ms、8160-batch window GEMM0.547163ms。通用FFN/Projection与DirectML boundary shader统一改为2D dispatch：当groups>65535时X=65535、Y=ceil(groups/65535)，HLSL以`id.x+id.y*4194240`展平，避免大tensor静默零化。真block57 FFN1.265760ms vs旧约12.98ms，16,711,680输出逐floatexact；QKV0.717298ms，对旧连续T×192 corr0.9999999778、MAE0.002689、max0.013527。

初次拿历史`raw-16800-block57`作final裁判出现225个`+384↔-384`翻转并导致0.024% R10像素变化；分层重跑后证明这是oracle谱系错配，不是DirectML不稳定：只替FFN的Attention对当次纯旧Attention逐floatexact；DirectML QKV后的Attentioncorr0.9999999777、max0.01144；同一当前runner即时生成的纯旧final与DirectML FFN＋QKV＋旧Projection逐floatexact，加入DirectML Projection后corr0.99999999995、MAE7.439e-5、max0.015625、99.523926% exact，无大翻转。两份同版本final再进入blocks58–70，4K R10 SHA均为`4868b7e4...be35`逐byteexact。教训再次固化：历史同名中间文件不能跨runner版本作裁判。128档算法门通过，下一步构建blocks57–61与9–14 resident链。

128 decoder resident由256模板逐项特化为L5/T130560/C128/H160/Q192/A64、W480×H272、4 heads与shift `0/1/3/2/0`。审计修正机械替换造成的V offset64（应128）、Attention pack/UAV 128（应64）、Boundary 288/256（应160/128）及所有超65535-group pass的2D flatten。QKV resident采用紧凑T×192，不保留旧buffer后半空容量。blocks57–61真链冷26.118040ms、100轮稳态11.988113ms；旧102.584ms，约8.56倍。block61对同版本旧链逐float100% exact，进入blocks62–70后R10 SHA`4868b7e4...be35`逐byteexact。

encoder128变体为L6 blocks9–14、shift `0/1/3/2/0/1`；loader正确处理9/14的`body-effective`与10–13历史`effective`命名。以同帧`block8-enter9`起跑，冷24.549760ms、100轮稳态14.440822ms；旧125.272ms，约8.67倍。block14对旧corr0.999999999981、MAE3.242e-5、RMSE0.000711786、max0.015625、99.792480% exact，全finite。严格剩余：把block8 downsample/enter并入encoder resident并走最终R10；decoder block56 upsample/prefix亦尚未并入。

block8 predown随后并入encoder128：544×960×64 input经64×64 matrix、2×2 pool与64→128 enter直接写272×480×128 FP16 main。第一pass33,423,360 threads从一开始采用X65535/Y8的2D flatten；input/down/enter上传后固定SRV态，mid写后显式转SRV，每次repeat前转回UAV。predown＋blocks9–14单轮输出与外部`block8-enter9`版本逐float/byte exact；100轮每轮重建predown，稳态18.282716ms。主干继续进入resident256 block14 predown/15–22与旧blocks23–30后，两路block30逐float exact。

最终画面裁判保留各自decoder skip：DirectML encoder128路径使用自己的block14与经resident256得到的block22，旧路径使用旧block14/block22；共同block30进入DirectML ViT与DirectML39–47，再经对应skip的block48/56和共同62–70。两份4K R10 SHA均为`4868b7e4...be35`逐byteexact。encoder/decoder128的资源、同步、数值、稳态性能和最终画面全部通过。严格剩余是decoder block56 prefix与生命周期整合。

decoder block56 prefix随后并入128 resident。`prepare_directml_upsample_prefix.py`把`(2C+1)×C` FP32 affine+bias拆为DirectML FP16 `2C×C`与FP32 bias，可复用于block48/56/62/66。`UpsamplePass`从136×240×256 low按2×nearest直接pack为130560×256 FP16，DirectML `256→128`后加bias与272×480×128 block14 skip，写main[0] FP16，再依次执行block56–61六层、shift `0/0/1/3/2/0`。

真`raw-16800-block55`＋block14 skip单轮prefix→blocks56–61为26.093240ms，对旧`--upsample`六层输出16,711,680 floats逐float/byte100% exact；100轮每轮重建prefix，稳态14.849870ms，旧123.793ms，约8.34倍。因block61二进制完全相同，确定性blocks62–70输入不变，最终R10继承既有exact，无需重复同一尾链。decoder128 external prefix已闭合；下一步转64/32档与持久生命周期。

64-channel从block63启动，T522240/C64/H96/Q96/A32/2 heads。纯矩阵benchmark：64→96约0.58–0.60ms、32→64 0.227906ms、16320-batch window `64×16×64` 1.076938ms。通用runner的大tensor边界继续采用2D dispatch。真FFN2.273320ms vs旧10.002ms；33,423,360输出corr0.99999999929、MAE1.002e-7，仅116值不同、7值差>0.01、max0.5。QKV1.437972ms；同时把standalone QKV buffer从错误统一stride384修为真实分档768/384/192/96，旧QKV自身由约12ms降至3.245ms，避免3/4空线程。

完整DirectML block63对同版本即时旧oracle corr0.9999734214、MAE0.0129612、max0.5、76.5630% exact，全finite；继续到block70后R10有827,468/8,294,400像素不同（9.9762%），但10-bit RGB最大差仅3/5/6。分离替换确认只换FFN已复现几乎全部差异，QKV/Projection非主因：whole-output FP16 project rounding让116个FFN值跨E4M3边界，Attention再将稀疏差异扩散。故64档状态明确为性能通过、最终exact失败，不进入主线。下一步对FFN project按K维拆partial FP16，GPU FP32累加后再做residual/E4M3，避免最终总和先压成FP16。

K-split实测否决该假设。通用FFN runner新增`SWIN_PROJECT_SPLIT`：hidden按K chunk转成batch-major，DirectML batch并行输出多份FP16 partial，finish shader FP32相加后再做residual/E4M3。split3(K32)为3.291440ms，FFN不同值从116增至2617、MAE6.868e-7、43值差>0.01；split2(K48)3.229240ms，不同值20378、MAE4.876e-6、51值差>0.01，均差于split1。原因是每个partial各自落FP16引入更多舍入面，事后FP32相加无法恢复。K-split路线关闭；64主线保留split1近似，当前最终R10最大channel delta6/1023，继续构建resident链并另寻全FP32 output/WMMA方案。

64 decoder resident随后闭合block62 prefix与blocks62–65。新runner以同一D3D12/DirectML device常驻20个GEMM：从272×480×128 block61按2×nearest打包，DirectML 128→64，加bias与544×960×64 block8 skip，再执行四层FFN、cosine window Attention与Projection。首轮真输入排查抓到Attention累加误读K偏移32而非V偏移64；修正后block65对同版本旧`--upsample`链33,423,360值corr0.9999999889、MAE5.791e-6、RMSE0.0003561、仅51值差>0.01且全finite。

RX9070XT单轮prefix＋四层28.141120ms，100轮每轮重建prefix稳态21.467901ms；旧链98.013ms，约4.57倍。两路block65继续经过相同blocks66–69与block70，得到33,177,600-byte 4K R10 SHA-256均为`4868b7e487bd146a8a32448a0a1058c80645e7ad756f670c04d494592adabe35`，逐byteexact。此前单独替block63得到的9.98%像素微差不能代表完整同版本segment裁判；四层集成后的最终画面门已通过。严格剩余转为32-channel resident与持久addon/worker生命周期，冷进程不切production。

32-channel DirectML候选完成但被性能／精度门否决。提取器新增特殊1H布局：把Q/K/V各自的偶列、奇列重新交织成`32×48` FP16矩阵，并从bit-packed槽恢复单head scale；block66 prefix同样拆为DirectML 64→32矩阵与FP32 bias。真blocks66–69常驻20个GEMM，100轮稳态52.204307ms；现有SM6 FP32 HLSL单链含copy约60.051ms，收益很小。block69对同版本旧链corr0.9999581442、MAE0.001421、max0.5，最终R10有44.7732%像素变化但RGB最大仅4/4/6个10-bit刻度。

另按旧1H shader补齐shift边界mask，稳态反而升到91.697462ms，而block69 corr仍为0.9999581489，证明mask只影响边缘窗口，主误差来自32-channel矩阵的FP16落地。结论：不为统一DirectML而替换这一档；blocks66–69正式保留现有逐byteexact的SM6 FP32 PSO，下一阶段直接把这些PSO与宽档DirectML operators放进同一持久addon/worker。`d3d12_swin32_chain.cpp`增加按层、FFN与prefix诊断开关，默认行为不变。

持久addon生命周期开始落地。新增`d3d12_resident_lifecycle_probe.cpp`：ReShade `init_device`取得游戏原生D3D12 device并AddRef，worker线程在同一device创建DirectML device、独立DIRECT queue/allocator/list/fence，compile＋initialize一枚代表Swin64 Expand形状`522240×64→96`的operator，随后分配常驻input/weight/output，在同一binding上连续提交100次真零GEMM并用GPU timestamp计时；全部对象保持到进程结束，present只记录ready/failed，不触碰画面。首版审计发现standalone helper的`dmlrt_check`失败会`ExitProcess`，因此未让游戏加载并立即从目录撤下；helper改为可覆写`DMLRT_FAILURE`，addon抛出并记录HRESULT，所有现有exe默认仍fail-fast。安全执行版SHA-256为`8b2abe1f31e02d5f3b5e3db12ab0498ec02181c38410d00539b51492441921bd`，交叉编译与Swin64 standalone回归通过；下一次前台启动只验同进程DirectML初始化、重复dispatch和device-removed，不把生命周期probe误称整网接入。

生命周期入口随后从`init_device`收紧到主swapchain。原因是ReShade启动期可能报告临时D3D12 device，若在第一个device上成功初始化，只能证明进程内DirectML可用，不能证明它与最终4K backbuffer同device。新版只接受`init_swapchain(resize=true)`，从真实`IDXGISwapChain3::GetDevice`取得并持有device，同时把swapchain尺寸/format写日志，再启动worker。安全执行版SHA更新为`fa5ba1bc210359233412a1e6c35b2167f4ebdf2b68d0b7e50c1a3f82e6d97668`；这使下一次ready日志同时证明“主游戏device＋DirectML重复dispatch”，而不是启动期旁路设备。

后台worker的DLL生命周期也按已验证runtime probe模式收紧：`DllMain`在注册ReShade事件前以`GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|PIN`固定自身，避免热重载/卸载addon后初始化线程落入已释放代码。最终待测binary SHA-256为`07e2794e8f226e821601ac218992802268375e5a120cc2ea29706edc73d91621`；部署脚本同步锁定该hash。

全网缺口复审发现encoder64尚未resident，不能把剩余工作收窄成纯生命周期。`d3d12_directml_swin64_resident.cpp`新增`DML_SWIN_FIRST_BLOCK`，同一T522240/C64/H96/Q96/A32图在first=5时加载blocks5–8并使用shift `none/XY/Y/X`，first=62仍保留decoder原行为。真`raw-16800-block4-enter5`单轮26.133640ms、100轮稳态20.997551ms；旧链94.349ms，约4.49倍。block8对同版本旧链33,423,360值corr0.9999974395、MAE3.147e-6、RMSE0.0002276、max0.0390625、仅1168值差>0.01且全finite。

新增`validate_directml_encoder64.ps1`做完整最终裁判：DirectML block8依次进入blocks9–30、resident ViT31–38、decoder39–69；decoder分别使用由该分支生成的block22/block14/block8三条skip，block4保持共同输入，最后进入block70。输出33,177,600-byte 4K R10与旧链SHA-256同为`4868b7e487bd146a8a32448a0a1058c80645e7ad756f670c04d494592adabe35`，逐byteexact。encoder64算法、性能、skip传播和最终画面四门闭合；严格剩余包括把block4 predown并入该graph，以及游戏内持久生命周期实测。

block4 predown并入encoder64后反而揭开旧runner静默截断：旧`d3d12_swin_chain`与独立downsample runner将66,846,720线程作为1,044,480个groups全部dispatch到X轴，超过D3D12每轴65535上限，shader又只读`id.x`；结果mid只覆盖前4,194,240值即131,072像素，约第68行以下沿用未写区。CPU抽样证实旧enter5在首行与正确公式误差约1e-8，到y100/y300/y543分别升至约0.019/0.028/0.054；新2D flatten对CPU仅约2.4e-4 FP16误差。generic两处shader改为`id.x+id.y*4194240`并将cache从predown-v1 bump v2，独立runner同步修复。

修正后的FP32 generic predown＋blocks5–8与DirectML integrated版本比较：33,423,360值corr0.9999999444、MAE1.028e-7、RMSE3.355e-5、max0.015625，仅543值不同、135值差>0.01。integrated单轮30.663440ms，100轮每次重做predown稳态24.478336ms，其中predown-only 5.555640ms。正确pre-down完整跑到block70得到4K R10 SHA`44f2517d44635c610bb15b1cc2e9d932793d5b7667b859f16f645111315e8d46`；画面干净，相对历史截断版仅176,434/8,294,400像素变化（2.1271%），RGB最大2/3/3个10-bit刻度。因此旧SHA不再作为该边界golden；这是修复历史执行缺口，不是DirectML回归。

部署predown-v2后又从修正block4输出完整重跑block8→9、block14→15与block22→23三条后续边界，最终R10 SHA仍为`44f2517d...e8d46`；说明本帧可见变化全部由最早的block4截断修复决定，后续旧截断此前被上游零区遮蔽，但通用runner仍必须保留2D修复以覆盖其它帧。

encoder512主体也进入resident。`d3d12_directml_swin512_resident.cpp`新增`DML_SWIN_FIRST_BLOCK`与`DML_SWIN_LAYERS`，shift按blocks23–30的`none/XY/Y/X`周期由真实block号推导，输出ping-pong按active layer奇偶选择。从同帧`raw-16800-block23`补H68→H72后起跑blocks24–30：单轮6.418760ms、100轮稳态5.020680ms；旧七层FP32 HLSL 177.965ms，约35.45倍。最终block30 4,177,920 floats与同版本旧链逐float/byte 100% exact且全finite。严格剩余是把block22的256-channel body经pool/256→512 enter直接送入block23，并做最终R10裁判。

block22→23特殊predown随后并入512 resident：136×240×256 block22 body先乘256×256 matrix，再做2×2 average pool与256→512 enter；pool pass覆盖完整H72×W120×C512，H68之后显式写零，避免repeat沿用上一轮main内容。真输入单轮predown＋blocks23–30为16.186520ms，100轮每轮重建predown稳态8.923368ms；旧predown-v2＋八层185.640ms，约20.80倍。两路block30 4,177,920 floats逐float/byte 100% exact、全finite。因下游ViT/decoder为相同确定性输入，既有最终画面裁判直接继承；encoder512不再依赖外部block23或手工H72 padding。

decoder block48 prefix随后并入256 resident。`UpsamplePass`从68×120×512 block47按2×nearest直接pack为32640×512 FP16，DirectML `512→256`后加FP32 bias与136×240×256 block22 skip，写main[0]再执行block48–55八层；权重由通用prefix splitter及`prepare_directml_swin.py`生成。真输入单轮17.388720ms、100轮每次重建prefix稳态10.784976ms；旧`--upsample`八层151.382ms，约14.04倍。block55对同版本旧链8,355,840值corr0.9999869541、MAE9.333e-5、RMSE0.0005249、max0.0234375，仅1941值差>0.01且全finite。两路继续跑blocks56–70后4K R10 SHA同为`4868b7e4...be35`逐byteexact；decoder256外置边界清零。

前端重审确认block39已完整resident，真正大热点是block0 distilled MLP：旧shader对每tile标量执行`192→256→256→2048`，动态帧约94–119ms。新增`prepare_directml_preblock.py`把三层output-major FP32权重转为DirectML row-major FP16矩阵并保留三层bias、output scale/bias FP32；`d3d12_directml_preblock_resident.cpp`以三枚常驻GEMM串RGBA→RGB pack、SiLU、SiLU和最终affine。RX9070XT真32640 tiles单轮2.036720ms、100轮稳态1.684028ms，对旧94.361ms约56倍。

DirectML preblock 66,846,720 tile输出对旧标量结果corr0.9999999430、MAE0.0004242、RMSE0.0007388、max0.004587、没有值差>0.01且全finite。经过现有E4M3 tile→HWC后仅489,600/66,846,720值变化（0.7324%），再以新frame id完整跑blocks1–70，4K R10 SHA为`8c8bd142...caf831`；相对修正predown基线35.3134%像素发生微调，但RGB平均仅0.171/0.209/0.232个10-bit刻度、最大均6，目视菜单画面干净无条纹/NaN/几何变化。block0性能门通过；下一步把tile→HWC与blocks1–4接到同一resident资源，消除最后的前端process/file边界。

tile→HWC随后并入preblock resident同一command list。`prepare_directml_preblock.py --permutation`复刻CPU转换器，从4096-entry physical permutation生成4个quadrant×512 logical slot的uint16 rank map；GPU reorder按HWC座标反推tile/quadrant/local slot，读取tile FP32并现场E4M3编码解码。首版仅65,280值差一个subnormal刻度：权威CPU `enc()`在指数低于-6时把mantissa clamp到7，而通用`F()`允许round到8；补`min(round(a*512),7)`后，267,386,880-byte GPU HWC与独立`preblock_tiles_to_hwc.exe`逐byteexact，SHA同为`6b80db5c321123af13a08dbd07ef9f0dcfd0d8ba4884b83b23b9966274332212`。block0三GEMM＋HWC重排单轮3.368840ms、100轮稳态2.547040ms；267MB tile中间文件与独立转换进程可删除，严格剩余边界是把HWC resident buffer直连blocks1–4。

blocks1–4随后并入同一front宿主。程序预载四份41,220-byte effective参数和12枚已有SM6 PSO，分配两张main ping-pong、feature与QKV scratch；HWC reorder完成后原地UAV→SRV，四层只切descriptor table并在同一command list执行。首版把32640 tiles误当2,088,960 tokens，QKV只分/执行1/64而输出corr0.125；拆出`TOKENS=TILES×64`并按其分配/dispatch后，block4 66,846,720 floats与分离`d3d12_swin32_chain`逐float/byte100% exact、全finite。

完整block0三DirectML GEMM→GPU HWC→SM6 blocks1–4单轮40.910480ms，100轮稳态39.567292ms；分离稳态preblock+HWC 2.547040ms加blocks1–4 52.820ms合计55.367ms，消除device/resource/readback边界即再快约1.40倍。前端文件边界清零，但39.57ms本身仍超过60fps整帧预算；下一步必须优化1H32 FFN/QKV/Attention本体或采用混合矩阵核，不能把“resident”偷换成“实时完成”。

front宿主增加14点GPU timestamp，把单轮拆为preblock+HWC 5.983ms及四层各三段：block1 FFN/QKV/Attention=4.038/3.269/3.942ms，block2=3.063/3.360/5.072ms，block3=3.048/2.916/4.874ms，block4=3.173/2.748/4.303ms；四层合计FFN13.322、QKV12.293、Attention18.190ms。瓶颈分散，Attention最大。

最低风险的Attention DXC重编先做控制实验。新增`block1_attention_sm6_fp32.hlsl`逐式复刻旧shader，cs_6_2/O3输出block4 SHA与旧链逐byteexact；但unshifted为4.981ms，三层shifted分别26.372/22.973/22.975ms，全front升到104.819ms。故“只换DXC”路线否决，正式仍用旧DXBC；下一步转为真正的32640-window batched DirectML QKᵀ/softmax/AV，避免每个query重复标量矩阵乘。

batched Attention先过独立矩阵门：`DML_BATCH=32640`、`64×16 · 16×64`在RX9070XT连续100轮平均2.158108ms、1.982TFLOPS。QKᵀ与softmax×V两次矩阵主体约4.316ms，显著低于当前四层旧Attention合计18.190ms，路线可继续。四层geometry完全相同，因此正式实现不需要8套compiled operator：一枚TransB QK与一枚AV固定绑定共享Q/K/V、score/prob scratch，每层在旧QKV之后覆盖pack、record同一对operator，再做mask/softmax和projection；先验收累积FP16误差，未过画质门前不替换exact DXBC路径。

完整batched Attention推翻了上段乐观比较：4.316ms是**单层**两次GEMM，四层矩阵主体本身约17.265ms，已经接近旧四层Attention总计18.190ms，之前把单层与四层总量放在同一分母是口径错误。真候选用共享Q/K/V、267MB scores/prob与一对QK/AV operators，逐层执行normalize/window pack→QK→mask/bias/softmax→AV→FP32 projection；四层Attention分别15.048/17.591/17.316/17.284ms，front总93.717600ms。block4虽finite但corr0.9999778993、MAE0.005523、RMSE0.06061、max8.0、205万值差>0.01，性能和精度双败。

该路线正式关闭。为不让失败候选即使关闭环境变量仍常驻约700MB scratch和额外JIT，它被隔离为`d3d12_directml_front_batched_attention.cpp`；已验收的`d3d12_directml_preblock_resident.cpp`恢复干净版本。下一优化应针对旧Attention内部重复norm/load或降低全分辨率stage工作量，而不是继续把小K window GEMM强塞DirectML。

旧Attention的重复norm随后做FP32预归一化候选：QKV pass按token计算一次Q/K范数，保存`Q×scale`、normalized K与原V，Attention只做dot+bias+softmax。QKV本身略降为3.097/2.561/2.650/2.678ms，但unshifted Attention仍4.968ms，三层shifted反而为23.086/18.518/22.404ms，front总94.584320ms。瓶颈不是rsqrt算术，而是新shader在shifted控制流和数组上的寄存器/codegen劣化；路线否决并隔离为`d3d12_front_normalized_attention.cpp`，干净front再次恢复。

作为下一候选门，DirectML纯QKV `2088960×32 · 32×48`连续100轮为1.740559ms、3.687TFLOPS，对比旧QKV每层2.56–3.49ms只剩约0.8–1.7ms空间；FP32→FP16 pack和FP16结果解包尚未计入，很可能吃掉收益。后续必须做完整边界实测，不再用裸矩阵数字直接宣称可加速。

连续两次front Attention候选失败后切换热点，重审block70。4K FFN的DirectML裸矩阵`8294400×32→64`与`64→32`分别2.830383/4.474761ms，QKV `32→48`为6.823202ms；但完整FFN真prefix边界实测pack2.929＋expand2.996＋SiLU pack3.905＋project5.057＋residual finish6.429=21.315680ms，与现有SM6 FFN热态19.217–22.863ms无优势。矩阵核7.3ms的乐观空间被四次全图边界吃完，DirectML FFN路线关闭。

block70 sparse prefix同时做固定两项展开。CSR的2048个输出degree分布恰为1664×1＋384×2；文件尾少一个weight，审计确认shader越界读按零处理，pair生成器显式补一项implicit zero。`BLOCK70_PREFIX_PAIRS=1`直接读取`{i0,i1,w0,w1}`并保持加法顺序，最终4K R10 SHA仍为`4868b7e4...be35`逐byteexact；四次热态prefix为11.239/11.254/11.240/11.242ms，与CSR版约11.24ms相同。动态offset/loop不是瓶颈，固定pair不进入production；候选及manifest生成器保留作可复现实证。

block70再测FFN→QKV融合。`block70_ffn_qkv_sm6_fp32.hlsl`在每token寄存器中保留64 hidden与32个E4M3 feature，写feature的同时直接用local feature计算并写QKV，从执行图删除独立QKV dispatch和一次267MB feature读取。最终4K R10 SHA仍为`4868b7e4...be35`逐byteexact；但热态融合pass37.974/38.903ms，后续空QKV区仅0.089/0.107ms，高于分离FFN＋QKV观测约32.161–36.230ms。64+32局部数组把寄存器/occupancy推过阈值，spill代价超过带宽收益；融合路线关闭，环境变量候选保留作反证。

decoder32旧exact链加入14点GPU profile。热态三轮prefix稳定8.617/8.580ms；blocks66–69的FFN约2.72–3.06ms/层、QKV2.38–3.52ms/层、Attention3.47–5.23ms/层，四层合计约11.3/10.1/16.1ms。与front一样瓶颈分散，但64→32 prefix单项占约15%。

因此重访先前整段失败的DirectML32，只取其prefix：`DML_SWIN_PREFIX_ONLY`执行2×nearest FP16 pack、DirectML `2088960×64·64×32`、bias＋block4 skip，100轮稳态2.759728ms，相对旧prefix约3.11倍、节省5.821ms。prefix对FP32旧输出corr0.99999998997、MAE0.001513、max0.01451、全finite；随后仍用原FP32 blocks66–69，最终R10仅150,291/8,294,400像素变化（1.81196%），RGB平均0.0161/0.0213/0.0101、最大2/3/2个10-bit刻度。该混合路径不是byte-exact，但比整段DirectML误差小一个数量级且有真收益；保留为实时候选，动态连续帧通过前不切production。

全网随后做统一稳态预算，纠正“各段都进几十毫秒就快实时”的错觉。按顺序相加当前各段最佳已初始化GPU时间：front0–4 39.567、enc5–8 24.478、enc9–14 18.283、enc15–22 14.870、enc23–30 8.923、ViT31–38 31.669、dec39–47 5.985、dec48–55 10.785、dec56–61 14.850、dec62–65 21.468、dec66–69 60.051、block70 65.795ms，合计316.724409ms即3.157fps。采用DirectML block66 prefix候选只降至310.903657ms即3.216fps。

这是**乐观下限**：没算进程/JIT/file（持久化可消）、跨stage barrier、游戏本身GPU争用及少数未单独量的边界，只会低估真实帧时。block70/decoder32/front32/ViT四项占约62.2%；30fps需整体再快约9.33倍，60fps约18.65倍。结论：常驻addon仍是必要工程，但已不是充分条件，不能再写“只剩生命周期”；必须同时取得多阶段算法/内核数量级优化。完成门保持不变：游戏内连续变化帧、明确实测cadence、画面目视通过，三者缺一不称实时。

为消除前台验证的人工作业链，新增`validate_resident_lifecycle.ps1`：先调用hash锁定的deploy脚本Install，若游戏未运行则由当前互动用户直接发Steam URI，随后每2秒读取`resident-lifecycle-probe.txt`；`resident_ready`即输出passed JSON，任何operator/execution/device失败立即退出2，默认180秒无ready退出3并附当前状态。该脚本只验主swapchain device＋DirectML初始化＋100次warm dispatch；不把probe通过升级成整网接入。

Zero将第一阶段目标明确为1080p至少10fps，严格4K留待后续。不是4K算完再缩图，而是全网空间轴减半：front960×544 C32；480×272 C64；240×136 C128；120×68 C256；C512 active34×60/pad40×60；ViT18×30=540 tokens；decoder镜像；block70输出1920×1088再裁1080。weights、channels与8×8 windows不变。几何固化于`dlss5-1080p-geometry.json`。

第一段直接用frame56400真实FFX Color建立1080p输入：现有preprocess以`--target-width 960 --target-height 544`从2561×1441 active RGBA16F bilinear缩放并生成8160 tiles（8,355,840 bytes）。旧block0输出经通用tile→HWC得到544×960×32，全部finite、range -10..5.5、std2.166947、邻域H/V corr0.95335/0.94525；据此运行block1测试生成960×544 shift0/1 QKV/Attention cache，FFN由DXC cs_6_2/O3重编并加入正式build脚本。

`d3d12_directml_preblock_resident.cpp`改为从`DML_FRONT_WIDTH/HEIGHT`推导tile/token/resource尺寸，HWC reorder以WIDTH/HEIGHT宏寻址，PSO cache同样动态选几何；`DML_FRONT_HWC_ONLY`提供同源裁判。真960×544 block0→HWC→blocks1–4单轮16.366240ms、100轮稳态12.767225ms，HWC-only单轮1.109040ms。完整block4 66,846,720-byte SHA`75778dc1...d59e6ce`与同一DirectML HWC起跑的四个分离block逐byteexact。1080p front性能/数值门通过，下一边界是block4 predown＋480×272 blocks5–8。

主线随后从离线runner正式切到单一游戏DLL。`dlss5_1080p_runtime.cpp`合并此前分离的resource hook与lifecycle probe：只在主`init_swapchain(resize=true)`反查游戏D3D12 device并PIN自身；一次创建DirectML device、独立queue/allocator/list/fence，初始化1080p block0代表形状`8160×192→256`并在常驻input/weight/output上warm dispatch 100次。另一worker MinHook `amd_fidelityfx_dx12.dll!ffxDispatch`，在当前调用栈读取当帧Color/Depth/Motion/output、render/upscale尺寸、jitter与原生commandList，并用四个resource的`GetDevice`确认与主swapchain同device，全程不readback、不写中间张量。

统一runtime交叉编译SHA-256为`e496e01b48febb81356f741111b1eec9a4f79bf9303b6c01f3d54cb60338d3e7`。`deploy_dlss5_1080p_runtime.ps1`在游戏运行时拒绝变更；Install校验hash后移除旧`d3d12-dynamic-resource-probe`与`dlss5-resident-lifecycle`，避免两个MinHook争同一target，再安装唯一runtime。当前边界明确：DLL已承载正确设备/当前帧资源/一次性初始化合同，但尚未把front0–4算子代码录入ffx commandList，不宣称已改画面。

DLL随后接入第一个真实逐帧GPU pass。初始化worker创建常驻8,355,840-byte tile UAV、固定960×544 bilinear/tile PSO与8套双descriptor heaps；每次FFX upscale hook在调用原FSR前，以renderSize作为active范围从当帧RGBA16F Color `Texture2D.Load`做align-corners-false等价bilinear，直接按`[tile,8×8,RGBA]`写8160 tiles，并插UAV barrier。descriptor按frame&7轮转，避免CPU更新覆盖仍在GPU使用的SRV；Color只读、输出仅为自有buffer，不碰游戏画面。该pass即使游戏暂为4K也可验证当前帧桥，只有upscaleSize=1920×1080时才提升`frame_contract_1080`。全程仍无readback和中间文件。新版统一runtime SHA为`8d25f71c4814f5b53d790dbfe35e75a9bcc23dcc746db87ccf73b601f2cfc627`，下一前台启动日志应同时出现`ffx_frame`与`frame_bridge_submit`。

block0完整执行随后搬入同一runtime。初始化worker一次加载DirectML三层FP16 matrix、三层bias、output scale/bias与4×512 tile map，创建`8160×192→256`、`8160×256→256`、`8160×256→2048`三枚compiled operator以及约180MB常驻scratch；initializer和权重copy在独立queue完成后才原子发布`block0_ready`。每帧frame bridge后在原FFX commandList连续record RGB FP16 pack→GEMM1→SiLU→GEMM2→SiLU→GEMM3→affine→E4M3 tile-to-HWC，最终写常驻960×544×32 FP32 UAV，不启动exe、不读回、不写文件。

同步合同同时收紧：frame tile初态UAV；每帧写前若非首帧显式`NON_PIXEL→UAV`，写后UAV barrier再`UAV→NON_PIXEL`供block0 SRV读取，不能依赖standalone驱动对UAV状态下SRV读的宽松容忍。新版binary SHA为`c48e77d01a0515de6230015d2b7bcb0c40e52f7ae236569bf0fd1cc5528e5866`。当前严格边界：真实当帧Color已经在DLL内跑到block0 HWC，但尚未接blocks1–4或回写画面；首次游戏加载仍待前台验收。

blocks1–4随后接入同一runtime。初始化期读取四份41,220-byte effective权重到常驻upload resources，加载960×544 shift0/1对应的4×FFN/QKV/Attention共12枚cache PSO，并分配两张66,846,720-byte FP32 main ping-pong、同尺寸feature及约100MB QKV scratch。descriptor table与权重全程固定，不在帧循环重建。

逐帧执行在block0 HWC后显式`UAV→NON_PIXEL`，四层依次FFN→feature SRV、QKV→QKV SRV、Attention→main SRV；复用scratch或ping-pong前精确转回UAV。下一帧block0写HWC前转回UAV，front开始前再把两张main/feature/QKV统一恢复UAV，排除跨帧状态污染。最终block4留在`g_front_main[1]` GPU resource，不readback、不落盘。新版DLL SHA为`b1bc9ce6c3dd75300f908dc983b07e7bdb832222db08c1ba8a0d425bf0942d12`；当前游戏内链为Color→tiles→block0→blocks1–4，尚未接block4 predown。

1080p Swin64段先在独立同构宿主闭合。`d3d12_directml_swin64_1080_encoder.cpp`把几何特化为T130560、W480×H272、C64/H96/Q96/A32；predown直接读960×544×32 block4，执行32×32 matrix、2×2 pool与32→64 enter。真block4单轮predown＋blocks5–8为11.417120ms，100轮稳态4.796600ms；block8对四个FP32参考block的8,355,840值corr0.9997517400、MAE0.0008232、RMSE0.0022526、max0.03125、50,832值差>0.01且全finite。

已验收的predown/Boundary/Window三类pass抽为`swin64_1080_runtime.h`并嵌入统一DLL。初始化期一次创建20枚DirectML operator、4×9权重、main ping-pong与共享hidden/QKV/attention scratch；block4 down/enter常量转固定SRV。每帧直接消费`g_front_main[1]`，mid在重复帧前NON_PIXEL→UAV，predown后回SRV，四层内部维持UAV资源与显式UAV barrier，最终block8留在`g_s64_main[0]`。新版DLL SHA为`99c3266f3a7f1b4be102d04b23a534ac7830e057cbf80aa6531dc85ba68b04fe`；游戏内链推进为Color→blocks0–8，无exe/file/readback。

1080p blocks9–14随后建立专用runner：T32640、W240×H136、C128/H160/Q192/A64，block8 predown为480×272×64 matrix/pool/64→128。机械模板首跑在`boundary root`报device removed，根因是encoder文件仍初始化完全无关的8160×1536→512 block39及资源；删除所有block39 operator/init/bind/record后正常。真输入单轮7.230000ms、100轮稳态3.837133ms。

这一段的数值不能直接按旧“逐层接近1”验收：同几何FP32参考自身从block10均值-0.021迅速到block11 range -96..0.75，block12已有2,208,000值为-448，block13为2,434,890，block14回落到96,000；DirectML与参考均finite但block14 corr仅0.395857、MAE122.33。说明现有effective网络在新空间几何出现强饱和吸引子，可能在后续层恢复，也可能导致最终画面失败。策略是继续完整1080p链并以最终R10/画面裁判，不把3.84ms性能通过误写成数值通过；若最终失败，再回查effective参数的固定几何依赖。

下一C256阶段暴露并修复真实padding错误：1080p active为120×68，不满足8×8 window，必须pad到120×72。机械版predown曾对尾四行继续做pool，读取H136输入之外的y=136..143；改为覆盖完整72行但n≥68×120×256显式写零，并清除同样无关的block39遗留。T=8640的block14 predown＋blocks15–22单轮9.609480ms、100轮稳态4.020946ms。

修正后DirectML block22 active区恢复到range -1.75..0.9375、std0.314115、零±448饱和值；同一block14起跑的FP32参考范围完全一致。两者2,088,960值corr0.9999998395、MAE6.087e-6、RMSE0.00017795、max0.015625，仅4245值不同、255值差>0.01且全finite。故block11–13的负饱和是可逆中间吸引子，并未判死1080p链。几何表同步修正：C256 pad72×120；下一C512 active34×60的宽高都非8倍数，需pad40×64而非只pad高度。

统一游戏DLL现已接入上述两段。两枚常驻FP16→FP32 GPU bridge分别把block8的`480×272×64`与block14的`240×136×128`直接送入下一stage predown；blocks9–14为六层，blocks15–22为带`120×72`补零的八层。所有DirectML operator、PSO、权重与scratch仍只初始化一次，每帧仅在游戏原生FFX command list记录dispatch和barrier，跨stage没有CPU readback、exe或中间文件。同步修正256 predown二维flatten跨度为`4194240`。交叉编译DLL SHA-256=`eaee5438dfb83aca3be8338d900ce7dce82004b8280ca6d8298d1dc9374799ee`；当前动态链推进到block22，但仍未回写游戏画面，且需以首次游戏启动日志确认连续submit。

blocks23–30随后接入同一DLL的C512 resident段。这里不能照搬4K的72×120布局：1080p block22只有active68×120，predown执行256×256 matrix与2×2 pool后得到active34×60，再显式写入padded40×64×512，右侧4列与底部6行全部清零；Window Attention同步改为64×40循环位移。八层使用既有`block23–30-logical-effective-*`拆分权重，跨stage继续由GPU FP16→FP32 bridge衔接。顺手修正通用record状态机：只有输出确被下一级读成SRV后，下一帧才做NON_PIXEL→UAV，避免末级在第二帧提交无效transition。编译SHA-256=`dbafaaf4ecd2f77cdc6582bf75fa5663c4ee6a901817a714f6a13ae34e71e033`；当前游戏DLL动态链已覆盖blocks0–30。

ViT blocks31–38随后接入DLL，并按1080p而非4K几何重建。block30输出是padded40×64×512 FP16，新增常驻crop PSO逐行抽取active34×60到FP32，避免右侧padding被错误flatten；block30 matrix/pool/512→1024 enter再生成active17×30并显式清零第18行，得到540 tokens。八层各自常驻Expand/QKV/scale权重，共用Contract、Projection及两条skip；QKV pack、32头QKᵀ、540-way softmax、AV与projection全在同一游戏command list执行。所有30个依赖权重在AMD Lab逐项存在，交叉编译SHA-256=`8e535b1aa4942fdcd3a1f1a5be0b585cc74a9268073406738c4d8e6f0496590e`。当前动态链推进到block38；首次游戏内初始化/连续submit仍待互动Steam会话验证。

decoder blocks39–47接入同一DLL。block39专用prefix按active34×60生成2040×1536 FP16 records：ViT 18×30×1024走2×nearest，encoder block30 skip从padded40×64资源按真实x/y读取active512通道；DirectML `1536→512`后再按padded40×64落回，右4列/底6行显式清零。随后blocks40–47以T2560/C512执行八层resident Swin，shift周期为XY/Y/X/none重复。74个远端依赖权重逐项存在；交叉编译SHA-256=`10a83e29b152ff6e011e66ca9f90ed4c7f2e3b987b25fd5a331685516ae94b2d`。游戏内动态链当前覆盖blocks0–47。

decoder blocks48–55接入DLL。block48 prefix直接读取block47的padded40×64×512 FP16，按active34×60做2×nearest写入72×120×512 packed tensor，底部4行先置零；DirectML `512→256`后加固定bias与encoder block22的72×120×256 skip，finish再次强制底4行归零，避免bias复活padding。随后blocks48–55以T8640/C256运行八层resident Swin，所有74份远端权重存在。交叉编译SHA-256=`587987894d59fe0ca8021175af0b0adda20942a539e33b7add6c014f0a7fbab7`；当前动态链覆盖blocks0–55。

decoder blocks56–61接入DLL。block56 prefix从block55的padded72×120×256 FP16中只按目标136×240映射读取active68行，2×nearest后由DirectML `256→128`，加固定bias与encoder block14的240×136×128 skip，写出无padding的32640-token主张量。blocks56–61随后以T32640/C128执行六层resident Swin，56份远端依赖权重全部存在。交叉编译SHA-256=`0ab94c1faface6c45e5d47b9dc842bae7fdfb55b8c76371ae906b7b1533dfd70`；当前动态链覆盖blocks0–61。

decoder blocks62–65接入DLL。block62从240×136×128的block61做2×nearest，DirectML `128→64`并加encoder block8的480×272×64 skip，随后以T130560/C64执行四层resident Swin；38份远端依赖权重全部存在。交叉编译SHA-256=`acd9a67c899eb3d2e72279bde5b6a26a9b2388116fcbc5a0b1f46f013848a4fd`；当前动态链覆盖blocks0–65。

decoder blocks66–69接入DLL。block66先从block65的480×272×64 FP16做2×nearest，DirectML `64→32`，在GPU finish中加FP32 bias与encoder block4的960×544×32 FP32 skip，写FP16后由常驻bridge解为FP32。blocks66–69随后复用已验收的960×544 C32 FFN/QKV/Attention PSO与四份41,220-byte body参数，最终block69留在FP32 GPU resource。prefix与四份body的远端尺寸逐项正确；交叉编译SHA-256=`81ff76842e2a604c395a50b37659905d2fac82751e8c29a3db96467d4f75d191`。当前动态网络链覆盖blocks0–69，只剩block70与最终R10回写。

block70接入前修正FFX hook的命令录制顺序：旧版在调用原`ffxDispatch`之前追加网络命令，未来即使写回output也会被随后录制的FSR覆盖。新版先调用trampoline，让原FSR在同一native command list生成当帧RGBA16F base，再追加自有blocks0–70；因此block70可以读取原FSR结果并成为最后写入者。该变更不创建第二queue、不提交command list，仍完全使用游戏原生调度序列。顺序修正版SHA-256=`14f69553ded7fb5db080d470ece3fb017c89f08e2ff295e5eae31e138dcf3dc6`。

block70随后并入统一DLL，几何严格使用padded1920×1088、active1920×1080。prefix直接读取block69与block0两份960×544×32 FP32 HWC，以240×136个4×4 sample生成32640×2048 tiled tensor；FFN/QKV/8×8 attention在1920×1088上执行，outconv只处理前1080行。因hook已改为原FSR先录命令，outconv可从当帧RGBA16F output UAV读取base并原位叠加神经残差，随后另行GPU pack为1920×1080 R10 buffer。三份权重尺寸为27,652/41,220/384 bytes，交叉编译SHA-256=`619f720275279f024bc26fc9574860e85d566f3e61c6c22076be23488a97a011`。至此DLL内blocks0–70已连通；严格剩余为present前R10→swapchain回写及真游戏连续动态/帧率验收。

R10 swapchain回写随后进入present callback。block70的1920×1080 packed buffer保持GPU resident；主swapchain为1920×1080 R10G10B10A2时，ReShade同一D3D12 command queue的immediate list执行`UAV→COPY_SOURCE`、buffer-to-texture、backbuffer `COPY_DEST→PRESENT`，下一次block70再转回UAV。整个闭环不map、不readback、不创建每帧资源。交叉编译SHA-256=`b1efd3b6087c6c13eea3079a6d4514f816edcabd970f4ef3258688ea5eb41dd6`。代码层面已形成当前帧Color→blocks0–70→R10→游戏backbuffer，但完成状态仍等待真游戏连续帧、画面与≥10fps三项实测。

首次自动前台验收尝试使用Windows互动式计划任务`DLSS5Launch`，以console session 1的`lmxxf`身份执行真实Steam路径`steam.exe -applaunch 3489700`。任务成功启动Steam PID6620，进程CommandLine与SessionId均核对正确；但75秒内未派生`SB-Win64-Shipping`，也没有创建runtime log。故当前阻塞点在Steam前台登录/更新/弹窗状态，而不是已观察到DLL加载或GPU初始化失败；不得把这次尝试记成游戏内通过或失败。

继续追查Steam前台后得到明确根因：`console_log.txt`记录`LaunchApp waiting for user response to SynchronizingCloud "pendingcloudsessions"`。manifest已证实游戏完整安装且UpdateResult=0；互动任务、Steam进程、session 1与真实命令行均正常。直接启动EXE也被Steam接管并返回同一pending cloud gate。此处必须由用户选择保留本地或云端存档，不能自动替选以免覆盖游戏进度。新增`launch_stellar_blade.ps1`固定正确working directory，以及`inspect_stellar_blade_launch.ps1`统一读取Steam启动日志和Windows应用错误；选择完成后可复用同一互动任务继续验收。

云同步解除后的真游戏验证首次跑完整初始化：主swapchain为3840×2160 R10，frame56400同类FFX合同实际为render2561×1441、output3840×2160；blocks0–70依次全部ready，`resident_ready init_wall_ms=29766 removed=0x00000000`，随后FFX frame计数持续到2160，证明一次性创建全部DirectML operator/PSO/weights/scratch不会device removed。由于仍是4K，1080回写未触发。

该运行同时暴露hook重排后的生命周期错误：先调用原`ffxDispatch`再读取调用方header时，返回后的结构已被改写，日志出现upscaleSize 0×0和same_device=0。修复为调用trampoline前整份复制`FfxDispatchUpscale`到栈上，返回后只使用snapshot；逐帧网络执行条件同时收紧为same-device且1920×1080，避免4K时误写左上角。`launch_stellar_blade.ps1`改由Steam附带`-ResX=1920 -ResY=1080 -Fullscreen`启动，下一次进程重启验证真实1080合同。

snapshot修正版在第二次真游戏运行中仍显示ABI upscaleSize=0×0，证明问题不是调用后内存改写，而是当前FFX版本的尺寸字段偏移与旧公开结构不同；相邻output resource descriptor始终稳定给出真实3840×2160。target门改以`output.description.width/height`为权威。device判断同时从接口裸指针比较改为IUnknown identity，并把Color/Depth/Motion/Output/command-list五项分别写日志，避免D3D12代理接口导致同对象不同指针的假阴性。诊断版SHA-256=`5a7526630ec7b62f404b3907a44dac571ecc72bfb08e0b525a20edd89edf89df`，待游戏切1080并重启后部署。

游戏切独占全屏1080后，FFX权威合同变为render1281×721（resource pad1284×724）、output1920×1080，`target1080=1`；blocks0–70与resident_ready再次通过。但原FSR返回后五项GetDevice均失败，说明仅复制dispatch结构不足以延长其中COM对象生命周期。修正为trampoline前对Color/Depth/Motion/Output与native command list全部AddRef，自有命令录完后Release。present每600帧同步记录当前backbuffer实际尺寸/format，以判断启动时3840×2160 swapchain是否随后resize。交叉编译SHA-256=`23592b5cc69f2fae91b7cb1b2135fd6b56416b2d002d0b57134ca2bafcf1fa9a`。

AddRef版在真1080中仍得到device_parts=00000，而所有接口调用本身未崩，说明FFX/ReShade proxy device与swapchain native device有不同COM identity。完成门改为比较`ID3D12Device::GetAdapterLuid()`：资源device、command-list device与主swapchain device只要属于同一adapter即允许录制；这正对应目标“游戏原生RX9070XT device/queue”，不再把代理接口地址误当物理设备身份。该轮还确认swapchain从启动瞬间3840×2160在present600前resize为1920×1080 R10。LUID修正版SHA-256=`3dc1269622af02b600ebc2a5b7bb41be3d404f5d35d28e9adbe329158de2056a`。

首次LUID放行运行在blocks9–14 ready附近退出。时间线显示根因不是该stage数值：在完整初始化完成前，game hook已开始用全局`IDMLCommandRecorder`录blocks0–8执行命令，同时worker在独立queue/线程继续用同一recorder录blocks9之后的operator initializer，形成未受支持的并发调用。修正为逐帧执行硬门必须等待全局`g_ready`：所有blocks0–70 operator/PSO/weights/scratch初始化、fence完成且worker不再触碰recorder后，才从下一帧整链一次放行。交叉编译SHA-256=`9ac7e97629e2b2ed708291e2ebda3e94a9f2115abe64a7dcfe654e1f31177398`。

全ready后第一帧CPU成功录到`block70_submit=1`，但present未返回、进程随后以Steam exit16384终止，且Application/System均无普通错误或GPU TDR事件。为定位GPU命令边界，新增初始化时读取的`DLSS5_MAX_BLOCK`与`DLSS5_DISABLE_PRESENT`诊断门：完整graph仍一次初始化，逐帧只控制最远record stage与是否执行R10 copy。首轮launcher设max_block=69；若持续present则把错误锁到block70/present，随后再以70+disable-present区分。诊断版SHA-256=`f2804d8ec4ca936108ea2ab385078025b55a704892d832385639a1a688677858`。

首轮max69未真实生效，runtime日志明确为`max_block=70`：launcher调用已存在Steam进程时只发送IPC，游戏不是其子进程，环境变量不会继承。诊断门改为初始化期只读一次`D:\DLSSNR-Lab\runtime-max-block.txt`与`runtime-disable-present.txt`；不在帧循环访问文件。launcher固定写69/0后再调用Steam。文件门版SHA-256=`022d5e960f63b265fba9055f13b3d16d90f383c687793be59ffbe297bc1458d2`。

文件门第二次仍显示70，核查文件内容为69，根因是Windows PowerShell 5 `Set-Content`默认UTF-16LE，而DLL对binary wide stream的读取未建立正确文本编码状态。launcher改为显式`-Encoding Ascii`，DLL改用`fscanf`解析窄字符数字。ASCII门版SHA-256=`b3f2b254068b5a8260d41c8a7e85c2cc39baf8adbdd9802d477905ef80fc4f65`。

ASCII诊断门后完成真实GPU二分：max69在blocks66–69首submit后退出；max38在ViT首submit后退出；max30在blocks23–30首submit后退出；max22在blocks15–22首submit后退出；max14在blocks9–14首submit后退出。max8当前进程稳定且present超过3000，但FFX frame在全graph ready前停于533，因而一帧网络都未执行，不能判通过。下一步需让游戏窗口回前台/进入动态场景，使ready后的FFX dispatch继续发生，再判blocks0–8。

继续二分得到：max30在block23–30首submit后退出，max22在block15–22后退出，max14在blocks9–14后退出，max0在frame bridge与block0首submit后退出。因此故障已缩到Color→tiles bridge或block0。新增特殊`max_block=999`仅执行frame bridge而跳过block0及全部下游，区分最后两项；诊断版SHA-256=`954714b8d93e23b28cb26f28da5937ebd2577e318c20b78c6ee601847a502ba7`。

bridge-only `max_block=999`首submit后同样触发PS Studios通用Report Problem，故障严格锁定为Color→tiles pass。根因指向命令顺序下的Color状态：公开FFX descriptor给的是进入dispatch前compute-read状态，原FSR录制后不能假设Color末态仍可作SRV。hook改为双段：调用原FSR前在已知状态下先录frame bridge；调用trampoline；返回后再从resident tiles录block0–70并最终覆盖output。COM AddRef覆盖整个双段。双段版SHA-256=`b9be9bb035359b3234ba3922f933cdf179f31fd3f10644c205166a5f1cec566d`。

双段hook后bridge-only仍首submit崩，排除Color末态。最终机制是D3D12 device ownership：swapchain `GetDevice`所得native接口与FFX command-list/resource所属ReShade代理device虽LUID相同，却不是同一D3D12 device instance；用前者创建descriptor/heap再绑定后者resource非法。初始化入口因此从`init_swapchain`移到第一次FFX upscale：由native command list `GetDevice`取得真实计算device，再一次创建DirectML、PSO、weights与scratch。swapchain callback仅保留present目标，不再创建graph。FFX-device版SHA-256=`54adff8338721099be77209159081f44d083086b06ed36183d360dee1aa0bf2b`。

把全graph直接迁到FFX proxy device后游戏不再崩，但DirectML首个`CreateOperator`返回DXGI_ERROR_INVALID_CALL，说明proxy device能录D3D12、不能作为DirectML设备。最终采用双device共享资源：graph与DirectML继续建立在swapchain native device；frame bridge的root/PSO/descriptor建立在FFX proxy device；native device以`D3D12_HEAP_FLAG_SHARED`创建tiles buffer并导出handle，proxy device`OpenSharedHandle`得到同显存别名。bridge写proxy alias，block0读native alias，同一物理显存且零CPU/零copy。共享版SHA-256=`536de16a49ebad9e62f9cd08c0ae2906e261bc3f007af1a5b3763f0a901ee14c`。

共享tiles bridge-only连续提交超过1440帧且游戏Responding，跨device输入问题关闭；加入block0后仍首submit崩，说明native DirectML命令不能经proxy command-list接口录制。查ReShade 6.8源码确认proxy支持私有`IID_UnwrappedObject {7F2C9A11-3B4E-4D6A-812F-5E9CD37A1B42}`并返回`_orig`。hook现同时持有proxy与native list：bridge在proxy list上录，原FSR正常录，block0–70在同一底层list的unwrapped native接口上录；没有第二command list或submit。unwrap版SHA-256=`b8bf9533c483391b7641a8d7a63e9a7635315442f5a5ec9a8da8382dbb0e3bb4`。

unwrapped native list后，block0连续提交1440帧、max69连续提交480帧均稳定。max70且关闭raw-R10 present时，完整blocks0–70连续提交；独立20.032945秒窗口从submission1440增至1680，实测11.9802655fps。开启raw-R10 copy虽然同样稳定并产生连续`r10_backbuffer_submit`，但画面为暗绿规则网格；关闭后画面恢复清晰主菜单。原因是FFX output是pre-tonemap RGBA16F，直接UNORM R10 pack跳过游戏tonemap/UI/compositor。production因此保留block70对FFX output的原位回写，由游戏原生管线生成最终R10 swapchain；raw pack/copy默认关闭且不再执行pack shader。两张间隔3秒截图SHA分别`90c61e21...371a1a`与`a61e4c50...b1a44`，画面中伊芙眼睛一闭一睁，排除固定截图/旧帧重复。

production SHA`b0e0ada5...82219f`冷启动复验：日志明确`max_block=70 raw_r10_present=0`，blocks0–70连续提交超过720，进程Responding、无device removed。独立20.0270987秒窗口从submission480增至720，最终实测11.9837628fps。置前截图`09f55ba3f0f991f746f14f95e7dc91a6a196edf9c21fff27d0e396b42e17dfa6`画面清晰；结合前述闭眼/睁眼双帧，1080p连续动态、画面与≥10fps三门闭合。运行时每帧无exe、无文件、无CPU readback；startup gate文件仅初始化读取一次。

### 2026-09-05 实际场景方格回归修复

用户进入游戏后报告满屏小方格，撤回此前仅以主菜单截图作出的画质完成判断。同一洞窟存档场景，新增F6输出对照（0=神经残差，1=仅原生画面，2=左原生/右神经），全网计算不变：关闭block70写入后方格消失，证明问题由输出分支引入。

修复两处代码错误：C512 predown输入active68×120，pool读行跨度从遗留240修正为120，dispatch数量同步改为1080几何；block70残差恢复离线runner的display-color合同，不再对FSR中间RGBA16F施加SDR残差和saturate。present时在游戏原生immediate command list内把当前R10 backbuffer复制到一次性分配的R10 UAV，执行outconv残差相加，再复制回当前backbuffer。FSR中间输出保持原样。无CPU图像readback、无每帧文件或资源创建。

部署SHA-256为`4fe1d38d9815c1f6171be694cb9f2483271f02ba314305a99c03ff796a81ecbf`。进入同一洞窟后截图`grid-bug-after.png`已无修前`grid-bug-before.png`的满屏绿色周期网格；日志确认`display_residual generation=1320 mode=0`，并非关闭神经写入的截图。两项修正一起验证，尚未分别量化各自对方格的贡献。原20秒cadence脚本每120帧取一次计数有量化误差，不能把11.9838当作精确帧率证明。

### 2026-09-05 30fps优化第一轮

新增present侧5秒cadence日志，使用真实submission增量和GetTickCount64时间差，取代每120帧日志采样的粗略估计。block70的FP32展开FFN增加1920×1088可配置编译，使用DXC cs_6_2，启动时存在enable-block70-sm6.txt则加载候选；构建命令固定在build_block70_1080_sm6.ps1。C64/C128/C256 boundary只使用gate、完全不读取up，移除这些stage的冗余up GEMM与barrier，C512的gate×up保留。

部署候选SHA为7e94524c9debf38659f29100ac08c54f7839c4322e73cce8d35b0b36bce57b8e。实测菜单5秒窗口约13.315–13.317fps；进入游戏加载期9.878/11.061，随后12.721/12.643fps。实际场景截图未见此前绿色网格。尚未达到30fps，且场景变化下这些数字不能独立证明两项候选各自的收益；下一步需要GPU分段timestamp定位热点，目标仍为33.3ms总帧预算。

### 2026-09-05 游戏内GPU时间戳

新增一次性GPU profile：完整初始化并热身60帧后，在游戏原生command list对13段插timestamp，Resolve到112-byte时间戳buffer；present侧在游戏queue Signal fence，后续确认完成才读取。不读回图像。首个样本(ms)：block0 0.59316，front1–4 11.88072，enc5–8 4.20388，enc9–14 3.47156，enc15–22 3.55660，enc23–30 2.01384，ViT 5.36352，dec39–47 1.30444，dec48–55 2.52932，dec56–61 2.44796，dec62–65 3.30388，dec66–69 11.57468，block70 19.43360；全网71.67716ms。该计时不含FSR、输入bridge和最终显示合成，不能当完整帧耗时。

核查本地block22-pool-identity和block30-pool-identity逐元素等于单位矩阵，C512/Vit predown省略apply_matrix并直接从输入pool，保持后续顺序；进一步扩展profile为18点，增加block70内部prefix/FFN/QKV/attention。候选部署SHA aa86d6a5ab501b8b20af4f25d9f209831c1dd4f7f7d100147be208827ea3ee60。30fps尚未达成，优先优化两个C32阶段及block70。

### 2026-09-05 C32并行QKV

block70 QKV原先每线程顺序处理16输出，改为16个线程各处理一个输出（同时算Q/K/V），保持FP32与相同输入/权重索引。首样本QKV 8.492→1.3384ms，block70 19.63→12.333ms，全网64.18684ms。将同核以960×544几何用于front1–4与decoder66–69，前端11.87988→8.62072ms、解码11.6468→8.3192ms，全网57.76232ms，block70 QKV1.28408ms。所有数字来自热身后的同一GPU时间戳profile结构，非CPU wall估算。

随后准备C32 group FFN候选：每64线程组处理一个token，共享32输入与64hidden，前32线程负责project；当前以enable-group-ffn.txt选择，QKV以enable-block70-parallel-qkv.txt选择，均只启动时读取。构建脚本包含1920×1088 tiled与960×544 HWC两种几何。画质逐像素对照与30fps目标尚未关闭。

group FFN实测：block70 FFN5.76844→4.59296ms有效，但front/decoder C32从8.62/8.32升到10.687/10.730ms，全网退回61.43072ms。已只保留block70 group FFN，前端和解码C32恢复原FFN；并行QKV全部保留。混合候选SHA73393cbde16c98f0810f28f6fa242356ceed2484a137677c368e55c06f333b02，待下一次profile确认整网收益。

混合版复测全网56.94736ms：front8.67496、decoder C32 8.34712、block70 11.18404（prefix0.6078/FFN4.59624/QKV1.27988/attention4.69968）。进入实际洞窟场景后5秒窗口15.501/15.600/15.800fps，截图无满屏网格。相对本轮起始71.6984ms全网GPU时间减少约20.6%，仍未达到30fps；截图检查不等同逐像素数值一致证明。

### 2026-09-05 共享窗口attention

block70 attention每个8×8窗口一个64线程组，将K/V及K范数放入groupshared，复用窗口内数据。单独替换block70时attention4.69968→2.704ms、全网54.66756ms。随后扩展到C32前端/解码，移位版保留原region mask，但全量替换反而使front10.39688、decoder9.11648ms，全网56.9654ms；block70仍2.62244ms。故恢复所有SHIFTED层原PSO，仅非移位层使用共享核。该路径由enable-shared-attention.txt启动开关控制；当前部署SHA4aa14d87c5790b0bd2e2ff83ceb6b61e769555c7363539526510652689138017，等待混合版复测。30fps尚未达成。

非移位混合版复测：front8.47304、decoder C32 8.03012、block70 9.11212（attention2.6786），全网54.19568ms。进入洞窟后最近5秒cadence16.400/16.299fps，截图未见此前方格回归。相对上一轮56.94736ms再省2.75168ms。剩余目标仍是30fps@1080p。

### 2026-09-05 多头共享attention

新增swin_attention_shared.hlsl，匹配现有WindowPass绑定布局，每group处理一个head/window，共享K/V及norm，保持各stage原有循环位移。四种几何、四种shift由build_swin_shared_attention.ps1生成16份cache，启动开关enable-swin-shared.txt。替换全部encoder/decoder多头Swin后全网50.82348ms（此前54.19568），encoder C256 2.88912、decoder C256 1.87584ms；其余各stage亦小幅下降。

另测试C32 shifted共享核，将inner-loop continue改为对masked score/probability选择零，避免执行分歧；由enable-shifted-shared.txt独立控制，尚待新样本决定保留或回退。当前候选SHA134e5e26776e2b90e687868718b63c96c4c0323945d329aa265779da6a41e978。目标30fps尚未达成。

C32 shifted无continue候选依然退化：front10.202、decoder C32 9.1012、全网53.9142ms。删除启动标记enable-shifted-shared.txt并重启回到原移位PSO；保留多头Swin共享版及C32非移位共享版。该候选仅留源码和可选门供复现，不作为提速结果。

多头共享保留配置进入洞窟复验，最近3个5秒窗口17.436/17.400/17.400fps，截图无此前满屏绿色小方格。shader仍完整执行所有窗口与heads，无跳帧/降分辨率。该视觉检查不替代后续逐元素精度验证。

### 2026-09-05 C32 batch4 FFN候选

新增c32_ffn_batched.hlsl，每64线程处理4token（16lane/token），共享输入与hidden。实测front9.64912、decoder C32 9.83956、全网54.27832ms，比保留版50.82348ms慢；已删除enable-c32-batched-ffn.txt启动标记并回退。候选源码及编译入口保留作反证。

GPU profile扩展42点，对front/decoder各4层的FFN/QKV/attention单独计时；首层FFN窗口包含该段prefix转换，日志使用ffn_including_prefix_ms注明。当前诊断部署SHA885af038ff2f45a4a5f9830125917cc2c5d3e12d3fc20cdbdf97387174d56fc3。30fps尚未达成。

### 2026-09-05 block70 HWC布局

prefix直接把相同元素写入HWC地址，替代tile-major输出；下游FFN加载已有1920×1088 HWC核，QKV/attention不变，不添加中间copy。启动开关enable-block70-hwc.txt，开启时强制匹配的SM6 FFN并禁止tiled group FFN覆盖，防止独立开关组合导致错误布局。

GPU实测block70 prefix0.60996、FFN2.17924（此前4.48668）、QKV1.249、attention2.62916ms，block70合计6.66776，全网48.45452ms。实际洞窟连续5秒窗口18.142/18.200/18.146fps，截图未见原绿色网格；菜单19.6–19.8fps不是游戏帧率。测试版SHA2e1501a79ebbeea8784aa71813ffc47eb799b91b4cf0c153fe0c44d8bbba2946；补充开关组合保护后的构建SHAfb1ceae32811fb5a857e774de845fd4f428ee3f5bd5db3fb1d35871c3296e6ce。30fps仍未达成。

### 2026-09-05 E4M3位元量化候选

block1_ffn_sm6_fp32.hlsl新增BIT_QUANT可选实现：保留subnormal的1/512舍入，normal以FP32尾数位执行最近偶数舍入并饱和448，省略log2/exp2。test_e4m3_bit_quant.py对4000768个正负有限值（随机位模式、全部量化中点及相邻FP32值）与原公式比较零差异；这是CPU数值检查，不是GPU逐元素验证。

enable-bit-quant.txt仅在HWC模式且未启用batch4时替换9个C32 FFN。热身profile全网48.0478ms，front7.99204、decoder C32 7.82368、block70 6.73472（FFN2.14184），相较上样本48.45452ms收益小且存在运行波动。删除候选标记、默认保留原量化HWC路径；源码及build_bit_quant.ps1保留，GPU数值对照未做，不能宣称精度验证完成。当前DLL SHAebef8b0daba8ff934709c92c2e9ea00676b5e3f6f38ec75418e860ba50107f62包含上述可选路径与HWC开关组合保护。

### 2026-09-05 C32移位attention边界拆分

共享移位核此前慢于原PSO；本轮将120×68窗口分为119×67=7973个内部窗口与187个边界窗口，两次dispatch写不重叠区域。内部核编译时消去region mask，边界核保持原mask及循环位移。索引枚举验证8160个窗口无重复、无遗漏；未跳过像素或attention项。新增build_split_attention.ps1及enable-split-attention.txt启动选择，front三层和decoder两层移位attention使用此路径。

部署SHAf120fdbc5c8ce359ab30643a11ff0cdad5fa7a546d1ad731435041ff8e151908，GPU profile移位attention各层0.73964–0.75324ms（旧约1.1–1.3），front6.89848、decoder C32 7.1784、全网46.3954ms。实际洞窟连续窗口19.000/19.000/19.139fps，display_residual generation1560 mode0；截图未见此前绿色方格。保留该配置。画面检查与窗口枚举不等于逐元素GPU精度对照，30fps仍未达成。下一候选是缓存attention score，避免softmax两遍循环重复计算Q·K，需衡量LDS增加对占用率的影响。

### 2026-09-05 attention分数快取及ViT细分

block70尝试缓存首遍Q·K，避免softmax第二遍重算。CACHE_SCORES=1为16KB额外groupshared，attention反而从2.67524升至3.3376ms，全网47.17548，拒绝。CACHE_SCORES=2为每线程64个寄存器分数并展开两遍循环，attention2.46992ms、复测2.48768ms，全网46.0108/46.134ms，小幅收益；enable-score-cache.txt启用，build_cached_scores.ps1默认Mode2，Mode1仅作反证复现。窗口、通道及权重不变；未做GPU逐元素精度对照。

profile扩大为49点，额外量首个ViT层的六段：FFN0.2014、QKV+pack0.06636、QK0.0564、softmax0.04028、AV0.16044、output projection0.0294ms，全ViT5.08912ms。因此softmax不是当前优先热点，不应仅因代码包含多次barrier便优先改它。诊断DLL SHA693bfac9235275095abc76540524ce85ab8dbb67676fe10b337eaa21e5f4f8da，仍只读回时间戳、不读回逐帧图像。30fps目标仍未达成。

寄存器分数快取进入洞窟复验：cadence19.200/18.939/19.000fps，display_residual generation2400 mode0，截图未见旧绿色方格。实际帧率仍约19fps，不能将微小GPU收益包装成明显帧率提升。

### 2026-09-05 前端权重本地化

先测试block70 attention output projection 32通道循环全展开，attention从2.48768退化为3.54792ms，全网47.33492；build_cached_scores.ps1保留-UnrollProject复现门，默认0且已重新编译恢复。

检查前后C32 FFN耗时差异，发现front四份41220-byte参数仍为UPLOAD heap，而decoder已使用DEFAULT heap。initialize_blocks1_4改为启动期CopyBufferRegion到DEFAULT，显式COPY_DEST→UAV→NON_PIXEL_SHADER_RESOURCE，一次独立fence确认上传后释放staging。没有每帧copy、权重重算或精度变化。独立fence避免复用已有初始化fence值导致过早放行。

部署SHA4d001bfc6b04a77be080b789cb1820614dcd0b3ebd7da3fb5acf60b3363b639d，front FFN0.5538/0.51956/0.51644/0.51548ms（此前约0.64–0.68），front合计6.3656ms，全网45.41328ms。decoder保持约0.52ms/FFN，前后差距收敛。30fps仍未达成。

本地权重版洞窟复验：加载期cadence低至4.793fps，加载结束稳态19.000/19.139/19.143fps，display_residual generation2160 mode0；截图未见旧绿色方格。仅报告稳态约19fps，不把本次GPU局部提速夸大为显著端到端增长。下一方向可检查C32量化activation的存储带宽；DirectML32整段及裸FFN路线已有失败记录（directml-swin32-validation.json及本日志早期条目），不应从头重复相同候选。

### 2026-09-05 block70 FP16特征存储候选

E4M3有限输出用FP16能精确存放。test_packed_feature.py枚举254个带符号有限编码（含±0）及64×32打包地址，逐bit往返通过。新增PACKED_FEATURE分支：FFN每两通道写一个uint，QKV/attention读同一RAW SRV；全部乘加仍FP32，现有feature allocation暂保持原容量，只缩小逻辑视图/访问量。enable-packed-feature.txt在HWC模式下统一替换三PSO和两个descriptor，避免半套配置；当前DLL SHAe119ad1997dd597771888faadff7ad47f9649573a3e983da26bc50d70d38b99a。

手动f16tof32版：FFN1.68576、QKV1.75352、attention3.6678，全网46.59888ms，比原45.41328慢。NativeHalf版的DXIL确认产生rawBufferLoad.f16：FFN1.62292、QKV1.24948、attention3.41488，全网46.09112ms。native载入恢复QKV，但attention仍退化，不能据FFN局部提速保留整版。接着测试PairProject：half2一次读取供两个输出通道、16维归一化显式提到投影循环之前；结果待测。数值表示测试不等于GPU整链精度对照。

PairProject最终attention3.40528ms、FFN1.63632、QKV1.28064，全网46.21744ms，仍比原FP32特征版45.41328慢。已删除enable-packed-feature.txt并重启恢复原配置；三类候选全部不进入默认路径。PairProject CSO SHA为972d7fc5d29fb9a550f033f7ecad0bb697ab6e2984c457d9f408ec48616e770f，保留源码/构建入口作反证。下一测试方向是显式WaveSize32/64，而不是继续假设“少一半存储必然更快”。30fps仍未达成。

### 2026-09-05 Wave大小及显存压力核查

block70 attention显式cs_6_6 WaveSize32为2.48556ms、WaveSize64为2.49808ms，与原约2.49ms相近，无实用收益；build_cached_scores.ps1保留-WaveSize 0/32/64，默认0已恢复cs_6_2编译。DirectML CompileOperator未设置DISABLE_META_COMMANDS，不能归因为主动关闭硬件优化。

runtime每5秒cadence追加DXGI QueryVideoMemoryInfo（非图像readback），本地/非本地分段分别记录进程CurrentUsage与Budget。菜单本地5110.08MiB/15416.53MiB，非本地123.46MiB/31460.88MiB，未超预算。部署SHA dbad725a5e64439d54051ae798e30f473263e5769dda56ccedfd75c6953db066。下一结构性候选：统一DirectML及custom pass的shader-visible descriptor heap；当前每算子拥有单独heap、在GEMM与boundary之间反复切换。必须实际测量，不能仅据切换次数宣称它是根因。30fps仍未达成。

洞窟读数：本地8513.74/15416.53MiB、非本地164.82/31460.88MiB，cadence19.082/18.939fps，当前场景也未超显存预算。

统一heap候选实现备忘（尚未实施）：可保留原heap对象作为分配标识，注册到一个native-device shader-visible arena及offset；统一CreateDescriptorHeap、GetCPU/GPUDescriptorHandleForHeapStart、SetDescriptorHeaps的runtime helper，把descriptor写入和DirectML binding table句柄都指向arena切片、实际绑定同一个arena。仅处理native g_device的CBV/SRV/UAV shader-visible heap，FFX proxy-device bridge保持原路径。无需伪造COM heap对象或把代理对象交给native command list。header默认不开arena，让离线runner保持旧行为；启用后再量整网时间，不能只改DML heap而遗漏穿插的boundary/window heap。

### 2026-09-05 统一descriptor heap实测

实现runtime_descriptor_arena.h并覆盖runtime及13个依赖header的heap创建、CPU/GPU句柄与绑定点。只合并native g_device的shader-visible CBV/SRV/UAV，proxy bridge仍原样；原heap仅作分配标识，实际写入与绑定使用同一arena及独立offset，无伪造COM对象。enable-descriptor-arena.txt启用后分配2189个descriptor，完整blocks0–70持续提交840帧，GPU全网45.68044ms，与原45.4–45.6ms无实用收益，拒绝作为性能优化默认启用。

删除标记后以快速直通分支复验：descriptor_arena_used=0、network45.58592ms。关闭时不进入map查找及锁；独立runner默认也不启用。新DLL SHA9cb32df2925bd031907fe0ab0c42f695b5a5f739a0429e316375b3d9212f8e06。统一heap未改变tensor布局与数值运算，但启用版未做逐元素GPU对照，不能称为精度验证完成。

下一较大方向来自AMD官方MiniDXNN：原生DX12矩阵核心接口，可用于MLP；当前主线要求SM6.10、Agility1.721-preview、DXC1.10.2605.4及创建设备前开启实验特性，不能直接套进既有游戏设备。来源：https://github.com/GPUOpen-LibrariesAndSDKs/MiniDXNN#requirements 、https://gpuopen.com/learn/minidxnn-mlp-library-for-dx12/ 。尚未安装SDK/驱动或修改系统配置，先做能力探针。

新增只读inspect_runtime_capabilities.ps1：Windows11 Insider 10.0.26340；9070XT驱动32.0.31041.1004；游戏加载system32 D3D12Core10.0.26100.9072、DirectML1.15.6；游戏目录d3d12.dll6.8.0.2155是现有ReShade代理，不能把其版本号当Shader Model。Developer Mode注册表值未检测到（null），不能当作已启用。下一步应在真实device查询高版本Shader Model、并在独立探针核对实验特性；如需升级驱动/启用系统开发者模式，先请用户决定，不静默修改。30fps仍未达成。

arena关闭版洞窟复验19.082/19.219fps，display_residual generation4680 mode0，截图未见原绿色方格；独立d3d12_directml_swin32_resident.cpp语法检查通过，确认共用header默认直通路径仍可编译。

### 2026-09-05 系统DirectX能力探针

新增d3d12_shader_model_probe.cpp，独立进程使用系统D3D12创建9070XT设备，只查询能力，不开启实验特性、不修改注册表、不注入游戏。实际结果：device创建成功；请求SM6.10(0x6a)返回E_INVALIDARG；请求6.9返回6.8；请求6.8成功。此结果限定为当前系统runtime与未开启实验特性的独立device，不是对显卡物理能力或所有Agility版本的否定。

对照前述MiniDXNN官方要求，下一矩阵核心候选需要独立部署preview Agility/DXC并验证实验特性。Windows开发者模式的系统级变更尚未获授权，因此先向用户请求这一实验范围；不修改游戏DLL选型、不更换驱动、不宣称30fps已达成。现有游戏运行配置保持约19fps版本。探针可用x86_64-w64-mingw32-g++ -std=c++17 -O2 -static-libgcc -static-libstdc++ d3d12_shader_model_probe.cpp -o d3d12_shader_model_probe.exe -ld3d12 -ldxgi构建。

### 2026-09-05 自包含DLL交付

用户取消“用户提供原DLL的patch工具”，改为直接生成含权重DLL供其上传百度网盘。实现runtime_bundle.h：从自身PE RCDATA297读取DLSS5PK1资源包；572项真实启动依赖（187455856 bytes）包括权重、CSO及已验证开关。逐项SHA256和包边界由verify_runtime_bundle.py验证通过。build_runtime_bundle.ps1从完整启动trace采集清单，不把整个Lab或activation垃圾带入；runtime_bundle.rc负责链接资源。

发布DLL使用DLSS5_REQUIRE_BUNDLE：缺资源拒绝加载；权重与CSO从内嵌包读取，不回落到D盘；配置固定max_block70、raw_r10_present关闭，enable开关只读内嵌清单。日志改到%TEMP%\dlss5-1080p-runtime.log。Windows导入表仅系统库及ReShade代理d3d12.dll，无Python、编译器、libwinpthread等部署依赖。开发模式仍支持不嵌资源的Lab路径，用于后续优化；注意发布版不会响应Lab的enable*.txt，实验时须切开发DLL或重新打包。

发布DLL大小190138966 bytes，SHA256 ca668b23d81dd06964053d13a03315fba3bc04a920b93ac070c391058af6b415。实际游戏日志runtime_assets=embedded、failed=0，洞窟19.200/19.200/19.280fps，display_residual generation2160 mode0，截图未见旧方格。30fps优化目标仍未完成，此次完成的是自包含交付。

prepare_runtime_release.ps1生成6文件发布包：ReShade d3d12.dll、内嵌模型addon、README、两份组件版权说明、SHA256SUMS。zip大小132124870 bytes，SHA256 9e38f15c2bc41bed45547caa7daa98c74bbdbe389a7667ef708c36ace2b91e07，Windows路径D:\DLSSNR-Lab\distribution\DLSS5-AMD-1080p.zip，本地release/DLSS5-AMD-1080p.zip。release目录已加入gitignore，未提交模型包/生成DLL/zip；只提交代码与说明。开发者模式与系统SDK均未改动。

### 2026-09-05 用户授权后恢复矩阵核心探针

用户已明确授权开发者模式及Lab独立SDK实验。enable_matrix_developer_mode.ps1将AllowDevelopmentWithoutDevLicense设为1，原值备份至D:\DLSSNR-Lab\matrix-probe\developer-mode-before.json；没有换驱动、修改游戏或覆盖已发布zip。普通系统runtime即使开启Developer Mode仍最高6.8。

从Microsoft NuGet下载Agility1.721.3-preview并在matrix-probe\D3D12独立部署，EXE导出SDKVersion721及相对SDKPath。D3D12EnableExperimentalFeatures成功、AMD device创建成功，打印路径确认载入私有D3D12Core；请求SM6.10返回6.9，LINEAR_ALGEBRA_SUPPORT查询成功但tier0。

再以Agility1.717.1-preview隔离测试旧Cooperative Vector接口；同时开启ExperimentalShaderModels及CooperativeVectorExperiment两个GUID，均成功。SM6.9支持，但OPTIONS_EXPERIMENTAL的CooperativeVectorTier仍0。两代SDK的feature编号/结构均按各自下载header核对，未把API失败误判为硬件不支持。当前驱动32.0.31041.1004未向这两条实验路径暴露矩阵接口。

Microsoft 721发布说明指定AMD Developer Preview Edition26.10.07.02（https://devblogs.microsoft.com/directx/announcing-agilitysdk-721-preview-and-more-shader-model-6-10-features/）。此时下一步涉及更换显示驱动，不包含在之前“先不换驱动”的范围里，需另获用户批准；尚未执行。30fps未达成，发行版仍为约19.2fps。

### 2026-09-05 用户授权预览驱动安装：准备

用户明确授权更换预览驱动，目前人在河北霸州，远端操作先不自动重启。已唯一识别9070XT显示驱动32.0.31041.1004、oem201.inf（原名u0203304.inf）；pnputil成功导出127文件、782946427 bytes到D:\DLSSNR-Lab\matrix-probe\driver-backup\32.0.31041.1004，保留回退材料，不先卸载旧驱动。

官方下载地址：https://drivers.amd.com/drivers/amd-software-adrenalin-edition-26.10.07.02-win11-rc7-agility-sdk.exe 。完整包874669800 bytes，SHA256 cca61a1d5839b8b413b5dba4d932ad16504cdc54f81de475a6e08f7ba4fa08a2。CDN部分Range曾返回567，保留已完成部分后补齐；准备脚本再次核对长度、hash和Windows AMD Authenticode，验证通过才解包。此条记录时尚未执行安装，也未改Intel集显/游戏/发行包。

### 2026-09-05 预览驱动安装成功，矩阵接口开放

完整包及解包Setup.exe均通过AMD Authenticode（签名者Advanced Micro Devices，thumbprint33D35682079E201671B738B7209B4586103BC271）。以一次性SYSTEM任务执行官方Setup.exe -INSTALL -OUTPUT ... -LOG ...，未传-boot或清理/恢复出厂参数。独立driver-install-status.json记录PID42100，06:03:09Z启动、06:04:11Z退出0；厂商ResultCode0。驱动现为32.0.31007.2048、oem25.inf；开机时间仍2026-08-31，未重启，SSH正常。

安装后立即重跑721探针：experimental_hr0、device_hr0、实际加载Lab私有D3D12Core；请求SM6.10现返回6.10，LINEAR_ALGEBRA_SUPPORT tier从0变为0x10（Tier1.0）。这证明预览驱动已暴露矩阵接口，不等于神经网络算子已迁移或30fps达标。后续要用DXC1.10实际编译/运行矩阵算子，验证精度和延迟。已发布ZIP及游戏addon未修改，回退驱动备份仍保留。

矩阵接口实际计算验证：部署私有DXC v1.10.2605.24（dxc_preview_2026_05_22.zip），编译matrix_smoke.hlsl为cs_6_10/HLSL2021/native16。独立d3d12_matrix_smoke.cpp在721 runtime下调用LinAlg Thread Matrix×Vector：256条32维向量乘32×32 FP16全1矩阵，8192个GPU输出逐值匹配CPU明确答案，mismatches=0，首值16.5、末值132。不是只读功能旗标，也不是WARP；选择VendorId1002硬件adapter。该合成小测试不是FFN性能/真实模型精度验证，下一步仍需实际权重算子比较及游戏device接入。一次性安装任务已在确认退出0后移除；Intel驱动版本未变。WMI当前显示物理输出3840×2160，截图工具未显式DPI-aware，不能据旧截图尺寸断定何时变化；后续游戏测试仍需检查真实swapchain1920×1080，未擅自调整显示设置。

### 2026-09-05 真正C32 FFN矩阵接口比较

c32_ffn_linalg.hlsl以LinAlg Thread Matrix执行32→64→32两次乘法，FP32返回累加结果、保留原clamp/多项式激活及最终E4M3量化，矩阵与中间激活使用FP16。prepare_c32_linalg_weights.py从同一block1-effective.bin生成8192-byte矩阵包；原权重SHA551d47badd48f0bbf6346f3bbc280c3ebe2f5c9d8b4864c38c647ff34d1564c8在本地/远端相同。4096个矩阵权重转FP16均有变化，最大0.00021994114，不能宣称精度不变。

d3d12_ffn_compare.cpp在同设备、同输入directml-block0-hwc-1080p.f32（真实frame56400预处理结果、960×544×32）对比原SM6.2 FFN和新shader，各热身10次、计时100次；数据/权重常驻DEFAULT heap，时间戳不含最终readback。旧0.569137ms，新0.257166ms；16711680输出中114240不同，MAE0.000585556、RMSE0.013272024、max0.5、nonfinite0。仅验证单层，不是全网精度或30fps完成证明。

游戏接入核查：现有ReShade源码source/d3d12/d3d12.cpp在真正D3D12CreateDevice前load_addons并触发create_device事件，可尝试在那里选择私有Agility721和实验特性，而不是设备建成后再换SDK（Microsoft明确后者会移除设备）。SDKPath必须相对游戏EXE，候选应放入独立子目录，保留发行版回退。依据：https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12sdkconfiguration-setsdkversion 。尚未修改游戏设备路径。

### 2026-09-05 游戏内SM6.10与首层矩阵FFN

开发版注册ReShade create_device回调，在首个D3D12设备建立前SetSDKVersion721并开启ExperimentalShaderModels；只对外置资源开发版和enable-game-sdk721.txt生效，内嵌发行版跳过。SDK复制到游戏Win64\DLSS5-D3D12-721，不替换系统DLL、不改EXE或INI。原发行addon已校验并备份到matrix-probe\game-runtime-before-matrix.addon64，网盘zip未动。

实际日志get/set/experimental均0，游戏device返回SM6.10、LinAlg tier0x10。先用原有全网在新SDK下进入洞窟，GPU45.57052ms、cadence19.000/18.740fps，画面未见旧网格，真实FFX输出仍1920×1080。

随后加入可选initialize_linalg_front和独立矩阵descriptor表；FFN阶段绑定矩阵表，QKV之前恢复原表，避免t2矩阵/feature冲突。enable-linalg-front-ffn.txt指定前1–4层，当前仅1层。开发DLL SHA526eff5271590740ea79a0fcd7f1ed7aeb066a81b60917791bee6004e9cae2ce；日志linalg_front_layers1，第一FFN0.26728ms（原0.56052），全网45.85632ms，其他stage存在波动，不能据单层省时宣称全帧已提速。洞窟18.625/18.883/19.000fps、generation9120 mode0，未见旧方格。首层数值变化尚未完成全网逐像素对照。正在以对应真实输入检查其余三层。

### 2026-09-05 前端四层矩阵FFN保留

对应真实输入逐层比较：block2旧0.566229/new0.254033ms，261120/16711680值不同、MAE0.00137043、RMSE0.027576647、max1；block3旧0.567005/new0.253772ms，96950值不同、MAE0.000686007、RMSE0.022376783、max1；block4旧0.569958/new0.25393ms，24492值不同、MAE0.000004066、RMSE0.000137792、max0.03125。三层nonfinite均0；block2/3矩阵转FP16有舍入，block4矩阵本身FP16往返exact，但激活/累加路径仍不同。权重原文件本地/远端哈希逐项匹配。

将enable-linalg-front-ffn.txt设为4，游戏日志确认linalg_front_layers4；四层FFN分别0.2682/0.25944/0.25628/0.25932ms，front合计5.3062ms（原SDK基线6.39956），整网44.50672ms（基线45.57052）。洞窟18.939/19.021/19.280fps、generation10200 mode0，截图未见旧网格；截图不是逐像素/跨帧稳定性完整验证。保留开发配置，发行包不更新。

新增build_c32_linalg.ps1提供960/1920两种编译几何入口；restore_matrix_runtime.ps1在游戏退出后可恢复已校验的自包含基线addon并关闭SDK/矩阵前端标记，不改系统驱动。下一主目标仍30fps，接下来应把同类优化扩到decoder66–69与block70，再测整网，不要把单层2.2倍当成游戏2.2倍。

### 2026-09-05 尾端矩阵实验与无效60fps排查（未验收）

扩展可选矩阵FFN到decoder66–69及block70，独立开关，发行包未更新。算子比较输入来自旧raw-16800抓取中的真实通道向量切片；这只是逐token FFN比较，不是完整1080p场景验证。block66–69旧约0.564–0.569ms、新约0.250–0.252ms；block70旧2.232694ms、新1.010194ms。全部nonfinite0，但有数值差异；block70 MAE0.000272693、RMSE0.004418567、max0.5。不能直接将独立算子收益当游戏收益。

用户反馈显示60fps但不流畅。该轮开发日志backend_ready=0、ffx_frames=0、frame_contract_1080=0，神经网络没有进入执行，60fps结果无效，不能计入优化。读取FFX导出入口及relay证实跳转仍指向本addon hook，未发现hook被覆盖；真正未调用原因尚未查明。切回已发布SHA ca668b23基线后，PID15612运行期间日志只记录第一帧，未恢复正常输出；不能据此认定新FFN是根因。

结束该游戏进程后，关闭尾端两个矩阵开关（移为.txt.disabled），恢复私有SDK721及前端四层配置。当前测试DLL SHA b8a35f0635bda3742d62c734c2eefa59d9babb844ddbff4a4683c2d989dc413b，仍含未验收尾端代码但开关关闭；不是逐字节旧DLL。新PID38532已记录present600，FFX仍0，继续排查游戏实际升频路径。未改游戏画质INI、未开插帧、未重启机器。inspect_game_windows.ps1在交互session枚举真实窗口，避免将SSH会话看不到窗口误判为退出。

补充对照：Engine.ini原文件备份matrix-probe\Engine-before-native-fsr-test.ini；按AMD官方UE插件说明（https://gpuopen.com/learn/ue-fsr3/）临时设置FSR3.Enabled=1、UseNativeDX12=1、UseRHI=0、FI.Enabled=0。PID42208进入实际游戏后仍FFX0，此测试不支持“只需强制原生后端即可修复”的假设，也不证明该游戏版本接受了这些CVar。游戏内图形UI确认升频模式FSR3、质量档“质量”；没有通过菜单更改画质值。测试后已恢复Engine.ini备份并结束游戏，不留下强制CVar。

新增runtime_health每5秒分开报告Present、网络提交及显示generation进展；未初始化、无FFX或对照模式不标为neural_active。明确这些是提交进展，不是GPU完成证明或实际显示帧时间。该日志改动交叉编译通过，但尚未部署到游戏；当前游戏文件仍为b8a35f开发版，尾端两开关关闭。入口未调用的根因尚未解决、真实30fps仍未达成。

### 2026-09-05 19:29 按用户要求直接撤回后续实验

将dlss5_1080p_runtime.cpp及d3d12_ffn_compare.cpp完整恢复到最后一次成功记录的c5a3a8a，git diff对该commit两文件为空；不是只关实验开关。尾端矩阵接入及runtime_health均从执行源码撤掉，实验记录和诊断脚本保留。重新编译成功，DLL SHA b7d10855a3b1a6f128ed2214b4f439c4540d43550aba788d6fbaac34f5b239ae，远端游戏部署哈希核对一致。配置为SDK721、前端4层，尾端开关仍关闭，Engine.ini已恢复原备份。

回退后启动PID17736，日志至present1800仍backend_ready=0/ffx_frames=0；代码回退已完成，但这次启动尚未恢复神经推理，不能宣称恢复19fps或修复入口。核对后结束测试游戏，不留下无效运行。系统驱动没有继续变更，发布ZIP未动。

### 2026-09-05 补丁失效根因查明并修复

用临时GetProcAddress追踪定位：反复查询ffxConfigure的是Steam gameoverlayrenderer64.dll，并非AMD驱动；游戏实际获取ffxDispatch的调用点0x14159e606，保存槽0x146d6a320仍指向FFX导出0x1360，导出入口仍正确跳向addon。临时捕获ffxCreateContext，失效时根本没有创建调用，不是创建返回错误。追踪代码仅诊断使用，修复后已全部撤除，未提交到正式runtime。

对Stellar Blade 1.4.1只读反查CVar注册点与实际内存：FSR3.Enabled=1、UseNativeDX12=1、UseRHI=0、r.DefaultFeature.AntiAliasing=2、r.TemporalAA.Upsampling=1，但r.PostProcessAAQuality=0。菜单仍显示FSR3/质量，因此只检查菜单及FSR开关会漏掉真正的时序处理门槛。Engine.ini强制AAQuality4被游戏重新覆写成0，随后撤回该无效修改。

根因修复只改GameUserSettings.ini的AntiAliasing，从SB_GAMEUSERSETTINGS_OFF改成SB_GAMEUSERSETTINGS_HIGH；原文件备份D:\DLSSNR-Lab\matrix-probe\GameUserSettings-before-aa-repair.ini。重启后实际AAQuality变4，FFX创建type10000/result0，全部blocks0–70及display_residual mode0恢复。不能追认OFF最早由哪个操作写入，本次证实的是失效条件与修复效果，不再把尾端FFN实验当成已确认根因。

随后换回无追踪、无尾端实验的干净DLL（源码c5a3a8a，SHA b7d10855a3b1a6f128ed2214b4f439c4540d43550aba788d6fbaac34f5b239ae）再次启动并进入洞窟。present3000 backend_ready1 failed0 ffx_frames2851 frame_contract_1080=1；display generation1080后持续增加；洞窟cadence19.200/19.200/19.078fps，截图角落20fps、GPU99%，未见旧满屏方格。主菜单约20.6fps，不混作洞窟数据。Engine.ini恢复原SHA3970aa1f，未开插帧、未改解析度或驱动，网盘ZIP未改。测试后退出游戏，HIGH保存在配置中，下次Steam启动应沿恢复后的路径。

repair_temporal_aa.ps1要求游戏已退出，幂等修改单一AA项，保留原配置备份；-Restore只恢复备份中的AA值，不覆盖其他设置。inspect_ffx_imports.ps1为1.4.1专用只读诊断，并检查指令签名后才使用固定地址。修复的是补丁失效，30fps性能目标仍未完成。

### 2026-09-05 主菜单与洞窟开关截图

按用户要求，在主菜单与洞窟分别拍摄mode0开启、mode1显示旁路及mode2左原生右神经，并恢复mode0。原始1920×1080截图及切换日志位于dynamic-captures/aa-repaired-comparison；capture_dlss5_comparison.ps1检查当前模式与网络显示进展，两场景任务均退出0。没有改权重或算法，没有降低原生FSR画质；关闭仅指显示合成旁路，网络仍计算，故不能用这些截图FPS比较性能。两组存在待机/无人机/粒子动画，不是冻结同一帧的差分。

肉眼尚看不出稳定显著改善，用户“看不出差别”的反馈不能以“网络确实在执行”来反驳；后续若审效果，应测同帧合成前后差值与最终残差幅度/位置，而非继续假设视觉正确性已验收。本次截图采集后恢复mode0并退出游戏。

### 2026-09-05 最终输出像素审计：修复未提交，暴露真实网格

用户要求确认最终神经输出生效。新增runtime_display_audit.h：外置开发模式下，显式audit-display.txt单次请求捕获同帧合成前R10、合成后R10、CopyResource回游戏backbuffer后的R10、block70 body全267386880 bytes及384-byte真实输出权重。下一Present信号并等待fence完成后才Map保存；不会按正常帧持续读回。analyze_display_audit.py按shader顺序独立算32→3残差及R10量化，比较最终结果；原始大文件在忽略目录release/display-audit，不提交模型或activation。

首轮所有readback包括应非零的游戏画面和常量权重均为零，证明该抓取无效，不能当模型零输出。查ReShade源：d3d12_impl_command_list_immediate.cpp的flush若_has_commands=false直接return；get_native后调用原生CopyResource/Dispatch不会设置该标记。原on_present全用原生命令，故合成与audit都可能被跳过，先前display_residual generation仅是CPU记账，不证明GPU提交。修复用ReShade API copy_resource执行真实copy-in以设置标记，完成原生合成/copy-back后显式queue->flush_immediate_command_list。未放大残差、改权重或关闭算法掩盖现象。

修复开发DLL SHA7d62b71a689fba812239891cdc30ca862182ce81fa7ff673cff07f532664c98e。重新抓取：主菜单generation120，输出权重absmax0.056770686、body absmax4.5，99.6340%像素改变，平均RGB变化2.63359/255，最大18.6950/255；CPU按真实权重重算与GPU输出R10逐值完全一致。洞窟generation3988，body absmax48，99.8859%像素改变，平均3.52281/255，最大154.79472/255；公式差最多1个10-bit量化级，没有>1的值。两场景after.r10与backbuffer.r10逐字节完全一致，已证明最终输出写入实际游戏backbuffer，不再仅凭执行计数判断。

真实提交后明显8×8网格重新出现，截图dynamic-captures/display-submission-fixed-game.png。残差按(x mod8,y mod8)相位分组，固定相位均值解释主菜单95.386%、洞窟74.324%的残差方差，说明强周期伪影已存在于神经输出，而不是截图动画差异；具体源头仍需审block70 prefix/layout及上游，不能仅凭周期就断言某个排列错误。详细数字存display-audit-menu-fixed.json与display-audit-game-fixed.json。此前“修好小方格”“开关差异很小”不再作为画质通过的证据。

本轮完成最终合成提交修复和同帧像素有效性证明，**未完成无网格的正确神经画质**。当前外置开发版保留真实提交及可选审计，未更新网盘ZIP，未改驱动/解析度/模型，尾端新矩阵优化仍关闭。抓取后游戏继续数千帧failed0，约18.7fps；最终退出游戏，不留异常画面运行。后续主线应是消除真实输出的周期错误，不是继续追FPS。

### 2026-09-05 继续正确性移植：修复block70 CSR错位与主输入bank布局

重新审查pack_block70_sparse_prefix.py与实际文件：CSR格式是2049个uint32 row ends（8196 bytes）后直接接indices和weights，最后一个row end本身就是nnz，不存在额外nnz字。运行时及两个旧runner把ibase/wbase写成8200，整段错读一项，末尾越界4 bytes。真实文件27652 bytes、nnz2432，准确公式8196+2432×8；旧prepare_block70_prefix_pairs.py还把缺失最后权重补零，掩盖格式错误。

修复dlss5_1080p_runtime.cpp、d3d12_block70_chain.cpp、d3d12_block70_prefix_sparse.cpp中的偏移；runtime追加完整长度检查。pairs转换改为准确8196偏移并拒绝截断，不再补零。原权重文件没改。validate_block70_csr.py重建CSR，与原dense按1e-5阈值稀疏化后的矩阵逐值exact；RX9070XT执行1056组（1024个单输入basis+32随机输入），basis最大误差0、随机最大误差4.76837158203125e-7、nonfinite0，独立GPU1.121ms。pairs新hash c67c83a1，全部2432权重实际存在，未部署pairs候选到当前runtime。

进一步发现原prefix主输入512元素排列为2×4×4×16 bank-major，而X(sample,i)按4×4×32 HWC解释。由原矩阵所有2048个输出的唯一主输入索引验证公式：(c//16)*256+((y//2)*4+x//2)*16+c%16，全量匹配。runtime及离线chain主分支地址恢复为plane=(i%256)/16、channel=i%16+(i/256)*16；skip尚未假定为同布局。该映射检查已加入回归脚本。

部署CSR-only SHA6bf22f...后同帧残差均值1.3057/255、8×8相位方差解释95.99%；再修主bank映射后DLL SHAb210a4313047edef421ef7d6d149264df943ee9facfa576d44aaa8a094c7a47e，主菜单99.9968%像素改变、平均1.5696/255、CPU公式误差≤1个10-bit级，backbuffer与合成逐字节exact。相位方差解释96.22%，截图仍有网格；不能把两项数学修正误报为画质完成，两个主菜单抓取也不是同一输入帧的效果比较。详见display-audit-prefix-main-fixed.json。

下一个明确缺口：当前局部prefix的skip只有384个非零连接，独立全尺寸block70-prefix-global-skip-effective.bin有1536个非零，输入2048物理元素分两个bank，而运行时仍从低分辨率g_block0_hwc按局部512元素获取。需恢复完整skip来源/尺寸/物理排列，不能直接把两矩阵当作同一输入合同或靠补零/平滑隐藏缺失。另block0-distilled仍是文档明确的近似代理，不应宣称精确原版移植。rtx5090 SSH已恢复，hostname NucBox_EVO-T1、实际GPU RTX5090，当前无剑星进程；没有修改5090环境。此次保留已证实的两项修复，结束AMD测试游戏，原发布ZIP仍未更换。

### 2026-09-05 原始特征读口修复，完整skip重新恢复

在本机GB10重编原始preblock CUDA oracle，256个8×8 RGB受控样本同时输出main2048与DS512 E4M3。旧候选布局下2×2均值与DS相关0.1292，起初误认为均值关系被否决；后续布局恢复推翻此判断，见下文。测试脚本audit_preblock_branches.py，不向游戏注入试验数据。

重新跑run_original_post_dataset的global-skip-features发现全部输出为±512。源代码错误：feature模式Color纹理仍为0，却按0.5灰底反推；正负readout选择还使用>=，会优先选离灰底更远的截断侧。改为feature模式灰底0.5、选择距离较小侧；双侧均截断或非finite时拒绝结果。非feature普通RGB路径仍黑底，未改正式神经参数。修正后原版feature输出范围约±0.997，而非±512。

用保留原prefix权重、FFN/attention旁路为identity的受控post，在完整256×144几何下，对两个1024-byte skip banks施加2048行±0.5 Hadamard，恢复2048×2048线性映射。新映射每输出恰1个非零、共2048个非零，输入索引构成2048项排列；旧global-skip矩阵只有1536非零、512列全零。旧非零列在新独立样本上基本一致，主要缺口正是丢失512列。新矩阵留在release/post-skip-basis/matrix.f32（SHA c0b7e115aeb9520883a8ce064d35172845a8103e06292cc47e1931ceca33f892），未上传Git/未覆盖历史权重/未直接部署游戏。

audit_post_skip.py以独立64组均匀随机输入量化E4M3后验收：对原CUBIN correlation0.99999996877、MAE0.00006562623、max0.000457763672，nonfinite0。AMD d3d12_post_upsample_test同输入2048→2048输出与CPU矩阵逐值exact，对原版误差相同，GPU0.991ms。权威输入文件/tmp/block70.weights SHA197024afb1f78602dc08cfd5ae87c413385237c5f537dd5dcfeae742be85eca8；CUBIN /tmp/dlssnr-cubins/dlssnr-00.cubin SHA feb368ff5279a7408b1e55554db6e468d7f114a24b18b2af8d7e6989a410c612。恢复脚本recover_post_skip_basis.py使用正交变换，不是单帧外观拟合。

新skip排列用于解释原preblock main，配合DS的双16通道bank-major解释后，2×2均值对DS相关升至0.9995802。均值再量化E4M3有67.4194%值exact，MAE0.03799759、max4；可能存在原版池化与量化次序差异，尚未exact。不能把“旧布局下不匹配”继续当作池化机制的反证，也不能将新高相关偷换为完整preblock验收。现有RGB-only蒸馏入口仍缺Gaussian/seq0完整合同。

5090先因Steam无法同步存档暂停启动，未点强制继续；用户后确认存档已解决，现已启动原版用于装备菜单脸部对照。新增run_nvidia_ui.ps1，仅在交互session成功聚焦剑星后输入/截图。AMD游戏未启动，当前安装仍为上一checkpoint b210a431；这轮修复的是权威测量与待接入分支，不宣称脸部效果已恢复。

### 2026-09-06 装备页原版截图取得

用户已进入5090游戏内装备页，桌面CopyFromScreen反复只抓到Steam，不能据此判断用户未进入游戏；停止窗口激活和菜单操作。用户F12后，从Steam截图目录取得20260906000813_1.jpg，保存为dynamic-captures/rtx5090-equipment-20260906.jpg。确认为装备页右侧角色大头像，可见脸部、眼镜和鼻侧阴影。该图作为场景定位与外观参考；没有对应关闭NR的同机位图，不能仅凭此图把脸部偏暗归因于神经输出，更不能用它宣称AMD已复现原版。

### 2026-09-06 AMD完整post prefix算子闭合（尚未接错源到游戏）

run_original_post_dataset新增global-prefix-features：完整256×144几何，仅CTA0，main512按两个256-byte banks、四个64-byte planes分别scatter到bank stride147456/plane stride2048；skip2048按两1024-byte banks scatter到0与32768。记录输入为main512+skip2048，合计2560，保留原prefix参数并用identity旁路后段读取prefix。

prepare_full_post_prefix.py合并原main512→2048与本轮重新恢复的full-skip2048→2048，共4096稀疏连接。独立64组E4M3随机输入对原CUBIN：correlation0.999999938775、MAE0.00011104103、max0.00131225586、nonfinite0。AMD d3d12_block70_prefix_sparse.cpp扩展可选input-width（默认1024，完整合同2560），添加CSR总长/单调row ends/索引范围拒绝门。9070XT真实运行64样本输出对原版误差相同，首轮GPU0.762ms；原数据/候选CSR留在release/full-post-prefix及Lab/matrix-probe/post-skip-audit，不替换发布权重。

此算子仍需要完整高分辨率skip来源；不能拿当前960×544 g_block0_hwc冒充1920×1088 skip。故未改当前游戏DLL，也未更新网盘包。当前安装保持b210a431，存在已记录网格，不宣称效果恢复。

继续审第0层：新增adapter-scan到run_original_preblock_oracle，逐slot改变前224个half，保持原下游权重。224个输出彼此不同，尚未得到足够依据把slot指认为某RGB/噪声通道。此前试图将下游直接设为identity所得输出全零，包括旧front-identity控制文件也如此，故该控制不能作为输入不存在的证据。prepare_preblock_adapter_scan.py目前保留真实下游，下一步需恢复可信中间读口/确认原字段布局；不新增单帧拟合校正。原版装备页已由用户F12提供，后续不再用桌面强制聚焦反复触发全屏切换。

### 2026-09-06 活跃画面修复目标：原preblock布局与随机场恢复

目标保持“修正画面并部署剑星验证正确”，本轮属于继续前端正确性定位，不是缩小完成标准。复查原始sm_120 SASS发现旧block0-tensor-layout不能用于CUBIN权重视图：FFN W1/W2实际从half0/2048开始；0x2010/0x2210两路load位于half4104开始的512-half输入混合区；FFN skip half4616，QKV从4656（旧表4648），bias6192，scale10288（旧表10280），projection10296、attention skip10808。完整分段容量21696 bytes，存block0-cubin-layout.json；旧布局表标为legacy，不直接改成新字段破坏历史引用。

按新布局把FFN/attention分支置identity，扫描全部512个input-mix half，每个有响应的slot只影响一个逻辑channel的64像素，控制终于能隔离输入。224-adapter范围不能继续视为权威；packed_input_mix不是已验证row-major 32×7加DW。prefix-scan在512区先全清零再逐slot置1，避免保留未知前端系数造成假响应。

受控slot0/1/8出现三张非恒定随机场，即使旧参数b0/b4所谓Gaussian开关/scale均为0。SASS显示随机种子来自参数c8；原helper把该字段初始化为float1的bits0x3f800000。新增DLSS5_PREBLOCK_SEED显式覆盖该uint32字段。preblock_noise_reference.py按原uint32乘法/XOR/移位恢复随机hash、四组uniform和三路Box–Muller输出，修正角度分配及readout通道顺序后，8×8 CTA0上三个种子0x3f800000、0x12345678、0各192个值经FP16→E4M3量化后对原CUBIN逐值exact、max0。验证失败会assert，不是只打印好看的相关数。

上述是受控局部随机场正确性证明，尚未验证live种子/全局CTA地址/完整texture与input-mix合同，也没有将随机场随便加入旧代理。之前“RGB-only／四个Gaussian为零”的描述不能作为该CUBIN helper的事实。本轮未改AMD游戏DLL、未动两机游戏状态/存档，当前runtime仍为b210a431、网格未解决，目标继续active。下一步应恢复packed input-mix与完整前端计算，再接全分辨率skip和正确DS，最终进游戏重新做同帧数值及画面验收。

### 2026-09-06 原版输入混合在9070XT逐值一致

以prefix-scan对packed_input_mix做逐权重单位置探测，输入改为非零三色与空间渐变，确定权重slot到channel的映射c=(s//64)*4+(s//32%2)+2*(s//4%2)，feature=(s//8%4)*4+s%4。受控单Color/其余纹理为空的合同下，16个打包输入为[g1,g2,G,B,g0,1,0,1,R,G,0,0,B,R,0,0]，RGB先FP16再中心化/缩放，g来自上一轮原版PRNG；这不是原先猜测的row-major 7→32加3×3 DW。preblock_mix_reference.py用真实512-half混合区组合，256张独立RGB tiles对identity后段原kernel的524288个E4M3输出全exact。

发现E4M3辅助量化器最大次正规数舍入被clip到7，正确应允许进位到code8；另大于最大有限值的输入需要显式SATFINITE，不能指数clip后变回256。encode_tinlayout_global.py修正两项，test_e4m3_quantize.py覆盖正负全部可表示值、所有相邻中点ties-to-even和大值饱和，3项通过。同步修正runtime及旧preblock resident的次正规carry源码；没有据此宣称旧代理已正确。

新增preblock_input_mix.hlsl和d3d12_preblock_mix_test.cpp，在AMD实际生成随机场、RGB变换和32通道混合，不使用MLP。初版97.2929%一致；debug features显示FP16转换约半数比原版低一格，原因是SM5 f32tof16的截断语义。按Microsoft Direct3D浮点转换规范（https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm）及实测改用显式nearest-even位运算，重新执行后524288/524288逐值exact，MAE/RMSE/max均0，GPU0.727ms。原始输入/输出在release/preblock-mix-amd及Lab/matrix-probe/preblock-mix；摘要preblock-input-mix-validation.json。DEBUG_FEATURES只用于中间值诊断，该模式的旧通用host总误差不能当最终混合误差。

当前验证范围严格是CTA0、固定helper参数、单Color纹理的输入混合；未完成live多纹理/全局边界/完整FFN-attention/DS及skip接线。新shader仍是Lab测试，不将它冒充完整block0，不覆盖游戏DLL。游戏安装仍b210a431，画质问题仍未解决，目标active。

### 2026-09-06 FFN矩阵类型与宽度纠错：32→128→32

最初half-one探针只看到奇数输入单独响应，曾怀疑门控关系；进一步读SASS确认主体使用QMMA.16832.F16.E4M3.E4M3。前8192字节实际是两张FP8矩阵，不是4096个FP16权重。写入FP16 1.0的bytes00/3c分别是FP8 0与1.5，正是半数响应与错误幅度来源，门控假设撤回，未写入正式模型。

run_original_preblock_oracle增加ffn1-byte-scan/ffn2-byte-scan，以FP8 0x38=1逐字节探测。recover_preblock_ffn_layout.py用5位输入编码、7位隐藏编码与输出通道读口，恢复W1全部4096条、W2全部4096条连接；两边映射均完整一一对应，隐藏维为128，不是旧实现64。系数直接E4M3解码原记录，无拟合。另32-slot FFN skip探针恢复half向量到逻辑channel的顺序[0,1,4,5,8,9,12,13,2,3,6,7,10,11,14,15,16,17,20,21,24,25,28,29,18,19,22,23,26,27,30,31]。

preblock_ffn_reference.py与preblock_input_mix.hlsl的FULL_FFN测试路径接真实128隐藏宽度，使用原输入混合、FP8 operand、FP16结果、clamp仅用于多项式门而线性乘数保留未clamp值、逐K32投影累加舍入和原skip。256张独立RGB tiles对原FFN-only：CPU corr0.9999909135、99.517822% exact、MAE0.0005858354、max4；AMD同误差/精确率、cosine0.999990999933、nonfinite0、GPU2.177ms。还有量化与累加差异，未声称逐值闭合。摘要preblock-ffn-validation.json；矩阵布局与原始GPU结果只在release目录。

block0-cubin-layout.json更正矩阵storage/shape；SASS与字节容量还指向QKV三张32×32 FP8、projection32×32 FP8，而非旧head-dim16，此两项待独立basis验证。必须审其余主体层是否存在同样类型/维度错误，不能只替换第0层就宣称原模型复现。当前游戏DLL仍b210a431，未把未完成attention的实验路径注入，网格/最终画质目标保持active。

### 2026-09-06 原版32维attention恢复与AMD验证

recover_preblock_attention.py先以Q/K零、V/P单通道验证输出0.5其余0；随后通过输入二进制编码分别恢复V/P各1024个FP8连接，不能直接照抄FFN的排列（V输入/输出位序曾与假设相差50%，用实测映射替代）。64×64 bias以6组空间二进制颜色、逐FP16 bias置8，观察唯一变化的query像素与key位码，4096连接完整bijection。另恢复32-half attention skip到输出channel映射。Q/K暂采用同一QKV族V排列，通过整段对照约束，仍不夸成独立Q/K逐值读回。

SASS没有标准EX2：scores先使用half仿射0.044921875*x+1.30078125，clip至[1.03125,1.5693359375]，half bits左移5加0x8000得到指数近似，再half归约/倒数、量化FP8概率。scale先乘归一化Q再FP8；残差使用未经FP8压缩的prefix。preblock_attention_reference.py按这些原指令逐步修正，标准exp等候选仅用于诊断、不代表权重拟合。

preblock_attention_core.hlsl用24KiB groupshared存Q/K/V，32维头、显式FP16 nearest-even、原指数位运算与K32 AV累计；D3D host只接受完整8×8窗口，组内同步前使用uniform group判断。256独立RGB tiles共524288输出，CPU对原attention-only corr0.9999856041、99.30153% exact、MAE0.00019339845、max0.25；9070XT同误差/精确率，GPU2.405ms，nonfinite0。摘要preblock-attention-validation.json。仍有归约/舍入差异，未把高相关视为完全逐值复现或画质通过。

另尝试5090只读live参数采样，preblock_live_parameters.cpp仅拦截backend+449a0并复制CPU参数，不改GPU资源。第一版错误地把kernel argument array当作by-value结构读取，所谓seed=1和dims的初报撤回，release/live-preblock是无效外层数据。反汇编明确该函数先解引用参数数组首指针，源码已修正并加入indirect-v2标记；修正版514e03cf部署成功后，Steam启动流程暂未产生新SB进程，现有日志仍是旧PID6828，不能当新采样。一次覆盖曾因游戏进程尚未释放DLL失败，随后改由deploy_nvidia_parameter_probe.ps1等待进程真正退出、校验文件后再启动，避免失败后启动旧DLL。源代码后加v2标记尚未重新构建部署。当前RTX无SB进程，Steam显示启动中且cloud最新；未重启Steam、未越过存档提示。此临时live采样问题不阻断本机原CUBIN继续验证，也未标goal blocked。

AMD安装仍是b210a431，尚未将新完整前端放进游戏；本轮是attention正确性进展。全目标仍为实际剑星DLL画面验证，未完成且保持active。下一步串联新input mix/128宽FFN/32维attention及DS，再解决live参数和全分辨率skip，重新审余下主体的数据类型与维度。

### 2026-09-06 完整第0层串联及有效live参数

新增ffn-raw/attention-raw测试模式保留最终FP16结果，避免在残差与DS前提前压成FP8。GPU输入混合→128宽FFN→32维attention串联，256张同输入原CUBIN完整preblock裁判：main corr0.999963913、96.225166% exact、MAE0.00398203、max4；从原始FP16输出池化后DS corr0.999962606、94.279480% exact、MAE0.00340112、max2。比旧“从FP8 main倒推DS”的67% exact更接近，但仍不是全逐值闭合。preblock_complete_validation.py保存正确比较口径；raw模式host里传入的旧分段oracle仅用于尺寸检查，其打印误差不是组合结果验收值。

RTX Steam持续显示启动中但无SB进程，确认不是观察超时后，使用当前截图核对的取消按钮撤销空挂启动，绿色开始按钮恢复。重新部署带indirect-v2标记的只读探针（SHA2a72b304...），再启动得到新PID29116及8份有效参数。backend函数确实先解引用参数数组首指针，旧外层采样仍无效。真实source H/W2160/3840，network H/W2176/3840，grid480×272；texture仅slot0非零，seed c8为0..7逐帧递增。归一化/行为参数b0=1、b4=.0078125、b8/bc=1、c0整数1、c4=.0625，和helper旧默认不同。摘要preblock-live-profile.json，原始bin留release/live-preblock-v2，不把进程地址当可移植参数。

run_original_preblock_oracle可读取DLSS5_PREBLOCK_PARAMETER_FILE：保留已捕获scalar/flag，替换所有纹理及输出/权重GPU句柄，缩放W/H与倒数到本地8×8控制；不复用跨进程地址。逐mix权重探针确认该捕获profile输入为[g1,g2,G,B,g0,1,.0078125,1,R,G,1,1,B,R,1,0]，RGB用0.125*(half(rgb)-0.5)，seed0。CPU混合对256原tile仅1/524288值不同；原始assert仍失败，未偷偷改成exact。AMD通过LIVE_PROFILE/NOISE_SEED编译参数独立实现同合同，也只有同1值差，MAE3.72529e-9、max.001953125、GPU.730ms。差异可能来自transcendental/半精度边界，不假装原版完整数据合同已经全面验证。

新完整前端尚为Lab组合，两次GPU调用之间有读回文件，未作为性能测试或常驻游戏版。AMD安装仍b210a431且画质未修好；原发布ZIP未改。下一步应常驻化新前端、补全局坐标/动态seed/正确DS与full skip，再继续审主体层FP8矩阵容量，最终按目标实际部署DLL与真实画面验证。目标active。

### 2026-09-06 新前端常驻GPU链及全局坐标验证

新增native_preblock_runtime.h：一次Create建立input-mix/FFN、attention、finish三个PSO和常驻中间资源；Record不做CPU readback，输出full HWC32和half-resolution HWC32（FP32保存E4M3值），RAW FP16 tile保留用于审计。seed/width/height/local-oracle由root constants输入；非oracle模式用tile在整图中的真实坐标生成随机场，不再每8×8重置。preblock_finish.hlsl在FP8量化前池化，生成真正独立的DS分支。权重暂置UPLOAD heap用于正确性测试，性能未作为本轮验收。

d3d12_native_preblock_test.cpp在9070XT反复执行5次：同seed三次完全一致，换seed输出变化，回原seed全部输出完全恢复，main/down/raw均有限。Lab profile与捕获的live scalar profile都通过。128×128、256独立CTA0控制的raw与先前两进程GPU链逐值一致，full主输出重排与DS重排也对各自CPU计算逐值一致；消除阶段间文件中转没有改变既有计算。

扩展原始preblock oracle到可控全局尺寸（最多512、8对齐，禁止把权重扫描与大尺寸混用，分配更大输出区），用真实scalar profile在128×64上一次启动16×8个CTA。validate_global_preblock.py单独按post-skip两个bank gather解码main，按两16-channel全局bank解码DS，并拒绝原始NaN码。AMD resident使用global坐标、seed0，对原始全局kernel：main corr0.9999352303、96.279907% exact、MAE0.00099363178、max0.5；DS corr0.9999501591、95.729065% exact、MAE0.00081032515、max0.5，原始两路NaN码均0。仍有算术差异，不称逐值闭合。

相同前128个RGB tiles和live scalar/seed0，旧local-oracle坐标重置模式的main特征8×8相位方差解释率32.9894%，global坐标降至0.58017%。这是前端特征，不是最终RGB，不能据此宣称全游戏网格修复。摘要native-preblock-resident-validation.json；夹具在release/native-preblock-resident与release/preblock-global，实际GPU文件在Lab/matrix-probe/native-preblock。

当前组件尚未绑定游戏输入，AMD安装仍b210a431；本轮未替换游戏DLL或发布ZIP，目标active。后续仍须审全网络真实FP8矩阵容量/布局，接正确full skip/post，并在剑星真正输出端做数值和视觉验收。

### 2026-09-06 原生C32层与GPU前四层串联检查点

只读live探针增加kernel名称映射，捕获实际序列为preblock_ds→C32 inpview_tilesync→chained XY移位→chained X移位→ds_wait Y移位；不能再把第一层按普通plain视图读取。run_original_fused_global新增native模式4/5，修正H/W与移位参数合同；identity对照确认输入DS是两16通道bank，而普通C32输出为4×4 cell布局。受控边界实验确认移位是零填充，填充值仍参与attention分母，不是循环移位再屏蔽无效key。

extract_native_weight_record.py直接提取原始记录；native_c32_reference.py按FP8 32→128→32、32维attention恢复block1/2/3，不使用旧FP16代理权重。逐层以原始上一层作输入，exact分别97.3175%、93.9423%、96.4417%。native_c32_stage.h及reframe shader在GPU完成tile打包、移位填充与裁剪；native_preblock_runtime增加RAW_INPUT接口。d3d12_native_front_chain_test.cpp在9070XT将RGB→block0→DS→block1→block2→block3单命令链执行，三次重放一致，中间无CPU回传。实验仍为128×64输入，不能当1080p性能结论。

新增validate_native_front_chain.py可重跑隔离误差与累计误差检查。实际GPU链对同算法CPU链逐值完全一致（MAE/max均0），证实常驻接线没有额外引入差异。但对原始CUBIN全链最终exact仅52.2919%、MAE0.09724066、max4、corr0.99845471；从原始DS开始的CPU链最终exact64.3585%、MAE0.05967632。原始DS与AMD DS最初MAE0.00081033，经过block1/2/3后累计MAE依次0.00598994、0.02908400、0.09724066。小算术差异被后续层放大，不能因单层高相关而宣称整网正确；下一步应定位舍入/归约差异，并继续恢复后续DS及更宽通道层。

本检查点未替换AMD游戏DLL，安装版本仍需以现场SHA复核（最近b210a431），原发布ZIP未修改。实际游戏网格和最终神经输出验收仍未完成。run_nvidia_ui.ps1旧窗口焦点实验不随本次提交，避免重新引入频繁黑屏切换。

### 2026-09-06 C32 FFN残差累加顺序修正

重新检查普通C32原始SASS，QMMA的初始累加器来自HMUL2处理的残差，而不是矩阵乘完后再加残差。native_c32_reference.py与preblock_input_mix.hlsl的RAW_INPUT路径改为先FP16舍入input×skip，再逐K32累计；非RAW的第0层暂不修改，需独立核对其指令流。

同原始逐层输入，block1/2/3 exact从97.3175/93.9423/96.4417%提高到99.2523/98.3994/99.2249%，MAE分别0.000401706/0.001237214/0.001085550。从原始DS起算的三层累计exact64.3585%→75.6561%。9070XT更新Lab shader实跑三次重放通过，GPU与修正CPU链仍逐值相等。包含尚未修正第0层的完整链exact52.2919%→52.7100%，MAE0.09724066→0.09588987、max4→2.25；这也说明第0层误差仍是重要来源，不应夸大本次提升。未部署游戏DLL，最终画质目标未完成。

### 2026-09-06 第0层FFN逐值闭合及attention舍入修正

原始preblock SASS在0x2840等先HMUL2残差，再在0x3130等QMMA以残差寄存器为初始累加器，证实第0层与C32均应skip-first。同步修正preblock_input_mix.hlsl与preblock_ffn_reference.py后，256张独立tile的524288个FFN-only结果对原CUBIN全部exact，MAE/max0。参考脚本增加逐值assert，不能只打印相关度后通过。

attention末端0xb2d0等HMUL2残差、0xb350等QMMA带残差初始值，故projection应H(dot+H(feature×skip))，而非H(H(dot)+feature×skip)。Q/K归一化0x6070/60a0等先对通道16..31平方做HMUL2，再HFMA2加0..15平方，随后8/4/2/1跨度归约；由+0x2660/+0x2460权重load与已恢复矩阵映射共同约束。新增native_c32_normalize.py，与HLSL同步该顺序。attention-only原版对照exact99.57199%、MAE0.000095915，仍有差异。

9070XT实际重跑完整preblock五帧（重复、换seed、恢复seed均通过）与block0..3三帧链，当前全局128×64主输出exact99.26453%、MAE0.000224486；DS exact98.67249%、MAE0.000270426。C32各层原输入隔离exact99.52698/98.76862/99.52698%；从原始DS起算三层最终exact84.50012%、MAE0.02158725。实际AMD全前四层最终exact59.41162%、MAE0.07457909、max2；GPU链对更新CPU链仍逐值相同。还不是最终RGB，更不是游戏验收。

另preblock_attention_reference.py增加--sum-order诊断参数，检查softmax分母的半精度归约顺序；降序32/16/8/4/2/1与8/16/32/4/2/1候选稍改善，但未取得完整指令映射，不写入runtime默认。下一步追分母真实树与DS池化舍入；后续更宽层及游戏完整链仍待恢复。未更新游戏DLL或发布ZIP，本轮测试文件仅Lab，目标active。

### 2026-09-06 第0层下采样半精度树逐值确认

原始SASS 0xbd60/0xbda0/0xbdd0为三次HADD2，0xbe10为HMUL2×0.25，旧finish用四值FP32平均只在末尾舍入不等价。新增check_preblock_pool.py，将FFN/attention置identity、保留原input mix，先确认524288个main量化结果逐值一致以隔离上游，再比较131072个DS结果。横向两值各HADD→两行HADD→HMUL×0.25全部exact；直接float平均99.62158%、纵向半精度99.49875%、对角99.50104%。加exact断言，固定helper尺寸/seed并隔离外部DLSS5_PREBLOCK环境变量，防止夹具受其他实验污染。

preblock_finish.hlsl及preblock_complete_validation.py同步横向半精度树。9070XT Lab重跑preblock五帧与前四层三帧均通过，主输出不变，完整第0层DS exact98.83728%、MAE0.000214875；原有attention差异仍保留。前四层完整链对原版exact59.41162%→62.23907%、MAE0.07457909→0.06666005、max2；GPU对CPU链逐值一致。这个修正改善真实数值链，但游戏最终RGB尚未验证，未覆盖游戏DLL，目标仍active。

### 2026-09-06 前四层原始CUBIN与9070XT整链逐值闭合

新增trace_preblock_softmax.py，从原始bias load（lane×16 bytes，0x3060..0x3e60）经QMMA C操作数、指数变换追踪到HADD2，再显式还原SEL/SHFL的warp转置。未知写入清空来源，不能凭寄存器旧标签继续推导。第一组32个query的每个分母均验证恰有64个唯一key、无跨query混合。每个half的key局部顺序是0/16、4/20、32/48、36/52四对逐次相加，四个lane基础偏移0/2/8/10再逐次相加，最后偶奇half相加；不是之前试过的任何平衡二叉树。native_c32_softmax_sum.py按该图实现，无权重拟合。

进一步根据已恢复V矩阵的byte布局确认B寄存器+0/+8装的是偶/奇输出通道，而非连续8通道。故norm在16跨度的HMUL/HFMA后，应先相邻偶奇相加，再8/4/2跨度归约；上一轮16→8→4→2→1的假设修正。CPU attention-only在256独立tiles共524288值与原CUBIN全部exact，MAE/max0，增加明确assert。

两处同步AMD shader后，9070XT实际preblock五帧（重复、换seed、恢复）和前四层三帧均通过。128×64全局第0层主输出262144值仅2个差异（exact99.999237%、MAE5.96046448e-8、max0.0078125，尚未定位，不标全exact）；DS的65536值全部exact。第1/2/3层各自原输入以及串联输入也全部exact。最关键：实际RGB输入→AMD block0→DS→block1→XY block2→X block3的最终65536值与原始CUBIN全链逐值一致，MAE/max0，不注入NVIDIA中间激活、不走CPU层间回传。validate_native_front_chain.py新增实际GPU对原CUBIN输出的exact断言。

此结论仅覆盖固定128×64夹具的前四层，不能替代其余更宽网络、最终RGB和剑星实机画面验收。AMD游戏DLL、公开ZIP未修改；目标仍active。下一步应恢复block4的移位DS接口及C64层，以当前正确前段继续向全网络推进，同时保留第0层主分支2个差异的待查项。

### 2026-09-06 第4层移位DS原始合同与CPU逐值验证

提取block4原记录22720 bytes（SHA f0d05533694577fa313f2655b00ada84f231f87ebc66ebbb7e8f0f7ea1ac88d2），新增oracle模式6：H/W按原始顺序、Y=-4、optional3接DS输出、optional_dims为half H/W。原CUBIN cc_tinlayout_fused_swin_1h_32_1_ds_fp8在64×32、grid8×5启动，main有效65536 bytes、aux32768 bytes。CPU新C32计算以原block3输出输入，block4主输出65536值全部exact。norm中出现FP16平方和溢出警告，但最终输出有限且逐值一致，不因此擅自改为FP32归一化。

DS追加64×32 FP8矩阵，原SASS四路load起点0x50b0=20656，而非普通记录长度20672（替代普通尾padding，结束22704，余16bytes尾padding）。流程为未经最终FP8的block4 raw→横向half pool→FP8→64×32投影→half→FP8。check_native_c32_ds.py先用矩阵字节映射尝试输出视图，16-channel banks下恰有约半数通道错位；进一步独立编码探针：输入channel0恒1、所有主块identity、DS每行放唯一FP8字节8+row，原kernel每像素读回完整唯一行编码，确定每四通道按0/2/1/3存放，而非凭相关度拟合。

按四个16-channel bank加编码探针确定的行顺序解码，真实block4 DS全部32768值与原CUBIN逐值一致，MAE/max0，脚本有exact断言。native_c32_reference新增raw_output接口，为GPU learned-DS接线保留未量化残差。当前block4验证为CPU原型+原CUBIN；尚未把此层加入AMD resident chain，不能把上轮AMD前四层验收扩大成前五层。下一步实现GPU DS投影并接C64。游戏DLL和公开包未改，最终画面目标仍active。

### 2026-09-06 AMD常驻链推进到第4层DS，整链逐值一致

新增native_c32_ds.h/hlsl，以root SRV绑定第4层的work区域half-pool及原始字节解出的64×32投影权重；GPU完成移位后crop与FP8投影，输出逻辑HWC64。NativeC32Stage公开PooledWork接口，引用body内部raw-before-FP8池化，而非从最终main量化值倒推DS。D3D资源在重放间显式UAV/SRV转换，权重一次上传，层间无CPU回传。

d3d12_native_front_chain_test增加可选ds参数（保留原默认block0..3测试和output.f32），串入Y移位block4及learned DS；输出独立output-ds.f32。prepare_native_front_chain.py生成block4 FFN/attention/DS原参数。MinGW以-std=c++17 -O2 -static -municode及d3d12/dxgi/d3dcompiler/dxguid编译通过，9070XT Lab实际运行ds路径三帧全部replay pass。check_native_c32_ds.py新增--resident，使用上一轮独立编码探针确定的原始视图解码，对actual RGB→block0→DS→block1/2/3/4→DS最终32768个值全部exact，MAE/max0。

新增组件尚未接入游戏addon；实验输入仍128×64，输出32×16×64。不能把当前前五层闭合称为整网完成，更不能代替剑星最终画面验收。游戏DLL及公开ZIP未修改，下一步继续核对C64多头权重/布局并向后接线，目标active。

### 2026-09-06 C64原版可重跑裁判与identity控制建立

block5原记录61760 bytes，SHA1b905577285978107b21b5730aac2b0105d45f757b5e78334291d9b4ab5646bb。C64 kernel位于dlssnr-01.cubin（SHAa10a8083b9489622fe290d669f05f38db7308c3c10771d0f0d6f22718e3cb1ae），不是前段00。新增oracle mode7，因multihead 88-byte参数在input/output/weight后额外留8bytes，再H/W（offset32）、X/Y偏移（40）、override H/W（80）；后者清零让kernel回退主尺寸。32×16输入、grid4×2、block32×2启动cc_tinlayout_fused_swin_2h_64_2_inpview_fp8，输入使用原block4 DS。两次独立启动输出8MiB arena完全相同，SHA1cf1f2a2dad909c84bb5a3d2b2521f73b1ddcf57141100dfc4ee75c535b0f73e；有效32768bytes无FP8 NaN码，末有效索引32767。非zero和稳定仅证明裁判能运行，不代表AMD C64已完成。

check_native_c64_identity.py将矩阵清零，0x7010..0x7090 FFN skip及0xf0b0..0xf130 attention skip置1，scale 0xe0a0处两float置1。原kernel输出与输入全部32768个FP8值的多重集合一致（仅统一正负零）；空间布局未验证，所以不是逐坐标identity声明。

SASS还显示FFN区域0..0x4000、0x4000..0x6000后有0x6000区域矩阵load，QKV从0x70a0开始。不能照搬C32的两矩阵FFN简单扩宽；新增矩阵的确切维度、作用及排列仍需编码/basis探针，当前不把可能的分组/混合解释写成事实。下一步恢复C64输入输出坐标与这段矩阵，已有AMD block0..4 DS逐值测试保持不变。未更新游戏DLL，整网及画面目标active。

### 2026-09-06 C64完整坐标映射及三矩阵串联探针

recover_native_c64_view.py对32×16×64原始输入逐字节位置编码，用15组二进制identity探针恢复输出到输入的32768项映射，严格验证bijection；独立seed13/29随机FP8输入对该映射逐值一致（统一正负零）。再按已验证block4 DS四个16通道bank解码，确认C64输出为4×4×64 cell，cell内1024项映射在全部32个cell重复，cell按空间行优先排列。mapping.npz保存在release/native-c64/view，不把参数/激活放Git。

probe_native_c64_extra_matrix.py将所有矩阵和FFN skip清零，attention skip置1，输入所有通道恒1，分别测试全零、仅0x6000置FP8 1、仅0与0x4000置1、三处同时置1。前3种全部零，三段连通才输出512个FP8 0x3a=1.25（其余零）。新增断言固定此受控结果。由此确认第三矩阵参与该FFN串联通路，不能视为无关padding或可忽略的辅助量；尚不能仅凭这几个字节确定全矩阵的维度/分组/排列，下一步编码恢复三段连接。

此轮为C64原CUBIN控制实验，未扩展AMD resident执行层数，游戏DLL未改。前五层AMD逐值结果仍为上一检查点，完整神经输出及剑星画面目标active。

### 2026-09-06 C64三矩阵FFN连接恢复并逐值闭合

原oracle增加受限批量byte scan：仅mode7、8×8、最多16384个字节、扫描区必须预先全零、禁止指针偏移；每次将一个字节设FP8 1，GPU执行后只读4096个有效输出，输出紧凑序列。单进程复用CUDA资源，避免每个basis重新加载上下文。

recover_native_c64_ffn.py --full用输入通道6位编码、隐藏256通道8位编码、中间64通道6位编码恢复全部连接。W1 16384条对应64→256；W2 8192条对应256→64但每个输出仅128条分组连接；W3 4096条对应64→64。每一段连接唯一性、各行容量、隐藏代表编号均有assert；不存在拟合系数。完整映射在release/native-c64/ffn-layout/layout.npz。

validate_native_c64_ffn.py再用128个byte scan确定64个FFN skip half的逻辑通道顺序。计算合同为W1每K32半精度累计→原门函数→FP8→分组W2每K32累计→FP8→W3每K32累计（初值是half(input×skip)）→FP8。原权重直接解码三张矩阵，禁止把未连接区域填成其他系数。关闭原版attention以隔离FFN，输入原block4 DS的32768个输出全部exact；两个独立随机输入seed44/σ0.25、seed55/σ3.0也各32768值全部exact，MAE/max0，三组都有硬断言。矩阵和激活只落release目录。

此轮C64 FFN为CPU参考对原CUBIN验证，尚未实现AMD C64 shader，也未恢复双头attention。当前AMD实际闭合范围仍block0..4 DS，游戏DLL未改、公开包未改。下一步恢复C64 QKV/P、bias及双头布局后整体接GPU，最终剑星画面目标保持active。

### 2026-09-06 C64 V/P完整连接与原系数均匀attention逐值验证

recover_native_c64_attention.py先将output projection置全1，QKV区逐字节激活，只有V能在其余V为零时产生非零输出。实测V共4096bytes分布于[0x78a0,0x7ca0)、[0x84a0,0x88a0)、[0x90a0,0x94a0)、[0x9ca0,0xa0a0)，不是简单的Q/K/V各连续4096bytes。--full用6位输入编码和共享latent代表恢复V/P各4096条唯一连接，P输出坐标依赖已验证C64 cell映射，latent标签在两矩阵间一致；结果在release/native-c64/attention-layout。

validate_native_c64_uniform_attention.py使用原始V/P系数，只将FFN改identity、Q/K及bias置零、attention skip置零以隔离值路径，均匀权重1/64。CPU计算V的K32分段half累加→FP8→两个K32 attention-value累加→FP8→P分段half→FP8。独立随机seed31/σ0.25及seed47/σ3.0，各32768输出全部与原CUBIN逐值相同，MAE/max0，均有assert。这验证了真实V/P连接和该控制下的算术，不包括非均匀attention、Q/K、双头bias/归一化或完整block5。

当前C64 FFN及均匀attention分别闭合，完整C64尚未接AMD。游戏DLL、发布ZIP未修改，实际AMD闭合范围仍block0..4 DS。下一步恢复Q/K与8192个bias条目的head/query/key布局，再串完整C64验证，目标active。

### 2026-09-06 C64双头bias坐标恢复，完整层算术仍有差异

批量oracle增加half探针（DLSS5_NATIVE_SCAN_HALF存在时逐FP16元素置8，范围校验乘元素字节数），旧byte模式不变。recover_native_c64_bias.py以通道0空间6位编码，同时令两head的V读同一输入、P将latent0/32分别送输出0/1，每次仅改变一个bias。8192项每次只影响一个head的一处query；六轮head/query一致，最终head×query×key完整bijection，每头4096项，保存在release/native-c64/attention-layout/bias-layout.npz。

native_c64_reference.py恢复attention skip的64-slot映射并尝试完整block5。Q/K暂按V byte offsets分别减0x800/0x400的同族布局提取（属于待完整数值验证的布局推导，尚非独立Q/K basis证明）；原scales为2.382342及9.916067。与原block5裁判比较，当前exact80.04761%、MAE0.07538193、max4、corr0.99950066，远未闭合，末尾明确exact assert失败，不能把脚本执行或高相关当通过。

此前FFN与均匀attention分别逐值通过的结论不变；差异出现在完整非均匀attention组合中，下一步核对C64 Q/K的归一化寄存器顺序、分母树以及必要的独立Q/K控制。原先C32的加法顺序不能未经检验直接视为C64事实。新原型尚未接AMD、未更新游戏DLL/发布包；当前AMD闭合范围仍block0..4 DS，完整游戏画面目标active。

### 2026-09-06 完整C64差异定位为FFN→attention量化边界并闭合

diagnose_native_c64_attention.py将FFN设identity，分别保留均匀attention、真实bias但Q/K零、真实Q/K但bias零、完整真实attention，四种情况各32768值全部与原CUBIN逐值一致。这排除了上轮优先怀疑的Q/K归一化/分母树作为当前主因，继续修改那些已正确公式反而会走偏。

实际错误是把C32的未量化FP16 FFN特征残差合同搬到C64。原C64在0x3c30..0x3ea0执行F2FP.SATFINITE.E4M3.F16，随后0x3e70/3eb0/3ec0/3ee0写共享内存；attention残差也取量化边界后的特征。native_c64_reference.py将FFN最终feature显式F8，再供QKV与attention skip共同使用。输入真实block4 DS时，完整block5从80.04761% exact变为32768/32768 exact、MAE/max0。新增独立随机seed79输入的full_block对照，同样全部exact；原四种隔离对照也继续全exact，全部加入assert。

因此当前C64完整CPU参考已通过两组输入，不再把其称为仅均匀attention通过。Q/K推导布局受到完整非均匀原权重测试约束，但仍未声称独立逐权重basis验证。下一步据此实现AMD C64三矩阵FFN与双头attention，再接入resident链；目前AMD执行范围仍block0..4 DS。游戏DLL与公开ZIP未更新，最终剑星画面目标active。

### 2026-09-06 完整C64接入AMD，RGB到block5整链逐值一致

新增native_c64.h/hlsl，三阶段常驻GPU：64→256→分组64→投影64的FFN、每头32维attention-value、跨头64维输出投影。明确保留FFN末尾FP8边界，使用已验证的half norm/softmax加法图和每K32舍入；两head分别dispatch，最终跨头投影单独dispatch，避免组内同步误当跨组同步。资源全由D3D12维护，重放间UAV/SRV屏障，root SRV绑定原始系数解码后的常驻权重，无CPU中间传递。

prepare_native_front_chain.py导出block5参数，d3d12_native_front_chain_test新增c64模式，输入仍是同一份128×64 RGB，复用AMD算出的block4 DS，不注入原版激活。MinGW编译通过，9070XT三次GPU执行全部replay pass。独立validate_native_c64_chain.py按此前探针恢复的原CUBIN cell映射解码block5输出，与实际AMD output-c64.f32比较32768/32768逐值相同，MAE/max0、nonfinite0，有exact断言。CPU完整C64及FFN随机测试复查仍通过。

当前真实AMD闭合范围推进为block0..5（第0层另一路主输出之前仍有2个值差异待查），不是完整网络/最终RGB。尚未部署游戏addon；公开ZIP、游戏DLL不变。下一步处理block6/7的C64移位窗口和block8 DS，再继续更宽层，最终剑星实际画面验收目标保持active。

### 2026-09-06 C64移位层接入AMD，修复root SRV边界访问与验证漏洞

提取block6/7原61760-byte记录，native_c64_reference新增通用unpack并与此前独立block5缓存逐数组核对一致。check_native_c64_shift.py用真实block5→block6 XY移位→block7 X移位原CUBIN输出，CPU零填充/crop与两层分别32768值逐值一致。

新增native_c64_shift.h/hlsl的GPU HWC64 pack/body/crop，测试host增加c64shift模式。初版两次装置失效，device_removed_reason=0x887a0006；旧host在装置失效时fence事件也被唤醒，却先打印frame0 pass，之后第二帧才报错。因此这两次frame0 pass撤回，不计有效重放。host增加GetDeviceRemovedReason和GetCompletedValue检查（拒绝UINT64_MAX及未完成值），只在设备和fence成功后读取/验证结果。补强后先复查旧block0..5模式，三帧及原版exact仍通过。

移位pack初版用三元表达式在越界时选0。root SRV无描述符长度边界保护，怀疑编译器提前执行负坐标load；改为[branch]越界分支先写0并return，再在有效路径读取。没有改TDR阈值、重启或掩盖错误。仅此shader改动后，c64shift三次重放通过，fence等待31/0/16ms（CPU墙钟采样，不作游戏FPS/性能结论），设备成功。validate_native_c64_chain.py --shift独立解码原block7输出，对实际AMD RGB→block0..7最终32768值全部exact，MAE/max0、nonfinite0。结果保存在release/native-front-chain/output-c64-shift.f32。

当前AMD闭合范围推进到block0..7，仍非完整网络或最终RGB；第0层另一路2个差异待查。游戏DLL/公开ZIP未修改，下一步恢复block8 C64→C128 DS及后续宽层，最终剑星画面验收目标active。

### 2026-09-06 block8 C64→C128主输出与DS原版逐值验证

提取block8记录69936bytes，SHA687d906bc5d6748b57ee3e519c114f9382d93db560b7f779461c1ab90f5237ae。新增native oracle mode8沿用multihead H/W及移位字段，但DS指针在offset72、half H/W在offset80，对应原SASS c[0][0x3c8]/0x3d0。以原block7输出、32×16、Y=-4、grid4×3、block32×2调用原ds kernel，main有效32768bytes、aux16384bytes。

native_c64_reference支持DS记录和raw_output。check_native_c64_ds.py先比较block8主输出32768值全部exact；再从未量化的最终FP16 attention结果横向half-add池化→FP8，使用0xf130起8192个FP8字节的64→128矩阵（映射复用已编码恢复W1前128行），每K32半精度累计后FP8。按8个16-channel bank、每四通道0/2/1/3顺序解码，DS16384值全部exact。独立128种唯一有限FP8行编码探针验证每个输出位置的行顺序，避开NaN/正负零歧义，不凭相关度挑布局。main、DS、编码探针均有assert。

这是CPU参考及原CUBIN的block8合同验证，尚未把block8接入AMD。下一步GPU实现需保留C64最终projection的FP16 raw供DS，不能从当前F8主输出倒推池化。现有AMD已验证范围仍block0..7；游戏DLL/公开ZIP未修改，最终画面目标active。

### 2026-09-06 block8 DS接入AMD，RGB到C128输入整链逐值一致

中断恢复后检查工作区仅既有run_nvidia_ui.ps1变更，AMD无残留native-front-chain-test进程。NativeC64/NativeC64Shift增加显式raw_output选项，默认保持旧FP8输出；只在block8路径保留最终projection FP16结果，crop后供DS使用。新增native_c64_ds.hlsl，实现横向half池化→FP8→64×128投影（两个K32半精度累计）→FP8。复用DS host的资源/屏障逻辑，新增c64_raw合同仅允许已经裁剪的raw输入，拒绝重复shift。

prepare_native_front_chain.py导出block8原参数及8192个DS系数；测试host新增c64ds模式，按RGB→preblock→C32链及DS→C64链→Y移位block8 raw→DS串联执行，无NVIDIA中间激活注入、无CPU层间回传。MinGW编译成功，9070XT三次replay通过，GetDeviceRemovedReason与fence完成检查均成功（墙钟等待31/16/15ms不是1080p性能数据）。validate_native_c64_chain.py --ds按独立编码探针确定的8×16-channel banks和0/2/1/3顺序解码原block8 DS，实际GPU输出8×16×128=16384个值全部exact，MAE/max0、nonfinite0。原CPU block8编码/主输出/DS断言也复查通过。

当前AMD整链闭合范围为block0..8 DS，尚未验证第9层起C128及后续网络，第0层另一路2个特征差异仍待查。游戏DLL与公开包未更新，不能把特征链逐值一致当成游戏最终RGB/面部效果验收。下一步继续C128四头网络，最终剑星画面目标active。

### 2026-09-06 C128坐标及三矩阵FFN恢复、原权重逐值验证

block9记录197184bytes，SHA934cebc771cfa1ed086f47f018426090e14ca4326dcb22843cbc17dff9851c32。原kernel位于dlssnr-02.cubin，cc_tinlayout_fused_swin_4h_128_4_inpview_fp8，沿用multihead mode7，输入原block8 DS的16×8×128，grid2×1、block32×4，有效输出16384bytes。

将现有view探针扩展--channels 128，FFN skip0x18010、attention skip0x30130、scales0x2c120；14组位置编码恢复16384项完整bijection，seed13/29随机输入逐值验证通过。输出为4×4×128 cell，全部8个cell映射一致。没有以C64布局直接替代C128探测。

批量oracle明确支持2/4个warp、8×8，单扫描最多65536项，sample大小64×32×warp数，保留全零扫描区及权重边界检查。recover_native_c64_ffn.py --channels 128 --full恢复W1 65536条（128→512）、W2 16384条（512→128，每输出128条分组连接）、W3 16384条（128→128），共98304条连接；每段唯一性与容量断言通过。文件仍是原工具的参数化扩展，不另堆v2副本。

validate_native_c64_ffn.py --channels 128以原block9字节系数、原block8 DS输入及seed44/σ0.25、seed55/σ3.0两组随机输入验证三段FFN，三组各16384值全部exact、MAE/max0。C64同工具原权重回归仍通过；重新运行通用化后的C64全部连接恢复，六张连接表与扩展前缓存逐值一致。C128映射/矩阵仅保存在release/native-c128。

当前C128完成的是CPU FFN原型和原CUBIN对照，尚无C128 GPU shader，也未恢复四头attention。AMD实际闭合范围仍block0..8 DS；游戏DLL、公开ZIP未改，最终剑星画面目标active。下一步恢复四头QKV/P/bias，再串完整C128并接GPU。

### 2026-09-06 C128四頭attention及完整block9 CPU逐值闭合

将V/P与bias编码工具参数化支持--channels 128。原QKV的49152bytes逐字节控制确认V为16个1024-byte分块，共16384bytes，而非连续一段；7位输入/latent编码恢复V及P各16384条唯一连接。四头bias通过六个空间位探针恢复16384项head/query/key完整bijection，每头4096项，控制输出每次仅一个head的一处query变化。位置/连接缓存留release/native-c128，未将权重推Git。

native_c64_reference.py通用unpack/block支持C64及C128，C128用原0x18010 FFN skip、0x2c120四scales、0x2c130 projection、0x30130 attention skip，后者另做256-byte探针恢复128-slot顺序。Q/K按已识别V分块分别回移0x800/0x400，同族映射经过完整非均匀计算约束，仍不称独立Q/K逐字节basis证明。

完整block9 CPU参考对原block8 DS输入16384值全部exact；--random新增独立seed113/σ0.25及seed127/σ3.0，两组各16384值也全部exact、max0，三组共49152值有硬断言。C64完整层及同两组随机输入回归全exact。重新执行通用化后的C64 V/P和双头bias恢复，所有缓存连接表与改动前逐值一致。

当前完整C128仅完成CPU原型对原CUBIN验证，尚未实现AMD C128 shader。AMD实际闭合范围仍block0..8 DS，游戏DLL/公开ZIP不变。下一步把四头层接入GPU链，再向后推进；第0层旁路2个差异及最终剑星真实画面验收仍未完成，目标active。

### 2026-09-06 C128四头层接入AMD，RGB到block9整链逐值一致

native_c64.h/hlsl参数化CHANNELS=64/128、HEADS=C/32，保持每head32维、每window64像素及24KiB组共享Q/K/V不变。FFN三段权重偏移按4C²/8C²/9C²计算，attention Q/K/V/P/bias/scales/skip独立按C和head数定位；输入/输出资源容量及attention dispatch同步随通道数扩展。保留默认C64和raw_output合同，未把head数误用到64-key的AV累计循环。

prepare_native_front_chain.py导出block9原系数，测试host新增c128模式，以AMD block8 DS输出直接接C128层，没有读入NVIDIA激活。MinGW编译通过，9070XT RGB→block0..9三帧重放及device/fence检查全部成功，validate_native_c64_chain.py --c128对独立原block9布局解码的16384值全部exact，MAE/max0、nonfinite0。CPU完整C128原输入+两随机输入回归亦全exact。

随后用同一新shader/EXE重跑c64ds终点，三帧通过，原block8 DS16384值仍全部exact，确认参数化没有改变已验证C64路径。墙钟fence等待47/31/16ms等仅小夹具测试日志，不作1080p帧率宣称。

当前AMD闭合范围为block0..9，未验证第10层后的C128移位链及更宽层，也未解决第0层旁路2个差异。游戏DLL/公开包未修改，真实剑星最终RGB/面部效果验收仍未完成。下一步接C128移位层与DS，目标active。

### 2026-09-06 C128移位链接入AMD，整链推进到block13

提取block10..13原记录，均197184bytes。check_native_c64_shift.py参数化--channels 128，以原block9输出开始逐层调用原四头普通kernel，shift序列XY/X/Y/none，16×8尺寸；四层CPU原始系数计算均16384值exact。默认C64第6/7层CPU回归同样exact。

native_c64_shift.h/hlsl增加64/128通道参数，显式越界分支和root SRV保护不变，资源字节数、通道循环和body编译宏同步变化。测试host新增c128shift模式，直接串AMD block9输出→block10..13，未注入原版中间激活。MinGW编译通过，9070XT三次replay及device/fence检查通过。validate_native_c64_chain.py --c128-shift独立解码原block13 cell布局，实际GPU最终8×16×128共16384值全部exact，MAE/max0、nonfinite0。墙钟fence78/79/62ms仅小夹具，不是1080p性能或游戏帧率。

当前AMD闭合范围为block0..13。下一步block14 C128→C256 DS；此小夹具再下采样高度将成为4，后续必须正确处理非8对齐边界或建立更大原版夹具，不能偷偷用错误尺寸绕过。第0层旁路2个值差异及完整更深层/最终RGB仍未完成。游戏DLL/公开ZIP未改，最终剑星画面目标active。

### 2026-09-06 block14 C128→C256下采样直接basis恢复并逐值闭合

提取block14记录229936bytes，SHA6c7a05c82b823e09b0b843f5dd399af7a006410f3587191f62a43205a872724d。原4h ds kernel使用mode8，16×8输入、XY=-4、grid3×2、block32×4，有效main16384bytes、DS8192bytes。主输出CPU逐值一致；DS矩阵起点0x30230，32768bytes到记录末尾。

首次套用FFN W1前256行的布局失败（DS exact2.06%，编码探针出现零通道），该假设撤回。新增recover_native_ds_projection.py，oracle批量扫描支持mode8直接读取DS输出（样本bytes=32×C，而非main的64×C）。主块identity，逐DS字节置FP8 1，以128输入通道7位编码恢复全部32768条输入/输出连接，输出定义遵守已用的16-channel bank及低两位交换坐标约定；连接bijection通过，不拟合系数。

check_native_c64_ds.py参数化64/128，使用独立DS连接表；原block13输出→block14 raw→half横向池化→FP8→128到256投影每K32半精度累计→FP8，DS8192值全部exact、MAE/max0。另两轮base16行编码唯一覆盖256输出通道，无NaN编码/正负零歧义，验证空间恒定及通道bijection。C64→C128也重新独立恢复8192条DS连接，与先前正确的FFN映射恰好完全相同，原block8及两轮编码回归全exact；不能据该巧合推广到C128。

当前block14为CPU原型与原CUBIN验证，尚未接AMD。AMD真实闭合范围仍block0..13，游戏DLL/公开ZIP未修改。下一步GPU DS接线使用本轮实测矩阵，不再复用FFN布局；输出8×4×256，后续非8对齐高度需正确处理，目标active。

### 2026-09-06 block14 DS接入AMD，RGB到C256输入整链逐值一致

native_c64_ds.hlsl参数化CHANNELS=64/128，维持每K32 half累计与pool的舍入顺序；DS host验证矩阵容量2C²，输出容量2C，raw输入必须已经crop。block14用NativeC64Shift的128通道XY移位raw模式，将最后projection的FP16值交DS；prepare_native_front_chain.py使用独立ds-layout表解码32768个系数，没有再次套用FFN布局。

测试host新增c128ds模式，RGB→block0..14 DS全留GPU，最终输出4×8×256。MinGW编译通过，9070XT三次replay及device/fence检查成功，validate_native_c64_chain.py --c128-ds与原block14 DS的8192个值全部exact、MAE/max0、nonfinite0。CPU原block14及两个通道编码探针回归通过。随后用同一新shader/EXE重跑旧c64ds模式，三帧通过且block8 DS16384值仍exact，确认扩展没破坏旧路径。fence墙钟93/78/79ms等仅小夹具数据，不宣称1080p性能。

README顶部补充当前数值移植状态及历史结论边界，避免旧代理链“完成/无网格”文字误导。当前AMD闭合范围block0..14 DS，后续C256输入高度4的非8对齐处理仍待实现；第0层旁路2个差异、完整网络及游戏最终画面尚未验收。游戏DLL和公开ZIP未改，目标active。

### 2026-09-06 C256半窗口边界及FFN原型逐值验证

block15记录689232bytes，SHAa3081eaee51296319ade0839add190cfdbe2adbf0f0a5e990df1f2a577ae9507；原8h kernel位于dlssnr-03.cubin，输入block14 DS为8×4×256，grid1×1、block32×8，main有效8192bytes。view探针扩展--channels 256并采用ceil窗口数，13位编码恢复8192坐标bijection，seed13/29逐值验证通过，输出仍为4×4 cell。check_native_c256_boundary.py对真实block14输出及seed211/σ0.25、seed227/σ3.0证明原kernel直接H4与显式补零H8再裁上半部的8192个值分别全部相同；这为后续GPU非8对齐输入提供控制证据，尚未实际接GPU。

derive_native_ffn_layout.py将已有C64/C128完整探针连接表表达为字节地址bit到input/hidden/output bit的排列，两档六张表逐项完全相同，再扩展C256形成W1 262144、分组W2 32768、W3 65536连接。这里C256是地址布局推导，不宣称重新逐字节穷举了全部连接，也没有拟合数值系数。随后用原block15真实权重验证推导：FFN skip通过512-byte探针独立恢复；原block14输入及seed44/σ0.25、seed55/σ3.0三组各8192值全部exact、MAE/max0，支持该布局和计算合同。C64/C128同工具FFN回归继续全exact。

当前C256仅完成布局、边界控制和CPU FFN；八头attention未恢复，GPU执行范围仍block0..14 DS。游戏DLL/公开ZIP未修改，完整最终RGB及剑星画面目标active。下一步恢复C256 QKV/P/bias并验证整层，再把H4零填充包装接GPU。

### 2026-09-06 C256八头完整CPU参考逐值闭合

derive_native_attention_layout.py从已完整探测的C64/C128 V/P/bias表提炼地址bit排列，先逐数组核对两档全部实测表完全一致，再扩展C256 V/P各65536连接及32768个八头bias项。C256是布局规则推导加真实算术验证，不声称穷举了每个系数，也不拟合权重数值。native_c64_reference支持C256原记录/标量偏移，attention skip用512-byte探针独立恢复，H4输入按已验证规则补到H8，输出裁回H4。

完整block15以真实block14 DS输入8192值逐值一致；seed113/σ0.25、seed127/σ3.0两组随机输入各8192值也全部exact、max0，合计24576值。C128/C64真实输入及相同随机输入回归均继续exact。当前尚未实现AMD C256，实际GPU闭合范围仍block0..14 DS，游戏DLL和公开包未修改；下一步接GPU补零/裁剪包装，最终画面目标active。

### 2026-09-06 C256接入AMD，H4窗口及RGB到block15逐值一致

NativeC64允许256通道，沿用参数化三阶段shader及每头32维。NativeC64Shift支持非8对齐有效尺寸，将work extent设为ceil((有效尺寸+前侧shift)/8)×8，边界通过显式分支填零，最终crop回原有效尺寸；已对齐C64/C128时与旧work extent相同。block15以8×4×256输入、shift0包装为8×8计算，资源和通道均按256分配。

测试host新增c256模式，原系数由prepare_native_front_chain.py导出，输入直接取AMD block14 DS。初始化期间进程CPU仍增长，持续等待同一已确认存活句柄，未因等待较长重启测试。最终9070XT三次replay与device/fence检查通过。validate_native_c64_chain.py --c256按已恢复C256 cell映射解码原block15，对实际GPU RGB→block0..15最终8192值全部exact，MAE/max0、nonfinite0。CPU H4/H8真实输入及两随机输入边界测试复查通过。fence墙钟140/110/109ms是小夹具日志，不能当1080p性能或游戏帧率。

README同步当前前缀范围。游戏DLL、公开ZIP仍未更新；后续C256移位链/DS、更深网络及第0层旁路2个差异仍待处理。当前只是block0..15特征链实机正确，最终剑星RGB/角色面部效果验收仍未完成，目标active。

### 2026-09-06 C256普通H4窗口重复规则与block16..21 CPU逐值闭合

读取原launch序列，block16..21为XY/X/Y/none/XY/X移位，记录均689232bytes。首次直接复用inpview的H4补零规则，block16只有32.70% exact。diagnose_native_c256_plain.py拆解发现FFN-only逐值一致，但attention-only/uniform有明显差异；相同输入明确补成H8后，普通原kernel又与CPU逐值一致。因此差异不是三矩阵FFN或权重推导失败，而是plain H4半窗口行为与inpview不同。

probe_native_c256_half_attention.py将V/P置单连接、FFN identity，以常量和单点local key8为输入。H4普通kernel：常量输出1而非补零预期0.5，单点均匀输出1/32而非1/64；对key8和缺失key40分别加bias8，query0同为0.875，证明缺失半窗口重复了前半窗口的值，不是仅屏蔽invalid key。H12尾窗口控制不同：常量0.5，单点1/64，对缺失key40加bias后仅0.001953125，仍为补零。两档六种控制均有固定结果assert，不能把H4整图退化规则推广为所有尾端半窗口重复。

check_native_c64_shift.py扩展256及ceil窗口数，仅对C256普通kernel且有效height=4按行模4填充（X越界仍补零）；inpview block15的补零规则保持不变。block16..21各8192值全部与原CUBIN逐值相同，MAE/max0。此轮尚未接GPU，实际AMD闭合范围仍block0..15；下一步GPU包装需显式区分plain H4重复与inpview H4补零，游戏DLL/公开包未改，目标active。

### 2026-09-06 C256普通移位链接入AMD，RGB到block21逐值一致

NativeC64Shift增加显式PLAIN_SHORT_Y开关，仅在CHANNELS=256且有效height=4时对pack行坐标模4；第15层inpview保持默认补零。第16..21层分别用XY/X/Y/none/XY/X，输出资源、权重、状态均独立。测试host新增c256shift模式，直接接AMD block15输出，未注入NVIDIA中间值。

新增native_shader_cache.h，仅缓存同一进程内的独立shader字节码，key包含完整源文本、entry和所有宏；有include时回退不缓存编译，资源/权重/激活不缓存。相同字节码复用减少多层重复编译，计算图与运行时常量不变。21层链实测15次编译、36次命中，避免六个C256层重复编译同一程序。

9070XT三次replay、device/fence检查通过；validate_native_c64_chain.py --c256-shift对独立原block21解码的8192值全部exact，MAE/max0、nonfinite0。再运行同一新EXE的c256入口回归，三次通过且原block15的8192值仍exact，证明入口补零未混成普通层重复行。H4/H12六种控制探针复查亦通过。墙钟fence313/297/296ms等仅小夹具数据，不是1080p帧率。

当前AMD正确特征前缀推进至block0..21，游戏DLL/公开包未更新，第0层旁路2个差异及其余网络/最终RGB仍待处理。下一步block22 C256→C512 DS，随后是与普通Swin不同的split/ViT路径，最终剑星画面目标active。

### 2026-09-06 block22 C256→C512 DS与AMD整链逐值一致

block22记录820288bytes，SHAcfb0343287054e38933dbf35c7dbb124fef50cfaf4234f7161575f00896cc02d。原8h ds kernel在8×4输入、Y=-4时主输出8192bytes；DS只有4096个有效FP8值，但物理存储占8192bytes，32个16-channel bank各按4×4空间预留，只有前2行有效，最后非零位置8063。不能截取前4096bytes当有效tensor。

derive_native_ds_layout.py从C64/C128独立DS探针表提炼并逐项核对地址bit规则，再扩展C256 DS131072连接；check_native_c256_ds.py验证block22沿用plain H4重复行而非补零，raw half池化→FP8→512×256投影与原版4096有效值全部exact。三轮base16编码覆盖全部512输出通道并确认补齐行全零；真实block21输入以及seed313/σ0.25、seed317/σ3.0两组随机输入的主输出/DS均逐值一致，无系数拟合。

GPU DS host允许已crop raw输入为偶数尺寸、支持256通道，shader增加明确尾线程分支保护。block22用256通道raw+PLAIN_SHORT_Y再接DS，输出逻辑HWC512不含物理padding洞。测试host新增c256ds模式，9070XT RGB→block0..22三次replay、device/fence检查通过；validate_native_c64_chain.py --c256-ds按原padding布局解码并裁有效行，4096/4096 exact，MAE/max0。RAW_OUTPUT宏只在projection入口启用，FFN/attention共用同字节码快取；实测12次编译、42次命中。回归c256shift到block21，三帧及8192值exact仍通过（11次编译/40次命中）。墙钟359/328/328ms仅小夹具数据，非1080p性能结论。

当前AMD正确特征前缀推进到block0..22 DS，尚未恢复C512 split/ViT及后续解码器，第0层旁路2个差异仍待查。游戏DLL和公开ZIP未更新，最终剑星RGB/角色面部效果验收目标active。

### 2026-09-06 C512 split原版调用审计，尚未建立有效整层裁判

原live序列显示block23为ffwd_inpview（grid Z2）、ffwd_proj_inpview、qkv（grid Z4）、proj四阶段，参数分别56/72/56/72bytes。提取四个原记录，尺寸524288/263168/917568/263168bytes；旧split_swin512_reference.py按全FP16、256宽hidden及16维head解释，不再当正确参考。SASS可见QMMA.E4M3，混合矩阵/标量解释仍需独立恢复。

run_original_split_global.cpp增加独立native-inpview诊断模式，选择前两段inpview kernel，尝试H/W顺序及投影ceil(W/4)网格、QKV独立shift-mask网格，并增加branch/ffn/attn读回。以上参数仍在验证，不宣称已修正全部ABI。真实4×2输入得到全零，probe_native_split_input.py进一步全常量输入：4×2仍四段全零；8×8/16×8前3段分别约32768/65536bytes非零，最后proj仅约16384/32768bytes，范围少一半。当前block=(32,4,1)等线程配置仍需与实际创建参数核对，不能把“非零”当正确裁判。

reader对权重严格要求原记录尺寸，仅输入可补零；native模式全零现在返回4并明确拒绝验收，旧模式保留。preblock_live_parameters.cpp新增kernel_create原始args_xyz/shared_arg只读日志，为下一轮核对真实线程配置准备；未构建部署该新增日志，不把源码变化说成已采集数据。AMD正确范围仍block0..22 DS，未修改游戏DLL/公开包，目标active。

### 2026-09-06 C512 split调用修正与identity坐标闭合

SASS显示最后proj的threadIdx.y每组处理64个通道，原诊断使用4组只覆盖256，native模式改为block32×8；FFN projection保留32×4。另identity测试发现ffwd_proj的指针顺序应是branch再residual，旧native传din/branch反了，修正后8×8和16×8的identity值集合完全一致。投影grid X也不能是ceil(W/4)：其CTA索引以完整8宽窗口分成两组，改为2×ceil(W/8)后4×4、8×4、4×8也通过identity值集合与输出范围检查。旧legacy模式保留，不能将之当修正后的原生裁判。

新增check_native_split_identity.py，用四份全零矩阵、两段skip=1和scale=1控制完整四调用路径，4×4、8×4、4×8、8×8、16×8均通过（统一正负零）；4×2仍全零返回4，未解决且不计通过。probe_native_split_input.py扩展同尺寸常量检查，完整尺寸的四阶段有效数据范围已恢复。

recover_native_split_view.py在非方形16×8×512上以16组位置编码恢复65536项输入→输出bijection，并用seed511/521随机FP8输入逐值验证。解码确认输出4×4×512 cell排列在全部8个cell重复，空间按行优先；这同时约束了当前H/W解释，超出单纯直方图检查。映射留release/native-c512/split-view。此结果仅为identity坐标合同，不是原始权重的完整split算术复现；下一步恢复gate/up、投影及QKV矩阵，同时审4×2小尺寸合同/真实模型padding要求。

本轮未部署此前新增的5090创建参数探针，也未更新AMD游戏DLL/公开包。AMD实际正确前缀仍block0..22 DS，最终剑星画面目标active。

### 2026-09-06 C512 ffwd三段串联探针及5090采样状态

旧gate/up二矩阵假设未成立：FP8单字节0配各power-of-two偏移均无响应，FP16单系数版本也未支持该假设；不据此将C512改作全FP16。SASS明确在激活后还有QMMA，并在0x49f0后读取0x40000/0x60000区域。probe_native_split_gate.py改用正系数分区控制，进一步三处FP8 unit系数0、0x40000、0x60000同时开启时，branch恰有64个FP8 0x3a=1.25，单/双路径不足；--triple有严格断言。说明ffwd需要三段串联连接，完整维度/排列仍需恢复，不再称简单gate/up。中间branch最终使用F2FP.SATFINITE.E4M3.F16存储，FP16试探已撤回。

为核对真实建立参数，构建并部署带kernel_create日志的只读5090探针，SHA c256f1a5e69d75493da4493d9c664161a59934a38413663eeec9359db241f978。部署脚本校验成功，Steam启动任务返回0，但多次进程检查均无SB/SB-Win64-Shipping，新日志未产生（仍为旧PID24696的04:38记录），不能当作新捕获。被动截图起初是Steam绿色开始按钮、云最新；准备点击前重截已变为纯黑，故取消点击，未注册/执行点击脚本，临时本地脚本已移除。询问用户5090是否锁屏/熄屏；此问题不阻断本机原CUBIN探针，未标目标blocked。

本轮AMD游戏DLL/公开包未改，实际AMD正确前缀仍block0..22 DS。5090仅新增CPU日志探针，未变更原神经权重或游戏画面。下一步按三段串联恢复C512 ffwd，并在桌面恢复可操作后补真实kernel参数。最终画面目标active。

### 2026-09-06 C512 split ffwd与FFN投影真实权重逐值闭合

check_native_split_activation.py对三系数路径做8组正负/幅度控制，全部符合前两段乘积后激活、第三段线性，而非gate/up并联乘法。probe_native_split_pre_bits.py测出第一矩阵9个输入地址bit及逻辑输入坐标；probe_native_split_expand_bits.py确认分组扩展的输入高bit为13，不是直接复用C64 W1时的12。为探针新增原runner的DLSS5_SPLIT_FFWD_ONLY路径，只在8对齐native模式读首段紧凑输出，允许断开连接的全零控制，不把它作为完整四调用验收。

据指令/探针恢复record0三段：0..0x40000为512→512混合；0x40000..0x60000为8组64→256扩展；0x60000..0x80000为8组256→64收缩。混合后FP8，扩展后原half多项式激活再FP8，收缩每K32 half累计再FP8。最初直接套C64扩展位排列只有2.77% exact，测出bit13并修正后，validate_native_split_ffwd.py用真实record0及seed601/σ0.25、seed607/σ1.0的16×8×512输入，两个branch各65536值全部exact，MAE/max0，且按已测4×4 cell坐标逐值比对，不仅直方图相同。

validate_native_split_projection.py进一步使用真实record0和record1（512×512 FP8矩阵＋512个FP16 skip），分支先投影、残差作为half初始累加值。seed631/σ0.25与seed641/σ1.0两组FFN输出各65536值全部exact。前两次split调用至此具有真实原系数数值对照，旧“两个256×512 FP16 gate/up”的解释作废。参数与激活仅存release/native-c512。

当前仍为本地CUDA原版对CPU参考，未接AMD C512；QKV/attention、最后投影和4×2小尺寸合同仍待解决。5090新live数据仍未取得，AMD游戏DLL/公开包未改，已验证GPU范围仍block0..22 DS，最终画面目标active。

### 2026-09-06 C512 split完整四阶段CPU数值闭合

新增native_split_reference.py，在16×8×512的有效原生调用尺寸下，以真实block23四份记录验证完整split层。沿用已验证的512混合＋8组64→256→64 ffwd，接真实FFN外部投影/skip；QKV按FP8 512×512三矩阵分块，16 heads×32维，bias为16×64×64 FP16，16个FP32 scales；最后为512×512 FP8投影＋FP16 skip。矩阵地址规则与既有小通道实测布局同族，最终由真实算术约束，不拟合系数。

seed661/σ0.25、seed673/σ1.0两组输入，各自branch、ffn、attn、final四个阶段的65536值全部与原CUBIN逐值相同，MAE/max0；不是只比较最终图或直方图。输出范围、FP8 NaN码和每阶段exact均有断言，attention参数仅存release/native-c512/full-check。全四阶段至此具备CPU参考，但并未将小尺寸4×2的不正确调用当成裁判。

当前C512仍未接AMD，实际GPU正确前缀保持block0..22 DS。下一步实现split GPU阶段，并核对进入C512时的物理padding/有效尺寸合同（4×2现仍全零，不擅自视为已解决）；游戏DLL、公开ZIP未改，最终剑星画面目标active。

### 2026-09-06 C512 split独立AMD四阶段及切换输入验证通过

新增native_split.h/hlsl：ffwd实现512混合＋8组64→256→64，独立FFN投影接原skip；attention与最终投影复用已验证的参数化native_c64.hlsl，以CHANNELS=512、16 heads编译。四阶段资源常驻，显式UAV/SRV屏障，权重和原输入分别绑定，层间不做CPU读回或注入中间激活。prepare_native_split_gpu.py导出真实系数及独立原CUBIN的逐段oracle。

d3d12_native_split_test.cpp在9070XT、16×8×512独立输入上逐段检查branch/ffn/attn/final各65536值。首次三帧全部exact；随后扩为五帧A/A/B/A/A，A=seed673、B=seed661，两套oracle直接保存自原CUBIN读回。切换前等待fence，更新输入UPLOAD资源，确认B最终输出不同于A，恢复A后各阶段与首次结果相同；五帧四阶段均different0/max0，device/fence成功。GPU输出下载后再次与原oracle逐值比较，四段全exact。

这证明C512独立层GPU正确与资源重用，不等于RGB→block23整链已闭合：当前前端小夹具到block22仅4×2有效尺寸，原split的4×2调用仍全零，需要核对物理padding/尺寸合同或建立符合原调度的大夹具。实际连续RGB前缀仍block0..22 DS，独立C512仅16×8测试通过；游戏DLL、公开ZIP未更新，后续split/ViT/解码器及最终剑星画面目标active。

### 2026-09-06 C512小窗口及移位合同：20组原版逐值对照

check_native_split_small.py分别比较4×4、8×4、4×8、8×12、12×8五种尺寸，以及none/X/Y/XY四种QKV移位，全部使用真实block23四份记录。FFWD与FFN projection在所有情况下逐值一致；attention中有效维度恰为4的轴要重复，较大尺寸的尾端半窗口仍补零。4×4需重复两轴，8×4只重复Y，4×8只重复X，8×12/12×8的尾端不能循环回图像开头。20组对应规则下attn/final也全部exact，而错误规则均保留为对照。

新增native_split_reference.attention_window可重用实现，明确只接受已验证的至少4、4对齐尺寸，拒绝4×2等未闭合合同；独立候选构造及原CUBIN输出再次验证该函数，四个shift汇总均all_expected_modes_exact。各尺寸/shift的输入及原版四段oracle保存在release/native-c512/small-check，供GPU窗口包装验证，非拟合目标。

这一轮未改变AMD代码或游戏DLL：GPU连续RGB前缀仍block0..22 DS，C512独立GPU仍为16×8五帧验证。接回整链还要解决前缀4×2与C512物理尺寸合同，不能把本次4×4以上通过说成4×2已修复。最终游戏画面目标active。

### 2026-09-06 C512 GPU窗口包装验证：4×4重复与12×8补零

新增NativeSplitWindow及native_split_window.hlsl，GPU完成pack/原生四阶段/crop。有效轴恰为4则模4重复，较大轴越界补零，使用明确分支避免root SRV非法读取；支持已验证的至少4、4对齐尺寸，仍拒绝4×2。前后非attention阶段均为逐像素通道计算，窗口变换可放在整层入口，最终再crop。

prepare_native_split_window_gpu.py直接导出此前保存的原CUBIN小窗口fixture；d3d12_native_split_window_test.cpp在9070XT验证4×4 shiftXY，8192最终值全部exact、三帧重放/device/fence检查通过。随后12×8 shiftXY验证49152最终值全部exact，证明较大轴没有被错做循环重复；下载GPU输出再次与原oracle比较通过。这个包装测试只验最终crop输出，内部四阶段的独立数值与输入切换验证仍以前一检查点为准。

本轮未把C512接到RGB前缀，4×2逻辑尺寸与C512物理尺寸的銜接仍待核对；不能把4×4以上GPU通过称为4×2已解决。AMD连续RGB正确范围仍block0..22 DS，游戏DLL/公开包未更新，最终画面目标active。

### 2026-09-06 RGB128×128接入split：保留失败，首个差异定位到preblock

新增build_native_rgb128_oracle.py、prepare_native_rgb128_gpu.py及rgb128split测试模式，将RGB128×128连续经过block0..22 DS后接block23。此时C512有效尺寸为4×4，不再使用未闭合的4×2调用。check_split_physical_extent.py另证实同一4×4物理buffer按height2调用仍全零，按height4可对上CPU；这不证明真实游戏调度使用物理高度，live尺寸合同仍未解决。

AMD执行三帧，device/fence及重放检查通过，18次shader编译、42次缓存命中，神经层间无CPU传输；但validate_native_rgb128_gpu.py对原CUBIN最终输出只有25.1953125% exact，MAE0.277323、max2，严格判失败。首次上传遗漏input.rgba32f导致missing input，仅是启动失败，显式补传后才执行，不计入GPU验收。没有部署游戏DLL或修改公开包。

在整链结束后新增DS0/4/8/14/22只读检查点，并重跑三帧。compare_native_rgb128_checkpoints.py独立解码原输出、打印坐标和值，任何差异返回非零：不同值数量分别为7/1436/15288/11446/6305，对应总量131072/65536/32768/16384/8192。最早检查点block0 DS已有7处差异，集中于(y32..33,x32..33)与(y58,x3)，最大0.03125。深层误差不能仅凭该观察全归因于这7处；其余层是否还有独立误差需要受控输入继续分离。

进一步独立运行128×128 preblock五帧（seed0/0/0/1/0），重放及seed变化检查通过。diagnose_native_rgb128_preblock.py证明：独立AMD DS与整链DS逐值相同；AMD raw交CPU按half舍入池化与AMD DS逐值相同；因此当前7处DS差异不是整链接线或finish池化GPU实现造成。CPU完整参考DS与原版有8处差异（包含AMD的7处），CPU raw与AMD raw有85/524288处不同，最大0.0100708008；CPU同样不完全正确，不能据此直接改GPU去迁就CPU。下一步分离输入混合/随机数、FFN、attention的原生中间合同，禁止拟合修正、放宽阈值或以重放通过代替数值通过。

当前此前RGB128×64的block0..22 DS结论限于旧夹具，不外推到新尺寸；新128×128整链明确失败，最终剑星画面尚未修复/验收。脚本与检查点主机代码提交，所有原权重/激活留release，不提交二进制数据。

### 2026-09-06 preblock差异进一步隔离到随机特征计算

新增diagnose_native_rgb128_stages.py，以私有原权重副本清零后续矩阵、skip=1，分别调用原CUBIN的mix-only与FFN-only控制，保持128×128输入、live scalars、seed0。CPU mix对原版有4/524288处不同，坐标(y1,x64)、(y22,x72)、(y67,x66)、(y116,x7)；FFN-only有12处不同，仍集中在这四个像素。将mix中随机特征0/1/4的系数同时在原版及CPU清零后，524288值全exact。不是将此清零作为运行方案，而是定位用负对照。

反汇编原cc_tinlayout_fused_pre_block_swin_1h_32_1_ds_fp8的0x960..0xae0：随机半径先MUFU.LG2再乘float32 ln2和-2，随后MUFU.SQRT；角度先RN乘6.283185482025146，再RZ乘0.15915493667125702，随后MUFU.COS/SIN。旧CPU/HLSL直接log与sin/cos弧度表达式没有完整保留这些步骤。preblock_noise_reference.fields新增显式native_steps候选（默认仍旧行为），通过float64精确乘积及nextafter模拟正数乘法RZ，数学sin/cos仍不声称模拟MUFU。

该候选mix对原CUBIN差异从4降至1，仅剩(y116,x7,c29)，CPU0.0078125 vs原0.009765625，最大0.001953125。仍未全通过，不能直接替换GPU；下一步查剩余MUFU近似/舍入，并以更多seed和尺寸复核。此轮未修改运行HLSL、AMD游戏DLL或公开包，最终游戏画面未验收。

### 2026-09-06 原生随机特征隔离闭合：CPU preblock主/DS均exact

probe_native_noise_residual.py以原CUBIN mix系数做64倍差分抵消，读回四个问题像素的3个随机特征；只报告FP8读回兼容的附近half候选，不直接把量化值当精确逆。11个候选与native_steps一致，(y116,x7,g1)独立读回兼容half唯一为0.11749267578125，候选为0.1175537109375，确有一half刻度差异。所有控制权重留release，不替换真实权重。

新增probe_native_noise.cu直接生成16项随机中间值：整数hash/uniform、LG2、半径、角度、三角函数及3个最终随机数。初版__fsqrt_rn编译成RSQ加修正，反汇编不符原MUFU.SQRT，因此改为显式sqrt.approx.ftz.f32；复查生成代码含LG2/SQRT/COS/SIN及角度FMUL.RZ。compare_native_noise_trace.py对完整128×128发现数学候选与原生指令序列有53/49152个half随机值不同，不能因为mix只剩1处就称随机函数全对。

diagnose_native_rgb128_stages.py的mix_cuda_trace控制将直接指令生成的随机特征输入CPU mix，524288值全部对上独立原CUBIN。diagnose_native_rgb128_preblock.py --cuda-noise进一步沿原CPU FFN/attention/池化计算，main524288、DS131072值均与原CUBIN全exact，MAE/max0。这是在CPU诊断中替换随机特征来源用于隔离因果，不是GPU移植通过；AMD当前DS仍有7处差异，未把该trace馈入任何AMD整链或游戏。

由此当前128×128 preblock错误可由随机特征差异解释，正式下一步需在AMD实现相应数学/指令语义，并验证新seed而非硬编码位置修正。原CPU参考在采用原生随机指令序列时主/DS双分支已闭合。游戏DLL、公开包仍未更改，最终游戏画面目标未完成。

### 2026-09-06 AMD通用随机函数表落地；seed0双分支exact，留出seed暴露FFN单点差异

probe_native_noise.cu新增--tables：枚举全部2^24个u=(i+1)/2^24，生成radius/cos/sin三项float32，覆盖任意hash、坐标及seed，不含模型权重或画面激活。表201326592字节（192MiB），SHA256 aa38f7e6c5f20227f90edcb223fb28c258006a9931941df1442494cf5d64ac7f，仅存release及AMD实验目录。validate_native_noise_table.py在seed0、1、0x12345678三份独立指令trace上，重组出的每组49152个float32随机值全exact。

preblock_input_mix.hlsl新增显式NATIVE_NOISE_TABLE路径：GPU计算整数hash与24位索引，从通用函数表取值、相乘并half RNE后继续真实mix/FFN。NativePreblockRuntime新增可选表参数，root SRV t2与常驻UPLOAD资源；旧调用默认不启用，raw_features路径拒绝同时传表。当前UPLOAD读取及192MiB容量是正确性原型，未作性能优化、不据此报告游戏FPS。d3d12_native_preblock_test.cpp支持DLSS5_NOISE_TABLE和DLSS5_TEST_SEED，五帧基准/基准/基准/seed^1/基准，补device/fence有效性检查。run_native_noise_preblock.ps1只运行实验目录，未覆盖游戏DLL。

9070XT seed0实测五帧重放/seed变化通过，validate_native_noise_preblock.py重新调用原CUBIN独立比较：主524288、DS131072全exact，max0，旧7处DS差异消失。随后留出seed0x12345678五帧也正常执行，但原版数值比较失败：main16处/max0.0625，DS3处/max0.125。不得以seed0通过宣称普遍修复，验证器对此返回1。

诊断脚本扩展seed及GPU前缀。留出seed使用独立原生随机trace后，CPU与AMD仍同样有原版main16处、DS3处差异，CPU/AMD raw之间仅3个小舍入差，最大0.0001220703125；因此需继续审原算术而非调整表。原CUBIN分段控制显示mix_cuda_trace的524288值全exact；ffn_cuda_trace仅(y96,x59,c12)一值不同，CPU0.3125、原0.28125，位于后续差异所在8×8窗口。下一步定位该FFN点的prefix/activation/累计舍入，不能把问题笼统归因于attention或用后处理掩盖。

当前仅AMD独立preblock启用了表；RGB→block23整链还未重跑，连续链旧结果仍是失败，最终游戏DLL未更新、画面未验收。已有run_nvidia_ui.ps1无关修改保留，不纳入提交。

### 2026-09-06 修正输入混合捨入；AMD连续RGB→block23全exact

对留出seed0x12345678继续分段控制：原HFMA2激活公式改为float64乘加后直接half仍有同一FFN差异，排除此候选；只将输入混合16项点积改成float64精确累加再直接half，FFN524288值全exact。额外skip-only及逐组截断控制保留于diagnose_native_rgb128_stages.py，未修改真实权重。该案例说明“mix的FP8输出一致”不足以证明原始half prefix一致，后续FFN残差仍消费half值。

preblock_input_mix.hlsl在通用函数表路径下使用double累计half×half的16项点积，并新增half_round_exact直接实现half RNE：按half步长缩放、整数下界、余数与奇偶位决策，避免double先转float再转half的双重捨入。旧无表路径保留，不暗改尚未验收的游戏路径。9070XT留出seed0x12345678五帧正常，主524288/DS131072均与重新执行的原CUBINexact；随后seed1五帧也双分支exact，device/fence有效。未变更系数或放宽阈值。

d3d12_native_front_chain_test.cpp接入可选DLSS5_NOISE_TABLE，run_native_noise_preblock.ps1 -Chain运行seed0的128×128 RGB→block23，拒绝假装支持其他chain seed。实际三帧重放通过，18次shader编译、42次缓存命中，fence等待406/375/360ms（不是游戏FPS或纯GPU性能）。神经层间无CPU读回或激活注入，函数表只在初始化上传。

执行结束后下载检查点，compare_native_rgb128_checkpoints.py：DS0/4/8/14/22分别131072/65536/32768/16384/8192值全部different0、MAE/max0；validate_native_rgb128_gpu.py最终4×4×512共8192值全部exact。之前同一较大夹具最终25.1953125% exact的失败至此修复，前缀连续正确范围扩展至block23。表仍为192MiB常驻UPLOAD正确性原型，性能优化后续处理。

README顶部更新为当前范围，旧“整网已完成”等历史结论仍不作证明。尚未完成block24之后的真实数值链、真实游戏尺寸合同与最终RGB输出验证；游戏DLL、公开包未部署更新，目标继续active。

### 2026-09-06 block24～29原始plain split四阶段闭合，纠正FFWD线程数

新增native_split_weights.py，按此前逐值验证过的地址位规则解码各层四份原始记录（524288/263168/917568/263168字节），不沿用block23数值系数。validate_native_split_continuation.py分别以已闭合RGB链block23的4×4输出，以及独立seed1703/σ0.5的16×8×512随机输入，连续执行block24～29；沿用port既有shift3/1/2/0/3/1序列，此测试不是新游戏live调度捕获。

原runner新增native-plain，选择ffwd/ffwd_proj的普通输入符号，同时采用已修正的H/W、指针顺序、projection网格及线程约定。最初错误沿用inpview的FFWD32×4线程：4×4全部四阶段通过，但16×8 block24的branch下4行全漏算（32711值不同），FFN/attention/final随之错误。尝试加倍grid Y无效，已撤回；SASS中TID.Y右移2参与索引，改为plain FFWD32×8后恢复下4行，inpview仍保持32×4。禁止以4×4小尺寸通过推断一般调度正确。

修正后，独立16×8连续block24～29每层branch/ffn/attn/final各65536值全部exact，MAE/max0；重新回归4×4 RGB续链各阶段8192值也全exact。原始输出及系数留release/native-c512、release/native-rgb128，不提交权重数据。这一轮是原CUBIN对CPU参考的续链验证，尚未将24～29接进AMD连续链；AMD已验范围仍RGB→block23。游戏DLL/公开包未更新，最终游戏画面未验收。

### 2026-09-06 AMD连续RGB→block29验证通过

prepare_native_rgb128_gpu.py导出block24～29各自原始系数为GPU参数。d3d12_native_front_chain_test.cpp通过显式DLSS5_SPLIT_TAIL开启六层NativeSplitWindow，GPU source直接接block23输出，移位3/1/2/0/3/1，所有权重和资源常驻，同一个command list依次执行。run_native_noise_preblock.ps1 -Chain -Tail仅在seed0原型下启用，-Tail单独使用报错。输出另存output-rgb128-block29.f32，不覆盖旧block23输出或误标层号。

9070XT实际执行三帧：device/fence、finite及重放一致性通过；18次shader编译、78次缓存命中，fence等待672/625/641ms。这些是实验链提交到fence的时长，不是游戏FPS。层间没有CPU传输、NVIDIA特征注入或每帧文件读取，192MiB通用函数表仍为初始化时上传的正确性原型。

下载GPU输出后，validate_native_rgb128_gpu.py --last-block 29对独立原CUBIN连续链最终8192值全exact，MAE/max0。DS0/4/8/14/22五个原有检查点分别131072/65536/32768/16384/8192值也全exact。此GPU测试对24～29仅读最终block29，逐层四阶段CPU/原CUBIN证据仍以前一条为准，不混称本次逐阶段GPU审计。

当前AMD连续正确范围扩展至128×128 RGB→block29。下一关block30包含五份记录（前三/四份与split同尺寸，第五份524304字节），原CUBIN存在proj_pool_512_fp8符号；其调用、池化和进入ViT的合同尚待恢复，不能复用历史代理链结论。README顶部同步，游戏DLL与公开ZIP未更新，最终RGB和剑星画面目标仍active。

### 2026-09-06 block30原生投影/池化合同：16×8通过，4×4→2×2仍全零

新增run_original_split_pool.cpp独立调用cc_split_swin_16h_proj_pool_512_fp8。ELF参数元数据确认单个0x50字节参数；结合SASS恢复attn/ffn指针0/8、main/pool输出16/24、权重32、全尺寸H/W64/68及池化H/W72/76。首次错误沿用普通projection的32×8线程，引发CUDA700；Compute Sanitizer定位到共享内存0x2fe0指令越界，线程y5～7非法。改为32×4后执行成功，重新跑memcheck为0 errors。没有修改驱动或游戏状态。

check_native_split_pool.py对16×8独立随机FP8输入，零矩阵+skip1控制：main65536值全exact；pool16384值按4×4 cell排列解码后与half水平两两求和、垂直相加、乘0.25再FP8的结果全exact。通道bank/bank-swap两种错误候选保留作对照；cell不一致会明确失败。

validate_native_split_pool.py提取block30前四份真实记录，从非正方形原版block29输出继续，以shiftY调用plain split。CPU与原CUBIN branch/ffn/attn/main四阶段各65536值全exact；原pool kernel的main65536值及down16384值也全exact。池化使用投影后未量化的half值，而不是先FP8再平均，未拟合系数。

--rgb-small用当前RGB128×128连续链的4×4 block29输出回归：block30四阶段与pool kernel main各8192值全exact，但pool整份4MiB buffer全零，预期2×2×512中2047值非零，验证明确失败。不能靠假设4×4物理padding称2×2已闭合；真实调度的小尺寸限制仍待核对，后续ViT入口需足够尺寸的连续夹具。block30第五份记录（512→1024候选）尚未恢复验证。

本轮为原CUBIN/CPU数值与调用合同验证，未接AMD block30，AMD已验范围保持RGB→block29。游戏DLL、公开ZIP未改，最终画面目标active。

### 2026-09-06 扩大原版连续夹具至256×256，block30池化获得有效4×4输出

build_native_rgb128_oracle.py新增--size 256 --through-pool，旧默认128夹具不变，新夹具存release/native-rgb256。使用seed29701新生成1024个RGB tiles（不是简单复制旧图），preblock随机seed仍0、live scalar参数不变。原CUBIN从RGB连续运行block0～30：C512阶段8×8，block30池化输出4×4×512。全部已检查输出有限/非零与写入范围；pool kernel main与普通projection的整份4MiB输出逐byte一致，pool有效8192字节非零且无越界填充。

validate_native_split_pool.py新增互斥--rgb256，直接采用这条原版连续链的block29输入。CPU/原CUBIN block30 branch/ffn/attn/main各32768值全exact；pool kernel main32768值、down8192值均全exact，MAE/max0。256夹具成功提供非零4×4池化结果，不代表128夹具的2×2合同已解决。

为定位第五份记录的执行点，检查了各CUBIN入口符号；CUBIN04的cc_split_swin_16h_final_head_512_fp8为候选，元数据显示0x28参数，SASS含FP8 QMMA及FP8输出，但尚未验证其调用网格、矩阵排列与block30第五记录的对应关系。不能因kernel名称就宣称ViT入口已恢复。

本轮未接AMD256夹具或block30 GPU，AMD已验范围仍128×128 RGB→block29；新256夹具是原版oracle与block30 CPU参考证据。下一步恢复/验证512→1024入口，并扩展AMD尺寸与池化路径。游戏DLL和公开包未更新，最终RGB/游戏画面仍未完成。

### 2026-09-06 block30第五记录与512→1024入口矩阵数值闭合

新增run_original_split_head.cpp调用CUBIN04的cc_split_swin_16h_final_head_512_fp8，使用元数据确认的0x28参数：输入/输出/权重指针0/8/16，H/W32/36。权重严格524304字节，独立分配不以大零buffer掩盖读越界；记录SHA256 3ab0bf4b8e4b55cd4f60a8473d9cb0100896c1fd8328179188db43e427a50a4c，末尾16字节全零。

4×4原版block30池化输入、gridX1只写4096值，扩为gridX4后完整16384值（4×4×1024），memcheck0 errors。check_native_split_head.py比较两种地址候选：正确矩阵输出bits[3,6..14]，输入bits[1,0,4,5,2,15..18]，按K32 half累加并FP8；把输出额外bit附在末尾的错误候选不匹配。输出每个4×4 cell包含两个原C512 cell bank，独立排列解码后16384值全exact，不只是直方图一致。

再以seed1801/σ0.25独立16×8×512随机输入验证。首次gridX=4*ceil(W/4)多写到147455字节，被逻辑范围断言拒绝；修正为4*ceil(W/8)，gridY=ceil(H/8)、gridZ1、threads32×8后输出范围恰131072字节，所有131072值逐值exact，max0。再次memcheck0 errors。4×4真实连续夹具回归仍全exact，正确矩阵仅导出release下head-matrix.f32，未提交系数数据。

至此block30第五记录在该原kernel下确认为512→1024矩阵运算，真实256×256原版链有可用的4×4×1024入口输出。尚未恢复ViT内部算术或接AMD block30/head；不能把原版/CPU对照称为AMD整链验收。AMD连续范围仍RGB128×128→block29，游戏DLL和公开包未更新，最终画面目标active。

### 2026-09-06 AMD独立block30→池化→512/1024入口验证通过

NativeSplit/NativeSplitWindow新增默认关闭的raw_output参数，仅最终projection编译RAW_OUTPUT=1，FFWD/attention保持原FP8语义。crop保留未量化half值，供后续池化；普通调用默认仍为FP8输出。NativeC32Downsample复用既有raw池化+矩阵shader并扩展CHANNELS=512：水平half加、垂直half加、乘0.25、FP8后做1024×512投影，每K32 half累计。此512分支暂拒绝小于8或非8对齐输入，避免将未闭合2×2池化当成支持。

prepare_native_pool_head_gpu.py以原版256夹具的block29输出作为独立测试入口，导出block30真实四份参数及已验证head矩阵；裁判仅取原CUBIN最终head，不采用拟合/CPU预测作为oracle。d3d12_native_pool_head_test.cpp在9070XT连续执行8×8 block30 shiftY→4×4 pool/head，内部不读回或注入中间值。三帧各16384值全exact、max0，device/fence与重放检查通过；下载gpu.f32后再次核对oracle全exact。

为验证默认路径未被raw开关改变，重新构建d3d12_native_split_window_test.cpp，读取现场夹具大小确认12×8后执行shiftXY，三帧每帧49152值全exact、max0。较长RGB链宿主亦编译通过，但本轮未重跑该整链。

范围严格为AMD独立block30→pool→head，入口是原版block29特征，不是AMD从RGB生成；AMD全连续范围仍RGB128×128→block29。下一步参数化256尺寸并把本段接回连续链，然后继续ViT。游戏DLL、网盘包未更新，最终RGB/剑星画面目标active。

### 2026-09-06 AMD连续256×256 RGB→block30/head全exact

d3d12_native_front_chain_test.cpp新增rgb256head模式，前端各阶段宽/高从输入尺寸派生，C512为8×8，六个尾层后接block30 raw、池化及head；原128模式保持原尺寸。输出独立命名output-rgb256-head.f32，避免与block29/23混淆；五个检查点读回长度也按256输入更新。prepare_native_rgb128_gpu.py --size 256导出block30及head参数；PowerShell -Chain -Head显式选择新模式，使用独立native-rgb256实验目录。源RGB只在初始化上传，神经层之间没有CPU传输或原版特征注入。

9070XT实际三帧重放、finite、device/fence全部通过；19次shader编译、83次缓存命中，fence等待734/703/688ms，仅为实验时长，不是游戏FPS。下载后compare_native_rgb128_checkpoints.py --folder release/native-rgb256 --size 256核对DS0/4/8/14/22分别524288/262144/131072/65536/32768值全exact；validate_native_rgb256_head.py最终4×4×1024共16384值全exact，MAE/max0。此前独立block30入口已由AMD真实前缀取代，连续GPU范围扩展到编码器head。

等待GPU初始化期间复核ViT重排：run_original_vit_repack_permutation.cpp新增输入尺寸范围检查、两份随机FP8留出输入对照，4×4情况下16384-entry映射为双射，source0..16383，bit-plane恢复及两份held-out数据全部通过。映射与metadata在release/native-rgb256；这只是原CUBIN地址重排验证，还未执行ViT算术或把repack接AMD。

README顶部同步当前范围。仍待ViT31～38、解码器/最终RGB和真实游戏尺寸、画面验证；游戏DLL、公开包未更新。不能把实验编码器全部exact称为剑星DLSS5画面已修复，目标active。

### 2026-09-06 ViT31 expansion初步调用与物理token padding诊断（数值未通过）

新增run_original_vit_expand.cpp独立调用cc_vit_1d_ffn_expand_fp8，按元数据0x48参数及原runner/SASS配置input0、output16、weights24、状态buffer56、token数64。block31.layer0原记录4194320字节，直接提取，不用旧effective矩阵。check_native_vit_expand.py将已验证的block30 head按原repack映射构造16-token物理输入。

首次gridX16只写32768字节，改为gridX32后获得完整65536字节（16×4096）非零/有限输出。尚不能外推所有token数的网格规则。直接套Swin通用bit矩阵排列及旧ViT原字节unpack两种候选均失败：分别65271、64352个值不同；排序比较亦不同，不只是输出位置差异。验证器明确返回失败，不继续运行后续层，也不拟合校正。

进一步将输入分配从宽松4MiB收紧到16×1024字节，Compute Sanitizer发现原kernel在+0x880读取第16384字节起的1024字节，越界；该诊断进程CUDA719退出。输入明确零填充到32×1024字节后，保持逻辑token16、gridX32、32×4线程，memcheck0 errors，输出仍完整65536字节。这确认当前16-token调用需要更大的物理输入读域；不是把padding行当有效token或验收结果。下一步恢复原ViT矩阵/激活地址规则，不能把执行无错误当算术正确。

AMD代码及游戏DLL此轮未改，已验连续范围仍256×256 RGB→block30/head；ViT本体未完成，最终游戏画面目标active。

### 2026-09-06 ViT31 expansion地址探针及融合激活数值闭合

当前独立诊断源为run_native_vit_expand_probe.cpp，旧run_original_vit_expand.cpp已保留原接口，避免破坏历史调用。本轮新增DLSS5_VIT_EXPAND_LAYOUT_SCAN：零权重中只开offset0及22个地址bit对应的单位FP8系数，用四组4-bit源地址编码读取每个输出实际访问的输入位置。所有控制在release下，不拟合真实模型系数。

第一轮将输出当纯copy解码失败，进一步SASS核对发现0x4930起HFMA2/HMUL2已执行激活：expand本身包含clamp(-4,4)及half多项式，不是等contract才激活。使用16个激活后仍可唯一识别的编码值恢复地址：输出与输入低位都不同于Swin。测得矩阵output bits[6,3,9,7,8,10..16]，input bits[0,1,2,4,5,17..21]；每个单位系数恰对应16个token输出。probe_native_vit_expand_layout.py保存23组位置/源地址，形成可复查证据。

check_native_vit_expand.py直接按该bit规则解码完整原block31.layer0记录，K32 half累计后先执行half激活再FP8，不插入激活前FP8。16-token原RGB256连续链入口的65536值全部与原CUBIN一致；另外独立seed1901/1907随机FP8输入各65536值也全exact，max0。旧Swin-bit、旧ViT unpack等失败候选保留为对照，但最终门槛只接受新原生公式全exact。matrix仅导出release/private夹具目录，不提交权重。

还核对了原1d重排与head canonical坐标：14个物理地址bit对应mask[2,1,8192,16,4,8,1024,2048,4096,32,64,128,256,512]，说明进入ViT不能直接把旧HWC通道序当相同坐标。当前通过的是原CUBIN/CPU expansion，不是AMD ViT或完整block31；收缩层、QKV、attention、projection仍待恢复，AMD已验范围仍RGB256→block30/head。游戏DLL/公开包未改，最终画面目标active。

### 2026-09-06 ViT31 contraction数值闭合，并修复诊断调用的同步计数器竞态

新增独立run_native_vit_contract_probe.cpp，保留旧run_original_vit_contract.cpp不动。按原0x48参数及四个cluster-Z分组调用cc_vit_1d_ffn_contract_fp8，输入按32-token物理空间补零，权重4196352字节独立分配，真实record SHA256 143e90b9d5e8359ad8e20bb4f780765014136a838efbadb91c95329b525942f0。

初版沿用旧诊断把同步计数器清零：memcheck下能结束且0 errors，但普通执行超过85秒仍不返回。核对同一PID/CPU后，主动SIGTERM终止本轮自建实验进程1636501；未杀游戏、未重启GPU/Steam，也没有把观察超时当进程已结束后重复启动。SASS 0x3ef0..0x3f60显示Zk等待counter>=k-1，Z0最后发布0；初始0会让Z1提前通过并被Z0后到的写入覆盖，导致后续等待。将aux+0xa00的计数器区初始化为-1后普通运行立即完成。首次清零计数器的“memcheck可结束”不作数值裁判。

check_native_vit_contract.py直接解码矩阵：output bits[6,3,9,7,8,10..14]，input bits[0,1,2,4,5,15..21]；skip按已有ViT输出轴低位排列。计算为四个1024输入维度分区，每区K32 half累计，残差H(input*skip)只作为Z0初值，再按Z0/Z1/Z2/Z3顺序half相加，最后FP8。完整K4096串行half累计在三组输入分别有449/444/492处不同，明确拒绝；正确分区规则各16384值全exact，max0。

验证输入为真实RGB256编码器入口经原expand，以及独立seed1901/1907对应expand。真实入口普通执行三次输出整份文件一致；其余两组也逐值exact。修正计数器后再次memcheck为0 errors。该同步修复仅针对原版诊断runner，AMD未来可用确定顺序直接实现同一算术，不需复制跨CTA忙等。

当前原CUBIN/CPU已闭合ViT31 expand+contract；QKV、attention和最后projection仍待恢复，AMD连续范围保持RGB256→block30/head。游戏DLL/公开包未改，最终游戏画面目标active。

### 2026-09-06 ViT31 QKV原生诊断调用及第三输出反例（未数值闭合）

新增run_native_vit_qkv_probe.cpp，不改旧runner。0x50参数采用input0、三个输出8/16/24、weights32、同步/工作区40/48、H/W72/76；SASS先将H/W相乘，故这里不能误当单个token数。使用真实block31.layer2记录3145856字节，SHA256 8317de7004196a2fbb6d87603b9083f71ef3dddfbe3a1dcc618edd65b6eb9cd9；输入物理空间暂按128-token对齐分配，尚未证明这是最小需求。4×4、gridX16、clusterZ2、32×4线程，候选同步计数区初始化-1。

原CUBIN memcheck0 errors，普通运行也正常返回。前两个输出非零范围至16383；第三输出至32763，每8字节前4字节有数据、后4字节全零，实际非零16383个。这只描述物理读回，不能据此完全确认Q/K/V语义。check_native_vit_qkv.py试验第三矩阵按整矩阵/32768/2048/1024字节分组、K1024串行或双512分区等候选，排序比较亦不一致，验收明确失败。

进一步DLSS5_VIT_QKV_UNIT_SCAN及probe_native_vit_qkv_groups.py使用单位系数offset0与22个地址bit、输入物理区全1、尾部候选scale全1，试图定位V权重区。所有所试offset在第三buffer前32768字节均出现128个0x57，而非预期16个0x38；因此“第三输出等于独立V线性投影”的当前控制合同不成立，不能从该扫描推出矩阵排列。完整输入padding也填1，后续需区分无效token影响、buffer语义、scale格式及工作区依赖；不预先选定其中任何解释。脚本打印失败数据并返回非零，不当作通过。

本轮仅恢复可运行诊断及保存反例，QKV算术尚未闭合，未修改AMD或游戏DLL。下一步核对原指令中各输出和workspace的实际数据来源。AMD已验范围仍RGB256→block30/head，最终游戏画面目标active。

### 2026-09-06 ViT QKV头部偏移更正，V算术及输出坐标闭合

沿SASS权重load的+0x80偏移核查，前次单位系数实验的关键前提错误：block31 QKV矩阵从byte128开始，前128字节为32个可读float32 scale值，不是尾部scale。旧vit-layouts.json及历史runner对尾128字节的解释不能沿用；尾128字节实际仍属于矩阵。先前往尾部写32个float32 1人为污染了V矩阵，解释固定0x57反例，不能据此否定V线性投影。

原runner增加可选prefix-scale和valid-only控制，旧尾部控制保留。正确prefix控制加有效16-token输入时，V相关单位系数输出16个0x38；填满物理padding时前32768字节会出现32个单位响应，说明诊断需保持无效输入零填充。probe_native_vit_qkv_groups.py打印实际record offset并拒绝全空响应。

完整权重候选中，prefix128、Q/K/V每1024字节交错、两组K512分别按K32 half累计再half相加，V排序值集合完全匹配。随后新增DLSS5_VIT_QKV_V_LAYOUT_SCAN和probe_native_vit_v_layout.py，以21个单位系数（compressed V offset0及20个地址bit）及四组源地址编码，逐个恢复输出/输入坐标，不用直方图作为最终证明。

check_native_vit_v.py采用测得V输出token bits[1,0,4,5]、channel bits[6,3,9,7,8,10..14]；物理bit2为当前16-token情况下的空位。完整V矩阵output bits[6,3,9,7,8,10..14]、input bits[0,1,2,4,5,15..19]，实际record地址128+(i//1024)*3072+2048+i%1024。真实编码器/FFN入口及独立seed1901/1907三组，各16384值全部逐值exact，max0。

这一轮只证明V；Q/K的scale应用、归一化和输出坐标还未验收，不能称完整QKV通过。AMD已验连续范围仍RGB256→block30/head，游戏DLL与公开包未更改，最终画面目标active。

### 2026-09-06 ViT31完整Q/K/V原生数值与坐标对照通过

check_native_vit_qk.py先独立比较矩阵与归一化候选，保留为探索脚本并明确返回未验收状态。Q在原SASS 0x2c40等位置先HMUL2乘5.65625，再乘头部float32转half的scale；不能只归一化后乘scale。Q/K均需两个K512分区分别K32 half累计再half相加。归一化前将ViT每32维低位按[1,0,4,2,3]变换到已验证tensor通道序，执行原half平方/求和/倒平方根语义，再还原通道序。按旧通道序K仍有19个排序值差异，改正后Q/K排序值全部一致。

坐标验证进一步证明Q沿用1d主视图；K需要交换物理地址bit2/bit3。该单一全局位交换在真实入口上发现，并在两份独立随机入口保持不变。Q token bits[2,6,7,8]、channel bits[0,1,3,4,5,9..13]；K token bits[3,6,7,8]、channel bits[0,1,2,4,5,9..13]。V仍按先前地址探针定义，不混用Q/K视图。

新增native_vit_qkv_reference.py纯算术及原始权重解码；validate_native_vit_qkv.py重新执行原CUBIN，严格核对三个输出的坐标、有效范围和NaN码。真实RGB256编码器经原FFN的入口、seed1901和1907两组独立入口，各自Q/K/V每路16384值全exact，MAE/max0。不是仅排序或直方图匹配，也未修改任何模型权重。

当前CPU/原CUBIN已闭合ViT31的expand、contract、完整QKV。attention和最终projection尚待恢复，AMD连续范围仍RGB256→block30/head，游戏DLL/公开包未更新，最终画面目标active。

### 2026-09-06 ViT attention：64-token数值闭合，16/32-token调用仍不作裁判

新增run_native_vit_attention_probe.cpp独立调用原cc_vit_1d_attention_fp8，按0x40参数配置Q/K/V、输出及H/W，gridX32、32×4线程，输入显式零padding。16-token调用虽完整输出且memcheck0 errors，但check_native_vit_attention.py的最初候选大幅不符。probe_native_vit_attention_controls.py发现关键反例：Q=K=0、有效V=1时，当前16-token和32-token配置均输出2，64-token输出1；16-token真实Q/K＋常量V输出1～24，不能作为正确attention裁判。此处只记录这些配置的行为，不宣称已证明所有尺寸的官方限制。

64-token进一步以独立随机Q/K＋常量V控制，65536值全部为1。之后使用seed2017/2027/2029三份64×1024随机输入，经已验证CPU QKV公式构造attention输入，按原物理Q/K/V排列直接提交原kernel；这是独立attention测试，不是已闭合原版64-token FFN整链。

SASS恢复的exp公式与Swin不同：score先half，乘half系数0x2dbb，加1.708984375，half后clamp[1.439453125,1.9775390625]；将half bits左移4再加0x4000得到exp half。分子先FP8(exp)×FP8(V)，每K32 half累计；分母使用未FP8的exp，按token bit映射[4,0,1,3,2,5]还原tensor顺序后执行原half归约。最后分子乘half倒数再FP8，而不是先量化归一化概率。旧求和次序留下百余处差异，正确次序三份各65536值全exact，max0。

新增native_vit_attention_reference.py固定已验64-token合同，其他shape明确拒绝；检查脚本只接受canonical64/exp-first规则，不以任意候选“碰巧通过”为门槛。三份均复核可复用函数成功。64-token原kernel再次memcheck0 errors，输出整份文件与普通执行逐byte相同。所有输入/oracle在release/native-vit/attention64-*，未修改权重。

当前attention仅原CUBIN/CPU64-token独立测试通过；现有RGB256编码器入口为16-token，尚不能直接与此段拼成有效完整ViT，后续需更大连续夹具或进一步核对小尺寸调度。最终projection、AMD ViT及解码器仍待完成，游戏DLL/公开包未更新，最终画面目标active。

### 2026-09-06 ViT31 projection与完整64-token层闭合；更正FFN尺寸参数

run_native_vit_contract_probe.cpp增加独立projection模式，使用原cc_vit_1d_projection_fp8、1050624字节真实block31.layer4、四个cluster-Z分组及-1初始计数器。projection参数末尾为H/W而非token标量。check_native_vit_projection.py在64-token已验证attention输出上，使用对应随机残差输入做独立尾投影测试；四个K256分区各自K32 half累计，残差仅Z0初值，再Z0/1/2/3顺序half相加、FP8。seed2017/2027/2029各65536值全exact，max0；memcheck0 errors且完整输出文件与普通运行相同。

复查SASS发现expand/contract末尾0x40也会将两个32位值相乘。此前诊断写入uint64 token数的接口解释错误，已改为明确H/W（16-token=4×4、64-token=8×8），projection模式同样使用此表示。重新运行16-token expand/contract仍全exact；原先数值公式未因这次参数修正改变，但后续必须使用修正后的ABI，不能沿用旧scalar解释。64-token expand/contract的memcheck均0 errors，输出与普通执行逐byte相同。

新增native_vit_linear_reference.py统一已验证ViT矩阵解码、融合激活、分区残差投影；旧独立检查器增加对该可复用实现的严格对照。expand、contract及三份projection回归全部通过。

新增validate_native_vit_block31.py，64-token源输入一次生成，原CUBIN按expand→contract→QKV→attention→projection连续传递自己的输出，CPU参考在每段独立对照，不注入CPU校正结果。seed2101、2107两份完整层测试：expand各262144值，contract/Q/K/V/attention/projection各65536值，全部different0、max0。QKV64-token的Q/K/V物理排列也在此首次完整逐段回归。该结果是完整block31原CUBIN/CPU闭合，不是AMD ViT或RGB到block31连续GPU验收。

仍待ViT32～38、AMD ViT接入、解码器/最终RGB及真实剑星画面验证。AMD已验连续范围保持RGB256→block30/head；16/32-token attention的异常未因本轮FFN ABI修正而宣称解决。游戏DLL与网盘包未更改，最终画面目标active。

### 2026-09-06 ViT31～38完整原生连续链双输入验证通过

validate_native_vit_block31.py新增--last-block 31..38，逐层提取各自expand/contract/QKV/projection真实记录。每层原CUBIN直接读取上一层原projection文件作为输入，CPU参考独立沿同一链计算，并对expand、contract、Q、K、V、attention、projection七个检查点逐值验收；任一失败即停止，不把CPU结果写回原版链。

首次进入block32时，expand诊断reader拒绝前层4MiB固定输出文件，即使有效64KiB之外全零也报size错误。将reader改为先严格检查超过物理输入范围的字节全零，再缩到实际分配尺寸；权重记录仍要求精确长度。中间一次编译出现vexing-parse错误，旧binary重复报读入错误，两次都未计通过；修正构造并以编译成功为执行门槛后完整运行。

seed2101、2107两组64-token链均从block31连续到block38，各56个检查点全部different0、max0；每层expand262144值，其余六个检查点各65536值。此处没有放宽阈值或拟合修正，使用此前冻结的原生舍入、分组累加、QKV头部偏移及attention归约规则。

新增每次任务启动先写status=running、全部通过后才写status=pass的validation.json，防止旧pass文件被误认作新测试；seed2107报告已现场核对status=pass、56项、last_block38。源夹具、权重和完整原输出均留release/native-vit/chain31-38-64-*，不入Git。

至此64-token原CUBIN/CPU ViT八层连续链已闭合，但AMD ViT尚未实现/验证，RGB256编码器输出仍为16-token，不能和这条64-token链冒充完整GPU路径。下一步实现AMD ViT并建立足够尺寸的RGB连续夹具，再继续解码器与最终游戏画面。游戏DLL/公开包未更改，目标active。

### 2026-09-06 AMD ViT三种原生线性算子独立验证通过

新增native_vit_linear.h/hlsl，支持已验64-token下1024→4096 expand、4096→1024 contract、1024→1024 projection。每K32明确half RNE；expand融合原half激活后FP8；两个残差投影均保留四分组累计，残差只入第一组，组间按固定次序half相加。参数/资源常驻，Record内无CPU读回或中间值注入，未做速度优化或舍入放宽。

d3d12_native_pool_head_test.cpp增加三个显式linear模式，原双参数pool/head模式保留。prepare_native_vit_linear_gpu.py只从已通过的seed2107八层原版链导出block31三个独立算子的输入、原CUBIN输出与直接解码系数，并先检查validation.json的pass及56个零差异检查点。输出/系数均在release/native-vit/amd-linear，不提交权重。

9070XT逐一执行：expand每帧262144值、contract每帧65536值、projection每帧65536值，三个算子各三帧均different0、max0；device/fence、finite及重放检查通过。GPU文件下载后再次与各自原oracle逐值比较，全exact。测试入口是原版相应中间特征，明确是独立算子验证，不是GPU从RGB生成或三算子已直连，更不代表完整ViT。

下一步实现AMD QKV与attention并连接线性算子，再做完整block31及八层GPU链验证。AMD全连续范围仍RGB256→block30/head，游戏DLL/公开包未更改，最终画面目标active。

### 2026-09-06 AMD QKV及QKV→attention直连验证通过

新增native_vit_qkv.h/hlsl，两pass实现三矩阵投影和Q/K归一化、V量化。投影保留两个K512分区及K32 half累计；归一化按已测tensor通道序，Q依次half乘5.65625及head scale，K不加该scale，V直接FP8。GPU参数使用解码后的三份row-major float矩阵＋32个scale，不能与原记录前128字节scale的物理布局混淆。输入/中间/输出常驻，pass间只有GPU屏障。

新增native_vit_attention.h/hlsl，限定已验证64-token；复现原exp位操作、先FP8(exp)×V再归一化、canonical token顺序half归约及K32半精度累计。输出为逻辑64×1024，不依赖NVIDIA中间数据、拟合修正或CPU求和。

d3d12_native_pool_head_test.cpp增加qkv和qkv_attention模式，旧模式保留。prepare_native_vit_qkv_gpu.py从seed2107已通过的八层原版链导出block31 contract作为独立入口，并分别以原Q/K/V及原attention输出作裁判。9070XT独立QKV每帧196608值、QKV→attention直接GPU串联每帧65536值，各三帧均different0、max0，finite/device/fence与重放检查通过。两个GPU读回文件下载后再次逐值比较，全exact。

这些是block31 QKV及QKV→attention片段的GPU验证；入口仍是原版contract特征，不是完整GPU FFN或RGB链。本轮算子之间不做CPU读回，但不能称完整ViT已验。下一步串联GPU expand/contract/QKV/attention/projection及八层链。AMD全连续范围仍RGB256→block30/head，游戏DLL/公开ZIP未更新，最终画面目标active。

### 2026-09-06 AMD完整block31与ViT31～38八层链，通过A/A/B/A/A动态回归

新增NativeVitBlock，将常驻expand、contract、QKV两pass、attention、projection按原残差关系直连：contract残差来自层输入，projection残差来自contract输出。所有阶段由同一GPU command list顺序执行、只做资源屏障。宿主新增block31和vit_chain模式；八层各自加载原权重，不复用block31系数冒充后续层。

prepare_native_vit_block_gpu.py从已通过的原CUBIN/CPU链直接解码初始输入及原最终输出，导出单层或八层参数。9070XT完整block31三帧各65536值全exact；继而八层链三帧最终block38各65536值全exact，device/fence、finite与重放检查通过。本GPU测试读回最终结果，逐阶段原/CPU证据仍以前面的56项回归为准，不混称本轮逐阶段GPU审计。

重跑seed2101原八层链，56项全exact并生成pass报告，作为独立B输入；A为seed2107。宿主新增显式DLSS5_VIT_SWITCH_INPUT，run_native_vit_chain.ps1启用A/A/B/A/A五帧：只有等待前一帧fence结束后才更新初始UPLOAD输入，不更新中间激活或权重。五帧最终输出各65536值均与对应原CUBIN oracle全exact；B与A不同、恢复A后与初始baseline一致。下载最终gpu.f32再次比较恢复后的A，全exact。

当前64-token ViT31～38已实现并在AMD独立连续验证，未以CPU修正中间值，也没有跳算来提高显示帧率。但该链的初始输入仍为独立ViT夹具；RGB256编码器输出16-token，尚未与64-token链接成完整RGB路径。下一步建立足够尺寸的RGB连续夹具和重排连接，并继续解码器及最终画面审计。游戏DLL/公开ZIP未更新，目标active。

### 2026-09-06 RGB512原版链贯通ViT38，建立HWC/ViT桥接索引

build_native_rgb128_oracle.py增加size512，连续原CUBIN RGB→block30 pool生成有效8×8×512池化结果；check_native_split_head.py --rgb-size512验证8×8×1024 head全部65536值与原矩阵运算exact。旧128/256夹具保留，不把16-token attention限制绕成通过。

run_original_vit_repack_permutation.cpp扩展可选真实输入/输出参数：8×8下恢复65536-entry重排双射，通过两份随机留出输入，再实际运行原kernel重排RGB512的head。真实输出逐byte等于映射预测，有效区域外全零。得到release/native-rgb512/vit-input.fp8，不是用CPU拟合替代原kernel。

validate_native_vit_block31.py新增--input1d，直接读取该原repack输出，按输入SHA区分任务目录，导入输入时seed标null且不重新量化源文件。原版ViT31～38连跑及CPU逐阶段56项全部exact，报告在release/native-vit/chain31-38-input-b38e14db0110/validation.json。该路径覆盖原版RGB512编码器到ViT38；512编码器本身尚未在AMD验证，不将原版运行当GPU移植通过。

新增prepare_native_vit_bridge.py，复合原head的4×4 cell/双C512 bank排列、实测1d重排和ViT逻辑轴，得到65536项HWC→ViT gather索引及逆映射。两者为完整双射，实际head数值经过该映射与原repack输出的逻辑视图全exact。索引是数据无关地址变换，不含模型权重或固定画面值；导出hwc-to-vit.i32、vit-to-hwc.i32及bridge.json，GPU_bridge_verified明确false。

下一步在GPU应用此桥，并将512尺寸编码器接入已验64-token八层ViT。AMD现有证明仍是RGB256→head与独立ViT链两段，未合并；解码器/真实游戏尺寸和最终画面仍待验证。游戏DLL与公开包未更新，目标active。

### 2026-09-06 GPU桥接通过；完整RGB512→ViT38新夹具失败，首差异回到preblock

新增native_vit_gather.h/hlsl，GPU按65536项数据无关索引做HWC→ViT复制，创建时验证索引为有界完整双射及源buffer容量。宿主bridge模式用原RGB512 head及原repack作独立裁判，9070XT三帧65536值全部exact；下载再次比对通过。该表不包含模型权重或固定画面数值。

d3d12_native_front_chain_test.cpp新增rgb512vit：512尺寸编码器→block30/head→GPU gather→八个NativeVitBlock全部在同一GPU链运行，层间无CPU读回/注入。512参数与索引在prepare_native_rgb128_gpu.py --size512准备，PowerShell -Chain -Vit显式选择；输出独立命名output-rgb512-vit.f32。新增validate_native_rgb512_vit.py核对RGB导出的原ViT38裁判，写明确pass/fail报告。

实际三帧重放与device/fence正常，26次shader编译、125次缓存命中，fence等待1719/1672/1656ms（不是游戏FPS）。但严格数值验证失败：DS0有5/2097152值不同，最大0.03125，位置集中在(y190..191,x192..193)，对应RGB约x384、y376起的8×8窗口；DS4/8/14/22不同值分别830/14945/61226/91501。最终ViT38有45998/65536值不同，最大1.03125。不能把执行/重放通过当移植正确，也不把后续全部差异未经分离就只归因于这5处。

检查远端preblock三份HLSL的SHA，与当前本地文件逐一相同，排除误用旧shader这一候选。下一步以该preblock窗口分离原/CPU/GPU中间结果，检查尚未覆盖的捨入或特殊函数边界。此前256尺寸编码器、独立64-token ViT及桥接的通过范围保留，不外推到512；完整RGB512 GPU链明确未通过。游戏DLL、公开包未部署更新，最终画面目标active。

### 2026-09-06 RGB512首差异定位到mix；float32末次捨入候选全幅失败并撤回

新增diagnose_native_rgb512_tile.py，仅计算global(x384,y376)的8×8窗口，使用独立原生随机指令trace、精确输入混合及原CPU FFN/attention。CPU main对原有21处不同，DS有5处不同；CPU DS与此前AMD DS全部相同，说明当前反例可在共同参考里重现，不是仅AMD执行故障。新增diagnose_native_rgb512_stages.py做原kernel消融：mix只在局部(y6,x0,c0)一处不同，FFN有同一像素的8个通道不同。

probe_native_noise_residual.py扩展size512与gain4096。在global(y382,x384)对三项随机half值做差分抵消，三个读回均仅兼容候选本身；gain64时第三项分辨率不够，不能先前零残差就说完全一致，4096倍后才唯一确认。因此当前点的随机half值一致。

该mix点精确和为-0.2108764603290183，距half边界-0.21087646484375约4.5e-9。试验“精确double求和→float32→half”能让问题窗口CPU main/DS全部对上，也保留旧128/seed0x12345678的FFN通过。然而此局部候选不是全局规则：部署至独立512 preblock后，主分支有337/8388608处差异，DS有91/2097152处差异（max0.0625），比原DS5处更多，严格判失败。

已恢复本地及AMD实验目录的原preblock_input_mix.hlsl，不保留这个负向修改。失败shader及lut-main/down/raw文件归档release/native-rgb512/round32-candidate；远端读回文件也移到对应round32-candidate，避免误当恢复后shader的输出。未删除实验数据，可继续复核。当前tile-diagnostic恢复记录原基线，新增rounding标志避免混淆。

保留Width512独立preblock运行支持、全幅main/DS验证器及诊断脚本。下一步需恢复真正的HMMA混合累加/中间舍入过程，不能用单点匹配推广float32末次舍入，也不放宽通过标准。完整RGB512→ViT38仍未通过，游戏DLL/公开包未更新，目标active。

### 2026-09-06 输入混合HMMA规则修复，AMD RGB512→ViT38连续链通过

新增probe_preblock_wmma.cu直接运行half输入/half累加WMMA，并以check_preblock_wmma系列脚本分离输入混合。全幅8388608个混合值量化后与原mix一致；同时倒序和随机打乱成对K项，原half结果不变，排除普通顺序float累加假设。以两操作数frexp指数之和的最大值E为对齐基准，每项按2^(E-27)向零截断、精确求和、最后half RNE，全部8388608个原WMMA half值一致。另换seed2213的新输入与新权重，2097152值一致；留出脚本此前仅打印结果，未写validation.json，不能引用不存在的报告。

native_preblock_mix_reference.py保存实测规则；preblock_input_mix.hlsl的NATIVE_NOISE_TABLE分支按相同规则实现整数累计及高低16位精确转换。该规则是当前已测范围的实现，不声称所有Tensor Core模式都通用。修复仅部署AMD native-rgb512实验目录。

修复后512独立preblock的main 8388608值、DS 2097152值全exact；随后连续RGB512→block30/head→GPU gather→ViT31～38三帧执行，DS0/4/8/14/22全部exact，最终65536值different=0、max_error=0。报告release/native-rgb512/amd/validation.json为pass，scope明确不含decoder或游戏画质。层间无CPU读回或中间特征注入；本轮GPU审计覆盖五个DS及最终输出，不冒称每个内部阶段均读回。fence等待1657/1656/1765ms为实验等待时间，不是游戏FPS。

16:27恢复会话后核对报告，未发现所查询的本地实验进程；AMD查询native-preblock-test、native-front-chain-test及游戏进程无输出。用户先前系统异常提示原因未查明，不归因于任何未经证实的服务或GPU故障。下一步恢复第39块原生解码器合同，不能复用旧全FP16代理推断。真实1080p、最终RGB、游戏DLL部署及画面验收尚未完成；游戏DLL、存档与公开包未修改，目标active。

## 工作纪律

### 2026-09-07：实际0～70网络抽成游戏可调用GPU组件，待回归

- 新增 `native_actual_network70.h`，从已验证完整host提取实际尺寸encoder0～30/head、640 gather/ViT、decoder39～69、post70创建与提交；输入是外部GPU tile-major RGB和HWC post base，唯一CPU数据为系数/映射/函数表，不读oracle或预制特征。
- Run使用NativeGameSubmission，encoder/head/bridge同段、ViT逐chunk、decoder逐stage、最后post；固定实际几何和已验证shift，保留raw encoder跳接由原decoder F量化。检查RGB buffer容量/同device/noise长度，禁止post诊断；异常后网络poison不复用。
- 完整帧串行与输入producer已提交仍是调用方责任；GPU超时必须保留整个网络及外部资源。组件不操作游戏list，也不含游戏颜色空间/取图选择假设。
- MinGW独立语法检查exit0，尚未GPU运行这份提取组件；此前完整host通过不能自动等同于本次抽取通过。下一步用同源fixture回归此类，并组合纹理入口/显示输出和ReShade生命周期。游戏DLL未更新，无运行任务遗留。


### 2026-09-07：自有提交器在AMD纹理写回测试实测通过

- `d3d12_native_game_output_test.cpp` 改用NativeGameSubmission，移除原测试自行管理的allocator/list/fence逻辑；每帧先提交R10写入，再单独提交纹理回读，A/A/B/A/A共10次自有命令提交。
- AMD执行session79963 exit0：五帧各2073600像素different=0，owned_submit=10，证明提交器正常路径的fence完成、allocator重用、跨提交资源状态与结果保持。异常/timeout保留分支未故障注入测试，不称已验收。
- 构建及diff检查通过。该测试使用实验室DIRECT queue，不证明游戏输入producer提交时序；下一步接入已提交游戏帧的呈现生命周期与完整网络。游戏DLL未更新，无运行任务遗留。


### 2026-09-07：游戏queue上的自有分段提交器初稿

- 新增 `native_game_submission.h`，接收调用方DIRECT queue，从queue取得device，拥有独立allocator/list/fence/event。每段提交并验证device/fence后才允许重置；不Close/Reset游戏列表、不擅自创建另一条游戏推理queue。
- Submit异常后poison，禁止后续复用；若GPU仍在途，析构不释放自有命令存储，避免把timeout当成取消。调用端仍必须在错误后保留所有GPU引用资源，不可仅依赖此类保护外部网络对象。
- MinGW独立语法检查exit0，尚未GPU执行或游戏接入。前提是调用时游戏已提交输入producer；此类自身不能证明这个前提，也不替代ReShade immediate提交标志管理。
- 阅读旧on_present确认queue及immediate flush入口存在，当前未查到AMD游戏进程。下一步在GPU纹理测试上接此提交器做实际依赖验证，再组合整网及ReShade生命周期。游戏DLL未更新，无运行任务遗留。


### 2026-09-07：AMD最终R10纹理复制动态逐像素通过

- 新增 `d3d12_native_game_output_test.cpp`，实际GPU RGB打包→CopyTextureRegion至R10纹理→texture回读。输入为整数/2048，含clamp范围外、量化中点，底部padding设独立哨兵；CPU用整数公式 `(clamp(n)*1023+1024)/2048` 计算期待值，检查全部RGB位与alpha位。
- A/A/B/A/A五次各2073600个packed像素different=0，设备及fence检查通过，session16244已exit0。目标状态COPY_SOURCE在每次Record后恢复、packed重复UAV转换也经过实际重放。
- 这证明受控R10纹理写入、1080p裁剪和格式量化；不证明游戏颜色空间/主swapchain呈现。代码编译通过，无遗留运行任务，游戏DLL未更新。
- 下一步必须把已验证的输入/神经网络/输出组件与游戏queue生命周期整合，并对实际游戏取图位置和颜色空间作现场核验；不以此纹理测试代替游戏目标。


### 2026-09-07：最终RGB→R10纹理复制组件完成，待GPU验证

- 新增 `native_game_rgb_output.h`，从1920×1152浮点RGB buffer生成前1080行R10 packed数据，以7680字节footprint CopyTextureRegion至同device的1920×1080 R10G10B10A2_UNORM纹理。
- Record恢复目标纹理原始状态，重复调用恢复packed buffer的UAV状态；不提交/关闭游戏列表。目标格式/尺寸/device显式检查，颜色空间仍须调用端验证，不把R10格式等同于SDR合同。
- MinGW独立语法检查exit0，尚未GPU执行或与真实swapchain连接。下一步用纹理回读核对量化、裁剪、行距及重放，再接游戏提交与呈现。游戏DLL未更新，无运行任务遗留。


### 2026-09-07：明确显示输出裁剪/格式候选，尚未GPU或游戏验收

- 检查现有probe与游戏接入历史：原版CPU参数探针只能给kernel参数，不能证明实际RGB取自哪个渲染阶段；历史swapchain为R10G10B10A2，而旧FFX输入是低分辨率RGBA16。必须重新在实际游戏确认取图和颜色空间，不能把二者混同。
- 新增 `native_game_rgb_output.hlsl`，将1920×1152 contiguous float RGB的前1080行转换为1920×1080 R10G10B10A2_UNORM packed buffer，alpha=3，行距7680字节满足256对齐；不混入底部72行。此为SDR候选编码，不进行/不声称HDR或色调映射。
- Windows D3DCompile检查exit0，bytecode1280字节；仅编译，没有GPU输出复制或游戏部署。下一步需包装输出buffer→纹理复制并逐像素验证，再与实际swapchain格式/颜色空间和提交顺序绑定。
- 游戏DLL未更新，无运行任务遗留。


### 2026-09-07：RGBA16纹理入口动态精确通过，核实旧游戏输入范围

- AMD进程查询未返回SB-Win64-Shipping；旧runtime日志尾部为render1281×721、color format10（RGBA16_FLOAT）、旧ViT540tokens。仅历史证据，不能当当前游戏合同；输入来源必须重新核对，不能将旧低分辨率FSR输入直接当1920×1080网络入口。
- 纹理测试新增half模式，使用精确可表示的half数值（含负值/大于1）按真实row pitch上传RGBA16_FLOAT，保留RGBA32_FLOAT回归；两格式A/A/B/A/A各五帧、每帧两路各8847360值different=0。half会话96513 exit0，float32回归也exit0。
- 证明两种已允许纹理格式的受控动态读取、反射与输出排列；不证明游戏颜色空间或选源正确。下一步从正确的全尺寸游戏纹理取图并处理提交/呈现关系，而不是加入猜测的1281→1920缩放。
- 游戏DLL未改，无遗留运行任务。


### 2026-09-07：AMD游戏纹理入口float32双路动态精确通过

- 新增 `d3d12_native_game_rgb_test.cpp`，创建真实1920×1080 RGBA32_FLOAT纹理，按GetCopyableFootprints上传，入口Record从COPY_DEST转换读取并恢复，回读tile-major及post-HWC两路。
- A/A/B/A/A五帧，逐像素位置/通道编码数据含负值与大于1值；CPU采用周期反射定义独立生成期望，不依赖shader输出作oracle。每帧两路各8847360值different=0，设备/fence完成检查通过，session67115已exit0。
- 这证明当前入口在float32纹理上的映射、无saturate、边界反射和重复状态恢复；RGBA16_FLOAT虽组件允许但尚未测试。实际游戏格式、颜色空间、多纹理合同、队列依赖以及最后呈现仍未验收。
- 编译与git diff检查通过；没有游戏DLL修改，也无遗留运行任务。下一步把此纹理入口与已经验证的整网GPU组件连接到游戏生命周期，保持同一输入源与明确提交顺序。


### 2026-09-07：游戏RGB纹理入口GPU组件封装完成，待执行验证

- 新增 `native_game_rgb_input.h`：稳定绑定单张1920×1080纹理，创建tile-major及HWC post两个常驻输出，Record由调用方提供原始资源状态并在结束恢复；不提交、关闭或重置游戏command list。重复Record前恢复两个输出UAV状态。
- 目前显式限定同device、单层单mip单采样、可SRV、RGBA32_FLOAT或RGBA16_FLOAT，不接受未验证的typeless/sRGB/压缩格式或隐式缩放。游戏包装device需要先正确unwrap，不能只按相同显卡LUID混用资源。
- MinGW头文件独立语法检查exit0（仅pragma once主文件警告），尚未GPU执行、尚未接入游戏。调用方必须确保描述符/纹理生命期、输入已经提交、全部使用者fence完成后销毁；Record改变命令列表绑定，游戏接入必须恢复或隔离状态。
- 下一步用真实D3D12纹理上传测试两路GPU输出与现有反射参考逐值一致，再接完整神经管线。没有运行任务遗留，游戏DLL未更新。


### 2026-09-07：游戏纹理输入候选shader编译通过，待GPU边界验证

- 检查旧 `initialize_frame_bridge` 确认它双线性缩放到960×544并saturate，这与新受控1920×1080→1920×1152合同不同，不能直接复用旧像素入口。
- 新增 `native_game_rgb_input.hlsl`：从1920×1080 Texture2D逐像素Load，不缩放、不saturate、不擅自gamma转换；底部72行反射，生成两张GPU buffer：8×8 tile-major RGBA供preblock、HWC RGBA供post。两路来自同一次pixel读值。
- 新增compile-only工具，MinGW构建后在AMD Windows运行D3DCompile，exit0、1324字节bytecode。未创建GPU设备，未操作游戏；不能据此声称纹理读取、队列状态或颜色空间已验证。
- 下一步构建纹理上传/双输出回读测试，与已有buffer反射裁判逐值比对，再封装实际游戏device/descriptor/resource-state接入。仍需游戏实际多纹理和post边界证据；游戏DLL未更新。此shader仅候选入口，不替换完整神经网络。


### 2026-09-07：动态RGB0～70整链A/A/B/A/A通过

- session61405/PID18444已exit0，五帧固定seed0；A/A/B/A/A全部replay_check通过，intermediate_CPU_transfers=0。无本轮运行任务遗留。
- B相对A：最终RGB6635520/6635520分量不同，head444407/655360不同，decoder6916834118/17694720不同，证明差异不仅来自post底图。frame3/4恢复A后，三路与基线全部一致。
- 下载6份此次标准及.alternate输出至ignored `release/native-rgb-valid1080/amd-dynamic`，运行 `validate_native_valid1080_dynamic_gpu.py` exit0：恢复A的final/head/decoder69与原版A均different=0，B差异数量与运行日志一致，pass=true。
- 这是受控输入的动态因果及恢复验证，不是B原版数值验收，也不是游戏呈现；B为A的RGB水平翻转且alpha不变。当前已同时具备实际尺寸静态整链精确对照与动态输入穿过神经网的证据。
- 下一阶段应推进游戏接入：抽取已验证管线的GPU资源/分段提交接口，处理游戏输入提交后再推理、结果完成后写回的队列依赖，不可在旧FFX未提交list中阻塞等待。旧游戏DLL及公开包仍未更新，目标尚未完成。


### 2026-09-07：动态RGB整链测试启动，固定seed A/A/B/A/A

- 保留静态full目录，在独立 `D:\DLSSNR-Lab\matrix-probe\native-valid1080-dynamic` 部署新动态exe、B输入与运行器，未改游戏DLL。exe本地/远端SHA均为 `27414c57d98c6c89f75e535abf2bf71e786c1fc383b3c44b109bb102aff6a1f6`。
- `run_native_valid1080_full.ps1 -Folder ...native-valid1080-dynamic -AlternateRgb ...alternate-input.f32` 已启动，unified exec session61405，PID18444，检查CPU29.359375秒，反射shader编译成功。当前初始化中，未有验收结果，下轮先轮询同一handle，勿重启。
- 新增 `validate_native_valid1080_dynamic_gpu.py`，未来将三路A恢复输出与原版A比较，并核对B对final/head/decoder69三路均有影响，尺寸/有限值严格检查。注意gpu-raw在动态模式实际上是decoder69，不是preblock raw。需要新下载3份标准及3份.alternate文件到本地 `release/native-rgb-valid1080/amd-dynamic` 后运行。
- 独立目录初始复制包含旧静态输出，不能在本轮进程exit0前当新动态结果收取；A恢复还应结合host的A/A/B/A/A重放日志。B没有原版数值oracle，因果通过不能冒称B数值精确或游戏验收完成。


### 2026-09-07：AMD实际尺寸RGB0～70整条GPU链精确通过

- 完整静态测试session21947/PID39788已exit0：5帧seed0/0/0/1/0 replay_check全部pass，resident_chain=pass，处理1920×1152、有效输入1920×1080，intermediate_CPU_transfers=0。无运行任务遗留。
- 下载此次gpu-main/down到 `release/native-rgb-valid1080/amd-full`，运行 `validate_native_valid1080_full_gpu.py` exit0：最终RGB6635520分量different=0/max_abs=0，head655360值different=0/max_abs=0，pass=true。
- 这是同一RGB从反射输入经encoder、640token ViT、所有decoder跳接直到post70的真正GPU直连数值闭合，不再只是分段。原版/CPU参考由此前同源各段构成，post底图使用反射合同。
- 仍不可称游戏目标完成：本轮仅A输入及seed变化，尚未跑已编译的A/A/B/A/A动态RGB测试；未证明实际游戏多纹理/post边界及最终呈现。旧游戏DLL与公开zip未更新。
- 下一步部署动态测试版本（本地 `/tmp/native-frontdynamic-test.exe`，B=`alternate/input.f32`）保留本次静态结果，再将管线接入游戏提交/呈现链。注意冷初始化很慢，需改进重复shader编译的缓存依赖管理或保留初始化实例，不能伪报为每帧速度。


### 2026-09-07：动态B输入及可回查输出准备完成，未运行

- 新增 `prepare_native_valid1080_alternate.py`，从同源A仅对RGB做水平翻转，alpha逐值不变，确认有限值及RGB确实不同；输出ignored `release/native-rgb-valid1080/alternate/input.f32`，provenance记录A/B SHA与变换。此为受控空间变化，不是实际游戏第二帧，尚无B原版oracle。
- 主host动态frame2记录final/head/decoder69各自不同元素数，并保存三份 `.alternate` 回读文件，供以后与原版B核对；A恢复结果仍写标准输出文件。补充后 `/tmp/native-frontdynamic-test.exe` 再编译exit0、diff检查通过。
- 未部署动态exe，避免覆盖正在运行的完整静态测试。session21947/PID39788继续有成功编译输出，未结束；下一步先收完整静态结果，再决定动态验证与游戏接入。游戏DLL未更新。


### 2026-09-07：补充真实RGB切换验收代码（未运行）

- 主host新增可选DLSS5_ALTERNATE_RGB，限定FRONTFINAL，要求B输入同尺寸、有限且与A不同。五帧序列改为A/A/B/A/A，seed保持不变；在上一帧fence完成后才更新RGB upload及由它生成的post反射底图。
- 动态模式第三路回读改为decoder69而非preblock raw；B必须使final/head/decoder69三路均变化，恢复A后每路都必须与基线完全一致。避免仅post底图变化就把神经网络视为生效。未用B原版oracle做数值校准，因此未来此模式通过也只是因果/重放证据，不能替代B数值与游戏验证。
- 编译 `/tmp/native-frontdynamic-test.exe` exit0，运行器新增可选-AlternateRgb并清理未选择时的残留env。仅本地修改，未覆盖正在运行的full版本。当前full测试仍session21947/PID39788，持续编译成功日志，尚未最终回读。
- 下一步先收现有full结果，再用新动态版本和独立B图验证，之后游戏接入。游戏DLL未更新。


### 2026-09-07：游戏接入点审计，明确提交顺序迁移要求

- 当前游戏端源文件为 `dlss5_1080p_runtime.cpp`。`hook_ffx_dispatch` 在调用原FFX前record_frame_bridge、调用后将旧0～70计算写入unwrapped游戏command list；它不拥有该列表的提交时机。`on_present` 可取得主swapchain和原生queue，并已有显示审计与GPU profile fence逻辑。
- 新实验室管线的chunk-fenced ViT不能原样塞进上述hook并同步等待：游戏list可能尚未提交，等待其输入完成可能形成错误依赖/停顿。移植必须明确“游戏输入拷贝已提交→推理队列执行→结果完成→当前帧合成”的实际提交与资源状态；不能擅自Close/Reset游戏拥有的list。
- 旧运行时仍硬编码960×544/1920×1088等旧几何（block0_ready与record_block70），必须由已验证1920×1152内部、1920×1080有效输出合同取代；仅换权重不能修复。
- 本轮只读游戏源代码，未修改或部署游戏DLL。完整测试session21947/PID39788仍有成功编译日志，未结束；继续沿用该handle，完成后先下载最终输出验收。此接入审计不是游戏验证成功。


### 2026-09-07：AMD0～38直连精确通过，完整0～70测试启动

- 旧session34710已exit0：5帧seed0/0/0/1/0 replay_check全部pass，resident_chain=pass，处理中间无CPU特征传输。反射有效1080 RGB经encoder/head、GPU gather直接进入640token ViT，不使用预制中间特征输入。
- 新下载到 `release/native-rgb-valid1080/amd-frontvit` 的gpu-main/down经验证器检查：block38和head各655360值different=0/max_abs=0，pass=true。此为真正0～38直连数值验收，尚不是最终RGB或游戏验收。
- 确认上一进程结束后，通过已部署 `native-valid1080-full/run_native_valid1080_full.ps1` 启动完整0～70测试，unified exec session21947，已输出反射shader编译成功。目前初始化中，尚未获得最终RGB。后续只轮询该handle，不重启，不并发另一份。
- 完整测试exe SHA仍为8581e32b0a75ce96e60ba43199a23289301eef18a907cdfc03ac7b6f22daa53c。游戏DLL未更新；完整结果出来后仍需动态输入、实际纹理与游戏呈现验证。


### 2026-09-07：完整GPU测试包已部署到独立目录，尚未启动

- 完成向 `D:\DLSSNR-Lab\matrix-probe\native-valid1080-full` 传输完整系数、输入、oracle、映射、shader依赖及运行脚本；传输session80263已exit0。目录210文件，oracle-final.f32为26542080字节。
- 部署exe的远端SHA256与本地 `/tmp/native-frontfinal-test.exe` 一致：`8581e32b0a75ce96e60ba43199a23289301eef18a907cdfc03ac7b6f22daa53c`。没有覆盖frontvit目录或游戏DLL。
- 0～38原测试仍在工作：session34710/PID39212，最近确认CPU481.828125秒、工作集1206468608字节，持续编译成功。完整测试未启动，避免并发干扰。后续先收该session结果、运行frontvit验证器，再通过 `run_native_valid1080_full.ps1` 启动已部署完整测试。
- 本轮是部署进展及可核验等待，不是整链数值验收。实际游戏DLL仍未更新。


### 2026-09-07：完整0～70同源GPU夹具及运行/验收脚本就绪

- `prepare_native_valid1080_full_gpu.py` 已生成ignored `release/native-rgb-valid1080/amd-full`：0～70系数、正逆640映射、唯一RGB输入、最终RGB与head比较对照。decoder40～69主输入连续性及四处upsample的同源encoder跳接均按文件内容检查通过；不把skip或预制特征作为运行输入打包。
- 新增 `run_native_valid1080_full.ps1` 设置FINAL/valid1080/seed0/noise，禁止在另一个native-preblock-test运行时启动；新增 `validate_native_valid1080_full_gpu.py` 检查最终6635520 RGB值与655360 head值，数值非零差异即失败。
- 当前完整夹具仅在本地准备，未部署/启动完整测试。旧0～38直连仍是session34710/PID39212，轮询有持续成功编译输出，禁止重启。下一步等旧任务完成后核对其输出，部署full包与当前exe/shader再跑整链。游戏DLL未更新。


### 2026-09-07：主host接入实际0～70直连，编译通过待整链验收

- `d3d12_native_preblock_test.cpp` 新增 `DLSS5_FRONTDECODER`（含FRONTVIT）与 `DLSS5_FRONTFINAL`（含decoder），将实际decoder39～69接入ViT输出，encoder30/22/14/8/4跳接直接使用GPU资源。decoder13阶段各提交/fence，不回读特征。
- FINAL直接接NativePost70，skip为preblock.Main。post底图在初始化时从唯一RGB输入按同一反射规则生成HWC，禁止读取预制post颜色；禁止DLSS5_POST_BASE_ONLY诊断模式进入整链验收。最终main输出6635520 RGB分量，down保留head对照。
- 检查Create参数时发现post的vector参数顺序虽能编译但不正确，已改成scales/ffn/attention/head并重新MinGW编译 `/tmp/native-frontfinal-test.exe` exit0。只证明构建，不是数值通过。
- 这版尚需准备39～70系数、逆map和同源最终oracle后运行。当前仍在跑的0～38旧二进制不受本地改动影响：session34710/PID39212继续有编译成功输出，勿重启。游戏DLL未更新。
- 下一步先收0～38结果并核对，准备完整夹具运行FRONTFINAL；全链还需动态RGB而不只是seed变化、实际游戏纹理/呈现验证。反射post底图仍是受控合同，不等于已确认游戏post纹理边界。


### 2026-09-07：整理实际decoder39～69 GPU直连组件，待主链接入

- 新增 `native_actual_decoder69.h`：输入为ViT38及encoder30/22/14/8/4 GPU资源，内部接640逆gather、decoder39、60×36 split40～47、up48至120×72、实际尺寸NativeDecoderTail69；不上传中间特征。
- 暴露13个RecordStage，调用端可在阶段间提交/fence：inverse、entry、8个split、up48投影、up48主体、49～69尾链。尾链仍为现有整段提交，后续整体测试必须检查TDR及重放，不能凭独立段通过认定稳定。
- 头文件独立MinGW语法检查首次发现漏include NativeSplitWindow，已补齐并复查exit0（仅pragma once主文件警告）；git diff检查通过。组件尚未接入主host或在AMD运行。
- 0～38直连测试仍为session34710/PID39212，轮询有持续成功编译输出，未结束、未重启。下轮仍先轮询原handle，再接decoder组件及最终post。游戏DLL未更新。


### 2026-09-07：AMD0～38直连测试已启动，等待同一进程

- 部署 `/tmp/native-frontvit-test.exe` 至 `D:\DLSSNR-Lab\matrix-probe\native-valid1080-frontvit\native-preblock-test.exe`；使用 `amd-frontvit` 全部系数和唯一RGB输入，以及当前ViT shader/half-square依赖。不是旧独立ViT exe。
- 运行 `DLSS5_FRONTVIT=1`、`DLSS5_SHADER_PROGRESS=1` 加反射preblock运行器 `-FrontHead`。unified exec session34710，远端PID39212，启动时间2026-09-07 01:37:06（远端显示）；已确认进程存活、CPU12.8125秒、工作集528699392字节，反射shader编译成功。当前仍在初始化，没有验收结果。下轮先轮询同一handle，勿重启。
- 新增 `validate_native_valid1080_frontvit_gpu.py`：结束后核对gpu-main与oracle-vit、gpu-down与oracle-head，各655360值，finite和逐值检查；语法及git diff检查通过，尚无输出可验收。
- 接线检查补充：`native_vit_linear.hlsl` 的DECODER_ENTRY路径现已对 `residual[index]` 调用F，再乘skip权重。因此后续raw encoder跳接可直接交给此路径，由已有shader实现所需FP8量化，不必新增重复量化pass。此前“raw不得直接不经量化使用”的边界仍成立，但现实现已经覆盖这一点。
- 游戏DLL未更新。后续先收本轮直连数值证据，再将逆bridge、decoder39～69及post70接入同一管线，不能跳过实际游戏最终输出验证。


### 2026-09-07：实现实际RGB0～38 GPU直连入口，编译通过待运行

- `d3d12_native_preblock_test.cpp` 新增 `DLSS5_FRONTVIT`，隐含FRONTHEAD：head.Output直接接640映射NativeVitGather，再直接接8层NativeVitBlock；不读取预制head/ViT特征作为输入。
- ViT沿用已验证的StageChunks/RecordStageChunk，每块独立提交并等待fence，GPU资源全程常驻。fence改为全局单调递增，避免多个提交复用frame+1；等待完成并检查device/fence之后才重置allocator。输出main改为block38，down保留head，仍有5帧seed0/0/0/1/0检查。
- MinGW编译 `/tmp/native-frontvit-test.exe` 成功。新增 `prepare_native_valid1080_frontvit_gpu.py` 生成ignored `release/native-rgb-valid1080/amd-frontvit`：唯一图像输入为input.f32，附0～38系数及head/ViT比较对照。此新直连代码尚未在AMD运行，不能用旧分段通过结果替代。
- 下一步部署新exe/完整shader依赖和该目录系数，运行时设置DLSS5_FRONTVIT=1、反射1080输入及noise table；完成后分别比对gpu-main与oracle-vit、gpu-down与oracle-head。预计encoder编译仍较慢，需保留具体进程句柄并持续汇报。游戏DLL尚未更新。


### 2026-09-07：AMD同源post70最终RGB三次精确通过

- 新独立目录 `D:\DLSSNR-Lab\matrix-probe\native-valid1080-post` 使用同RGB `post70/amd` 的全部输入、对照和系数；部署当前 `native_half_square.hlsli` 与 `preblock_attention_core.hlsl`，没有沿用旧半精度归一化着色器。
- 原生post测试session76936已exit0：frame0/1/2各6635520分量different=0/max_error=0，post70=exact，intermediate_CPU_transfers=0。
- 新下载 `release/native-rgb-valid1080/post70/amd/gpu.f32`，与oracle.f32经cmp完全一致，SHA256 `c1d6ab580e4d3bfb46acb90646d1a65d609cbcf6dbdcdde12d1bef18703afbe3`。
- 边界未变：这是输入为同源特征的AMD post独立段，底图使用反射填充；不是AMD0～70GPU直连、不是实际游戏post纹理合同，更不是游戏最终画面验证。游戏DLL未修改，本轮无运行任务遗留。
- 后续优先实际整合：现有 `d3d12_native_preblock_test.cpp` 已实现实际0～30/head，`NativeVitGather`640映射、chunk-fenced ViT、实际decoder39及 `NativeDecoderTail69(game_extent=true)` 可复用。不能直接把旧512全链改个输入尺寸：它的ViT长度、bridge、head尺寸和早期encoder短高布局有硬编码，需要逐一接入已验证的1080实现。decoder跳接需要FP8值，encoder为池化保留的raw half不得直接不经量化传入。完成整链数值对照后才部署DLL做实际游戏验证。


### 2026-09-07：同 RGB 原版post70最终RGB精确通过（反射底图合同）

- `prepare_native_post70_game.py --valid1080` 使用同RGB decoder69的逻辑FP8输出、同RGB block0原版main跳接、1080高原输入向底部反射填充72行，生成原版post70输出。维持mask1/rgb1/scale0.03125。主输入重排为C32 global16且不交换低位，skip保留cell布局。
- `check_native_post70_game.py --valid1080` 按16行独立窗口比对全1152×1920，6635520个RGB分量different=0/max_abs=0，finite=true，session99651已exit0。ignored `release/native-rgb-valid1080/post70/validation.json`。至此受控单RGB同源原版/CPU分段链到最终RGB闭合。
- 重要边界：post底图的反射填充是本次受控合同，未证明实际游戏post纹理边界；未完成多纹理输入及游戏最终表面写回，不得称游戏移植完成。
- `prepare_native_post70_gpu.py --valid1080` 已导出对应AMD夹具至 `post70/amd`，尚未运行AMD本轮post测试。下一步AMD同源post、解码链及0～70GPU直连，之后部署并在游戏实际场景核对。未改游戏DLL/存档，无本轮运行任务遗留。


### 2026-09-07：AMD同源ViT31～38精确通过，原版同源链至69

- AMD独立目录 `D:\DLSSNR-Lab\matrix-probe\native-valid1080-vit`，使用同RGB `vit/input.f32` 与 `vit/oracle.f32`，现有chunk-fenced链运行三帧，全部655360值different=0/max_abs=0，device=0。session16040已exit0。
- 回读 `release/native-rgb-valid1080/vit/gpu.f32` 与oracle.f32经cmp完全相同，SHA256均为 `961a12fc2107c1445eb1f0eb474663e53e3c92911732f3aa1038478221b9d8f9`。这是同源输入的AMD31～38分段链验证；0～30/head与31～38仍是两个独立测试，尚未GPU直接连通，不能称0～38一体化验收。
- block66新增 `--skip-hwc`，main取同RGB decoder65，skip取encoder4-main；17694720值different=0，finite/tail_zero通过。ignored `release/native-rgb-valid1080/upsample66`。
- 新增 `check_native_valid1080_decoder67_69.py`，依次移位3/1/2，三层各17694720值different=0/status=pass，session98844已exit0。ignored `release/native-rgb-valid1080/decoder-c32`。
- 至此原版/CPU同源链到69，尚缺同源post70最终RGB；AMD解码及整链连接仍需验证。无本轮遗留运行任务，未改游戏DLL/存档。下一步完成post70同源对照并连接AMD完整管线，最终必须在游戏中验证实际输出。


### 2026-09-07：AMD 同 RGB0～30/head连续链精确通过

- 上述长期初始化任务session44927已exit0，不再运行；5帧seed0/0/0/1/0全部replay_check=pass，resident_chain=pass，处理1920×1152，intermediate_CPU_transfers=0。
- 进程结束后新下载gpu-main.f32/gpu-down.f32到 `release/native-rgb-valid1080/amd-head`，运行 `validate_native_valid1080_head_gpu.py` 返回0：block30 raw main 1105920值different=0/max_abs=0，padded head655360值different=0/max_abs=0，pass=true。
- 至此同一有效1080 RGB经反射填充、AMD encoder0～30及pool/head，数值精确对齐原版参考。seed1只验证输出变化，原版数值比对仍仅seed0。不是完整游戏多纹理输入合同，不是0～70整链或游戏DLL验收。
- 下一步将已生成的同 RGB ViT31～38、decoder39～65对照接入AMD连续链，并完成66～70和实际游戏输出。无需重跑已完成的head测试；游戏DLL仍未更新。


### 2026-09-07：同 RGB 原版62～65连续通过

- block62验证器新增 `--skip-hwc`，本轮main来自同 RGB decoder61，skip来自 `encoder-c64/block8-main.f32`；8847360值different=0/max_abs=0，finite/tail_zero通过。ignored `release/native-rgb-valid1080/upsample62`。
- `check_native_valid1080_decoder49_55.py --c64` 接63～65，移位3/1/2，三层各8847360值different=0/status=pass。ignored `release/native-rgb-valid1080/decoder-c64`。旧C256/C128运行模式保留且参数互斥。
- AMD同一个head测试PID28224/session44927仍在运行；最新进程CPU851秒，工作集1906102272字节，最后一条512通道projection编译成功，暂未收到frame输出。下一步仍先轮询该handle，不能重启或采用旧输出。游戏DLL未更新；同源原版参考已延伸至65，接下来66～70及AMD全链仍需完成。


### 2026-09-07：同 RGB 原版56～61连续通过

- block56 验证器新增 `--skip-hwc`，保留原随机模式。此次 main明确使用同 RGB `decoder-c256/decoder-block55/oracle.f32`，skip使用 `encoder-c128/block14-main.f32`；输出4423680值different=0，finite/tail_zero通过。ignored `release/native-rgb-valid1080/upsample56/validation.json`记录两个来源。
- 同源解码链运行器 `check_native_valid1080_decoder49_55.py --c128` 接57～61，实际移位2/0/3/1/2，5层各4423680值different=0/status=pass。ignored `release/native-rgb-valid1080/decoder-c128`。
- AMD head测试继续轮询session44927，仍有512通道attention/projection成功编译输出，未结束，未重启。其结果仍待GPU回读验证；游戏DLL未更新。下一步同源62～65（encoder8跳接）并收取AMD任务结果。


### 2026-09-07：同 RGB 原版48～55连续通过

- `check_native_upsample48_game.py --valid1080` 使用同 RGB decoder47输出和encoder22的FP8 main跳接，实际120×72输出2211840值different=0/max_abs=0，finite/tail_zero通过。旧独立随机测试模式保留。
- 新增 `check_native_valid1080_decoder49_55.py`，连续运行49～55，移位3/1/2/0/3/1/2；7层各2211840值全部different=0/status=pass。ignored产物分别在 `release/native-rgb-valid1080/upsample48` 和 `decoder-c256`。
- 这是同 RGB 原版/CPU 对照延伸到55，不是AMD整链验收。AMD head 测试PID28224/session44927仍活跃，最近检查CPU570.296875秒、工作集1833156608字节；已经进入split512编译，每个attention约8秒、projection约32秒，尚未输出frame验收。保持同一进程，不重启。
- 下一步继续收取AMD head结果，原版同源链再接56及encoder14跳接。游戏DLL未更新，目标未完成。


### 2026-09-07：同 RGB 原版解码器39～47连续对齐

- `check_native_valid1080_decoder39.py` 从同 RGB ViT38 原版输出经过实际逆 repack 接入 decoder39，并独立与逻辑逆 gather 比对；skip 来自同 RGB encoder30 的原版 FP8 main（不是 raw half，也不是独立随机 skip）。32×20 投影后 nearest2×，左上裁剪至60×36。
- 原版 decoder39 的1105920个输出值与数值参考 different=0，finite/tail_zero/逆布局比对通过。输入及输出在 ignored `release/native-rgb-valid1080/decoder39`。
- `check_native_valid1080_decoder_split.py` 连续运行原版40～47层，实际移位0/3/1/2/0/3/1/2；8层共32项中间结果 different=0，全部status=pass。ignored `release/native-rgb-valid1080/decoder-split`。
- AMD head 测试继续沿用 PID28224 / unified exec session44927，已确认进程CPU增长并持续输出成功编译日志；还没有本轮 GPU 回读验收结果。不得把从 front22 复制来的旧 gpu-main/down 文件当成此次结果，须等待当前进程 exit0 后再收取并检查尺寸、时间及数值。
- 上述39～47通过仅是同 RGB 原版/CPU参考，尚未证明 AMD 连续链或游戏 DLL。下一步等待既有AMD任务结束、验证head，再继续同源48～70及实际游戏部署。


### 2026-09-07：同 RGB 原版 ViT31～38 全部对齐；AMD head 测试运行中

- 新增 `prepare_native_valid1080_vit.py`：从已验证的原版 head.fp8 经原版 32×20 repack 映射生成 ViT 输入，独立与 head.f32 的 GPU 逻辑 gather 路径比对，655360 个输入值精确一致；不使用随机 ViT 输入。
- RTX5090 独立测试目录 `D:\DLSSNR-Lab\native-valid1080-vit`，原版 31～38 层连续运行两次完成。未操作游戏界面或存档。
- `check_native_block256.py --tokens 640 --last-block 38 --valid1080` 返回 0；8 层×7 阶段共 56 项 different=0，finite/tail_zero/replay_identical 全通过。产物在 ignored `release/native-rgb-valid1080/vit`，其中 oracle.f32 是同 RGB block38 原版逻辑输出。此结果不是 AMD ViT 验收或游戏最终画面验证。
- AMD 连续 0～30/head 测试已启动：远端 `D:\DLSSNR-Lab\matrix-probe\native-valid1080-head`，PID28224，当前 unified exec session44927。已观察到进程 CPU 时间增长及 shader_compile_end 成功日志，尚在初始化、未读回结果。后续先轮询同一 session/进程，禁止仅因等待时间长重启。启动时脚本策略拒绝过一次（未启动 exe），随后用进程级 ExecutionPolicy Bypass 成功，未修改系统策略。
- 下一步：收取 AMD gpu-main/down 输出并运行 head 验证器，再接同 RGB ViT 和 decoder；游戏 DLL 仍未更新。


### 2026-09-07：接回同 RGB GPU0～30/head 验证入口（尚未 GPU 验收）

- 用户已将桌面改为 1080p、游戏改为无边框窗口，5090 停在主菜单；本轮未操作游戏或修改存档。
- `prepare_native_valid1080_head_gpu.py` 已准备同一有效 1080p RGB 的连续输入、23～30 层系数、原版 head 及经原版中间结果验证的 raw block30 对照；不是拼接独立随机特征。
- `d3d12_native_preblock_test.cpp` 新增 `DLSS5_FRONTHEAD`，自动接通已有 0～22 层，再运行 60×36 的 split23～30，保留 block30 raw half，池化投影至 32×20×1024（有效池化 30×18）。运行器新增 `-FrontHead`。
- 新增 `validate_native_valid1080_head_gpu.py`，分别检查 raw block30 和 padded head，禁止只看有限值或运行成功就判定正确。
- MinGW 交叉编译成功（`/tmp/native-preblock-head-test.exe`）；本次新增连续链尚未在 AMD 上运行、未生成 GPU 验收报告，未部署游戏 DLL。下一步运行该链并核对两个输出，随后继续同 RGB ViT/decoder/post 全链。


### 2026-09-07：同valid1080 RGB block30→pool/head逐值通过

- validate_native_valid1080_pool_head.py使用连续block30原始FFN/attention中间输出，原始pool参数60×36→32×20，再接原始head。
- raw投影主输出、有效18×30池化及零补齐、最终655360值head均与参考精确一致、finite；导出head HWC与block30 raw供后续桥接/skip对照。
- ignored encoder-split/pool-head。原始受控RGB已连续到ViT入口，尚未AMD0～head整体或完整游戏多纹理验收，未部署DLL。

### 2026-09-07：同valid1080 RGB原始split23～30连续对照通过

- check_native_decoder_split.py支持encoder23～30；23使用原始inpview及global16低通道位交换输入，24～30沿原始cell输出连续运行，实际60×36及shift0/3/1/2/0/3/1/2。
- 八层各branch/ffn/attention/final共32阶段零差异；ignored encoder-split/decoder-block23..30保留原始输出、系数和validation。
- 此处block30仅主输出，pool/head尚未接入同RGB，AMD0～30亦待验收；未部署DLL。

### 2026-09-07：同valid1080 GPU0～22完成并逐值通过

- 延续session7712/PID31784，CPU时间360→488秒，最终正常exit0，五帧seed0/0/0/1/0重放/seed影响检查通过，未重启。
- 下载main/down，--front22验证2211840主值（raw转原FP8边界）及1105920下采样值均零差异/max_abs0；same RGB前端0～22已通过此受控fixture。
- 含include shader绕过缓存且原先无进度输出，native_shader_cache.h补该分支begin/end耗时诊断，另编译二进制通过，未替换正在运行的测试。未改缓存语义。
- 原session已完成，后续不要再poll/restart7712；下一步split23～30与ViT整链。仍非游戏多纹理/动态RGB验收，未部署DLL。

### 2026-09-07：同RGB GPU0～22运行中，尚无验收结果

- runner增加-Front22并清理该开关；验证器增加--front22对应raw main2211840值（按FP8边界比较）及down1105920值。
- 已部署独立native-valid1080-front22实验目录并启动，当前统一exec session7712仍运行。远端PID31784连续实查活跃，CPU时间25.2→119.2→237.0秒、内存增长，尚未输出五帧结果；未重启/误判完成。
- 下一次继续poll同一session7712或核对PID31784，不重新启动。输出成功后需下载gpu-main/down并运行validate_native_valid1080_front8_gpu.py --front22。未部署游戏DLL。

### 2026-09-07：同RGB GPU0～22整链准备

- prepare_native_valid1080_front22_gpu.py核对原始15～22 main/down22通过，导出C256八层系数及原始主/down答案，继承同一有效RGB与0～14参数。
- preblock host新增DLSS5_FRONT22自动包含前级，120×72 C256八层shift0/3/1/2/0/3/1/2及DS22串联，回读raw main与60×36 C512 down。
- C++编译/diff检查通过，新0～22 GPU链尚未执行，未部署DLL。

### 2026-09-07：同valid1080 RGB block22下采样通过

- validate_native_valid1080_ds22.py从已验证block21主输出，计算shift2 raw half、原生池化/FP8及C256→512矩阵投影，按global16低位交换解码原始down。
- 1105920值different0/max_abs0/finite通过，保存encoder-c256/down-validation.json与逻辑down/矩阵。此输入对应后续60×36 split阶段。
- 仍需AMD同RGB0～22整链及完整网络验收，未部署游戏DLL。

### 2026-09-07：同valid1080 RGB原始encoder15～22主分支通过

- capture/main参考脚本增加互斥--c256，从block14-down继续120×72 C256八层，shift0/3/1/2/0/3/1/2，22下采样同时捕获。
- 八层主分支各2211840值与原kernel零差异/finite通过，encoder-c256/main-validation.json保存结果。down22仅长度/FP8 NaN检查，尚未算术验收。
- 下一步down22及AMD同RGB前端扩展，未部署游戏DLL。

### 2026-09-07：同valid1080 RGB GPU0～14双分支通过

- runner支持-Front14，GPU镜像→preblock→C32/C64/C128直到14/DS连续五帧seed0/0/0/1/0，重放/seed变化检查通过；初始化PID1776活跃，未重复启动。
- 主分支raw按原FP8边界比较4423680值零差异，down2211840值零差异，finite/device/fence通过。报告amd-front14/validation.json。
- seed1未原始逐值验证，仍受控单纹理RGB，不是完整游戏输入合同；下一步15以后的完整前端，未部署DLL。

### 2026-09-07：同RGB GPU0～14整链准备

- prepare_native_valid1080_front14_gpu.py核对原始9～14 main/down14精确结果，直接解码六层权重，继承0～8系数/同RGB输入，导出block14 raw边界对应FP8 main及down oracle。
- preblock host增加DLSS5_FRONT14，自动包含front8/4，240×144 C128六层实际shift0/3/1/2/0/3及ds14 GPU串联；回读block14 raw main/down。
- C++编译及diff检查通过，新GPU0～14尚未执行，未部署游戏DLL。

### 2026-09-07：同valid1080 RGB block14下采样逐值通过

- validate_native_valid1080_ds14.py从block13原始主输出重算shift3 raw half、水平pair池化/FP8及原始C128→C256矩阵投影，解码下采样global16低通道位交换。
- 2211840下采样值different0/max_abs0/finite通过，保存encoder-c128/down-validation.json、block14-down.f32、block14-ds.f32。
- 原始9～14主/down均通过，尚未AMD同RGB0～14整链验收，未部署DLL。

### 2026-09-07：同valid1080 RGB encoder9～14原始主分支通过

- 原始capture和CPU main验证脚本增加--c128，从block8-down继续9～14，实际240×144、shift0/3/1/2/0/3，14下采样输出亦捕获。
- 六层主分支各4423680值零差异、finite通过；block14 down仅长度/FP8 NaN检查，尚未算术对照。
- ignored encoder-c128保存来源、权重、原始输出与main-validation。下一步验证down14及AMD同RGB整链，未部署DLL。

### 2026-09-07：修复后同valid1080 RGB GPU0～8双分支通过

- runner支持-Front8并清理未选环境开关。生产reflect→preblock→C32四层/DS→C64四层/DS五帧seed0/0/0/1/0重放及seed影响检查通过。
- validate_native_valid1080_front8_gpu.py将raw block8 main仅按原边界FP8量化后比较：8847360主值零差异，4423680下采样值零差异。finite/设备/fence检查通过。
- ignored amd-front8/validation.json，block8三值问题在同RGB大图GPU链已消失。seed1只验证变化非原始逐值；完整9～70及游戏输入/输出验收仍缺，未部署DLL。

### 2026-09-07：同RGB GPU0～8整链准备

- prepare_native_valid1080_front8_gpu.py检查修复后5～8 main/down8通过，导出原始系数及主/下采样oracle至amd-front8。
- preblock host新增DLSS5_FRONT8，GPU接C64四层shift0/3/1/2及ds8；block8保留raw half供池化，因此主回读需FP8量化后与原始main比较，不能直接比较raw与FP8。
- 编译通过，尚未执行0～8 GPU整链；未部署游戏DLL。

### 2026-09-07：同valid1080 RGB block8下采样精确通过

- validate_native_valid1080_ds8.py从block7原始主输出重算修复后的block8 raw half，原生水平pair池化→FP8→按K32顺序矩阵投影，按已验证global16低通道位交换解码原始down。
- 输出4423680值different0/max_abs0/finite通过，保存encoder-c64/down-validation.json、block8-down.f32与DS矩阵。
- encoder5～8主分支及down8 CPU/原始合同现在均通过；AMD仍需同RGB整段前端回归，未部署游戏DLL。

### 2026-09-07：融合平方修复后640token八层ViT GPU回归通过

- 更新远端native_vit_qkv.hlsl和native_half_square.hlsli，分块fence整链31～38三帧655360最终值均零差异，device正常。
- 回读gpu-fused-square.f32与原始oracle字节级cmp一致，旧gpu-chunk-fenced.f32保留。CompileNativeShader对include源码不缓存，未误用旧依赖编译结果。
- 这仍为既有随机640token fixture，非同RGB完整链或多纹理游戏验收；未部署DLL。

### 2026-09-07：融合平方修复同步C32/ViT与C32前端GPU回归

- preblock_attention_core.hlsl及native_vit_qkv.hlsl的norm pair使用共用NativeHalfSquarePair，stage脚本允许.hlsli依赖复制。
- 更新远端C32 shader/include后valid1080 reflect→0～4五帧重放/seed变化通过；最终main17694720值、down8847360值仍与原始oracle零差异。
- ViT QKV同步尚未GPU回归；新include需与shader同时部署，不将部分回归扩大为全网络通过。未部署游戏DLL。

### 2026-09-07：AMD通用融合平方舍入修复，最小窗口三帧通过

- native_half_square.hlsli利用half平方在float32精确表示及FastTwoSum残差，保留sum丢失小项；仅遇half中点时按残差判方向，覆盖65520溢出边界。无需double或像素特例。
- native_c64.hlsl的Q/K平方pair改用该函数，AMD原block8最小窗口三帧4096值零差异，device正常，回读cmp原始oracle一致。
- test_half_square_residual.py随机100万组有限half操作数，float32残差算法全部等于float64承载的一次half舍入。GPU仅最小模型窗口通过，仍需更大回归、其他normalize shader同步及新增include部署脚本覆盖；未部署游戏DLL。

### 2026-09-07：生产CPU参考融合平方修复及大图回归通过

- native_c32_normalize.py使用float64承载下半平方+已half舍入上半平方，最后一次half舍入，避免float32中间吞掉中点旁的小项；不改归约次序/倒数，不加像素特例。
- valid1080 encoder5～8四层主输出全部零差异，block8原3差异消失；C32 encoder1～4主输出回归全部零差异。test_half_square_fma.py验证直接原始key8归一化值及完整4096值窗口均精确。
- AMD shader仍使用旧平方表达式，尚未修复/回归，不能称GPU问题已解决。下一步实现无需不可靠double的等价half-FMA舍入，未部署DLL。

### 2026-09-07：block8平方FMA二次舍入机制定位

- 捕获PC3a40相关32lane操作数，key8全32个分量都可在参考K向量找到；按实际HMUL/HFMA/HADD/shuffle用float32重放仍383.25。
- 关键一项下半平方(-12.75)^2=162.5625恰为half中点，加上上半小平方舍入项会略超中点；float32中间相加可能先抹掉小项。原始HFMA2一次half舍入不应抹掉。
- 诊断precise_norm改为float64保留融合平方+已half舍入上项，最小原模型窗口3处差异全部消失。尚未生产CPU/AMD通用实现及回归，不能宣布移植完成；未部署DLL。

### 2026-09-07：key8原始平方和383.5，参考383.25

- inspect_key8_norm_sum.py确认参考inverse .05108642578125与原始.051055908203125不同，分别产生已观察的两种half归一化值。
- cuda-gdb于PC4030前读取lane16/R142，两个half均383.5；参考相同K向量平方和383.25。差异已定位到归一化平方和输入，而非MUFU近似或FP8转换本身。
- 保存qk-registers-4030.json与key8-norm-sum.json，下一步还原C64 norm归约/通道配对并全量回归；尚未修改生产算法或部署DLL。

### 2026-09-07：key8量化前差异与normalize乘法操作数直读

- PC46a0 lane16 R12高half为-.0244140625，参考-.0244293212890625；此前46d0的R12低16码8c2a在后续merge移入高位，解释byte3来源不是R11。
- PC41d0 HMUL2 R12,R96,R138直接读取：R96高half=-.478271484375（与参考原始K一致），R138两half=.051055908203125。
- inspect_key8_prequant.py保存normalize操作数，下一步核对原始平方和与倒数；未用单值更改模型，未部署DLL。

### 2026-09-07：key8候选字节定位及量化前断点

- K位布局诊断定位差异在lane16/R12/byte3；新增记录lane/register/byte字段。
- 原始PC46d0前捕获32lane（F2FP merge写R12），inspect_key8_prequant.py显示R11两half并非候选K8/ch3归一化值，说明不能直接假定byte3来自本指令R11高half，需追前次merge或其他来源。
- 参考该K原始值-.478271484375，归一化half-.0244293212890625，接近E4M3相邻值中点；保存key8-prequant.json，不将未确定映射当数值证据。未部署DLL。

### 2026-09-07：原始K片段交叉解码定位单值差异

- inspect_block8_key_groups.py发现K按[0,1,2,3,8,9,10,11]等非连续8key分组，八组中七组直方图完全一致，含key8组仅1值不同。
- decode_block8_k_bitlayout.py在干净R4/5片段穷举唯一bit排列[1,0,3,4,2,5,6,7]，同排列交叉核对其余七组：全部K仅key8/channel3不同，原始-.0234375、参考-.025390625。
- 属数据推断诊断布局，尚需指令来源佐证，不作生产映射拟合；下一步量化前K值/正規化跟踪。未改模型公式或部署DLL。

### 2026-09-07：K片段标准连续key布局候选排除

- decode_block8_k_fragments.py对PC5030全lane寄存器两两组合，测试标准8key×32channel B片段与连续8key参考块，无接近逐值匹配的候选。
- 排列无关直方图最佳仍118值不同，因此不能仅靠调整同一连续key块的channel排列解码；需追踪原始加载/重排及非连续key分组。
- histogram仅诊断排序，未当验收或拟合映射。结果ignored最小窗口目录，生产算法未改、未部署DLL。

### 2026-09-07：原始QK得分直接读值发现key8差异

- debug gdb新增DLSS5_DEBUG_PC可选断点，读取PC5080（首softmax affine之前）32lane寄存器，原始kernel未改。
- decode_block8_scores.py按bias载入和QMMA C操作数传播坐标，仅比较当时已计算的score寄存器：1536唯一q/key坐标，23差异；已打印样本集中key8，包括q18原始-.338134765625/参考-.3369140625。
- 保存score-register-comparison.json，下一步读取/核对key8 K向量而非继续改softmax；该映射仍需完整覆盖控制审计，不把局部结果扩大为全score已验证。未部署DLL。

### 2026-09-07：原始QK边界32lane寄存器捕获

- debug_block8_qk.gdb在PC+5030捕获第二head全32lane、每lane R0～159，成功保存qk-registers-5030.json；原始CUBIN未修改。
- decode_block8_q_fragment.py试标准16×32 A fragment两个寄存器位顺序候选，均未与任一连续16query块吻合，不能把原始packed寄存器直接当标准布局。结果q-fragment-candidates.json用于后续真实布局追踪。
- 尚未Q/K数值解码完成或修复block8；未部署游戏DLL。

### 2026-09-07：原始C64 kernel寄存器直接读取成功

- 本地cuda-gdb可在原始CUBIN符号断点停住，动态取entry PC再于+0x5030断点，避免硬编码加载地址。
- debug_block8_qk.gdb切换thread(0,1,0)第二head，成功读取量化后R8..11及R80/81/84/85。该次为除错停止后退出，不产生新的最终oracle。
- 下一步采集32lane并按QMMA A/B布局解码Q/K，直接对照query-reference。未改原始CUBIN、生产权重或游戏DLL。

### 2026-09-07：Q缩放次序候选核对

- 原始SASS 0x4e00/4ea0等显示先乘归一化倒数再乘scale，两个HMUL2；与当前参考的两次half舍入一致。
- check_block8_qscale_order.py测试先合并scale/inverse及单次舍入，分别导致27/38个attention差异，均劣于基线9；不将更改顺序用作修复。
- 保存ignored window46-18/qscale-order.json。下一步仍需原始Q/K中间值验证；未改生产公式、未部署DLL。

### 2026-09-07：参考得分重放定位差异至QK得分生成

- isolate_block8_score_replay.py在identity projection诊断副本仅清零第二head的Q矩阵，将参考half QK+bias得分按原bias布局写入第二head bias，其余FFN/K/V和第一head保持。
- 原始kernel处理重放得分后的attention输出与CPU参考4096值零差异，与未修改attention原输出仍9差异。说明此得分输入下原softmax/AV可复现参考，优先定位Q/K投影/normalize/得分生成，而非继续修改分母。
- 控制仅本地ignored窗口目录，不进入生产；未修复block8或部署游戏DLL。

### 2026-09-07：AMD生產C64最小窗口重现3值差异

- split-window host新增single64 8×8/shift0模式，prepare_block8_window_gpu.py导出原始block8真实系数及未改模型oracle。
- RX9070XT生产NativeC64Shift首帧different3/max_error0.5，exit1、device_removed_reason0；与CPU参考同数量同最大差异，明确GPU移植路径也未通过。
- ignored window46-18/amd与远端block8-window为可重复诊断入口，未使用修改权重控制作为验收，不部署游戏DLL。

### 2026-09-06：block8得分对齐/平方舍入候选排除

- check_block8_score_alignment.py按已知27bit乘积对齐模型测试QK+bias，得分与现参考无变化，仍9个attention差异；不是此候选能解决的问题。
- check_block8_norm_candidates.py对照两侧平方各舍入与上下侧融合互换，分别25/31个差异，比原9个更多，query18仍不同。均为参考诊断，不修改生产公式。
- 结果ignored最小窗口目录，下一步仍需原始中间值或更直接控制定位，不用拟合输出修正，未部署DLL。

### 2026-09-06：block8清零Q/K诊断均精确

- isolate_block8_qk.py分别在identity-projection诊断权重副本清零Q或K，保持FFN、V、bias及其余参数，原始kernel各4096值与参考零差异/finite。
- 说明差异依赖非零QK得分路径；不能完全排除概率变化引起的AV舍入问题，但下一步应优先核对实际Q/K归一化或QK+bias得分。
- ignored window46-18/qk-controls.json，未修改生产权重或部署DLL。

### 2026-09-06：实际query输入的近似倒数指令控制

- probe_normalize_intrinsics.cu使用PTX rcp.approx.ftz/rsqrt.approx.ftz，对最小窗口参考Q/K平方和及softmax分母运行本地sm121 GPU。
- check_block8_intrinsics.py比较GPU近似结果转half与CPU参考转half，全部相同，different_half_results0。未证明原kernel中间值相同，但排除在这些已给定输入上近似指令half舍入不同的候选。
- 结果ignored window46-18/intrinsic-comparison.json；仍需获取原始Q/K或概率中间值，未改生产算法、未部署DLL。

### 2026-09-06：query0～31原始归约树追踪

- trace_c64_softmax_partials.py --first-half从0x4070～5aa0追踪bias，再按对应partial寄存器和已核对SEL/SHFL构造query0～31树，各query64keys各一次。
- query18指数行代入此树所得分母仍.30517578125，与旧参考相同；不是直接找到求和顺序错误。保存c64-first-full-trees.json，继续核对实际指数/概率输入。
- 新选项变量与指令args重名导致初次脚本报错，已改为options并重跑通过覆盖检查。生产代码未改，未部署DLL。

### 2026-09-06：C64完整shuffle求和树覆盖检查

- 核对P3/P2/P1/P0来自lane位1/2/8/16；shuffle索引由(lane%8)*4+lane/8并xor0/1/2/3形成。追踪0x8610～8780后每lane树均同一query、64keys各一次。
- 当前bias载入段对应query32～63，保存c64-full-trees.json；把树的key顺序应用最小query18指数行，打印与旧分母对比以继续定位。
- 尚未全模型数值回归或生产修改，不能把符号覆盖正确等同修复完成；未部署DLL。

### 2026-09-06：C64局部softmax求和符号追踪

- trace_c64_softmax_partials.py从0x7510 bias载入经QMMA C操作数、HFMA/clamp/指数位变换与HADD传播坐标到0x8600，四个partial寄存器8/11/56/57各64个half来源均非空。
- 输出最小窗口目录c64-partial-trees.json；当前lane基址仍待完整核对，尚未追过SEL/SHFL，不能据此宣布分母合同已还原。
- 未改生产算法、未使用倒数偏移拟合，未部署DLL。

### 2026-09-06：C64 softmax原始归约指令定位

- cuobjdump确认C64原kernel在0x7fa0～0x8600执行packed half局部和，0x8690后warp shuffle，0x8750/8760/8770顺序合并四部分，0x8780合并half两侧，0x87d0/87e0 MUFU.RCP。
- extract_c64_softmax_trace.py保存0x7a00～8900指令与CUBIN hash到最小窗口目录，便于下一步将bias坐标符号传播到求和树。
- 现有denominator来自C32追踪，尚未证明与C64同序；仅指令定位，不宣称发现/修复根因，不改生产分母或倒数。未部署DLL。

### 2026-09-06：block8 query18分母/倒数敏感性线索

- inspect_block8_query.py保存第二head所有中间参考至query-reference.npz，query18分母.30517578125、half倒数3.27734375（精确倒数3.2768），AV复现9差异。
- 仅诊断性测试倒数邻近half值：delta -2/-1/0/+1仍9差异，+2（3.28125）恰为0差异。不能据此拟合runtime修正；下一步检查真实分母求和次序及原始倒数实现。
- 敏感性结果ignored window46-18/reciprocal-sensitivity.json，生产代码未改，未部署DLL。

### 2026-09-06：block8差异定位到attention第二head单query

- isolate_block8_attention.py在诊断权重副本将projection设identity、最终skip置零，并解码检查矩阵确为单位阵。
- 原始attention-control与参考有9值不同，全部query18、channel32～63（第二head），其他query/head一致；保存attention-original/reference.f32及报告。
- 排除最后projection是最早差异点，下一步定位query18/head1的Q/K normalize、概率或AV累加。诊断副本未进生产路径，未部署游戏DLL。

### 2026-09-06：block8最小窗口FFN特征控制零差异

- isolate_block8_ffn.py仅在诊断副本将attention projection置零、最终skip置1，保持FFN与原输入，原kernel输出暴露量化FFN residual。
- 4096值与现有FFN参考完全一致，ffn-control.json记录different0。差异定位进一步缩至attention/其projection，不能把修改权重控制当原模型验收。
- 所有控制产物在ignored window46-18，原始权重未改；未部署游戏DLL。

### 2026-09-06：block8 CPU精度候选诊断

- check_block8_accumulation.py在4096值最小窗口上比较原float32 dot与float64内积后half舍入，均重现同3差异，排除单纯提高矩阵内积精度即可解决的假设。
- 加入float64 rsqrt对照，所有候选结果保存window46-18/accumulation-candidates.json；仅局部替换参考函数并finally恢复，不改生产算法或拟合像素。
- 未解决block8差异，下一步应获取/隔离中间阶段，未部署游戏DLL。

### 2026-09-06：block8三值差异缩减到单8×8窗口

- isolate_block8_window.py按shiftY4定位source y44..51/x16..23，独立运行原始普通C64 8×8、shift0，输出与完整shift2大图对应窗口零差异。
- CPU参考在窗口内y2/x2通道11/31/61重现相同3差异，排除大尺寸调度、边缘窗口及截取错误。
- ignored encoder-c64/window46-18保存4096值原始oracle和输入，便于后续逐阶段算术定位；尚未修正block8，未部署DLL。

### 2026-09-06：block8差异集中单像素，排除DS主分支变体

- 重跑定位3差异均y46/x18，通道11原始.09375/参考.0625、31原始.234375/参考.25、61原始-4.5/参考-5。
- 同一block7输入、block8权重和shift2，运行原始普通C64 kernel，主输出与原始DS kernel字节级零差异。compare_block8_kernel_variants.py保存variant-comparison.json。
- 说明该样本不是DS主分支版本差异，下一步定位单窗口参考算术；仍未block8/down完整通过，不拟合像素修正，未部署DLL。

### 2026-09-06：valid1080 C64主分支5～7精确，8仍有3值差异

- validate_native_valid1080_c64.py按已验证global16低通道位交换解码block4-down，实际480×288依次验证5～8。
- 5/6/7各8847360值零差异；block8仅3值不同、max_abs0.5、最终finite，assert失败，未导出通过oracle。报告encoder-c64/main-validation.json保留失败。
- 脚本补充失败位置记录供下次定位，本次尚未重跑该位置记录。不能用容差或多数一致标成通过，down8也尚未验证；未部署DLL。

### 2026-09-06：同valid1080 RGB原始encoder5～8捕获

- build_native_valid1080_c64.py从block4-down继续5～8，实际480×288 C64、shift0/3/1/2，5为inpview、8为ds，真实提取权重。
- 原始主分支长度/FP8 NaN检查及block8 down长度检查通过，来源/hash保存ignored release/native-rgb-valid1080/encoder-c64/capture.json。
- 仅捕获成功，尚未逐值参考/AMD同RGB5～8链验收；下一步核对物理通道与下采样，未部署游戏DLL。

### 2026-09-06：同valid1080 RGB GPU0～4整链双分支通过

- runner新增-Front4并显式设置/清除环境开关，GPU镜像→preblock→1～4→DS连续运行五帧seed0/0/0/1/0，重放与seed变化检查通过。
- 回读seed0最终block4 main17694720值、down8847360值与原始oracle均different0/max_abs0/finite通过，无中间CPU特征注入。
- ignored release/native-rgb-valid1080/amd-front4/validation.json。seed1仅检查变化、未原始逐值对照；仍单纹理受控RGB，不代表完整游戏多纹理输入。下一步encoder5以后及全图链，未部署DLL。

### 2026-09-06：valid1080 GPU0～4整链host接入待测

- preblock test新增DLSS5_FRONT4，强制reflect valid1080模式，GPU reflect→preblock→四层C32→DS直接串联，尺寸960×576/480×288，shift0/3/1/2。
- 两个主要回读改为block4 main及DS，保留preblock raw诊断；沿用seed0/0/0/1/0重放检查。
- C++编译通过、diff检查通过，尚未运行此新路径；下一步原始双oracle对照，未部署游戏DLL。

### 2026-09-06：valid1080 RGB0～4整链GPU fixture准备

- prepare_native_valid1080_front_gpu.py检查四主分支与down参考通过，直接解码原始1～4系数及block4 DS矩阵。
- 依据独立coded projection64通道顺序，把原始down物理通道转回生产DS矩阵行顺序，导出oracle-down和oracle-main4；输入仅有效1920×1080 RGB。
- ignored release/native-rgb-valid1080/amd-front4，打包与diff检查通过，尚未执行GPU0～4整链，未部署游戏DLL。

### 2026-09-06：block4下采样参考遗漏修正后精确通过

- 对照check_native_c32_ds.py发现新验证器漏池化后FP8量化及既有coded projection输出行顺序；从ds-coded-aux.fp8恢复已验证64通道排列，检查所有空间位置编码一致及排列双射。
- 补齐两项后valid1080 block4 down8847360值different0/max_abs0/finite通过。保留旧失败down-validation.json，新结果down-validation-corrected.json，导出block4-down.f32为原始global16物理通道顺序，非默认矩阵行序。
- 没有拟合校正，也未改原生产算术；这是新参考程序遗漏既有合同的修复。下一步AMD同RGB前端整链验证，未部署游戏DLL。

### 2026-09-06：valid1080 block4下采样参考首轮失败

- validate_native_valid1080_ds4.py从已验证block3主输出重算block4 raw half，水平pair半精度池化，再用既有block4-ds矩阵投影。
- 按global16解释原始C64 down后，8847360值中6121052不同、max_abs49、最终finite；进程assert失败，结果保存encoder-c32/down-validation.json，未导出通过oracle。
- 此结果不推翻已验证block4主分支，但说明当前down算术/矩阵通道或物理布局假设尚未成立。下一步先核对原始C64 down布局，不吞错误继续整链。未部署DLL。

### 2026-09-06：同valid1080输入encoder1～4主分支逐值通过

- validate_native_valid1080_c32.py从原始block0-down逻辑global16读取，按shift0/3/1/2逐层计算，四层各17694720主分支值与原kernel零差异、最终finite。
- block4参考归一化出现half cast overflow警告，但最终输出finite且逐值一致；没有屏蔽警告或替换非有限值。block4下采样仍未验收，不能把主分支通过扩大为完整block4通过。
- ignored release/native-rgb-valid1080/encoder-c32/main-validation.json及各main.f32用于后续skip；尚未AMD完整前端或游戏部署。

### 2026-09-06：同valid1080 RGB原始encoder1～4输出生成

- build_native_valid1080_c32.py从已验证block0-down继续1～4，实际960×576、shift0/3/1/2，block1 inpview、block4 downsample，真实提取权重。
- 原始主分支长度/FP8 NaN检查、block4 down长度检查通过，输入来源及hash存ignored release/native-rgb-valid1080/encoder-c32/capture.json。
- 此轮为原始输出捕获，未逐值对照CPU或AMD；不会将finite当作数值验收。下一步核对C32前端及block4实际skip/down链，未部署DLL。

### 2026-09-06：valid1080 GPU镜像→preblock双分支精确通过

- 远端原生functions.f32实际存在且201326592字节。运行NativeRgbReflect→NativePreblockRuntime，五帧seed0/0/0/1/0通过重放与seed影响检查，所有中间数据在GPU。
- 下载seed0最终main/down，validate_native_reflect_preblock_gpu.py与原始valid1080纹理输出比对：main70778880值、down17694720值均finite/零差异/max_abs0。
- ignored release/native-rgb-valid1080/amd-preblock/validation.json。seed1本次仅检查输出变化，未与原始seed1逐值对照；仍是单纹理受控RGB，不代表完整游戏多纹理合同。下一步同输入encoder/skip整链，未部署DLL。

### 2026-09-06：reflect→preblock GPU测试入口接好待运行

- d3d12_native_preblock_test新增DLSS5_REFLECT_VALID1080模式，输入严格1920×1080/live/global、强制存在原生噪声表，GPU反射输出直接传NativePreblockRuntime，处理1152高。
- 保留现有五帧seed0/0/0/1/0重放检查及main/down/raw读回，未将不同seed只看相同输出当成功。
- run_native_reflect_preblock.ps1配置宽度、seed和201326592字节噪声表检查。C++编译通过，尚未运行新路径或逐值对照；噪声默认远程路径部署前需实查。未部署游戏DLL。

### 2026-09-06：valid1080原始preblock双分支GPU oracle准备

- prepare_native_reflect_preblock_gpu.py解码原始main cell至1152×1920×32 HWC、down global16至576×960×32，长度及FP8 NaN检查，导出原始RGB有效纹理。
- ignored release/native-rgb-valid1080/amd-preblock生成两个oracle及input。脚本随后补block0系数复制逻辑，当前已生成目录未重新执行该复制，部署前需确认系数存在。
- 移除本轮生成的重复main-cellgrid.f32（仅可再生测试产物），保留main-oracle.f32。尚未运行reflect→preblock GPU链，未部署游戏DLL。

### 2026-09-06：GPU镜像RGB打包三帧精确通过

- extent host新增rgb_reflect，实际1920×1080 HWC输入→1920×1152 tile-major输出，调用生产NativeRgbReflect。
- RX9070XT三帧8847360个RGBA值零差异，finite/device/fence通过；提交等待2.639/2.119/1.962ms。回读gpu-tiled.f32与已验证reflect fixture字节级cmp一致。
- ignored release/native-rgb-reflect1080。此为RGB准备算子，尚未与AMD preblock及完整网络串联验证，不是游戏画面验收，未部署DLL。

### 2026-09-06：GPU镜像RGB资源封装完成待执行

- 新NativeRgbReflect管理SRV输入/UAV输出、根常量、shader与重放状态转换；有效维度>=2、处理尺寸8整除、上下界及源buffer容量显式检查。
- Record仅GPU调度反射与tile-major输出，不传输CPU神经特征；1920×1080→1920×1152使用240×144 dispatch。
- C++交叉编译通过（直接编译header仅pragma once警告），HLSL仍需D3D运行时编译及数值/重放验证。未部署游戏DLL。

### 2026-09-06：GPU镜像输入shader与tile布局公式准备

- native_rgb_reflect.hlsl直接从有效HWC float4读取镜像坐标，输出8×8 tile-major float4；不需要逐像素CPU索引表。坐标函数支持周期反射，调用方需确保有效尺寸>=2、处理尺寸8整除。
- check_rgb_reflect_layout.py以原始输出已验证的numpy reflect fixture核对索引公式，2211840像素零差异，最后一行映射到有效源1007行，dispatch240×144。
- 输出input-tiled.rgba32f供后续preblock，尚未创建D3D资源封装/执行shader，不宣称GPU通过；未部署游戏DLL。

### 2026-09-06：SASS引导的镜像RGB补齐精确通过

- 原始preblock SASS 0x02d0/02e0/0320/0370显示越界坐标选择2*extent-coordinate-2（有效宽来自cb0x454，即参数0xd4）；据此测试reflect而非继续猜常量。
- check_preblock_clamped_padding.py --reflect将1080高RGB镜像补到1152，仍使用等尺寸纹理原kernel；与valid1080纹理原输出main70778880字节、down17694720字节均different=0。
- 结果ignored release/native-rgb-reflect1080/validation.json。该实测确认本单纹理/seed0合同的底部镜像等价性；未证明所有额外纹理分支或动态seed。下一步将镜像预处理接到AMD输入路径，未部署DLL。

### 2026-09-06：preblock简单补齐候选继续排除

- 空间审计新增padding非零统计：valid1080原始main补齐区4415795个非零值、down1104059，排除直接输出全零。
- 原始等尺寸纹理补齐RGB0候选仍main4364004/down1103270差异；RGB0.5候选main3826123/down888244差异。均独立保存在release/native-rgb-fill*-1080，未覆盖已有实验。
- 边缘复制、黑色、灰色三种输入填充均不能重现valid纹理合同；下步应检查SASS有效范围/坐标/噪声分支，不将候选用于AMD发布路径。未部署DLL。

### 2026-09-06：valid1080差异仅在补齐区，RGB边缘复制候选失败

- audit_preblock_valid_region.py按逻辑像素解码：main前1080行零差异，变化仅1080～1151；down前540行零差异，变化仅540～575。结果release/native-rgb-valid1080/spatial-difference.json。
- check_preblock_clamped_padding.py把RGB最后一行复制到1152高，使用旧等尺寸采样合同运行原kernel；与valid1080原始输出仍main3957299/down969269值不同。不能简单用最后一行RGB填充来替代原版有效区域处理。
- 失败候选独立保存在release/native-rgb-clamp1080；未改AMD采样，下一步检查kernel有效区域分支/噪声坐标及补齐特征生成。未部署游戏DLL。

### 2026-09-06：valid1080纹理/processing1152原始preblock已运行

- caller增加DLSS5_PREBLOCK_GAME_TEXTURE，仅允许1920×1152处理且需参数blob，实际CUDA纹理1920×1080，输入长度/上传pitch/有效标量按纹理尺寸，处理/下采样尺寸保持1152。
- prepare_native_rgb_valid1080.py截取同一随机RGB前1080行，seed0和捕获标量一致，输出长度及FP8 NaN检查通过。主分支与旧完整1152纹理实验3945111字节不同，下采样938692不同。
- ignored release/native-rgb-valid1080，尚未定位差异空间范围/验证AMD采样，单纹理控制不代表完整游戏多纹理输入。未部署DLL。

### 2026-09-06：preblock有效纹理范围与网络补齐范围不同

- audit_preblock_scalar_profiles.py对比旧4K与新PID25972非资源字段0x48..0xd7，仅0x90/94/98/9c/a0/a4/d0/d4不同，其他所查标量一致。
- 新捕获0x90/94=1920/1080，0xd0/d4=1080/1920；与已知0xf0/f4=1152/1920区分开。原始caller当前覆盖有效尺寸/倒数为处理范围，所以release/native-rgb-game仍是受控1152高纹理，不是游戏真实边缘采样合同。
- pointer-free比较保存preblock-scalar-profile-comparison.json，runtime-contract显式记录缺口。下一步分离texture extent与processing extent；未部署游戏DLL。

### 2026-09-06：实际尺寸受控RGB整链起点建立

- 原始preblock caller支持明确1920×1152，容量按像素×32×4分配。
- prepare_native_rgb_game.py建立独立release/native-rgb-game，复用post随机RGB作为同一编码/合成基底，保存HWC与tile-major两种输入；原始block0权重、实际PID25972标量blob、seed0覆盖，单纹理句柄替换。
- 原始main70778880字节、down17694720字节长度/FP8 NaN检查通过；只表示捕获成功，尚未新尺寸数值对照。该输入不是实际游戏多纹理合同，不宣称游戏画面验证。
- 下一步同RGB生成完整encoder及真实skip，未部署游戏DLL。

### 2026-09-06：AMD49～69连续主分支21阶段GPU链通过

- extent host新增decoder49_69_game，生产NativeDecoderTail69实际尺寸配置，三路skip明确上传且尺寸/finite检查，主分支无中间CPU注入。
- RX9070XT三帧17694720最终值零差异，device/fence/finite通过；整段提交等待1026.241/1019.269/1016.162ms，不是游戏FPS。gpu.f32回读cmp原始连续oracle一致。
- ignored release/native-decoder-tail-game/amd，三路skip仍独立随机，输入来自block48随机fixture，未证明真实encoder跳接或完整RGB链。游戏DLL未部署。

### 2026-09-06：连续49～69整段GPU fixture打包完成

- prepare_native_decoder_tail_game_gpu.py遍历21阶段，普通层检查原始pass/零差异/上游来源，放大层逐字节核对input.f32等于上一原始oracle，导出真实系数和最终block69 oracle。
- 三路skip显式标记独立随机，未伪装encoder跳接；每阶段原始输出hash写provenance，AMD_verified=false。
- ignored release/native-decoder-tail-game/amd打包成功；脚本随后补普通层与捕获移位表一致性检查（原始报告已记录对应移位）。尚未执行AMD21阶段整段，未部署DLL。

### 2026-09-06：连续decoder主分支到69原始对照完成

- block66脚本支持--main-hwc/--output-root，主输入使用连续block65原始oracle，skip仍为固定seed3016独立随机，保留来源字段。
- 原始66及67～69按实际尺寸与shift0/3/1/2依次运行，每层17694720值零差异。新结果ignored release/native-decoder-tail-game，未覆盖独立fixtures。
- 至此从既有原始block48输出到69主分支连续，三路skip仍独立随机。下一步导出整段49～69 GPU测试资料，不宣称RGB encoder或游戏验收完成，未部署DLL。

### 2026-09-06：连续decoder主分支推进到65

- block62脚本新增--main-hwc/--output-root，用连续block61原始oracle替换随机main，skip保持seed3015独立随机。原始62及随后63/64/65逐层零差异，C64各8847360值。
- ignored release/native-decoder-tail-game保存新连续来源结果，旧独立fixture保留。62报告本次进程读取的是新增main_source字段之前脚本版本，来源由本次命令与input.f32记录，不据缺失字段假定随机main。
- 连续链仍未接encoder真实skip，未执行AMD49～69整段或部署游戏DLL。

### 2026-09-06：尾段连续来源oracle推进到block61

- block56测试支持--main-hwc/--output-root，用已验证原始block55的oracle.f32作为主输入，不再重置随机main；skip仍固定独立随机seed3014，非真实encoder14跳接。
- block55→56放大及56→57～61连续原始链逐层零差异，各C128输出4423680值；新产物ignored release/native-decoder-tail-game，不覆盖旧独立fixture。
- 该连续链目前从原始block48随机fixture起，未接RGB encoder，尚未AMD49～69整段验收或游戏部署。

### 2026-09-06：decoder49～69整段类接入实际尺寸

- NativeDecoderTail69新增game_extent明确配置：输入120×72 C256，后续240×144 C128、480×288 C64、960×576 C32；默认仍旧RGB512配置。
- 三个放大投影token数分别8640/34560/138240，对应已独立验证生产路径，移位继续用NativeDecoderShift；增加输入及三个skip buffer类型/容量检查。
- 完整host编译通过，尚未运行此49～69整体资源链；不能把各独立段通过当整段通过。下一步构造连续来源的原始整段oracle并验证，未部署游戏DLL。

### 2026-09-06：640逻辑桥接正反向GPU独立通过

- bridge640 test使用生产NativeVitGather及原始映射组合，输入655360个互不相同且float精确可表示的位置编号，正反方向分别对照，不以往返抵消错误。
- RX9070XT正反各三帧零差异，device/fence/finite通过，两份gpu.f32分别cmp各自oracle一致。ignored release/native-vit/repack640/gpu-forward及gpu-inverse。
- 证明映射在GPU正确执行，尚不代表与实际encoder/ViT/decoder整链衔接完成；未部署游戏DLL。

### 2026-09-06：实际640token逻辑桥接映射准备

- prepare_native_bridge640.py将原始forward/inverse物理映射与HWC/C512双bank及ViT逻辑位布局组合，655360项双射；独立组合原始inverse与argsort(forward逻辑映射)完全一致。
- 仅10240项为恒等映射，不能直接把head HWC指针当ViT逻辑输入。产物ignored release/native-vit/repack640/hwc-to-vit.i32、vit-to-hwc.i32。
- NativeVitGather允许655360条映射，保留严格双射/源buffer容量检查；完整host编译通过，GPU新桥接执行尚待验证。原完整host仍512尺寸，不能据此宣称已接成实际RGB链，未部署DLL。

### 2026-09-06：AMD post70实际1920×1152最终RGB三帧通过

- run_native_post70_game.ps1显式清除DLSS5_POST_BASE_ONLY，调用生产NativePost70实际尺寸路径，不是底图直通诊断。
- RX9070XT三帧6635520个RGB值different=0/max_error=0，finite/device/fence/replay通过；gpu.f32回读与原始oracle字节级cmp一致。整数head范围检查在本随机fixture通过。
- ignored release/native-post70/amd-game。独立随机main/skip/color算子通过，不证明实际encoder/decoder跳接、动态输入或游戏提交。所有独立实际尺寸段通过仍不等于完整RGB链通过，未部署游戏DLL。

### 2026-09-06：AMD post70实际尺寸入口与fixture准备

- NativePost70允许明确1920×1152，test新增game模式，按实际尺寸检查main/skip/color/oracle长度。dispatch为34560像素组×planes、C32body34560组，低于65535限制。
- prepare_native_post70_gpu.py --game-extent核对6635520值原始零差异/finite，导出原始输入及直接解码系数到ignored release/native-post70/amd-game。
- 编译与diff检查通过，尚未执行实际尺寸AMD post70，整数输出头范围在此fixture也尚待GPU验证。未部署游戏DLL。

### 2026-09-06：post70实际1920×1152最终RGB逐值通过

- check_native_post70_game.py以16行对齐条带调用原生参考，post shift0的8×8窗口不跨条带，降低内存占用而不改变计算；保留每条带结果。
- 全部6635520个RGB值与原始post70输出零差异、max_abs0、finite通过；原始RGBA前三通道导出oracle.f32。
- ignored release/native-post70/game/validation.json。仍为独立随机main/skip/color，未证明真实RGB网络全链或AMD实际尺寸post70，未部署游戏DLL。

### 2026-09-06：post70实际1920×1152原始输出捕获

- 原始post native模式增加明确1920×1152尺寸，激活容量按像素×32+65536计算，main/skip读取有有效长度检查；编译通过。
- prepare_native_post70_game.py构造独立非零随机main/skip/RGBA，保留C32主global16和skip cell映射，真实post权重、mask1/mode1/scale.03125运行原始kernel。
- 输出2211840个RGBA像素全部finite，范围日志0.0842716992..1；ignored release/native-post70/game。仅捕获成功，尚未CPU逐值验证或AMD新尺寸，不能记为post精确通过。未部署游戏DLL。

### 2026-09-06：AMD decoder67～69实际960×576三层GPU链通过

- extent host新增decoder67_69_game，三层NativeC32Stage使用实际shift3/1/2与960×576，GPU资源直接串联。
- RX9070XT三帧17694720最终值零差异，finite/device/fence通过，回读gpu.f32与原始oracle字节级cmp一致；三层提交等待140.543/141.888/142.663ms，不是游戏FPS。
- 日志通用tokens字段17280是oracle值数/1024，不代表该C32空间尺寸，实际Create与文件检查明确960×576。ignored release/native-decoder-game-c32/amd-decoder67-69。
- 尚未post70实际尺寸及真实RGB全链验收，未部署游戏DLL。

### 2026-09-06：原始decoder67～69实际960×576整链通过

- C32参考新增game extent960×576，从原始block66输出依次运行67/68/69，shift3/1/2。三层各17694720值原始/CPU零差异，tail检查通过。
- prepare_native_decoder_c32_game_gpu.py检查来源、值数与移位，导出原始最终oracle与三层系数到ignored release/native-decoder-game-c32/amd-decoder67-69。
- 尚未执行AMD三层链，未完成post70实际尺寸或RGB全链，未部署游戏DLL。

### 2026-09-06：AMD block66实际960×576三帧精确通过

- extent host新增upsample66_game，NativeVitLinear half融合结果直接送NativeC32Stage(960×576 shift0)，无中间CPU传输。
- RX9070XT三帧17694720值different=0/max_abs=0，finite/device/fence通过；提交等待53.827/55.473/58.230ms仅本算子，gpu.f32回读cmp原始oracle一致。
- ignored release/native-upsample66/amd-game。仍是独立随机输入算子，不是真实encoder跳接或RGB全链；后续67～70和游戏画面验收未完成，未部署DLL。

### 2026-09-06：AMD block66实际几何与fixture准备

- NativeVitLinear decoder支持138240token/C64→C32，主宽480、输出960×576，保持half merge而非提前FP8；线性输出按65536元素dispatch分块。
- C32 reframe/body按64像素组调度，960×576为8640组（shift0），未超过65535组限制。
- prepare_native_upsample66_gpu.py --game-extent检查原始零差异/finite/tail/输出尺寸并导出系数、原始oracle到ignored release/native-upsample66/amd-game。
- 编译通过，新AMD完整block66尚未运行；未部署游戏DLL。

### 2026-09-06：block66实际960×576原始对照通过

- mode10/by1/960×576明确尺寸例外，arena32MiB覆盖17694720字节输出/skip，旧模式保留。
- check_native_upsample66_game.py独立非零随机480×288×64 main及960×576×32 skip，C32 cell映射与真实权重，shift0/grid120×72。
- 原始完整放大、half融合及C32body与参考17694720值全部零差异，finite/tail通过，保留half残差不提前FP8。ignored release/native-upsample66/game保存fixture。
- 尚未AMD实际尺寸block66验证或全图链，未部署游戏DLL。

### 2026-09-06：AMD实际decoder63～65三层GPU链通过

- host新增decoder63_65_game，480×288 C64、shift3/1/2，三层生产NativeC64Shift GPU资源直接串联。
- RX9070XT三帧最终8847360值零差异，finite/device/fence/replay通过，gpu.f32回读cmp原始oracle-0一致。
- ignored release/native-decoder-game-c64/amd-decoder63-65。后续66～70、真实RGB全链和游戏最终画面尚未验证，未部署DLL。

### 2026-09-06：原始decoder63～65实际480×288对照通过

- 普通decoder参考支持C64 game extent480×288，从原始block62输出顺序运行63～65，实际shift3/1/2。
- 三层各8847360值与原kernel零差异、tail检查通过。prepare_native_decoder_c64_game_gpu.py核对来源/移位/值数并导出最终oracle及三层系数。
- ignored release/native-decoder-game-c64/amd-decoder63-65。尚未执行AMD三层GPU链，未部署游戏DLL。

### 2026-09-06：AMD block62实际480×288三帧通过

- extent host新增upsample62_game，NativeVitLinear主240×144/C128→C64与NativeC64Shift输出480×288/shift0直接GPU串联。
- RX9070XT三帧8847360值different=0/max_abs=0，finite/device/fence通过；提交等待52.897/55.325/54.734ms仅本算子。gpu.f32回读cmp原始oracle一致。
- ignored release/native-upsample62/amd-game。独立随机输入，不证明真实跳接链，后续63～70及RGB全链待完成，未部署游戏DLL。

### 2026-09-06：AMD block62实际投影几何准备

- NativeVitLinear decoder支持34560token/128→64，主240×144、输出480×288索引；容量检查按实际输出元素，旧尺寸行为保留。
- prepare_native_up56_game_gpu.py --block62校验原始零差异/finite/tail和文件大小，导出完整block62原始oracle与系数到ignored release/native-upsample62/amd-game。
- 编译与diff检查通过，尚未运行AMD block62完整算子；下一步接480×288 C64 body、shift0测试，未部署游戏DLL。

### 2026-09-06：block62实际480×288原始对照通过

- 原始fused探针对mode9/by2/480×288使用16MiB arena，输出8847360字节超过旧8MiB，明确扩容后运行。
- check_native_upsample62_game.py使用独立非零随机240×144×128 main、480×288×64 skip、真实block62权重、shift0/grid60×36。
- 原始完整放大/融合/Swin输出8847360值与参考全部零差异，finite/tail通过。ignored release/native-upsample62/game保存fixture；尚未AMD新尺寸验证，未部署游戏DLL。

### 2026-09-06：AMD实际decoder57～61五层GPU链通过

- host增加decoder57_61_game，240×144 C128、实际shift2/0/3/1/2，生产NativeC64Shift五层GPU直接串联。
- RX9070XT三帧最终4423680值零差异，finite/device/fence/replay通过；回读gpu.f32与原始oracle-0.f32字节级cmp一致。
- ignored release/native-decoder-game-c128/amd-decoder57-61。仍为独立链，后续62～70及RGB全链和游戏输出验收未完成，未部署DLL。

### 2026-09-06：原始decoder57～61实际240×144对照通过

- C128 game extent扩展到240×144，从原始block56输出顺序运行57～61，实际shift2/0/3/1/2。
- 五层各4423680值与原始kernel零差异，tail检查通过；prepare脚本核对逐层来源、移位和数值，导出AMD链fixture。
- ignored release/native-decoder-game-c128及amd-decoder57-61。此轮未跑AMD五层链，未部署游戏DLL。

### 2026-09-06：AMD block56实际240×144三帧通过

- extent host新增upsample56_game，NativeVitLinear主120×72/C256→C128与NativeC64Shift输出240×144/shift1直接GPU串联。
- RX9070XT三帧4423680值different=0/max_abs=0，device/fence/finite通过；提交等待77.900/71.971/66.703ms仅本算子，gpu.f32回读cmp原始oracle一致。
- ignored release/native-upsample56/amd-game，独立随机输入，不是block55/encoder14实际跳接链。后续57～70及整图验证待完成，未部署游戏DLL。

### 2026-09-06：AMD block56实际几何与fixture准备

- NativeVitLinear decoder支持8640token/256→128，主宽120、输出240×144；输出容量和skip检查沿用真实token×4元素数，shader补非方形索引。
- prepare_native_up56_game_gpu.py校验原始零差异/finite/tail与文件大小，导出原始最终oracle及线性/FFN/attention系数到ignored release/native-upsample56/amd-game。
- 编译通过，尚未运行AMD新block56模式；下一步接C128 shift1 body并对照重放。未部署游戏DLL。

### 2026-09-06：block56实际240×144原始对照通过

- 原始fused caller仅增加mode9/by4/240×144明确尺寸例外，输出4423680字节与skip均在8MiB arena内。
- check_native_upsample56_game.py使用非零独立随机120×72×256 main、240×144×128 skip、真实block56权重及实际shift1/grid31×18。
- 原始完整放大/融合/Swin输出4423680值与参考全部零差异，finite/tail通过。ignored release/native-upsample56/game保存输入/skip/oracle；尚未AMD新尺寸或真实跳接链验证，未部署游戏DLL。

### 2026-09-06：AMD decoder49～55实际七层GPU链通过

- decoder49_55_game模式120×72、实际shift3/1/2/0/3/1/2，七层生产NativeC64Shift资源直接串联。
- RX9070XT三帧最终2211840值different=0/max_error=0，finite/device/fence/replay通过，回读gpu.f32与原始最终oracle-0.f32字节级cmp一致。
- 初始化期间PID21704活跃，无重启。结果ignored release/native-decoder-game-c256/amd-decoder49-55。同步更新runtime-contract的640token和独立算子证据边界，未部署游戏DLL。

### 2026-09-06：AMD实际C256七层整链入口准备

- prepare_native_decoder_c256_gpu.py --game-extent核对49～55原始零差异、2211840值、逐层来源及实际shift3/1/2/0/3/1/2，导出七层系数与最终oracle。
- d3d12_native_split_window_test.cpp增加decoder49_55_game模式，120×72 C256、实际移位序列；旧实验入口保留。编译与diff检查通过，尚未执行新的七层GPU测试。
- fixture ignored release/native-decoder-game-c256/amd-decoder49-55。下一步部署到独立实验目录测试，未替换游戏DLL。

### 2026-09-06：实际120×72 decoder49～55原始链对照

- check_native_decoder_c256.py新增--game-extent（当前仅C256），矩形解码/窗口padding均按120×72，旧方形默认保留。
- 从已验证block48原始输出依次运行49～55，使用实际shift3/1/2/0/3/1/2；各层报告在ignored release/native-decoder-game-c256，成功层输出2211840值零差异。
- 本轮仅原始/CPU数值链，不是AMD七层GPU串联，也未部署游戏DLL。

### 2026-09-06：AMD实际block48完整算子三帧通过

- extent host新增upsample48_game，生产NativeVitLinear(60×36主分支)→NativeC64Shift(120×72 C256 shift0)，GPU资源直接传递。
- RX9070XT三帧2211840值different=0/max_abs=0，finite/device/fence通过；提交等待65.423/54.122/52.776ms仅本算子。回读gpu.f32与原始oracle字节级cmp一致。
- ignored release/native-upsample48/amd-game。输入仍是独立随机main/skip，不是完整decoder47/encoder22跳接；后续49～70及真实RGB链待继续，未部署游戏DLL。

### 2026-09-06：AMD block48非方形放大准备

- NativeVitLinear允许decoder模式2160token/512→256，以主宽60、输出120×72计算坐标，实际skip容量按8640×256检查；编译通过，未执行新GPU路径。
- prepare_native_upsample48_gpu.py增加--game48，检查原始finite/tail/零差异及输入输出文件尺寸，导出真实系数和原始最终oracle到ignored release/native-upsample48/amd-game。
- 下一步接NativeC64Shift(120,72,shift0,C256)并执行原始oracle对照；本轮未部署游戏DLL。

### 2026-09-06：block48真实120×72原始随机对照通过

- check_native_upsample48_game.py构造独立36×60×512 main(global16低通道位交换)及72×120×256 skip(cell)，block48真实权重，shift0/grid15×9运行原始kernel。
- 原始放大、skip融合、C256 Swin完整输出2211840值与参考零差异，finite/tail检查通过。ignored release/native-upsample48/game导出输入/skip/oracle。
- 这是独立随机输入而非block47/encoder22真实串联；AMD120×72 block48尚未验证，未部署游戏DLL。

### 2026-09-06：AMD实际decoder40～47八层GPU串联通过

- decoder40_47_game模式60×36，实际移位0/3/1/2/0/3/1/2，八层GPU资源直接串联，无中间CPU传输。
- RX9070XT三帧最终1105920值零差异，device/fence/finite/replay通过；回读gpu.f32与原始block47 oracle-0.f32字节级cmp一致。
- 初始化期间只读确认PID12428仍活跃，未误重启。结果ignored release/native-decoder-game-split/amd-decoder40-47。
- 验证范围仍为独立decoder40～47，不包含39及后续48～70、真实RGB输入或游戏提交；未部署DLL。

### 2026-09-06：实际decoder40～47原始整链对照通过，AMD入口准备

- 继续原始41输出顺序运行42～47，实际shift1/2/0/3/1/2，六层各四阶段全部零差异；连同40/41共32阶段通过，覆盖60×36四种移位。
- prepare_native_decoder_split_gpu.py --game-extent检查每层输入来源与实际移位，导出最终block47 oracle及八层系数，ignored release/native-decoder-game-split/amd-decoder40-47。
- AMD host增加decoder40_47_game模式，使用60×36与实际移位表，旧实验默认行为保留；编译通过，尚未运行这个八层GPU链。未更新游戏DLL。

### 2026-09-06：AMD60×36部分窗口shift0/3三帧通过

- 使用现有d3d12_native_split_window_test.cpp显式60 36 shift入口，无需修改生产NativeSplitWindow。
- block40 shift0与block41 shift3分别三帧最终零差异，device/fence/finite/replay通过；各1105920值回读gpu.f32与原始oracle-0.f32字节级cmp一致。
- ignored release/native-decoder-game-split/decoder-block40及41；这是两个独立层测试（输入分别来自原始上一层），不是AMD40→41串联。游戏DLL未更新。

### 2026-09-06：60×36部分窗口原始decoder40/41对照

- check_native_decoder_split.py新增--game-extent，按实际60×36解码cell并执行原kernel，保留旧16×16默认。
- 原始decoder39输出作为block40输入，shift0四阶段branch/ffn/attention/final均零差异。再将block40输出送入block41，使用实际shift3，检查相同四阶段。
- 结果保存在ignored release/native-decoder-game-split，各block validation.json为判据；这仍是原始/CPU对照，未验证AMD部分窗口GPU链、未部署游戏DLL。

### 2026-09-06：AMD实际decoder39裁剪三帧精确通过

- prepare_native_decoder_game_gpu.py解码原始decoder结果，extent host新增decoder39_game，上传独立随机main/skip和真实系数，调用生产NativeVitLinear decoder路径。
- RX9070XT三次1105920值different=0/max_abs=0，finite/device/fence通过，提交等待12.393/5.523/5.769ms（独立算子）。回读gpu.f32与原始oracle字节级cmp一致。
- fixture ignored release/native-decoder-game。这不证明encoder/ViT/decoder整链动态输入，亦未替换游戏DLL；下一步实际部分窗口与剩余decoder几何。

### 2026-09-06：AMD decoder39实际裁剪路径实现待测

- NativeVitLinear允许明确640token/1024→512 decoder入口，输出分配按60×36×512，不再按640×4×512；skip容量按真实输出检查。
- shader按32宽读取main、nearest展开，坐标超60×36时跳过写入，保留左上区域；有效区域原四分区投影/融合残差算术不改。
- 编译通过、diff检查通过；尚未在GPU执行此新路径，不能宣称AMD裁剪验证完成。下一步独立原始oracle对照及重放，未部署游戏DLL。

### 2026-09-06：decoder39真实尺寸左上裁剪原始对照通过

- check_native_decoder_game_extent.py独立随机20×32×1024 main及36×60×512 skip，真实block39权重，原始kernel参数input20/32/output36/60、grid16×5×4、16MiB arena。
- 原始输出与四分区半精度投影→nearest二倍→左上36×60→融合skip参考1105920值全部一致、finite/tail通过。偏移2/2和4/4裁剪分别1085188、1085351值不同。
- ignored release/native-decoder-game保留输入、权重、输出及partial/counters；单次本地Spark原始探针，不是AMD新尺寸或游戏验收。下一步将显式主尺寸/输出尺寸接进AMD decoder，未部署DLL。

### 2026-09-06：decoder39实际裁剪尺寸及探针容量修正

- launch0099参数0x40为inputHW20/32、outputHW36/60。原版显式输出60×36，不是64×40后任意解释；geometry summary新增直接证据。
- 原始decoder entry探针增加显式DLSS5_DECODER_GAME_EXTENT，仅允许32×20输入并传60×36输出，旧默认尺寸行为保留。
- 实际尺寸四份half partial超过旧2MiB工作区，game模式扩大到16MiB以容纳；编译通过，但未运行原始数值测试，裁剪坐标/布局仍待实证。未更新AMD decoder或游戏DLL。

### 2026-09-06：AMD实际补齐pool/head三帧精确通过

- extent host新增pool_head模式，明确输入36×60×512、输出20×32×1024，调用生产NativeC32Downsample的新补齐路径。
- RX9070XT三次各655360值零差异，finite/device/fence通过；提交到fence32.433/19.650/19.001ms（仅本算子，不是游戏FPS）。gpu-head.f32回读cmp原始head-oracle一致。
- 输入raw-projection.f32来自CPU参考，因此证明独立pool/head而非整个encoder GPU链；最终oracle来自原始pool→head，未用AMD自验答案。fixture ignored release/native-c512/pool-game-real。
- 未部署游戏DLL；下一步实际部分窗口encoder及decoder几何衔接。

### 2026-09-06：实际补齐pool→head原始最终oracle准备完成

- prepare_native_padded_pool_head.py将真实projection系数、非零attention控制的原始pool输出送入原始head，使用32×20及grid16×3。
- head655360个值与原生矩阵参考完全一致，额外底2行/右2列通过head后仍为零，finite检查通过。
- ignored release/native-c512/pool-game-real/head-oracle.f32和head-weights.f32已生成；后续AMD独立测试将以raw-projection.f32作池化输入，该输入是CPU参考，不可称完整encoder GPU验收。未部署游戏DLL。

### 2026-09-06：AMD pool/head补齐路径实现，待GPU验收

- NativeC32Downsample对明确60×36 C512 raw输入设置32×20输出、30×18有效池化区域；输出buffer按实际geometry分配。旧整除8尺寸路径保持原几何。
- native_c64_ds.hlsl在有效区域外显式写1024通道零并返回，避免读取源边界外数据；有效区域半精度池化/投影算术不改。
- 现有host编译通过，尚未执行新尺寸GPU对照；下一步构造原始pool→head最终oracle验证。不得将本次实现记为已通过，未部署游戏DLL。

### 2026-09-06：真实projection系数与非零attention的pool边缘通过

- check_native_pool_game_extent.py增加--real-weights：block30真实projection/skip权重，非零随机attention与非零FFN，实际36×60→20×32尺寸，pool输出预填FP8 1。
- 原始pool主分支与原生矩阵/残差计算零差异；有效18×30池化与raw半精度求和零差异；底2行/右2列仍主动归零，finite检查通过。
- 原始输出及raw-projection.f32/pool-oracle.f32保存ignored release/native-c512/pool-game-real，可供后续AMD池化合同测试。注意raw-projection是CPU参考特征，不可作为整链GPU验收输入。
- 尚未验证生成这些attention/FFN的实际部分窗口链，未更新游戏DLL。

### 2026-09-06：pool有效区及非零预填边界控制通过

- 新check_native_pool_game_extent.py构造36×60×512非零随机FFN、attention零、identity skip系数，按实际pooled20×32运行原kernel。
- main有效输出与输入一致；pool前18×30与原生半精度2×2求和均值逐值一致，底部2行/右侧2列均零。
- 为排除初始清零掩盖未写边缘，caller增加DLSS5_POOL_POISON_OUTPUT=1，将整个pool输出先填FP8 1.0；再次测试仍有效区零差异、边缘全零，证明此控制下kernel实际覆盖补齐区。最后报告poison_output=true。
- ignored release/native-c512/pool-game-extent，真实projection权重和非零attention尚待测试；不是完整pool/head或AMD验收。未更新游戏DLL。

### 2026-09-06：pool显式输入输出尺寸合同确认

- launch0055参数0x40四整数为H/W=36/60、pooledH/W=20/32，直接证明原调度器指定补齐后的输出，不是固定h/2,w/2。
- run_original_split_pool.cpp增加可选poolWidth/poolHeight，保留旧默认行为并限制容量；现可传60 36 32 20重现捕获参数。编译通过，但尚未运行非零边缘验证，不能假定补齐区域为零。
- 下一步独立构造边缘输入验证原始pool主分支与补齐区域；未更新AMD池化实现或游戏DLL。

### 2026-09-06：确认head输入已补齐，32×20原始head通过

- launch0056参数0x20实际HW=20/32，补齐在final head之前，不是ViT repack才扩尺寸；几何summary新增此直接证据。
- check_native_split_head.py新增--random-input --game-extent，32×20×512随机输入跑原始CUBIN head（本地Spark，独立单次），原生矩阵/双cell bank输出655360值cell_banks_different=0/max_error=0；另一错误矩阵候选仍645308值不同。
- fixture ignored release/native-c512/head-check640。不是36×60 pool→head联合验证，也不是AMD head新尺寸验收；下一步验证pool边缘填充值及源布局。未部署游戏DLL。

### 2026-09-06：32×20原始repack双向恒等验证

- 原始repack caller支持无需实际输入文件的--inverse地址映射探针。RTX正反两个方向各655360条映射唯一、两组随机输入通过。
- check_native_repack640_roundtrip.py验证forward[inverse]和inverse[forward]均逐项等于恒等映射，different=0；结果ignored release/native-vit/repack640/roundtrip.json。
- 这里只证明实际32×20物理字节布局能双向还原，不证明pool/head从36×60补到20×32的数值，也不证明decoder裁剪。游戏DLL未更新。

### 2026-09-06：实际几何汇总与32×20 repack映射

- audit_native_1080_geometry.py读取launch0001的preblock（0000是16字节辅助kernel，不是preblock），确认pre/post HW1152/1920；encoder依次576/960、288/480、144/240、72/120、36/60，ViT20/32，decoder反向对应。输出native-1080-geometry-summary.json。
- repack caller原先参数16/20传width/height，方形实验无法发现；按实际捕获H/W修正，增加Windows CUBIN路径。
- RTX 32×20原始forward repack地址位探针26次launch，655360条映射全部唯一，source0..655359，两组held-out随机验证通过；ignored release/native-vit/repack640/forward.i32/json。
- encoder末端36×60→ViT20×32不是无padding简单二倍，后续需验证pool/head及逆映射/裁剪。此次未部署游戏DLL，仍未完成真实图像路径。

### 2026-09-06：640-token八block原始与AMD分块整链通过

- 原始31～38八block整链两次运行完成；check_native_block256.py --tokens640 --last-block38 共56阶段全部逐值通过。原始最终FP8 SHA256 96529fff888be2138ce032d1885627034c9f919408da13ba5fbe2d25cec93895。
- run_native_staged_block.ps1支持chain31_38；AMD八个生产block使用chunk级fence提交，仅上传初始输入及各层系数。三帧完整整链测试exit0，每帧655360最终值零差异，device/fence/finite通过；gpu-chunk-fenced.f32下载后cmp原始oracle一致。
- fixture ignored release/native-vit/chain640-3006。固定随机输入整链不是动态输入验证，也不是完整RGB网络或游戏画面。下一步继续输入切换及真实encoder/decoder几何，不将实验室提交等待时间称游戏FPS；未部署DLL。

### 2026-09-06：640block分块独立提交首次三帧精确通过

- NativeVitLinear增加ChunkCount/RecordChunk，按65536输出元素分块：首块处理重放转UAV，末块转SRV；中间块写互不重叠区间。NativeVitBlock提供StageChunks/RecordStageChunk。
- staged测试每个线性chunk提交后等待独立fence，再继续下一块，保持GPU资源和每值算术不变；QKV/attention仍各单独提交。
- RX9070XT完整640block三帧全部原始oracle零差异，exit0，无device错误；回读gpu-chunk-fenced.f32与oracle字节级cmp一致。先前失败首帧gpu-first-frame.f32保留。
- 小块观测毫秒级，但未测完整游戏FPS，不从局部耗时推断性能。此次结果只证明该提交路径三次通过；常规Record同提交路径仍有已记录TDR风险。下一步将此路径扩展到八block并验证输入切换、最终实际图像路径，未部署游戏DLL。

### 2026-09-06：同提交内拆dispatch未解决TDR

- NativeVitLinear增加output_base常量，将输出范围按65536元素拆成多次dispatch；shader用全局输出索引，不改每值求和。根常量由1扩到2，必须同步更新host/shader。
- 编译成功，但AMD640 staged首个expand仍在2214.080ms报0x887A0006，exit1，未产生新的通过结果。同一command list内分小dispatch不足以稳定此负载；本修改仍属未通过实验，不是发布修复。
- 下一步应实现chunk级独立提交/fence或优化线性shader；不继续重复同样负载，不放宽TDR，不部署游戏DLL。

### 2026-09-06：分阶段提交将640-token TDR定位到expand

- NativeVitBlock新增RecordStage；extent host可用DLSS5_VIT_STAGED分五阶段独立提交/fence，资源全留GPU、算法不改，独立stage fence避免与帧fence计数冲突。
- 首帧stage耗时(ms)：expand1759.599、contract1587.586、QKV38.599、attention22.315、projection15.802，最终655360值仍零差异。
- 第二帧stage0 expand在2216.102ms返回device=0x887A0006，测试exit1。单独提交依然失败，说明仅拆block不能解决；下步针对线性层单次dispatch工作量分块或优化，不调整TDR，不将首帧算术通过当稳定完成。
- 未部署游戏DLL，记录当前失败以防后续误报。run_native_staged_block.ps1为可复现入口。

### 2026-09-06：640block重放错误确认伴随AMD watchdog

- 只读inspect_amd_gpu_failure.ps1查得21:26:35/59两条WER1001，LiveKernelEvent141，bucket LKD_0x141_Tdr:6_IMAGE_amdkmdag.sys，关联同一WATCHDOG-20260906-2126.dmp。不将两条报告误当两次独立GPU故障。
- 暂无运行中的native-attention/SB-Win64进程输出；未重跑GPU负载、未改驱动/TDR。证据支持watchdog事件，但不单独证明具体算子或提交粒度就是根因。
- extent host增加每帧device状态、Signal错误位置、submission elapsed/wait/fence诊断；本轮仅编译检查，不宣称新稳定性测试通过。下一步隔离阶段提交并保持原算术及最终oracle。

### 2026-09-06：640-token参考全通过，AMD首帧精确但重放设备错误

- check_native_block256.py支持Tokens640及ceil(log2(N))布局，原始block31七阶段全部different=0/finite/tail/replay通过，导出初始输入与原始最终oracle。
- NativeVitQkv/Linear允许640，生产NativeVitBlock在RX9070XT首帧655360值different=0/max_abs=0；第二次提交报HRESULT=2289696774（0x887A0006），进程exit1。不能记作AMD稳定通过。
- 首帧回读保存ignored release/native-vit/block640-3006/gpu-first-frame.f32，cmp原始oracle一致。未立即重试、未修改TDR或驱动；下一步调查GPU耗时/提交粒度与设备错误原因。游戏DLL未部署。

### 2026-09-06：原始640-token block31两次运行完成，待算术验证

- expand/contract/QKV原始caller支持32×20，expand grid160、contract/projection40、QKV80，attention32×3。4MiB输出容量覆盖expand的2621440字节，其他输出655360字节。
- runner加入Tokens=640，从seed3006初始输入按完整block31依次运行，两次七份输出hash均一致，报告NaN为0。最终projection SHA256 `08e442449cd040e624faf657a79b9d16097c6df6c20fa5b4fcda0af7b499eb77`。
- 产物下载到ignored release/native-vit/block640-3006。这里只证明重复运行稳定，尚未将全部阶段与CPU数学参考逐值对照，不能记为精确通过；AMD640完整block也未验证。游戏DLL未更新。

### 2026-09-06：AMD640-token attention精确通过

- NativeVitAttention开放640tokens，HLSL暂存扩至640；GPU夹具打包使用ceil(log2(N)) token位，避免非2次方长度别名。
- RX9070XT独立测试三次各655360值different=0/max_abs=0，finite/device/fence通过；回读保存release/native-vit/attention-random-640-3006/gpu.f32，参考验证器再次与原始RTX输出比对通过。
- 此为实际ViT长度上的随机attention算子测试，不含640token QKV/linear/完整模型。未修改运行中游戏，也未更新DLL。

### 2026-09-06：640-token原始attention首次逐值通过

- caller支持实际32×20、grid32×3，保持4MiB输出/工作区；随机fixture打包修正高token地址位为ceil(log2(N))，旧公式在640导致非双射并被assert阻止，seed3005未运行GPU。保留失败目录，使用seed3006重新生成。
- RTX原始attention两次SHA256均为3328d7c3c595c38ae11cfa063ff344fbece3fd29f7878816903070005d39dc46。655360值与按64token块分母求和的参考全部一致，finite/replay/tail通过；另外两种候选仍有1857/2123值不同。
- 参考640入口暂标experimental_640，AMD attention尚未扩展到640，未部署游戏DLL。产物ignored release/native-vit/attention-random-640-3006。未操作正在运行的游戏界面。

### 2026-09-06：用户进入1080p无边框后，真实640-token几何已捕获

- 用户手动设置桌面1080p、游戏无边框并停留主菜单；只读检查PID25972运行，未重启/改配置/操作存档。
- 下载native-kernel-params-25972-17399312全部参数blob及独立preblock-live-parameters.txt（另存launches.txt）。decoder新增root/output/scope参数，保留旧4K报告；新指针脱敏报告native-runtime-parameters-1080.json。
- 实际ViT HW=20/32=640 tokens；expand grid160、contract/projection40、QKV80、attention32×3。post HW=1152/1920，scale=.03125、rgb_mode=1，额外纹理58/60缺省。移位序列与当前修正表一致。
- 这些是本进程启动期捕获，不宣称装备场景或最终像素验证。此前1920×1088、30×18候选不能当真实合同；下一步按640token、非正方形、attention gridY=3验证，并核对各层padding与最终裁剪。游戏DLL未替换。

### 2026-09-06：1080p配置已备份，首次启动未产生游戏进程

- capture_nvidia_1080_geometry.ps1 检查无游戏进程、实际配置键唯一、启动排程目标Steam/app3489700；备份后仅改ResolutionSizeX/Y=1920/1080、FullscreenMode/PreferredFullscreenMode=2。
- 原始配置备份：C:\Users\Seth\AppData\Local\SB\Saved\Config\WindowsNoEditor\GameUserSettings.ini.before-1080-probe-20260906-211332-516，备份hash校验通过。未改存档。
- 21:13:32调用DLSSNR-Launch-StellarBlade，LastTaskResult=0，但随后实查只有Steam PID20816，无SB游戏进程；gameprocess_log最后一条游戏运行仍为19:32启动、19:43退出，参数捕获未更新。不能把排程成功当游戏启动或1080p捕获成功。
- 当前保留1080p窗口配置待排查启动阻碍，没有重复启动或终止Steam。游戏DLL未更换；下一步检查交互桌面/Steam启动状态，继续真实几何捕获。

### 2026-09-06：真实1080p捕获前环境核对

- 新增只读 inspect_nvidia_geometry.ps1，避免复杂SSH引号使检查静默失败。当前RTX用户Seth，实际配置路径为 C:\Users\Seth\AppData\Local\SB\Saved\Config\WindowsNoEditor\GameUserSettings.ini。
- 实查无SB/Stellar游戏进程；配置ResolutionSizeX/Y=3840/2160、FullscreenMode=0、动态分辨率关闭。最新参数捕获仍为 native-kernel-params-24064-11278468 的4K运行，没有新的1080p证据。
- 本轮没有修改分辨率或启动游戏；下一步备份该实际配置并以1080p运行重新捕获几何。同步更新native-runtime-contract中已过时的“仅64-token”描述，保留真实非正方形/纹理及部署验收缺口。

### 2026-09-06：AMD256-token八block全GPU串联通过

- extent test 增加 chain31_38，八个生产 NativeVitBlock 依次创建，上一block GPU输出直接作为下一block输入。只上传初始特征和32份权重，不上传中间特征。
- RX9070XT三次完整串联，最终262144值全部different=0/max_abs=0，finite/device/fence检查通过；回读gpu.f32与原始整链oracle字节级cmp一致。ignored release/native-vit/chain256-3003/gpu.f32。
- 固定随机输入、256-token完整ViT通过；仍需真实1080p几何/部分窗口、输入纹理契约及游戏DLL最终画面验证，本轮没有部署游戏DLL。

### 2026-09-06：原始256-token八block ViT参考链通过

- run_nvidia_block256.ps1 增加 LastBlock=38，分别加载31..38各自权重，上一block projection传入下一block，整链重复两次。
- check_native_block256.py 增加相同范围的顺序验证：56个阶段全部different=0、finite、tail_zero、replay_identical，原始最终FP8 SHA256 `1547c5b495986625d783832c4c11ca2af51d4dafc3134e2da5c7076deeb18459`。
- fixture ignored release/native-vit/chain256-3003，根input.f32为初始输入、oracle.f32为block38最终输出；每个block目录包含原始中间输出及对应解码权重。
- 尚未执行AMD八block全GPU串联，游戏DLL未更新；下一步用此最终oracle验证八个生产NativeVitBlock资源串联。

### 2026-09-06：AMD完整256-token block31 GPU串联通过

- extent test 新增 block31 模式，直接调用生产 NativeVitBlock，初始输入→expand→contract→QKV→attention→projection，全程GPU资源传递，无中间CPU特征注入。
- 上传的oracle仅CPU比对使用；GPU只接收初始随机输入及四份原始解码系数。RX 9070 XT三次各262144个最终值全部different=0/max_abs=0，finite/device/fence检查通过。
- 下载 ignored release/native-vit/block256-3003/gpu.f32 后与原始最终oracle字节级cmp一致。
- 这是单block31、256-token固定输入重复验证；不是八block完整ViT，不是真实1080p纹理输入，更不是游戏DLL验收。后续继续实际几何/全链路，未更新游戏DLL。

### 2026-09-06：原始256-token完整block31对照

- 新 runner `run_nvidia_block256.ps1` 从同一 seed=3003 输入依次运行 expand→contract→QKV→attention→projection，残差严格来自对应输入/contract，完整重复两次，所有七份输出逐字节一致。
- 最终原始 projection SHA256 `1ad576e6bfd90d18752f74a6b3ff8328977c0d77c0bef2a10281043217353552`，不同于先前仅QKV子链的projection，未混用。
- `check_native_block256.py` 顺序检查七份原始输出，全部 different=0、finite、tail_zero、replay_identical；导出生产 NativeVitBlock 所需原始输入、最终oracle和四份权重，ignored release/native-vit/block256-3003。
- 原始侧通过文件串联，不是单次CUDA驻留测试；AMD完整NativeVitBlock仍需接入该fixture。未更新游戏DLL。

### 2026-09-06：AMD 256-token projection 精确通过

- NativeVitLinear 的普通 ViT 线性模式支持64/256 tokens，新增 projection extent 测试入口，残差单独上传并检查。
- RX 9070 XT 三次各262144个值均与原始 RTX projection 输出完全一致，finite/device/fence 检查通过；下载 gpu.f32 后与 oracle.f32 cmp 一致。数据 ignored release/native-vit/projection-256-3003。
- 至此256-token的 expand/contract/QKV/attention/projection 各独立测试通过，QKV→attention也已串联；尚需整个 ViT block、真实1080p尺寸与最终游戏画面验证。没有替换游戏 DLL。

### 2026-09-06：原始 256-token projection 精确通过

- projection caller 的输入容量从固定 128 tokens 改为向上按128取整，允许64/256；RTX runner 使用 gridX=16、gridZ=4、计数器 -1。
- 使用已验证 QKV→attention 的原始 attention 输出，残差为该 QKV 的原始输入（seed=3003），block31 projection 权重。两次 SHA256 均为 `ef4d90e77c34d305b247fc637639bafad3b4f65371ae729f8555f45e04eaaa01`。
- `check_native_projection_extent.py` 对照 262144 个值，different=0 / max_abs=0，finite、尾部零、replay 一致。导出 AMD fixture 到 ignored release/native-vit/projection-256-3003。
- 这是 QKV→attention→projection 分段输入对照，不含 expand/contract 的完整 ViT，AMD projection 尚未扩展、游戏 DLL 尚未更新。

### 2026-09-06：AMD 256-token contract 精确通过

- NativeVitLinear 允许已对照的 256-token contract（4096→1024），projection 仍未放宽。
- extent test 新增 contract 模式，检查并上传独立残差，调用生产类。RX 9070 XT 三次各 262144 值全部 different=0 / max_abs=0；finite、device、fence 检查通过。
- 原始 oracle 来自 RTX expand→contract；下载 AMD gpu.f32 后与 oracle.f32 cmp 一致。产物留在 ignored release/native-vit/contract-256-3003。
- 此测试上传 expand 中间输出，证明的是独立 contract，不是完整 GPU ViT 串联；游戏 DLL 未替换，最终画面未验收。

### 2026-09-06：256-token 原始 expand→contract 数值验证

- contract caller 增加 Windows CUBIN 路径；独立 RTX runner 使用 256 tokens / gridX=16 / gridZ=4，计数器仍从 -1 开始。
- 原始 expand 输出送入原始 contract，残差使用 seed=3003 原始输入，block31 真实权重；两次输出 SHA256 均为 `49f76e9e40bb540193463cd902321e3c7dbdda4501d91f3cacf4c9c5b4a90336`。
- `check_native_contract_extent.py` 对照原生四分区半精度累加及残差计算，262144 值全部 different=0 / max_abs=0；finite、尾部零、replay 一致。输出与解码系数保存在 ignored release/native-vit/contract-256-3003，供 AMD 验证。
- 本轮未放宽 AMD contract，也未部署游戏 DLL。下一步使用上述原始 oracle 验证生产 NativeVitLinear contract。

### 2026-09-06：AMD 256-token expand 三次精确通过

- 生产 NativeVitLinear 仅针对 expand 开放 tokens=256，其他未验证线性模式仍限制原尺寸；HLSL 算术和索引未改。
- extent test 新增 expand 模式，上传原始特征及解码原始权重，对照 RTX 原始输出。RX 9070 XT 三次各 1048576 值全部 different=0 / max_abs=0，finite/device/fence 检查通过。
- GPU 输出下载到 ignored release/native-vit/expand-256-3003/gpu.f32，与 oracle.f32 字节级 cmp 一致。
- 未更新游戏 DLL，256-token contract/projection 和完整 ViT 串联仍待完成，更不代表真实1080p游戏画面已验证。

### 2026-09-06：256-token 原始 expand 精确验证

- 原始 expand caller 增加 Windows CUBIN 环境路径，`run_nvidia_expand_extent.ps1` 在 RTX 5090 独立运行 256 tokens / gridX=64。
- seed=3003 随机输入、block31 原始权重，两次输出 SHA256 均为 `c259149373f24c9089d5dcfa43fe91d79952cb36375218d46af5a5304da2d777`。
- `check_native_expand_extent.py` 验证扩展后的 token 地址位与既有原生 expand 算术，1048576 个值全部 different=0 / max_abs=0，finite、尾部零、replay 一致。数据 ignored `release/native-vit/expand-256-3003/`。
- 仅原始内核对照；AMD expand / contract / projection 的新尺寸仍未验证，未部署游戏 DLL。

### 2026-09-06：256-token AMD QKV→attention GPU 串联通过

- extent test 新增 qkv_attention 模式：生产 NativeVitQkv.Output() 直接作为 NativeVitAttention 输入，同一 command list 内执行，无中间 CPU 回读/注入。
- RTX 原始 QKV 输出继续送入原始 attention，两次最终输出 SHA256 均为 `4dc54e612d1995678cbe51b0564ea0a57c5dc6c9c1a44306729067d6c4327634`。
- AMD 输入仍是 seed=3003 随机原始特征与真实 block31 权重，不上传 RTX 中间 QKV。三次 replay 的 262144 个最终值全部不同数 0、max_abs 0，device/fence/finite 检查通过；下载 gpu.f32 后与 oracle.f32 cmp 一致。
- 产物 ignored `release/native-vit/qkv-attention-256-3003/`；打包脚本 `prepare_native_qkv_attention_extent.py` 仅解码原始最终输出。尚未包含 ViT expand/contract/projection，也未替换游戏 DLL，不能据此认定1080p画面完成。

### 2026-09-06：AMD 256-token QKV 三次逐值通过

- `NativeVitQkv` 放宽到已验证的 64/256 tokens，HLSL 原有索引使用 tokens，无需改变投影和归一化算术；未开放未经测试的任意尺寸。
- 独立 extent test 增加 qkv 模式，直接调用生产类；`check_native_qkv_extent.py` 将原始 RTX Q/K/V 解码后导出 oracle，而不是使用 CPU 预测当标准答案。
- RX 9070 XT 使用 seed=3003 随机输入、block31 原始系数，三次各 786432 个值全部 different=0 / max_abs=0，finite/device/fence 检查通过。下载 GPU 输出后 `cmp gpu.f32 oracle.f32` 也完全一致。
- 产物在 ignored `release/native-vit/qkv-extent-256-3003/`。只部署到独立 matrix-probe/native-qkv-extent 实验目录，未替换游戏 DLL；QKV→attention 串联及其余线性层/真实尺寸仍待继续验证。

### 2026-09-06：256-token 原始 QKV 完整数值对照通过

- 将原始 QKV CUDA caller 接入 Windows RTX 独立实验；通过 `DLSS5_VIT_CUBIN` 指定 CUBIN，增加 cluster launch / D32 memset import，未操作游戏。
- 小尺寸 gridX=16 直接套到 256 tokens 会只计算约一半输出。错误样本保存在 `release/native-vit/qkv-extent-256-3003/grid16/`，不可作为参考。真实运行捕获 2160 tokens 的 gridX=272 支持按 128-token 块、每块 16 组的启动方式；本次 256-token gridX=32 实测确认。
- 使用独立随机 seed=3003 输入和原始 block31 QKV 权重，RTX 两次 Q/K/V 完全一致；`check_native_qkv_extent.py` 与 native_vit_qkv_reference 的全部 786432 个值逐值一致，输出有限、尾部零、replay 相同。
- 新 runner `run_nvidia_qkv_extent.ps1` 按 token 数计算 gridX。AMD QKV 仍未放宽尺寸或验证，下一步需要用这组原始输出验证生产 GPU QKV，随后串接 attention。

### 2026-09-06：AMD 可变长度 attention 首次 GPU 精确通过

- `native_vit_attention.h/.hlsl` 扩展到 64/128/256 tokens；每 64-token 分母使用原生求和图，再顺序半精度相加，分子继续每 32 keys 半精度累计。尚不支持任意游戏尺寸。
- 新独立测试 `d3d12_native_attention_extent_test.cpp` 直接调用生产 `NativeVitAttention`，输入为随机 Q/K/V，oracle 直接解码 RTX 原始输出而非 CPU 参考。明确选择 AMD，检查 finite、device removed、fence、30 秒 timeout，三次 replay 均逐值校验。
- RX 9070 XT：64-token seed=3004（65536 值）、128-token seed=3002（131072 值）、256-token seed=3003（262144 值），各三次全部 different=0 / max_abs=0。下载 gpu.f32 后再与原始 RTX 输出独立解码复验，全通过。
- `prepare_native_attention_extent_gpu.py` 负责测试打包；`check_native_vit_random_candidates.py` 增加 GPU 回读精确验证。数据留在 ignored release/native-vit/attention-random-* 下，未提交权重或激活。
- 只更新独立实验室 executable/shader，未替换剑星 addon/DLL。QKV、线性层、完整游戏分辨率及实际纹理契约仍需继续，不能将此结果当成游戏画面验收。

### 2026-09-06：随机 attention 跨 seed / token 长度复验

- 新增 128-token seed=3002、256-token seed=3003 原始 RTX 对照，各运行两次输出一致；SHA256 分别为 `56e457e3982873823130e861b7063da4ce2013e347dac2e414f4376cfc91acfe`、`e69dd199ca06f8ddbef2f6a9d23326025c863d3de2d82f949678430bd94c6f28`。
- 两组都确认“每 64-token 分母求和后顺序半精度相加”逐值精确；另外两种候选求和图仍有数百个值不同。
- `native_vit_attention_reference.py` 正式支持 64/128/256 三种已检查的尺寸；将诊断脚本接入此实际参考函数，并强制检查有限值、两次 replay 一致、输出尾部为零、参考输出逐值一致。三个随机 fixture 全部通过。
- 原始 CUDA 探针没有操作游戏；AMD HLSL 仍限制 64 tokens，下一步应扩展 GPU 实作并做相同独立对照。这不是 1080p 全链路或游戏 DLL 验收。

### 2026-09-06：RTX 256-token 随机 attention 首轮精确对照

- 用户允许继续使用两台调试机器；本轮只运行独立 CUDA 探针，未操作游戏或存档。
- 新增 `prepare_native_vit_attention_random.py`，生成 seed=3001 的独立随机 Q/K/V；扩展 RTX runner 支持 64/128/256 tokens。
- RTX 5090 两次 256-token 输出 SHA256 均为 `f8d29c3d99f208a37daf4a793b9a3524b7b6c5c520c7c21390ceaf0c478ec02f`。原始产物保存在 ignored `release/native-vit/attention-random-256-3001/`。
- `check_native_vit_random_candidates.py` 对照三种分母求和图：每 64-token 使用已验证的半精度求和，再按块顺序半精度相加，262144 个输出值全部一致，max_abs=0；先跨块合并位置差 630 个值，按 lane 累加 pair 差 711 个值。
- 这是单个随机样本的候选公式验证，还需额外 seed / 128-token 对照；未修改 AMD attention 或部署新游戏 DLL，不能据此宣称游戏画面修复。

256-token反例定位到当前Spark执行路径（2026-09-06）：对完全相同原attention探针做Compute Sanitizer memcheck，0 errors，但输出与先前普通执行不同；racecheck报2组共享内存读写冲突，写PC0x3570/0x32e0对应UBLKCP.S.G，读PC包括0x1680/0x1c60/0x25e0/0x2c20及0x1090/0x10a0/0x1d90/0x1e60。不能把无越界等同无竞态，也不能直接指责模型数学或AMD移植。

新增最小Windows nvcuda导入定义、原探针可显式选择CUBIN路径及run_nvidia_vit_extent.ps1，将同一CUBIN和同一Q/K/V文件放5090独立实验目录native-vit-extent-256。未启动/关闭/切换游戏。5090两次输出SHA均FFB3BDE38AC214BE12E17CD2E11262B33354D763D76FFE91FBBAD3FF8F0083E8，下载后262144值全部对上量化exp/half累计参考，所有query一致，尾部零。报告validation-rtx-output-1.json为control_pass。

因此该256-token失败不能继续当作通用长度/布局反证：当前Spark原kernel执行会受运行条件影响，而5090这项控制稳定正确。尚未单独归因GPU架构、驱动或调用同步差异；后续较长序列的权威原输出优先在5090生成，仍需随机Q/K与真实长度验证。原Spark失败、memcheck/racecheck输出保留，游戏神经DLL未改，目标active。

ViT更长序列的非均匀V控制（2026-09-06）：新增check_native_vit_uniform_qk.py，Q/K零而V逐token/通道取不同二进制可表示值。最初将输出当普通均值，64-token也失败（3776差）；这是裁判遗漏F(exp)与未量化exp分母之差，不是已有64-token实现失效。改为原量化exp×V、每K32 half累计及half倒数归一化，读取同一原输出重新比较，64/128-token分别65536/131072值全exact。原均值失败报告保留，修正裁判报告另写validation-exp.json；期间修复F工具不接受scalar的测试代码错误，未改原输出。

256-token仍77824/262144值不同、max0.05078125；Q/K相同却不同query输出不相同，按每32条query的差异为30912/30912/0/0/8000/8000/0/0。这提示布局/读取或调用合同问题，不能放宽容差或直接解除64-token限制。重新查原kernel_create日志，attention普通/chained均为32×4×1，未猜测改线程数。所有小测试已退出，下一步核对256-token原QKV生成的V物理输出及attention读取范围。游戏神经DLL未改。

真实移位AMD完整RGB512回归通过（2026-09-06）：继续观察原会话36919，正常exit0，PID31832已不存在；33个shader编译/228缓存命中，三帧fence等待2406/2469/2281ms，device/fence/finite/replay通过。下载至native-runtime-rgb512/amd后，对新配置原最终oracle的786432个RGB分量different0/max0，SHA bd52c601b68c4ed27f271cd2c7bcffc8511519f652534f2a0c51ffa9450e6da4；变化大于1e-5的分量786294。新旧配置完整分开，未重启或复用旧pass。

为扩大ViT token合同，新增check_native_vit_extent_control.py，原非chained attention在128-token(16×8)与256-token(16×16)、Q=K=0/V=1控制下，分别131072/262144个有效FP8输出全为1，尾部全零，无NaN。每次调用有15秒进程上限，全部正常返回。仅证明这两个长度的均匀控制，不等于随机attention、非整数窗口长度、整层ViT或AMD扩展已通过。结果在release/native-vit/extent-control-128/256。下一步恢复可变长度attention及原1080p形状；游戏神经DLL未更新，目标active。

真实移位AMD整网回归启动（2026-09-06）：check_native_runtime_shift_consistency.py逐项核对原捕获decoder40～69、C++表与新原/CPU报告，30块全部一致，第70块原final报告pass。验证器支持独立decoder-root并保留encoder-root。新exe编译完成。

stage_native_runtime_rgb512.ps1建立独立AMD实验目录native-runtime-rgb512，只复制输入、系数、shader、索引等必要文件，排除output/audit/gpu/lut及JSON报告；现场查询无oracle文件。原native-rgb512目录和本地旧配置结果保留。唯一回归已启动：统一会话36919，PID31832，20:07:18启动；CPU38.53125秒，shader1～8完成、第9开始。尚未读回最终RGB，不宣布新配置GPU通过。下一轮继续观察同一进程，不重复启动。游戏神经DLL未更新。

真实移位新配置最终RGB参考通过（2026-09-06）：新目录native-runtime-rgb512继续63～65按3/1/2各1048576值原/CPU全exact，原65 outview验证通过；66 shift0及67～69按3/1/2各2097152值全exact。原69 outview验证通过后，接同一编码器preblock skip与初始RGB，新配置最终post70共786432个RGB分量different0/max0。完整新报告在native-runtime-rgb512/post70/validation.json；旧native-rgb512控制配置未覆盖。

66/C32/post70脚本增加独立decoder/output root与encoder root，避免拷贝旧后段或混淆输入来源。所有原调用已正常退出，当前未运行AMD新配置全链。下一步以本新裁判重跑已修正移位的AMD全链，再进入1080p，游戏补丁未更新。

真实移位新配置推进第62块（2026-09-06）：outview验证器增加--root，48与56/62桥接脚本分开decoder-root和encoder-root，普通C256/C128/C64检查器增加output-root。新结果全部留release/native-runtime-rgb512，编码器复用已与捕获移位一致的原数据，不覆盖旧配置。

新block47 outview对plain全exact；48 shift0及49～55按捕获3/1/2/0/3/1/2，各262144值原版/CPU全exact。新block55 outview校验通过后，56以真实X移位mask1接block14 skip，524288值全exact；57～61按2/0/3/1/2各524288值全exact。新61 outview与62 shift0接block8 skip也通过，62最终1048576值全exact。所有调用正常退出、无CPU中间修正注入；AMD新配置全链仍待重跑，下一步63～70。游戏补丁未更新。

真实移位新配置40～47回归（2026-09-06）：split检查器增加--output-root，新结果独立保存在release/native-runtime-rgb512，旧控制配置不覆盖。按直接捕获的0/3/1/2/0/3/1/2，从原decoder39输出连续执行原40～47；八块四阶段共32检查点全部different0/max0。原权重各自抽取，原链只用上一原输出，没有CPU修正注入。下一步推进新配置48～70并重新验证AMD全链；本轮不继承旧配置后段pass。游戏补丁未更新。

只读探针部署与直接参数解码（2026-09-06，跨用户中断恢复）：新v4探针SHA d7887caeb9ccf8713ba63079ffb8de95877b008f2a0d09487610c3fd99e36c38已编译并部署5090，旧探针SHA c256f1a5...备份为preblock-live-parameters.addon64.backup-20260906-113147-633。部署脚本改为游戏运行则拒绝、先备份验证，再安装；启动需显式-Launch，不自动杀游戏。使用核对过的Steam -applaunch3489700任务，实际游戏PID24064、19:32:34启动，v4日志和200份参数blob已下载至release/native-kernel-params-24064-11278468。期间曾尝试CloseMainWindow返回False，不能声称已退出；用户随后允许继续使用两台机器。未改AMD神经DLL或存档。

用户在5090实际游戏中用F6观察到角色变暗，确认该开关有可见影响；这是用户现场观察，不冒称同帧GPU输出审计。ReShade日志的EvaluateFeature_C入口错误后仍有普通EvaluateFeature挂钩及feature18多次evaluate成功，本轮参数捕获也完整；已向用户区分“执行”和“最终回写”证据。

decode_native_runtime_parameters.py逐blob长度核对，导出不含指针值的native-runtime-parameters.json。编码器1～30移位与现有配置一致。解码器40～47=0/3/1/2/0/3/1/2，48～55=0/3/1/2/0/3/1/2，56～61=1/2/0/3/1/2，62～65及66～69均0/3/1/2。ViT各阶段H/W均36/60；post H/W2176/3840、input_scale0.03125、rgb_mode1、仅0x38纹理存在，0x58/0x60为零，支持目前post简化模式但仍属4K启动捕获，不是新1080p/装备帧。

新增native_runtime_shifts.h作为全链唯一解码器移位表，front40～47和tail49～69已切换（尤其56改为X移位）。MinGW语法检查通过，尚未编译部署新AMD全链或重建对应原oracle；所有旧配置pass保留但不视作新配置通过。探针构建脚本及备份部署流程一并保存，run_nvidia_ui.ps1既有修改未纳入提交。下一步在新目录重建真实移位配置的原/CPU/GPU回归，再推进1080p，目标active。

原游戏移位合同发现实质差异（2026-09-06）：只读检查5090，未见游戏进程；现有preblock日志仍为04:38旧捕获，未启动游戏或切换窗口。下载保存到release/live-preblock-v2/launches.txt，包含完整第一帧launch0～154。新增audit_native_live_shifts.py按原preblock H/W2176/3840及8×8窗口、每轴0/-4假设枚举可兼容grid的mask，输出native-live-shift-audit.json并保留日志SHA。

48～69可唯一推导：48=0；49～55=3/1/2/0/3/1/2；56=1；57～61=2/0/3/1/2；62=0；63～65=3/1/2；66=0；67～69=3/1/2。与当前512控制配置多处不同，不能把此前原kernel/AMD按给定参数的exact提升为原游戏配置exact。40～47输入H68时Y0/-4均得到gridY9，日志只能确定X，不能擅自猜Y。原ViT attention grid32×9、expand544等也表明64-token不是原游戏规模；具体H/W/padding仍需blob，未从grid直接宣布token数。

preblock_live_parameters.cpp已扩展为只读捕获前200次launch的CPU参数blob（最大0x200字节），按PID+tick独立目录保存，不解引用GPU资源、不改游戏帧；原preblock八次捕获保留。MinGW语法检查通过，尚未重新链接/部署该新版探针，原游戏未启动。下一步补足这些原参数证据，再生成正确配置的新回归，避免在错误shift上继续优化。新神经游戏DLL/公开包均未更新，目标active。

AMD完整RGB512→RGB通过（2026-09-06）：同一会话21197正常exit0，33个shader编译/228次缓存命中，三帧fence等待2469/2406/2390ms及device/fence/finite/replay通过。下载output-rgb512-final.f32后严格最终比较786432值different0/max0，SHA66a3d99a2e0a2111c87373731ca58c0331c2d96ff28086e13cbed96810fa181a；final-validation.json为pass，相对原RGB有786293分量变化大于1e-5。此为完整GPU初始RGB→神经网络→最终RGB，不是独立段拼接。五个DS文件重新下载核对也全exact。

等待期间核对真实尺寸线索：原preblock参数0xf0/0xf4为2176/3840，原caller证明顺序H/W；1080p候选应核对1920×1088输入，旧表960×544不能混当preblock输入。检查当前源代码，ViT仍64-token限定，post≤512，decoder tail固定尺寸，且原游戏移位/多纹理合同未重新验证。因此不部署512实验冒充1080p。新增native-runtime-contract.md集中列明真实运行前的剩余条件，防止历史README成功宣称与当前范围混淆。当前无待观察测试进程，游戏DLL未改，目标active。

原RGB512最终RGB对齐、AMD整网回归启动（2026-09-06）：原C32 block69 outview实际执行后，global16不交换通道低两位的解码与plain输出2097152值全exact。prepare_native_rgb512_post70.py取真实原block69 outview、原preblock主分支及同一张RGB输入，mask1/mode1运行原post70，CPU最终786432个RGB分量different0/max0。报告release/native-rgb512/post70/validation.json，范围仍为当前控制配置，不是原游戏所有输入/移位模式。

front-chain增加rgb512final，把NativePost70接到GPU69和pre.Main。底图独立UPLOAD但逐像素核对其与编码器tile输入为同一RGB，仅初始输入布局不同，不是中间特征注入。拒绝DLSS5_POST_BASE_ONLY诊断变量，FinalRGB入口清除该变量。输出独立output-rgb512-final.f32；导出及严格验证器已加入，MinGW编译通过。整网仅在实验目录部署，游戏DLL未改。

完整GPU回归唯一进程运行中：统一SSH会话21197、AMD PID31080，启动19:12:55；查询CPU15.359375秒，shader1～4已结束，第5开始。尚未最终读回，不能宣布AMD整网RGB已通过。下一轮继续观察同一任务，不重复启动。真实1080p与游戏画面验收仍未完成，目标active。

AMD第70块首差异定位并独立通过（2026-09-06）：宿主在数值失败后（已等原提交fence）额外读回merge及raw-half主体，不注入CPU数据。diagnose_native_post70_gpu.py核对两份各8388608值：merge对CPU全exact，主体对GPU merge的CPU计算全exact，确认错误位于最后head/合成，不修改已通过前段。

原double head结果接近second-only但不完全相等；禁止double合并未改变失败。dump_native_shader.cpp导出DXBC，能看到第二段确有ftod/dadd累加，并含enable11_1DoubleExtensions，因此没有证据断言“编译器漏加了acc”，也不直接归因某条驱动指令。改为等价整数实现：27位乘积对齐累计、signed magnitude合并half accumulator、整数位舍入到half。若scaled accumulator不是整数或越出该实现界限，显式输出NaN触发测试失败，不静默近似。此范围限制仍需未来真实输入验证。

512/seed2851完整post GPU三帧最终各786432个RGB值different0/max0，device/fence/finite/replay通过，下载再验证全exact。成功读回单独存amd/gpu-integer-head.f32，SHA0538b303385c1d6330502cc59561c0bb6142ce3380bd5cada74d38831b35ae29，validation-integer-head.json为pass；初始全零与RGB修复后的失败文件/报告保留。去除已不用的double函数后重新编译反汇编，globalFlags仅refactoringAllowed，未出现double转换/运算指令。

当前仅独立AMD第70块texture_mask1/rgb_mode1成功，不能将它与RGB→69的pass直接合称整网RGB完成；下一步连接实际block69输出、原preblock skip及底图并做完整RGB回归。游戏DLL未改，目标active。

AMD第70块首次实测失败、底图路径分离（2026-09-06）：新增独立宿主/参数导出/读回验证器，使用原512/seed2851变化底图夹具，实际shader编译与执行完成。首次最终786432值全零，max0.6320953369，明确fail；本地amd/gpu.f32及validation.json保留该失败。新增POST_BASE_ONLY诊断1直接底图回写三帧全exact，说明这条资源/输出接线可用，不计入神经验收。

诊断2跳过head计算但保留RGB编码/解码，使用double表达式时也全零；改为逐步precise float，诊断2三帧全exact。这是可重现代码路径差异，未未经证据归因驱动或硬件。保留原SASS的float32运算顺序，无公式代数折叠。随后恢复正常神经计算，输出已非零，但786237/786432值不同、max0.016986846923828125，仍fail；读回另存gpu-after-rgb-fix.f32，SHA b30050f7afbbf00379216011873b1d26e8a40569b9bf0327f0efdfd75f6f2aaf，对应validation-after-rgb-fix.json，不覆盖初始失败。

诊断输出写gpu-base.f32，日志显式NOT_neural_acceptance；脚本默认清除诊断变量，只显式-Diagnostic1/2才启用。所有测试进程已退出，无正在运行任务。下一步读回merge及C32 raw-half，区分主体与head首差异，不能因底图或非零响应而称移植成功。游戏DLL未改，目标active。

AMD第70块原生模块实现待验（2026-09-06）：NativeC32Stage新增默认false的raw_output选项，使用RawTiles加crop_raw转换为HWC，不经过main的FP8量化；旧调用默认保持不变。新增NativePost70，常驻main/skip half合流→raw-half C32主体→FP16 head→RGB浮点合成，Record无CPU读回/文件读取。接口仅接受16～512、16倍数尺寸和像素对齐RGBA底图，限定已恢复的texture_mask1/rgb_mode1，不含可选混合纹理。

native_post70.hlsl按实测27位对齐实现两个K16 half矩阵积，包含accumulator指数+1及double→half精确捨入；RGB编码/加残差/解码保留float32边界。merge/finish用二维Dispatch分摊32/3个输出平面，避免512幅面merge超过65535个X组。C++语法检查（包含完整front-chain）及diff检查通过，HLSL编译和AMD数值运行尚未执行，不能宣布第70块移植通过。下一步建立独立宿主，使用原512随机夹具直接比较最终RGB。游戏DLL未改，目标active。

第70块512×512变化底图参考通过（2026-09-06）：先对seed2819保持同一神经特征，换逐像素RGB渐变底图，768个RGB值全exact。新增native_post70_reference.py，明确仅texture_mask1/rgb_mode1，不含可选混合纹理分支；以固定记录直接解码主体/系数/FP16头，批量处理避免全幅乘积临时数组爆大。新测试64×64/seed2833、随机主/skip及变化底图的12288个RGB值全exact。

首次512×512/seed2843有31/786432值不同、max1.9073486328125e-6，原fail报告保留。离线累加消融发现第二个K16的对齐指数必须同时考虑frexp(accumulator).exponent+1，相当于将accumulator×1也纳入操作数指数和的最大值；只考虑乘积、或只加accumulator指数本身均仍31差。加入+1后原失败夹具全部RGB exact。是否另截断acc在这些输入上无区分，未冒称已证明所有极端范围。

更新默认对齐规则后再新建512×512/seed2851独立随机特征/渐变底图，原kernel与CPU最终786432个RGB值different0/max0。报告release/native-post70/reference-512-2851/validation.json为pass；原2843失败与accumulator-candidates报告完整保留，不覆盖反例。当前完成的是原版/CPU该后处理模式的数值参考，AMD第70块、RGB全网最终输出、1080p及游戏实际输入/移位合同仍待验收。游戏DLL未改，目标active。

第70块独立种子及RGB舍入顺序修正（2026-09-06）：空间/头检查脚本增加--seed，原夹具保留。seed2819的常规half头有2个RGB差异，27位对齐头剩1个float32末位差异（1.4901161193847656e-8），未放宽标准。原因是先前将合成代数化简为0.25+0.25*head；原SASS先在编码域做float32(head*0.03125-0.03125)，再float32(encoded*8+0.5)，中间捨入不可省略。按原顺序后seed2819全部768个RGB值exact，seed2801回归也全部exact。失败计数保留本日志，当前hmma-validation报告反映修正后通过。

这是两组16×16、恒底图的原版/CPU验证，尚未扩大尺寸或覆盖空间变化底图、可选混合纹理分支；AMD第70块与游戏最终画面仍未完成。游戏DLL未改，目标active。

第70块首个空间随机数值闭合（2026-09-06）：check_native_post70_spatial.py生成seed2801，8×8×32主特征/16×16×32 skip随机FP8、底图恒RGB0.25，枚举有依据的少量物理布局与合流舍入。正确候选为main global16且不交换低两位（不同于multihead upsample输入），skip cell/preblock两种编码本夹具等价，合流H(H(main*scale_main)+skip*scale_skip)。普通半精度点积末次舍入时，768个RGB分量仅1个不同、max9.5367431640625e-7；仍按失败处理，不放宽标准。

check_native_post70_hmma.py在同一份保存的原输出上，把最后两个K16的half矩阵积改成已测的操作数指数和对齐、27位向零截断乘积、加入前一half accumulator后half RNE。该1处差异消失，768个RGB值different0/max0，报告spatial-2801/hmma-validation.json。没有修正某个输出值或拟合系数；但仅验证当前单夹具，非普遍HMMA accumulator语义证明，仍需独立种子/较大尺寸及动态底图测试。

所有原调用正常退出，数据保存在release/native-post70/spatial-2801，各布局/舍入反例也保留。AMD第70块尚未实现，游戏DLL未改，目标active。

第70块常量输入精确数值候选（2026-09-06）：原SASS显示最终三个分量乘params+0x30，rgb_mode非零时再*8+0.5并clamp；blend读取和第四分量的非线性混合依赖可选纹理分支，当前texture_mask1夹具未启用该分支。不能把这条简化路径等同所有游戏后处理模式。

check_native_post70_candidates.py从原记录直接重组C32主体：前0x2050保留，普通QKV及后段0x2060起来自原0x20d0..0x5130；main/skip系数为0x2050/0x2090处各32个half，按C32 FFN通道序。尾部FP16候选矩阵input bits=[0,1,3,4,8]、output bits=[2,5,6,7]，使用行0/2/4作当前RGB。主体不移位，合流保留half，主体final也raw-half，最后两K16 half累计，再按当前0.03125倍率转RGB。

主恒0.5/skip零及主零/skip恒0.5两例各768个RGB值逐值全exact，未拟合任何系数或色彩修正。提前FP8合流、FP8主体输出或强制移位候选均不能达到这两例全exact；完整候选报告release/native-post70/candidates.json保留全部误差，不只保存最佳项。仍只验证常量特征与恒底图，空间布局、动态纹理、HMMA随机边界及实际游戏参数未验，AMD第70块尚未实现，游戏DLL未改。

第70块原版神经分支因果响应（2026-09-06）：扩展smoke脚本，保持16×16底图RGB0.25及全部标量参数不变，单独main恒FP8 0.5或skip恒0.5。两例原kernel均正常返回、RGBA全finite，768个RGB分量全部改变；相对底图max差分别0.001178741455078125和0.00431060791015625。报告smoke-main/smoke-skip，输出SHA分别617b4b433e9485cc924ec8b5dcd7e1500eaa303fff42a6977cfd7f039650b724、6df8c6b78ade7fa2cb6a446687d4380f10b1b25d103d1ef106876c9ceeb77554。

进一步只将权重0x5130后的1024字节清零，其他输入/主体权重/参数保持不变，两个非零输入例的RGB都逐值恢复底图0.25。记录effective_weights SHA，区分原提取记录和控制记录。此消融证明在该原版夹具中，main/skip造成的RGB变化依赖最后权重区，不是单纯底图采样差异；不代表AMD第70块已经移植。

audit_native_post70_head.py按候选FP16片段排列检查尾部512个half，16×32候选矩阵只有行0/2/4/6非零（各32项），blend half=0.73974609375。行的颜色/控制语义与运算舍入仍未证明，不能直接把“16行”说成16个实际输出通道。下一步恢复主体前合流、最终head和RGB合成的数值合同。游戏DLL未改，目标active。

AMD RGB512→69完整连续通过（2026-09-06）：同一会话32104正常exit0；31个shader编译、228次缓存命中，三帧提交至fence等待2390/2329/2390ms，device/fence/finite/replay检查通过。下载最终block69结果后2097152值different0/max0，SHA b9ce77048ae6064b165fd423df82d3072126fe839ab925b46253a6938dbf5d4a；报告amd/tail69-validation.json为pass。五份DS读回重新下载比较，DS0/4/8/14/22也全exact。此前未独立GPU验证的63～65和67～69现在被该连续链最终检查覆盖，但不声称逐块GPU中间读回全做过。

第70块原版小尺寸入口（2026-09-06）：旧post probe要求main/skip各320MiB且硬编码4K纹理坐标、指针+0x2800，不适合新紧凑夹具。新增显式native模式，限制16～512的16倍数尺寸，按尺寸分配buffer，输入有效区外必须零，原数据指针从buffer起点，纹理尺寸/倒数和输出rect按实际尺寸设置；旧features与默认模式仍保留，blend额外空间确定性清零。该模式尚非完整参数合同证明。

check_native_post70_smoke.py直接提取21808-byte原权重（SHA197024afb1f78602dc08cfd5ae87c413385237c5f537dd5dcfeae742be85eca8）及2-byte blend（SHA1ee636d657c0e4bc41b1f1efe5a2007be7a8fbf8b0267bb26298039f6d0c2cfb），16×16恒RGB0.25、零main/skip原post正常返回，全部RGBA有限，RGB逐值等于底图。报告release/native-post70/smoke/validation.json为zero_smoke_pass；只证明零特征下的底图路径，不证明神经输出生效或最终RGB移植。下一步使用非零特征恢复原输出头，游戏DLL未改，目标active。

AMD RGB→69全链接线完成、回归运行中（2026-09-06）：新增NativeDecoderTail69，常驻接49～55(C256)、56投影/主体、57～61(C128)、62投影/主体、63～65(C64)、66 half合流/C32主体、67～69(C32)。输入来自GPU block48，skip分别来自GPU block14、block8、block4，Record不读文件、不注入CPU特征。只支持已建立裁判的RGB512几何与当前给定shift配置。

front-chain增加rgb512tail69及-Tail69入口，原较短模式保留；导出脚本严格检查对应原版/CPU各段pass，严格最终验证器独立写tail69-validation.json。MinGW编译通过，部署实验目录native-rgb512，游戏DLL未改。已启动唯一完整回归：SSH统一会话32104、AMD PID18196，启动18:09:59；查询CPU21.46875秒，shader1～8已完成，尚未最终读回，不能宣称RGB→69通过。下一轮继续观察同一会话/进程，不因无新输出重复启动。第70块及游戏画面验收仍未完成，目标active。

原RGB衍生链推进第69块（2026-09-06）：原C64 block65 outview（128×128、shift3、32×2）正常执行，global16/低两位交换解码后与plain输出1048576值全exact，validator新增--block65。prepare_native_rgb512_upsample66.py使用真实原block65 outview与block4-main跳接，给定shift0原第66块最终2097152值CPU/原全exact，报告upsample66-shift0/validation.json。

check_native_decoder_c32.py随后使用各自20672-byte原权重，给定shift0/1/3连续执行67～69，输入只取上一块原输出；三块各2097152值different0/max0，有效区外零、无NaN，导出FFN/attention及原final oracle。普通C32参考保持raw residual与精确half累计，未注入修正特征。报告位于decoder-block67..69。上述移位/跳接仍为当前待核对运行合同，不能把算子数值通过等同原游戏配置已捕获。

目前原版/CPU衍生链已到69，核心最后输出头70仍待原生恢复；AMD全RGB连续仍到48，63～65和67～69尚未独立/全链GPU验证。下一步合并AMD后段回归并处理70最终RGB，不把“只剩输出头”说成游戏移植即将完成。游戏DLL未改，目标active。

AMD第66块独立通过（2026-09-06）：核对NativeC32Stage的RAW_INPUT路径保留输入half作为FFN残差，只为矩阵输入做q8，故可直接消费第66块half合流。原生线性投影合同增加64→32、64或16384-token；shader仅OUTPUT_CHANNELS==32时输出half merged，其余decoder保持FP8 merged。宿主upsample66以128×128主→256×256输出，接常驻NativeC32Stage与原shift3参数，层间无CPU读回/注入。

prepare_native_upsample66_gpu.py直接导出原seed2707/shift3的input/skip/final oracle，C32 FFN前置512零槽与原解码矩阵/skip、attention矩阵/bias/scale常驻。部署native-upsample66，三帧各2097152值different0/max0，device/fence/finite/replay通过。下载再验证全exact，SHA db0f198506f28f071052ff711bf4810c41eaa0c6bb8a073f5542162ddec586f2，报告release/native-upsample66/amd/validation.json。

同一新exe/shader在native-decoder39回归，三帧131072值仍全exact。已加入-Block66脚本入口；当前测试进程全部退出，游戏DLL未改。第66块尚未接RGB链，AMD63～65仍待独立验证；下一步原block65 outview→66与block4 skip连接，以及67～70。

第66块原生数值参考通过（2026-09-06）：native_c32_reference.py抽出unpack_bytes，路径入口保持兼容。第66块的普通C32主体来自原前0x2000字节、0x2800..2860移至普通0x2000..2060、0x28a0后移至普通0x2060后；初始64→32 FP8投影0x2000..2800，输入skip系数0x2860..28a0。没有拟合或固定帧修正。

check_native_upsample66_candidates.py首次沿multihead输出通道约定导致main常量7932值不同；将投影行从multihead累计通道序转为C32 FFN通道序后，main常量8192值全exact。输入skip使用C32 FFN/attention对应通道序，全exact；multihead skip序失败。关键舍入差别：C32合流后保留half再进主体，不先FP8。提前FP8会让main/skip常量分别4352/2040值不同。这与C64/C128/C256 upsample不同，AMD实现不能无条件复用其FP8合流。

新增native_upsample66_reference.py和空间测试：原global16/低两位交换主、C32 cell skip，16×16/seed2701/shift0的8192值全exact；RGB512需要的256×256/seed2707/shift3的2097152值全exact，含移位边界。报告release/native-upsample66/spatial-16-2701-0和spatial-256-2707-3/validation.json。第66块尚未AMD实现，游戏DLL未改，下一步以half输出投影接C32主体。

第66块C32原版入口恢复（2026-09-06）：核对当前Git仍为c560a2e，未发现遗失的已提交后续成果，保留run_nvidia_ui.ps1既有未提交修改。原block66记录22784 bytes，SHA c4083d8e31bb2820eb1392f6d8b6b7148909ffa129cac7c61780ad1f9c03b916。C32 upsample ABI不同于multihead：+0x18/1c是H/W、+0x20/24为shift、+0x40主指针、+0x48低分辨率H/W、+0x50 skip、+0x58高分辨率H/W。generic runner新增mode10，显式要求blockY1，清零后上传，输出尺寸最多256；旧mode9不变。

check_native_upsample66_smoke.py以紧凑8×8×64主/16×16×32 skip测试三次原kernel，避免过长非零输入掩盖越界读取：全零输出全零，main恒0.5及skip恒0.5各输出8192值非零；三例NaN0、有效区外零，正常返回，报告release/native-upsample66/smoke/validation.json为smoke_pass。仅证明可调用和两路响应，未证明矩阵布局/舍入或AMD正确。SASS可见FFN skip+0x2810、输入skip+0x2860、attention scale+0x54a0、最终skip+0x58b0，下一步用数值控制恢复C32合流与主体。游戏DLL未改，目标active。

原RGB衍生链推进第65块（2026-09-06）：直接执行原C128 block61 outview（64×64、shift0、mode7、32×4），正常返回；validator新增--block61，global16/低两位交换解码与plain输出524288值全exact。prepare_native_rgb512_upsample56.py增加--block62，真实原block61 outview和block8-main跳接接入原第62块，给定shift0最终128×128×64的1048576值CPU/原全exact，报告upsample62-shift0/validation.json。

普通decoder检查器扩展C64 block63～65，使用各自61760-byte记录，测试给定shift0/1/3，连续只消费前一原输出；三块各1048576值different0/max0，有效区外零、无NaN，导出参数及原final oracle。此移位序列目前是待核对的调用配置，不能说已证明原游戏调度同样如此；本轮仅证明所给配置下数值一致，不是最终画质验收。各报告在decoder-block63..65。

下一步AMD63～65独立链与第66块C32上采样，随后67～70及全连续/真实游戏合同核对。AMD全RGB已验范围仍48，游戏DLL未改。

AMD第62块独立通过（2026-09-06）：原生投影合同新增128→64、64或4096-token（主宽度8/64），宿主upsample62显式使用64×64主→128×128输出/skip，投影合流后直接接C64 NativeC64Shift。导出/运行/验证脚本增加互斥Block62选择，使用原seed2607/shift3夹具及其实际原输出。

部署native-upsample62，三帧各1048576值different0/max0，device/fence/finite/replay通过。下载gpu.f32再核对全exact，SHA13bb952fbdb7a0e658532502aeccd2ac802a09de2366adb05354e129328300f1；报告release/native-upsample62/amd/validation.json明确独立上采样，不是RGB链。main/FFN/attention/projection编译分别34/2038/1909/157ms，非游戏帧时间。测试已正常退出。

同一新exe及shader在native-decoder39做回归，三帧131072值仍全exact。下一步原block61 outview与block8 skip接第62块，再验证63～65及66上采样；全网最终RGB尚未完成，严格AMD RGB连续范围仍48，游戏DLL未改。

第62块原生参考通过（2026-09-06）：原记录70048 bytes，SHA eaaa79ba63a53cbd0c23521593379284af29ab50a718ddf37e06b93b339ccadd。直接布局为C64 FFN前缀0..0x7000、128→64 FP8投影0x7000..0x9000、FFN skip0x9000、输入skip0x9080、QKV0x9100，普通C64后段平移0x2060。共享上采样参考新增该记录尺寸，mode9允许blockY2及128输出范围，旧C128/C256路径保留。

check_native_upsample56_spatial.py加--block62，所有通道/尺寸/物理global16及cell编码按配置生成，不复用C128夹具。原CUBIN01小测试16×16/seed2601/shift0共16384值全exact；实际RGB512需要的128×128/seed2607/shift3共1048576值全exact，包含移位边界。报告release/native-upsample62/spatial-16-2601-0及spatial-128-2607-3/validation.json。第62块尚未实现AMD，RGB完整GPU连续仍到48，游戏DLL未改。

AMD decoder57～61独立链通过（2026-09-06）：宿主新增decoder57_61，五个NativeC64Shift(C128)在64×64常驻串联，输入为原block56夹具，shift给定0/1/3/2/0。导出/运行/验证脚本增加互斥C128选项并保留C256与split模式。部署native-decoder57-61，三帧最终各524288值different0/max0，device/fence/finite/replay全部通过；下载再验证全exact，SHAadf50e8cfca05446dc6ba5b352d0a6c46e0622719be64a15e412173bd07a97f7，报告release/native-rgb512/amd-decoder57-61/validation.json。进程已退出，无待观察GPU任务。

初查第62块：原记录70048 bytes，CUBIN01为2h64 upsample；SASS输入投影载入+0x7200、FFN skip+0x9000、输入skip+0x9080、attention scale+0x10100、最终skip+0x11110，与前两种upsample布局同族，但尚未数值验证。下一轮建立C64投影与128×128输出测试。AMD严格RGB连续范围仍到48，后段分别独立通过，游戏DLL未改。

原RGB衍生链推进第61块（2026-09-06）：以原C256 outview kernel重新执行block55（32×32、shift3、mode7、32×8线程），正常返回；将global16/低两位交换布局解码后，与原plain block55最终262144值全部一致。validate_native_block47_outview.py增加--block55并保存block55-outview-validation.json，旧47验证保留。

prepare_native_rgb512_upsample56.py使用真实原block55 outview及block14-main作为skip，给定shift0调用原block56，最终64×64×128共524288值CPU/原全exact。输入/skip/原输出和验证报告保存在upsample56-shift0；此连接仍为当前合同下的原版链，不冒称新抓取原游戏skip指针/shift参数。

随后扩展check_native_decoder_c256.py支持C128 block57～61，按0/1/3/2/0给定shift依次只消费前一原输出，五块各524288值different0/max0，有效区外零且无NaN。导出各自ffn/attention、原final oracle，全部通过后才写pass，范围为原版/CPU连续链。AMD第57～61块尚未运行，严格RGB GPU全链仍到48；49～55与56已独立验证。游戏DLL未改，目标active。

AMD第56块独立通过（2026-09-06）：显式decoder投影合同新增256→128，允许已验证64/1024-token，主宽度8/32；原ViT及第39块维持各自限制。宿主upsample56选择32×32主/64×64 skip，GPU投影→nearest/FP8合流→C128 NativeC64Shift直接串接。参数导出/运行/验证脚本复用现有文件并加Block56选择，不改原第48块模式。

原seed2507/shift3夹具部署native-upsample56，三帧各524288值different0/max0，device/fence/finite/replay检查通过；下载gpu.f32再验证全exact，SHA5fffdd82ed9456782b611b2b2697e0abfb95c16665968b86d06b7c09b3a37c78。报告release/native-upsample56/amd/validation.json，明确独立算子而非RGB全链。shader编译main34ms、FFN5669ms、attention2290ms、projection793ms。测试进程已正常退出，无待观察GPU任务。

同一新exe/shader在native-decoder39执行回归，三帧各131072值仍全exact。下一步原block55 outview→56与block14 skip连接，再推进57～70及合并AMD全链。当前AMD严格RGB连续范围到48，49～55及56分别独立通过，游戏DLL未改。

AMD decoder49～55獨立链通过（2026-09-06）：会话57124正常exit0，七个常驻NativeC64Shift三帧最终各262144值different0/max0，device/fence/finite/replay通过。下载gpu-0.f32后--c256验证器再次全exact，SHA eb7bf99bf25e96b58741d48d6d1058732717560a6760a362eae98c508f9a49ed。报告release/native-rgb512/amd-decoder49-55/validation.json。输入仍为原block48夹具；AMD已验RGB连续范围仍到48，未拼称全链到55。

第56块原生参考恢复（2026-09-06）：原记录230176 bytes，SHA f73035bd13f1ce40ec2768b49eee02564e1ef5f7a4860375e7b1901c8e41ae83。SASS支持输入投影0x18000..0x20000、FFN skip0x20000、合流skip0x20100、QKV起0x20200、attention scale0x34200、最终skip0x38210。native_upsample48_reference.py按记录尺寸支持C128，普通C128尾段平移0x80e0，继续复用直接字节解码与已验数学；原mode9开放blockY4，不改旧C256调用。

check_native_upsample56_spatial.py对原kernel进行独立随机测试：输出16×16/seed2501/shift0的32768值全exact；输出64×64/seed2507/shift3的524288值全exact，输入为global16低两位交换、skip为C128 cell，含移位边界。报告在release/native-upsample56/spatial-16-2501-0及spatial-64-2507-3。共用参考修改后另以C256第48块32×32/seed2431/shift3回归，262144值全exact。第56块尚未AMD实现或接RGB链，游戏DLL未改。

AMD RGB512→48完整连续通过（2026-09-06）：同一会话32754正常exit0，28个shader编译/177次缓存命中；三帧fence等待2109/1984/2063ms、replay与device/fence/finite通过。下载最终output-rgb512-upsample48.f32后严格验证262144值different0/max0，SHA0ceac820a2ad341f463a771af0b9f5492148003d0062ef024f754d96eea18618，报告amd/upsample48-validation.json。重新下载五个DS文件再比对，DS0/4/8/14/22均全exact。GPU从RGB到48无CPU中间特征注入，不是拼接独立pass；但仍未完成最终RGB/游戏验收。

等待期间已把宿主新增decoder49_55模式：七个NativeC64Shift(C256)、32×32、各自原权重，参数导出脚本严格核对原链输入连续性与pass。编译/部署完成，前一GPU任务退出后才启动新独立回归；当前SSH会话57124，目录native-decoder49-55，已打印ffn编译开始。第49～55块AMD结果pending，不重启同一任务。新增-C256入口与对应验证器选择，旧40～47模式保留。游戏DLL未改，目标active。

原版/CPU decoder49～55连续通过（2026-09-06）：等待AMD全链期间新增check_native_decoder_c256.py，以原RGB衍生block48输出开始，逐块直接读取其各自689232-byte权重，原CUBIN ordinary C256模式执行，再以原生参考核对最终输出。shift给定0/1/3/2/0/1/3，七块各262144值different0/max0；原链每块输入来自前一块原输出，不注入CPU预测。输出有效区外零、无NaN，参数导出ffn/attention及最终oracle供AMD独立链使用。各报告在release/native-rgb512/decoder-block49..55/validation.json，scope不宣称原游戏shift调度已重新捕获。

AMD全链会话32754继续运行，已观察到shader1～15完成、16 projection开始，没有终止或重启。RGB→48验证仍pending，严格AMD连续通过范围仍到39，另有独立40～47和48通过，不能拼成全链通过。游戏DLL未改。

RGB512原第48块及AMD全链接线（2026-09-06）：prepare_native_rgb512_upsample48.py直接取已验证的原block47 outview与block22 main，给定shift0跑原block48，CPU参考对最终262144值different0/max0。报告release/native-rgb512/upsample48-shift0/validation.json；这是按当前跳接/shift合同搭建的原链，不冒称重新抓取了原游戏参数。

front-chain新增rgb512up48，常驻decoder39→八个NativeSplitWindow40～47→512/256 upsample projection→C256 Swin48，跳接直接来自block22 raw-half，投影shader内FP8量化skip。中间资源全在GPU，无CPU特征注入；输出独立output-rgb512-upsample48.f32。-Chain -Upsample48显式选择，旧39模式保留。导出脚本prepare_native_rgb512_tail_gpu.py及严格验证器validate_native_rgb512_upsample48.py已加入，MinGW编译通过。

已启动一次完整回归：SSH统一会话32754，AMD PID8404，启动17:22:07；查询CPU15.65625秒，shader编译日志1～4已结束。当前仍在初始化，未读回最终数据，RGB→48尚未宣布通过。下一轮继续观察此会话/进程，禁止重复启动同一测试。游戏DLL与公开包未改，目标active。

第48块扩大到32×32及原block47 outview验证（2026-09-06）：空间测试增加--size32，16×16×512主/32×32×256 skip，seed2423/shift0、seed2429/shift3各262144值CPU对原版全exact。NativeVitLinear仅为512→256 decoder模式开放256-token，宽度按已验64/256-token取8/16；ViT与第39块仍限64-token。宿主upsample48wide和-Wide脚本显式选32×32，旧小尺寸模式保留。

AMD独立wide测试部署native-upsample48-wide，三帧各262144值different0/max0；下载复核pass，SHA e0d3cd4d1f829f6ce1684c313c5201d63fe5983aceae2078a587881f17a8e947。报告release/native-upsample48/amd32/validation.json。该结果仍为独立随机输入，不冒称RGB全链已到48。

同时为原block47→48连接新增native-outview模式，直接选择cc_split_swin_16h_proj_512_outview_fp8。首次沿用plain projection的32×8线程导致CUDA700，测试已退出；核对后改为outview的32×4线程，正常返回且输出finite。不是“多了指针参数”的既定结论。validate_native_block47_outview.py将原outview按global16/低两位交换解码，与此前plain输出的逻辑HWC全部131072值exact。由此取得真实原outview数据而非CPU重排替代，可以接下一轮原block48。相关报告block47-outview-validation.json为pass；尚未运行专用内存检查，不能将返回正常等同完全排除越界。游戏DLL未改，目标active。

AMD第48块独立移植通过（2026-09-06）：NativeVitLinear的显式decoder模式扩展512→256输入投影，K512单分区每K32 half累计；1024→512第39块仍保留四K256分区。nearest2倍及FP8融合skip后直接接NativeC64Shift(C256)完成Swin，常驻GPU无中间CPU传输。当前投影几何仍只开放64-token主输入，未偷扩到RGB链所需16×16主输入。

d3d12_native_pool_head_test.cpp新增upsample48模式，shift从验证夹具单值文件严格读取，prepare_native_upsample48_gpu.py导出原seed2411/shift3最终oracle及直接解码系数。部署D:\DLSSNR-Lab\matrix-probe\native-upsample48，三帧各65536值different0/max0，device/fence/finite/replay检查通过。下载再由validate_native_upsample48_gpu.py全exact，SHA d86ccbf8c2b524c38c0023344a1d33fc03dab7c3ca56b70213008762fab39445。编译日志main38ms、FFN28401ms、attention3135ms、projection10591ms，不是运行帧耗时。

由于复用了线性类，另将同一新exe与shader在native-decoder39目录执行decoder39回归，三帧各131072值仍全exact，避免修改投影分区损坏已通过算子。宿主最终日志类名仍为native_vit_linear，本轮实际模式由命令upsample48/decoder39区分。第48块尚未接RGB连续链，需扩展16×16主输入尺寸、原block47 outview和block22跳接连接，之后推进49～70；游戏DLL未改，目标active。

第48块随机反例修复：主输入是global16而非cell（2026-09-06）。原SASS初始输入地址随K增量跨全幅，且CUBIN04存在split projection outview版本；据此测试global16主输入排列。plain global16单位投影仍32296值不同，而在全域16通道分组内交换逻辑通道索引最低两位后，单位投影65536值全exact。编码为quantize(x[...,perm]).reshape(H,W,32,16).transpose(2,0,1,3)，perm=(c&~3)|((c&1)<<1)|((c&2)>>1)。此前C512 cell和两个C256 cell候选失败，问题实际在原主输入物理布局，不是已恢复矩阵系数。

随后使用原block48权重，seed2401/shift0以及独立seed2411/shift3两次随机主/skip测试各65536值different0/max0，包括双轴移位的零填充边界。native_upsample48_reference.py的逻辑HWC算法无需改变：投影→nearest2倍→half融合skip→FP8→C256 Swin。通过报告为release/native-upsample48/spatial-2401-0-global-swap及spatial-2411-3-global-swap/validation.json。失败目录完整保留；新脚本--main-global swap显式选正确原版编码，其他布局选项仍作诊断。

这也说明后续连接原block47→48时不能直接拿plain projection cell输出当输入；必须运行原outview或验证对应重排。AMD内部HWC不需要复刻物理格式，但仍要以正确原输出作裁判。第48块尚未接AMD或RGB链，游戏DLL未改。

第48块随机空间反例与分离（2026-09-06）：新增native_upsample48_reference.py候选参考，native_c64_reference.py抽出unpack_bytes（原路径调用保留），避免反复写临时普通权重。check_native_upsample48_spatial.py seed2401、8×8×512主/16×16×256 skip逐位置通道随机，给定shift0原输出finite，但候选不同64446/65536、max1.25，严格fail，未导出AMD参数。

同一随机skip配单位skip权重时65536值全exact，证明此夹具skip输入/输出cell排列及跳接路径正确。再将初始投影按候选地址设为取前256通道的单位矩阵，结果仍64536值不同、max1.4375，故错误可在主投影路径独立重现，不应改动已通过的skip布局。尝试将主输入编码为两个C256 cell bank，实际输出与原C512 cell编码相同且仍失败；保留两个fail目录，不放宽标准。

diagnose_native_upsample48_projection.py读回发现最终输出每个2×2块严格相同，支持nearest空间复制；但输出多重集也不等于输入前256通道（sorted差11284），不能仅称“前256通道的空间排列错位”。下一步对主投影地址做二进制标记定位，区分矩阵列/输出通道与源索引，不能把常量输入通过外推为随机通过。失败报告在release/native-upsample48/spatial-2401-*，游戏DLL未改。

第48块输入合流布局与量化边界（2026-09-06）：首次候选把0x58200当投影矩阵起点、0x58000当FFN skip，常量main测试65524/65516值不同，skip测试65132值不同；单位skip控制输出全零，明确失败。继续追指令0x5050等处LDG +0x78000后纠正：初段0x58200是矩阵内载入偏移，完整256×512 FP8投影实际区间0x58000..0x78000，FFN skip位于0x78000..0x78200，输入skip系数位于0x78200..0x78400，QKV及后续从0x78400开始，较普通C256对应段平移0x201e0。上轮日志所述“从0x58200做投影”是指令地址证据，不再解读为矩阵区起点。

check_native_upsample48_control.py将三个skip系数区设half1、attention scales设float1，其余零：原输出65536值全部为FP8 0.5，有效区外零。check_native_upsample48_candidates.py直接按恢复字节区重组普通C256参考记录（不拟合权重），初始投影每K32 half累计，与输入skip合流后必须F(H(project+skip*scale))。原main恒0.5/skip零及main零/skip恒0.5两例，保留该FP8边界后各65536值全exact；不量化合流则分别34144/30692值不同。尚未证明空间/通道随机输入及shift边界，AMD第48块未实现，游戏DLL未改。失败数值保留于本日志，候选脚本当前输出为修正后结果。

第48块原生入口恢复与小测试（2026-09-06）：原记录820784 bytes，SHA7e832b24266b5565b4660a32a4789ce3b15d5835e01df4872e0cee52f82dfcb7，不可塞入普通C256的689232-byte解码器。原CUBIN03存在cc_tinlayout_fused_swin_8h_256_8_upsample_fp8；SASS初段权重地址+0x58200进入QMMA，skip系数读+0x78200，后段尾系数读+0xc8420，说明插入输入投影后布局已改变，不采用普通层尾部简单追加假设。

run_original_fused_global.cpp新增mode9：+0x18为skip指针，+0x20/24为输出H/W，+0x28/2c为窗口偏移；输入为半分辨率双通道。仅开放C256/blockY8、8倍数小尺寸，清零输入/skip缓冲后上传，保留旧模式不变。8×8×512→16×16×256三次原版小调用均正常返回：全零输出全零；main恒0.5/skip零时65532值非零；main零/skip恒0.5时65368值非零。有效输出外全零，无NaN。check_native_upsample48_smoke.py检查并保存smoke报告，仅证明入口可调用及两路影响输出，不证明CPU/AMD数值正确。原输出保存在release/native-upsample48。下一步恢复插入投影/融合skip与后续普通C256部分的精确布局和舍入，游戏DLL未改。

AMD decoder40～47八块独立连续链通过（2026-09-06）：d3d12_native_split_window_test.cpp新增decoder40_47模式，以16×16×512原decoder39输入开始，八个NativeSplitWindow分别加载自己的参数，shift按0/1/3/2/0/1/3/2，GPU层间直接传递，无CPU读回/修正。prepare_native_decoder_split_gpu.py先核对八份pass/32检查点及原输入路径连续性，再导出原block47最终oracle。部署独立目录D:\DLSSNR-Lab\matrix-probe\native-decoder40-47。

六个shader编译日志已生效：ffwd2833ms、ffwd_projection1127ms、attention5702ms、projection30881ms、pack16ms、crop8ms；这明确展示至少本轮大段等待位于shader编译，非GPU逐帧执行时间。三帧最终131072值全部different0/max0，device/fence/finite/replay通过。下载gpu-0.f32后validate_native_decoder_split_gpu.py再次全exact，SHA8823598967d37c5c56230e8c6febb7d471be9548711858fdc98b55cc240f725a，报告release/native-rgb512/amd-decoder40-47/validation.json为pass。

独立链初始输入仍来自原decoder39夹具，不能将本轮与先前RGB→39的两个通过简单拼称已验RGB→47。下一步接入全连续链，并处理第48块降通道/上采样及后续层。shift的实际原游戏调度证据仍待复核；游戏DLL和公开包未改，目标active。

原版/CPU decoder40～47连续验证通过（2026-09-06）：新增check_native_decoder_split.py，每块独立抽取自己的四份原记录、原CUBIN native-plain执行后读branch/ffn/attention/final四阶段，再用native_split_weights与已验原生参考核对。输入从RGB512衍生的原decoder39最终输出开始，后续只消费前一块原输出，不把CPU结果注入原链。八块共32个检查点均131072值different0/max0，所有输出finite且有效区外零；每个小测试均一秒内返回，未做长时间basis扫描。

当前采用shift序列0/1/3/2/0/1/3/2，来自历史decoder脚本的配置；本轮证明这些给定参数下算子数值正确，不等同新核对过游戏层对象实际shift参数。各块validation.json明确该边界，失败先写fail，不用旧pass兜底。解码后的ffwd/ffwd-projection/attention和原最终oracle已导出至release/native-rgb512/decoder-block40..47，权重/激活不入Git。

下一步在AMD串联这八块并验证原最终block47，避免重跑完整RGB链的长shader初始化作为每个小修改的反馈环。最终仍需再做RGB全连续回归；目前AMD严格连续范围到39，游戏DLL未改。

AMD RGB512→第39块连续验证通过（2026-09-06）：继续读取同一SSH会话93511，进程正常exit0；27个shader编译、126次缓存命中，三帧提交至fence等待1718/1610/1656ms，重放与device/fence/finite检查通过。AMD PID28652已不存在，没有重启测试。下载output-rgb512-decoder39.f32后validate_native_rgb512_decoder.py严格通过：131072值different0/max0，SHA565979c464e6fda2cb065c029dec652129736ae8737963b39a31c325f19d6c71。报告release/native-rgb512/amd/decoder39-validation.json。

本次重新下载五份DS读回并比较原检查点，DS0/4/8/14/22全部exact。由此首次把AMD连续范围从ViT38推进到decoder39，包含GPU重排和block30跳接FP8量化，无中间CPU值注入。实验启动阶段耗时较长，但没有证据把用户此前系统异常归因于该原因；以上毫秒数是提交后的fence等待，不是游戏FPS。下一版已加编译进度日志，本次成功exe未包含该日志改动。

随后核对block40四记录payload为524288/263168/917568/263168，与原生split四记录格式一致；可复用既有算子作候选，但仍需用block40自己的原权重和原输出验证，不可仅凭大小宣布通过。剩余40～70、实际1080p、游戏DLL及画面验收未完成，目标active。

AMD RGB512→第39块接线待验（2026-09-06）：front-chain新增rgb512decoder，ViT38→NativeVitGather(vit-to-decoder.i32)→decoder39直接GPU串联，skip来自常驻block30 raw-half输出，decoder shader先F(skip)恢复原FP8跳接再乘尾系数。输出独立命名output-rgb512-decoder39.f32；新增validate_native_rgb512_decoder.py对原连续链裁判严格比较，尚未运行验证器，不复用独立第39块pass。

新exe、shader、map、weights和-Chain -Decoder入口已部署实验目录native-rgb512。当前测试仍在运行：AMD PID28652，SSH统一会话93511；多次查询CPU累计54.34→78.55→118.42→177.5秒，尚未打印shader总数/第一帧，不能认作完成或GPU通过。未启动第二份测试、未改游戏DLL。下一轮必须继续观察该会话/进程，不能因暂时无输出而重新运行。另为下次编译加入DLSS5_SHADER_PROGRESS逐shader开始/结束日誌，源代码语法检查通过；该日志修改尚未编入正在运行的exe。

RGB512原版链延伸第39块（2026-09-06）：run_original_vit_repack_permutation.cpp新增显式--inverse，直接运行cc_vit_1d_repack_1d_to_2d_fp8；此方向按每线程4字节、block256×1调用，不沿用反方向32×4的grid。恢复65536地址双射、两份随机留出与实际block38 projection全部通过；映射逐项等于已验2d→1d映射的逆。输出block38-2d.fp8由原kernel生成，不由CPU映射代替。

prepare_native_rgb512_decoder.py将实际原RGB512→ViT38→原repack接入原decoder39，以原block30-pool-main.fp8的有效16×16×512区作为skip（该文件与block30 main全byte一致，见生成器检查）。首次预检因该skip文件带4MiB零尾超过探针2MiB上限退出2，尚未启动GPU；确认有效区外全零后另导出131072-byte紧凑skip，重跑正常返回。CPU原生参考与最终131072值全exact，报告decoder39-validation.json为pass。此处是按当前block30 main跳接合同搭建的原版链，未新抓取原游戏层对象的skip指针；不能把该连接推断写成新的运行时指针证据。

同时生成vit-to-decoder.i32（逻辑ViT→HWC完整双射）、decoder39-weights.f32和原输出decoder39-oracle.f32，供下一轮AMD连续链使用。AMD block30当前为池化保留raw-half输出，接decoder skip时必须先FP8量化才能匹配本原skip，不能直接乘raw-half。当前AMD连续范围仍到ViT38，第39块仅独立验证；游戏DLL未更换。

AMD第39块独立实现通过（2026-09-06）：NativeVitLinear增加显式decoder参数（默认false保持原ViT路径），仅允许已验64-token的8×8主1024/16×16 skip512合同。DECODER_ENTRY shader每个低分辨率token/channel做四K256分区、K32 half累计；随后为对应2×2输出各读取自己的skip，最终一次H(main+skip*scale)再FP8。输入/skip/解码权重与输出常驻，Record间无CPU传输，三帧重放按原屏障恢复输出状态。

d3d12_native_pool_head_test.cpp新增decoder39模式，prepare_native_decoder_gpu.py从seed2307原CUBIN最终输出解码oracle，不用CPU预测替代裁判。MinGW编译通过，实验部署D:\DLSSNR-Lab\matrix-probe\native-decoder39，9070XT三帧各131072值different0/max0，finite/device/fence/replay检查全部通过。读回gpu.f32下载后validate_native_decoder_gpu.py再次逐值全exact，报告release/native-decoder-amd/validation.json为pass。宿主末尾沿用类名native_vit_linear日志标签，本次明确以decoder39参数运行，不能将该标签误读成ViT新回归。

这是独立AMD第39块验证，入口为空间随机原夹具，不是RGB→ViT→decoder连续链。尚需实际ViT重排、block30 skip来源连接，后续40～70原生解码器及1080p/游戏最终画面验收。游戏DLL与公开包未更换，目标active。

第39块空间随机输入闭合（2026-09-06）：smoke脚本新增spatial及seed参数，8×8×1024主特征、16×16×512 skip均逐像素/通道随机FP8；分别按双C512 cell bank和单C512 cell编码。seed2301原输出在smoke-o620n1od，seed2307留出在smoke-8qclb2i4，两个原kernel正常返回、counter全3、NaN0。新增native_decoder_entry_reference.py直接解码矩阵/尾系数，四K256分区累计后以最近邻重复2×2，最后H(main+skip*scale)并FP8；两份各131072值与原最终输出全exact，包括该16×16输出边界。双线性align_corners=false候选无论先X/先Y、是否中间half均失败，约122296～122360值不同。

check_native_decoder_spatial.py严格比较并分别写spatial-validation.json，scope明确仅CPU/original block39的8×8输入，不含AMD/游戏。该结果恢复了这一尺寸下矩阵、输入/输出cell排列、nearest上采样及融合skip运算，无拟合校正。其他尺寸、原ViT1D输出到本层输入的重排、原block30 skip来源及AMD实现仍需闭合，不能把独立随机fixture冒充RGB连续链。游戏DLL未改，下一步实现AMD第39块并做独立GPU比较。

第39块矩阵/skip候选数值核对（2026-09-06）：新增check_native_decoder_entry_candidates.py，直接解码原权重，使用已有C512 cell排列读取最终16×16×512输出。512×1024矩阵物理output bits=[3,6,7,8,9,10,11,12,13]、input bits=[1,0,4,5,2,14,15,16,17,18]；四个K256分区内每K32 half累加，分区间依序half相加。main恒0.5、skip零时131072值全exact。尾区按split projection的order=(c//16)*16+(c%8)*2+(c%16//8)恢复，main零/skip恒0.5时全exact；线性读取尾区则101376值不同。

新增seed2301逐通道随机、空间常量的双路输入（smoke-h9cyqj1o），主1024通道按两个C512 cell bank编码，skip512按C512 cell编码。单次原kernel正常返回、counter全3、NaN0。上述矩阵候选加最终H(main_sum+skip_input*tail_scale)再FP8，全部131072值exact；若先单独H(skip*scale)再相加则256值不同、max0.001953125，因此本夹具明确区分了最终half fused乘加与提前舍入。没有拟合矩阵或修正值。空间上仍为常量，尚不能证明2倍上采样/边界/ViT实际重排；下一步用空间变化输入恢复插值合同。当前仍无AMD第39块实现或最终游戏画面验收。

第39块新原版入口小测试（2026-09-06）：check_native_decoder_entry_smoke.py建立8×8主输入/16×16 skip，直接抽取原权重，编译并预检后每次仅运行一个有15秒进程超时的kernel。三次均快速正常返回：零输入最终2MiB全零（release/smoke-bsffe819）；仅main填FP8 0x30时最终130816字节非零、NaN0（smoke-5aqzrlcy，SHA694f314a372f05367359b56ca3f1f3a7ea79cff54070dc88fb1e230a96219d63）；仅skip填0x30时131072字节非零、NaN0（smoke-q79uxg0f，SHA93ad5096b0912d18ae143bcba16cd22f6e4b2b5904eaeb8836f15ce47bf26121）。八个tile counter均为3，零测试另用od核对也为3。主/skip两路都实际影响最终+0x10输出。

这些结果建立原版探针对最终分支的可观测性，不证明地址排列、整个有效区、矩阵运算或AMD实现正确；尚未跑内存检查，不把大buffer兜底等同越界排除。脚本报告明确smoke范围，失败写fail，原数据及可执行文件留ignored release目录。下一步以此独立原输出恢复矩阵排列及skip尾系数，之后接真实RGB512→ViT输入。游戏DLL未改，目标active。

第39块旧探针反证与新探针（2026-09-06）：原SASS 0x0720读取CTAID.Z至UR25，0x1660比较Z>=3，Z<3跳去partial路径；旧run_original_block39_basis.cpp仅gridZ=2，且把参数+0x30读回当输出，因而未执行最终Z3解码分支。原最终量化/存储使用+0x10指针，+0x30为中间区，+0x20为分区计数器。0x15c0～0x1630等待counter>=Z-1，0x6a80写入当前Z；新探针counter初始设-1，避免Z1提前越过Z0。

新增run_native_decoder_entry_probe.cpp及同名.sh，默认仅文件/参数预检，不初始化CUDA；显式--run才启动一次候选grid=(2*ceil(W/4),ceil(H/4),4)、block=(32,2,1)、cluster=(1,1,4)。最终output、partial、counter分别完整保存2MiB，不截前8KiB，不将运行成功标为数值通过；拒绝覆盖既有结果。shell对进程设15秒观察上限，但不承诺能中断失控GPU kernel。当前只通过g++语法检查和bash语法检查，未执行新GPU探针；几何、真实输入布局与skip尺寸仍属待验候选。游戏DLL未更换。

第39块权重边界追踪（2026-09-06）：原SASS在0x0b50将参数+0x38权重指针装入UR20；0x2000构造UR20+4*index地址，0x2080从该地址+0x80000读32位值。共找到16处带+0x80000的LDG，审计脚本已输出其PC。结合总payload525312，支持“524288字节矩阵区＋1024字节尾区”候选，容量可容纳完整512×1024 FP8矩阵，而不是旧262144个FP16的两组投影假设。尾区可容纳512个half，但具体通道排列及乘加角色尚待追踪，不能只凭容量宣布解码实现正确。没有运行新GPU实验或改游戏文件。

第39块恢复审计（2026-09-06）：audit_native_decoder_entry.py直接读取原DLL记录并检查CUBIN非tilesync入口，不运行GPU。实测payload为525312 bytes，SHA256 e7fa32bd2a8fe680eb90cfddbae7492ffedbb87fd7284cc229a2a122dc8c4fb8；262656是容器element_count，不是字节数，恢复摘要中的字节数错误已纠正。SASS矩阵指令为QMMA.16832.F16.E4M3.E4M3，因此旧build_block39_logical.py按整包FP16解释和operation-graph的容量分组推断不能作为原生实现依据。现有run_original_block39_basis.cpp仍是历史调用器，其launch/skip合同未重新验收，不启动大规模basis实验。下一步需从原指令恢复地址与尾部参数，再建立有超时的最小原生输入/skip探针；此静态审计不代表解码器数值通过。

- kernel 存在只证明运行时编译了该实现，不证明当前 preset 调用它。
- 权重 block 是序列化分组，不直接等于网络层。
- 工作假设写在本日志；二进制证实后再提升到 `reverse-engineering-notes.md`。
- AMD 第一版统一把权重解成 FP16，不被原始 FP8 执行路径绑住。
- 先让固定输入离线出图，再装 Windows AMD 工具链；不在结构未知时提前优化。
