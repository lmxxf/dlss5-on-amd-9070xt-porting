# Round4独立审查与接手实验

对象b60e6a8c。先读生产diff、原始CSV/JSON，再读报告。未派子代理，未改生产或安装/发行包。

## 审查结论

认可R4-A与最终载荷在已测范围内的性能晋升。独立复算final七档84slot、r4a四档48slot、final-pure-abba三档36slot，共168slot：

|档位|最终Frame mean改善µs|最终p99改善µs|
|---|---:|---:|
|Original1088|-464.790|-421.979|
|Original1152|-351.489|-259.129|
|Original960|-200.781|-211.649|
|Small1080|-136.189|-180.583|
|Small900|-180.567|-287.167|
|Small720|-179.660|-309.420|
|Smallwindow|-181.257|-144.918|

七档每轮mean/p99均改善。用户消息中的Original1088 p99 -221.81是旧轮数字；本轮JSON/CSV为-421.979。

R4-A单刀相对1005424b：Original1088 -114.976µs、Original960 -69.891µs、Small1080 -68.486µs、Small900 -43.067µs，四档12轮mean/p99全胜。

纯NN同批旧/新：Original1088 7.983631ms（-191.883µs，p99-191.756µs）；Small1080 4.794650ms（-120.507/-73.467µs）；Small900 3.683215ms（-106.795/-92.842µs），每轮mean/p99均赢。7.983631只是这批数据，另NMMN批为8.032311，不应宣称所有稳态都已低于8ms。

80个实际HSACO的SHA逐项匹配hip/SHA256SUMS。四组AE adaptive.csv两臂字节一致，32对输出文件hash按name配对相同；AE3 Small实际记录有连续reuse1/2/3后reason3回compute，补齐了此前仅设置flags的缺口。原语日志给出的Swin区间4513793模式与四个clamp区间边界/特殊值检查通过；clamp属于4785484样本/区间，不是全部2^32位型穷举。

我另在CPU独立穷举Swin和ViT各4513793个区间位模式，保存cpu-proof.json，确认各自旧整数映射与独立推导的简化公式一致，并检查反向配对的两位置。这个CPU证明不替代GPU完整核Gold。

## 剩余报告问题（不撤销已测收益）

1. “R2D_Both为最优组成”过强。Small1080 pooled mean最好，但C32-only的p99为5.720481、Both为5.733283ms；Both对NoR2D第二轮mean还退7.63µs。Original1088 Both相对NoR2D pooled mean+6.75µs、p99+9.62µs。可以说Small1080此次pooled mean最优、选择保留Both；不能说所有档/所有指标均最优。最终组合过门并不使此消融反例消失。
2. 动态脚本已修Post70 shift，但R4-A的计数注释把静态已展开key/kt四组与外层qt四次混在一起：脚本`dynamic_qt_iters*12`称每qt只省8shift+4xor，而报告声称核静态省32shift+16xor。应按实际ISA回边确定这48条属于每次qt还是整个block，然后修模型；不能同时使用两种口径。该动态估算不是收益裁判，本轮不据此加跑原GPU性能批。
3. “全网每个核砍47~48条”应限定为本次改动的attention导出，ViT及其它未改核不在此范围。stage账仍是新HIP与旧ACO诊断域对比，不是同批归因分摊。

## 我亲自执行的新候选：ViT成对指数映射，正确性STOP

独立目录：Development/results/yami-round4-review-20261031；远端同名D:\DLSSNR-Lab目录。

ViT的区间是[1.439453125,1.9775390625]，旧映射shift4/+0x4000，不能直接用Swin的shift5掩码。我推导并CPU穷举：

    pair = (perm(bits(af1),bits(af0),0x06050201) >> 1) & 0x3ff03ff0

微型LLVM23汇编减少位操作；随后只在隔离生成源码中改首个ViT fused模板（其余同构模板未扩围），使用正式COMGR、gfx1201、空RTC_EXTRA_OPTS编译。baseline.hsaco SHA与生产deep_fast-packed-fast.hsaco逐字相同；候选仅6个ViT attention导出机器码变化，V-fragment VGPR61→55、QK-fragment60→54、普通byte68→67，LDS/private0。详resource-check.json。

持原子gpu.lock、game-check、100GB余量与15秒看门狗运行配对Gold：Original1152 baseline通过，candidate首帧SHA为970c0623beaf5db706730f2485e52eaa74e6a76fc82a95d3f79e77cd5ae23e24，未匹配既有Gold，runner立即拒批并释放锁。

结论：该完整核改写STOP，不进性能测试，不合生产。整数映射正确不证明浮点表达式重排/编译后的整个核正确；本次未完成差异定位，不猜测FMA或调度就是根因。资源改善不是通行证。保留gold目录原始输出和日志，perf.json仅为未执行计划，不冒充性能结果。

准备阶段有两次工具失败：源码片段实际出现两次，初始唯一性断言中止；随后明确只改第一个模板。Windows执行策略默认拒.ps1，按既有远程方式使用进程级ExecutionPolicy Bypass重新运行，没有更改系统策略。这两次发生在GPU作业前。正式COMGR与Gold作业均按唯一锁执行，均已结束。

## 当前状态

生产仍b60e6a8c，新增内容仅此审查/隔离实验目录。未安装、未发包、未push。已有R4-A保留；新ViT候选保留为具体实现失败，不宣称ViT映射机制不可能。后续若继续，应先定位该候选的第一处中间值差异，而不是扩大改写范围或先追FPS。
