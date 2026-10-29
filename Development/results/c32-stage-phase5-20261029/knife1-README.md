# 刀1 跨qt权重共享：两候选均STOP

1A将第二循环OutProj两份16B片段及16个FP32残差系数预读一次供4qt使用；与qt无关的指定读取从4次变1次。源码与ISA计数分别见1a-source.patch/1a-isa.json。不能把静态opcode或源码载入次数减少直接当成DRAM字节下降，未测硬件事务。

1A热点147/148VGPR，0spill/private0，LDS4096；编译器occupancy8。1B新增第一循环双qt：Prefix/Chain/Expand/Contract/QKV共享相同权重，独立qt的K/ht/ci顺序、激活/FP8/RTZ不变。全开Prefix164/166、Up172/189超160，未上GPU；一次Prefix生命周期缩短仍164，停止该分支。最终MASK31只启用Chain/Mapped/Finish/Post（151–156VGPR），Prefix/Up保留1A。全部_w16≤160/0spill，norm900配方同验。Runtime module occupancy查询stock/1A/1B四个代表核均16 blocks per multiprocessor，threads32，最大驻留未降；不把LDS说成独立的每SIMD bank或实测动态8wave。

两候选分别七档42帧Gold0bit-diff/invalid0、两模型设备实时钟3ABBA、四档Frame3ABBA（每档960稳态帧/arm）完成。所有slot独立进程且串行。1A C32族Orig−30.45µs/Small−13.33µs，但全局尾门失败；1B C32族Orig+32.46/Small+17.61µs且四档mean三轮均退。

| 候选/档位 | mean差µs | p99差µs |
|---|---:|---:|
| 1a/original1088 | -19.039 | -21.539 |
| 1a/original960 | -18.578 | -44.615 |
| 1a/small1080 | -21.372 | -105.474 |
| 1a/small900 | -11.271 | +14.965 |
| 1b/original1088 | +30.071 | +11.223 |
| 1b/original960 | +6.719 | -4.306 |
| 1b/small1080 | +25.989 | +56.782 |
| 1b/small900 | +12.540 | +12.742 |

1A虽均值四档都改善，Small900尾回退+14.965µs，Orig1088第三轮p99+33.01µs，未达到稳定mean+p99门，不为此追加复杂geometry/双导出路由或刷轮。1B直接STOP，不带入下刀。局部好值与反例均保留，不能从这些数据证明“权重共享无效”或“LLVM天花板”。生产源码/80模块/SHA保持原样。
