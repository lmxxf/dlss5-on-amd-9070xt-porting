# 刀1：1088与900 compact资格产品化（采纳）

生产 `hip_reference_network.h` 两处compact初始化已修改：原1920×1152行为保留，新增Original-only 1920×1088 / 1600×960；设备/FAST1/MP1/graph/temporal/selector/sparse及成对export条件全部保留。400token的权重尾地址不依赖token数量，完整读写范围已源审。最终源及产物SHA见 `final-build-receipt.json`，中间全几何candidate保留，不覆盖。

## 完整Frame性能

每档3轮4-slot ABBA，240帧/slot弃80取160，共960帧/arm；原HDR1296×720输入经正式NativeGameFrame，NET0、edges_only1、Style1、FAST1、MP1/AE0/historyoff。不是第一阶段纯NN event口径。

|处理尺寸|旧ms|新ms|差ms|差比例|pooled p99差ms|
|---|---:|---:|---:|---:|---:|
|1600×960|6.548212|6.477252|**−0.070960**|−1.084%|−0.028782|
|1920×1088|9.090362|8.880001|**−0.210362**|−2.314%|−0.181314|
|1920×1152|9.167039|9.160298|−0.006740|−0.074%|−0.130967|

900/1088每轮均值及p99都改善。1152原路径不变，均值小差按中性看；其R2/R3 p99分别+0.043854/+0.035559ms，明确保留，不宣各轮尾都好，不刷轮。性能对象是中间candidate；最终只收窄新增资格为!small_active，Original三档路由同，正式产物另做三档Gold，不把Small收窄称新性能优化。

## Gold与兼容

- 三几何×真实HDR/两种受控图样×baseline/candidate×3逐帧读回，共54帧完整Frame Gold均与成对旧路逐位一致；1152真实HDR仍0900E489…0F4A，1088为8a45a974…46ab，900为5fe1ace6…d475。
- final-frame三档各3帧再次核同，共9帧，实际生产源SHA与final编译快照一致。
- 三几何×default/FAST0/冷MP2/1→2→1热切/缺contract/缺projection/缺bstream/缺bout×双arm×3帧，**144帧兼容trace，24对全输出逐位同**。
- 缺导出是隔离harness在 `hipModuleGetFunction` 边界返回失败的故障注入，真实模块照常加载，不冒称实际删除二进制export。缺bstream/bout时整体字节跨块回退；FAST0/冷MP2 compact关闭。热MP2保既有compact句柄，实际1→2→1回环核同，不把冷启动资格强套热路径。
- 默认trace：1088 161→153；900 165→157；每档减少8次pack。1152仍153。无UNMAPPED，显式SP/anyorder都在计数。
- Small新几何明确排除；gfx1200仍由原device资格回旧。本轮未宣其硬件/Small全链新测，也未宣所有动态输入的数学证明。

原始证据 `evidence/`，统计 `statistics.json`，兼容 `compat-results.json`；源审 `source-review.md`，构建与隔离补丁见本目录。所有GPU操作遵game-check、≥100GB、独占gpu.lock、15秒看门狗；未修改已装游戏、驱动或发行ZIP。**刀1结题，后续刀2以该生产资格为基线。**
