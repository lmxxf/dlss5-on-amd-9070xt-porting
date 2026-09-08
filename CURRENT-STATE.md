# 2026-09-07 收工现场：正确画面慢速展示

## 2026-09-08 05:15 光之朱雀（Hikari no Suzaku）接手性能优化（闇 GPT 额度见底）

**17:41 快速版 + 时序，用户游戏内确认效果与 exact 版一致、约 8fps（光）。当前安装：DLL `72b4035c…777d`，assets = fast-fp8-proj 的 shader，flag 文件 native-game-flags-fast.txt（多 DLSS5_FP8_OPERANDS/QKV/HIDDEN/LDS 四条宿主 flag）。** 踩坑：首次叠加时 DLL 只放行 DLSS5_TEST_ 前缀，四条宿主 flag 没进去 → shader 按 FP8 读、宿主按 f16 打包 → FFN 输出近零、网络退化成恒等，症状是"效果很小"而不是花屏，186ms 那个时间也对不上。切换命令：`deploy_fast.ps1 -Source <目录> -Flags <flag文件>`，exact 对 native-network70-shared-scratch + native-game-flags-exact.txt，fast 对 fast-fp8-proj + native-game-flags-fast.txt。

**17:32 时序接通，用户游戏内确认 F6 切换效果明显（光）。** 快速版停在 111.9ms 那个 commit 不再推进（用户 17:07 决定）；游戏 assets 已换回 exact shader。DLL `ce229a2f…7aa1`：从 ffxDispatch 读运动向量纹理（+120，1284×724 RG16F，UV 单位，motionVectorScale=renderSize=1281×721）与 reset（+408），native_temporal_feed.hlsl 把运动向量换算成 1080p 像素单位、把上一帧网络输出转成 float4 history，再走闇验证过的 native_temporal_coordinates/sample（变换 {0,0,1281,721,1/1920,1/1080}，与 09-05 抓的 NGX 参数 A0/A4=1/1920,1/1080 同构），网络 temporal 输入 = 采样结果；首帧和 reset 帧无 history。门控文件 `D:\DLSSNR-Lab\temporal-history.txt`。踩坑：坐标/采样类用指针相等判设备，在 ReShade 下失败（initialization_failed: motion buffer device mismatch），已改 NativeSameDevice。未做：运动向量方向/单位只按两家文档约定推断，用户观感正常但没有数值裁判；jitter 未用；history 是我们自己的上一帧输出，与 NGX 的独立 history 纹理语义假定一致。

**快速版阶段 2/3（光，钟点以用户下一条消息为准）：暖轮 111.9ms（约 8.9fps），对 exact 链 PSNR 42.0dB、max 0.057、≥4/255 像素 5.2%。入口 `run_fast_fp8_qkv_network.ps1`，证据 release/fast-fp8-proj/fast-validation.json。游戏 assets 已同步该版 shader。** 链条：fast-accumulate 159.8 → fp8-ffn 154.8（Swin FFN E4M3 操作数，误差与阶段 1 逐位相同——乘积精确、累加器同一个）→ fp8-vit 151.6（ViT expand/reduce E4M3；**LDS 打包 8 位矩阵 Load 的 stride/StartIdx 单位是打包后的 uint，不是元素**，写成 32 会得到 34.8dB 的"半坏"结果）→ fast-epilogue 130.1（FFN 激活多项式去掉 3 次中间 H()、单次 RNE 量化：C32 FFN 1.47→0.56、preblock FFN 8.6→4.2）→ fast-attention 115.3（归一化用 WaveActiveSum、exp 去 H、分母 f32 四 lane 求和、概率单次量化、残差单次 H：C32 attention 1.66→1.12、preblock attention 8.7→5.1）→ fp8-proj/qkv 111.9（投影 E4M3 中性；多头 QKV 从 thread-scope f16 matvec 换 wave E4M3 GEMM 0.23→0.13）。踩坑：分裂块投影权重漏打 FP8 包，PSNR 8.8 的垃圾就是它。所有权重矩阵部分 100% 在 FP8 格点上（偏置/scale 向量不是，仍走 f32）。

**11:09 快速版阶段 1 用户游戏内确认：效果在、无异常，约 6fps（日志 avg_ms_per_frame=164）。** tag `0.01` = exact 终点（186ms）。阶段 1（DLSS5_FAST_ACCUMULATE=1 编译期宏，入口 run_fast_accumulate_network.ps1，证据 release/fast-accumulate/fast-validation.json）：所有 wave 矩阵 GEMM 去掉每 K32 一次的软件 H()，硬件 FP32 累加跑完整 K、末尾 H 一次；测试台 186→159.8ms，对 exact 链 PSNR 42.6dB、max 0.055、≥4/255 像素 3.8%。游戏 assets 已同步快速版 shader（同名覆盖），回 exact：`deploy_fast.ps1 -Source D:\DLSSNR-Lab\native-network70-shared-scratch`。误差报告工具 compare_fast_output.py，测试程序 DLSS5_TEST_ALLOW_INEXACT=1 不再因不一致中止。

**09:00 用户进游戏确认：画面连贯，约 5fps（光）。** 首版 DLL（45fe8f…）画面一帧神经一帧默认闪烁——老诊断模式每 250ms 轮询只替换一帧；游戏内 render_begin→render_complete 稳定 187ms，与测试台一致，无换页。第二版 DLL（SHA `934b3d5e1b9515c591ebbfbed809deb722f038a1f67b94af8fcf17a658af28d6`，当前安装）加 `continuous-every-frame.txt` 每帧同步模式：每个 FSR 帧都跑网络回写，首帧后不读回不落盘，日志每 100 帧一行 `every_frame avg_ms_per_frame`。仍每帧 reset history，时序累积未做。回退方法同上。

**08:32 之后（钟点以下一条用户消息的时间戳为准）游戏 DLL 已部署（光）——等用户手动进游戏验收。**

