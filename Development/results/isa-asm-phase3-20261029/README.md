# gfx1201 汇编优化 Phase 3，2026-10-29

生产起点 3540cf0d，继承 a71f5d72 的已验收 Host 路由。三刀均按证据 STOP，没有获胜改动合入。指令数下降、VGPR 降低不能替代同批产品性能门。

## 三刀结论

| 刀 | 汇编实证 | 数值和性能结果 | 提交 |
|---|---|---|---|
| 1A 成对 FP8 打包 | byte-in 684→646 指令，95 VGPR；非 byte-in 119→112 VGPR，零 spill | 七档 Gold 同；Original1088 +17.239µs，Small900 +7.476µs，STOP | 1f780a34 |
| 1B 权重预取 | 全预取 byte-in 118 VGPR 跨档，CPU 拒绝；受限版只启用非 byte-in，118 VGPR/零 spill | 七档 Gold 同；Original1088 +16.878µs、900 +11.089µs，均三轮均值退，STOP | 同上 |
| 2 B 不变折叠与预取 | 普通 B4 折叠仍 spill；独立非 W16 预取资源不变；SP 保持原 B2，run184→153、recover235→204 VGPR/零 spill | 两候选分别七档 Gold 同、四档 ABBA；SP Original1088 +58.954µs，三轮均值全退；普通预取不稳定，STOP | cb5a59d2 |
| 3 serial_norm 平方缓存 | 原编译器已复用平方；C64/SP 代码完全相同；C512 2104→2096 指令，188 VGPR 不变，平方数仍64 | 七档 Gold 同；四档均值全部退步，STOP | 98ba6985 |

每项源码补丁、ISA 分类、资源、逐轮计时见 [刀1](knife1-README.md)、[刀2](knife2-README.md)、[刀3](knife3-README.md)。1A 的额外 Hrtz 位掩码试验既未过资源门，又被首帧 Gold 拒绝；不计入通过项。该次资源检查后误启动 Gold 的流程错误已记录，未继续 ABBA。

## 验证口径

所有远端 GPU 与 RTC 作业使用 run-jobs.ps1：game-check、进程检查、D盘≥100GB、CreateNew 独占 gpu.lock、15秒游戏检查，finally 释放锁。严格串行，每 slot 独立 frame.exe，三轮 ABBA，240 帧弃前80，160稳态帧，960/arm，edges_only=1、NET0。Gold 七档首尾逐位同、invalid=0。资源失败的普通 B4 折叠未送 GPU。所有测得反例保留，无刷轮替换。

## 最终生产重建与算力缺口

下列结果由 final-statistics.json、gap-statistics.json 和 full-rebuild-verification.json 生成；Frame wall 与纯NN HIP/Vulkan GPU计时分别列账。由于三刀均未采用，生产重建前后用于复核环境噪声，不能宣称本轮优化收益。Mochizuki 固定 d1185d2，保持其源目录完整文件哈希/大小/时间戳不变，缓存只写本实验目录。


### 最终七档稳态 Frame（ms）

| 档位 | 重建版 mean | p99 | 对照 mean | 差 µs |
|---|---:|---:|---:|---:|
| small1080 | 5.449903 | 5.629798 | 5.456265 | -6.362 |
| small900 | 4.176986 | 4.300051 | 4.184027 | -7.040 |
| small720 | 2.959011 | 3.135656 | 2.957941 | +1.070 |
| smallwindow | 1.903605 | 2.035761 | 1.898671 | +4.934 |
| original1152 | 8.976510 | 9.114514 | 8.975602 | +0.908 |
| original1088 | 8.693582 | 8.832030 | 8.690044 | +3.538 |
| original960 | 6.405414 | 6.502056 | 6.409044 | -3.630 |

代码与资源相同的重建自对照，差值用于观察测量漂移，不作为优化收益。84 个独立性能进程、每档/arm 960 个稳态帧；14 个 Gold 进程共42帧。Gold 硬哈希与每 slot 首尾哈希均由 runner 核验。

### 1088 纯 NN 同批 NMMN

HIP **8.321360 ms**，锁版 Mochizuki d1185d2 **7.864895 ms**；差 **0.456466 ms（+5.80%）**。

| 轮 | HIP ms | Mochizuki ms | 差 ms |
|---|---:|---:|---:|
| 1 | 8.303606 | 7.861116 | +0.442491 |
| 2 | 8.320525 | 7.863059 | +0.457465 |
| 3 | 8.339950 | 7.870509 | +0.469441 |

三轮 NMMN、每进程80 warm+160 measured、每 arm960帧，Original1x/1920×1088/有效1080/640token/71块/style1/history关。HIP 用 GPU event，Mochizuki 用其 GPU total/160；M 没有逐帧CSV，不能提供其p99。两端使用各自锁定、半精度一致输入夹具与输出硬哈希；跨实现不要求原始输出逐位一致。宿主/提交、噪声生成与执行图边界差异仍存在，此账本是该既定纯NN测试边界下的差值。

旧 gap-phase3 的 0.478631198 ms 仅保留作历史账本；跨批环境和生产版本不同，不能把新旧差值归因到本轮三刀。当前只测得延迟差，不据静态WMMA指令数、VGPR或全网平均吞吐反推峰值算力缺口。下一步应对实际热点做有计时归因的独立实验，尤其检验内存流水与驻留的取舍；保持权重复用、减少指令并不自动更快。

### 重建与交付身份

全部80模块已实际重编（12个LLVM23、68个COMGR，外加gfx1200 C512配方纠正一次），80个ELF架构均核验；全部函数代码及资源与锁定生产相同。容器SHA完全相同的有68个，其余仅容器身份变化，已刷新hip/SHA256SUMS。gfx1200 c512-m32-deep实际代码对应max-ilp，gfx1201对应空选项；初次空选项gfx1200版本隔离未上GPU，纠正及函数比对见full-code-comparison.json。没有新增优化内核。

正式O2 addon/runtime/frame及pure宿主已构建，SHA见final-build/hosts.sha256；运行时烟测见smoke/evidence。完整交付位于本目录payload（不进Git），分析脚本、关键ISA计数和统计进Git，大体积反汇编和编译中间文件清理。原始CSV/log/命令证据位于各case/evidence（不进Git），evidence-index.json可核验身份。锁版Mochizuki文件树及native模型资产前后完整哈希核对，未改玩家游戏文件和0.45 ZIP。
