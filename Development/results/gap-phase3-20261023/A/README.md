# 第三阶段A：当前FAST1的QT32双16query共享K/V重试

2026-10-23。用户明确授权按A→B→C次序重试；先交一个A候选，不扫展开/tiling。只改隔离源，root独占正式COMGR/GPU，生产未改。

## 先纠正历史前提

旧 `vit-1080-gap` 的M32不是“仅有损做过”：`failed-experiments.md:2433`与`results/vit-1080-gap-20261001/README.md`明确记载**19组SAME**；900三轮+0.201/+0.213/+0.213ms，1080+0.145/+0.144/+0.147ms，全负。旧代码在分支`vit-1080-gap`（当前指向`accb0663d1671b9b07227d66c638bcebb6b7eb26`）可直接读到双query共享K/V实现。

本次是用户授权的**当前基线重测**，不是把旧实验说成没做过。旧报告关于“当前最优/不在逐位可重排范围”的判断属于当时具体实现与测量；本次结果重新由当前三档Gold/ABBA裁决。

## 候选与旧M32的实际差异

- 双16query共享K/V的核心组织沿用旧M32，逐query的QK两步WMMA、half概率构造、den和AV的key顺序不变。
- 当前正式 `deep_fast-packed-fast` 的FAST1数学被保留：`VIT_FAST_NUM=15`、现行`vit_inv`裸rcp、`vit_attn_hrtz`及`byte_F`当前规则；不是恢复旧M32结尾的显式Hrtz，也不改softmax/half舍入。
- 旧M32未改主机网格，后半多派wave进入即退；本次exactname派发改为`ceil(tokens/32)*32`个wave，每wave32线程，真实不发多余wave。
- 旧400尾只mask Q1 load/store，但仍计算无效第二条链。本次用`template<bool Two>`，export按`first+16<tokens`选择一次；尾部`Two=false`编译期删掉第二条score/prob/den/AV，不产生虚构query算术。
- 新export不覆盖旧函数，两个都存在才路由；可在同host、同candidate模块里用开关作对照。
- 历史M32报告95VGPR；当前本地LLVM21筛查96VGPR。没有证据声称本次寄存器已经解决旧M32并行度问题；正式COMGR与性能另测。

## 数学、地址和网格

新`vit_attention_qt32_body`同时持有q0/q1、各自sum、各自两个AV片；K片读取一次先更新a0再更新a1，V片一次先更新acc0再更新acc1。两条链无共享累加器，**每条链仍按key=0,16,...；每个score仍先K0再K16**。概率helper逐字沿用当前相同表达；den保原f16×f16→f32 WMMA，AV保原FP8乘加，最终同`vit_inv`和输出量化。

400尾first=384，Q0 token384..399；Two=false没有Q1 load、计算或store。K/V最后key=384，lane索引最大399，均在分配范围。640无尾。

CPU穷尽地址检查：[index-proof.py](index-proof.py)、[结果](index-proof.json)。所有Q字节各读一次、输出字节各写一次，K/V全在界内。按源码WMMA次数得到：

|tokens|旧wave数|新双链wave|新单尾wave|每层WMMA旧/新|逻辑K/V请求bytes旧→新|
|---|---:|---:|---:|---|---|
|400|800|384|32|100000 / 100000|20480000→10649600（−48%）|
|640|1280|640|0|256000 / 256000|52428800→26214400（−50%）|

逻辑请求是源码中K/V元素字节，不是缓存miss、DRAM流量或整体耗时。本次尚未以动态trace核VALU周期；减少wave也可能降低隐藏延迟的能力。

## 宿主路由与回落

`NR_VIT_QT32=1`启用，默认/0旧路；首次读取后固定。资格为Original、FAST1、n恰400/640、原callee是对应bytein_bout，并且400/640两个新exports齐全；Small、FAST0、其它尺寸/变体、任一export缺失都回旧callee。

新导出：

- `vit_attention_fused_400_bytein_bout_qt32`
- `vit_attention_fused_640_bytein_bout_qt32`

两者ABI仍in/out/tokens/reuse_gate；reuse_gate非零时与旧核同样直接退出。两条Vit入口都调选择器，实际选择时加计数，析构输出`VIT_QT32 requested=... calls=...`。完整71块单遍每帧应8次；计数和真实函数需一起核。

`Run`的两个exactname设置`threads=32, groups=((count/512+31)/32)*32`，调用仍传`count=n*512`。不依赖默认count/256规则，也不改其它核网格/权重/布局。

