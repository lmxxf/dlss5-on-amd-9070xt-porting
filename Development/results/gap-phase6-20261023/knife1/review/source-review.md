# Phase6刀1独立地址/同步审查

本地CPU，不修改生产、不目标操作。四token实际pair raw已独立逆排通过，身份见pair-probe/build-receipt.json；基线锁在phase6/baseline。

**Q/K布局：**part*n*1024+(head*(n/16)+tile)*512+lane*16+khalf*8+e；对应旧逻辑token=tile*16+lane%16，channel=head*32+khalf*16+(lane/16)*8+e。V保持phase5 head-major B fragment。CPU逐地址覆盖288/400/448/640，每平面无重复/越界、所有16B起点对齐；两平面相差n*1024，不互叠。wave内旧acc ownership→512B byte tile→新fragment读的符号转置完全一致。

**raw复用：**每wave float raw占16*33*4=2112B，新byte tile仅重用本slice前512B；五wave最大10560B，在16KiB LDS内，互不重叠。Q/K norm读/WMMA完成后才复用；前后release/wave/acquire保留，后续没有WG barrier。288/448末组各192 inactive wave（跨三parts），都在原WG barrier之后统一return；后续wave同步仅live完整wave，不等待已return的其它wave。正式ISA报告也给出norm末读及wait→WMMA→首overwrite、store→wait→LDS读→wait→global store的实际PC，不能只凭源码bare barrier宣安全。

**数值：**新producer仅将原byte_F的同一字节经LDS转置；consumer的两个K16 WMMA顺序仍K0后K16。Q/K/V元素值、容量、归一/softmax没有改。独立GPU pair原raw以consumer-oriented(head,tile,lane,khalf,e)遍历核新Q/K到旧row-major，V raw直接同、AV逐byte同、guards/FP8有限同，四token全PASS；reuse_gate1的四kernel全buffer哨兵由GPUreceipt确认。旧raw基线是实际Vfrag，不混最早AoS V。

**物理收益边界：**新每head/tile连续512B及每lane16B是请求地址/指令组织事实。旧16个lane或16个token的稀疏地址不等于16次DRAM事务；缓存命中、请求合并、sector大小、重放及跨wave复用会改变物理流量。逻辑QKV容量和Q/K/V总读元素数保持；新少load/store指令可能有利，也要付LDS转置成本。不得写“DRAM流量砍16倍”或从61→60VGPR宣occupancy跨档。实际净收益等整Frame/纯NN结果。

没有扩展其它layout/任意token资格；pair仅覆盖一组真实QKV权重与受控输入，整网多模型Gold/AE另验。
