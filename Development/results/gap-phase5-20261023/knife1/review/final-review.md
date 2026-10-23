# Phase5刀1独立最终签审：采纳pair-only，保原serial norm

**现有数值与性能证据支持pair-only采纳；perm-only与combined不带入。** 本审仅本地CPU复算/回读，未新增目标机操作或GPU实验。最终生产默认、non-W16回退及双架构产物由root另锁身份；不提前冒称这些未采回门已过。

## 独立证据链

1. phase4最终基线80模块/两产品宿主/pure身份已独立锁，root现gfx1201复制40项同；没有使用旧10月6日代码当今天基线。
2. 三档三图样四臂共108帧Gold全同。实际helper GPU probe和本地raw复算：32768lane旧/新norm及lower/upper广播逐32bit同；QKV/prob/AV各524288值逐byte同，±0参考、输入/输出canary通过。当前NOF1路径、full EXEC和采样FP32范围明确。
3. 正式113导出只影响compact/compact_w16，其他111函数代码同。pair转换144→72、VALU1367→1025，VGPR188不变；资源差异限预取size，不称整descriptor字节相同。
4. pair-only完整Frame三档各3轮，**9轮mean全赢**：900−23.331µs、1088−11.701µs、1152−18.815µs；三档pooled p99均改善。但分轮p99只有5胜4退：900R1+95.325、1088R1+6.444、1152R2+15.521/R3+12.824µs，全部保留。
5. pair-only pure1088同host同输入：**8.342062750→8.326007260ms，−16.055490µs**；三轮mean全赢，pooled p99 **−6.302900µs**。R1/R2 p99仍+8.938/+7.381µs，R3−47.038µs；不是每轮尾都好。各960样本，无删点、截尾或重刷。

## 为什么不带permlane

perm-only1088 mean+4.869µs、pooled p99+12.988µs，未证收益；combined虽然Frame有小改善，pure pooled p99却+184.606µs，不能用平均值盖掉尾退。这个决策基于分别测过的实现，**不宣称已把combined尾退因果归给perm**，也不把permlanex16语义正确等同于性能更好。原ds_bpermute串行norm保留。

具体历史与NaN/零语义见README；全部分位/原样本位置见combined-pure-review.json。pair单项复算见pair-frame-review.json与pair-pure-review.json。最终可宣传的是小幅、已测范围内的平均收益，不是声称普遍更快或所有尾延迟都改善。
