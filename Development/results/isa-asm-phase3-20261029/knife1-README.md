# 刀1：ACT成对打包/跨阶段预取，STOP

1A：仅W16 FFN Stage2改为原Hrtz→同一激活→fp8_add0→既有c5d_pack8f。_bin指令684→646、VGPR95不变；非byte-in698→659、119→112，LDS不增/0spill。Stage2转换减半，整核cvt64→56并非整核减半；perm4→0，or3还剩1条地址运算，不伪称全部0。

1B初版完整预取_bin到118VGPR跨原95资源档，CPU门STOP。受限版ByteIn=true保持1A原码，只有非byte-in执行提前cbw/ebw与下一h2预取；非byte-in119→118不退于原生产资源档，但比1A112高，明确是trade，不声称与1A同驻留。两版分别七档42帧Gold同/invalid0、四档3ABBA（每档960/arm）完成。

1A：Orig1088 mean+17.239µs，Small900+7.476µs；Orig900与Small1080有局部正值但有尾反例，不能全收。1B：Orig1088+16.878µs/p99+50.153µs（三mean全退）、Orig900+11.089µs（三mean全退），Small两档不稳。两者全STOP。

额外按任务检查激活前Hrtz的位掩码替代：_bin112/非byte144VGPR资源不合格。工具串联漏加资源条件导致误送Gold，首个candidate被Gold门SHA mismatch拦下并释放锁（停止请求抵达时进程已退出）；此部分不算通过，未跑ABBA。原始输出bd1314...812e不同于0900...0f4a，保留日志。域推断标为被真实GPU反例否定，具体原因未隔离，不冒称数学等价，不泛化替换Hrtz。原Hrtz保留。

生产源码/80模块/SHA不动，无部署、改ZIP/push。日志/CSV留忽略Git的各case/evidence，索引与统计入库。后续资源核验与GPU启动分开，清理按case完成。
