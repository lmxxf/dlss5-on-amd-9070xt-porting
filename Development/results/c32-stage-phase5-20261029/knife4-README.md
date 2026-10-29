# 刀4：Post寄存器RGB尾部

保持c0..31同序RGB累加，PostTap/非空logit保留旧路径。首版保留相邻两qt达到177VGPR；单qt-f32交换161VGPR，两者资源门STOP未上GPU。最终单qt-h2成对交换：145VGPR（原131）、4096B LDS、private/spill=0，全部active W16导出≤160。8条静态ds_bpermute交换对侧半wave的半精度通道，避免先F32展开再交换的额外寄存器。每qt由g0写16像素，g1参与全EXEC交换。

Post_b8静态指令1510→1488；ds_store_b128 4→2、ds_load_b128 6→2、ds_bpermute 0→8。计数是静态编码，不能忽略loop次数和active lane：旧RGB尾部32lane×2轮，新RGB发生在4轮qt且仅半wave写出，因此减少LDS不保证总吞吐增加。资源≤160是本轮资格线，不从VGPR或LDS容量直接编造真实active occupancy。

七档42帧Gold逐位全同、invalid=0。两档硬件实时钟+四档3×ABBA（每slot独立进程240帧弃80，960稳态样本/臂）如下：

|档位|基线ms|候选ms|mean Δµs|p99 Δµs|
|---|---:|---:|---:|---:|
|original1088|8.693740|8.738462|+44.722|+217.618|
|original960|6.399437|6.430336|+30.899|+28.290|
|small1080|5.400220|5.445129|+44.909|+29.767|
|small900|4.189888|4.218449|+28.560|+46.316|

original1088 C32阶段Δ +41.997µs；Post单核：`{"c32_wave1:c32_wave1_post_b8_w16": {"baseline_ms": 0.72763, "candidate_ms": 0.7708133333333335, "delta_ms": 0.04318333333333346}}`。

small1080 C32阶段Δ +45.903µs；Post单核：`{"c32_wave1:c32_wave1_post_b8_w16": {"baseline_ms": 0.7472533333333334, "candidate_ms": 0.79293, "delta_ms": 0.04567666666666659}}`。

结论：STOP，恢复刀2基线，不用减少22条静态指令代替实测收益，不追加档位救援或刷轮。

补充Runtime occupancy API：候选3/4四种既有C32核的threads32最大驻留均16 blocks/multiprocessor（原基线同16），见occupancy34原始stdout；这是最大驻留上限，不是实测active waves或每SIMD独立LDS分区证明。
