# 第二阶段：三刀实测结题

2026-10-23。按刀1→刀2→刀3顺序实施、验Gold、完整Frame三档各3轮ABBA并结题。**收刀1；刀2、刀3止负。** 最终生产代码仅修改 `Development/HIP/hip_reference_network.h` 两处compact资格，原HIP算子和80模块不变。

## 实测结果

下表为各刀自己同批对照的完整Frame稳态耗时差，负数为改善。不是纯NN event，也不跨刀拼绝对值。

|手术刀|900（1600×960）|1088（1920×1088）|1152（1920×1152）|裁决|
|---|---:|---:|---:|---|
|1 compact/W16/byte资格|**−0.070960ms**|**−0.210362ms**|−0.006740ms，中性|**采纳1088/900**|
|2 C32 FMA_MIX|−0.007787ms，不稳定|+0.010902ms|−0.000474ms，近零|STOP|
|3 C512 projection N-pair|+0.000909ms，近零|+0.006152ms，三轮均退|−0.011935ms，有尾反例/漂移|STOP|

每刀三档，每档3轮4-slot ABBA，240帧/slot丢前80，共960稳态样本/arm/tier；三刀共108性能槽。每次只读首尾，正确性另作逐帧检查。全量轮次及p99/漂移保留，未刷掉不利轮。

## 刀1落地

- 原1920×1152行为保持；新增 **Original-only 1920×1088、1600×960**。Small新几何明确排除，保原设备/FAST1/冷MP1/graph/temporal/selector/稀疏及成对导出资格。
- 900/1088每轮均值及p99均改善；1152原路径不变，不将6.74µs小差叫新增收益，R2/R3 p99正差保留。
- 实际派发1088 **161→153**、900 **165→157**，各少8次pack；1152仍153。
- 权重分配容量在1088/900各少 **120MiB**：423438120→297609000B、432883752→307054632B。来自对应Frame stderr，非进程驻留峰值。
- 54帧三档三输入完整Frame Gold＋最终产物9帧Gold通过；144帧兼容覆盖FAST0、冷MP2、1→2→1热切及四类缺导出API故障注入。缺导出注入不冒实际删二进制；热MP2保持既有compact句柄，与冷启动关闭区别记录。

[刀1报告](knife1/README.md) · [统计](knife1/statistics.json) · [独立读审](knife1/final-review.md)

## 刀2为何不收

MIX与ALT两个源码变体完整.text相同，LLVM归一化了操作数交换；只测一份MIX。首级两个精确half常量用一个SGPR承载，变量和结果仍F32，Gold54帧同。代码缩短64–72B，但VOPD/VALU/寄存器/WMMA/wait没有减少；1088均值和p99反退，1152近零。因此没有把源代码“看着更聪明”当收益。

[刀2报告](knife2/README.md) · [统计](knife2/statistics.json) · [独立读审](knife2/final-review.md)

## 刀3为何不收

正式COMGR基线whole SHA同生产，新模块旧89函数代码/资源全同，仅新增N-pair两函数。VGPR100→80、LDS1024/零spill；66帧Gold包含普通F32skip与实际旧模块缺导出回退。性能同host/同candidate模块，只切实验NR0/1，每槽实际3856次N-pair命中（含准备帧）已核。

但旧W16本来已A复用4个输出片，N-pair没有减总读取量，只改配对方向、循环和活跃期。1088三轮均值都退，900近零；1152小正值不抹，但没有带来目标档收益，且R2 p99退。停止这个具体实现，不推广成所有N-pair无用。

[刀3报告](knife3/README.md) · [统计](knife3/statistics.json) · [独立读审](knife3/final-review.md)

## 产物、复现与状态

- 生产变更：仅刀1两条件。刀2/3仅存在隔离实验源码及回执，失败总账、DevHistory、WorkingPlan均已更新。
- 大型构建/原始raw在本地 `/home/lmxxf/work/dlss5-gap-phase2-20261023/` 与远端 `D:\DLSSNR-Lab\gap-phase2-20261023`；仓库保源码/补丁、命令、CSV、hash、ISA和审计，未把模型权重/raw入库。
- GPU与COMGR全程game-check、盘余量≥100GB、独占gpu.lock、15秒看门狗；未杀游戏、未改驱动、未更换已装游戏或0.44发行包。
- 三刀候选分别冻结，不把上一刀失败实现带入下一刀。所有性能基线含刀1时明确记录；后续绝对帧时不同不算跨批回归。
- `run-jobs.ps1`拒覆盖已有case。完整Frame复算用 `analyze-frame.py`；兼容/身份复核脚本在各刀目录。
- 正式addon/runtime最终构建回执见 [final-build/receipt.json](final-build/receipt.json)，二进制位于 `/home/lmxxf/work/dlss5-gap-phase2-20261023/final-payload/`；只构建，不部署/发布。

已知实验流程修正：刀2初次门禁误匹配系统音频trace-agent，在GPU前停止后按PID路径收窄名字；刀1分析脚本把注入标记拼错，独立读审修成真实GAP_GETFUNCTION_INJECT并重算JSON。原始日志未变，未因此重跑GPU。

最终addon/runtime均构建通过，详构建回执；本轮未新增最终DLL API smoke，GPU证据为前述完整Frame与兼容harness。最终锁状态见 `final-gpu-state.log`。
