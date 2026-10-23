# 当前生产对锁版M：最终独立复核

2026-10-23，仅本地读回与复算，未连接目标机、未运行GPU。

**PASS。** 独立从6份HIP `timing.csv`各160条GPU样本，以及6份M stdout的`GPU total / 160`复算；顺序为3轮N-M-M-N，每臂960帧。没有对已经只有160行的HIP CSV再丢80帧。

| 项目 | 复算ms/frame |
|---|---:|
| 当前生产HIP（仅phase2刀1） | 8.36690828125 |
| 锁旧M d1185d2 | 7.88827708333 |
| 剩余差距 | **0.47863119792** |
| 相对M耗时差 | **6.06762659%** |

三轮差距分别0.471143500、0.477640531、0.487109563ms，全部HIP较慢；与`statistics.json`一致（浮点尾数误差<1e-12）。M只有聚合GPU总时间，重复均值用于加权只是在复算均值，**没有M逐帧分布/p99**。stdout总时间打印到0.001ms，不把其除160产生的小数位当新增计时精度。

身份/正确性核对：

- 当前host本地文件实算SHA与远端identity/source-lock一致：`543a4b6a…f6a82f63`；当前生产header实算SHA=`deb120d5…dcf6a34`，确认是刀1版本。
- 本轮40个gfx1201模块SHA逐项与生产`hip/SHA256SUMS`一致，没有phase3 A/B/C实验模块混入。
- M exe=`8ad3ac1c…f5e8d153`，plan=`93f8ab69…761f2e0`，model=`2b41c888…1f7fbd`；本轮68项M资产清单逐项匹配10-06旧锁账local-manifest对应项，`old-assets-unmodified.json`也通过。68不冒称历史全部70项重核。
- 输入沿用旧锁fixture：processing1088 `3ef42d34…15b49b68`，valid1080 `17b2e09b…e3ae4e2a`；与10-06 input.json/source-lock匹配。HIP命令80warm/160measure/1088；M80warm/160repeats/chunk1、Style1、seed0。6个M槽均打印1920×1088工作区、123派发；不把“one submit”解读为160帧一次提交。
- 18条raw SHA回执全部保持旧值：HIP首末12份均`153ac018…5647f1`，M最终6份均`624d9d1a…31a612`。6个HIP stdout同时有`raw_bitdiff=0 output_finite=1`。这是各自对旧裁判保持，不声称H/M彼此逐位相同。
- HIP日志实际Original71块、MP1、FAST1，compact contract/projection W16与byte流激活；Bridge Pulse调用0。驱动回执仍RX9070XT / 32.0.31007.2048。run.log结束为12槽完成、锁释放。

该差额是本轮共同受控输入、同1088/640token的纯NN口径，不是游戏FPS，也不与M新版ACO公布数字混算。
