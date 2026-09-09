# DLSSNR → AMD 实验路线

目标不是一口气做出“AMD 版 DLSS 5”，而是沿着一条每一步都有独立产物的路线前进：

```text
DLL 里的权重包
  ↓
还原权重记录格式
  ↓
还原 block → 算子执行图
  ↓
CPU / 通用框架离线出图
  ↓
AMD GPU 跑通
  ↓
针对 RDNA 优化到可交互／实时
```

前三步解决“这个模型到底是什么”，第四步证明数学复现正确，第五步才叫移植，第六步才叫能玩游戏。任何一步单独做成，已经够写一期公众号。

## 成功标准分三级

### Level 1：离线复现

给定一组固定输入，自己写的推理程序能输出一张结构正常的结果图。速度不重要，可以跑几秒甚至几分钟。

这一级证明：

- 权重格式读对了；
- 执行图大体正确；
- 算子语义没有根本性错误。

### Level 2：AMD GPU 跑通

同一模型在 AMD 显卡上完成一次推理，输出与 Level 1 在容许误差内一致。先接受 FP16、BF16 或混合精度，不强求第一版就原样 FP8。

这一级证明：模型没有被 CUDA 从数学上绑死，NVIDIA CUBIN 只是原实现，不是唯一实现。

### Level 3：游戏里可用

接入 RenoDX／ReShade 或最小 DirectX 12 测试程序，连续处理帧，画面稳定，速度达到可交互。再往后才追逐真正的毫秒级实时性能。

这一级要分别记录：

- 单帧推理时间；
- 各阶段耗时；
- 显存占用与峰值中间激活；
- 分辨率变化曲线；
- FP16、FP8 或其他量化路径的画质误差。

## Phase 0：冻结样本与基准

输入样本：

- DLL：`C:\Users\lmxxf\Downloads\nvidia\nvngx_dlssnr.dll`
- SHA-256：`e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e`
- DLL 版本：310.8.0.0
- 权重资源：`WEIGHTS_HT`，147,695,410 字节

先保存三类基准，避免以后 DLL 更新后混样本：

1. 296/297 两个分析脚本的完整输出；
2. 一组固定游戏输入与 NVIDIA 输出截图；
3. 测试时的分辨率、模型 Style、Intensity、mask 和其他运行参数。

最重要的是第 2 条。只有输入、参数、参考输出固定，后面才能判断“自己跑出来的图”到底对不对。

## Phase 1：把 `WEIGHTS_HT` 变成清单

### 已知

- 资源开头 8 字节等于整个资源长度；
- 接下来 8 字节是第一条名字长度 19；
- 第一条名字是 `block0.layer0.layer`；
- 能扫描到连续的 `block0` 到 `block70`；
- 顺序 parser 可闭合解析 153 条记录；旧正则统计的 152 条漏掉了 `block70.layer0.blend_scale`。

### 要做

还原单条记录的格式：

```text
name_length
name
若干 size / shape / dtype 字段
payload
padding / scale / metadata
```

当前最直接的办法：

1. 从两条相邻名字的文件偏移计算记录总跨度；
2. 对照名字后的几个 64 位整数与真实跨度；
3. 比较小 block、大 block、带 layer1—4 的 block，找稳定字段；
4. 已确认 payload 是 FP16；继续解释 40 字节元数据及加载到 FP8 kernel 前的转换／布局；
5. 写成 parser，导出 `weights-index.json`。

建议 JSON 结构：

```json
{
  "block": 0,
  "layer": 0,
  "name": "block0.layer0.layer",
  "record_offset": 18129264,
  "payload_offset": 0,
  "payload_size": 0,
  "dtype": "unknown",
  "shape": [],
  "extra_fields": []
}
```

第一轮不认识的字段不要猜名字，原样保存成 `unknown_u64_0`。先保证所有记录能无缝走到资源结尾，再解释语义。

### 验收

- parser 从资源开头走到结尾，不靠搜索下一个 `block` 字符串跳读；
- 所有记录长度相加与 147,695,410 字节闭合；
- 71 个 block、153 条顶层记录全部被结构化导出；
- 修改任意一个长度字段，parser 能明确报错而不是静默错位。

