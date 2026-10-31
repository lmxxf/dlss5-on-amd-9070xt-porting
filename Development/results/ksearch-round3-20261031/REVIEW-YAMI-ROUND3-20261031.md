# Round 3 独立审查：已测性能门通过；计数账需修，搜索尚未耗尽

审查1005424b。先读生产diff与CSV/JSON，再读报告；没有运行GPU或修改生产、部署、原始数据。

## 已独立确认

从原始out.csv逐槽丢80取160重算：final七档84slot，21/21轮mean和p99均改善；最终Small1080 -61.757µs / p99 -55.697µs。Original1088 -292.672/-221.811µs；Original1152 -245.174/-167.007µs；Original960 -109.002/-86.257µs；Small900 -108.985/-149.887µs；Small720 -120.305/-225.340µs；Smallwindow -170.185/-169.186µs。与JSON一致。

final-pure-abba的36个timing.csv独立复算：Orig1088 -85.329/-63.941µs，Small1080 -72.608/-50.211µs，Small900 -67.103/-93.655µs；三档每轮mean/p99均赢。

r3a相对b1af的四档增量48slot也复算一致：Orig1088 -71.665µs、Orig960 -71.646µs、Small1080 -102.729µs、Small900 -43.690µs。Small1080增量首轮p99有反例，不能把“12/12轮mean全胜”改写成此增量批每轮尾都赢；最终批全胜不抹历史。

80个实际HSACO逐SHA匹配hip/SHA256SUMS。已查看ones-WMMA run-v2日志：8个fixture有16个不同query列，C32/MH denominator、inverse fast/exact、probability fast/exact全diff0、invalid0。多配置两臂raw-hashes按文件名逐一相等，实际68对条目（见下文）。

因此承认1005424b在已测范围的性能晋升；本轮不再把Small1080性能门作为阻断。以下问题不抹掉已证收益。

## 1. P2：窄2×2探针交互项符号反了

355定义 I=(Base-P0)+(Base-P1)-(Base-P2)=Base-P0-P1+P2。
独立读取probes_sm/evidence各timing.csv得到：

|档位|按355定义I，µs|报告/JSON|
|---|---:|---:|
|Original1088|+153.703|-153.703|
|Small1080|+32.457|-32.457|
|Small900|+23.651|-23.651|

Round2粗探针采用的是355定义；同一张新表混了相反符号。修生成上游statistics的公式，保原CSV，不只是改Markdown。该错误影响相互作用解释，不改变P0/P1各臂均值，也不改变R3-A收益。

## 2. P2：compute-dynamic-rcp.py不是实际dispatch计数器，“精确514万/2649万”尚不成立

脚本硬编码Post70 shift=0，但实际profile/production_options.generated.h设o.post_shift=3；hip_reference_network.h:1110按W+2*sx、H+2*sy计算post窗口。

仅Original1088的post：脚本1920*1088/64=32640窗口；shift3实际1928*1096/64=33017，少377窗口。按脚本自己的每wave 4次qt、每qt省7rcp公式，仅这项少算10556条wave级rcp；按62条cmp/cnd公式少93496条，尚未处理边界early-return。

脚本还把SP各层任务总量叫wgs，并凭预设层表/shift生成执行量；这可以作为解析模型，但必须与真实grid、任务完成数、每个核的边界退出/循环谓词对上，不能称计数器实测。它也从r3b/r3c以外的固定模板推各档，需核SpEnabled的实际资格，不应一概假定C256都SP。

修法：读取实跑参数与dispatch/task记录，区分launch waves、有效body waves和lane有效数；脚本加针对post_shift、SP回收不执行、分辨率/模型的断言。数字未闭合前降格为估算，删除“全部由原始CSV/ELF直接生成”的过宽说法。脚本里的Q/K normalization倒数注释也应核ISA：v_s_rcp的用途不能仅凭同核存在猜。

## 3. P2：多配置“62文件”计数不符；AE决策覆盖还缺直接证据

multi_reg-summary.json的files_checked求和及原始两臂raw-hashes按name配对都是68，不是62。这里是68对比较项，且first/frame0/final存在重复输出，不能称68个独立帧。

AE1/AE3配置确实写入flags，Small AE初始化日志存在；但归档未给adaptive决策CSV或compute/reuse计数。固定图像输出相同不能区分“复用成功”和“每帧全算成功”。这四组out.csv还均为reset=1，benchmark_live_capture.cpp:54在temporal=0时确实每帧传reset；不能仅靠配置名推真实跨帧复用覆盖，也不能仅据reset断言AE必失效（需追实际AE状态）。

应补最小计算/复用/失效回到计算的状态序列与两臂决策匹配，或收窄为“AE配置下输出回归”，不要写“全配置全部闭环”。MODE1输出序列确有变化，F9输出确有模型/多遍变化，这些证据应保留。

## 4. P2：R3-A改的是整个cw_clampf，不限于Softmax

生产只在build recipe打开既有CW_FMED3_CLAMP，cw_clampf所有调用点均变：Softmax、输入/残差±448裁剪，以及部分输出0..1裁剪等。有限普通输入上的clamp等价理由合理，已测Gold通过；但“收益全部来自Softmax32次clamp”仍需实际热点ISA路径支持。

若声明所有位型逐位等价，补正式recipe下旧clamp与fmed3的定点原语：三个实际区间、阈值相邻ULP、±0、±Inf、NaN约定。无需重新全网穷举；也不要把ones-WMMA原语当成clamp原语。SGPR下降是已读出的资源变化，不等于“SGPR阻塞被证明消除”或驻留因此增加。

## 5. 搜索策略：保留胜出组合，但不要把R2-D已证明为Small1080最佳组成

s1080_4arm在K5固定时：R2-D C32 +27.193/+14.359µs；MH +25.299/+3.646µs；Both +22.153/+44.356µs（mean/p99）。这是实测负结果，最终组合打赢旧8a9c并不能推翻它；R3-A改变编译排程后，交互可能改变，需要新的控制变量而非故事解释。

下一条高价值候选是固定1005424b的R3-A+K5，重新隔离R2-D C32/MH开关。若去掉某部分更快且Gold/尾门通过，按shape采纳；若不赢，保现在组合。不是重刷旧实验，而是新clamp基线上的成分消融。

同时把“C32指数/概率量化剩余机制”列Open n=0：R3-A只处理了clamp，P0还含概率转换/打包。先刷新1005424b上的窄探针，再按结果选择有语义证明的成对转换/位映射合并；不要算518-72当可回收预算。

R3-B单wave直接读的负结果可封存，不必重复。R3-C 238VGPR未兑现收益也可封存；它们不构成softmax父机制已经搜索完的证据。

## 审查范围

独立重算final、r3a、pure ABBA共168slot；窄探针三档原始timing.csv；80模块哈希；11组两臂68个文件名/hash配对；已读原语日志、生产diff、动态计数/表格生成脚本与报告。没有把hash清单当成本地重新执行GPU的证明，也没有重跑全80模块反汇编或重新采集真机。

结论：本轮真实提速和最终已测性能门通过；修复交互符号/动态计数/覆盖表述。继续攻关优先做新基线的R2-D组成消融与C32剩余量化分支，不为制造驳回而重复已经通过的七档全套。
