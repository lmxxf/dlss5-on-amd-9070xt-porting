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

当前 pinned probe SHA-256：

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

进程在 block36 layer4 dump 中途退出。根因不是 hook 地址／签名：是 probe 把所有异构 Layer state 统一读取 0x50 字节，某些 state 对象较小，越界读取触发访问异常。

### 自动压缩后的精确续点

下一步第一件事：

1. 把 `dlssnr_runtime_probe.cpp` 中 `dump_qwords(file, state, 0x50)` 改成只读 `0x08`；
2. 重新编译、更新 `deploy_runtime_probe.ps1` 的 SHA；
3. 部署后重启 Steam／《剑星》；
4. 目标：安全走到 `block[70]`，得到全部 153 个 live layer 的 flat weight GPU VA；
5. dump 完整后立即移除 probe，恢复已知稳定的 RenoDX + DLSSNR 游戏状态；
6. 后续按具体 Layer class 逐个扩展 state 字段，禁止再统一扫未知对象长度。

当前 5090 游戏进程已因第一次 probe 越界退出；probe仍部署在游戏目录，下一轮启动前必须先编译并覆盖修正版，不能直接重启游戏。

## 工作纪律

- kernel 存在只证明运行时编译了该实现，不证明当前 preset 调用它。
- 权重 block 是序列化分组，不直接等于网络层。
- 工作假设写在本日志；二进制证实后再提升到 `reverse-engineering-notes.md`。
- AMD 第一版统一把权重解成 FP16，不被原始 FP8 执行路径绑住。
- 先让固定输入离线出图，再装 Windows AMD 工具链；不在结构未知时提前优化。
