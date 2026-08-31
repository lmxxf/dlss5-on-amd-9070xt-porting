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

## 工作纪律

- kernel 存在只证明运行时编译了该实现，不证明当前 preset 调用它。
- 权重 block 是序列化分组，不直接等于网络层。
- 工作假设写在本日志；二进制证实后再提升到 `reverse-engineering-notes.md`。
- AMD 第一版统一把权重解成 FP16，不被原始 FP8 执行路径绑住。
- 先让固定输入离线出图，再装 Windows AMD 工具链；不在结构未知时提前优化。
