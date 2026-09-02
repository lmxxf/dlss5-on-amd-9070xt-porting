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

## 工作纪律

- kernel 存在只证明运行时编译了该实现，不证明当前 preset 调用它。
- 权重 block 是序列化分组，不直接等于网络层。
- 工作假设写在本日志；二进制证实后再提升到 `reverse-engineering-notes.md`。
- AMD 第一版统一把权重解成 FP16，不被原始 FP8 执行路径绑住。
- 先让固定输入离线出图，再装 Windows AMD 工具链；不在结构未知时提前优化。
