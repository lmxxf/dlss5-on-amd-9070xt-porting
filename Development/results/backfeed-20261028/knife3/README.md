# 刀3：W2_FFN_W16_SMALL=3 STOP

以刀1生产代码为基线，刀2已全部回退。当前两个c64-wave2配方原本已有W2_FFN_QT_SMALL_MASK=3，本轮只增W2_FFN_W16_SMALL=3。正式LLVM23/RowOpts/-real-true16编译；两个baseline整HSACO SHA与生产一致。候选各新增10导出、旧函数代码/资源全部同，所有新导出private/spill=0。C64 bi_bo VGPR199→193，C128242→246；LDS不变。b128确增加，WMMA数量不变，见wide-load-isa.json。

七档42帧Gold逐位同、invalid0。四档各3轮4slot ABBA/240弃80/960样本每arm：Small900 mean+8.466µs/p99+15.439µs（3轮mean全退）；Small1080 mean−3.851µs，但三轮p99均退；Original900 mean+2.263µs/p99+14.066µs；Original1088 mean+0.195µs且分轮不稳。整刀STOP，不改生产配方和80模块，不继续扫参。原CSV/命令/模块锁/ISA/编译manifest保留，临时源码副本与二进制清理。GPU锁释放。
