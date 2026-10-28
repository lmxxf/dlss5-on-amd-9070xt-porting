# 2026-10-28 backfeed 刀1

基线 HEAD 965c47c5（完整 v0.45），模块未重编。每slot独立进程，串行3轮ABBA，240弃80，960样本/arm；NET0、edges_only1。详细原始数据见 evidence，统计见 tier-statistics.json。

- Small900：4.191973 → 4.104375 ms，−87.598µs（−2.090%），p99−79.872µs，三轮mean/p99全部改善。
- Small720：2.903172 → 2.903577 ms，+0.405µs，p99+15.896µs；STOP，默认不扩围。
- Small free 896×512 / 128token：1.915961 → 1.833996 ms，−81.966µs（−4.278%），p99−90.334µs，三轮mean/p99全部改善。此值是C256融合+400桶/V-fragment组合，不能归因单项。

六档标准Frame和小窗两臂Gold均0diff、invalid0。独立裸网trace证明900/720均87→83派发，1080保持83；trace两臂RGB逐位同。小窗Frame日志确认processing896×512，V-fragment producers/consumers均32。

默认仅放Small900 C256和Small896×512组合；其它未测自由几何不泛化。NR_BACKFEED_SMALL_C256 / NR_BACKFEED_VIT400显式0回退、1用于隔离实验。Original默认路由不变。Small720负结果保留。

首轮trace任务沿用knife2标签但引用knife1输出目录，candidate因参考文件不存在退出；更正标签后重跑通过，未视为数值失败。旧失败在远端knife2目录保留。最终默认七档21帧回归全部逐位一致、invalid0；GPU锁释放。未部署游戏、未改0.45 ZIP。
