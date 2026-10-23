# 第三阶段：动态供数与跨核依赖，实测结题

2026-10-23。按A→B→C完成实现、正式ISA核验、Gold与三档3轮ABBA。**三项均止负，生产仍仅保留第二阶段刀1。**

## 当前真实剩差

同1088/640token、当前生产全71块，最后另做三轮同批纯NN对账：**HIP 8.366908ms，锁定旧Mochi 7.888277ms，剩差0.478631ms（6.0676%）**。这取代跨批估算的约0.45ms；M仍为d1185d2/v0.0.2.5，新a9 ACO作者数据不混进此表。

[最终差距报告](current-gap/README.md) · [原统计](current-gap/statistics.json) · [独立复核](current-gap/final-review.md)

## 三项整网结果

以下是各自同批完整NativeGameFrame稳态耗时差（候选−基线）；不是上述纯NN GPU时间，不能跨表相加。每档3轮4-slot ABBA，240帧/slot弃80，960稳态样本/arm。

|候选|900差ms|1088差ms|1152差ms|裁决|
|---|---:|---:|---:|---|
|A ViT QT32共享K/V|+0.012039|**+0.097277**|+0.090150|STOP|
|B C512单边tile-counter|+0.086410|**+0.124866**|+0.052815|STOP|
|C 尾部独立WMMA移至等待前|+0.004076|−0.004563|+0.000788|不稳定，STOP|

### A：确实减少读取请求，仍未赚

旧10-01 M32也曾19 SAME，不能归为仅有损。本次保当前FAST1数学，真实半grid，400尾编译期只算一链；有用WMMA保持，K/V逻辑请求400减少48%、640减少50%，不是DRAM计数。正式98VGPR/0LDS；旧89函数身份保持。60帧主Gold/回退通过；1088/1152三轮mean和p99全退。没有继续扫QT32变体。

[A报告](A/README.md) · [独立复核](A/final-review.md)

### B：确有计算重叠，但协议净成本更高

16条C512 FFN→projection边，64bit累计target无需每帧清零，有界acquire；候选持有/释放缓冲的成本计入，NR0不人为加同样工作。独立超时probe验证完整零写+canary，完整Frame强制故障最后status1/exit1拒整批；这不是生产正确回退。32位进位与64位极限关闭通过。

三档ordered trace均overlap0，AnyOrder三档16/16边都观察到consumer真WMMA完成早于末producer发布。主测关clock，84帧主Gold/诊断通过；九轮mean及p99全退。不能把重叠数量直接当收益，也未单独分摊atomic、fence、缓存和hold成本。

两处工程修正完整保留：producer实际生产empty-opts，而非脚本可选max-ilp（fresh整SHA已复现，错误组未上GPU）；首个候选afterwait缺池被保护拒绝，只修有界冷预热稳定池，不撤保护、不清counter/status。实际warm4/5次，性能按实际调用计数核对。

[B报告](B/README.md) · [独立复核](B/final-review.md)

### C：已有partial wait，只找到一处小窗口

C512两处、C128十一处、C256十八处wait0均有真实RAW，不能直接降低阈值。只将C512 W16最后7条独立WMMA移到wait0前，最后两条仍受等待保护。正式113函数仅目标顺序变，资源、大小、全部指令及操作数multiset相同；54帧Gold通过，整网未形成稳定收益。

C原r3期间root在目标机准备后续对账文件，存在额外CPU/I/O干扰；steady/pooled统计前声明排除，全部原始轮次保留，补一轮quiet r3。有效结果只用原r1/r2+quiet r3，不按数值刷轮。

[C报告](C/README.md) · [排除记录](C/round3-exclusion.json) · [独立复核](C/final-review.md)

## 结论与未解释边界

- 已证明A真减少逻辑读取、B真形成有用计算重叠、C真移动安全等待窗口；均未在本轮变成稳定整网收益。不能用结构/ISA漂亮代替ABBA。
- 这些是具体候选的裁决，**尚未将剩余0.479ms逐项因果分摊**，也不支持“差距只可能在访存和跨核排空”的排他判断。数学、布局/实际工作、noise生成位置、完整stage组织和调度仍须按锁定版本分别验证。
- 新版a9 ACO尚未本机同口径运行；不能以作者5.70ms给当前差距归因，更不能将其全部叫编译器收益。
- 未合入A/B/C，未重编/安装新的生产包，未改游戏/驱动。源码中的有效优化仍为第二阶段Original1088/900两资格，已构建宿主仍在phase2 final-payload。

全部GPU/COMGR执行game-check、盘余量≥100GB、原子gpu.lock与15秒看门狗。原始CSV、SHA、命令、失败日志、源码/ISA、trace与独立复核在子目录；大型raw/二进制留 `/home/lmxxf/work/dlss5-gap-phase3-20261023/` 与 `D:\DLSSNR-Lab\gap-phase3-20261023`。DevHistory、WorkingPlan和失败总账已同步。最终锁状态见final-gpu-state.log。
