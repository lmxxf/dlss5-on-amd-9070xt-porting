# C32 Phase5B：收网与修复闭环（2026-10-29）

**最终采纳三项组合：fast模块全档默认norm-hoist、OutProj权重/残差缩放跨qt共享、修复后的finish→pool融合。** 七档42帧Gold 0 bit-diff/invalid=0，七档Frame三轮ABBA合并mean/p99全赢，七档mean各三轮均赢；正式runtime-smoke通过。起点4cb74f52，未修改游戏或发布的0.45ZIP、未push。

## 修复与生产行为

- C32 fast的CW_FAST_NUM=3令Hrtz恒等，独立MH投影却做真正FP16 RTZ。融合尾部改用显式cvt_pkrtz，随后按任务要求在最终FP8编码前加+0.f；保持pool平均阶段原来的fast语义，不能一并改成真RTZ。组合修复一次通过全七档，源码/ISA摘录和独立实测见[Step1](step1-README.md)。不把一次组合修复的成功说成分别证明两项都单独必要。
- DownCrop按24种几何/shift检查有效目标恰好写一次，2×2采样顺序一致；实际pool调用valid_width/height=0。main仍保留供decoder skip。融合的是Block4 finish与pool projection，Block1 mapped仍由后续链消费。
- c32-wave1-fast配方直接加入CW_NORM_HOIST=1和CW_OUTPROJ_HOIST=1，fast-norm900同步；没有新增几何专属norm宿主分支。OutProj两组16B权重与16个scale在第二qt循环前只载一次，四qt复用，累加顺序不变。
- 融合默认开启，NR_C32_POOL=0可在进程启动时关闭。只有W16默认模式1、字节边、无observer/dump/skip/tap且实际所选模块含新导出时才融合；旧模块或其他W16模式走原路径。所有最终候选及pure/runtime都检查真实融合调用计数。

## 资源与80模块正式构建

gfx1200/gfx1201各40模块实际重编：12个LLVM23正式RowOpts/-real-true16目标、68个当前驱动COMGR目标，COMGR仅gfx1200 c512-m32-deep沿用max-ilp，其余含gfx1201该模块为空opts。80个ELF架构、SHA、函数及完整metadata差异均核验；更新hip/SHA256SUMS。正式O2 addon/runtime/标准和小窗frame/pure构建完成。gfx1200只做构建/ISA/资源核验，GPU实验均为gfx1201。

两架构生产W16导出最高158VGPR，全部导出private=0、spill=0；最终prefix/post148、up_b8 147、融合151VGPR。必须明确：基线每个fast模块已有7个非默认旧回退导出超过160（最大177），本轮未增加这些资源，保留兼容性，并未声称字面上所有旧导出都≤160。完整清单见final-gfx1200/1201-resource-gates.json。

一个旧prefix回退函数出现唯一PC相对常量偏移变化：两架构两模块共4处，逐指令与完整metadata核对，其他指令完全相同，实际载入的4字节常量均0000003c（1/128）；legacy-relocation-proof.json记录地址计算证明。不是悄悄改变旧数学。

## 最终七档Frame三轮ABBA

A=Phase5起点4cb74f52生产宿主/模块，B=本轮正式宿主/模块。每slot独立进程frame.exe、严格串行；240帧弃80取160，每臂960稳态帧，edges_only=1、NET0。下表均为同批合并统计，完整逐轮见final-statistics.json。original960即900档1600×960；Small1080/900/720/window对应1920×1152、1600×1024、1344×768、896×512。

|档位|A ms|B ms|mean Δµs|mean Δ%|p99 Δµs|mean赢轮数|p99赢轮数|
|---|---:|---:|---:|---:|---:|---:|---:|
|original1152|9.071776|8.970630|-101.146|-1.11%|-96.262|3/3|3/3|
|original1088|8.688029|8.601204|-86.825|-1.00%|-67.218|3/3|3/3|
|original960|6.400539|6.382294|-18.245|-0.29%|-0.828|3/3|2/3|
|small1080|5.433036|5.381091|-51.945|-0.96%|-48.671|3/3|3/3|
|small900|4.193351|4.130983|-62.368|-1.49%|-58.074|3/3|3/3|
|small720|2.968534|2.936005|-32.529|-1.10%|-70.108|3/3|3/3|
|smallwindow|1.888030|1.881068|-6.962|-0.37%|-15.318|3/3|1/3|

按Zero校准门禁：合并mean改善、硬件阶段确认加速，并由聚合p99或多数轮次支持；不再用单轮p99直接否决。每臂每轮实际320稳态样本（2slot×160），合并960。小窗只有1轮p99改善但聚合p99仍−15.318µs，依法保留；没有无证据地把波动归因为系统中断。此次组合只跑这一批正式七档，不刷轮。

## C32＋pool硬件实时钟

生产默认没有逐核探针；诊断宿主使用MSG_RTN_GET_REALTIME、设备属性100000kHz和80ticks邻接stamp校正。每帧仅轮转一个业务dispatch，完整序列每slot两轮，三轮ABBA共12观测/dispatch/臂。融合使Original1088 153→152、Small1080 83→82 dispatch。

|模型|旧C32+pool ms|新C32+pool ms|Δµs|
|---|---:|---:|---:|
|original1088|2.827527|2.734623|-92.903|
|small1080|2.249580|2.180300|-69.280|

独立融合的四档合并mean/p99都改善，但Small1080整网仅−0.655µs，接近噪声；其阶段时钟三轮均加速、合并−5.247µs，因此按校准规则进入组合。最终组合净收益独立验证，不把各刀微秒数直接相加。

## 最新生产 vs v0.0.8 ACO：纯NN三轮NMMN

每slot80 warm+160测量，每臂960稳态帧；同机、同模型来源/处理几何、正常精度模式，非EXPERIMENTAL/INT4。M原样导入预编译pipeline binary，无驱动白名单绕过、无系统驱动改动。两边分别固定自己的输出SHA，不声称跨实现逐位相同；M只给160帧GPU总计，不虚构其p99。

|模型|新HIP NN ms|v008 ACO NN ms|差ms|HIP慢%|
|---|---:|---:|---:|---:|
|original1088|8.214575|5.685226|2.529349|44.49%|
|small1080|4.946637|3.450390|1.496248|43.36%|
|small900|3.795411|2.640130|1.155281|43.76%|

本轮有可重复的同批Frame与硬件阶段收益，仍不能宣称接近硬件上限。新鲜NMMN账本刷新实现差距；不得把跨批旧NN数字之差全部归为本轮收益，也不保证在我方0bit-diff约束下能追回全部ACO差距。

## 数值、护栏与交付

最终七档两臂×3帧共42帧固定Gold首尾全同，invalid=0；含Original0900E489…0F4A、Small65D71486…8FCB8，完整六档预置SHA及小窗旧基线配对/历史SHA复核见final-gold-reference.json。正式runtime-smoke在Small1080、proc1920×1152、skip0运行成功，并证明默认融合被调用。

远端所有GPU/RTC统一game-check、进程检查、D≥100GB、CreateNew独占gpu.lock及15秒游戏看门狗，slot无并发。全部大临时输出、候选二进制和缓存清理，正式隔离交付保留在payload；Git只保存代码、脚本、报告、关键ISA摘录和证据索引。锁版d1185树、v008原始源码、已安装游戏、已发布0.45ZIP及外层仓未改；无amend、Co-Authored-By、push。

回执：final-decision.json、final-verification.json、full-rebuild-verification.json、final-build/hosts.sha256、final-cleanup.json及各阶段evidence-index。Step1独立提交d510bb69；最终源码与构建收网独立提交。