- 显存：块内 scratch 共享（DLSS5_TEST_SHARED_SCRATCH：多头块的 result/scratch 走 workspace；DLSS5_TEST_SHARED_C32_SCRATCH：所有 C32 实例共用一对 ffn/raw，按首个创建者加 shift 余量定容），测试进程本地段 14.68→7.26GB，15帧exact，暖 186ms。证据 release/native-network70-shared-scratch。
- 安装的 DLL：`native-submission-order.addon64` SHA `45fe8f29b11f8c463802884493389b737321332dfb9cc9b89804df8418d0a26a`（源码本仓库 HEAD，`bash build_native_game_verification.sh ... --tiled`）。**回退**：游戏退出后把同目录 `native-submission-order.addon64.before-fast-20260908`（SHA `b6e42d35…c903`，即 09-07 的慢速展示版）复制回去；并把 `D:\DLSSNR-Lab\native-game-tiled-assets.shaders-before-fast-20260908\` 里的 cso/hlsl 拷回 assets 目录（只有 shader 变了，权重没动）；删掉 `D:\DLSSNR-Lab\enable-game-sdk721.txt`。部署脚本 deploy_native_fast_verification.ps1（游戏运行中会拒绝执行）。
- DLL 相比 09-07 版的两处新行为：(1) ReShade `create_device` 事件里在游戏建设备前 `SetSDKVersion(721,".\DLSS5-D3D12-721\")` + `D3D12EnableExperimentalFeatures`（游戏目录里 09-05 放的 D3D12Core.dll 仍在），由 `enable-game-sdk721.txt` 门控，结果写 `logs\native-submission-order.txt` 的 `sdk721_before_device get/set/experimental`（三个都要 00000000）；(2) 初始化时读 `D:\DLSSNR-Lab\native-game-flags.txt`（56 条 DLSS5_TEST_*，与 run_shared_scratch_network.ps1 链完全一致、ASYNC_SUBMIT 未开）写进进程环境，`logs\native-game-oneshot.txt` 里 `flags_applied=56`。
- 怎么看：正常从 Steam 启动，`continuous-reset-preview.txt` 仍在则自动慢速连续处理（每帧 reset history），看 oneshot 日志的 render_begin→render_complete 间隔；09-07 版约 4 秒/帧，现在预期 0.3 秒量级（含读回/落盘开销，不是 186ms）。初始化仍要编译 shader，约两三分钟。**如果 sdk721 三个值有非零或出现 initialization_failed，先别猜，把日志尾巴贴过来。**
- 显存：网络 7.26GB + 游戏 7.2GB 贴着 16GB，若 render 时间远大于 0.5 秒且日志无错，先怀疑换页。

**08:35 累计（光）：暖轮 183ms（约 5.5fps），15帧exact。完整入口 `run_c32_ffn_raw_store_network.ps1`（叠在 fused-shift → c32-split → direct-attn 之上），证据 release/native-network70-c32-rawstore2/。** 四项进展：(1) 多头注意力 direct-attn2 203.5ms——新 normalize pass（每 token 一个 wave，lane=通道，用 WaveReadLaneAt 复现平方和树）输出窗口主序 f16 Q/K/V 到共享 workspace（首版每块独立分配把显存推过预算 850MB 导致换页，已改共享），注意力核 wave load 直取、只有 exp 表进 LDS：首C64 attention 1.72→0.32 + normalize 0.35；(2) C32 注意力 c32-split 184.6ms——闇的融合核拆成 qkv/normalize/attention/projection 四个 pass，用 raw（f16 [token][64] 恰好等于 f32 [token][32] 字节数）和 main（前半 v16、后半注意力输出）当暂存，零额外显存：C32 stage1 2.80→1.69、preblock 12.2→7.4、post70 19.8→15.5；(3) fused-shift 183.8——pack/crop 折进 body（f16 pack 直接读 raster、FFN 残差映射读、注意力投影映射写 raster），exact 但只 −1ms，散射写抵消了省下的拷贝；(4) C32 FFN：矩阵 Store 经描述符表 raw UAV 得到垃圾（release/native-network70-c32-rawstore/failed-*），改走根描述符后 exact 但中性（183.2）。C32 FFN 每 token 5ns 的成本仍无解释，需要 RGP。

**进游戏前的硬阻塞：显存。** 测试进程本地段 14.68GB / 预算 14.86GB，全是每块各自分配的中间 scratch（C64 一块约 300MB×8、C128 ×12、C256 ×16、C32 各 283MB×7、preblock/post70 全分辨率各 1.1GB+）。游戏本身要 7.2GB，叠上去必换页，FPS 没意义。需要把块内 scratch（padded/result/scratch/qkv_raw/matrix_input/qkv_norm、C32 的 ffn/raw/main/down）改成串行共享，只保留 decoder 需要的各段跳接输出常驻；估计能压到 5GB 上下。另外游戏 DLL 里没有 env，需要从配置文件读 flag 再 _wputenv，且新 cso 要一并放进 assets 目录。

**06:45 累计（光）：暖轮 225.0ms（约 4.4fps），15帧exact。完整入口 `run_wave_split_project_network.ps1`，证据 release/native-network70-split-project/。** 新增两项：wave-decoder 237.7（decoder39 入口与四段 2× 上采样投影换 wave 矩阵，native_wave_decoder_entry.hlsl，DLSS5_TEST_WAVE_DECODER_LINEAR；decoder_stage1 4.51→0.31、stage10 2.63→0.19、tail56/62/66 project 1.75/1.17/0.78→0.19/0.49/0.64）→ split-project 225.0（512 分裂块的 FFWD/注意力投影复用 native_wave_project.hlsl C512 版，DLSS5_TEST_WAVE_SPLIT_PROJECT；每段 0.47→0.11ms，encoder23_30_body 17.0→11.7）。至此网络里除注意力核和 C32 FFN 外的所有 GEMM 都在 wave 矩阵路径上。剩余大项：多头注意力 ≈61、C32 注意力 ≈45、C32 FFN ≈21、pack/crop ≈15、split 注意力 0.47×16。

**06:22 驱动回退根因已闭合（光）**：04:22 的 26.8.1 重装来自 AMD 装软件时默认建的两条 SYSTEM 级计划任务——`AMD Install Manager - Check For Updates`（每日 03:00）和 `AMD Install Manager - Install Updates`（机器空闲触发，`-InstallUpdates -Auto`）。Adrenalin 界面的自动更新开关不管它们；Security 日志 04:22 前无交互登录。用户已于 06:22 在 AMD 机上 Disable-ScheduledTask 关闭两条任务（可 Enable 恢复）。当前显卡驱动仍为预览版 32.0.31007.2048。探针脚本 amd_trigger_probe.ps1 / amd_task_probe.ps1 入库。

**06:12 收工：暖轮 242～252ms（约 4fps；run-to-run 噪声约 ±5ms，小于 5ms 的差异不要当结论）。完整入口 `run_local_c32_attention_network.ps1`（246.3ms，release/native-network70-local-c32-attn）。**

06:05 之后三个探针都在噪声内、未采纳（flag 保留、默认关）：fast-f（位元 FP8 量化，DLSS5_BUILD_FAST_F；第一版用 half-away 舍入直接 differs——HLSL round() 是 RNE，改 RNE 后 exact 但无收益，失败日志 release/native-network70-fast-f/failed-half-away.stdout.log）；static-length（acc.Length() 循环改静态 8 次展开，DLSS5_BUILD_STATIC_LENGTH，证实 Length()==8，收益 ≤ 噪声）；matrix-store（f16 隐藏层输出从 GetCoordinate 逐元素 Store 改 Cast<F16>().Store，现为默认，NATIVE_SCATTER_STORE=1 可回退，中性）。**结论：C32 FFN 每 token 5ns、多头注意力每组 65µs 的成本都不在算术，也不在带宽（pack/crop 拷贝实测 530GB/s，FFN 只到带宽下限的 1/4）；剩下的解释只有 wave 矩阵路径本身的指令开销/延迟链，没有指令级 profiler 之前别再猜。**

**给下一位（闇或光）的下一步，按预期收益排**：(1) 多头注意力（1.6ms×38≈61ms）——让 QKV GEMM 直接输出窗口主序的 f16，注意力核用 wave load 直取 Q/K/V、不再经 LDS 转存，同时把输出改矩阵 Store；(2) C32 注意力（preblock 12.7 + 7×2.85 + post70 内）——把 QKV/投影 GEMM 从每窗口融合核拆成整层 wave GEMM（像多头路径那样），融合核只留 QK/softmax/AV；(3) decoder 入口与三段上采样投影（约 10ms）仍是标量 NativeVitLinear decoder 形态，可套 blocked reduce；(4) 512 split 注意力/QKV/投影三段各 0.5ms×16。注意力以外的 GEMM 类算子已全部在 wave 矩阵路径上，再挤空间很小。

**没动的东西**：游戏 DLL、发行包、任何默认行为（所有新路径都是显式 flag）；闇的“整阶段合并提交 DEVICE_HUNG”结论未复测；AMD 驱动自动更新未关（04:22 那次回退的触发者仍未知）。

**06:05 累计：暖轮 246.3ms（约 4.1fps；闇最后基线 472.9ms，−48%），15帧全部exact。当前完整入口 `run_local_c32_attention_network.ps1`，证据 release/native-network70-local-c32-attn/。**

05:52 之后新增：fused-exp 289.5（多头注意力exp直接从QK寄存器写入f16 Q/K槽，删掉16KB f32 scores共享区，首C64 attention 2.02→1.69ms）→ vit-attn-half 271.4（ViT注意力读打包f16 QKV、wave load直取K/V、分母树两lane并行，3.33→0.78ms/层）→ wave-project 254.5（Swin FFN/注意力输出投影改wave矩阵，0.45→0.22ms×2×38块）→ local-c32-attn 246.3（C32注意力权重打包f16驻留、wave load直取，不再每窗口4次拷进LDS；preblock stage1 15.5→12.7）。

**剩余分布（246ms）**：C32族≈85（post70 body 20.1、encoder1_4 18.0、tail67_69 16.1、preblock attention 12.7 + FFN 11.8 + prefix 6.8）；多头Swin C64/128/256 编码器≈36 + 解码器≈32（每块约1.6ms注意力 + 0.8 FFN + 0.45 投影 + 0.25 pack/crop + 0.3 QKV）；split C512 ≈ 17+8×2.4；ViT 20；decoder入口/上采样投影≈10。注意力核（多头1.6×38 + C32 2.85×7 + preblock 12.7 + post70内）仍占约120ms，是唯一还没被结构性改造的大项——LDS并发和barrier链，而非算力。

**05:52 累计：暖轮 293.2ms（闇最后基线 472.9ms，−38%），15帧全部exact。** 叠加顺序（每步一个release/native-network70-*目录、一个run_*.ps1 入口，每个入口链式调用上一个）：async-submit 456 → blocked-ffn 427 → blocked-vit 359 → coalesced-qkv 366（噪声内）→ resident-weights 338 → shift-stack 332（重评闇的COALESCED_MULTIHEAD_SHIFT，现在有效）→ wave-vit-qkv 321 → blocked-vit-proj 312 → split-blocked 303 → c32-ffn-blocked 293。当前完整入口：`run_blocked_c32_ffn_network.ps1`。

**共同规律**：所有收益来自数据搬运，不是算术——(1) 每wave一份A tile复用4个权重tile（寄存器分块）；(2) 中间层用f16存（F()输出都在FP8格点，转换无损）；(3) 权重全部驻留VRAM；(4) 取消LDS→temp→LDS的转存和多余barrier，用GetCoordinate直接写；(5) 取消分chunk小dispatch。每个输出元素的K32乘加序列和H()调用次数完全不变，所以逐字节exact是构造性保证，不是靠运气。

新增文件：native_wave_ffn_blocked.hlsl（Swin C64/128/256 FFN）、native_wave_vit_blocked.hlsl（ViT expand/reduce，含K=1024投影版）、native_wave_vit_qkv.hlsl（ViT QKV，1.53→0.39ms/层）、native_wave_c32_ffn_blocked.hlsl（C32 FFN，preblock stage0 13.3→10.0ms）、native_wave_split_ffwd_parallel.hlsl 加 NATIVE_SPLIT_FFWD_BLOCKED（1.18→0.65ms/块）。

**剩余分布（293ms）**：注意力约145ms（多头C64/128/256 约2.2ms×38块≈84；C32 3.0×7+preblock 12.8；ViT 3.35×8=27；post70 body 20.6 内含C32型注意力）；C32 FFN 1.8×7+10；prefix 7.4；pack/crop；decoder入口/上采样投影约10。**下一步该动注意力核**：多头核每组(窗口×头)约65µs，是合理值的~10倍，瓶颈是LDS 28KB限制并发+12个barrier+64线程串行段，不是算力；候选是把scores存f16 exp（省8KB LDS）、分母树并行化、或把C32的QKV/投影GEMM从融合核里拆成整层wave GEMM。

**05:41 累计：暖轮 337.8ms（闇最后基线 472.9ms），15帧全部exact，四项叠加、全部显式flag、默认关闭、游戏DLL未改。**

全部权重驻留GPU本地（DLSS5_TEST_RESIDENT_WEIGHTS=1）：此前所有权重/偏置表/索引表都在UPLOAD堆（系统内存，非本地段492MB），每帧经PCIe读取。NativeMaybeResident在各Buffer helper初始化后复制到DEFAULT堆；非本地占用降到205MB，本地14.44/15.14GB。暖366.0→337.8ms。decoder_stage1在多次运行间4.5/13.9ms双稳态（与同步/延迟提交无关）的根因就是它：decoder39入口权重落在系统内存时慢9ms。证据release/native-network70-resident-weights。

多头注意力Q/K/V协同装载（-D NATIVE_COALESCED_QKV_LOAD=1，DLSS5_BUILD_COALESCED_QKV=1）：原来只有64线程各做96次跨步标量读；改为256线程按token 128字节连续读入scores/values共享区再分发，数值不变。首C64 attention 2.42→2.25ms，收益小。证据release/native-network70-coalesced-qkv；同配置同步模式对照release/native-network70-coalesced-sync（暖377.8ms，证明per-stage时间戳在同步模式下也把入口权重非本地的9ms记在decoder_stage1）。

ViT寄存器分块（DLSS5_TEST_BLOCKED_VIT=1，native_wave_vit_blocked.hlsl）：expand 1024→4096写f16隐藏层、reduce 4096→1024直接A::Load f16，每wave 4个输出tile，整阶段单dispatch（取消20段chunk）。vit31 stage0 4.1→0.44ms、stage1 4.7→0.63ms；ViT合计115→61ms。暖426.5→359.5ms。证据release/native-network70-blocked-vit。

寄存器分块FFN＋f16隐藏层15帧exact，暖426.5ms（基线456.2ms，vit-chunk2的472.9ms）。DLSS5_TEST_BLOCKED_FFN=1：native_wave_ffn_blocked.hlsl 每wave算16 token×64输出，A tile一次装载复用4份权重tile；expand把隐藏层以f16存（F()输出是FP8格点，转换无损），contract直接A::Load f16、不再经LDS转存。每个输出元素的K32乘加顺序与H()次数不变。首C64 FFN contract 1.982→0.334ms、expand 0.931→0.495ms；encoder5_8 22.9→16.5、tail63_65 22.9→16.4。证据release/native-network70-blocked-ffn/profile-validation.json，入口run_blocked_ffn_network.ps1（叠在async-submit之上）。游戏DLL未改，10fps未达到。

延迟提交（DLSS5_TEST_ASYNC_SUBMIT=1）15帧exact，暖472.9→456.2ms。native_game_submission.h 增加8槽allocator/list环，Submit不再逐条等fence，只在复用槽位时等待；Flush()供读回前调用。队列顺序不变、命令不变。首次运行在NativeResidentTable初始化时DEVICE_REMOVED——该helper在Submit后立刻释放上传缓冲，已加Flush修复（native_submitted_readback同样补上）；失败日志留release/native-network70-async-submit/failed-resident-table.*。注意：延迟模式下各stage时间戳会前移（ViT各段显得变快、decoder_stage1多出9ms是上游未完成的工作），只能信整帧total_ms。游戏DLL默认仍同步模式。

ViT展开单dispatch块16→32 tokens有明显收益：VIT_EXPAND_CHUNK2=1仅Wave expand的chunk_values65536→131072，每块仍单独Submit/fence等待，未合并整阶段command list。15帧exact，暖472.903032ms、末5帧471.599902ms，八层stage0合计75.66819→33.15094ms；旧整网540.422252ms。无新buffer或舍入变化，无DEVICE_HUNG。证据release/native-network70-vit-chunk2，入口run_vit_expand_chunk2_network.ps1；游戏未改，10fps未达到。

多头归一化并行候选未采纳：-ParallelNorm新增512B共享倒数，raw Q/K用half暂存后全组逐元素缩放，15帧exact但暖546.822146ms、末5帧545.92308ms，首C64 attention2.44709ms无改善；encoder15_22和tail49_55升到34.10/35.60ms。保持上一八Wave并行softmax约540ms，不加ParallelNorm。证据release/native-network70-multihead-norm，游戏未改，10fps未达到。

多头八Wave＋并行softmax15帧exact，暖540.422252ms、末5帧541.122424ms；首C64 attention2.43663ms（旧四Wave2.85449）。-EightWaves -ParallelSoftmax：256线程按16查询/16输出列拆矩阵，原64查询标量段和求和/H/F保持，无新增共享容量。证据release/native-network70-multihead-eight-wave；C32仍八Wave并行exp/prob/norm。游戏未改，10fps未达到。

多头四Wave并行softmax阶段15帧exact，暖546.147404ms、末5帧546.866452ms；首C64 attention2.85449ms（旧四Wave3.52036）。-ParallelSoftmax全组4096元素原地scores→exp，原64查询分母树写新增256B共享inv，再全组量化概率到死Q/K区。无新增整图buffer，H/F/求和顺序不变。证据release/native-network70-multihead-softmax；入口run_multihead_four_wave_network.ps1 -ParallelSoftmax，C32仍八Wave并行exp/prob/norm。游戏未改，10fps未达到。

C64/C128/C256四Wave注意力候选15帧exact：MULTIHEAD_FOUR_WAVES=1，128线程、标量段t<64、矩阵查询循环按四Wave分工；无共享容量增加。暖552.613228ms、末5帧552.030206ms，首C64 attention3.52036ms，较555.090092ms基线只小幅整体差异，暂不宣称稳定收益。证据release/native-network70-multihead-four-wave；独立runner run_multihead_four_wave_network.ps1，C32仍八Wave/并行exp/prob/norm。游戏未改，10fps未达到。

C32逐通道Q/K归一化并行15帧exact，暖555.090092ms、末5帧554.68984ms。-EightWaves -ParallelExp -ParallelProb -ParallelNorm，原平方和/rsqrt由64查询线程计算，新增128 float共享倒数；全组从原raw Q/K scores按元素缩放/量化，保留每次H/F和V量化。post70_body21.95771ms、preblock attention12.47767ms、首C32 attention3.01443ms；上版567.012028ms。证据release/native-network70-c32-parallel-norm；ParallelOutput仍关闭，游戏未改，10fps未达到。

C32最终残差/写回并行候选未采纳：-ParallelOutput，2048元素全组分工，原half_add_preserving_midpoint及H/F不变；15帧exact但暖568.236776ms、末5帧566.920302ms，post70_body25.59524ms较基线24.75947ms慢。保持ParallelOutput关闭，有效-EightWaves -ParallelExp -ParallelProb约567ms。证据release/native-network70-c32-parallel-output；游戏未改，10fps未达到。

C32概率量化全组并行15帧exact，暖567.012028ms、末5帧566.189546ms。-EightWaves -ParallelExp -ParallelProb：原64查询分母树输出倒数至新增256B共享区，同步后全组按原key-major布局执行F(H(exp*inv))，再同步进入AV。post70_body24.75947ms、preblock attention15.37691ms、首C32 attention3.73171ms；上版574.62942ms。证据release/native-network70-c32-parallel-prob。权重/结构/舍入不变，游戏未改，10fps未达到。

C32八Wave并行exp通过15帧exact：-EightWaves -ParallelExp，4096 score+bias/exp由全256线程按元素处理，增加一次全组同步，再执行原64查询分母树/概率步骤。无新增共享/全图buffer，暖574.629421ms、末5帧575.382012ms；恢复后八Wave基线582.260006ms。preblock attention16.28447ms、首C32 attention4.19661ms、post70_body26.13631ms。证据release/native-network70-c32-parallel-exp；启动能力SM6.10/tier0x10正常。游戏未改，10fps未达到。

重要更正/驱动恢复：八Wave最初PSO失败来自驱动回退，不是该shader不支持。当前检查发现AMD32.0.31041.1004、SM6.9/tier0，已知四Wave也PSO失败；SetupAPI显示09/08 04:22 AMD26.8.1安装器活动（触发者未知）。按既有授权恢复32.0.31007.2048，PID21368退出0，无重启，Intel不变；SM6.10/tier0x10恢复，四/八Wave均PSO成功。一次性任务完成后移除，更新设置未改。

恢复后同环境两轮各15帧exact：八WavePID21052暖582.260006ms、末5帧585.337214ms；四WavePID30596暖594.28691ms、末5帧594.138682ms。八Wavepost70_body27.96500ms vs四Wave30.52498，preblock attention18.30349 vs20.81232。八Wave在当前环境有收益，可显式-EightWaves使用；旧569ms是恢复前历史测量，不用作跨环境直接比较。证据release/native-network70-c32-eight-wave与-c32-four-wave-restored，旧失败日志pre-driver-*保留；驱动证据release/driver-install-20260908-restore。新增启动前SM6.10/LinAlg能力门禁，避免再次误判代码；游戏未改，10fps未达到。

C32八Wave候选PSO创建被拒绝：256线程按查询/输出列分工，DXC通过但PID14572初始化native preblock HRESULT2147942487(E_INVALIDARG)，exit1，无完整帧/性能数据，无DEVICE_HUNG。原因尚未定位，不宣称硬件普遍不支持256线程。-EightWaves显式开启，默认仍四Wave569ms。证据release/native-network70-c32-eight-wave，test_c32_wave_ownership.py仅验证索引覆盖；游戏未改，10fps未达到。

C32四Wave融合核15帧exact，暖569.132636ms、末5帧572.907292ms。独立preblock_attention_four_wave.hlsl每窗口128线程，四个Wave各负责16查询矩阵工作；标量段仅t<64，barrier保持全组；输入/投影暂存协同连续搬运，不增LDS/全图buffer。preblock attention20.79282ms，首C32 attention5.13972ms，post70_body30.78596ms（旧33.60917）。证据release/native-network70-c32-four-wave；runner run_c32_four_wave_network.ps1，其他无收益候选均关闭。游戏未改，10fps未达到。

C32软件H显式[branch]实验无收益：15帧exact，暖589.228183ms、末5帧589.26818ms，post70_body33.68957ms、preblock attention23.88590ms、首C32 attention5.86296ms。NATIVE_C32_BRANCH_HALF默认关闭。证据release/native-network70-c32-branch-half。量化/舍入/驻留/简单padding这组局部实验已无明显收益，下一步需要调整C32融合核任务分配或分阶段执行，并先核算scratch预算；不重试整网大提交。有效基线586ms，游戏未改，10fps未达到。

硬件half补偿实验数值恢复但更慢：NativeCorrectedHalf对RTZ相邻half中点做RNE修正，2048探针对软件bit0，C32整网15帧exact。暖616.247572ms、末5帧611.408438ms，post70_body39.04664ms，未优于586ms软件H，默认关闭。证据release/half-conversion-probe及release/native-network70-c32-corrected-half；runner run_c32_corrected_half_network.ps1。该舍入差异已形成探针→修正→整网验证闭环，不继续将直接cast当免费RNE。游戏未改，10fps未达到。

half差异已独立重现：8192边界样本intrinsic对软件6144不同；读回2048样本软件全部匹配NumPy RNE，f32tof16/f16tof32与float16_t cast均1536不同且全部匹配朝零截断。仅当前驱动/DXC实测，不宣称通用API规则。证据release/half-conversion-probe/validation.json，源码half_conversion_probe.hlsl，通用probe增加“-”跳过fixture读取以导出数据。下一步硬件转换需RNE修正或寻找可指定舍入接口，不能直接换H。有效基线不变，10fps未达到。

C32硬件half转换候选未通过exact：NATIVE_C32_HARDWARE_HALF=1用f16tof32(f32tof16(v))替代H，保持中点补偿/网络结构。PID35852首帧6627936/6635520值不同，RGB finite、MAE0.00615155、max0.05422974、RMSE0.00785048，非可忽略位差；测试exit1，未暖测，未采用。证据release/native-network70-c32-hardware-half。原因尚未独立定位，不预设硬件RTZ/驱动bug；下一步需舍入探针。默认软件H，游戏未改，10fps未达到。

Wave C32 attention位元FP8复测失败（性能）：NATIVE_FAST_C32_FP8=1专门重编完整attention CSO，15帧exact但暖608.875417ms、末5帧609.813532ms，post70_body37.79873ms（原33.60917）、preblock attention28.99365ms、首C32 attention6.93963ms。未采纳，默认旧F。证据release/native-network70-wave-c32-fast-fp8，runner run_wave_c32_fast_fp8_network.ps1。不是早期FXC标量复测，同样结论；游戏未改，10fps未达到。

C32 Wave score行padding候选15帧exact：SCORE_ROW32→33、SCORE_WIDE64→65，score容量4096→4224 float（+512B），矩阵/标量索引同步改。暖585.284452ms、末5帧586.848162ms，post70_body32.85373ms（旧33.60917），首C32 attention5.64669ms。整网差异很小，仍显式候选、默认关闭，不宣称稳定整体提速。证据release/native-network70-padded-c32-scores；runner run_padded_c32_scores_network.ps1。游戏未改，10fps未达到。

矩阵版C32权重驻留复测15帧exact但未采纳：RESIDENT_C32_WEIGHTS=1，暖587.328718ms、末5帧591.87154ms；post70_body33.63794ms对旧33.60917ms几乎无变化，preblock attention23.91003ms、首C32 attention5.74253ms。仍不启用该flag；证据release/native-network70-resident-c32-matrix，runner run_resident_c32_matrix_network.ps1。权重复制发生初始化，不改数值，游戏未改，10fps未达到。

post70细分15帧exact：body33.60917ms、merge1.10036ms、RGB0.84625ms，begin队列间隔0.33840ms。暖全网591.25693ms，纯观测无提速。大头是末层C32，不是RGB精确投影；下一步定位C32共享注意力/FFN。旧post70标签现在仅最后间隔，完整比较必须加post70_*，不能宣称post降为零。证据release/native-network70-post-detail；有效无诊断基线仍约586ms，游戏未改，10fps未达到。

多头shift合并访存候选15帧exact：COALESCED_MULTIHEAD_SHIFT=1使用元素级二维dispatch，原零填充/短高度循环/crop索引保持；首C64 pack/crop0.14298/0.09751ms（此前约0.37/0.35）。整网暖583.589209ms、末5帧587.675586ms，较586.27370ms有效基线差异小，尚不宣称稳定整网提升，默认仍旧路径。证据release/native-network70-coalesced-shift，runner run_coalesced_shift_network.ps1。无新buffer，游戏未改，10fps未达到。

C64收缩全tile预载实验未采纳：PRELOAD_C64_CONTRACT=1一次载入16×256 half到8KiB LDS（旧1KiB逐K32），保留K32 H/F，15帧exact。暖589.420234ms、末5帧589.916056ms，首收缩1.94924ms，局部改善极小，未优于586.27370ms有效基线。默认关闭，证据release/native-network70-preload-contract，runner run_preload_contract_network.ps1。没有新增全图buffer，游戏未改，10fps未达到。

共享exp/prob候选15帧exact但未采纳：run_multihead_av_network.ps1 -SharedProb编译NATIVE_SHARED_PROB=1，复用scores行存exp，概率直接写死Q/K区，去掉线程ex/prob数组。首C64 attention3.54480→3.21213ms，但全网暖587.486075ms、末5帧592.152464ms，未优于586.27370ms基线。默认不加SharedProb，当前有效配置不变。证据release/native-network70-shared-prob；不宣称实际测得寄存器spill/银行冲突，仅记录算法及计时。游戏未改，10fps未达到。

最新C64/C128/C256 Wave AV通过15帧最终exact，暖586.27370ms、末5帧588.739364ms。WAVE_MULTIHEAD_AV显式选择独立native_wave_av[_c64/_c128].cso，复用死Q/K存两段K32概率，V为原F8格点half，H/F与概率求和顺序不动；无新增全图scratch。首C64 attention4.07296→3.54480ms，收益有限，归一化/概率阶段仍待细查。证据release/native-network70-multihead-av，运行run_multihead_av_network.ps1继承parallel split配置。游戏未更新，10fps未达到。

最新C64首层细分计时15帧exact，暖601.95719ms（仅增加观测，不是提速）。attention4.07296ms、FFN收缩2.00431ms、展开0.93054ms，pack/crop0.37155/0.34641ms，QKV矩阵0.21472ms。下一步优先原native_c64.hlsl中仍串行的prob×V；只QK已Wave，AV还没换。证据release/native-network70-c64-detail。encoder5_8标签现在排除了首层c64_probe*，必须加回这些区间才能与旧标签比较，不能报27.8ms为收益。游戏未改，10fps未达到。

最新512 FFWD按8个独立通道组并行：15帧最终exact，暖597.88739ms、末5帧598.934558ms。PARALLEL_SPLIT_FFWD显式启用，内部共享mix仅16×80 half，每组只算自身64混合输出但仍读取全部512输入；不增整图scratch、不改网络/权重/舍入。首split_stage0 3.43011→1.17887ms，encoder23_30_body 42.02133→22.32274ms。旧融合8组候选仍保留且WAVE_SPLIT_FFWD=0。证据release/native-network70-parallel-split；run_parallel_split_network.ps1继承649ms配置。游戏DLL未更新，10fps未达到。

最新基线逐提交诊断完成：15帧exact，每帧512次（reflect/temporal＋网络，不含验证读回；连读回计513），暖GPU区间642.60923ms、外层wall760.63721ms、差118.02799ms，其中逐Submit内部wall合计699.20364ms。额外timestamp/readback/fflush造成观察开销，不把此wall与无诊断649ms直接比较；GPU区间含访存/barrier，非纯计算。证据release/native-network70-current-submit-profile；analyze_submission_profile.py排除初始化队列及验证读回。仍需重点改GPU算子，下一候选512 FFWD分阶段并行；不重试已DEVICE_HUNG的整网/整阶段合并。生产路径未改。

最新ViT Wave注意力15帧exact，暖649.227125ms、末5帧652.626334ms。WAVE_VIT_ATTENTION显式开启，16查询/组，QK和AV用Wave矩阵；原exp位映射、denominator树及K32 H/F顺序不变，输入F8→half无损。八层stage3由约7.54～7.83ms变为3.68～4.58ms；其他阶段有波动，整网仅由661.67降到649.23ms。21,568字节组共享区，无新增全图缓冲/无提交合并。证据release/native-network70-wave-vit-attention，运行run_wave_vit_attention_network.ps1继承C32 coalesced配置。游戏DLL未改，10fps未达到。

最新C32收尾按通道合并访存：COALESCED_FINISH显式开启，保留旧入口默认路径、H/F和池化求和顺序。15帧最终exact，暖661.67148ms、末5帧662.32926ms；preblock_detail_stage2由9.30013到1.14322ms，首个c32_probe_stage2由2.69509到0.24597ms。其他阶段存在波动，不把全部整网差额归因此改。无新增GPU缓冲，2D dispatch覆盖回归通过。证据release/native-network70-coalesced-finish；run_coalesced_finish_network.ps1继承上一有效配置。游戏DLL未改，10fps未达到。

最新raw下采样池化＋Wave投影15帧exact，暖688.03063ms、末5帧685.43865ms。WAVE_HEAD先把ViT入口head16.74132→0.04410ms（含池化）；WAVE_DOWNSAMPLE再覆盖C64/128/256 raw路径，非raw的ds4保持旧实现。矩阵无损half本地驻留、池化借共享packed区；valid矩形补零不变。证据release/native-network70-wave-head与-wave-downsample。参数化初版误改FP8常数已修正并加静态回归，失败日志保留；游戏未改，10fps未达到。

512融合Wave FFWD候选15帧exact但暖708.75353ms，不优于706.43771ms，保持WAVE_SPLIT_FFWD=0。新增首Split分段计时确认FFWD3.48644ms、QKV0.23348/attention0.45686ms；ViT入口head单独16.74132ms，下一步优先下采样投影。证据release/native-network70-wave-split-ffwd与-split-profile。encoder23_head现在只剩bridge间隔，需加encoder_head/encoder23_30_body和首层split*，不能误报标签变小为提速。

首層拆分已修复并通过15帧exact，暖706.43771ms、末5帧706.45990ms。小型双路探针定位混合prefix已错误（全为微小正数），将signed-int32×2^exponent直接RNE到half的等价整数公式代替拆分路径double转换后恢复；202905 CPU随机/边界案例bit0，GPU小探针前馈/最终两轮bit0，整网15帧bit0。确切编译器内部机制未定位，不宣称已证明驱动bug。证据release/native-network70-split-preblock[-fixed]；SPLIT_PREBLOCK_FFN显式开启、无新全图缓冲，游戏未改，10fps未达到。

最新两个实验未采纳：直接F16矩阵累加0.697196ms但438687/8847360值不同（max2），收益小，保留exact方案。首层prefix→Wave FFN拆分（SPLIT_PREBLOCK_FFN）首帧6635518/6635520最终值不同，输出范围0.1..0.9、接近原输入，MAE0.03134，不能当硬件小误差。默认关闭，数据release/native-network70-split-preblock/；下一步单独核对prefix与FFN接点，当前有效基线仍约746ms，游戏安装未改。

最新C32完整Wave注意力（QKV/QK/AV/projection）15帧exact，暖745.959355ms、末5帧744.382106ms。AV模式先将exp/prob转置存入已闲置Q/K half区（不改softmax求和），V存FP8格点half，后续两K32 H；投影仍保留最后half_add_preserving_midpoint。AV单独约752.19737ms，额外投影收益小。初版F32 B矩阵Cast到F16在初始化存取违例，已移除；直接half V方案恢复，故障证据保留。见release/native-network70-wave-c32-av和-c32-full-attn。游戏未改，10fps未达到。

最新普通C32 Wave FFN＋初始化无损打包本地权重：15帧exact，暖均762.82839ms、末5帧756.13036ms。首个C32 FFN9.30389→1.90317ms；每组重复转权重版约867ms无收益。WAVE_C32_FFN=1及WAVE_C32_FFN_LOCAL=1启用，仅raw_features，RGB/noise前缀未改。证据release/native-network70-c32-ffn-local/。新增C32首层细分计时，encoder1_4标签现不含该首层全部区间，比较须加c32_probe*，不能误报标签缩短为加速。游戏未改，10fps未达到。

最新512分裂层矩阵QKV＋Wave评分15帧exact，暖均862.25461ms、末5帧855.93912ms；encoder23_head=58.36901ms（旧109.29753）。MATRIX_SPLIT_ATTENTION显式启用并要求图级workspace，QKV无损half权重GPU本地、临时量共用，FFWD/投影不改。local14.711GB低于15.397GB预算。证据release/native-network70-matrix-split/。下一大项preblock/C32前馈；安装游戏未改，10fps未达到。

最新ViT Wave收缩/投影15帧exact，暖均965.47653ms、末5帧964.39536ms。WAVE_VIT_REDUCE显式开启，K4096/1024两CSO，保持四分区累计、初始残差H、每K32 H，矩阵无损half驻留GPU、skip系数原FP32位元。local14.686GB低于15.387GB预算。证据release/native-network70-wave-vit-reduce/。下一大项encoder23_head约109ms/preblock约103ms；游戏未改，10fps未达到。

最新ViT Wave展开＋本地half权重15帧exact，暖均1042.86360ms、末5帧1037.73500ms。保留65536输出/逐块提交，权重原值无损half，组内K32软件H。WAVE_VIT_EXPAND与RESIDENT_WAVE_VIT_EXPAND显式启用；先仅Wave为1133.27541ms，再本地权重降到1042.86360ms。local14.598GB在15.387GB预算内，证据release/native-network70-wave-vit-local/。下一步ViT收缩/投影，游戏未改，10fps未达到。

最新C32 Wave评分及QKV两方案均15帧exact：仅scores暖1183.24196ms，scores+QKV暖1170.98909ms、末5帧1171.23997ms。共享32KiB阶段复用不增全尺寸scratch，QKV权重转half前检查无损。WAVE_C32_QKV依赖WAVE_C32_SCORES，专用CSO；证据release/native-network70-wave-c32[-qkv]/。相对前版1.187秒改善有限，下一步转ViT/512与preblock大项；游戏未改，10fps未达到。

最新C64/C128/C256全Wave路径15帧exact，暖均1187.29283ms、末5帧1188.06103ms。WAVE_C64显式依赖MATRIX_C64，共享区使用C64容量；encoder5_8=37.62461、tail63_65=27.28023ms。local14.531GB低于15.396GB预算。证据release/native-network70-wave-c64/profile-validation.json。此前C64 Thread矩阵回退不适用于本Wave结果；游戏仍未更新，10fps未达到。

最新C128/C256 Wave整网15帧exact，暖均1223.90318ms、末5帧1220.70869ms。C128 encoder9_14=30.22797ms、tail57_61=24.68307ms；local14.469GB在15.381GB预算内。WAVE_C128显式开关、_c128 Wave CSO分开命名，C64仍旧分块。证据release/native-network70-wave-c128/profile-validation.json；游戏未更新，10fps未达到。

最新C256 Wave展开/收缩/QK评分已接整网15帧exact：DLSS5_TEST_WAVE_C256=1需MATRIX_C256，C128矩阵/共享workspace保持，C64旧分块。暖均1311.13692ms、末5帧1304.05339ms，tail49_55=24.51406ms。local14.469GB低于15.381GB预算。证据release/native-network70-wave-c256/profile-validation.json。游戏安装尚未更新，10fps未达到。

最新Wave Q×K评分接C256核心：归一化后的FP8格点Q/K用half共享，wave合作16×32乘32×16写scores，后续softmax近似/归约/V加权原样。五轮最终字节一致，attention2.71318→0.96467ms，完整核心5.17005→3.42863ms。证据release/matrix-c256-wave-scores/result.log。仅显式wave_scores/C256，默认关闭，未接整网/游戏。

最新Wave收缩接C256核心：每组16像素×16输出，K1024按K32加载组内half小块，保留每步H与最终F；无新增全尺寸输入打包。对照上一版Wave展开/旧收缩，五轮最终字节一致，contract1.70686→0.65209ms，完整核心5.89964→4.88883ms。证据release/matrix-c256-wave-contract/result.log。use_wave_contract显式开启、依赖Wave展开，默认关闭；未接整网/游戏，下一大项attention约2.44ms。

最新Wave展开已接C256 GPU核心链：同轮baseline为Thread矩阵展开＋矩阵QKV，候选只换Wave展开；五轮最终2211840值原版/两路字节一致。展开1.24613→0.34431ms，打包约0.194ms均计入，完整核心6.97843→6.11890ms。证据release/matrix-c256-wave/result.log。use_wave显式参数、仅packed C256，默认关闭；下一步Wave收缩（当前约1.7ms），尚未整网/游戏启用。

2026-09-08新方向：Wave scope矩阵×矩阵展开探针16×32乘32×16，保留每K32软件H，完整8847360值对exact激活参考零差异，0.732240ms。此前Thread scope完整K256累加1.713773ms且172676值不同，无实质收益不采用。证据release/matrix-real-probe/timing-wide-wave.txt。下一步把Wave算子接GPU链，尚不含运行时packing、不宣称整网速度。

C64矩阵实验15帧exact但变慢：暖均1441.31863ms、末5帧1437.48887ms；相比C128/C256共享基线，encoder5_8+42.09、tail63_65+31.96、body62+10.18ms，回退主要就在C64。local14.531GB低于15.397GB预算，不是上轮超预算情形。DLSS5_TEST_MATRIX_C64默认关闭，不采纳到游戏；仅开启时扩workspace至488×296×64，否则维持C128大小。证据release/native-network70-matrix-c64/。

图级共享矩阵工作区已通过15帧exact：DLSS5_TEST_SHARED_MATRIX_WORKSPACE=1，按248×152×128元素容量，共用packed/qkv临时量，编码器/解码器显式传入，跨层输出/skip不共享。本地usage15721615360→14468567040，节省1253048320字节，末预算15373082624；同C128+C256配置暖轮1527.07107→1354.10840ms、末5帧1344.92482ms。证据release/native-network70-shared-workspace/。C64矩阵尚未开放，扩大范围前需扩大workspace容量；游戏安装未更新。

显存诊断已确认：C128+C256矩阵整网15帧全部exact；初始化后local用量15049674752，运行后15721615360字节，最后预算15395364848，最高超预算326250512字节；nonlocal819978240。证据release/native-network70-memory-c128/。这说明预算压力，不等同已测到具体换页耗时。下一步优先共用顺序层的matrix_input/qkv_raw工作缓冲，保持独立图/设备隔离和barrier状态，不继续单纯增加每层常驻scratch。

C128矩阵路径整网五帧exact，但本轮暖均1515.8034ms，未优于C256-only的1347.77308ms。主要额外耗时发生在未改ViT attention（各+14～22ms），帧总时间1561→1553→1533→1502→1476ms仍下降，因此需查显存预算/暖机稳定性，不能直接判定C128算法慢了168ms。DLSS5_TEST_MATRIX_C128默认关闭，不部署；证据release/native-network70-matrix-c128/profile-validation.json。

最新硬件矩阵C256整网五帧已通过：DLSS5_TEST_MATRIX_C256=1经NativeC64Shift启用打包展开/QKV，含各层移位、raw输出、history off/on/reset，GPU实际最终结果与原版逐字节一致。暖轮1347.77308ms，encoder15_22=57.23774ms。证据release/native-network70-matrix-c256/profile-validation.json。此运行使用Agility721/experimental与预览驱动；旧1.470秒环境不同，不能将全部差额归给算子。游戏安装未改，10fps未达到，后续扩通道需沿该运行环境对照。

最新C256矩阵QKV核心：显式use_matrix_qkv依赖packed matrix expand，FFN结果在GPU再打包一次、LinAlg计算原Q/K/V后送回原normalize/attention/projection。block52五轮最终原版/旧核逐字节一致，完整核心13.80241→7.33708ms；QKV pack0.01224、matrix0.34057、剩余attention2.51536ms，对照旧attention8.86283ms。证据release/matrix-c256-qkv/result.log。尚未整网/游戏推广，下一步完整C256各层回归。

最新矩阵C256 GPU打包路径成功：每帧FP32→half打包0.19383ms＋矩阵展开1.10214ms=1.29597ms，同轮旧分块展开2.32118ms；完整核心13.88444→12.74870ms，五轮最终原版/旧核逐字节一致。无CPU逐帧打包。use_matrix/pack_input均显式开启才生效；packed CSO独立名native_matrix_expand_packed.cso，另需native_matrix_pack.cso。证据release/matrix-c256-packed/。还未接整网/游戏，下一重点是耗时更大的QKV/attention投影，不能将局部收益当10fps。

矩阵展开已接NativeC64真实GPU特征→收缩→attention→projection，block52五轮最终2211840值与原版oracle和旧分块baseline一致。仅显式use_matrix参数启用（C256/split限定），游戏未启用。性能反而差：展开6.15166ms vs已优化baseline2.14514ms；预打包half探针不能代表直接FP32输入接口成本。证据release/matrix-c256-integrated/result.log。下一步测GPU一次打包供各输出块重用，不能推广此慢版。

最新矩阵展开含gate/F：完整8847360输出矩阵/标量逐字节一致，CPU gate参考也different0。矩阵1.748128ms、标量探针2.384248ms；原生half转换变体2.156008ms且910515值不同、max2、MAE0.0020885349，不采用。仍未包含运行时输入packing及完整网络，下一步接GPU算子接口。证据release/matrix-real-probe/timing-activated.txt。

最新硬件矩阵完整尺寸线性展开：block52全部8640×1024输出，K256每K32 H；矩阵0.801680ms、标量探针2.170559ms（各10次、无独立暖机），8847360实际float结果finite且cmp逐字节一致。证据release/matrix-real-probe/timing-full.txt。尚不含运行时输入packing、gate/收缩、整网，不是对最优旧FFN的端到端速度比较。下一步接真实算子接口与激活。

硬件矩阵初步独立计时：同row0真实样本，MATRIX_PATH=1/2分别只执行矩阵/标量，十次dispatch均值0.056760/0.100652ms，实际32768-byte输出逐字节一致（b68a03ca…25d4）。没有独立暖机，不是对已优化分块整层的比较，更不能外推网络FPS。证据release/matrix-real-probe/timing-row0.txt。下一步完整像素/全通道独立算子与最佳旧核对照。

最新硬件矩阵真数据探针：block52 C256展开的全部1024行×256个选定像素、完整K256，8次K32 dot后各H，矩阵接口对GPU标量参考262144结果零差异。原输入/权重转F16无损；还未覆盖全部8640像素、gate/收缩或整网，未计时。证据release/matrix-real-probe/result.log及各row*.json。下一步真实独立算子性能测试，不必预先放弃exact。

## 22:21用户已批准新路线；22:24驱动安装与首个数值探针成功

用户明确允许预览驱动、保留exact裁判链、快速版仅允许硬件算术差异（权重/结构/输入依赖不动），先测矩阵是否能exact。此前“等待批准/blocked”记录仅为历史，不再是当前障碍。

20260907-approved安装退出0、厂商日志完成，AMD当前32.0.31007.2048，Intel不变，启动时间仍8/31，无重启。安装记录独立于9/5，回退127文件保留；一次性SYSTEM任务已确认Ready/result0后移除。开启进程实验特性后SM6.10、LinAlg tier0x10；不加--experimental的能力探针仍会显示tier0，不能误判驱动失败。

旧smoke8192值一致；新matrix_rounding_probe8192值亦一致（32项dot、带符号E4M3可表示输入/权重、F32矩阵输出加残差后H）。这只证明该探针可对齐，不证明全部累加情形或真实层。下一步真权重/真实输入验证，再接快速路径。精确链及游戏DLL未改。证据release/driver-install-20260907-approved/。

## 自动优化停在用户决策点

10fps未完成，有效整网基线约1.470秒。矩阵接口路线需要重新安装预览驱动，已多轮等待真人确认；期间两个普通shader替代实验均无收益。最后复查驱动仍32.0.31041.1004、无游戏/测试进程、工作区干净。暂将goal标记blocked，避免自动重复消耗额度；不表示理论上普通shader再无优化空间。待用户批准换驱动，或指定继续当前驱动路线后恢复。未自动安装或重启。

本页优先于 README 中旧里程碑。不宣称完整移植目标已经完成。

## 20:14之后：用户恢复性能工作

C32注意力位元FP8量化候选DLSS5_TEST_FAST_C32_FP8=1五帧exact，但1488.7480575ms慢于1469.7617425ms基线，保持关闭。证据release/native-network70-fast-c32/profile-validation.json。未换驱动、未改游戏安装版。

等待换驱动确认期间继续普通shader实验：DLSS5_TEST_RESIDENT_VIT_LINEAR=1将非decoder线性矩阵一次上传DEFAULT。五帧exact但暖轮1563.7835275ms，慢于1469.7617425ms基线，默认关闭、不推广。证据release/native-network70-resident-vit/profile-validation.json。未安装驱动，游戏安装版不变。

重要环境变化：重新检查Windows，AMD驱动当前32.0.31041.1004（不是9/5曾装的32.0.31007.2048预览驱动）。私有Agility721探针当前SM最高6.9、LinAlg tier0；旧matrix_smoke与新matrix_rounding_probe均PSO E_INVALIDARG。尚未改驱动，不能假设矩阵核心接口仍可用。新32项舍入探针只完成编译，未执行数值验证；需用户确认是否再次切预览驱动，普通SM5主线仍可继续。

最新逐提交计时（DLSS5_TEST_SUBMISSION_TIMING=1）五帧exact：每帧513次提交，暖轮GPU区间合计1390.92069ms，CPU录制至完成等待合计1469.05275ms，差78.13206ms（非纯CPU时间，含排队/同步等）。计算本身占主要成本，不能靠合并提交达到100ms。证据release/native-network70-submit-profile/；默认关闭诊断，未改提交策略，安装版不变。

ViT expand塊内共享实验DLSS5_TEST_TILED_VIT_EXPAND=1保留原65536输出提交边界，五轮exact但全网1482.349745ms，未优于1469.7617425ms基线；默认关闭、不部署。证据release/native-network70-tiled-expand/profile-validation.json。下一步需区分ViT单dispatch GPU计算与跨提交等待，不能继续把stage总计时当纯算子时间。

ViT同阶段合并chunk提交实验失败：session43310/PID41232 exit1，HRESULT2289696774=0x887a0006 DEVICE_HUNG，无完整帧。实验代码已撤回，仍用逐chunk等待，不能重试整网/整阶段大列表。独立AMD原codec三scale执行成功，GPU已可用。失败证据release/native-network70-batch-vit/；有效性能基线仍1.470秒，下一步只改算子（如ViT expand分块），保留提交边界。

最新C64/128/256/512注意力共享行padding通过完整五帧字节对照，1512.6764325→1469.7617425ms。显式DLSS5_TEST_PAD_MULTIHEAD_LDS=1启用，证据release/native-network70-pad-multi/profile-validation.json，安装DLL未改。

最新C32共享存储行跨度32→33实验五轮exact，暖轮1590.0457→1512.6764325ms。DLSS5_TEST_PAD_C32_LDS=1显式启用，证据release/native-network70-pad-c32/profile-validation.json；游戏安装版未改。

解码尾段细分已完成，release/native-network70-tail-profile/profile-validation.json五帧exact。tail49_55约104.12ms、tail67_69约76.54ms、tail57_61约63.01ms、tail63_65约42.60ms；三段上采样projection合计约3.55ms，不应优先改它。新增profile将原decoder_stage12拆成十段，该旧标签现在仅表示最后时间戳间隔，比较尾段须求tail*合计，不能拿近零旧标签宣称加速。

最新C32注意力输入量化复用整网五轮exact：1678.7057975→1590.0457ms，证据release/native-network70-cache-c32/profile-validation.json。DLSS5_TEST_CACHE_C32_INPUT=1开启，未启用收益不明的RESIDENT_C32_WEIGHTS；默认关闭，游戏安装版未改。

额外C32系数驻留实验DLSS5_TEST_RESIDENT_C32_WEIGHTS=1五轮exact，但全网1678.7057975→1665.2109175ms（不足1%），未确认超出波动，保持默认关闭、不作为确定加速推广。证据release/native-network70-resident-c32/profile-validation.json，后续性能基线仍以resident-noise约1.679秒为准。

最新192MiB noise表改为初始化一次上传DEFAULT/GPU本地只读缓冲，DLSS5_TEST_RESIDENT_NOISE=1显式启用，五轮最终字节一致。暖轮1720.227495→1678.7057975ms，preblock193.22972→144.14321ms。证据release/native-network70-resident-noise/profile-validation.json；无逐帧复制，安装版仍未更新。

最新普通C32共享FFN整网五帧通过，暖轮1856.0057175→1720.227495ms；显式DLSS5_TEST_SHARED_C32=1启用，只对raw_features路径生效，RGB噪声输入算法未改。证据release/native-network70-shared-c32/profile-validation.json。游戏安装版仍未更新，当前所有完整测试均已退出。

最新QKV分块已通过完整五帧原版字节对照，暖轮2173.7783275→1856.0057175ms，证据release/native-network70-tiled-qkv/profile-validation.json。DLSS5_TEST_TILED_QKV=1显式启用；默认关闭，游戏安装版未改。注意ViT stage2是QKV，stage3才是attention，先前关于stage2为attention的方向已纠正。

最新完整整网共享FFWD已通过（session2674/PID1808 exit0）：五轮最终原版逐字节一致，暖轮2439.2734975→2173.7783275ms；encoder23_head降至106.53807ms。证据release/native-network70-shared-ffwd/profile-validation.json。游戏DLL未更新、游戏仍退出。接下来可检查ViT attention单线程ex[640]/quantized[640]暂存开销，尚未实施该优化。

后续目标更新为实际神经画面超过10fps。新增默认关闭DLSS5_TEST_SPLIT_FFWD=1共享前馈候选，小夹具五轮四阶段/输入切换通过，前馈暖均34.16767→0.65429ms，整核心36.07349→4.27147ms；注意力段1.47757→3.12796ms有回退。仅16×8样本，完整尺寸/整网尚待验证，不能外推游戏FPS。日志release/split-shared-ffwd/result.log与release/split-projection-tiled/profile.log。

用户要求暂以现有正确性为基线，继续优化。首个候选将NativeSplit的两段C512投影切成共享分块，默认关闭，仅DLSS5_TEST_SPLIT_PROJECTION=1启用。现成四阶段输入切换五轮通过，完整off/on/reset五轮最终字节也一致；暖轮2.710→2.439秒，约10%收益，证据release/native-network70-split-projection/profile-validation.json。代码已保留，尚未推广到游戏DLL。

为避免GPU计时争用，21632已正常Alt+F4退出，完整测试PID21952已exit0；当前游戏不在运行，已安装DLL与开关文件不变。下文“最后检查PID21632”描述封存时历史状态，不是现时进程。继续优化时以此段及worklog最新条目为准。

## 当前能做到什么

- AMD完整0～70层、1080p输入、1152处理高度、原版shift3，五轮history off/on/reset最终输出与独立原版逐字节一致。证据：`release/native-network70-tiled/profile-validation.json`。全网暖轮约2.71秒，不是实时。
- 旧真实游戏单帧PID34096/request1已独立原版重放验证一致，证据：`release/native-live-neural-34096/`。
- 当前连续展示DLL每帧重设history=0，游戏内连续处理/回写已运行；用户20:09确认“这个是对的，虽然只有2fps，主人公有一点黑”。这是用户观感确认，不替代所有新帧的数值核验。
- 43156正面像及21632展示首帧已读回，finite、alpha保持、RGB有变化；这些新帧独立原版整网对照尚未完成。连续历史反馈、时序画质、最终开关对照仍待完成。不能用每帧reset冒充完整temporal。

## Windows现场（amd9070）

最后检查PID21632仍Responding，持续render_complete；不要假定下次PID仍相同。

- 游戏：`C:\Program Files (x86)\Steam\steamapps\common\StellarBlade\SB\Binaries\Win64`。
- 自动载入DLL：`native-submission-order.addon64`，实际为神经渲染验证版，名称带observer的老日志字段不代表只读。
- 当前SHA256：`b6e42d355396a88a3f4181556240497a9514dfb9827e8aeee517b5aa322c0903`。
- 资产：`D:\DLSSNR-Lab\native-game-tiled-assets`；噪声表：`D:\DLSSNR-Lab\matrix-probe\native-runtime-rgb512\functions.f32`。当前不是自包含发行包，不能只复制DLL到另一台机器。
- 开关文件：`D:\DLSSNR-Lab\continuous-reset-preview.txt`。存在即自动慢速反复处理；初始化本次约195秒，每次处理约4秒，屏幕FPS计数不等于神经吞吐。
- 暂停展示：把上述开关文件改名为`.disabled`，当前在途帧完成后停止自动请求；还原文件名即可继续。无须杀进程或改存档。
- 回退到手动单帧：先正常退出游戏，备份当前DLL，再用同目录`native-submission-order.addon64.before-continuous-reset`替换DLL，同时禁用上述开关。该备份SHA为`61e731057b39233b8e9c55cdd6d08791c6a7196d99d6cf44e2b812f18552b1cb`。不要在游戏运行中替换DLL。
- 旧错误渲染器`dlss5-1080p-runtime.addon64.before-order-probe`仍禁用，不要恢复成自动加载后缀。
- 正常启动Steam游戏即可自动加载；现有任务`DLSS5GameLaunch`也可启动。初始化后自动处理，不需要再发请求文件。
- 日志：`D:\DLSSNR-Lab\logs\native-game-oneshot.txt`。确认当前PID的ready、render_begin、render_complete；render_failed不是成功。连续模式只保存request1前后原始帧，避免无限落盘。

## 本地封存与Git边界

`release/`已由仓库`.gitignore`中的`/release/`忽略。没有删除旧证据、权重或远端备份；没有把权重、图片、DLL提交到Git。

封存目录：`release/checkpoints/2026-09-07-correct-game-preview/`：

- `native-game-reset-preview.addon64`：当前安装版本，SHA见上。
- `native-game-neural-tiled-explicit.addon64`：手动单帧版，SHA见上。
- `native-network70-tiled.exe`：已通过整网测试的程序，SHA `cf9de2707d6ce5ea4699b41f55a9b15bb2b7739970cdf07058b57b30f591e319`。
- `run_nvidia_ui.uncommitted.ps1`：此前未提交的5090聚焦实验原样保留，未认定为正式修复。SHA `f267268583383d81b7e1a38ccb3568cc33d41098293bf29cb90b65a967f08698`。根目录同名脚本已恢复HEAD内容；需要续该实验时对比这份封存，不要以为改动丢失。

其余大文件保留原位，不复制多份GiB权重：

- `release/native-live-tiled-43156/`：正面像before/after及预览，原始线性数据预览不是最终swapchain截图。
- `release/native-live-continuous-21632/`：连续展示首帧、日志快照和被终端遮挡的桌面截图；该截图不能作为完整画质证明。
- `release/native-network70-tiled/`与`release/native-network70-profile/`：候选与基线输出、计时、验证报告。
- `release/native-color-frame/`、`release/native-temporal-valid1080/`等原版参考及权重保持原目录。
- 远端`D:\DLSSNR-Lab\live-reference-43156-1`已生成该帧原版encode；尚未完成其余原版重放。

## 恢复工作

1. 先看本页与porting-worklog最新段，再查Git状态、实际游戏PID和当前DLL散列，不重启仍存活的测试。
2. 用户20:14重新授权性能优化，暂沿用现有正确性基线；仍保留回归，不以牺牲神经输出换FPS。新帧独立核验与历史反馈未完成项保留，不改写为已完成。
3. 构建当前展示版：`bash build_native_game_verification.sh /home/lmxxf/work/tmp-test/minhook.KmbJvO/repo /tmp/reshade680.MNSVvW/include OUTPUT.addon64 --tiled`。先确认外部MinHook/ReShade路径仍存在；DLL已封存，不依赖临时二进制存活。
4. `deploy_native_tiled_verification.ps1`是旧手动版的一次性部署脚本，硬编码旧SHA且拒绝覆盖备份；不要拿它重装当前展示版。当前展示版SHA/回退方法以本页为准。
5. 不推送/发布旧网盘包作为完成版。完整目标保持未完成。
