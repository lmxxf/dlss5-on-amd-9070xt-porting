# 第五阶段刀2：V-fragment Small/AE资格独立源审与隔离准备

2026-10-23。**源码/CPU准入PASS，GPU未执行。** 只改隔离header，生产保持原样；刀1实测闭环后由root串行安排本刀。

基线是phase4最终header `09cb03d5…72d734`，候选`8ae0e238…5c80ed28`。只扩大VFRAG资格，不改GPU源码、AE数学、anchor策略、阈值或动态决策。两臂最终应共用root采纳的刀1pair模块独立目录；phase5 baseline/modules不可覆盖。

## 改动与精确范围

- Original既有三档：1600×960/n400、1920×1088/n640、1920×1152/n640。
- Small四档：1344×768/n288、1600×1024/n448、1920×1088/n640（合法free1072输入）、1920×1152/n640。
- shape与实际n成对匹配；移除Small、显式AE0及adaptive_active这三个阻挡条件，保留FAST1、单次网络评估（Original1x/Small0.5x）、graph/skip/dup/observer/dump与原格式条件。
- KS2资格检查改为`n<=400 ? 400 : 640`，与实际导出选择一致；288不能误查640的KS2。若实际KS2存在则整对退旧，不能将新V交给KS2。
- 构造三export预热、缺任一全旧、NR诊断优先和DLSS5开关默认均保持。局部vfrag在producer之前决定，两端共用。
- `vfrag_ae_off`仍用于旧诊断输出，只是构造时是否显式AE0的记录；它不再是候选准入门。不能因字段为0便断言没走新路，也不能因route_calls非0便断言GPU执行了MMA。

`candidate.patch`只有注释与该资格函数变化。直接提取真实helper编译执行`scope-review.cpp`，407个CPU组合通过（包括Small形状/token匹配、AE active开关、FAST0/MP2/graph回退、逐个缺export、NR0和288/448的正确KS2名字）。HasFn是CPU stub，结果不冒实机loader证明。

## reuse_gate与norm生命周期

当前`AdaptiveVitGroup`先按原流程执行image/token stats与`reuse_decide`，得到同一`adaptive_state[0]`。它将`adaptive_active=true`包住8个ViT block；`Run`把同一state指针作为reuse_gate追加到每个kernel的原ABI中。

- 新producer `vit_stream_qkv_frag_bin_w5f8_vfrag`与新attention均在任何输入/输出、LDS barrier之前检查同一gate，非零直接return。
- expand、contract、projection同样保留各自原gate入口，未因本刀更改。
- gate=0：本次QKV完整生成新V布局，随后的匹配attention消费；norm仍本次Vit局部allocation，大小不变，不是持久anchor。
- gate=1：两端都跳过，norm/AV/full可保持旧内容；`reuse_finish`走reuse分支，只读本次input与anchor_in/out/gain，**不读取未写full/norm**，所以不需新增清零。
- `adaptive_active`在发完8块后恢复false，`reuse_finish`正常执行。它在compute路径才写anchor，在reuse路径按原gain估计或直接取anchor；本刀没有修改这一策略。

独立比较baseline/candidate的`AdaptiveVitGroup`、`Run`、`PrepareForExternalInput`、`SetModelMode`函数文本，全部逐字一致；见`source-review-check.json`。GPU新导出是phase4已经构建的同一份代码，不重新编译。正常stream顺序和两端gate ABI未改，不能把不透明的route计数当动态执行数。

## Small288/448尾与内存

沿最终head-major式重跑CPU穷尽`mapping-review.py`，四种n的全部QKV字节、前两段Q/K保护、V逆映射、16B对齐及canary通过：

| n | producer WG×160 | attention WG×32 | QKV总容量B | 全QKV inactive尾wave |
|---:|---:|---:|---:|---:|
|288|384|576|884736|192|
|400|480|800|1228800|0|
|448|576|896|1376256|192|
|640|768|1280|1966080|0|

288/448最后5wave组每head/part有2个inactive wave；producer保持原先参加协作barrier后才live-return，V新store位于其后，不写无效行。复用gate非零则整个WG在任何barrier之前一致退出。attention288选择400导出，448选择640导出，实际循环/平面stride仍使用288/448；query仍16，不复活已STOP的QT32。

## AE默认/“自动”语义

核心未设`DLSS5_VIT_ADAPTIVE`时mode=0；普通game模板当前设1，Magpie/RE9模板设0。用户说的自动AE是现有mode1自动判复用（界面AE/EXACT），不是新增字符串`auto`；原代码仍用数值0..3及原异常处理。候选不改任何模板或用户配置。

F8切EXACT时原策略不更新AE anchor，切回AE由原dirty/reset刷新；本刀只在这两种路径都允许成对V布局。F9模型/几何重建与两模型weight/gain交换保持。Small900/720/free1072的几何变化仍走原公共Frame重建，1152同geometry热切保持原流程。

## 测试与统计边界

已准备，不执行：

- `gold-jobs.json`：Small4档×3pattern×2臂×3帧，72帧。
- `ae-jobs.json`：Original3档＋Small4档，每臂6帧，84帧。受控测试把idle阈值设86400000ms避免读回/coldstart改变决策；**这不是默认500ms全语义验收**，生产阈值未改。
- `ae-default-idle-jobs.json`：Original1088/Small720，各两臂6帧，共24帧，不设idle覆盖。记录实际CSV/输出；wall引起决策差不能直接归因布局错误。
- `perf-jobs.json`：Small4档各3轮ABBA，240帧弃80，每臂每档960稳态样本。固定AE0量完整计算，不冒称AE稳态复用加速。

correctness后处理必须跨臂按帧索引SHA比较；AE不比较所有帧与首帧。实际最后N行AE CSV必须同时出现state0=0（compute）和1（reuse），并比较两臂完整决策/统计；route_calls只作成对launch资格证据。

独立default-idle观察脚本不硬要求CSV相同，保真实wall/状态。性能脚本按四profile分别统计，Small1152与free1088虽然同640token也绝不合并。runner ALLDONE不代替这些后处理。

## CPU产物

standard/free1072两臂O2 host都已构建。baseline整SHA复现phase4最终：standard `39c5e035…617994e`、free `7ea7f8c3…090f10`；candidate分别`2915d38c…cb1def`、`5ee936c2…5f7f25`。完整身份见`build-identity.json`。

job spec在准备时锁324个路径，包括40个gfx1201模块、4host、Small资产和新增184个Original资产期望SHA；Original期望来自已验证本地old-converted，本任务没有向目标机确认它们。运行前HashCheck必须实际通过，缺文件/不同物理存储不能静默忽略。

root将统一把所有manifest的module路径与40hash改到刀1最终pair目录，两臂同套，不覆盖immutable baseline。完成转换后不要无参数重跑生成器把模块锁改回准备值。此候选尚未GPU验收，不可直接合生产。
