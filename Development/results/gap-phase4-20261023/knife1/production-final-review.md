# 第四阶段刀1生产最终独立签审

**已测生产默认与回退门通过。** 本次只本地CPU读取/复算；没有目标机工具、SSH、GPU或正式宿主重编。细项见 `production-final-review.json`。

## 真旧生产→真新默认的纯NN

12槽原始timing.csv独立复算，80 warm＋160 measured，每arm960稳态帧；两臂均无NR环境覆盖。旧基线是phase2最终pure（SHA543a4b6a…82f63，经phase3/current-gap路径使用）及旧模块，新臂是本轮final-pure（SHAa686b013…d3faf）和最终模块，使用同一受控1088/640token输入与flags。

- 旧 **8.341778979 ms → 新8.305513708 ms**，**−36.265271 µs**。
- 三轮均值分别−33.755/−39.896/−35.145µs，三轮p99均改善。
- pooled p99 **−45.520900 µs**。
- 所有首尾RGB SHA均153ac018…647f1，finite/repeat通过；新默认实际producer/consumer各1928次。

这与候选完整Frame的−58.899µs属于不同测量边界，不相加或互换；也不是同Mochi新测，不能用此推算一个新的同批跨实现差距。

## 默认与回退39帧

13任务×3帧逐帧CSV/回执均finite/repeat且与正确同档/同模式参考hash一致：

- 三档默认没有NR或公开开关覆盖，requested1/available1，实际producer/consumer各32次。
- FAST0、冷MP2、AE1双臂均实际0次；AE1保持explicit_ae0=0，未借默认1扩大准入。
- 缺producer测试是900、缺consumer测试是1152；两者available0/实际0次，成对退旧，分别核对应档位Gold，不混分辨率。
- 公开`DLSS5_HIP_VIT_VFRAG=0`实际关闭。`nr-overrides-off`是NR1覆盖公开0，实际32次，验证优先级；不能按名字把它误写成禁用case。

上述为真实GPU门。额外20组helper检查另属CPU：本审先逐字核fixture中InitVitVFragment/VitVFragmentEligible函数体与本次生产源码一致，再本地重新编译运行20组断言全部PASS；涵盖默认/空值/优先级/非法值、逐个缺export、Small/MP状态变化、AE快照、尺寸/layout拒绝。HasFn仍是CPU stub，不冒GPU loader或真实Small/热切覆盖。

## 产物与代码身份

逐文件重新读最终80个HSACO并算SHA，全部匹配final-payload-identity与生产SUMS；各40个gfx1200/gfx1201，同时核ELF flags及内嵌target标记，目标与目录一致。共4文件更新（每架构两个模块），其余76保持；gfx1201两模块仍是已经测过的head-major机器码。gfx1200仅构建/身份核验，不称真机测试。

生产三文件SHA逐项匹配production-identity，final-frame/final-pure本地exe SHA核同。默认请求开启的解析改动没有更换已测设备数学或布局；实际资格仍Original/FAST1/MP1/显式AE0及固定三几何，两端共享同一次local资格决策。

之前候选Frame的900 R3 p99 +4.693µs仍须保留；本次default/pure通过不抹去该反例，也不宣全部玩家配置/动态场景均已验证。没有发现阻断默认落地的证据问题；Small、AE及模式热切的进一步扩展留给后续单独任务，不提前背书。
