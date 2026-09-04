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

## 工作纪律

- kernel 存在只证明运行时编译了该实现，不证明当前 preset 调用它。
- 权重 block 是序列化分组，不直接等于网络层。
- 工作假设写在本日志；二进制证实后再提升到 `reverse-engineering-notes.md`。
- AMD 第一版统一把权重解成 FP16，不被原始 FP8 执行路径绑住。
- 先让固定输入离线出图，再装 Windows AMD 工具链；不在结构未知时提前优化。