## Phase 2：找到真正的执行图

### 已知入口

DLL 里保留了这些 C++ 符号和日志：

- `CG2RNetworkManager::ResolvePresetToDescriptor`
- `CG2RNetworkManager::BuildActiveNetwork`
- `CCNetwork::build_blocks`
- `No network factory accepted config`
- `block type ...`
- `CC_Control_History_Blend_Quantize_With_Teacher_honest_tench_2026_07_04_22_30_weights`

这说明运行时存在：

```text
preset → network descriptor → block type 派发 → 构建执行图 → 加载对应权重
```

### 推荐顺序

1. 用 Ghidra 打开 DLL，先保留已有符号名，不急着全局反编译。
2. 从字符串 `CCNetwork::build_blocks: block type '` 找交叉引用。
3. 定位 block type 的 switch／派发表，给每个构造分支命名。
4. 反向追到 descriptor 的读取位置。
5. 从 `BuildActiveNetwork config=%s N=%d W=%d H=%d` 找 descriptor 的尺寸与 preset 选择逻辑。
6. 建立 `blockN → block type → 输入索引 → 输出索引 → weight record` 表。

不要先钻 GPU CUBIN。CPU 构图逻辑不到 0.7 MiB，而且保留大量类名和报错字符串；它更可能先交出完整网络顺序。CUBIN 留到知道“哪个 kernel 真被调用”以后再看，否则会淹死在 231 个核心和大量实现变体里。

### 验收

导出 `network-graph.json`，至少包含：

```json
{
  "block": 31,
  "type": "unknown",
  "inputs": [],
  "outputs": [],
  "width": 0,
  "heads": 0,
  "weight_records": []
}
```

然后自动生成 Mermaid／SVG 图，不再手画“看起来合理”的 U-Net。

## Phase 3：建立 NVIDIA 黑箱对照

只靠最终游戏截图很难定位哪一层写错。最好做一个最小宿主，直接调用 `nvngx_dlssnr.dll`：

1. 枚举 DLL exports；
2. 参考现有 NGX / Streamline 接入，创建最小 D3D12 feature；
3. 固定 Color、MVec、Depth、ControlMask 等输入纹理；
4. 保存 DLL 输出纹理；
5. 每次都用完全相同的输入重跑。

DLL 已暴露的输入名包括：

- `DLSSNR.Color`
- `DLSSNR.MVec`
- `DLSSNR.Depth`
- `DLSSNR.ControlMask`
- `DLSSNR.UI` / `UIAlpha`
- `DLSSNR.Backbuffer`
- `DLSSNR.BidirectionalDistortionField`

先确定“最少必须提供哪些输入”以及各纹理格式。这个最小宿主同时会成为日后 AMD 后端的测试外壳。

如果最小宿主太费时间，可以先从 RenoDX 捕获固定帧输入与输出，但要防止色调映射、UI 合成和 ReShade 其他 pass 污染对照。

## Phase 4：先在通用框架里离线出图

不要第一版就写 AMD kernel。先用最容易检查的实现恢复数学正确性：

- PyTorch；
- ONNX Runtime；
- 或简单 C++ / Python 参考实现。

策略：

1. 所有权重先解码成 FP16/FP32；
2. 每个 block 写独立单元测试；
3. 保存关键中间层输出；
4. 从最小输入尺寸开始；
5. 最后再接真实分辨率。

对照误差从粗到细：

```text
输出尺寸／范围正确
  ↓
结构与运动方向正确
  ↓
逐像素误差收敛
  ↓
量化路径与原 DLL 接近
```

第一张“像样但不完全一致”的输出就值得保存，它会告诉我们执行图大体接对了。

## Phase 5：AMD 后端第一版

### 首选目标

优先 RDNA 4（RX 9000 系）：

- 原生支持 FP8；
- 有 WMMA 矩阵指令；
- AMD 已公开 RDNA 4 的 fused GEMM 示例；
- 跟原 DLL 的 FP8 路径距离最短。

