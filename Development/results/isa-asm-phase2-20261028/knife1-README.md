# 刀1 C512 V WMMA正交转置：STOP

基线d78b688e。V单独对调操作数，Q/K不变；V直接片段化LDS写入/读取。坐标穷尽2048元素通过。正式COMGR基线与生产所有函数代码/资源相同，候选仅改变c512_qkv_attention_compact_w16。ds_load_u8 64→0、v_lshl_or22→2、指令2104→1970、VGPR188→187、LDS6144不变/0spill。七档Gold42帧逐位同。

初次四档3ABBA：Original1088 mean−4.978µs/p99−45.139µs且三轮全改善；Original900与Small两档不稳定，不能全开。随后隔离为新增_vfrag export，仅Original1088/gfx1201/MP1/FAST1选择，保留所有旧函数代码不变；新函数与原候选字节代码/资源完全同。双架构编译且ELF目标验证，gfx1200未实机。

最终路由七档Gold42帧、真实缺export回退3帧过；同宿主NR0/1的1088 ABBA均值−5.911µs、p99−7.823µs，但R2 p99+5.393µs。再将NR环境读取移出热路径、构造时缓存，七档Gold及默认路由45帧通过；以原生产宿主/模块对缓存后的真实候选做最后3ABBA，mean+12.284µs（3轮全退）、p99+17.925µs，整刀STOP，不继续刷轮。

源改造与资格分离原型、代码/资源核验、各批原统计全部保留，不以早期局部收益冒充产品收益。不归因全部退步到宿主或代码位置，缺硬件counter拆解。生产源码、80模块与hip/SHA256SUMS保持不变，无游戏安装/ZIP/push。

CPU准备期曾发现：新增导出缺extern C导致mangle、初版grid/counter替换命中错误位置、任务脚本路径/字段错误；均在GPU前核验/哈希门拦下并修正。没有把静默回退视为激活成功。最终七档要求available/calls，1088真调用64次/3帧（含warm），性能candidate3856次，其余0，真实旧模块available0回退过。原始Gold/性能回执在忽略Git的knife1/evidence，身份见knife1-evidence-index.json。临时大产物清理见knife1-cleanup.json。
