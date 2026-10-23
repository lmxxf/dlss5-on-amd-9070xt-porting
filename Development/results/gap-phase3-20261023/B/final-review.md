# Phase3 B 独立读审：有用重叠成立，净收益为负，支持STOP

独立复算36性能槽、84帧Gold回执、六份完整trace JSON、计数器/故障日志，并核正式producer/consumer身份。未新增GPU/RTC，未修改生产。复算数据见 `final-review.json`。

|档位|均值差 candidate−baseline|pooled p99差|
|---|---:|---:|
|900|**+86.410 µs**|+97.634 µs|
|1088|**+124.866 µs**|+115.762 µs|
|1152|**+52.815 µs**|+65.640 µs|

每档240帧/槽丢前80、三轮4-slot、每arm960样本。九轮均值与p99均为正，逐轮/合并与statistics一致，支持STOP。该结论针对完整链化候选总成本，不把损失单独归给atomic、等待、cache、retain或release。

## 路由和计时可信度

所有性能command使用 `frame-warmed.exe`、同一trace-off candidate模块，NR0/1对照。NR1每槽记录真实cold warm为**4或5次**，actual calls/AnyOrder分别为**3904或3920 = (warm+240)×16**，ordered0、status0、counter_mismatches0；没有错误硬套3856。NR0实际calls0。

源码 `chain_keep` 只在PrepareC512Chain成功时持有；NR0不人为retain。候选ReleaseChainFrame在既有完整Frame Flush之后、elapsed截止之前，成本确已计入。最终counter/status检查在计时外drain执行；其通过不被偷换为零协议开销。

正式模块清单用empty-opts producer `7d9c6beb…`，与 `producer-noopts-identity.json` 对齐；其生产baseline为 `a6866439…`，旧91函数代码/资源同。consumer `b249f6b9…` 对生产旧89函数保持。max-ilp的 `fff2…/c1bf…` 组不是性能清单，原错误身份比对与89函数差异仍保留。初次candidate触发afterwait pool保护并exit1，和修复后的有效样本分开；没有把失败或重编配方混进性能。

## Gold、安全与故障

- 三档三图样双臂54帧Gold；ordered/AnyOrder诊断18帧；32carry/64limit共6帧；旧模块缺导出6帧：合计**84帧**，finite/repeat及相应基线hash同。另smoke2未加入此84计数，初失败不算通过。
- 独立timeout-zero probe与CPU collector均PASS。额外根据规格直接构造期望raw字节串并算SHA，float/byte全零payload＋两端canary、ready0＋canary、status1＋canary的四份期望SHA均与远端实际读回SHA一致。它证明无producer、n16、wait1场景的有界失败与定义输出，不等于所有并发故障形式的证明。
- 强制full slot0未来target：actual status1、counter_mismatches0、exit1，整批正确拒绝；没有把零写失败当正确网络回退或有效性能样本。
- 32carry：原trace全部16边target及每个ready值都为**4294967344**，越过32位而保持一致。
- 64limit本次实际初值是**18446744073709551600（UINT64_MAX−15）**，不是文档中可选的最后8倍数。日志只执行16条链后enabled0/overflow1，status0、counter_mismatches0，输出Gold同；不声称trace导出了limit64全部raw计数器，该case以最终读回检查日志为证。
- 合法缺导出两档requested1/actualcalls0，旧路径输出同。

## 有用计算重叠的独立复算

六份trace逐边核live producer marker非零、invalid尾marker为零、consumer marker非零、ready逐项等target。1152每边8个无效尾wave确实不发布；900/1088没有无效尾wave。

|档位|ordered有重叠边|AnyOrder有重叠边|AnyOrder满足先后条件的consumer数|
|---|---:|---:|---:|
|900|0/16|16/16|6641|
|1088|0/16|16/16|13091|
|1152|0/16|16/16|14795|

条件为consumer首条真实WMMA完成marker早于该边最后producer pre-publication marker，非入核或等待起点；复算与trace-summary一致。诊断有额外store/fence/clock成本，数量不代表trace-off性能帧的精确重叠比例，不按猜测频率换算毫秒。

真实重叠与整网变慢可同时成立；本轮已经完成这条具体边的完整验证，不应重新命名扫wait/同步变体，也不能据此否定所有TCHAIN。生产hip仍无diff。原大raw以远端hash回执核对，本次没有重复下载或重测。未发现影响STOP结论的证据缺口。
