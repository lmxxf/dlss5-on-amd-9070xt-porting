# 第三阶段A独立源审：FAST1 ViT QT32共享K/V

2026-10-23。只读CPU审计，不执行GPU/RTC、不改生产；A闭环前不实施B。用户已明确授权重试QT32，本报告修正历史前提而不增加确认流程。

## 1. 旧负账到底测过什么

`Development/results/vit-1080-gap-20261001/README.md`及`abba-M32.txt`不是有损QT32的记录。锁定提交`e5d3b7434e4d342d35981700fdbe7045e316c6f7`真实`hip/deep_fast.hip`已取出，见`old-m32-source.txt`与SHA回执。

旧`HIP_VIT_ATTN_M32`同样一个wave带两条独立16-query链，K/V片段只读一次；保原float score affine/位映射、half操作数+FP32 accumulator的分母WMMA、AV次序。它通过19组SAME（含EXACT/AE序列和回绕），三轮完整ABBA全慢：

| 档位 | 三轮mean退步ms |
|---|---|
| 900/400token | +0.2013 / +0.2129 / +0.2125 |
| 1080/640token | +0.1447 / +0.1439 / +0.1465 |

记录中VGPR68→95、LDS/private0；这是当时产物资源，不冒本次新核。同期A/A约-0.001/+0.003ms支持旧负账真实性。

旧README后面“32-query组织只能靠half数学”和“现役已经是逐位最优”的概括过强：前面自己的M32就是逐位反例。能说的是那一个具体M32更慢，不能证明所有未来组织已到上界。也不能反过来将旧M32说成仅有损、假装本次首次试共享K/V。

10-06另有**halfmath M32**与grid实验，属于不同数学：`vit-m32-grid-20261006`在同halfmath产物上仅将640的1280组改640组，测到约-3.263µs，弱于14.9µs漂移；这不能拿来否定旧逐位负账，也不能预言本次缩grid就翻盘。

## 2. 本次与旧版的真实差异

- 当前基线FAST1用`vit_inv`裸硬件rcp，并由`vit_attn_hrtz`省掉输出RTZ half往返；旧10-01是旧数学helper。新核必须调用**当前同一helper**，而非照抄旧`Hrtz`。
- 当前生产W16/compact/byte上下游和完整Frame基线已变化，但这不自动改变attention内部的Q/K/V布局。
- 新宿主精确路由两新导出，实际grid为`ceil(tokens/32)*32`，不保旧双倍空wave。
- 初始本地源码仍在400尾计算幽灵第二链，只guard其读取/写出；独立审指出这一点后，**尚未上GPU**，实现者决定改`template<bool Two>`：尾WG整个第二链编译期删除。最终patch状态在后文核验。

这仍是对旧算法思想的有授权重试；其成功条件是当前Gold与完整同批性能，而不是重新命名。

## 3. 逐query数学不变的必要检查

每一条16-query链必须独立保留以下次序：

1. Q两个片段对应head32维的K0..15、K16..31；每个score accumulator先后执行同两次WMMA。
2. score仍当前float乘、加、clamp、位截断映射；没有换halfFMA、分母half tree、key permutation或softmax近似。
3. 每个key16片生成同样的8个half exponent与FP8 probability；pair打包顺序不变。
4. sum[q]每key片恰好一次原分母WMMA，key按0,16,...顺序；不把两query分母合起来。
5. AV[q][c]每key片用同V fragment更新，c0/c1两个输出片分离；各自key16顺序不变。
6. 结束后每query用自己的sum[0]算`vit_inv`，乘自己的acc，调用当前`vit_attn_hrtz`和`byte_F`。

共享的是只读K/V片段，不共享score、sum、acc或概率；两个独立query链的指令交错可变，每个输出内部依赖不能变。

## 4. 最终Two模板源码审查：PASS

最终`candidate.patch`已逐行复核：`template<uint MAXT,bool Two>`保独立sum0/sum1、acc0/acc1；所有第二链的load/score/prob/den/AV/store均受`if constexpr(Two)`。两个export在入口按`first+16<tokens`做一次WG-uniform分支，body里仍保`first>=tokens`退出。没有原子、LDS共享状态或跨WG同步。

