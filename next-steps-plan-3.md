# 0.04 之后（2026-09-09 08:36，光之朱雀）

起点 tag `0.04`：测试台稳定段 ≈33ms，游戏 GPU 32.0 / 每帧 CPU 35.5 / 23～25fps（不开 Splashtop）。链入口 `run_batch_submits_network.ps1`（→ inline_prefix → c32_attn_fast3 → post70_merge_fold → vit_attn_fp8 → attn_fast2 → wave_c32_ds → c32_chain_raw → …）。游戏 fast18（DLL `e5acc2e7…`，flag `native-game-flags-fast18.txt`，探针开着）。判据：PSNR 对 exact 链（现 41.98/42.01），逐位比对优先；时间用 `scratchpad/abn.sh` 交替 3 轮 + `cmpmin.py` 按帧取最小，只信稳定段；C32 encoder 段 A/A 也晃 ±1ms。

Zero 定的顺序：**1 → 6 → 3 → 8**。

## 1. C32 注意力一组两窗（估 −1.5ms，pre/post70/encoder/tail 都吃到）

现状（`native_wave_c32_split_attention.hlsl` PASS 2，`NATIVE_C32_ATTN_FAST3`）：一组 256 线程 = 一个 64 token 窗口；QKV 阶段只有 wave 0～3 干活；注意力每 wave 16 query × 32 key，softmax 分母要经 `partial_sum` 跨 wave 交换（一次组同步）。

改法 fast4（新 define `NATIVE_C32_ATTN_FAST4`，host flag `DLSS5_C32_ATTN_FAST4`，dispatch 组数减半）：
- 一组 = 两个窗口。QKV 阶段 8 个 wave 各 16 token（128 token = 2 窗）全忙，Q/K/V 写 `qkv8[2][64][96]`（12KB）。
- 注意力每 wave 管一个窗口的 16 query × 全部 64 key：4 个 score tile，exp 留寄存器，行和用滚动 f16 tile × 全 1 MMA（每 wave 私有 1KB 区，累加两次），**不再需要 partial_sum 和那次组同步**；P 硬件 Cast<F8> 进每 wave 私有 `p8`（16×64 字节 = 1KB，共 8KB）；PV 2 个 K 步 × 2 个输出 tile；输出 Cast<F8> 进每 wave 私有 512B（可与 p8 别名，P 用完再放），投影 FP8×FP8 两个输出 tile，残差按 INPUT_AT。
- LDS 预算：qkv8 12K + 行和 scratch 8K + p8 8K（别名放 attn8）+ ones 1K + inv 0.5K ≈ 29.5K。若超 32K：行和 scratch 改 512B/wave（16×16 tile 分四次累加）。
- host：`native_preblock_runtime.h` split 路径 dispatch 组数 = 窗口数/2（窗口数奇数时最后一组第二窗越界→核里 `if(window>=count)` 整 wave 跳过，barrier 仍要走）。W_BIAS/W_SCALE/权重偏移不变。
- 验证：逐位应与 fast3 相同（求和顺序可能差 1 ulp）；量 `preblock_detail_stage1`、`post70_detail_stage1`、`c32_probe_stage1`、`tail67_69`。

## 6. 游戏侧 CPU（2.8ms → 目标 1.5）

- 现状：每帧 ~25 个命令列（fast18 合批后）；`native_game_frame.h` 探针 cpu_frame − network 即开销。
- 再合批：encoder 那一大列已是一列；把 pre 块、C32 每块、post70 各自一列；ViT 8 层合成 2～4 列（注意"整网单列曾 DEVICE_HUNG"，逐步放大试）。flag 复用 `DLSS5_BATCH_SUBMITS=2`。
- 探针本身每帧多一次 Flush 等待，最终部署可关（`DLSS5_GAME_PROBE` 不写）。
- 更远：录制与 GPU 重叠（当前帧 GPU 跑时录下一帧）要改 ProcessSubmittedFrame 的同步语义，先不做。

## 3. ViT 小算子合并（估 −1ms）

- 每层 5 段：stage0 0.09 / stage1 0.155 / stage2（attention，已 FP8）0.18 / stage3 0.07 / stage4 0.06～0.08，8 层 ≈ 4.5ms。看 `native_vit_block.h` 各 stage 对应哪个核（qkv-normalize、expand、contract、attention、projection）。
- 合并候选：expand+contract（隐层留 LDS，640 token × 4096 hidden 太大→按 token 组分块，每组 16 token × 4096 f8 = 64KB 超 LDS，需按 hidden 分段流水：expand 一段→contract 累加，参考多头 `native_wave_ffn_fused.hlsl`）；qkv-normalize 合进 attention（ViT 32 头、640 token 全局注意力，每组一个 head 重算 QKV 不划算——同多头合核的教训，先量 qkv 段是否值得）。
- 更稳的一刀：ViT 权重已 FP8；检查 stage0/3/4 是否还有逐元素 epilogue（Ffast/H）可改硬件 Cast。

## 8. 雨景闪烁（画质线）

- 结论回顾（09-08 22:45）：时序反馈、回写竞争、网络不确定性、跳帧全排除；静态立绘也闪；FSR 输出静态场景也有亚像素微变，FP8 链把微差放大。
- 坐实：DLL 连续 dump 两帧输入（`native_game_frame.h` 有 `RequestTemporalDump`/`DumpTemporalNow` 现成，扩成任意帧、含 color），灌回测试台跑两遍比输出（`d3d12_native_network70_test.cpp` 已有 `frame_to_frame_different`）。
- 解法：时序路径已有 history（上一帧网络输出）+ 运动向量采样；在 `native_temporal_sample.hlsl`/网络输入混合处加置信度：|当前 − history| 小的像素多吃 history（指数平滑），大的少吃。先在测试台用两帧 dump 验证输出差异下降，再进游戏看雨景和立绘。

## 记住的坑

- root 描述符槽位有核偷读上一次的绑定：把小 buffer 绑到 t2 就 DEVICE_HUNG。新核换绑定要看后续 dispatch；宁可把小 buffer 放 t0。
- `native_c64.h` Create 里 flag 解析必须在 pso 加载之前（fp8_stream/attn_fused_qkv 已挪）。
- `native_post70.hlsl`、`native_split_window.hlsl` 运行时编译：改了要 `update-manifest.ps1`（测试目录 + 游戏 assets，一次一个名字）。
- 部署 DLL 前游戏必须关；host 打包改动都在 DLL 里，只换 cso 不换 DLL 会算错。
- 每做完一刀给 Zero 一行进度，别闷头跑一小时。
