# 当前计划（唯一的计划文件；旧计划在 history/；2026-09-09 23:25，光之朱雀；0.06 之后，游戏 fast33）

起点：游戏 29fps（网络 GPU 约 30.7ms + 游戏自身 6～7ms，GPU 满载）；测试台 sum-of-mins 约 33ms，PSNR 对 exact 41.91；网络显存 3.75GB。判据不变：结构改动逐位相同（`cmp`），算术改动 PSNR ≥ 41.9；同批 A/B 三轮取每段最小值（`Development/tools/abn.sh` + `cmpmin.py`）。

## 0. 先修量法（已做，09-09 23:55）

- `DLSS5_TEST_ISOLATE`：单 stage 重复 N 次取均值（用法和全表见 `CURRENT-STATE.md` 09-09 23:55）。帧内分段时间戳把大核尾巴算进后面的小段，旧的 <0.2ms 分段数字都偏大。
- 表读出来的顺序（取代下面 1–3 的原顺序）：**1a C32 链尾 finish+crop（块 4、69 各 0.54，−1.0）→ 3 升采样投影 wave 化（proj66 0.56 / proj62 0.31 / proj56 0.13，标量核，−0.7）→ 2a ViT contract 提速（0.152 vs expand 0.078 同 FLOP，−0.6）→ 1 多头 FFN+proj0 合核（上限 −0.8，不是原估 1～1.5）**。合计上限约 −3ms。

## 1a. C32 链尾 finish+crop（已做，09-10 00:30：−0.9 + −0.23，fast34）

- 块 4 和块 69 比同类 C32 块多 0.54ms，就是 `SetSkipFinish(false)` + `crop_needed` 那两步（tile 序 → raster f32）。消费者：块 4 → ds4（`PooledWork()`，已读 tile 序？）+ decoder skip4（`c32[3].Output()`）；块 69 → post 的 merge。看这三个消费者能不能直接读 tile 序/E4M3，把 crop 去掉或缩成一半。

## 1b. C32 中间量 f16（已做，09-10 01:45：−1.9ms，fast35）。多头/C512 查过：已是 E4M3 流或 token 太少，没油水

## 1c. finish + rgb 头并进注意力收尾、post merge fold 的整数除法（已做，09-10 03:25：约 −1.5ms，fast36）

## 1d. C32 家族 FFN 并进注意力核（已做，09-10 09:05：隔离 −1.2、整帧 −1.9，fast37，逐位对 precise 参考链）

## 已知问题（09-10 09:55，Zero 报告，暂不处理）：fast37 刚进游戏颜色骤变几秒后稳定；偶发整帧变黑。都是瞬态，先记着。

## 现状（09-10 09:05）：测试台 sum-of-isolates 约 23.6ms，整帧 min-of-means 35.4；游戏 fast37 待 Zero 看帧率（估 33～34）。0.07 包 = fast36 已发。

## 下一批候选（09-10 07:45 重排，按收益/代价）

1. ~~C32 家族 FFN 并进注意力核~~（已做 1d：实得隔离 −1.2 / 整帧 −1.9）。
2. **多头 26 块 FFN+proj0 合核**：量后上限 −0.8，加上少 26 次 drain 约 −0.5，合计 −1.3。两天。
3. post 的 FFN 仍 1.2 vs pre 0.63：merge fold 里 e4m3 解码 ×2 + 4 次 f16 往返，改 4 字节一次 Load 出 4 通道。估 −0.3。
4. dispatch 之间的空转仍约 3.5ms，只能靠 1、2 这类合核压。
5. 显存波动（换区掉帧）：PoolSize 6000→5000 或贴图降档；C512 result 共享 200MB；还能省的显存见旧第 4 项。
6. 网络约化/蒸馏：写作线，见旧第 5 项。九块跳过版（36.5dB，−2ms）等 Zero 看画面。

上限估计：1+2+3 全做约 −3ms → 网络 22ms → 游戏 35～36fps。再往上要动网络本身（约化/蒸馏）。

- dispatch 之间的空转仍约 3.5ms：只能靠合核减 dispatch 数——多头 FFN+proj0 合核（下面第 1 项）顺带每块少一次 drain。
- post 的 FFN 仍 1.2 vs pre 0.63（同分辨率）：merge fold 的 gather 里还有 e4m3 解码 ×2 + 4 次 f16 往返，可试把 skip/low 的解码改成 4 字节一次 Load 出 4 个通道（lane 按 4 通道分工）。估 −0.3。
- pre 的 FFN（mode 5 inline prefix）0.63、注意力 0.73：注意力 fast4 在全分辨率上占 pre 一半；没细看。

## 1. 多头 Swin 26 块 FFN+投影合核（量后上限 −0.8ms，两天）

- 现状：每块 FFN（展开+收缩，`native_wave_ffn_blocked.hlsl` 融合版）→ 投影（`native_wave_project.hlsl`，残差 f32 + 逐元素 H）→ QKV+归一化 → 注意力（fast2）→ 投影。两个投影各一个 dispatch，残差读 f32 raster。
- 改法：收缩的输出 tile 留寄存器，直接乘投影权重（K=通道数，16×32 A tile 来自收缩累加器 Cast，B 从投影权重）+ 残差矩阵化（`NATIVE_MATRIX_RESIDUAL` 那套三次对角 MMA）在同一核里出块输出。C64 LDS 够，C256 要看 hidden 4× 的 LDS 预算（C256×4=1024 hidden，每 wave 16 token ×1024 f8 = 16KB，可以）。
- 验证：逐位（收缩→投影中间是 F8 格点，能做到）；量 encoder5_8 / 9_14 / 15_22 与 tail49_55 / 57_61 / 63_65。

## 2. ViT contract 提速（−0.6）；expand+contract 合核（−0.3）其次

- 640 token，每层 stage0 0.09 + stage1 0.145。隐层 4096 宽，按 hidden 分段流水（expand 一段 → contract 累加），参考多头融合 FFN。收益上限就是省掉隐层写读（2.6MB）和一个 dispatch，先用 0 的量法确认值不值。

## 3. 升采样投影三段 wave 化（隔离量 0.13/0.31/0.56，共 1.0，估 −0.7）

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
