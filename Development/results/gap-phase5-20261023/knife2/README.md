# Phase5刀2：Small与AE的V-fragment资格（采纳）

只扩大header资格，刀2的GPU源码/AE策略不变。详[source-review.md](source-review.md)。

工作目录 `/home/lmxxf/work/dlss5-gap-phase5-20261023/knife2/`，四host已编译；`build-identity.json`列完整SHA。资格已合生产；最终header与实测候选逐字相同，两个重编Frame也与实测候选整文件相同。

测试清单：`gold-jobs.json`（24jobs/72帧）、`ae-jobs.json`（14jobs/84帧）、`ae-default-idle-jobs.json`（4jobs/24帧）、`perf-jobs.json`（48slots，四档各3ABBA）。schema沿phase4 runner，三类常规任务都不能拿ALLDONE代替成对数值复核。

本地后处理：

```bash
python3 analyze-correctness.py --kind gold --evidence EVIDENCE --output gold-results.json
python3 analyze-correctness.py --kind ae --evidence EVIDENCE --output ae-results.json
python3 analyze-default-idle.py --evidence EVIDENCE --output default-idle-results.json
python3 analyze-perf.py --evidence EVIDENCE --output perf-results.json
```

AE逐帧跨臂比较并读实际state，不假设全帧相同；route_calls只是launch数。主AE组idle86400000仅为受控验证，另有不设置idle覆盖的default500ms观察组；生产阈值不动。性能固定AE0，不拿复用帧白赚时间。四profile独立，不能把Small1152与free1088因同n640合并。

准备时module路径为phase5 baseline/modules。**该目录不可覆盖**；root将两臂统一转到刀1已采纳pair独立目录并刷新40hash。转换后勿重跑make-jobs.py覆盖新锁。所有脚本只已完成CPU编译/语法/范围检查，没有执行这些GPU jobs。

依据：`candidate.patch`、`source-identity.json`、`source-review-check.json`（407组真实helper CPU检查）、`mapping-review.json`（288/400/448/640全字节/尾/对齐/canary）、`build-host.sh`、`make-jobs.py`。三个分析器仅语法检查通过，尚无本轮GPU结果。

## 实机进展

两臂实际统一使用刀1已采纳pair-only模块（清单40SHA锁定，phase5 baseline目录不变）。Small四档三图样72帧跨臂Gold通过，实际31块/处理尺寸/新成对路由均核实。受控AE1七档84帧逐帧SHA与完整AE CSV全同，每档实际计算/复用均覆盖。默认idle及AE2/3继续验证，四档性能尚未判定。

## AE2/3最小补充（准备完成，待root当前perf结束后串行执行）

`ae-mode23-jobs.json`从当前已锁pair模块的ae-jobs直接派生，Original1088与Small720各mode2/3双臂6帧，共8jobs/48帧；新label不覆盖旧case。受控idle86400000仅测试配置，生产500ms未改。

```bash
python3 analyze-mode23.py --evidence EVIDENCE --output ae-mode23-results.json
```

mode2必须由实际最后6行CSV证明全compute，mode3必须同时实际compute/reuse；两臂逐帧输出SHA及完整AECSV比较，不能用mode1或default-idle结果代替。route_calls仍仅launch数。准备身份见`mode23-preparation.json`，没有执行GPU/SSH。

## 实机闭环与采纳

Small四档三图样72帧Gold，受控AE1七档84帧，默认500ms两档24帧，AE2/3两档48帧，共228帧跨臂逐位一致。AE1实际计算/复用覆盖；AE2按原策略全计算，AE3按原策略计算/复用，所有配对AE CSV完全一致。没有修改AE策略，未把AE0性能当复用帧收益。

|Small处理尺寸|基线Frame ms|扩围Frame ms|节省µs|
|---|---:|---:|---:|
|1600×1024 / 900|4.268004|4.244956|23.049|
|1344×768 / 720|2.994176|2.989513|4.663|
|1920×1152 / 常规1080|5.559334|5.499693|59.642|
|1920×1088 / free1072输入|5.382278|5.317102|65.176|

每档3轮4-slot ABBA，240弃80，每臂960样本。11/12轮mean改善、12/12轮p99改善；720R2mean+2.378µs保留，该档收益微小。两臂均使用本阶段已采纳C512 pair模块。资格只覆盖既有FAST1、单次网络计算、对应geometry/token/layout，MP2/3、graph、缺配对exports等继续回退；AE1/2/3保持原数学与anchor。

最终生产合入/默认与公开关闭回归及完整构建另见最终回执，本文不将初始CPU准备记录当GPU证据。

最终9组36帧生产默认/on/off全部跨臂raw与AECSV一致，actual路由核对通过；两个final Frame整SHA复现实测候选，最终GPU模块用正式产物。见final-default-review.json与production-identity.json，完整宿主构建见../final-build。
