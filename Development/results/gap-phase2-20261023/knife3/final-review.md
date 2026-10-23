# 刀3独立读审：支持整体STOP，保留1152小正值

直接复算36槽CSV的每槽后160帧、三轮/合并统计，核实际命中、命令身份和Gold回执，未新增实验。结果见 `final-review.json`。

|档位|均值差 candidate−baseline|判断|
|---|---:|---|
|900|+0.909 µs|三轮正负混杂，无稳定收益|
|1088|**+6.152 µs**|三轮均值都退，pooled p99 +21.655 µs|
|1152|**−11.935 µs**|三轮均值小改善保留；R2 p99 +36.063 µs，部分控制漂移大于均值差|

数据支持整体STOP：本任务目标1088未获益，900没有稳定收益，不能拿1152小正值覆盖两档。也不能把1152写成“没有任何改善”——原小正值和尾反例都应保留；结论仅限此单一N-pair布局/展开组织及三个测试档，不推广为所有N-pair组织无用。

## 身份与正确性

- 性能两臂使用同一个knife3/frame.exe与同一candidate模块目录，仅NR_C512_NPAIR=0/1。全部candidate性能日志实际calls=3856（含初始化1遍加240测量帧，各16次），baseline calls=0，排除了requested=1却跑回落的假收益。
- 三几何×三输入×双arm×3帧=54帧Gold，全部finite/repeat、成对hash一致；candidate实际calls=64（初始化加3帧）。
- float-skip双arm6帧另核同，candidate实际calls=64；旧模块缺新导出的1152/900共6帧actual requested1/calls0，输出分别对对应基线同。总计66帧Gold成立。
- 正式COMGR baseline/candidate文件SHA与身份回执一致；逐条核89个旧函数code hash/size和旧descriptor资源/浮点字段均同，新增仅2函数。入口地址调整已从资源比较剔除。
- 生产hip目录无diff，生产host头没有NR_C512_NPAIR；刀2MIX和刀3N-pair均未混入最终产品。

Gold核对依据原程序有限/重复检查与远端生成的raw-hash回执，未再下载所有大raw。静态指令数/寄存器下降不能替代当前动态循环工作量与整Frame结果。未发现阻断性证据问题，支持保留全部实验记录并停止本候选。