400token每head有12个完整32-query组与1个16-query尾组：最后first384，只访问query384..399，Q1整链消失。640token每head20完整组，最后first608，Q1覆盖624..639，全部有效。

宿主只允许Original、FAST1、n精确400/640、原bytein_bout函数名精确匹配且两新exports均存在；Small/FAST0/其它n/缺任一export保旧callee。先前若已选择有损`_ks2`，因名字不再匹配不会误叠QT32。复用原weights/input/output格式，不新增布局tag需求。`reuse_gate`早退出保持，AE活跃时需按实际gate判断计算量，不拿requested calls当实际计算次数。

Run exactname已设置threads32、groups=`ceil((count/512)/32)*32`；调用count=n×512，故400为416组、640为640组。不能沿用默认count/256（分别800/1280组）。

独立CPU枚举回执见`source-review-check.py/json`：全部409600/655360输出字节恰写一次，Q/K/V各自地址区间不交叉越界，最后8B读取安全。检查绑定最终patch SHA；它是源地址枚举，不伪装成执行了HIP kernel。

## 5. 真共享了什么，动态工作量如何算

新K片段`y`只加载一次，立即供a0/a1两个WMMA；新V片段同一`y`供acc0/acc1。源结构确实共享K/V，Q本身每query仍要各读一次。

按完整一层32heads计算，key tile数400/16=25、640/16=40：每一条16-query链每key tile有2次score WMMA、1次den WMMA、2次AV WMMA，共5次。最终Two=false删除尾第二链后：

| n | 旧QT16组 | 新QT32完整/尾组 | 旧/新有用动态WMMA | 旧/新逻辑K/V请求bytes |
|---|---:|---:|---:|---:|
|400|800|384 / 32|100000 / 100000|20480000 / 10649600（-48%）|
|640|1280|640 / 0|256000 / 256000|52428800 / 26214400（-50%）|

逻辑K/V bytes来自每lane每key tile的两次8B K读取与两份8B V gathering，乘32lanes、实际group/key循环次数。**这不是DRAM实际流量**：cache命中、请求合并、编译器载入组织另算。原Q字节和最终输出字节总量不减少；score/exp/den/AV每有效query也未少算。

初稿400仍无条件计算尾第二链，会变104000 WMMA/层（+4%）；该稿只做本地语法阶段、没上GPU，最终已移除。报告保留这个修正，避免把初稿结构当最终收益来源。

实际机器码还需验证K/V load没有被编译器重新复制到两query分支，最好按full/tail循环基本块的VMEM/VALU/WMMA条数乘上述动态次数。**总静态opcode数会把full/tail两套代码相加，不能用它推动态指令或吞吐。** 若没有可靠CFG展开/硬件动态计数，最多报告源层精确工作量与实际load位置，不造“动态VALU减少X%”。

## 6. 与旧M32的资源比较边界

旧记录VGPR68→95是旧数学、旧编译产物；当前FAST1候选需读实际VGPR allocation、LDS、private spill和float mode。FAST1去掉收尾转换与Newton迭代可能降低活跃区间，但双query仍增加acc状态，不能靠“现在更快的基线”假定这项tradeoff已逆转。

旧10-01负账没有invalid tail读取，只有ghost arithmetic和多余空wave。因此本次能说“更少无效工作”，不能把旧负账贬成错误程序。所有原数据和STOP都保留。

## 7. 最小实测门

- 400尾与640全组分别对当前FAST1 Gold，至少首末/重复/finite；必须新callee真命中。
- 400末16query不得写到输出canary；完整Frame还应有动输入或不同seed短门，避免受控零模式掩盖索引错误。
- 同一真实模块缺任一新export时函数名保旧，未半切grid；FAST0/Small请求QT32仍旧路径。
- actual grid为416/640；每完整网络8个ViT attention调用，AE若开需要区分跳过计算。
- 默认新旧模块的其它导出身份与metadata核对，禁止刀2/刀3负候选混入。
- 同批900/1088/1152完整测量按主队列门执行，若仅微核或理论load减但整网慢，照旧STOP，不改数学追分。

当前裁决：最终源码与地址枚举通过；GPU数值、实际ISA资源与性能结果由主任务后续回执给出。

