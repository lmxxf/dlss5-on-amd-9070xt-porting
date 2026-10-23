# C：实际Load/Wait依赖审计与唯一尾部调度候选

2026-10-23。只CPU/源码/ISA；生产未动。A、B和phase2刀2/3均STOP，基线仍phase2刀1。GPU/正式COMGR/Gold/三档ABBA由root执行。

## 先看真实依赖

读取当前已锁生产模块：`c512-m32-mh.hsaco` SHA `9afa918b…079b4`，目标`c512_qkv_attention_compact_w16`；`c64-wave2-fast.hsaco` SHA `475f515f…690e0`，目标`c128_wave2_bi_bo`、`c256_wave2_bi_bo_w16`。不是旧D4或旧Mochi缓存。

|核|`s_wait_loadcnt 0`静态个数|结论|
|---|---:|---|
|C512 compact W16|2|两处都必须等最后load的真实数据依赖|
|C128 wave2 bi_bo|11|均发现紧接着读取最后load目的寄存器的消费者|
|C256 wave2 bi_bo_w16|18|同上；不能直接将0改非0|

精确PC/最后load/随后使用见独立审计[dependency-windows.json](dependency-windows.json)，它是人工复核的局部依赖表，不是完整CFG验证器或动态stall统计。

C512两处：

1. `0xFBF74 wait0`：`0xFBE28 global_load_b128 v[128:131]`，紧接两条WMMA读取`v128:129`，所以这两条不能越过wait。
2. `0xFDC64 wait0`：`0xFDB48 global_load_b128 v[106:109]`，随后dual-add读取v106/v108，是真实score bias使用，不能只改阈值。

现C512有58条loadcnt wait，只有2条为0；其余已使用partial wait，最大20。两份wave2合计507条，只有29条为0，partial值最高47。锁a9的ACO `attn`同样有6处wait0，partial最高37；fswin128/256分别8/16处wait0。不同tile/循环/16B或8B装载决定计数语义，**数字更大不等于流水更深、更快**，也不支持“HIP不会partialwait”的说法。

## 找到的实际窗口：保留wait，移动独立算术

在C512第一个wait0之后，`0xFBF88..0xFBFB8`有7条WMMA只读取已经由前面的wait1保证就绪的B片（v100/132/140/112）和A107..110，目的累加器互异，且不碰最后pending的B128..131。

因此可以把这些独立WMMA移到wait0之前，让最后B片在其间完成；仍保wait0后才执行依赖B128的两条WMMA。这是**具体寄存器依赖证明得到的调度窗口**，没有删除等待、放宽memory fence或添加硬编码wait数。

对应源码唯一候选：仅`W16`分支最后`s>=30`的两个K16步，先更新j0..4的10个独立累加器，`__builtin_amdgcn_sched_barrier(0)`，再更新j5的两个累加器。其它30步、原D4环、两侧原scheduler barrier、ca/cb快照、加载布局/数量、每acc K0..31累加顺序均不变。s31虽已无pending也保同一分组，可能有代价；本轮不改成只s30再刷。

[候选补丁](candidate.patch)、[独立源审](source-review.md)。没有新runtime开关、host、新buffer或额外launch；只替换一个`c512-m32-mh.hsaco`，root继续用phase2刀1final host。

## CPU筛查已证明移动发生

本地LLVM21仅语法/初筛。**同一LLVM21**基线/候选均：187VGPR、24SGPR、6144B LDS、零private，18180代码字节、2510指令、404WMMA、168VMEM load，所有opcode计数相同。候选实际把上述7条WMMA从wait0后移到了wait1之后、wait0之前；wait0仍守最后B片。只改变顺序而未增算术/内存工作。

不要将这些LLVM21数字与正式COMGR基线188VGPR/25SGPR直接相减。正式版本须先fresh baseline whole SHA复现，再核候选真正窗口及非目标导出不变。详[CPU同编译器表](cpu-isa.json)。

工作根 `/home/lmxxf/work/dlss5-gap-phase3-20261023/C/`：`baseline.generated.hip`、`candidate.generated.hip`，亦归档[generated/](generated/)。模块recipe为`c512-m32-mh`，空extra opts；仍以实际生产二进制匹配为裁判，不只信recipe字段。`prepare.py`、`isa-check.py`、`check-module.py`及`source-identity.json`保复现入口。

## 没有另做哪些猜测

