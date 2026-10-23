# Phase5刀1组合：Gold通过，但pure尾部未过，不签直接采纳

仅本地CPU读取，独立复算108Gold、36完整Frame槽、12pure槽及正式COMGR身份；没有目标I/O、GPU重测、删除或裁剪样本。详combined-pure-review.json（含每槽分位数、两臂top20原frame位置与CPU时间）、identity-review.json。

## 正确性与编译身份

三档三图样四臂baseline/pair/perm/combined各3帧，共108帧finite/repeat、四臂同对应Gold。正式baseline整体SHA9afa918b…079b4复现phase4；组合实际SHA84b1be91…9dbf0。113函数仅compact及compact_w16两导出改变，其余逐字节同。descriptor变动限RSRC3指令预取size字段，VGPR/核心分配/浮点模式保持，不冒完整descriptor字节未变。

pair实际cvt_pk从144→72；perm实际ds_bpermute16→0、permlanex16为16，s_wait_dscnt45→31。组合2175指令/1084VALU、VGPR188；这些是代码组织证据，不预设运行收益。

## 完整Frame组合

|档|均值差|轮次|
|---|---:|---|
|900|−20.154µs|三轮mean都胜|
|1088|−8.193µs|R1 **+6.108µs**，R2/R3负；不稳定|
|1152|−19.648µs|三轮mean都胜|

九轮p99均改善。使用同phase5 baseline/frame.exe，仅模块目录不同；这些Frame结果不能覆盖pure发现的尾部回归。

## pure1088尾部：不仅一个最大值

全部960样本/arm保留，基线8.360376875→组合8.345058688ms，均值−15.318µs；但：

|统计量|baseline ms|combined ms|
|---|---:|---:|
|median|8.361505|8.339595|
|p90|8.448796|8.442365|
|p95|8.478644|8.484737|
|p99|8.555312|8.739918|
|p99.5|8.616089|8.764506|
|max|8.799430|8.860490|

pooled p99 **+184.606µs**。超过基线p99的样本：baseline10，candidate25。candidate最慢20个分散于R2B1(1)、R2B2(5)、R3B1(3)、R3B2(11)，不是只删一个最大值就能忽略的问题。

R2 p99+133.102µs；R3均值+0.018µs近零、p99 **+264.675µs**。原最大值：candidate R3B1/frame139为8.86049ms（CPU9.053ms）；baseline R2A2/frame74为8.79943ms（CPU8.948ms）。最大/分位与全部原位置留JSON，不去猜哪条是“坏样本”。

两臂同baseline/pure.exe、80warm/160measure、同冻结输入/flags，首尾SHA153ac018…647f1相同。中位数较好与尾部较差同时成立。现数据不能把回归归为permlane、pair或测量事件中的某一项；需看已经排定的独立pair/perm结果。**组合现在不应直接采纳**，也不把该组合的尾退泛化成两个单项都无用。
