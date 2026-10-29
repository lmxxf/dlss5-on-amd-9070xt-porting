# 刀3：finish→pool 融合，Gold门STOP

主图核验：mapped(Block1)还供后续C32链，Small供Block4；不能跳过它们直接接Down。真正可融合的是Block4 `finish_dcrop_b8d`→`mh_pool_project_c32_b8_out8`，main必须保留供decoder skip。因此候选新增finish_dcrop_pool导出，复用4096B residual LDS，完成同窗口2×2池化和C32→C64投影；不是删除整个mapped输出。

本地fast/norm900全部W16导出≤160VGPR、private=0/spill=0；新融合核134VGPR/4096B LDS。实现与资源/opcode证据见3-kernel.patch、3-host.patch、3-resources.json、3-isa.json。

首个Original1152 baseline Gold通过，候选实际调用融合核后首帧SHA为6317d208253de597b21d35f95c2bd6a18f25298e26a6711fb318f906ae713bc1，与固定Gold不符。invalid=0不能替代逐位一致。立即STOP，不跑剩余Gold或性能，不宣称实现已保持全部舍入/映射等价。局部4×4 token重排、裁剪边界、量化与投影的组合尚无正确性证明，不能把差异归因于WMMA硬件或将候选当性能基线。未经更细粒度中间张量定位，不猜测具体根因。

原始失败stderr/stdout/out.csv/命令/exit与基线Gold收录3/evidence及索引；失败发生在宿主成功结束后的SHA门，故候选exit0并不代表Gold通过。GPU锁finally释放。生产仅保留刀2已验Small1080路由，融合源码/模块不合入。
