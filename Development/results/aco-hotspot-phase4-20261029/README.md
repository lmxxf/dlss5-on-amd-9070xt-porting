# ACO hotspot Phase 4，2026-10-29

三阶段闭环：新版Mochizuki v0.0.8 ACO在当前Windows驱动原样可运行；完成三档同机纯NN对账、Small两档同帧阶段拆分、完整业务图逐核计时与两个真实优化原型。两个候选均通过七档Gold，但未过稳定产品性能门，均STOP，生产起点41c28059保持。

## 新版同机基线

| 模型/处理尺寸 | HIP GPU span ms | ACO GPU span ms | 差 ms | HIP较ACO慢 |
|---|---:|---:|---:|---:|
| original1088 | 8.301703 | 5.670747 | 2.630956 | 46.40% |
| small1080 | 5.005096 | 3.430510 | 1.574586 | 45.90% |
| small900 | 3.841217 | 2.607886 | 1.233331 | 47.29% |

每档3轮NMMN，240帧/slot=80warm+160measured，960稳态帧/arm；独立进程、严格串行。Original71块、Small31块，标准CC_SILVER_AARDWOLD/CC_SMALL_FP8，不用EXPERIMENTAL或INT4。两端各自输出硬SHA稳定/有限值；不宣称跨实现逐位一致。53个SPIR-V精确匹配包内NRP，VK_KHR_pipeline_binary/PAL适配在32.0.31007.2048驱动通过，不改版本白名单/系统驱动。

旧d1185d2的0.456ms/+5.80%不能代替新版ACO账本。上表遵循约定HIP/Vulkan各自纯NN计时窗，是实现/图组织/舍入/提交边界的组合差距，不能全部称为纯机器码或数学算力差距。模型和处理尺寸对齐不等于所有数学路径/有效输出裁剪/噪声生成开销逐项相同。新版已有显著更快的同类实现，不能再据旧版差距说已证接近上限；在严格生产0bit约束下能消除多少仍需实验，未测硬件峰值或roofline。

## Small精确拆分

冻结HDR capture同一诊断帧同时采HIP NN事件与D3D12阶段timestamp；三轮ABBA另测无event生产Frame，显式量化探针开销。Frame是离线NativeGameFrame宿主的输入/网络/输出流程，不含游戏渲染和Present。此表与上方合成输入对账分开，不跨数据集相减。

| 档位 | 无event Frame wall ms | 诊断Frame wall ms | NN GPU事件 ms | 前后处理GPU ms | bridge区间−NN ms |
|---|---:|---:|---:|---:|---:|
| small900 | 4.147897 | 4.314636 | 3.707780 | 0.157123 | 0.220516 |
| small1080 | 5.414077 | 5.577623 | 4.768868 | 0.241598 | 0.323158 |

前后处理GPU合计=encode/input/neural/decode/copy；bridge区间还含交接/提交间隙，CPU wall残差不全是GPU算子。诊断Frame额外开销约0.164–0.167ms，不能把诊断wall当生产成绩。NN事件是其明确API窗，不把它与下述设备实时钟拆解强行视为相同边界。

## 热点与计时校准

Windows HIP短事件出现负值/总和不闭合；native-event和no-system-fence变体也未通过一致性校验，未拿来排名。长批HIP event约75ms饱和值与CPU/实时钟不符，亦不作单位标尺。最终采用独立小核的gfx1201 MSG_RTN_GET_REALTIME，运行时WallClockRate确认100MHz，逐dispatch轮转采样、含SP专用launch，扣除100次邻接stamp中位0.8µs。重构与全网实时钟闭合0.7–1.0%，原始ticks/校正值/探针扰动都保留。不能由此编造DRAM/WMMA利用率。

Original：C128 bi_bo约0.819ms、SP run256约0.809ms、C32 post约0.728ms；前两者近并列，选择隔离干预清晰的C128为主靶。Small：C32 post约0.762ms、prefix约0.737ms，两者占核重构总和约31%。对手逐pass查询完整123/71个dispatch，给出同stage/族账本，融合边界不同不作逐核一对一硬减。

## 受控实验

| 候选 | 汇编/资源实证 | 单核结果 | 整网门禁 | 裁定 |
|---|---|---|---|---|
| 3A 深预取 | Contract前读至Expand前；B4不变；242→248VGPR、同硬件分配桶、LDS16KiB/0spill；仅目标函数变 | Original −0.521µs/次，Small不稳 | Orig1088 mean+2.481µs/p99+15.682µs；其他尾反例 | STOP |
| 3B 最小ISA移动 | 一条FW b64跨两条地址VALU；CFG/回边/异步写/EXEC/wait/delay检查；目标外全部字节不变 | Orig+0.085µs/次、Small+0.328µs/次，均不稳 | Orig900三轮均值全退，pooled+4.544µs/p99+15.587µs | STOP |

各7档Gold42帧/invalid0/0bit-diff，合计84帧；各两档单核与四档整网3轮ABBA，整网每臂/档960稳态帧，NET0/edges_only1。保留所有轮次、无刷轮/丢反例、未叠加未验证组合。3B实际完成.s→cc1as→ld.lld编码，再把已验证同尺寸的单函数注入原ELF，避免重链接wrapper的46个非目标字节差异进入实验；136其他函数及所有metadata/描述符不变。

C128本来没有显式waves下限；3A编码分配资源不变，不能把本试验说成完成了高/低驻留对照。源码预取确有局部小收益，但未形成稳定产品收益；ISA原型被否决也不证明汇编路线上限。这些负结果不能升级成“LLVM已到天花板”或“.s是唯一路径”的定律。

## 归档与提交

- [Step1原始统计、输入/输出/兼容边界](step1-README.md)：4a260061。
- [Step2校准、完整逐核/对手stage账本](step2-README.md)：a8103346。
- [Step3安全证明、资源与全部逐轮结果](step3-README.md)：bd1d2168。

数据表分别为step1-statistics.json、step2-hotspot-breakdown.json、3a/3b-statistics.json与3a/3b-kernel-statistics.json。原始命令/log/CSV在各case/evidence（不进Git），SHA索引入库；大体积反汇编、编译中间文件、raw图像和失败候选二进制清理。可复现的宿主/输入/shader payload保留在忽略Git的payload目录；模型私有数据不入Git。

没有赢家，不替换生产80模块或修改hip/SHA256SUMS。最终复核80文件SHA/起点源码、原M源码与锁版d1185完整文件树，结果见final-verification.json。没有安装游戏文件/发布ZIP更改、没有push，GPU锁释放。所有GPU/RTC作业遵守game-check、进程检查、D≥100GB、CreateNew锁与15秒游戏看门狗。

复现诊断构建：build-diagnostics.sh。重建M时先在隔离目录不存在的状态运行prepare-upstream-copy.py，再build-mochi.sh、上游Windows/Linux build_network.py与stage-payload.py。重建3A/3B前先按原LLVM23 RowOpts/-real-true16配方编译c64-wave2-fast到work/c64-baseline，随后prepare3a.py/prepare3b.py（3A再编译work/3a-source）。GPU作业始终使用本目录run-jobs.ps1及已归档manifest，先重建被清理的候选/输入再执行，不能跳过哈希/资源/Gold门。
