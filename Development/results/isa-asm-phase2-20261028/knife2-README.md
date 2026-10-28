# 刀2 ViT WMMA正交转置：全部STOP

基线57c6533e（GPU/宿主仍为原生产）。expand/contract/project三个独立宏位。K16顺序/外层部分和顺序保持，转置残差索引与成对转换同步调整，原producer/consumer接口不改，不加LDS。

正式COMGR结果：expand 490→407指令，b8store32→0/b64store4，VGPR77/0LDS/0spill不变。contract窄读写清零、指令372（短pack368），但VGPR113>112；project窄读写清零、指令325（短pack326），但VGPR115>112。后二者都STOP于CPU资源门，未运行GPU Gold/ABBA，不称数值已验证。仅一次明确的“缩短8个clamp临时值到成对pack”变体，仍超门，不继续扫参。

expand独立七档42帧Gold逐位同/invalid0，模块中仅目标expand函数变。四档3×4ABBA（240弃80，960每arm）结果：Original1088 mean+5.527µs/p99+32.674µs，三轮均值全退；Original900 +3.873/+18.600；Small900 +2.474/−11.967，三轮均值全退；Small1080−10.567/−35.337但R2+4.948/+79.784，不稳定。expand STOP，原数据不抹。

刀2无生产改动、无双架构同步需求，所有候选均未采纳。临时大文件清理，GPU锁释放，未安装/改ZIP/push。详细ISA、统计、源补丁、脚本及evidence-index分别保留。

knife2-address-shape.json是纯逻辑lane地址/块关联模型，不是硬件counter：32条b8变4条b64，但同一16×64输出tile仍1024B；32/64/128B块模型中唯一块数与每指令块关联总数均不减少。它说明opcode减少不等于流量按8倍减少，不将其冒充性能退步的已证唯一原因。
