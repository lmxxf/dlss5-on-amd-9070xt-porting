# C32 专项优化 Phase5（2026-10-29）

本轮**没有最终生产赢家，精确恢复ba057a5d原宿主路由和80模块**。Small1080 norm-hoist资格批曾三轮mean/p99均赢（−42.863/−43.602µs，C32时钟−45.753µs），但独立集成批合并p99退24.228µs（R1退65.243µs），按同一门禁撤销；没有追加轮次救回。

|独立刀|提交|结论|
|---|---|---|
|1：OutProj共享/双qt批处理|bc8cb62a|1A尾部不稳、1B四档各三轮mean全退，STOP|
|2：norm-hoist五档|3b7d0a03|资格只收Small1080；最终集成R1尾部反例，全部撤销|
|3：finish→pool融合|0cc4a90a|134VGPR/0spill，但首个Original1152 Gold不符，STOP，不跑错误候选性能|
|4：Post寄存器尾部|8b8dbb7b|145VGPR/0spill/七档Gold同；Post单核慢43/46µs，四档Frame全退，STOP|

逐刀源码、ISA、资源、每轮与失败证据分别见 [刀1](knife1-README.md)、[刀2](knife2-README.md)、[刀3](knife3-README.md)、[刀4](knife4-README.md)。刀3没有通过Gold，不能归入“所有候选逐位一致”；刀2资格结论也被独立集成反例撤销，最终交付不包含本轮优化改动。

基线为start.json锁定的ba057a5d；各独立候选从当时已接受生产状态构建，失败候选不叠加；刀2在最终集成失败后撤销，恢复起始生产。重现实验脚本应在对应基线快照执行，而不是把旧候选反复应用到已修改源码。

首次集成批及NMMN作为失败候选证据保留在integration/evidence、integration-nmmn/evidence与integration-statistics.json。恢复基线后的final批不是追加轮次救候选，不用于重新采纳已否决扩围。

## 最终正式构建与Gold

gfx1200/gfx1201 共80模块**实际重编**：12个LLVM23预编译行（正式RowOpts、-real-true16）+68个当前驱动COMGR目标。COMGR默认空opts，仅gfx1200 c512-m32-deep沿用已验max-ilp，gfx1201保持空。所有ELF架构、完整函数代码和metadata与锁定生产比较通过；本轮无获胜的核源码改动，全部函数代码/资源保持原生产身份。80个容器SHA也全部与锁定基线一致，hip/SHA256SUMS与隔离payload/HIP读回复核通过。正式O2 addon/runtime/frame（标准和小窗）/pure已构建，运行时smoke成功；首次隔离smoke缺codec HLSL资源的退出已留档，补齐payload/shaders后最终测试通过。gfx1200完成构建/ELF/代码核验，GPU数值/性能实测仅gfx1201。

七档Gold（两臂×3帧，共42帧）保持固定基线首尾0 bit-diff、invalid=0，包含Original1152的0900E489…0F4A和Small1080的65D71486…8FCB8，六档固定SHA在manifest，小窗另与本轮初始旧宿主SHA复核（final-gold-reference.json），原始SHA在raw-hashes；不以摘要代替完整比较。

## 最终七档Frame三轮ABBA

几何命名：original960是900档1600×960；Small1080是1920×1152、Small900是1600×1024、Small720是1344×768、小窗是896×512。A为本轮开始的生产宿主/80模块，B为撤销候选后正式重编的原生产宿主/模块；标准Frame二进制SHA恢复32340844…，两臂实际代码相同。每slot独立进程，严格串行，240帧弃前80；每档每臂960稳态样本，edges_only=1/NET0。以下是这个独立收口批次，不能与资格批相减推导优化增量；未改档位的小幅变化视为噪声，不包装成收益。

|档位|A ms|最终B ms|mean Δµs|p99 Δµs|
|---|---:|---:|---:|---:|
|original1152|9.024527|9.017091|-7.437|-30.595|
|original1088|8.741901|8.754795|+12.894|+14.241|
|original960|6.413017|6.412467|-0.550|+21.367|
|small1080|5.457850|5.453888|-3.962|-21.397|
|small900|4.158904|4.154525|-4.380|-10.800|
|small720|2.921801|2.920052|-1.750|+3.174|
|smallwindow|1.890662|1.890161|-0.501|-0.361|

## 最新生产 vs 新版v0.0.8 ACO：同批纯NN NMMN

每slot80 warm+160测量，三轮NMMN，每臂960稳态帧。M仍使用已验证的v0.0.8常规模型预编译ACO，非EXPERIMENTAL/INT4；pipeline-binary正常导入，未绕驱动身份检查、未改驱动。两边同模型来源/processing几何/正常精度配置，分别固定各自原始输出SHA；不声称HIP与ACO输出像素逐位相同，也不把GPU NN时间与Frame墙钟混比。M仅导出160帧GPU总计，不能编造其逐帧p99。

|模型/档位|HIP NN ms|v008 ACO NN ms|差ms|HIP慢%|
|---|---:|---:|---:|---:|
|original1088|8.299856|5.694142|2.605714|45.76%|
|small1080|5.015832|3.451845|1.563987|45.31%|
|small900|3.863329|2.639966|1.223364|46.34%|

## 算力缺口判断与边界

仍不能宣称接近硬件上限。本轮的norm-hoist资格批有约43µs收益，但独立集成未复现稳定尾收益，因此不能称为生产收益。所有手术的失败只约束这些实现，不能证明C++、LLVM或硬件已无空间。Post减少LDS却因新的循环/lane组织变慢，权重静态加载减少也不等于cache miss或显存事务同比下降。runtime最大驻留16 blocks/multiprocessor是上限，不是active waves/计算利用率。没有硬件counter/带宽roofline证据，就不把剩余差距全归因寄存器分配、wait或宣称.s是唯一路径。新版ACO与我方没有跨实现逐位一致合同，观测差距也不保证能在我方0bit-diff约束下全部追回。

硬件逐核时钟继续采用gfx1201 MSG_RTN_GET_REALTIME（设备属性100000kHz、100MHz）及80ticks邻接stamp校正；单核样本轮转完整dispatch序列，保持无event的生产默认快路径。本轮诊断pure小窗实际91dispatch，按实际序列校验，不能硬套83。最终NMMN是新鲜总时间对账，不把此前跨批ACO per-pass值冒充本轮重测；C32资格批的−45.753µs只作局部变化记录，不抵消最终集成的尾部反例；剩余差距必须继续由同口径热点实验验证。

## 护栏、复现与清理

所有远端GPU/RTC统一game-check、进程检查、D≥100GB、CreateNew独占gpu.lock与15秒游戏看门狗，slot严格串行。失败与资源门候选均隔离，不进入下刀基线。所有指令表是静态计数，不当成动态事务/时间。最终大临时输入/输出、候选模块、编译中间文件与缓存已清理；保留正式隔离payload，Git只存脚本/账本/报告/摘录/源码diff与证据索引。未部署、未改游戏或0.45 ZIP、未动外层仓、锁版d1185树及v008原始源码未变；无amend/Co-Authored-By/push。

关键回执：full-rebuild-verification.json、full-code-comparison.json、final-build/hosts.sha256、final-statistics.json、final-nmmn-statistics.json、各阶段*-evidence-index.json、final-cleanup.json、final-verification.json。脚本prepare-final.py/build-final.sh/prepare-final-tests.py分别覆盖模块、正式宿主与收口测试。
