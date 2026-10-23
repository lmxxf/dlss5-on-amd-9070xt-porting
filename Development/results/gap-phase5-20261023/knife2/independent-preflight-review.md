# Phase5刀2独立源与正确性预审

仅本地CPU，无目标机工具/SSH/GPU。性能尚未采回，本审不预写收益或采纳结论。

## 已核scope与AE机制

候选只修改VitVFragmentEligible的准入：Small四个精确(W,H,n)元组及Original原三个元组，FAST1/单遍、全层、兼容byte-contract等旧限制保留；KS2检查按n<=400正确涵盖288/448，缺任一新export整对回旧。实际helper函数体与scope-review fixture逐字同，本地重新运行**407组CPU准入检查通过**。HasFn为stub，不冒GPU loader实测。

AdaptiveVitGroup函数体与基线逐字节同，period/threshold/reset/image/token统计、gain、anchor与reuse_finish均未改。adaptive_active只决定Run附加同一adaptive_state指针；新QKV和attention都在入口先看同一reuse_gate。reuse_decide在前、八层顺序launch、reuse_finish在后，gate0计算、gate1两端一起早退。V-fragment只在local norm临时buffer内存在，不进入持久anchors；AE复用帧不会把旧V布局交给新consumer单独计算。

288/448使用实际n，400/640只为export模板上限；producer最后两个inactive wave仍参与shared staging/barriers后才return，head-major地址以实际n/16为stride，不以最大模板容量代替。模式/模型切换与cache管理没有改；MP2/3仍旧路。

GPU ViT两源SHA与phase4已测head-major一致，没有重编新数学。**实际四份执行manifest两臂均锁phase5已采纳C512 pair模块f3f37d03…a32430**，不是phase4旧C512。初始source-identity的phase4 module_root描述源码准备起点；本审JSON另锁执行模块层，避免混用。

## 独立正确性结果

- Small4 profiles×3图样×双arm×3帧=**72帧Gold**：每帧finite、真实effective0.5/31块、配对hash同；candidate新producer/consumer成对实际route数充分，非回落。
- Original3+Small4受控AE1共14jobs/84帧：每个profile最后6实际帧状态都**[0,1,1,1,1,1]**，同时覆盖计算与复用；跨arm每帧hash及完整AE CSV完全相同。route计数是launch，不冒每次都实际计算；baseline AE route0，candidate两端一起被调用。
- 默认500ms idle的Original1088/Small720共4jobs/24帧：确认flags无idle覆盖，实际状态同为[0,1,1,1,1,1]，两臂输出与完整CSV同。只记录本次默认行为，不保证任意宿主停顿的决策相同，不拿其wall作性能数据。

数据由原CSV/日志/hash独立核对，未调用原分析脚本代替这些比较。结果详independent-preflight-review.json和independent-default-idle-review.json。controlled AE1通过不冒AE2/3实测；root已另排那些模式，收到后再核。

## 分析器检查

analyze-correctness逐帧跨arm比SHA，并用AE CSV末N行区分实际compute/reuse，避免把构造预热算实际帧；不强求AE所有帧等首帧。default-idle分析器把wall驱动的状态差异当观察，不自动归因布局，这个边界正确。本轮确实两臂全同，无需任何例外或重测。