- **不扫D1/2/3/5。** 10-01 D4+sched已采纳，是为修复旧U4每迭代排空；D4无sched曾spill。10-14记录的D3/2/1是VGPR183/179/171静态审计，仍同档且更浅。本轮没有新的跨迭代依赖证据支持重做这些深度组合，也不把静态审计冒充三轮GPU性能。
- **不重做HOIST15。** 输入staging、FFN残差、Up low/skip的无分支提前加载已开，旧“load后立即select导致wait0”的问题并非现状。
- **不动小通道全局调度/寄存器封顶。** C128/C256接近高寄存器区，预读更多片段会延长活跃区；现局部wait都对应真实RAW，没有证据可安全直接放宽。
- **不另做Q-scale预读。** C512 scalar scale在`0xFC0E0`发出，到`0xFC51C`才第一次使用，中间约178条指令；已存在隐藏空间。其较早kmcnt等待还涉及其它scalar地址依赖，本轮未证明追加提前读取能缩短关键路径。
- ACO有partialwait，但当前HIP也有；双方阶段/布局/编译器不同，不能把ACO的37当目标常数硬搬。

此候选最多是每个C512实例尾部一次的小窗口；最后load可能在旧wait执行前已经完成，因此**不能仅凭移动7条WMMA预记任何毫秒收益**。若正式ISA不成立或三档ABBA不赚，按一个候选STOP，不扩扫。

## 正式COMGR最终签审：PASS，可进Gold/ABBA

fresh empty-opts baseline整模块SHA `9afa918bc6252ab1beee5bcd15ed6ab838c612cc8368d814b48e28b67a7079b4` 与生产完全同；候选 `760474627ec65c7d3be58b7a8e5aa9cf976665cfeaeb96905ba868a179989265`。

113个导出只有 `c512_qkv_attention_compact_w16` 代码变化，其余112函数原始机器码逐字节同；全部kernel descriptor资源/浮点字段相同。目标双方都是188VGPR/25SGPR/6144B LDS/零private，18084bytes、2483指令、404WMMA、168VMEM load、136wait，完整opcode计数字典也相同。

实际7条独立WMMA已移动到wait1之后，wait0从`0xFBF74`后移到`0xFBFAC`；依赖最后load的B128:129两条WMMA仍紧跟wait0。没有提前读取未就绪数据，没有增加/删除数学工作。末两K16按源码分组重排，资源/工作量不变不代表GPU必有收益。

正式证据：[formal-identity.json](formal-identity.json)、[comgr-isa.json](comgr-isa.json)、[formal-tail-window.json](formal-tail-window.json)。root随后执行同刀1host、仅换一个模块的Gold/三档三轮ABBA；本代理没有运行GPU。

## GPU结题：实际窗口移了，未形成稳定整网收益 STOP

正式COMGR baseline wholeSHA9afa918b…079b4复现生产；113函数仅目标W16代码改变，其余112原始代码同，所有资源descriptor同。188VGPR/25SGPR/6144LDS/private0、18084B、2483指令、404WMMA、168VMEMload、136wait与全opcode计数保持；7条独立WMMA确从wait0后移至wait1后/wait0前，最后两条依赖仍由wait0保护。

三档三图样双臂各3帧，54帧Gold全同。每档3轮4-slot ABBA，240弃80/960样本每arm。第三轮采集期间root在目标机准备后续对账文件，存在额外CPU/I/O干扰；在读取steady/pooled CSV之前记录round3-exclusion.json，原三档r3全部保留但排除，另仅补一轮安静r3。前两轮保持，之后不按数值刷轮。统计用analyze-frame.py --replacement-root evidence/quiet --replace-round 3，原r3不混入。

|档位|baseline ms|候选ms|差ms|pooled p99差ms|
|---|---:|---:|---:|---:|
|900|6.455125|6.459201|+0.004076|+0.010880|
|1088|8.847161|8.842599|−0.004563|−0.024269|
|1152|9.143506|9.144294|+0.000788|+0.001133|

轮次方向不稳定，变化与控制漂移相比很小；1088R1 p99还+40.309µs。STOP，不合生产，不继续改s30/s31或扫调度深度。保留1088小正值，不宣称所有形状更慢；也不能由零净收益证明这条wait永远不stall。此次只验证这个末尾窗口，主循环已经有partial waits，不能把31个真实RAW零wait写成全面串行病灶。

同一phase2刀1final-frame宿主，两模块目录只替换c512-m32-mh，另39模块原SHA；无新分配/发射/运行时开关。原CSV/执行命令/Gold/正式ISA/全部旧r3和安静替代轮在evidence，统计statistics.json。GPU锁已释放，生产/游戏/发行包未改。