## CPU构建与可运行产物

隔离根：`/home/lmxxf/work/dlss5-gap-phase3-20261023/A/`。

- `baseline.generated.hip`、`candidate.generated.hip`：正式COMGR deep_fast-packed-fast同recipe（空额外opts），亦归档`generated/`。
- `frame.exe`：正式O2，SHA `ab74c9059c1a1c3cc5e1fb904503ba1823d911521b0a624e6f606b07507485ec`。
- `source/`：当前生产header（含刀1）+A；不带已STOP刀2/刀3。
- `candidate.patch`、`prepare.py`、`build-host.sh`：源码差异和复现入口。prepare会覆盖隔离副本，GPU运行中勿重跑。

本地LLVM21仅语法/ISA筛查：旧400/640核68VGPR/SGPR12、0 LDS/spill；新核96VGPR/SGPR14、0 LDS/spill。新函数含full/tail两个loop静态合计15条WMMA，实际full每key10、tail每key5，不能把15当每次迭代动态成本。旧导出在同本地工具链下代码SHA保持。

正式COMGR应由root编译并取回后跑`isa-check.py <baseline.hsaco> <candidate.hsaco>`和`check-old-exports.py`，再做三档实际Gold/ABBA。CPU初筛数据不代正式COMGR机器码；源审见`source-review.md`。

## 正式COMGR与循环体核查（已完成）

候选SHA `86792aabd572fffbb0a0e5ccb4797a5fd23b843c9e534cc5e2846c7f59ed3e80`；基线生成源码与此前fresh基线逐字节同，复用已验证SHA `00e39349…095ce`。**旧89函数在新91函数模块里所有机器码与kernel资源/浮点配置字段相同**，只新增两个QT32 exports；入口地址变化不算资源改变。见`old-exports-identity.json`。

正式COMGR旧核68VGPR/SGPR12，新核98VGPR/SGPR14，LDS/private均0。新代码6060B含full/tail两条控制路径，不能拿总864指令/15WMMA当每次key迭代成本。

按反汇编back-edge拆出的实际key loop（没有内部其它分支），见`loop-isa.json`：

|每K16 key tile循环体|指令bundle|VALU（排除WMMA）|WMMA|global_load_b64|global_load_u8|wait_alu / wait_loadcnt|
|---|---:|---:|---:|---:|---:|---|
|旧QT16|123|84|5|2|16|3 / 5|
|新QT32单尾|123|84|5|2|16|3 / 5|
|新QT32双链|211|146|10|2|16|3 / 10|

因此两条query链实际共享了K/V加载，并把循环内部分地址/控制工作合并：211小于246、146小于168；WMMA总工作相同。上表可乘实际25/40个key tile和各full/tail网格数，得到loop动态指令预算，但**不是GPU实测周期或DRAM流量**。初始Q加载、尾部输出、运行时branch与调度成本另在循环外。wave数减少、98VGPR活跃期和依赖延迟仍可能抵消收益，必须等待三档ABBA。

root采用同新host、同candidate模块，仅`NR_VIT_QT32=0/1`切换，两臂旧函数身份已经锁定。

## GPU结题：Gold通过，QT32止负

同host/同candidate模块仅NR_VIT_QT32=0/1，实际每性能槽0/1928次命中（240帧加1准备帧，每帧8层）。三档三输入双臂54帧Gold及实际旧模块缺导出回退6帧全同；每档3轮4-slot ABBA，240弃80，每arm960稳态样本。

|档位|旧ms|QT32 ms|差ms|pooled p99差ms|
|---|---:|---:|---:|---:|
|900|6.473522|6.485561|+0.012039|+0.062208|
|1088|8.874891|8.972169|**+0.097277**|+0.089184|
|1152|9.166979|9.257129|**+0.090150**|+0.100465|

1088/1152每轮mean与p99都退；900不稳定且合并退。**STOP，不进生产，不刷轮、不继续扫QT32展开变体。** 旧M32并非只限有损，本轮是真实半grid、400尾单链、当前FAST1条件的明确复验，仍无净收益。

正式98VGPR/0LDS/private0，动态loop证明双链K/V加载与单链相同、有效WMMA不增加。结果表明减少逻辑请求不足以兑现整网收益；独立waves变少与单wave活跃区间变化是合理解释，但没有硬件stall分解，不将其说成唯一因果，也不称DRAM字节减半。原数据evidence/、statistics.json和ISA回执全保，游戏/生产/发行包未改；GPU锁释放。
