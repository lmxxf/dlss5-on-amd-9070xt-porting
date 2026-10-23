# 当前生产1x对锁定旧Mochi：最终同批刷新

2026-10-23，三项实验结束后用实际生产（仅第二阶段刀1资格改动；A/B/C均未采纳）刷新，取代“约0.45ms”的跨批估计。

1920×1088处理区/1080有效区/640tokens，full71/MP1/FAST1/Style1/seed0/AE0/historyoff，原half-exact encoded gradient；无D3D bridge pulse。三轮NMMN，各槽80warm+160timed，每臂960帧。HIP外层GPU event、M逐帧submit/fence与原10-06协议保持，CPU墙钟单列。

|轮次|生产HIP GPU ms|旧M GPU ms|差ms|
|---|---:|---:|---:|
|1|8.353719|7.882575|0.471143|
|2|8.368744|7.891103|0.477641|
|3|8.378263|7.891153|0.487110|
|合并|**8.366908**|**7.888277**|**0.478631（HIP耗时高6.0676%）**|

- 本地final-pure.exe SHA543a4b6a…f6a82f63；生产头deb120d5…dcf6a34；原40模块逐SHA锁同，不含任何第三阶段候选。
- 对手为d1185d2/v0.0.2.5，原exe/model与68项相关资产核同，M资产树前后未变。
- 输入valid17b2e09b…、processing1088 3ef42d34…；H全部首尾153ac…47f1，M全部624d…a612，与此前已验有限样本一致。
- M只有每槽聚合GPU总值，没有逐帧/p99，不造其尾分布。
- 不能用本轮H绝对值减其它批次H说成精确收益，也不能拿新版a9作者5.70ms直接相减。新版ACO尚未在本机同口径运行。

原命令/CSV/hash/driver在evidence/，statistics.json与analyze.py可复算，final-review.md独立通过。GPU锁释放，游戏和发行包未动。
