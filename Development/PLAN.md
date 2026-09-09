# 当前计划（唯一的计划文件；旧计划在 history/；2026-09-09 23:25，光之朱雀；0.06 之后，游戏 fast33）

起点：游戏 29fps（网络 GPU 约 30.7ms + 游戏自身 6～7ms，GPU 满载）；测试台 sum-of-mins 约 33ms，PSNR 对 exact 41.91；网络显存 3.75GB。判据不变：结构改动逐位相同（`cmp`），算术改动 PSNR ≥ 41.9；同批 A/B 三轮取每段最小值（`Development/tools/abn.sh` + `cmpmin.py`）。

## 0. 先修量法（半天，做其他事之前）

- 09-09 发现：AMD 上时间戳在命令流顶端写下，**紧跟大核后面的小段的 interval 不可信**（post70_rgb 0.77ms 实为注意力尾巴，rgb 头本身 <0.05）。多头块每块 0.5ms 的分段（FFN 0.14 / 注意力 0.15 / 投影 0.16×2 / QKV 0.06）同样可疑。
- 做法：测试程序加 `DLSS5_TEST_ISOLATE=<stage label>`：只重复录制/提交那一个 dispatch 序列 200 次（同一份输入，输出丢弃），整段 GPU 时间 / 200 = 单核真实成本。对多头 C64/C128/C256 各一块、C32 的 FFN/注意力、ViT 各段各量一次，重画成本表。合核之前先看这张表。

## 1. 多头 Swin 26 块 FFN+投影合核（估 −1～1.5ms，两天）

- 现状：每块 FFN（展开+收缩，`native_wave_ffn_blocked.hlsl` 融合版）→ 投影（`native_wave_project.hlsl`，残差 f32 + 逐元素 H）→ QKV+归一化 → 注意力（fast2）→ 投影。两个投影各一个 dispatch，残差读 f32 raster。
- 改法：收缩的输出 tile 留寄存器，直接乘投影权重（K=通道数，16×32 A tile 来自收缩累加器 Cast，B 从投影权重）+ 残差矩阵化（`NATIVE_MATRIX_RESIDUAL` 那套三次对角 MMA）在同一核里出块输出。C64 LDS 够，C256 要看 hidden 4× 的 LDS 预算（C256×4=1024 hidden，每 wave 16 token ×1024 f8 = 16KB，可以）。
- 验证：逐位（收缩→投影中间是 F8 格点，能做到）；量 encoder5_8 / 9_14 / 15_22 与 tail49_55 / 57_61 / 63_65。

## 2. ViT expand+contract 合核（估 −0.3，一天，不确定）

- 640 token，每层 stage0 0.09 + stage1 0.145。隐层 4096 宽，按 hidden 分段流水（expand 一段 → contract 累加），参考多头融合 FFN。收益上限就是省掉隐层写读（2.6MB）和一个 dispatch，先用 0 的量法确认值不值。

## 3. 升采样投影三段（tail56/62/66_project 0.16/0.37/0.58，估 −0.3）

- `native_wave_decoder_entry.hlsl` 路径，没看过内部；先量（0 的方法），再看是不是同样的"标量尾巴"问题。

## 4. 显存与波动（随时可做的零碎）

- 游戏换区仍会掉几秒（显存之争：游戏 13.3GB + 网络 3.75 > 16）。已做：`Engine.ini r.Streaming.PoolSize=6000`（有效但游戏不完全认）、驻留优先级 maximum、每 60 帧 MakeResident（fast32 起，日志看 make_resident_failed）。
- 还能收：C512 16 块的中间 result[0..2] 共享（约 200MB）；shared ffn/raw f32→f16（各 296→148，需改 attention 的 C::Load 输入为 f16 A 路径）；decoder entry+split8 331MB 里 entry 的 f32 权重。
- 若仍不够：池上限 6000→5000，或让 Zero 贴图降一档。

## 5. 网络约化（写作/作品线，周级）

- 跳块表已有（`CURRENT-STATE.md` 09-09 17:00）：{42,43,46} 40.7dB 约 −1ms 已在游戏里；九块 36.5dB −2ms 待 Zero 看画面。
- 蒸馏：老师 = exact 链，数据 = DLL 每帧 dump（`FLICKER_DUMP` 机制放开成连续），难点 = 71 块的可反向传播 torch 模型 + FP8 假量化 + 算力。Zero 定位：技术上值得、实现麻烦、作为写作与作品线。

## 记住的坑（新增）

- 时间戳归属：大核后的小段不可信（见 0）。
- 显存：所有 C32 段共用 SharedRaw，"后面读前面的 raw"要私有；映射输入/merge fold 的 stage 用 16 字节替身代替 packed/merged，Heap 的 SRV 大小要 clamp 到资源宽度。
- 一行注释 `//` 会吞掉同一行后面的代码（这些头文件全是单行函数）——只用 `/* */`。
- 部署：`native_post70.hlsl`、`native_split_window.hlsl`、`native_output_smooth.hlsl`、`preblock_finish.hlsl` 运行时编译，改了要 update-manifest（一次一个名字）；host 改动要换 DLL。
- 游戏开着不跑测试台（再挤 5GB 显存）。
