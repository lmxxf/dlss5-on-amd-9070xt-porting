# Phase5刀2独立最终签审：支持Small与AE资格扩展

本审无目标机/GPU操作；已独立核源helper、执行manifest、72Small Gold、84受控AE1、24默认idle、48 AE2/3，以及48槽Small AE0性能。详各independent-*.json/md。

候选只扩host准入，仍按模型、精确processing尺寸与actual tokens绑定，FAST1/MP1、pair availability、layout/KS2/observer等约束保留。AdaptiveVitGroup政策/anchors/阈值/period函数体逐字不变，新两端读同一个gate并成对早退。实际执行两臂均锁已采纳C512 pair模块，未把上一步收益混进本刀。

**数值门通过：**四Small形状三图样72帧配对同；AE1七profiles84帧每帧及完整decision CSV同、实际compute/reuse均出现；默认500ms观察24帧同，未覆盖idle；AE2/3在Original1088/Small720的48帧独立复核同。AE2实际6帧全compute，AE3实际两态，candidate真实route调用、baseline旧路；没有把launch数冒GPU执行次数。合计228帧中24帧是默认idle行为观察，其余为受控数值门。

**性能门：**AE0完整Frame Small900−23.049µs、720−4.663µs、1152−59.642µs、free1088−65.176µs；每档960样本/arm，11/12轮mean改善、12/12轮p99改善。720只有微小收益，R2 mean+2.378µs明确保留，不声称所有档位稳定显著加速，也不把AE0计时当稳态复用帧提速。

据以上证据支持按root授权采纳全部已列scope。未验证的任意尺寸、其它硬件、真实动态游戏性能不由此推导；gfx1200仍只构建身份范围。正式header冻结及最终宿主/default回执另锁，不把候选文件名当最终产物身份。
