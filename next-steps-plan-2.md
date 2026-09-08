# 0.03 之后还能切什么（2026-09-08 21:15，光之朱雀）

起点 tag `0.03`：快速链 62.7ms，游戏内约 15fps。判据不变：`compare_fast_output.py` PSNR 对 exact 链（现 42.1dB），画面 Zero 按 F6。测试台噪声 ±1～2ms，单步小于 1ms 的改动批量做完再量。

## 时间分布（release/split-direct）

| 家族 | ms | 说明 |
|---|---|---|
| preblock（C32 @1920×1152） | 10.7 | prefix 2.0 / FFN 2.9 / 注意力 2.8 / finish 1.2 / 其他 |
| encoder C32 ×4（@960×576） | 9.0 | 每块 2.2：FFN 0.5 / 注意力 0.7～1.0 / finish 0.3 / 映射读 0.05 |
| post70（C32 @1920×1152） | 8.5 | 同 preblock 结构 + merge |
| tail C32（66～69） | 5.5 | |
| 多头 Swin（C64/128/256 ×26） | 12.8 | 每块约 0.5 |
| C512 ×16 | 5.4 | 每块 0.34：ffwd 0.08 / 投影 0.06+0.03 / pack+crop 0.1 / 其他 |
| ViT ×8 | 7.2 | 每块 0.9：contract 0.21 / attention 0.36 / qkv 0.19 / expand 0.09 / 投影 0.07 |
| 其余（decoder 小段、bridge、post70 rgb） | 3.6 | |

C32 家族合计 33.7，占 54%。

## 一、C32 注意力核结构（约 8ms 在册）——21:40 后两项实验都排除了

- 占用：塞 12KB 假 LDS 时间不变（release/c32-aprobe3-*）→ 不是占用。
- 组同步：写了一窗一 wave、零组同步的版本（PASS 4 `attention_wave`，flag DLSS5_C32_WAVE_ATTENTION，链 `run_c32_wave_attention_network.ps1`，代码保留默认关），输出逐位相同，stage1 反而 4.7ms → 不是同步。
- 还没排除：K=32 小 tile 下 Q/K/V 的矩阵加载效率（aux 行距 96 字节，每行 16～32 字节散读）。验证要换存储布局（Q/K/V 各自连续 [token][32]），或者干脆量一次"只做加载不做算"的探针。
- 另：同一份代码空载跑 55ms、有游戏跑 63ms，早先的绝对数混了两种状态，只能同批次 A/B。

探针（release/c32-aprobe-*）已排除 exp、P 归一化、输出量化、bias 读；残差散读约 0.4ms@2.2M。剩下的假设：每窗 256 线程、4 次组同步、LDS 16KB（ex 8K + attn 4K + p8 4K）→ 每 WGP 只能驻 4 组。验证方法：先做占用实验——把 attn 和 p8 合并到同一块 uint LDS（p8 用完再放 attn，中间本来就有同步）看时间；如果占用是真凶，再考虑一窗 4 wave（每 wave 16 query × 64 key 全行，exp/求和/归一化在 wave 内做，组同步降到 1 次）。

## 二、C32 FFN 输入分级（约 1ms）

`prefix[i]=Ffast(input)` 每 lane 16 次软件量化；改法：让上一段（finish / 投影）直接多写一份 FP8 副本，FFN 的 A 直接从显存加载，残差从 f32 raw 读。等价于多头块做过的"残差流 FP8"。

## 三、finish 段（1.2 + 0.3×7 ≈ 3ms，内存绑定）

tile 序 → raster 的转置 + 2×2 池化，逐元素 f32 读写各 280MB。位元 F 已试过零收益（内存绑定）。改法：和投影融合，让 C32 注意力的投影直接按 raster 序写 main（映射输出，同多头 mapoutput），finish 只剩池化（读 1/4）。或输出改 f16。

## 四、多头 Swin（12.8ms，预期 −2～3）

每块 0.5ms 里 FFN 0.14、注意力 0.15、投影 0.16×2、QKV 0.06。投影仍是 f32 残差 + 逐元素 H；残差流已 FP8 但投影核里残差按元素解码。改法：投影残差用矩阵加载（A tile）而不是逐元素；输出 Cast 直存（已做）。另：expand+contract 融合核的 hidden 用 f16 LDS，可以试 FP8 LDS 减半（对 C256 16KB→8KB 提占用）。

## 五、ViT（7.2ms，预期 −2）

- attention 0.36：每 wave 40+20 步串行，1280 wave。改 BLOCK_M（一 wave 32 query）或 split-K 式按 key 分块两 wave 合并。
- contract 0.21 / qkv 0.19：已 m4；再上 FP8 QKV 权重（f16 → E4M3，流量再减半）。
- 投影 m4 变体结果不对（33dB）没查，只值 0.02ms，可放。

## 六、C512（5.4ms，预期 −1）

pack+crop 0.1/块 ×16 = 1.6：同多头做法折进 ffwd/投影（映射输入输出）。ffwd 0.08 已够小。

## 七、零碎

- pre 的 prefix 2.0ms：噪声 ALU 生成 + 16→32 混合；可把混合改成矩阵（K=16 补零到 32）。
- post70 merge 1.1、rgb 0.4：cs_5_1 老代码，没看过。
- 游戏侧：时序 feed + 采样约 1ms，回写约 5ms（每帧同步模式的 readback？值得看一眼 native_game_frame 的回写路径，5ms 是网络的 8%）。

## 到 20fps 的账

62.7 → 50ms 需要 −13：一 3.5 + 二 1 + 三 2 + 四 2.5 + 五 2 + 六 1 + 七 1 ≈ 13。都是小刀，没有一个能单独换 5ms 的了；C32 注意力结构那刀先做，它是唯一没验证过假设的大块。
