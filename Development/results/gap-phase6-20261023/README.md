# 第六阶段最终结果

2026-10-23启动，跨日2026-10-24闭环。**仅Q/K-fragment限三个档位采纳；Up两版与C512 LDS TR均停止。**

## 已合生产

|默认开启Q/K的档位|同批Frame基线ms|候选ms|节省µs|
|---|---:|---:|---:|
|Original900 / 1600×960|6.452625|6.440740|11.885|
|Small900 / 1600×1024|4.253105|4.245412|7.692|
|Small1080 / 1920×1152|5.498272|5.487188|11.083|

三个scope的合并mean/p99改善；不是每轮尾部全胜，Small900最弱，分轮反例完整列在[刀1报告](knife1/README.md)。其余四档调用原V-only核。NR_VIT_QKFRAG优先于公开DLSS5_HIP_VIT_QKFRAG，再默认1；1也不能绕过三shape，0关闭。

正式ISA：生产端Q/K/V为连续b128写，消费者Q入口一条b128、内循环K/V各一条b128；consumer VGPR61→60，producer78、LDS16KiB均不增，QKV容量不变。四token逆排/AV/guards/reuse通过；七档126帧Gold＋84帧AE逐位对账同，七档各3ABBA完成。限定默认/公开关闭/缺exports33帧和三档AE18帧通过。

**1088纯NN只有试验收益：**全范围强制候选8.336081→8.322504ms（−13.577µs），但pooledp99+10.364µs且Frame不稳，最终默认1088不启Q/K；不能把该数写成生产追回，也未重新同批测Mochi。

## 未采纳

Up全开仅把目标核cvt208→192，不是104；总指令3612→3446、W16资源不变。四个W16档位平均均退，pure1088 pooledp99+35.709µs，停止。最后!W8专用版只改两个FP16权重ByteOutput核，其他91核等基线；Small720/free1088新一批分别−5.874/−7.860µs，但Small900mean−0.459µs近零而p99+22.444µs，不通过受影响全范围门，亦停止。未执行修改核的Original1088控制读数不算优化收益。完整新旧代码、Gold、实际路由、84＋48槽计时见[刀2报告](knife2/README.md)。

LDS TR：本地LLVM23 ds_load_tr8仅gfx1250支持；正式COMGR load拼法未声明，提示read拼法后明确要求gfx950。gfx1201均拒，未发射GPU指令；不重跑已有负账的C512_COMPACT_VT。见[B支持排查](knife2/B-result.md)。

## 交付与纪律

最终源码只包含刀1限定QK；生产Up源码/recipe与刀1后基线相同。正式addon/runtime宿主已构建，完整HIP双架构80模块仅4个QK相关模块更新，其余76保持第五阶段，gfx1200仅编译/ELF核验。最终产物在`/home/lmxxf/work/dlss5-gap-phase6-20261023/final-payload`，构建身份见final-build。

GPU持gpu.lock、game-check、≥100GB、15秒硬超时，性能期间无额外目标机I/O；COMGR CPU编译单独180秒上限并同样守门。保留所有失败/负结果/尾反例，不剔样本。游戏安装与已发布ZIP未变。

最终回执已封口：[构建身份](final-build/README.md)、[完整清单](final-build/receipt.json)。GPU锁释放、无实验进程，磁盘余261992218624B，见final-state.json；git diff --check通过。
