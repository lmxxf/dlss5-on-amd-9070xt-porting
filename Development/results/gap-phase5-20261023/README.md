# 第五阶段：C512成对打包与ViT V-fragment扩围

## 结果

刀1仅采纳C512成对FP8打包，保留原serial norm；刀2采纳Small四档及Original/Small AE的V-fragment资格。均基于第四阶段生产，严格区分Frame与纯NN。

|改造|档位/处理尺寸|同批Frame节省µs|
|---|---|---:|
|C512 pair-only|Original900 / 1600×960|23.331|
|同上|Original1088 / 1920×1088|11.701|
|同上|Original1152 / 1920×1152|18.815|
|V-fragment扩围|Small900 / 1600×1024|23.049|
|同上|Small720 / 1344×768|4.663|
|同上|Small1080 / 1920×1152|59.642|
|同上|Smallfree1088 / free1920×1072输入|65.176|

刀1纯NN1088三ABBA：**8.342063→8.326007ms，−16.055µs**。这是本批旧/新生产实现比较，不从历史Mochi差距跨批相减。刀2两臂统一使用已采纳C512 pair模块，性能固定AE0，未把复用帧跳算当布局收益。

## ISA与未采纳结果

C512 pair：cvt144→72、总指令2483→2104、VALU1367→1025，VGPR188/LDS6144/private0不变。perm/or3各52→16，移位和掩码仍存在，不能写全清零。

permlanex16虽然将ds_bpermute16→0、wait_dscnt45→31，但单项1088均值+4.869µs；组合pure p99+184.606µs且分散多槽，均停止。只采纳pair，W2未扩。原失败与路径修正回执全保留。

## 验证与实际边界

刀1四臂108帧Gold、独立GPU/CPU norm及FP8边界probe、非W16三档18帧pairedGold通过。pair三档9轮Frame＋3轮pure均值改善，分轮p99反例如实保留，见[刀1报告](knife1/README.md)。最终gfx1201的113函数及.text/.rodata/.note同实测pair，双架构80模块更新2个，其余78不变；gfx1200仅编译核验。

刀2Small四档72帧Gold、受控AE1七档84帧、默认idle两档24帧、AE2/3两档48帧共228帧跨臂一致。AE政策/anchor/默认500ms阈值不变，全部实际状态另核。四档12轮Frame中11轮mean改善、12轮p99改善；Small720收益微小，R2mean+2.378µs保留。详[刀2报告](knife2/README.md)。

GPU执行持gpu.lock，game-check、≥100GB及15秒硬超时；COMGR为CPU编译，单独180秒上限并同样持锁/15秒检查。性能期间不做额外目标机I/O。最终默认/off 9组36帧回归通过，正式addon/runtime及双架构80模块完整构建完成。最终产物位于`/home/lmxxf/work/dlss5-gap-phase5-20261023/final-payload`，见[构建回执](final-build/README.md)；默认回归见knife2/final-default-review.json。gpu.lock已释放，无实验进程，磁盘余量279784194048B，见final-state.json。游戏安装与已发布ZIP未改。
