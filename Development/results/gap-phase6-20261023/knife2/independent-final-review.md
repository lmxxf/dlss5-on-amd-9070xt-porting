# Up A最终独立裁决：全部STOP

只本地复算既有fullA84槽、half-only48槽和fullA pure12槽，逐轮/合并统计全部吻合；没有新方法、删点、重测或额外构建。

fullA四个W16作用档pooled mean均退；pure虽mean−5.983µs，pooled p99+35.709µs且三轮尾都退，R3均值也回退，因此全开不收。

half-only仅两个half导出改变且同fullA代码/metadata，其他91（含W16/QK）同基线。Small720 mean−5.874µs、p99−64.665µs；free1088−7.860/−7.414µs；但Small900 mean−0.459µs近零，p99 **+22.444µs**（R2+35.843、R3+44.571）。未执行修改核的Original1088控制仍有−4.224µs小差及轮次波动，不能冒真实优化收益。

按既有pooled mean/p99门，half-only在受影响范围也未过，支持全部STOP，不再加分辨率条件或变体。保留Small720/free1088正值，不把它们抹成全慢。此前111帧正确性和路由门仍有效，但不替代性能门；fullA pure不能算half-only或最终生产收益。

最终产物只保刀1的QK三shape限定，生产没有HIP_UP_WIDE_BYTE_PAIR。详independent-final-review.json；全部样本及初版结果保留。