RDNA 3 可以作为第二目标。它现在能运行 AMD 自家的 ML FSR 4.1，但没有 RDNA 4 同等级的原生 FP8 路径；第一版可能要转 FP16／INT8，性能和误差都多一道变量。

### 后端选择

从易到难：

1. ONNX Runtime / DirectML：最快验证 A 卡能否出图；
2. HIP + hipBLAS／Composable Kernel：逐步替换热点；
3. HLSL Compute Shader：最方便接进 D3D12 游戏管线；
4. 手写 RDNA WMMA 与融合核心：最后冲性能。

建议不是四选一，而是逐层替换：先用 DirectML 跑全图，再 profile 找最慢的 5 个算子，换成 HIP/HLSL，自顶向下把通用实现掏空。

### 精度策略

第一版不要被“必须 FP8”绑死：

- 先把权重转 FP16，证明图能出来；
- 再还原 FP8 scale 与原始布局；
- 最后决定哪些层保留 FP16、哪些层下沉 FP8。

这能把“图错了”与“量化错了”分开，否则两个问题叠在一起很难定位。

## Phase 6：从能跑到实时

性能优化顺序：

1. profile 每个 block，先找占时最大的算子；
2. 消除中间张量的显存往返；
3. 融合 QKV、projection、FFN 与相邻量化／反量化；
4. 让 skip 特征尽量留在片上缓存或减少布局转换；
5. 针对固定通道数 32/64/128/256/512/1024 写专用 kernel；
6. 再尝试 FP8 WMMA、wave32/wave64、LDS 和寄存器布局。

模型权重包包含约 7380 万个 FP16 标量，权重容量不是主要矛盾。真正可能吃掉时间的是全分辨率／多尺度中间激活、频繁读写和融合不足。

每轮只改一个变量，并保留：

```text
commit
单帧耗时
逐层 profile
输出误差
显存峰值
测试分辨率
```

## 失败时先问哪一个前提错了

连续两次同类失败后，不继续微调参数，先按下面顺序检查：

1. 权重 record 边界是否读错；
2. block → kernel 映射是否错位；
3. tensor layout 是 NCHW、NHWC 还是私有 tiled layout；
4. FP8 是 E4M3、E5M2、带不带 block scale；
5. 输入色彩空间、motion vector 方向、depth 约定是否一致；
6. 比较的是模型原始输出，还是经过 tone mapping／UI 合成后的画面。

最危险的不是不会写 kernel，而是拿错误 tensor layout 写出一个“能跑但永远不对”的模型。

## 可以长成的公众号

1. 《140 MiB 权重包不是一坨浮点数：我找到了 71 个 block》
2. 《kernel 是工具箱，不是执行图：逆向模型最容易犯的错》
3. 《从 32 到 1024：DLSS 5 为什么是一座 U 形金字塔》
4. 《第一张图出来了：把 NVIDIA 权重搬进通用框架》
5. 《A 卡真的跑起来了：CUDA 不是数学定律》
6. 《能出图到能打游戏，中间差多少毫秒》
7. 《同一个 FP8 模型，NVIDIA CUBIN 和 AMD WMMA 差在哪》

不要提前许诺篇数。每跨过一个可复现的工程节点，再把那一步写出来。

## 回家后的第一小时

1. 运行并保存现有证据：

   ```bash
   python3 wechat/assets/296/analyze_dlssnr.py \
     /mnt/c/Users/lmxxf/Downloads/nvidia/nvngx_dlssnr.dll

   python3 wechat/assets/297/extract_model_evidence.py \
     /mnt/c/Users/lmxxf/Downloads/nvidia/nvngx_dlssnr.dll
   ```

2. 用 Ghidra 打开 DLL。
3. 搜索 `CCNetwork::build_blocks: block type '` 的交叉引用。
4. 只做一件事：画出 block type 派发表，不碰 CUBIN。
5. 把第一轮发现继续写回 `reverse-engineering-notes.md`，不要另起 v2 文件。

第一晚如果只找到“block type 一共有几种”，也已经是有效进展。
