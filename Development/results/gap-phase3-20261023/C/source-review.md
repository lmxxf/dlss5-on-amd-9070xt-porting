# 第三阶段C独立依赖源审：Load/Wait具体窗口

2026-10-23，只CPU/源码/反汇编阅读，不GPU、不改生产。基线只带第二阶段刀1；A/B及刀2/3负候选不带入。

## 1. 旧负账与当前配方

- 10-01 `c512-qkv-pipeline`已收D4＋SCHED1：保持每个acc的K16序，提前读未来fragment、消费时才zero-select、让长循环避免每迭代清空。不能再拿旧U4的“循环尾wait0”描述当前核。
- 10-14 `vit-w16`已经审D3/2/1：VGPR183/179/171仍同一资源档且LDS限定驻留，只减预取深度。本轮不重扫D。
- 9-30 `hoist-loads`已收普通C64/C128的W2_HOIST_LOADS15及C32 up3；其向SP推广有负轮未收。本轮不能把已外提的条件读列为新发现。
- 当前C512源码D4＋SCHED1、W16两K16打包；当前c64-wave2-fast含HOIST15/QKV_FUSE3/FFN_QT_BATCH4。已有recipe与本次真实机器码相互核对。

实际反汇编来自已锁生产模块，路径 `/home/lmxxf/work/dlss5-aco-isa-20261023/`；对应gfx1201 manifest：

- c512-m32-mh：`9afa918bc6252ab1beee5bcd15ed6ab838c612cc8368d814b48e28b67a7079b4`
- c64-wave2-fast：`475f515fe8d5fa2d1dc679fc74017adfd35b7ba38be1fb6c56dc67f9ce7690e0`

`inspect-dependencies.py`/`dependency-windows.json`保留所有零wait前最后VMEM与紧随consumer。脚本是词法提取后人工依赖审，不伪装完整CFG验证器或动态stall计数器。

## 2. 现有wait不能直接放宽

| 实际核 | `loadcnt0`数量 | 独立核查 |
|---|---:|---|
| c512_qkv_attention_compact_w16 | 2 | 最后QKV权重及attention bias均马上被消费 |
| c128_wave2_bi_bo | 11 | 全部例子紧随读取最后载入寄存器；有权重/输入/scale/bias |
| c256_wave2_bi_bo_w16 | 18 | 同上，若有LDS操作交错也不取消对应VMEM RAW |

C512两个明确例子：

1. `0xFBE28`加载B到v128:131；`0xFBF74 wait_loadcnt0`之后的`0xFBF78/80`马上用v128:129做WMMA。改成wait1会允许真正操作数仍未返回。
2. `0xFDB48`加载bias到v106:109；`0xFDC64 wait0`之后dual-add马上用v106/108。它不是无关未来prefetch。

当前QKV中已经存在`wait0x13→9→7→5→3`以及后续0xC/0x10/0x14等partial waits；有用计算与未来load已经交错。不能把“有一个wait0”当“整核串行读等”，更不能手改0为N绕依赖。

c128例子`0x13DC4C wait0`后用刚载入v111:112，`0x13F2AC wait0`后立刻乘最新scale v47；c256 `0x17AF44 wait0`后读v222:223，`0x180A3C wait0`后加最新bias v124..127。完整31个窗口见JSON。

## 3. 唯一可形成候选的窗口：最后weight到达前先做独立WMMA

虽然不能改wait值，C512尾部的**指令排列**仍有一小段依赖合法的机会：

- `0xFBF60 wait1`已保证除最后v128:131载入之外的旧操作数可用。
- 紧接两条v120:121相关WMMA后，旧代码先`wait0`，计算j5对应的两条v128:129 WMMA。
- 随后的`0xFBF88..0xFBFB8`七条WMMA只读更早的B片v100:101、v132:133、v140:141、v112:113与A v107..110；dest分别为v89:96、73:80、81:88、65:72、57:64、49:56、33:40，与最后load v128:131及那两条acc不交叠。
- 因而这七条工作在数据依赖上可以先移到wait0之前，最后仍保wait0再消费j5。**不删除wait，不越过真正RAW，不改变每个acc自己的K16链。**

这只是一处流水收尾机会。最后load在此之前已有不少WMMA可覆盖，未证明它真在stall；本报告不承诺收益，更不外推为大面积串行瓶颈。

## 4. 实现者选定的唯一C候选

ISA agent据上述窗口提出：只在W16分支最后两个K16步骤`s>=30`，先执行j0..4的10条WMMA，再插**编译器**`sched_barrier(0)`，最后执行j5的2条。现有每步外层sched barrier、D4、load ring、布局、所有数学不变。

设计审查可通过：各j独立acc，保持同一acc的s=0..31顺序；没有输出/store穿插，也没有新buffer。s31也分组可能有额外调度代价，因此这是一个固定候选，负了就停止，不再变成s30/s31扫参。

它改变的是“尚未必要等最后load时，先做独立工作”的调度。WMMA数量、权重字节、查询数量都不减少，不能包装成新增数据复用或少算矩阵。

GPU前的实际ISA门：

1. 真正让已ready的j0..4先执行，wait0继续紧守j5读取；不能只是源代码换行但机器码不变。
2. 不把新load覆写放到旧fragment最后使用之前；register ring的RAW/WAR仍有效。
3. 外层每个acc K16序保持，原math/float mode/ABI/LDS及非目标导出身份保持或明确列差。
4. 若编译器重新排回原序、资源退档/溢出、或出现新的全流水清空，则不值得上GPU硬试。
5. 不改wait立即数、不扫D1/2/3、不重新开HOIST15，不往c128/c256另塞第二候选。

## 当前状态

独立扫描和候选依赖设计完成；具体补丁、最终资源/机器码与GPUGold待后续回执。这里不把“依赖合法”当“必然更快”。


## 实际patch源审：PASS

已读`candidate.patch`及隔离`C/source/hip/c512_qkv_attention_compact.inc`。实际仅在W16分支的s>=30把原j0..5循环拆成j0..4、compiler sched_barrier、j5；仍先读取同一ca/cb快照、同一prefetch、同一外层前后barrier。

逐acc的32次WMMA顺序未变，最后两个s的每个j也各更新一次，不新增/删除MMA。没有修改指针算式、buffer大小、权重排列、skip/norm/softmax或host路由。非W16分支逐字保持。最后S31也拆分属于固定候选，不能用结果为由再刷S30专用变体。

源码身份见`source-review-identity.json`。机器码是否真让wait1后的独立操作先执行、旧导出是否全保持，仍等实际编译结果；若机器码不形成预期窗口就不把源码意图当成果。
