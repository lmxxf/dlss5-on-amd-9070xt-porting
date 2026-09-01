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

将该公式回填 block0 后，RX 9070 XT 生成 `stellar-block0-ffn-residual.png`。固定线性显示范围内为低对比灰色浮雕，输入人物／装备轮廓仍可辨认。原始 PPM：

```text
SHA256=3babef75fc33ee4c6bd9bfac927da4849cd559d5133f064b7a143fafe6fbd683
min=0.462745 max=0.529412 mean=0.499949 stddev=0.00237699
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

抓取完成后已退出游戏并从游戏目录移除 probe；`probe_exists=false`。RenoDX + DLSSNR 主测试环境未改动。

### 2026-09-01 最新续点

完整 live network dump 已取得，探针已移除。下一步：

1. 把 152 个 flat-weight GPU VA 归一化为相对首地址偏移；
2. 与 `weights-index.json` 的 archive record／block 边界做差分匹配，确定 GPU 权重布局是否保持文件顺序及其对齐规则；
3. 以 vtable／kernel class 分组，只对已反汇编确认过布局的 class 扩展 state 字段；
4. AMD 第一版继续坚持 FP16 解码，不复刻 NVIDIA FP8 kernel，只复刻图和张量语义。

## 工作纪律

- kernel 存在只证明运行时编译了该实现，不证明当前 preset 调用它。
- 权重 block 是序列化分组，不直接等于网络层。
- 工作假设写在本日志；二进制证实后再提升到 `reverse-engineering-notes.md`。
- AMD 第一版统一把权重解成 FP16，不被原始 FP8 执行路径绑住。
- 先让固定输入离线出图，再装 Windows AMD 工具链；不在结构未知时提前优化。
