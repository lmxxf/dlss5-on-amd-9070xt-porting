# Phase6 刀1：Q/K head-major fragment（隔离候选）

基线冻结自第五阶段生产。新 producer `vit_stream_qkv_frag_bin_w5f8_qkfrag` 与两个 attention `vit_attention_fused_{400,640}_bytein_bout_qkfrag` 成对；V-only 三个旧 export 保留。`NR_VIT_QKFRAG=1` 开启实验，同时必须满足现有完整 VFRAG 资格且三新 export 全部可用；任何缺失均回旧 V-only。现有 Original/Small/AE 门不变，预热发生在构造期。每帧局部 bool 同时选择生产和消费布局，避免跨模型热切混接。

Q/K 地址：`part*n*1024 + (head*(n/16)+tile)*512 + lane*16 + j*8+e`。该 byte 对应逻辑矩阵 `(token=tile*16+lane%16, channel=head*32+j*16+(lane/16)*8+e)`。V 平面不变，对应逻辑矩阵 `(token=tile*16+(lane/16)*8+e, channel=head*32+j*16+lane%16)`。全部容量仍 `3*n*1024`，每 lane 16B 地址对齐；两 Q/K 的 K16 WMMA 累加先后完全保留。

Producer 保留所有累加/归一化/byte_F 数学。复用每 wave 自己的 `raw`：norm 读完后 `WG_FENCE(release) → wave_barrier → WG_FENCE(acquire)`，再把原 byte_F 写入 16×32 byte LDS 行主矩阵；第二次同样 fence 后，各 lane 读同 token 的两个 K16 half，合为 16B head-major global store。已有全部 WG barriers 仍在 inactive-wave return 之前，后续只 wave 同步。288/448 有 inactive wave，400/640 整除五 wave。旧 VIT_QKV_TAIL_VEC 是不同的行主输出候选，未把其历史测量当成本刀证据。

Consumer 每 query 一次 Q b128；每 key tile 一次 K b128 和一次 V b128。只是逻辑加载请求变化，不预称 DRAM 流量或收益。正式 ISA 要核实际加载宽度和 norm-read/overwrite fence 顺序。

`generated-recipe.json` 锁四份 COMGR 输入，实际 RTC_EXTRA_OPTS 空。两候选本地 LLVM23 C++14 syntax check 通过（仅既有 constexpr-if 扩展警告）。`host-build-identity.json` 锁 standard/free1072 Frame 与 Original1x pure O2 主机。所有更改位于隔离 source，生产未改。

## 实机数值门

288/400/448/640 token独立pair GPU探针全部QK逆排0-diff、V raw不变、AV0-diff，guards/finite/reuse_gate/input/weights不变；CPU从consumer坐标再次逆排复验同。七档三图样双臂126帧Gold及七档双臂AE84帧，逐帧raw SHA和完整AE CSV配对一致，actual compute/reuse覆盖。84slot Frame ABBA进行中，生产未采纳。

## 裁决：仅三档默认开启

|处理档位|试验Frame变化µs|pooled p99变化µs|生产默认|
|---|---:|---:|---|
|Original900|−11.885|−15.124|QK-fragment|
|Original1088|−2.035|−22.968|保留V-only，轮间不稳|
|Original1152|−18.882|+18.668|保留V-only，尾退|
|Small900|−7.692|−8.495|QK-fragment，微小收益|
|Small720|+3.704|+3.212|保留V-only|
|Small1080/1152|−11.083|−7.045|QK-fragment|
|Smallfree1088|−1.669|+41.400|保留V-only|

门沿用合并mean/p99工程标准，不冒称每轮p99全胜。批准三档分轮反例：Original900 R1/R3 p99+8.033/+7.038µs；Small900 R2mean+1.656µs、p99+13.155µs；Small1080 R3p99+17.340µs。完整84槽原样保留，未剔除样本或追刷轮次。

全范围实验pure1088：8.336081→8.322504ms，mean−13.577µs且三轮均胜，但pooledp99+10.364µs。**最终默认1088仍V-only，这个试验均值不是已追回的生产收益，也不跨批扣Mochi差距。**

限定host按model/尺寸/token仅放Original1600×960/400、Small1600×1024/448、Small1920×1152/640；NR override优先公开DLSS5_HIP_VIT_QKFRAG再默认1，NR1也不绕形状，0关闭。完整父V-fragment约束/三export成对能力门保留。实际helper120组恰三shape，限定7档默认＋off＋两缺export共33帧、批准三档AE18帧验证通过；正式源逐字等限定源，GPU本体与全范围实测候选相同。

4个双架构模块已更新，其余76保持phase5；gfx1201实测，gfx1200仅编译/ELF核验。最终80包及SHA见final-payload-identity.json。
