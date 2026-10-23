# Phase4刀1独立读审：head-major V fragment候选通过收益门

仅本地CPU读取/复算，无目标机工具、SSH、GPU或新实验。36槽原始CSV每槽240帧丢80，独立重算三档每轮/合并统计；54帧完整Frame Gold及实际pair原raw重新检查。结果见 `final-review.json`。

|档位|baseline→candidate ms|均值差|pooled p99差|
|---|---:|---:|---:|
|900|6.457942917→6.442445625|**−15.497 µs**|−50.000 µs|
|1088|8.853382083→8.794483125|**−58.899 µs**|−61.764 µs|
|1152|9.153676250→9.101882188|**−51.794 µs**|−71.997 µs|

九轮均值均改善，三档pooled p99均改善，每arm每档960稳态样本；与statistics逐项吻合。**900 R3 p99真实退步+4.693 µs必须保留**，不能写成每轮/每槽尾都好，也不以计时分辨率抹去。1152 R1控制漂移较大，仍按原轮次保留，不刷轮。

性能command同一个实验frame.exe、同candidate模块目录，仅NR_VIT_VFRAG=0/1；各档flags同，首尾hash同。actual日志baseline producers/consumers=0/0，candidate=1928/1928（240帧+1初始化，各8层），证明新producer与consumer成对实际命中，而非请求值或fallback假比较。

## Gold及pair原始回读

三档三图样双arm各3帧=54Frame Gold，finite/repeat、成对hash全同。

重新直接读取本地`pair-raw/400`和`640`的二进制（不只看PASS字段）：

- Q/K前两平面逐字节同；按最终**head-major**地址`Vbase+(head*(n/16)+tile)*512+lane*16+j*8+e`独立逆排V，与旧row-major每个字节同；保存的inverse也同。
- 两边AV输出逐字节同；QKV/AV所有前后256B canary完整，E4M3 NaN码为零，QKV总容量相同。
- 所有raw重新SHA与归档cpu-verified.json吻合。
- reuse_gate1四个导出的全buffer哨兵不变由GPU程序receipt确认；没有单独reuse raw，本审不冒称独立重新读取了那些未导出的sentinel。
- receipt实际源文件为f16，6291520B，生产ReadWeights展开12583040B，原Frag8编码后3145856B。第一次只接受物理f32的host在CPU loader阶段拒绝，未产生GPU数值结果；旧身份与失败单列，后续生产loader修正未改算术/模块/布局。

旧tile-major草案在GPU前统一改head-major，未把两布局混测或混解raw。实际pair验证覆盖真实block31 QKV权重和一组有限小值输入；Frame三图样提供整网补充，仍不等于全输入数值证明。

## 签审范围

现证据支持候选收益成立并进入生产默认路径/回退的最后验收。这里签的是已测gfx1201显式候选：不提前声称默认无覆盖标记的新宿主、缺导出回退、gfx1200真实硬件或游戏部署已验收。root后续默认/fallback及pure对账应保留独立身份和数据；本轮没有提前刀2、没有构建正式宿主。
