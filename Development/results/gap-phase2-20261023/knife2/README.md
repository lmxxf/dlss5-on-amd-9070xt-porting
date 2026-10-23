# 刀2：C32 Horner首级 FMA_MIX，CPU/ISA候选交付

2026-10-23。生产HIP源码未动。隔离副本 `/home/lmxxf/work/dlss5-gap-phase2-20261023/knife2/{mix,mix-alt}/hip`，仅 `wave_owned_c32.inc` 改动。主进程负责Gold及三轮ABBA，本文件只记录CPU和ISA。

## 选择：只测MIX，不测ALT

两候选均用原 `cw_act_gate`，只将第一阶 `fmaf(abs(g),-.055908203125,.447265625)` 换成 `v_fma_mix_f32`。系数0xab28/0x3728为精确half值，`g`仍FP32、输出仍FP32，不改变K累加/舍入/FP8边界。第二候选在奇数分量交换第二阶FMA的乘数。

**MIX与MIX+ALT各自两模块的完整`.text`逐字节相同。** LLVM23把ALT规范化了，没有第二份GPU候选的价值，选更简单MIX。外壳SHA不同由生成源/cuid等引起，不能称机器码不同。

初始把packed literal `0x3728ab28`直接放asm被LLVM MC拒：带half选择的literal操作数被限制为16bit；ACO反汇编显示 `0xab28` 而实际附加字是 `3728AB28`，不能照抄文本生成丢失高half的错误代码。改为`uint 0x3728ab28`走单一`"s"`输入约束，两系数共用一个SGPR。先以tiny probe编译/反汇编验证，再重编完整模块；失败日志保留在各build目录 `*.literal-failed.log`。tiny首次one-shot驱动因缺amdgcn-link失败，改正式分离emit-LLVM→object流程后成功，无GPU。

## ISA筛选

正式LLVM23.1.2配方、`--row-opts --target-feature=-real-true16`，普通和norm900均构建。例：

|核（普通模块）|旧字节→MIX字节|旧指令→MIX指令|VGPR/SGPR/LDS/spill|VOPD/WMMA/wait|
|---|---:|---:|---|---|
|chain_w16|7256→7188|982→981|133/42/4096/0不变|148/54/64不变|
|prefix_b8d_w16|10748→10684|1498→1498|130/48/4096/0不变|186/52/116不变|
|finish_dcrop_b8d_w16|9888→9820|1410→1409|133/54/4096/0不变|166/54/123不变|
|post_b8_w16|10528→10456|1510→1508|131/58/4096/0不变|191/48/110不变|
|up_w16|11460→11388|1691→1689|159/42/4096/0不变|191/56/166不变|

norm900对应热点同样少64–72代码字节，资源/VOPD/WMMA/wait保持。主要变化是第一阶FMA从带32bit literal的12字节指令变为8字节MIX，以及少量scalar常量setup；**没有增加矩阵吞吐/驻留的证据，不预记可观收益**。

128个kernel descriptor（每模块64个）的RSRC1/RSRC2/properties全同，FP round/denorm mode全同。独立审计进一步核LLVM23 `AMDHSAKernelDescriptor.h:224` 与 `AMDGPUMCExpr.cpp:234–245`：15个普通导出/20个norm900导出的RSRC3仅 `INST_PREF_SIZE` 位4起宽8字段减1（整数值−16），对应代码预取少一条cacheline；其它位同。原值留在identity.json，不声称完整descriptor字节不变。所有选中热点零spill。

## 给主进程的唯一候选

路径：`/home/lmxxf/work/dlss5-gap-phase2-20261023/knife2/mix/build/gfx1201/`

- `c32-wave1-fast.hsaco` SHA `a329415246db60aca4fed4c037a4520df13603e975abe5dbac9a4b1e41fbe52c`
- `c32-wave1-fast-norm900.hsaco` SHA `911033ae102b38297129dca2fadf019f28f6fb18a204d9b0d0bde11a96b4fead`

在刀1最终40模块的隔离候选目录仅替换这两个文件，其余38个逐SHA保持；900必须带norm900。host/数学配置/输入均用刀1最终版本，先各档逐帧Gold、再同进程同批三轮ABBA；没有新增运行时开关。GPU操作交主进程统一锁，不在本代理执行。

## 复现材料

- `prepare.py`：从生产源重建两个隔离副本（会覆盖隔离副本，实验运行中勿重跑）。
- `mix.patch` / `mix-alt.patch`：两个完整差异。
- `build.sh mix`：正式本地LLVM23两模块编译；`mix-build.json`保存完整编译命令/源SHA/产物SHA。
- `isa-compare.py` / `isa-comparison.json`：symbol-size裁剪后的全W16导出资源与指令统计。
- `identity.py` / `identity.json`：两候选`.text`等同、descriptor float-mode核验。
- `mix-probe.hip`：最终SGPR约束的最小语法验证。
- `source-review.md`：独立源码/数值审计（由审计代理填写）。

## GPU结题：Gold通过，性能不收（STOP）

完整Frame三档×3种输入×双arm×3帧，54帧Gold全部与旧路逐位相同；普通与norm900两模块一起替换，另外38模块保持原SHA。所有性能使用刀1最终生产资格的同一个final-frame.exe，NET0/edges_only1/240帧弃80，三档各3轮ABBA、每arm960稳态样本。

|档位|baseline ms|MIX ms|差ms|pooled p99差ms|
|---|---:|---:|---:|---:|
|900|6.454713|6.446925|−0.007787|−0.102917|
|1088|8.842617|8.853519|**+0.010902**|**+0.031698**|
|1152|9.146898|9.146424|−0.000474|−0.039741|

1152均值近零，900三轮方向不一致（R2均值/p99都退），1088合并均值和尾延迟退步；不以900 R3较好尾值抵消另一档退步。**没有稳定整网净收益，STOP，不合入生产，不向C64/C128/C512扩散，不追加ALT重复实测。** 代码更短没有兑现成更快；不把这次具体SGPR承载MIX的结果推广为硬件MIX指令永远无用。

初次启动门禁将系统音频驱动 `...kpe_apo_win.../SKR/trace-agent.exe` 误匹配为实验进程，在GPU前停止；检查PID/路径后将实验名匹配收窄为明确名字，原日志 `preflight-process-false-positive.log` 留存，未杀系统进程。其后54Gold与36性能槽完整完成，未刷轮。原CSV/实际命令/首尾hash在evidence/，统计statistics.json。所有GPU守game-check/≥100GB/原子锁/15秒看门狗，最终锁释放。
