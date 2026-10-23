# Small compact：独立数值与身份复核

2026-10-23，**PASS**。仅本地读取已收集文件，未使用SSH/目标机/GPU工具，未读取或改动正在运行的性能作业。复算脚本`review-gold-local.py`，结构化结果`gold-review.json`。

| 证据 | 独立检查结果 |
|---|---|
| 完整Frame Gold | 4几何×3输入pattern×2臂×3帧＝**72帧**；逐帧hash成对相同，checked=1、invalid=0 |
| 实际派发trace | 8进程×3帧＝**24份F32输出**；同几何两臂逐帧hash相同，candidate日志每帧raw_bitdiff=0、finite/repeat通过 |
| 公共Frame兼容 | 7对/14进程/**108帧**；模式回环、AE决策CSV及MODE1序列成对相同 |

## 真正执行的范围

从原trace stdout重新解析，未依赖case名字推断：

| 实际Small处理区 / ViT | 派发数旧→新 | vit_pack旧→新 | active weight容量节省 |
|---|---:|---:|---:|
|1600×1024 / 448|95→87|8→0|125829120B（120MiB）|
|1344×768 / 288|95→87|8→0|120MiB|
|1920×1088 / 640（free输入1072）|91→83|8→0|120MiB|
|1920×1152 / 640（已有范围）|83→83|0→0|0|

四档全部actual requested/effective Small、31块；新三档8组compact contract/projection与W16、bstream/bout均启用，Up39实际为`decoder_project2x_h16w_w_bin_f8w16`（byte输入、F32输出）。448仍运行动态tokens448的640上限attention，288运行tokens288的400上限attention，未把token数冒成400/640。所有trace每帧VF producer/consumer **delta=0**。

120MiB是当前模型weight allocation容量差；各对pool数量/容量相同。它不是整个进程或双模型总驻留峰值。

## VF累计值与历史修正

- Small1152公共Frame构造会预热一次Original，汇总VF producer/consumer各8属于该Original预热；原model_prepared及71块日志可对应。Small真实帧没有使用VF。standalone Small trace没有这次Bridge预热，VF累计为0同样正确。
- 首个Small1080 baseline进程exit0，但runner错要求VF全程0；正式72帧采用`-checked`重跑，旧记录保留、不重复计入。
- free1088没有1152的Original预热。首baseline进程exit0、三帧raw完好，误门只做CPU回执修正，没有再刷GPU。

## 公共兼容边界

三组0.5→1→2→3→0.5回环分别证实900的1024↔960、720的1344↔1280、free1072的1088↔1152真实几何重建；重复同模式帧及回Small输出保持。它是公共Frame模型请求/重建路径，不扩大低层同geometry Network setter保证。

AE1三档实际发生reuse，七行决策/统计CSV两臂完全相同。MODE1在free1072使用Small head/blend0.76708984375，实际history_use为`0,0,1,0,1,1`；首帧864/831ms超过源码固定250ms idle门，使第二帧额外cold reset。帧2/4/5确实用history，帧0/1/3冷图同。此处保真实序列，不硬套理想`0,1,1,0,1,1`，也没放宽任意reset；仅静态fallback、无motion/depth，不宣全动态导引场景。

## 身份

独立重算4个Frame host、2个trace host本地SHA及两冻结header，均与构建回执一致；candidate header仍`09cb03d5…72d734`。不同批job spec的共享文件SHA无冲突，40个运行gfx1201模块逐项与复用80模块manifest对应项相同。runner先校验每个锁文件SHA才运行job，case copy也校SHA；本审计没有重新向目标机取文件。

大raw未再次搬运；逐位结论依据已收集SHA回执、原finite/repeat检查及trace直接bitdiff。header归一化检查已证明只增加三Small shape，VF Small拒绝、其余数学/资格未改。此报告不代性能统计，也不把正收益当数值通过依据。

最终生产核验：生产header整文件逐字节等于已测candidate（SHA `09cb03d5343a78abffa2add3ee7e00545689047ea8dd3f95872465513172d734`），模块manifest仍`3b6471c2…16e1`。源审的三Small范围之外没有新增改动，VF Small/AE拒绝保持。仅本地核验，无目标机操作。
