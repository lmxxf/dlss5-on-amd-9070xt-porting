# Phase3 A 独立复核：QT32 STOP成立

独立读取36槽原始CSV，每槽240帧丢前80，复算三档每轮/合并均值及p99，与 `statistics.json` 全部一致。每arm每档960稳态样本，未新增实验。详细复算见 `final-review.json`。

|档位|均值差 candidate−baseline|pooled p99差|
|---|---:|---:|
|900|+12.039 µs|+62.208 µs|
|1088|**+97.277 µs**|+89.184 µs|
|1152|**+90.150 µs**|+100.465 µs|

1088与1152三轮均值、p99均退步；900三轮方向不一致且合并退步。数据支持停止此QT32实现，无需刷轮或追加展开变体。

两臂command实际为同一个A/frame.exe与同一A/modules，仅 `NR_VIT_QT32=0/1`；每档flags一致，首尾hash一致。性能日志actual VIT_QT32 calls分别0/1928（240帧加初始化1帧，每帧8层）；Gold candidate calls32（3帧加初始化），排除未实际命中的假比较。

三档×三图样×双arm×3帧=54帧Gold全部finite/repeat、成对hash同；实际旧模块缺新导出回退的900/1152共6帧requested1/calls0，输出与对应基线同，共60帧成立。原大raw依据远端hash回执核对，未重复下载。

当前生产hip目录无diff，生产host头不含NR_VIT_QT32；本候选未进入生产。源码/loop ISA证明减少逻辑K/V请求，而本次结果证明该具体组织未兑现完整Frame收益，两者不矛盾。减少wave、增加98VGPR活跃区间等只是可能机制，没有硬件stall证据时不归为唯一根因；也不把逻辑字节减少冒称DRAM流量减少。STOP限本候选/配方/三个档位，不推广为所有QT32或所有跨query复用必慢。

未发现影响结论的统计或路由缺口。本轮未GPU/RTC、未修改生产或重编正式宿主。
